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
   - 落点：`src/AsyncNameResolverMan.cc::createPlainBootstrapResolverFactory()`、`src/AbstractCommand.cc::createServiceBindingDiscoveryResolvers()`
   - 行为：`--async-dns-mode=multi --async-dns-server=udp://1.1.1.1,tcp://1.0.0.1,dot://dns.example.org,https://dns.example.org/dns-query` 下，HTTPS/SVCB TYPE65 discovery 创建 DoT/DoH resolver 时，其 secure server 域名 bootstrap 使用配置里的 `udp://`/`tcp://` plain server；没有显式 plain server 时仍用默认 resolver 配置。

3. IPv6 默认改为启用。
   - 落点：`src/OptionHandlerFactory.cc`
   - 行为：`--disable-ipv6` 默认值为 `false`，包括 32-bit MinGW；旧 Windows IPv6 栈不稳时仍可显式设置 `--disable-ipv6=true` 降级。

4. `max-connection-per-server` 上限收紧为 64。
   - 落点：`src/OptionHandlerFactory.cc`
   - 行为：`-x 64` 可接受，`-x 65` 在参数解析阶段失败；默认值保持 `1`。

5. 文档同步。
   - `docs/command-line-help.zh-CN.md` 记录 async DNS disabled 行为、DoT/DoH bootstrap、IPv6 默认值、`-x` 范围、SVCB endpoint ALPN 与失败短期避让。
   - `doc/manual-src/en/aria2c.rst` 同步 `-x` 范围，并修正 HTTPS/SVCB 已可改变 TCP connect target/port 和收窄 ALPN 的说明。

## 已加测试

1. `test/AsyncNameResolverTest.cc`
   - `testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled`：覆盖 `async-dns=false` 时 secure DNS 配置不触发启动期校验失败。
   - `testPlainBootstrapFactoryUsesConfiguredPlainServers`：覆盖 `multi` 显式 `udp://`/`tcp://` plain server 会生成 `PlainBootstrapResolver`。
   - 既有 DoT/DoH/multi 配置校验测试显式设置 `async-dns=true`，避免新早退让测试空跑。

2. `test/OptionHandlerTest.cc`
   - `testFactoryMaxConnectionPerServerLimit`：覆盖 `-x 64` 成功、`-x 65` 失败。

## 当前验证

1. 静态检查：
   - `git diff --check` 通过；只有 Windows 工作区的 LF/CRLF 提示，没有 whitespace error。

2. CI：
   - 上一个已推送提交 `5708d426 Honor HTTPS SVCB endpoint ALPN in TLS handshakes` 的 GitHub Actions build 已通过，run id `27231314848`。
   - 本报告对应的当前第一刀尚未推送；推送前必须完成外部 review，推送后再回填 GitHub Actions run 和 artifact 链接。

3. artifact：
   - 当前阶段没有新的构建 artifact 链接；待本轮 commit 推送并 CI 生成 artifact 后补充。

## 47 条需求状态

### 配置加载（1-5）

未完成。当前第一刀没有改变 conf 自动发现顺序、`--conf-path=aria2.conf` 相对路径、`--no-conf=true`、命令行/conf 冲突优先级或 option 来源追踪。下一阶段需要单独设计 option source 模型，启动日志才能打印“默认值 / conf / 命令行 / 运行期修改”。

### DNS 模式（6-9）

部分完成。第 6 条已先打护栏：`async-dns=false` 不再配置 secure DNS resolver。第 7 条原有 c-ares 行为基本保持：未配 server 用系统 DNS，配 server 用配置 server。第 8、9 条的 DoT/DoH/multi 格式解析已存在，但 secure-only 下载域名解析策略尚未完成。

### multi 规则（10-16）

部分完成。第 11、12 条的 secure server 数值地址/域名 bootstrap 路径已有基础，且本轮让 SVCB discovery 里的 secure server 域名 bootstrap 复用显式 plain server。第 10、13、14、15、16 条还没完整实现：当前下载域名 `multi` 仍是 plain/DoT/DoH 并行 first-success，不是“plain 默认只 bootstrap/fallback”；完整降级链 `secure DNS -> c-ares system DNS -> getaddrinfo -> NAME_RESOLVE_ERROR` 和 network fallback 日志仍需下一阶段实现。

### 配置校验（17-20）

部分完成。`dot://223.6.6.6,180.184.1.1` 中第二项按裸 IP/plain UDP 处理的解析规则已存在；语法错误启动期失败已有基础。服务器不可达的运行期 fallback、全部无效时只走声明 fallback 链路，还需要配合 secure-first multi 和 fallback 日志一起补。

### IPv4 / IPv6（21-25）

部分完成。第 21 条已实现：32-bit MinGW 不再默认 `disable-ipv6=true`。第 22-25 条仍需继续做运行期地址能力、坏 IPv6 快速避让、v4/v6 混合并发下载和同 hostname 连接数约束的端到端验证。现有双栈 first-success 与后台 DNS cache fill 是基础，但不能宣称完整覆盖 RFC 8305 行为。

### HTTPS RR / H2 / H3（26-30）

部分完成。H2/H3 默认关闭已有基础；H3 仍只是能力门。HTTPS RR 查询目前已能跟随 DNS backend，且 SVCB endpoint ALPN 已接入 TLS ALPN 收窄。未启用 H3/无明确能力时“不额外查 HTTPS RR”的精确开关、DoH H2 日志 `DNS: DoH using HTTP/2`、以及 HTTPS RR 在 multi 下完全遵守 plain bootstrap/fallback 规则，还需要继续补。

### 日志可观测性（31-35）

未完成到验收标准。已有一些 network 日志，但还不能完整追踪查询域名、A/AAAA、DNS server IP、协议、bootstrap 来源、fallback 阶段、最终 DNS 地址列表、选中 IP、失败 IP、临时避让 IP、TLS remote IP 和 `Response received` 对应 remote IP。下一阶段应先补统一 DNS/connection log 上下文。

### 下载连接参数（36-39）

部分完成。第 37 条已实现：`max-connection-per-server` 上限为 64。第 38 条已核验并记录：`split=16`、`min-split-size=2M`、`max-connection-per-server=1`。第 36 条和第 39 条需要继续补源码/文档说明：server 按 URL hostname/protocol 维度，不按解析 IP 绕过限制；`split`、`-x`、`min-split-size` 的关系还需要更系统地写进用户文档。

### 兼容性（40-42）

部分完成。本轮 IPv6 默认改为单一基线，secure DNS disabled 模式不会误触发 DoH/DoT 配置失败。无 SSL 构建下 DoT/DoH/multi 仍由现有 option handler 拒绝。新 c-ares API configure 能力探测已有前置工作，但本轮没有新增 API。XP/Win7 上 DoH/DoT 不可用时的完整 fallback 链仍需后续实现与 CI/真实运行验证。

### 验收测试（43-47）

未完成到最终验收。当前只有单元级配置/参数测试和静态检查；还需要补真实功能测试矩阵：纯 DoT、纯 DoH、multi 混合、全 fake DNS、DoH/DoT 域名 bootstrap、`async-dns=false`、conf 自动发现、IPv4 only、IPv6 only、双栈、IPv6 不通、v4/v6 混合并发、H2/H3 默认关闭、HTTPS RR 不多查、DoH H2 开关，以及 network 日志断言。

## 下一阶段计划

1. 配置加载与来源追踪：实现 `cwd aria2.conf -> exe dir aria2.conf -> user default config`，支持 `--conf-path=aria2.conf` 按当前工作目录解析，支持 `--no-conf=true` 完全禁读，并加入命令行/conf 优先级选项。

2. secure-first multi：把下载域名解析拆成 secure resolver 并发优先，plain resolver 默认只用于 DoT/DoH server bootstrap；仅在显式 fallback 阶段进入 plain c-ares/system DNS/getaddrinfo，并打出 network 日志。

3. DNS/连接可观测性：统一记录 DNS query、server、协议、bootstrap/fallback 阶段、A/AAAA、最终地址列表、选中/失败/避让 IP，并让连接成功、TLS 成功和 response received 都能关联 remote IP。

4. 双栈下载：继续实现/验证 v4/v6 混合并发、坏 IPv6 快速避让、同 hostname 连接数限制不被不同 IP 绕过。

5. HTTPS RR/H2/H3 边界：默认不额外查询不需要的 HTTPS RR；补 DoH over H2 日志；H3 继续保持默认关闭和 unsupported 快速拒绝。

6. 验收报告：CI 通过后补 GitHub Actions run、artifact 链接和实际功能测试结果。
