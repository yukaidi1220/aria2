# async DNS 需求阶段报告（2026-06-10）

## 目标

本轮目标是把新增 47 条需求拆成可落地的工程阶段，先交付一刀低风险基础改动，并把尚未完成的行为明确写出来，避免把“规划目标”误当成“已经实现”。

总方向是：DoH/DoT 优先，plain DNS 只用于 bootstrap 或明确 fallback；日志要能解释 DNS server、协议、bootstrap 来源、fallback 阶段、remote IP 和 CUID 关联；XP/Win7/无 SSL/nghttp2/ngtcp2/nghttp3 构建不崩溃， unsupported 能力要在配置或运行边界清楚失败。

## 本轮已实现

1. `async-dns=false` 时跳过 secure DNS resolver 配置。
   - 落点：`src/AsyncNameResolverMan.cc::configureAsyncNameResolverMan()`
   - 行为：`--async-dns=false --async-dns-mode=doh --async-dns-server=` 不再因为 DoH server 为空在启动期失败；下载域名解析回到 hosts/cache 后的同步 `getaddrinfo` 路径。
   - 日志：network 级别打印 `DNS: async DNS disabled; secure DNS resolver config is ignored`。

2. `multi` 中 DoT/DoH server 域名 bootstrap 优先复用显式 plain DNS。
   - 落点：`src/AsyncNameResolverMan.cc::createPlainBootstrapResolverFactory()`、`src/AbstractCommand.cc::createHttpsServiceBindingDiscoveryPhases()`、`src/AbstractCommand.cc::createServiceBindingDiscoveryPhaseEntries()`
   - 行为：`--async-dns-mode=multi --async-dns-server=udp://1.1.1.1,tcp://1.0.0.1,dot://dns.example.org,https://dns.example.org/dns-query` 下，下载域名 A/AAAA 主解析和 HTTPS/SVCB TYPE65 discovery 的 secure server 域名 bootstrap 都优先使用配置里的 `udp://`/`tcp://` plain server；没有显式 plain server 时仍用默认 resolver 配置。
   - 行为：HTTPS/SVCB TYPE65 discovery 已改为阶段式：有 DoT/DoH 时先只启动 secure resolver；secure 阶段全失败或超时后才进入显式 plain DNS；最后进入系统 c-ares。安全 DNS 返回空 HTTPS RR 时会缓存短期空结果并终止，不再继续查 plain。

3. IPv6 默认改为启用。
   - 落点：`src/OptionHandlerFactory.cc`
   - 行为：`--disable-ipv6` 默认值为 `false`，包括 32-bit MinGW；旧 Windows IPv6 栈不稳时仍可显式设置 `--disable-ipv6=true` 降级。

4. `max-connection-per-server` 上限收紧为 64。
   - 落点：`src/OptionHandlerFactory.cc`
   - 行为：`-x 64` 可接受，`-x 65` 在参数解析阶段失败；默认值保持 `1`。

5. 文档同步。
   - `docs/command-line-help.zh-CN.md` 记录 async DNS disabled 行为、DoT/DoH bootstrap、IPv6 默认值、`-x` 范围、SVCB endpoint ALPN 与失败短期避让。
   - `doc/manual-src/en/aria2c.rst` 同步 `-x` 范围，并修正 HTTPS/SVCB 已可改变 TCP connect target/port 和收窄 ALPN 的说明。

6. 配置加载第二阶段基础能力。
   - 落点：`src/option_processing.cc`、`src/OptionHandlerFactory.cc`、`src/Context.cc`
   - 行为：未显式 `--conf-path` 且未启用 `--no-conf=true` 时，按当前工作目录 `aria2.conf`、程序所在目录 `aria2.conf`、用户目录默认配置的顺序查找。
   - 行为：新增 `--conf-precedence=command|conf`，默认 `command` 保持命令行优先；显式或配置为 `conf` 时，配置文件可覆盖重复的命令行选项。
   - 行为：启动日志打印 DNS、IPv6、H2/H3、分片/连接数和配置加载相关关键参数的最终值及来源。

7. 下载域名、UDP tracker、DHT entry point 的 DNS fallback 骨架。
   - 落点：`src/AsyncNameResolverMan.cc`、`src/AbstractCommand.cc`、`src/NameResolveCommand.cc`、`src/DHTEntryPointNameResolveCommand.cc`
   - 行为：`multi` 有 DoT/DoH secure server 时，下载域名主阶段只跑 secure resolver；secure 全失败后才进入显式 `udp://`/`tcp://` plain DNS；显式 plain 也失败或未配置时进入系统 c-ares。
   - 行为：纯 DoT/DoH 全失败后进入系统 c-ares；显式 c-ares server 全失败后也进入系统 c-ares。系统 c-ares 仍失败后，HTTP/FTP 下载路径最后走同步 `getaddrinfo`；同步也失败时保持 `NAME_RESOLVE_ERROR`。UDP tracker 和 DHT entry point 沿用各自既有的“记录失败并跳过该入口”语义。
   - 日志：network 级别打印从 secure/explicit plain 降级到 explicit plain/system c-ares，以及 async DNS 最终失败后 fallback 到 `getaddrinfo` 的原因。

8. 外部评审后的修补。
   - 修正 `multi` 只有 plain server 时的 fallback 日志，不再把 explicit plain DNS 误报成 secure DNS。
   - `AsyncNameResolverMan::reset()` 清理 hostname 和 resolver phase，避免复用对象时残留旧 fallback 状态。
   - 测试侧把 `createResolvers()` 收窄为 protected 观测口，并补真实 `multi` 主阶段 resolver 类型断言，避免只靠 mock 计数造成假测。

9. 日志可观测性第一阶段。
   - 落点：`src/AbstractCommand.cc`、`src/HttpConnection.cc`、`src/SocketCore.cc`
   - 行为：DNS 选址完成后打印统一 network 日志，包含 `CUID`、hostname、port、来源阶段、最终候选地址类型、最终选中 IP、地址族和候选地址列表。
   - 行为：HTTP response status 的 network 日志追加实际 remote IP:port，便于把 `Response received` 和本次请求连接端点关联起来。
   - 行为：TLS 握手成功后打印实际 remote IP:port、SNI、证书校验 host、TLS version 和 ALPN。无 SSL 构建不受影响；TLS 日志复用握手成功路径已有的 peer endpoint，HTTP response 日志读取 remote endpoint 失败时不会中断下载。

10. 双栈 backup 连接避让第一阶段。
    - 落点：`src/InitiateConnectionCommand.cc`
    - 行为：主连接使用 IPv4 时，backup 连接只选择公网 IPv6 地址；如果候选里只有 ULA、link-local、site-local、loopback 等非公网 IPv6，则不再发起 IPv6 backup，避免内网 IPv6 存在但公网不可达时额外拖慢首连。
    - 行为：主连接使用 IPv6 时，IPv4 backup 保持原有选择逻辑。

## 已加测试

1. `test/AsyncNameResolverTest.cc`
   - `testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled`：覆盖 `async-dns=false` 时 secure DNS 配置不触发启动期校验失败。
   - `testPlainBootstrapFactoryUsesConfiguredPlainServers`：覆盖 `multi` 显式 `udp://`/`tcp://` plain server 会生成 `PlainBootstrapResolver`。
   - `testStartAsyncCaresWithExplicitServerFallsBackToSystem`：覆盖显式 c-ares server 失败后进入系统 c-ares。
   - `testStartAsyncDotFallsBackToSystem`：覆盖 DoT 失败后进入系统 c-ares。
   - `testStartAsyncMultiStartsSecureBackendsBeforePlainFallback` / `testStartAsyncMultiFallsBackToExplicitPlainThenSystem`：覆盖 `multi` 下载域名解析的 secure-first 和显式 plain -> 系统 c-ares fallback 阶段。
   - `testStartAsyncMultiStartsSecureBackendsBeforePlainFallback` 额外断言真实 `createResolvers()` 主阶段只创建 DoT/DoH resolver，不提前创建 plain resolver。
   - 既有 DoT/DoH/multi 配置校验测试显式设置 `async-dns=true`，避免新早退让测试空跑。

2. `test/AbstractCommandTest.cc`
   - `testCreateHttpsServiceBindingDiscoveryPhasesCaresSystem` / `testCreateHttpsServiceBindingDiscoveryPhasesCaresExplicit`：覆盖 HTTPS RR discovery 在 c-ares 模式下的系统 DNS 和显式 c-ares -> 系统 c-ares 阶段。
   - `testCreateHttpsServiceBindingDiscoveryPhasesDot` / `testCreateHttpsServiceBindingDiscoveryPhasesDoh`：覆盖纯 DoT/DoH 的 secure -> 系统 c-ares 阶段。
   - `testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureFirst` / `testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureOnly` / `testCreateHttpsServiceBindingDiscoveryPhasesMultiPlainOnly` / `testCreateHttpsServiceBindingDiscoveryPhasesMultiSystemOnly`：覆盖 `multi` 下 secure-first、secure-only、plain-only 和空 server 的 HTTPS RR discovery 阶段顺序。

3. `test/OptionHandlerTest.cc`
   - `testFactoryMaxConnectionPerServerLimit`：覆盖 `-x 64` 成功、`-x 65` 失败。

4. `test/InitiateConnectionCommandTest.cc`
   - `testSelectBackupIPAddressSkipsScopedIPv6Backup`：覆盖 IPv4 主连接时不会把 ULA/link-local IPv6 选作 backup 连接地址。

## 当前验证

1. 静态检查：
   - `git diff --check` 通过；只有 Windows 工作区的 LF/CRLF 提示，没有 whitespace error。
   - HTTPS RR/TYPE65 staged fallback 切片在提交前已通过上述静态检查；本机缺少 `make`/`g++`/`clang++`/`cl`/`cmake`，尚未完成本地编译或单元测试。
   - 日志可观测性第一阶段切片已通过 `git diff --check -- src/AbstractCommand.cc src/HttpConnection.cc src/SocketCore.cc` 和外部 review；本机仍缺少 C++ 构建工具，GitHub Actions run `27242784981` 正在验证。
   - 双栈 backup 避让第一阶段切片已通过 `git diff --check -- src/InitiateConnectionCommand.cc test/InitiateConnectionCommandTest.cc docs/reports/2026-06-10-async-dns-requirements-progress.md` 和外部 review；本机仍缺少 C++ 构建工具，等待 GitHub Actions 编译验证。

2. CI：
   - 前置提交 `2997acde Align async DNS bootstrap and connection limits` 的 GitHub Actions build 已通过，run id `27233170820`。
   - 前置提交 `fed40f3e Support config discovery precedence and startup option logs` 的 GitHub Actions build 已通过，run id `27235349508`。
   - 本报告对应的 secure-first fallback 切片已提交为 `0e483039 Implement secure-first async DNS fallback chain`，GitHub Actions build 已通过，run id `27238331842`，耗时 `8m55s`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27238331842
   - HTTPS RR/TYPE65 staged fallback 切片已提交为 `b41ada1a Stage HTTPS RR discovery DNS fallback`，GitHub Actions build 已通过，run id `27240908069`，耗时 `8m20s`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27240908069

3. artifact：
   - `0e483039` artifacts：
     - `aria2-x86_64-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27238331842/artifacts/7521271861
     - `aria2-i686-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27238331842/artifacts/7521230494
     - artifact 过期时间：`2026-09-07T21:54:20Z`。
   - `b41ada1a` artifacts：
     - `aria2-x86_64-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27240908069/artifacts/7522240987
     - `aria2-i686-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27240908069/artifacts/7522203553
     - artifact 过期时间：`2026-09-07T22:50:05Z`。

## 47 条需求状态

### 配置加载（1-5）

部分完成。第 1 条自动发现顺序已改为当前工作目录 `aria2.conf` -> 程序所在目录 `aria2.conf` -> 用户目录默认配置；第 2 条显式 `--conf-path=aria2.conf` 继续按当前工作目录解析相对路径；第 3 条 `--no-conf=true` 会跳过所有配置文件；第 4 条新增 `--conf-precedence=command|conf`，默认命令行优先，也可显式配置为 conf 优先。第 5 条已先覆盖关键参数启动日志和来源，尚未做全量 option 来源追踪或运行期修改日志。

### DNS 模式（6-9）

部分完成。第 6 条已先打护栏：`async-dns=false` 不再配置 secure DNS resolver。第 7 条原有 c-ares 行为基本保持：未配 server 用系统 DNS，配 server 用配置 server；本轮又补了“显式 c-ares server 全失败后 fallback 到系统 c-ares”。第 8、9 条的 DoT/DoH/multi 格式解析已存在；下载域名、UDP tracker、DHT entry point 的 A/AAAA 主解析已开始执行 secure-first/fallback 骨架，但真实网络验收和更细日志断言还没完成。

### multi 规则（10-16）

部分完成。第 11、12 条的 secure server 数值地址/域名 bootstrap 路径已有基础，且本轮让 SVCB discovery 里的 secure server 域名 bootstrap 复用显式 plain server。第 10、13、15、16 条已有主链路骨架：`multi` 下载域名先并发 DoT/DoH，secure 全失败后再用显式 plain DNS，然后系统 c-ares，最后同步 `getaddrinfo`，失败才 `NAME_RESOLVE_ERROR`；HTTPS RR/TYPE65 discovery 也已改成 secure -> 显式 plain -> 系统 c-ares 的后台阶段式 fallback，不再把 plain 和 secure resolver 同阶段并行启动。这些降级会打 network 日志。第 14 条“失败 DNS server 只影响自己”仍主要依赖各 resolver 现有 server retry 逻辑，还需要真实网络/fake server 验收。

### 配置校验（17-20）

部分完成。`dot://223.6.6.6,180.184.1.1` 中第二项按裸 IP/plain UDP 处理的解析规则已存在；语法错误启动期失败已有基础。服务器不可达的运行期 fallback 已有主链路骨架，全部无效时最终会走到同步 `getaddrinfo` 后失败；还需要用 fake DNS/真实网络把“没有未声明路径偷跑”和日志断言补齐。

### IPv4 / IPv6（21-25）

部分完成。第 21 条已实现：32-bit MinGW 不再默认 `disable-ipv6=true`。第 23 条先补了 backup 连接避让：IPv4 主连时不会把非公网 IPv6 地址选作 backup，减少内网 IPv6 存在但公网不可达时的额外拖慢。第 22、24、25 条仍需继续做运行期地址能力、坏 IPv6 快速避让、v4/v6 混合并发下载和同 hostname 连接数约束的端到端验证。现有双栈 first-success 与后台 DNS cache fill 是基础，但不能宣称完整覆盖 RFC 8305 行为。

### HTTPS RR / H2 / H3（26-30）

部分完成。H2/H3 默认关闭已有基础；H3 仍只是能力门。SVCB endpoint ALPN 已接入 TLS ALPN 收窄。HTTPS RR/TYPE65 discovery 当前能按后端创建 resolver，DoT/DoH 域名 server 的 bootstrap 可复用显式 plain server，并且 `multi` 下已改为 secure-first 阶段式 fallback；该 discovery 仍是后台任务，不阻塞首连。未启用 H3/无明确能力时“不额外查 HTTPS RR”的精确开关、DoH H2 日志 `DNS: DoH using HTTP/2`，以及真实网络/fake server 验收还需要继续补。

### 日志可观测性（31-35）

部分完成。第一阶段已补 DNS 最终选中日志、HTTP response remote IP:port 和 TLS remote/SNI/verify/version/ALPN 日志，能把下载请求的 CUID、hostname、候选地址、最终选中 IP、TLS 建连端点和响应端点串起来。尚未完整覆盖 DNS server IP、协议、bootstrap 来源、每个 fallback 阶段的 resolver 级 server 明细、失败 IP、临时避让 IP、抓包级 v4/v6 对账断言；这些仍需继续在 resolver 和连接失败路径补细日志与测试。

### 下载连接参数（36-39）

部分完成。第 37 条已实现：`max-connection-per-server` 上限为 64。第 38 条已核验并记录：`split=16`、`min-split-size=2M`、`max-connection-per-server=1`。第 36 条和第 39 条需要继续补源码/文档说明：server 按 URL hostname/protocol 维度，不按解析 IP 绕过限制；`split`、`-x`、`min-split-size` 的关系还需要更系统地写进用户文档。

### 兼容性（40-42）

部分完成。本轮 IPv6 默认改为单一基线，secure DNS disabled 模式不会误触发 DoH/DoT 配置失败。无 SSL 构建下 DoT/DoH/multi 仍由现有 option handler 拒绝。新 c-ares API configure 能力探测已有前置工作，但本轮没有新增 API。XP/Win7 上 DoH/DoT 不可用时的完整 fallback 链仍需后续实现与 CI/真实运行验证。

### 验收测试（43-47）

未完成到最终验收。当前只有单元级配置/参数测试和静态检查；还需要补真实功能测试矩阵：纯 DoT、纯 DoH、multi 混合、全 fake DNS、DoH/DoT 域名 bootstrap、`async-dns=false`、conf 自动发现、IPv4 only、IPv6 only、双栈、IPv6 不通、v4/v6 混合并发、H2/H3 默认关闭、HTTPS RR 不多查、DoH H2 开关，以及 network 日志断言。

## 下一阶段计划

1. 配置加载与来源追踪：继续补全量 option 来源追踪、运行期修改日志和更多启动日志断言；当前已完成基础配置查找顺序、`--no-conf=true`、`--conf-precedence=command|conf` 和关键参数来源日志。

2. secure-first multi 验收：下载域名主解析骨架已改为 secure resolver 优先，plain resolver 默认只用于 DoT/DoH server bootstrap 或失败后的 fallback；下一步要用 fake DNS/真实网络验证 secure、显式 plain、系统 c-ares、getaddrinfo 每一层的日志和失败边界。

3. DNS/连接可观测性：统一记录 DNS query、server、协议、bootstrap/fallback 阶段、A/AAAA、最终地址列表、选中/失败/避让 IP，并让连接成功、TLS 成功和 response received 都能关联 remote IP。
   - 已完成第一刀：DNS selected、HTTP response remote、TLS connected remote/SNI/ALPN。下一刀继续补 resolver server/protocol/bootstrap 明细、失败 IP 和临时避让 IP。

4. 双栈下载：继续实现/验证 v4/v6 混合并发、坏 IPv6 快速避让、同 hostname 连接数限制不被不同 IP 绕过。
   - 已完成第一刀：IPv4 主连时不再把非公网 IPv6 地址选作 backup。下一刀继续评估本机公网 IPv6 能力判断、family penalty 和同 host 并发限制测试。

5. HTTPS RR/H2/H3 边界：TYPE65 discovery 已切到 secure-first 阶段式 fallback；下一步默认不额外查询不需要的 HTTPS RR，补 DoH over H2 日志；H3 继续保持默认关闭和 unsupported 快速拒绝。

6. 验收报告：CI 通过后补 GitHub Actions run、artifact 链接和实际功能测试结果。
