# async DNS 需求阶段报告（2026-06-10）

## 目标

本轮目标是把新增 47 条需求拆成可落地的工程阶段，先交付一刀低风险基础改动，并把尚未完成的行为明确写出来，避免把“规划目标”误当成“已经实现”。

总方向是：DoH/DoT 优先，plain DNS 只用于 bootstrap 或明确 fallback；日志要能解释 DNS server、协议、bootstrap 来源、fallback 阶段、remote IP 和 CUID 关联；XP/Win7/无 SSL/nghttp2/ngtcp2/nghttp3 构建不崩溃， unsupported 能力要在配置或运行边界清楚失败。

## 阶段性交付修复（04:54 之后）

本节只记录测试暴露后必须尽快修好的阶段性问题。47 条总需求里仍未闭环的项目继续保留在后文“47 条需求状态”和“下一阶段计划”，本轮不继续扩大战线。

1. disabled async DNS 配置覆盖已补强：`2c192561 Expand disabled async DNS config coverage`
   - 落点：`test/AsyncNameResolverTest.cc`、`docs/reports/2026-06-10-async-dns-requirements-progress.md`
   - 行为：`testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled()` 从单一 DoH 空 server 扩展为三组本应在 `async-dns=true` 下失败的配置：DoT 空 server、DoH 空 server、`multi` 裸域名 plain server。`async-dns=false` 时这些配置不再触发 secure DNS 校验失败。
   - 外审：Volta 只读复审无阻断。
   - CI：GitHub Actions run `27254138777` 已通过。

2. DoH HTTP/2 transport 门控日志测试补强：`cf425451 Expose remote endpoint in HTTP logs`
   - 落点：`test/AsyncNameResolverTest.cc`
   - 行为：新增 `testConfigureDohHttp2AffectsLoggedTransport()`，断言 `--enable-http2=true --enable-http-pipelining=false` 时 DoH query plan 记录 `transport=https-h1-or-h2`（无 nghttp2 构建记录 `https-h1`），而 `--enable-http-pipelining=true` 时回落 `transport=https-h1`。
   - 边界：测试显式固定 `resolverMan.setIPv4(true)`、`resolverMan.setIPv6(false)`，避免 CI 主机 IPv4/IPv6 探测结果影响日志断言。
   - 验证：Volta、Euclid 外审无阻断；GitHub Actions run `27255420287` 已通过。

3. HTTPS 建连成功日志补 remote IP：`cf425451 Expose remote endpoint in HTTP logs`
   - 落点：`src/HttpRequestCommand.cc`
   - 行为：用户可见的 network 日志 `HTTPS connection to ... established` 追加 `remote=<ip:port>`；IPv6 使用 `[addr]:port` 格式。
   - 目的：抓包看到连接 IP 时，可以直接从 HTTPS 建连成功日志里对上本次 CUID 和 remote endpoint，不再只看 hostname。

4. `Response received` 日志补 remote IP：`cf425451 Expose remote endpoint in HTTP logs`
   - 落点：`src/HttpConnection.cc`
   - 行为：info 级 `Response received` 由 `CUID#... - Response received:` 改为 `CUID#... - Response received from <ip:port>:`；原 network 级 `HTTP: ... Response status ... remote=...` 继续保留，并复用同一个 remote endpoint 字符串。
   - 目的：用户常看的 `Response received` 日志本身即可关联实际 remote IP，不必再额外找相邻 network 日志。

## 复查补强（上一轮已提交）

复查发现源码行为已补，但部分关键日志和 gate 没被回归测试钉死。本节只补稳定单元测试和可解释报告，不继续扩展新功能。

1. HTTP/HTTPS remote endpoint 日志格式测试。
   - 落点：`src/HttpLog.cc`、`src/HttpLog.h`、`src/HttpRequestCommand.cc`、`src/HttpConnection.cc`、`test/HttpRequestTest.cc`、`test/HttpResponseTest.cc`
   - 行为：把 HTTPS 建连成功、`Response received`、`Response status` 三类日志格式集中到 `HttpLog` helper；生产代码和单测使用同一格式入口。
   - 测试：`testHttpsConnectionLogIncludesRemoteEndpoint()` 断言 IPv4/IPv6 remote endpoint 和 `HTTPS connection ... established remote=...`；`testResponseLogsIncludeRemoteEndpoint()` 断言 `Response received from ...` 和 `Response status ... remote=...`。

2. DoH 实际 HTTP/2 协商日志测试。
   - 落点：`test/AsyncDohNameResolverTest.cc`
   - 行为：基于 fake DoH transport 模拟 ALPN 选择 `h2`，走到 DoH 写请求路径后抓 network log，断言存在 `DNS: DoH using HTTP/2`。
   - 边界：测试仅在 `HAVE_LIBNGHTTP2` 下启用，不影响无 nghttp2 构建。

3. 启动参数来源日志测试。
   - 落点：`src/StartupOptionLog.cc`、`src/StartupOptionLog.h`、`src/Context.cc`、`test/OptionProcessingTest.cc`
   - 行为：把 startup option source 判定和 `Option: key=value (source=...)` 格式拆成可测 helper；生产启动日志仍走同一逻辑。
   - 测试：`testStartupOptionLogSources()` 覆盖 command/default/conf/runtime 来源和 `--conf-precedence=conf` 下 command/conf source 名称反转。

4. HTTPS RR / H3 默认不多查测试。
   - 落点：`src/AbstractCommand.cc`、`src/AbstractCommand.h`、`test/AbstractCommandTest.cc`
   - 行为：把 `maybeStartHttpsServiceBindingDiscovery()` 的前置 gate 拆成 `shouldStartHttpsServiceBindingDiscovery()`，测试能直接断言默认关闭、cache 已存在、正在解析、`async-dns=false` 时不会启动 TYPE65 discovery。
   - 测试：`testCreateHttpsServiceBindingDiscoveryPhasesDisabledByHttp3Only()` 断言仅开启 `enable-http3=true` 但未显式开启 `enable-https-rr=true` 时仍不创建 HTTPS RR discovery phase，也不会启动 discovery。

5. v4/v6 端到端验收边界。
   - 只读复查结论：现有单测能证明 A/AAAA 并发返回、双栈地址选择、非 global IPv6 backup 避让、地址族失败惩罚和同 hostname 连接数限制；但测试入口默认 `SocketCore::setProtocolFamily(AF_INET)`，不能稳定证明“同一下载真实同时跑 v4/v6 请求”或“公网 IPv6 不通后端到端快速切 v4”。
   - 后续验收建议：需要新增 fake resolver + fake socket/connection factory 注入点，模拟 IPv6 立即失败、IPv4 成功，再断言 family penalty、下一连接目标、hostname 维度连接数和 remote IP 日志；不建议依赖 CI/本机真实 IPv6 环境。

## 继续补强（本轮新增）

本节只处理上一轮复查后仍缺的稳定自动化覆盖，不把依赖真实网络、真实 IPv6 或抓包环境的功能验收伪装成 CI 单测。

1. 全 fake DNS / fallback 链路耗尽测试。
   - 落点：`test/AsyncNameResolverTest.cc`
   - 行为：不在 `startFallback()` 的最终 false 路径额外打印“一路耗尽”日志，避免外层轮询重复刷屏；最终进入 `getaddrinfo` fallback 或失败仍由调用方日志记录。
   - 测试：`testStartAsyncMultiAllFakeDnsExhaustsFallbacks()` 用 mock resolver 跑完 `multi` 的 secure DNS -> explicit plain DNS -> system c-ares DNS 三段全失败，断言两段 fallback 日志、query plan 日志和 `createResolvers()` 调用数停在 3 次，证明没有继续启动第四条未声明 async DNS 路径。

2. `multi` 未声明阶段负断言。
   - 落点：`test/AsyncNameResolverTest.cc`
   - 行为：`testStartAsyncMultiSecureOnlySkipsPlainFallback()` 覆盖只有 DoT/DoH、没有 plain server 时，secure DNS 失败后直接进入 system c-ares fallback，日志中不出现 `phase=explicit-plain-fallback`。
   - 行为：`testStartAsyncMultiPlainOnlySkipsSecureBackends()` 覆盖只有 plain DNS、没有 DoT/DoH 时，primary 阶段 query plan 只记录 UDP/TCP c-ares，日志中不出现 `backend=DoT` 或 `backend=DoH`。

3. DoH endpoint 运行期成败日志测试。
   - 落点：`src/AsyncDohNameResolver.cc`、`test/AsyncDohNameResolverTest.cc`
   - 行为：`failCurrentEndpointOrServer()` 对最后一个 endpoint 失败也打印 `DNS: DoH endpoint ... failed: ...`，避免只看到 server failed。`testRetryNextServerOnConnectError()` 在 fake DoH transport 的第一个 server 连接失败、第二个 server 成功路径上抓 network log，断言 `DNS: DoH connecting to ... via ...`、`DNS: DoH endpoint ... failed: DoH connection failed: ...`、`DNS: DoH server ... failed: DoH connection failed: ...` 和下一 server 连接日志。
   - 边界：该测试不依赖真实网络，不验证抓包级 v4/v6 对账；这些仍归入后续功能验收。

4. DoT endpoint 运行期成败日志测试。
   - 落点：`src/AsyncDotNameResolver.cc`、`test/AsyncDotNameResolverTest.cc`
   - 行为：`failCurrentEndpointOrServer()` 对最后一个 endpoint 失败也打印 `DNS: DoT endpoint ... failed: ...`，避免只看到 server failed。`testRetryNextServerOnConnectError()` 在 fake DoT transport 的第一个 server 连接失败、第二个 server 成功路径上抓 network log，断言 `DNS: DoT connecting to ... via ...`、`DNS: DoT endpoint ... failed: DoT connection failed: ...`、`DNS: DoT server ... failed: DoT connection failed: ...` 和下一 server 连接日志。
   - 边界：该测试不依赖真实网络，不验证真实 DoT server、真实 TLS 栈或抓包级 v4/v6 对账。

5. 失败 IP / 临时避让地址日志测试。
   - 落点：`src/DownloadEngine.cc`、`test/DownloadEngineTest.cc`
   - 行为：`DownloadEngine::markBadIPAddress()` 统一打印 network 日志 `DNS: marking bad address host=... port=... ip=... family=...`，覆盖连接失败、TLS handshake fallback 等复用该入口的路径；HTTPS RR endpoint 临时避让仍保留独立 `HTTPS RR: temporarily avoiding failed endpoint ...` 日志。
   - 测试：`testMarkBadIPAddressLogsAddressFamily()` 不依赖真实网络，断言 IPv4 和 IPv6 地址被标记为 bad 时日志都包含 host、port、ip 和 family。
   - 边界：该测试只覆盖 bad-address 统一入口；真实 artifact 运行日志、CUID 上下文和抓包级 v4/v6 对账仍需后续功能验收。

6. v4/v6 混合并发端到端边界复核。
   - 复核结论：当前稳定单测仍只能覆盖选择、backup delay、非 global IPv6 避让和地址族惩罚等策略层；真正的“同一下载同时跑 IPv4/IPv6 请求”需要给主连接和 backup 连接引入 fake socket/connection factory 或测试可观察入口。
   - 后续实现建议：不要依赖 CI runner 真实 IPv6；先加最小连接工厂注入，让测试可模拟 IPv6 立即失败、IPv4 backup 成功，再断言 request remote、bad IP、family penalty、hostname 维度连接上限和相关日志。

## 最新增量（03:40-04:06）

1. 配置发现顺序补强：`0ab1da12 Test config discovery candidate order`
   - 落点：`src/option_processing.cc`、`test/OptionProcessingTest.cc`
   - 行为：抽出 `createDefaultConfigFileCandidates()` 纯函数，测试覆盖当前目录、程序目录、用户配置的候选顺序和去重；非 MinGW 测试覆盖程序目录 `aria2.conf` 自动发现。MinGW 下不伪造 `GetModuleFileNameW()`，避免把测试写成假覆盖。
   - 外审：Volta 两轮只读复审，结论无阻断。
   - CI：GitHub Actions run `27251537049` 已通过。

2. HTTPS RR discovery 绑定 `async-dns=true`：`1697ce06 Gate HTTPS RR discovery on async DNS`
   - 落点：`src/AbstractCommand.cc`、`test/AbstractCommandTest.cc`
   - 行为：`createHttpsServiceBindingDiscoveryPhases()` 自身也检查 `PREF_ASYNC_DNS`，所以 `--async-dns=false --enable-https-rr=true` 不会生成 HTTPS/SVCB TYPE65 discovery phase。
   - 测试：新增 `testCreateHttpsServiceBindingDiscoveryPhasesDisabledByAsyncDns`；旧 positive phase tests 显式设置 `PREF_ASYNC_DNS=true`。
   - 外审：Volta 只读复审无阻断。
   - CI：GitHub Actions run `27251874742` 已通过。

3. HTTPS RR/SVCB cache 消费也绑定 `async-dns=true`：`c175661a Disable cached HTTPS RR when async DNS is off`
   - 落点：`src/AbstractCommand.cc`、`src/HttpInitiateConnectionCommand.cc`、`test/AbstractCommandTest.cc`、`test/HttpInitiateConnectionCommandTest.cc`
   - 行为：`async-dns=false` 时不仅不会发起新的 TYPE65 查询，也不会消费已有 HTTPS RR/SVCB cache 改写 connect target/port 或追加 address hints。
   - 测试：覆盖 `async-dns=false + enable-https-rr=true + 已有 cache` 仍使用 origin；覆盖 `async-dns=false + enable-https-rr=true + doh 空 server` phases 为空且不触发 secure DNS 配置校验。
   - 外审：Volta 第一轮抓到旧 hints 测试未显式开关会失败；修复后第二轮复审无阻断。
   - CI：GitHub Actions run `27252444409` 已通过。

4. 47 条需求评审结论
   - Euclid 只读评审结论：需求方向合理，但 fallback 判定、multi secure/plain 边界、双栈快失败策略、兼容矩阵和日志字段契约仍是阶段性阻断；不能宣称 47 条已完成。
   - Lagrange 只读探索结论：`async-dns=false` 主解析路径已经回到同步 `getaddrinfo`，但 HTTPS RR cache 消费点原本只看 `enable-https-rr`，本轮已按该 finding 修补。

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

11. HTTPS/SVCB TYPE65 discovery 默认关闭。
    - 落点：`src/OptionHandlerFactory.cc`、`src/AbstractCommand.cc`、`src/Context.cc`
    - 行为：新增 `--enable-https-rr[=true|false]`，默认 `false`；未显式开启时，即使启用 HTTPS、async DNS、H2 或 H3 capability gate，也不会额外发起 HTTPS RR/TYPE65 查询，也不会消费已有 SVCB cache 改 TCP connect target/port 或追加 address hints。
    - 行为：显式 `--enable-https-rr=true` 后，直连 HTTPS origin 才启动后台 HTTPS/SVCB discovery，并允许 cached selected endpoint 参与 connect target/port 选择；无 async DNS 构建会在参数解析阶段拒绝该能力，不进入运行期崩溃。
    - 行为：`--async-dns=false` 时，即使显式 `--enable-https-rr=true` 且 engine 中已有 HTTPS RR/SVCB cache，也不会生成 discovery phase、不会消费 cached endpoint 改 connect target/port、不会追加 SVCB address hints。该边界由 `1697ce06` 和 `c175661a` 补齐。
    - 日志：启动日志打印 `enable-https-rr` 的最终值和来源，便于确认“默认不多查 HTTPS RR”的配置边界。

12. `max-connection-per-server` server 维度改为 URL `protocol + hostname`。
    - 落点：`src/FileEntry.cc`、`src/FileEntry.h`
    - 行为：`-x` 硬上限按 URL scheme/host 计数，不按解析出来的 IP 计数；同一 HTTPS hostname 解析出多个 IPv4/IPv6 地址也不能绕过上限。
    - 行为：`http://example.org` 和 `ftp://example.org` 视为不同 server key，分别受 `-x` 上限约束。
    - 文档：`src/usage_text.h`、`doc/manual-src/en/aria2c.rst`、`docs/command-line-help.zh-CN.md` 已同步 `protocol+host` 语义，并修正英文手册里的 `split=16`、`min-split-size=2M` 默认值。

13. DNS query plan 日志第二阶段。
    - 落点：`src/AsyncNameResolverMan.cc`、`src/AsyncNameResolverMan.h`
    - 行为：每个 async DNS A/AAAA family 启动前的代码路径会打印统一 network 日志，包含 `CUID`、hostname、qtype、mode、phase、backend、transport、server、bootstrap 来源和 `fallback_from`。
    - 行为：`multi` secure-first 阶段会分别打印 DoT/DoH plan；secure server 为域名时可看出 bootstrap 来自显式 plain DNS 还是系统 c-ares；显式 plain fallback 和系统 c-ares fallback 也会记录来源。当前已用单元日志断言覆盖这三个阶段，真实 artifact 运行日志仍待后续功能测试确认。
    - 边界：这仍是 resolver plan 级日志，不依赖 `ares_set_server_state_callback`，旧 c-ares/旧 Windows 构建不会因为缺新 API 编译失败；DoH/DoT connect failure 已有 fake transport 单测，真实 resolver/server 细节、HTTPS RR resolver 内部细节还需要后续继续补。

## 已加测试

1. `test/AsyncNameResolverTest.cc`
   - `testConfigureIgnoresSecureDnsConfigWhenAsyncDnsDisabled`：覆盖 `async-dns=false` 时 DoT 空 server、DoH 空 server、`multi` 裸域名 plain server 这些本应启动期失败的 secure DNS 配置不触发校验失败。
   - `testConfigureDohHttp2AffectsLoggedTransport`：覆盖 DoH query plan 的 transport 受 `enable-http2` 和 `enable-http-pipelining` 共同门控；开启 HTTP/2 且关闭 pipelining 时允许 `https-h1-or-h2`，开启 pipelining 时保持 `https-h1`。
   - `testPlainBootstrapFactoryUsesConfiguredPlainServers`：覆盖 `multi` 显式 `udp://`/`tcp://` plain server 会生成 `PlainBootstrapResolver`。
   - `testStartAsyncCaresWithExplicitServerFallsBackToSystem`：覆盖显式 c-ares server 失败后进入系统 c-ares。
   - `testStartAsyncDotFallsBackToSystem`：覆盖 DoT 失败后进入系统 c-ares。
   - `testStartAsyncMultiStartsSecureBackendsBeforePlainFallback` / `testStartAsyncMultiFallsBackToExplicitPlainThenSystem`：覆盖 `multi` 下载域名解析的 secure-first 和显式 plain -> 系统 c-ares fallback 阶段。
   - `testStartAsyncMultiStartsSecureBackendsBeforePlainFallback` 额外断言真实 `createResolvers()` 主阶段只创建 DoT/DoH resolver，不提前创建 plain resolver。
   - `testStartAsyncMultiLogsQueryPlans`：覆盖 `multi` 主阶段 DoT/DoH query plan、显式 plain fallback query plan 和系统 c-ares fallback query plan 的 network 日志字段。
   - 既有 DoT/DoH/multi 配置校验测试显式设置 `async-dns=true`，避免新早退让测试空跑。

2. `test/AsyncDotNameResolverTest.cc`
   - `testRetryNextServerOnConnectError`：覆盖 DoT 第一个 endpoint 连接失败后切换到下一 server，network 日志可看到 connecting、endpoint failed、server failed 和下一 server connecting。

3. `test/DownloadEngineTest.cc`
   - `testMarkBadIPAddressLogsAddressFamily`：覆盖 `DownloadEngine::markBadIPAddress()` 对 IPv4/IPv6 bad address 打印 host、port、ip、family，给失败 IP/临时避让地址提供统一可观测入口。

4. `test/AsyncDohNameResolverTest.cc`
   - `testHttp2SelectionLogsTransport`：覆盖 fake DoH transport 协商到 ALPN `h2` 后，network log 打出 `DNS: DoH using HTTP/2`。
   - `testRetryNextServerOnConnectError`：覆盖 DoH 第一个 endpoint 连接失败后切换到下一 server，network 日志可看到 connecting、endpoint failed、server failed 和下一 server connecting。

5. `test/AbstractCommandTest.cc`
   - `testGetUsableHttpsServiceBindingAddressHintsHonorsAsyncDns`：覆盖 `async-dns=false` 时即使 `enable-https-rr=true` 也不返回 HTTPS RR address hints。
   - `testCreateHttpsServiceBindingDiscoveryPhasesDisabledByDefault`：覆盖默认未启用 `--enable-https-rr` 时不会创建 HTTPS RR discovery phase。
   - `testCreateHttpsServiceBindingDiscoveryPhasesDisabledByAsyncDns`：覆盖 `async-dns=false` 时不会创建 HTTPS RR discovery phase。
   - `testCreateHttpsServiceBindingDiscoveryPhasesDisabledByHttp3Only`：覆盖仅开启 `enable-http3=true` 但未显式开启 `enable-https-rr=true` 时不会创建 HTTPS RR discovery phase。
   - `testShouldStartHttpsServiceBindingDiscoveryGatesQueries`：覆盖默认关闭、已有 cache、正在解析和 `async-dns=false` 时不会启动 HTTPS RR discovery。
   - `testCreateHttpsServiceBindingDiscoveryPhasesIgnoresSecureConfigWhenAsyncDnsOff`：覆盖 `async-dns=false + enable-https-rr=true + doh 空 server` 时 phases 为空，且不触发 secure DNS 配置校验。
   - `testCreateHttpsServiceBindingDiscoveryPhasesCaresSystem` / `testCreateHttpsServiceBindingDiscoveryPhasesCaresExplicit`：覆盖 HTTPS RR discovery 在 c-ares 模式下的系统 DNS 和显式 c-ares -> 系统 c-ares 阶段。
   - `testCreateHttpsServiceBindingDiscoveryPhasesDot` / `testCreateHttpsServiceBindingDiscoveryPhasesDoh`：覆盖纯 DoT/DoH 的 secure -> 系统 c-ares 阶段。
   - `testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureFirst` / `testCreateHttpsServiceBindingDiscoveryPhasesMultiSecureOnly` / `testCreateHttpsServiceBindingDiscoveryPhasesMultiPlainOnly` / `testCreateHttpsServiceBindingDiscoveryPhasesMultiSystemOnly`：覆盖 `multi` 下 secure-first、secure-only、plain-only 和空 server 的 HTTPS RR discovery 阶段顺序。

6. `test/HttpRequestTest.cc`
   - `testHttpsConnectionLogIncludesRemoteEndpoint`：覆盖 HTTPS 建连成功日志必须包含 IPv4/IPv6 remote endpoint。

7. `test/HttpResponseTest.cc`
   - `testResponseLogsIncludeRemoteEndpoint`：覆盖 `Response received from <ip:port>` 和 `Response status ... remote=<ip:port>`。

8. `test/OptionProcessingTest.cc`
   - `testStartupOptionLogSources`：覆盖启动关键参数日志的 `default/conf/command/runtime` 来源判定和 `conf-precedence=conf` 映射。

9. `test/OptionHandlerTest.cc`
   - `testFactoryMaxConnectionPerServerLimit`：覆盖 `-x 64` 成功、`-x 65` 失败。
   - `testUnsupportedFeatureOptionParser`：覆盖 `--enable-https-rr` 默认 `false`、显式 `false` 可解析、有 async DNS 构建时 `true` 可解析、无 async DNS 构建时 `true` 启动期拒绝。

10. `test/InitiateConnectionCommandTest.cc`
   - `testSelectBackupIPAddressSkipsScopedIPv6Backup`：覆盖 IPv4 主连接时不会把 ULA/link-local IPv6 选作 backup 连接地址。

11. `test/HttpInitiateConnectionCommandTest.cc`
    - `testSelectConnectionAuthorityIgnoresCachedSvcbByDefault`：覆盖默认未启用 `--enable-https-rr` 时即使已有 SVCB cache，也不改 connect target/port、不写 selected endpoint address hints。
    - `testSelectConnectionAuthorityIgnoresCachedSvcbWhenAsyncDnsOff`：覆盖 `async-dns=false + enable-https-rr=true + 已有 SVCB cache` 时仍使用 origin，不设置 endpoint info，不缓存 svc address hint。
    - 既有 cached SVCB endpoint、failed endpoint、proxy 和 connect failure 测试显式设置 `async-dns=true` 与 `--enable-https-rr=true`，确保 opt-in 后仍可消费 SVCB cache。

12. `test/FileEntryTest.cc`
   - `testGetRequest`：覆盖同 host 不同 protocol 可分别获得请求，验证 `http://localhost` 不再误挡 `ftp://localhost`。
   - `testGetRequest_limitsSameProtocolHost`：覆盖同 protocol+host 仍受 `max-connection-per-server` 限制，提升到 `2` 后只允许第二条同 server 请求。
   - `testFindFasterRequestUsesProtocolHostLimit`：覆盖 faster-server 替换路径也按 protocol+host 计数，HTTP 同 host 占满时不会挑同 HTTP 候选，但允许 FTP 同 host 候选。

13. `test/NameResolveCommandTest.cc`
   - `testUDPTrackerIgnoresSecureDnsConfigWhenAsyncDnsDisabled`：覆盖 UDP tracker 入口在 `async-dns=false + doh 空 server` 时构造阶段不触发 secure DNS 配置校验。
   - `testDHTEntryPointIgnoresSecureDnsConfigWhenAsyncDnsDisabled`：覆盖 DHT entry point 入口在 `async-dns=false + doh 空 server` 时构造阶段不触发 secure DNS 配置校验。

## 当前验证

1. 静态检查：
   - `git diff --check` 通过；只有 Windows 工作区的 LF/CRLF 提示，没有 whitespace error。
   - HTTPS RR/TYPE65 staged fallback 切片在提交前已通过上述静态检查；本机缺少 `make`/`g++`/`clang++`/`cl`/`cmake`，尚未完成本地编译或单元测试。
   - 日志可观测性第一阶段切片已通过 `git diff --check -- src/AbstractCommand.cc src/HttpConnection.cc src/SocketCore.cc` 和外部 review；GitHub Actions run `27242784981` 已通过。
   - 双栈 backup 避让第一阶段切片已通过 `git diff --check -- src/InitiateConnectionCommand.cc test/InitiateConnectionCommandTest.cc docs/reports/2026-06-10-async-dns-requirements-progress.md` 和外部 review；GitHub Actions run `27243094090` 已通过。
   - HTTPS RR 默认关闭切片已通过 `git diff --check`、外部 review 和 GitHub Actions；本机仍缺少 C++ 构建工具，编译验证依赖 CI。
   - `max-connection-per-server` protocol+host 切片已通过 `git diff --check`、外部 review 和 GitHub Actions，run id `27245665096`。
   - DNS query plan 日志第二阶段切片已通过 `git diff --check -- src/AsyncNameResolverMan.cc src/AsyncNameResolverMan.h test/AsyncNameResolverTest.cc docs/reports/2026-06-10-async-dns-requirements-progress.md`；新增单元日志断言；本机仍缺少 C++ 构建工具，编译验证依赖 GitHub Actions。
   - `4c0af2e3 Log async DNS query plans` 首次 CI 失败后，经外部 review 定位到 `formatDohServerList()` 中 `auto entry = "https://";` 被 C++11 推导成 `const char*`，后续 `+= std::string` 在 MinGW 编译期失败；`f354ff58 Fix DoH server list string construction` 改成显式 `std::string` 后通过 GitHub Actions。
   - `NameResolveCommandTest.cc` 入口级防回归切片已通过 `git diff --check -- test/NameResolveCommandTest.cc test/Makefile.am docs/reports/2026-06-10-async-dns-requirements-progress.md`、外部 review 和 GitHub Actions，run id `27253605059`。
   - `AsyncNameResolverTest.cc` disabled secure DNS 配置扩展切片已通过 `git diff --check -- test/AsyncNameResolverTest.cc docs/reports/2026-06-10-async-dns-requirements-progress.md`、外部 review 和 GitHub Actions，run id `27254138777`。
   - HTTPS established / Response received remote IP 日志补强和 DoH H2 transport 门控测试已通过 `git diff --check -- src/HttpRequestCommand.cc src/HttpConnection.cc test/AsyncNameResolverTest.cc docs/reports/2026-06-10-async-dns-requirements-progress.md`、Volta/Euclid 外审和 GitHub Actions，run id `27255420287`。
   - 当前复查补强切片已通过 `git diff --check` 和外部 review；本机仍缺少 C++ 构建工具，编译验证需依赖 GitHub Actions。

2. CI：
   - 前置提交 `2997acde Align async DNS bootstrap and connection limits` 的 GitHub Actions build 已通过，run id `27233170820`。
   - 前置提交 `fed40f3e Support config discovery precedence and startup option logs` 的 GitHub Actions build 已通过，run id `27235349508`。
   - 本报告对应的 secure-first fallback 切片已提交为 `0e483039 Implement secure-first async DNS fallback chain`，GitHub Actions build 已通过，run id `27238331842`，耗时 `8m55s`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27238331842
   - HTTPS RR/TYPE65 staged fallback 切片已提交为 `b41ada1a Stage HTTPS RR discovery DNS fallback`，GitHub Actions build 已通过，run id `27240908069`，耗时 `8m20s`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27240908069
   - 日志可观测性第一阶段切片已提交为 `07d2a84e Add DNS and connection endpoint network logs`，GitHub Actions build 已通过，run id `27242784981`，耗时 `9m6s`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27242784981
   - 双栈 backup 避让第一阶段切片已提交为 `d6186215 Skip non-global IPv6 backup connections`，GitHub Actions build 已通过，run id `27243094090`，耗时 `8m20s`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27243094090
   - HTTPS RR 默认关闭切片已提交为 `63934398 Gate HTTPS RR discovery behind option`，GitHub Actions build 已通过，run id `27244533609`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27244533609
   - `max-connection-per-server` protocol+host 切片已提交为 `8e0b8866 Key connection limit by URL server`，GitHub Actions build 已通过，run id `27245665096`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27245665096
   - DNS query plan 日志第二阶段已提交为 `4c0af2e3 Log async DNS query plans`，首次 GitHub Actions build 失败，run id `27246792087`。
   - 失败 run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27246792087
   - 编译修复已提交为 `f354ff58 Fix DoH server list string construction`，GitHub Actions build 已通过，run id `27247443220`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27247443220
   - 报告更新已提交为 `0a13f069 Update async DNS progress report`，GitHub Actions build 已通过，run id `27248076745`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27248076745
   - 配置发现顺序补强已提交为 `0ab1da12 Test config discovery candidate order`，GitHub Actions build 已通过，run id `27251537049`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27251537049
   - HTTPS RR discovery async DNS 门控已提交为 `1697ce06 Gate HTTPS RR discovery on async DNS`，GitHub Actions build 已通过，run id `27251874742`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27251874742
   - HTTPS RR/SVCB cache async DNS 门控已提交为 `c175661a Disable cached HTTPS RR when async DNS is off`，GitHub Actions build 已通过，run id `27252444409`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27252444409
   - disabled async DNS 入口级防回归已提交为 `9781c20a Cover disabled async DNS entry points`，GitHub Actions build 已通过，run id `27253605059`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27253605059
   - disabled async DNS 配置扩展已提交为 `2c192561 Expand disabled async DNS config coverage`，GitHub Actions build 已通过，run id `27254138777`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27254138777
   - HTTPS/Response remote IP 日志与 DoH H2 transport 门控测试已提交为 `cf425451 Expose remote endpoint in HTTP logs`，GitHub Actions build 已通过，run id `27255420287`。
   - run 链接：https://github.com/yukaidi1220/aria2/actions/runs/27255420287

3. artifact：
   - `0e483039` artifacts：
     - `aria2-x86_64-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27238331842/artifacts/7521271861
     - `aria2-i686-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27238331842/artifacts/7521230494
     - artifact 过期时间：`2026-09-07T21:54:20Z`。
   - `b41ada1a` artifacts：
     - `aria2-x86_64-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27240908069/artifacts/7522240987
     - `aria2-i686-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27240908069/artifacts/7522203553
     - artifact 过期时间：`2026-09-07T22:50:05Z`。
   - `63934398` artifacts：
      - `aria2-x86_64-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27244533609/artifacts/7523580423
      - `aria2-i686-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27244533609/artifacts/7523554794
      - artifact 过期时间：`2026-09-08T00:21:09Z`。
   - `8e0b8866` artifacts：
      - `aria2-x86_64-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27245665096/artifacts/7524007978
      - `aria2-i686-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27245665096/artifacts/7523998481
      - artifact 过期时间：`2026-09-08T00:52:21Z`。
   - `f354ff58` artifacts：
      - `aria2-x86_64-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27247443220/artifacts/7524663979
      - `aria2-i686-w64-mingw32`：https://github.com/yukaidi1220/aria2/actions/runs/27247443220/artifacts/7524650119
      - artifact 过期时间：`2026-09-08T01:42:31Z`。
   - `0a13f069` artifacts：
      - run 页面：https://github.com/yukaidi1220/aria2/actions/runs/27248076745
      - artifact 精确下载链接待补。当前 `gh api repos/yukaidi1220/aria2/actions/runs/27248076745/artifacts` 触发 GitHub API rate limit；已确认 run 本身结论为 success，待 API 恢复后补 artifact id 和过期时间。
   - `9781c20a` artifacts：
      - `aria2-x86_64-w64-mingw32`：https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7526761566/zip
      - `aria2-i686-w64-mingw32`：https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7526747997/zip
      - artifact 过期时间：`2026-09-08T04:39:47Z`。
   - `2c192561` artifacts：
      - `aria2-x86_64-w64-mingw32`：https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7526960957/zip
      - `aria2-i686-w64-mingw32`：https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7526944357/zip
      - artifact 过期时间：`2026-09-08T04:54:51Z`。
   - `cf425451` artifacts：
      - `aria2-x86_64-w64-mingw32`：https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7527410601/zip
      - `aria2-i686-w64-mingw32`：https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7527389521/zip
      - artifact 过期时间：`2026-09-08T05:30:21Z`。

4. 外部评审：
   - 47 条需求只读评审结论：当前分支已有配置加载、secure-first DNS fallback、HTTPS RR 门控、连接数限制和日志地基，但最终验收矩阵、resolver 运行期 per-server 细日志、真实 DoT/DoH/multi/fake DNS、双栈端到端、XP/Win7 退化验证仍未闭环。
   - H2/H3/HTTPS RR 评审结论：H2 active/idle reuse 只能按当前实现口径说明为同 `RequestGroup` 内条件可用；DoH over H2 只在 `--enable-http2=true` 且 `--enable-http-pipelining=false`、ALPN 选中 h2 时发生；H3 仍是能力门，不能宣称完整 H3 下载。
   - 双栈评审结论：现有 `FileEntry` family penalty、`selectIPAddress()` 和 backup 连接已经能作为第一阶段基础，下一步应优先补“已缓存双栈地址时的混合并发”和 bad family 避让测试，不要重写调度器。
   - 中文帮助文档评审结论：`docs/command-line-help.zh-CN.md` 已覆盖新增网络选项和示例，但还需要补解析路径图、DNS mode/server 对照表、配置加载合并顺序、`split/-x/-k` 关系、`network` 日志语义，以及“HTTP/3 目前只是能力门”的醒目边界说明。

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

部分完成。第 21 条已实现：32-bit MinGW 不再默认 `disable-ipv6=true`。第 23 条先补了 backup 连接避让：IPv4 主连时不会把非公网 IPv6 地址选作 backup，减少内网 IPv6 存在但公网不可达时的额外拖慢。第 25 条的硬限制已改为按 URL `protocol + hostname` 计数，不按解析 IP 计数；同 hostname 多个 IPv4/IPv6 地址不能绕过 `-x` 上限。第 22、24、25 条仍需继续做运行期地址能力、坏 IPv6 快速避让、v4/v6 混合并发下载和端到端验证。现有双栈 first-success 与后台 DNS cache fill 是基础，但不能宣称完整覆盖 RFC 8305 行为。

复查补充：当前单测入口默认固定 `SocketCore::setProtocolFamily(AF_INET)`，所以不能把现有单测解释为真实 v4/v6 同时下载验收。后续需要 fake resolver + fake socket/connection factory 级测试，或在功能测试报告中明确依赖真实 IPv6 环境。

### HTTPS RR / H2 / H3（26-30）

部分完成。H2/H3 默认关闭已有基础；H3 仍只是能力门。SVCB endpoint ALPN 已接入 TLS ALPN 收窄。HTTPS RR/TYPE65 discovery 当前能按后端创建 resolver，DoT/DoH 域名 server 的 bootstrap 可复用显式 plain server，并且 `multi` 下已改为 secure-first 阶段式 fallback；该 discovery 仍是后台任务，不阻塞首连。本轮新增 `--enable-https-rr=false` 默认门控，未显式开启时不会额外查询 HTTPS RR/TYPE65，也不会消费 SVCB cache 改 connect target/port，满足“未启用 H3、也没有明确需要 SVCB/HTTPS RR 的能力时，不应额外查 HTTPS RR”的第一阶段边界。DoH H2 已有 network 日志 `DNS: DoH using HTTP/2`；还需要继续补真实网络/fake server 验收。

复查补充：已补 `enable-http3=true` 但未显式 `enable-https-rr=true` 不启动 HTTPS RR discovery 的单元测试；DoH 实际协商到 HTTP/2 后的 `DNS: DoH using HTTP/2` 也已补 fake transport 日志断言。

### 日志可观测性（31-35）

部分完成。第一阶段已补 DNS 最终选中日志、HTTP response status 的 network remote IP:port 和 TLS remote/SNI/verify/version/ALPN 日志，能把下载请求的 CUID、hostname、候选地址、最终选中 IP、TLS 建连端点和响应端点串起来。测试暴露后，本轮继续补强用户直接看到的两条日志：`HTTPS connection to ... established` 追加 `remote=<ip:port>`，info 级 `Response received` 改为 `Response received from <ip:port>`。第二阶段已补 async DNS query plan 日志代码路径，并用单元日志断言覆盖 `multi` 主阶段、显式 plain fallback 和系统 c-ares fallback 的 mode、phase、backend、transport、server、bootstrap 来源和 fallback 来源。后续又补 DoH/DoT endpoint 运行期 connect failure 日志断言，并在 `DownloadEngine::markBadIPAddress()` 统一打印失败 IP/临时避让地址的 host、port、ip、family。尚未完整覆盖真实 artifact 运行日志、HTTPS RR resolver 内部 server 明细、CUID 贯穿 bad-address 入口、抓包级 v4/v6 对账断言；这些仍需继续在 resolver 内部和连接失败路径补细日志与测试。

复查补充：已补 HTTPS 建连成功日志、`Response received from` 和 `Response status ... remote=` 的格式级回归测试，防止后续把 remote endpoint 从用户可见日志里删掉。

### 下载连接参数（36-39）

部分完成。第 36 条已在源码中改为 URL `protocol + hostname` server key，不按解析 IP 绕过限制；`http://host` 与 `ftp://host` 分开计数，同一 `https://host` 的多 IPv4/IPv6 地址仍共享上限。第 37 条已实现：`max-connection-per-server` 上限为 64。第 38 条已核验并记录：`split=16`、`min-split-size=2M`、`max-connection-per-server=1`，英文手册旧默认值已同步修正。第 39 条已在中文帮助和英文手册补充三者关系，后续还要加真实下载行为验收。

### 兼容性（40-42）

部分完成。本轮 IPv6 默认改为单一基线，secure DNS disabled 模式不会误触发 DoH/DoT 配置失败。无 SSL 构建下 DoT/DoH/multi 仍由现有 option handler 拒绝。新 c-ares API configure 能力探测已有前置工作，但本轮没有新增 API。XP/Win7 上 DoH/DoT 不可用时的完整 fallback 链仍需后续实现与 CI/真实运行验证。

### 验收测试（43-47）

未完成到最终验收。当前只有单元级配置/参数测试和静态检查；还需要补真实功能测试矩阵：纯 DoT、纯 DoH、multi 混合、全 fake DNS、DoH/DoT 域名 bootstrap、`async-dns=false`、conf 自动发现、IPv4 only、IPv6 only、双栈、IPv6 不通、v4/v6 混合并发、H2/H3 默认关闭、HTTPS RR 不多查、DoH H2 开关，以及 network 日志断言。

## 下一阶段计划

1. 配置加载与来源追踪：继续补全量 option 来源追踪、运行期修改日志和更多启动日志断言；当前已完成基础配置查找顺序、`--no-conf=true`、`--conf-precedence=command|conf` 和关键参数来源日志。

2. secure-first multi 验收：下载域名主解析骨架已改为 secure resolver 优先，plain resolver 默认只用于 DoT/DoH server bootstrap 或失败后的 fallback；下一步要用 fake DNS/真实网络验证 secure、显式 plain、系统 c-ares、getaddrinfo 每一层的日志和失败边界。

3. DNS/连接可观测性：统一记录 DNS query、server、协议、bootstrap/fallback 阶段、A/AAAA、最终地址列表、选中/失败/避让 IP，并让连接成功、TLS 成功和 response received 都能关联 remote IP。
   - 已完成第一刀：DNS selected、HTTP response remote、TLS connected remote/SNI/ALPN。
   - 已完成第二刀：async DNS query plan 日志代码路径和单元日志断言，记录 CUID、host、qtype、mode、phase、backend、transport、server、bootstrap 和 fallback 来源。
   - 本轮阶段修复：HTTPS 建连成功日志和 `Response received` info 日志本身追加 remote IP，解决普通日志里看不出连接 IP 的测试问题。
   - 当前继续补强：DoH/DoT endpoint connect failure 日志已有 fake transport 回归断言，失败 IP/临时避让地址已有 `DownloadEngine::markBadIPAddress()` 统一 network 日志和 IPv4/IPv6 单测。
   - 后续待实现：HTTPS RR resolver 内部 server 明细、bad-address CUID 贯穿、真实 artifact 日志和抓包级 v4/v6 对账断言。

4. 双栈下载：继续实现/验证 v4/v6 混合并发、坏 IPv6 快速避让、同 hostname 连接数限制不被不同 IP 绕过。
   - 已完成第一刀：IPv4 主连时不再把非公网 IPv6 地址选作 backup。
   - 已完成第二刀：`max-connection-per-server` 按 URL `protocol + hostname` 计数，不按解析 IP 计数，避免不同 IP 绕过同 server 上限。
   - 下一刀继续评估本机公网 IPv6 能力判断、family penalty 和 v4/v6 混合并发端到端测试。

5. HTTPS RR/H2/H3 边界：TYPE65 discovery 已切到 secure-first 阶段式 fallback，并新增 `--enable-https-rr` 显式 opt-in 门控；下一步补 fake/真实 DNS 验收，确认默认不发 TYPE65、不消费 SVCB cache，开启后按当前 DNS backend 查询并允许 selected endpoint 生效，DoH over H2 日志可断言。H3 继续保持默认关闭和 unsupported 快速拒绝。

6. 验收报告：CI 通过后补 GitHub Actions run、artifact 链接和实际功能测试结果。
