# aria2c 命令行帮助中文草稿

> 状态：当前分支草稿。本文只整理命令行/配置项说明，不修改源码。
>
> 主要来源：`src/OptionHandlerFactory.cc::OptionHandlerFactory::createOptionHandlers()`、`src/usage_text.h`、`doc/manual-src/en/aria2c.rst`。新增网络能力额外核对了 `src/TLSSNIHostMapping.cc`、`src/HostMapping.cc`、`src/HttpTLSHandshakeParams.cc`、`src/SocketCore.cc`、`src/AsyncNameResolverMan.cc`、`src/AsyncDnsServerConfig.cc`、`src/AsyncDotNameResolver.cc`、`src/AsyncDohNameResolver.cc`、`src/HttpProtocol.cc`、`src/Http2HeaderBlock.cc`、`src/Http2Session.cc`、`src/HttpRequestCommand.cc`、`src/HttpInitiateConnectionCommand.cc`、`src/HttpSkipResponseCommand.cc`、`src/DownloadEngine.cc`、`src/RequestGroup.cc`、`src/download_helper.cc` 等实现文件。

## 1. 基本用法

```console
aria2c [OPTIONS] [URI | TORRENT_FILE | METALINK_FILE]...
aria2c -i urls.txt -j 4 -x 8 -s 8 --continue=true
aria2c --conf-path=aria2.conf --enable-rpc --rpc-secret=TOKEN
```

说明：

- 命令行长选项一般也可写入 `aria2.conf`，形式为 `option=value`，例如 `max-concurrent-downloads=4`。
- 布尔选项通常可写成 `--foo`、`--foo=true` 或 `--foo=false`；不带值时由对应 `OptionHandler` 的 `OPT_ARG` 规则处理。
- `OptionHandlerFactory.cc` 是当前可注册选项的准绳；`usage_text.h` 是 `aria2c --help` 文案；英文 manual 是长说明。三者不一致时，本文会标出差异。
- 以下表格里的“默认值”优先来自 `OptionHandlerFactory.cc` / manual；没有明确默认值的写“无”或“依构建/平台”。

多 URL 和下载项：

- 命令行里连续给出多个 HTTP/FTP 等 URL 时，默认会作为同一个下载项的多个 URI 进入同一个 `RequestGroup`，通常表示同一个文件的多个镜像源，而不是多个独立下载。源码落点是 `src/download_helper.cc::createRequestGroupForUri()`：在未启用 `--force-sequential=true` 时，流式协议 URL 会先被 `splitURI()` 合并成一组，再创建一个 `RequestGroup`。
- 要把多个 URL 当成多个独立下载，常用做法是使用 `--force-sequential=true`，或在 `--input-file` 中按 aria2 输入文件格式分成多个下载项。每个独立下载项会有自己的 `RequestGroup`。
- 这个边界会影响 HTTP/2 复用：当前 active/idle H2 连接复用只在同一个 `RequestGroup` 内发生。命令行多个镜像 URL 如果属于同一个下载项，才可能命中该复用；多个独立下载项之间不会因为同 origin 就共享 H2 连接。

## 2. 新增/重点网络能力

### 2.0 当前能力矩阵

| 能力 | 用户可用状态 | 关键边界 | 主要源码落点 |
| --- | --- | --- | --- |
| FakeSNI / `--tls-sni-host` | 已接入 HTTPS TLS 握手，可写单一 SNI 或 `TARGET:SNI` 映射 | 只改 ClientHello SNI；不改 DNS、TCP 目标、HTTP `Host:` 或证书校验主机；SNI 与校验主机不同时要求 TLS 后端支持 SNI override | `src/TLSSNIHostMapping.cc`、`src/HttpTLSHandshakeParams.cc`、`src/SocketCore.cc`、`src/TLSSession.h` |
| `--hosts-mapping` | 已接入直连 HTTP/HTTPS 解析路径 | 一边必须是主机名，另一边必须是 IP；代理解析不走这里；`IPADDR:HOST` 会改变逻辑 HTTP/TLS 主机 | `src/HostMapping.cc`、`src/AbstractCommand.cc`、`src/HttpRequest.cc` |
| DoT / DoH | 已接入异步 DNS 后端 | 没有 `--async-dns-over-https` / `--async-dns-over-tls` 独立参数；使用 `--async-dns-mode=doh|dot`；当前直连模式要求 DNS server 连接目标是数值 IP；`#TLS_HOST` 只作为 TLS/HTTP 名称 hint | `src/AsyncNameResolverMan.cc`、`src/AsyncDnsServerConfig.cc`、`src/AsyncDotNameResolver.cc`、`src/AsyncDohNameResolver.cc` |
| IPv4/IPv6 双栈选择 | 已有第一阶段 Happy Eyeballs 行为 | A/AAAA 异步并发解析，任一成功先用；后台补齐新地址后唤醒后续连接；异步 DNS 且 IPv6 未禁用时 opposite-family 备份连接延迟为 `0ms`，否则保持 `300ms` | `src/AsyncNameResolverMan.cc`、`src/AbstractCommand.cc` 内的 `AsyncDnsCacheCommand`、`src/InitiateConnectionCommand.cc`、`src/BackupIPv4ConnectCommand.cc`、`src/ConnectCommand.cc` |
| DoH over H2 | 条件可用 | 需要 `HAVE_LIBNGHTTP2`、`--enable-http2=true`、`--enable-http-pipelining=false` 和 TLS ALPN；ALPN 未选中 `h2` 时回落 HTTP/1.1；DNS query 作为 HTTP/2 POST DATA 发送 | `src/AsyncDohNameResolver.cc`、`src/Http2SingleStreamExchange.cc`、`src/Http2Session.cc` |
| HTTP/2 / H2 | 实验性可用，依赖 `HAVE_LIBNGHTTP2`、HTTPS 和 TLS ALPN | 不是全局连接池；当前 active/idle H2 复用只在同一 `RequestGroup` 内；origin coalescing 条件很保守，421 只记录本下载组内的负缓存；普通下载路径只提交请求头，带请求体的 H2 发送路径主要用于 DoH | `src/HttpTLSHandshakeParams.cc`、`src/HttpProtocol.cc`、`src/Http2HeaderBlock.cc`、`src/Http2Session.cc`、`src/HttpRequestCommand.cc`、`src/HttpInitiateConnectionCommand.cc`、`src/HttpSkipResponseCommand.cc`、`src/DownloadEngine.cc`、`src/RequestGroup.cc` |
| HTTP/3 / H3 / QUIC | 只有禁用占位参数 | `--enable-http3=true` 会被 `UnsupportedFeatureOptionHandler` 拒绝；源码没有 QUIC 传输、H3 command、`h3` ALPN 分发或依赖探测 | `src/OptionHandlerFactory.cc`、`src/usage_text.h` |
| ECH | 手动 ECHConfigList 第一阶段可用 | `--ech-config-base64=BASE64` 或 `--enable-ech=true --ech-config-base64=BASE64` 对 HTTPS 启用 required ECH；没有自动 HTTPS/SVCB 发现、retry config 自动重试或 H3 discovery；不能与 `--tls-sni-host` override 混用；TLS 后端不支持或握手后未接受 ECH 会失败 | `configure.ac`、`src/OptionHandlerFactory.cc`、`src/HttpTLSHandshakeParams.cc`、`src/SocketCore.cc`、`src/TLSSession.h`、`src/LibsslTLSSession.cc` |
| XP/Win7 兼容 | 以 HTTP/1.1 fallback 和可构建性为主 | 旧 Windows 原生 TLS 栈通常不要指望 ALPN/FakeSNI override；H2 要看 OpenSSL/GnuTLS ALPN，FakeSNI override 要看 OpenSSL/GnuTLS | `src/FeatureConfig.cc`、`src/SocketCore.cc`、`src/TLSSession.h` |

### 2.1 FakeSNI / `--tls-sni-host`

注册路径：

- `src/OptionHandlerFactory.cc` 注册 `PREF_TLS_SNI_HOST`，参数格式为 `HOST or TARGET:SNI[,TARGET:SNI]...`。
- `src/TLSSNIHostMapping.cc::getTLSSNIHostConfig()` 解析单一 SNI 或映射表。
- `src/HttpTLSHandshakeParams.cc::createHttpTLSHandshakeParams()` 计算 `sniHost`、证书校验主机 `verifyHost` 和 ALPN 列表。
- `src/SocketCore.cc::SocketCore::tlsConnect()` 校验 SNI 主机格式，调用 `TLSSession::setSNIHostname()`，并在 SNI 与证书校验主机不同时检查 `TLSSession::supportsSNIHostnameOverride()`。

语义：

- `--tls-sni-host=front.example`：所有 HTTPS 请求的 TLS ClientHello SNI 都使用 `front.example`。
- `--tls-sni-host=origin.example:front.example,redirect.example:redirect-front.example`：优先按当前请求 host 匹配；没有命中时，再按逻辑/默认 host 回退匹配。重定向后的请求会重新计算这两个 host。
- `TARGET` 可以是 DNS 主机名、IPv4 地址或 `[IPv6]`。IPv6 映射必须加方括号，例如 `[2001:db8::1]:front.example`。
- `SNI` 必须是可用于 TLS SNI 的 DNS 主机名；`isTLSSNIHostname()` 会拒绝 IP、`localhost`、单标签名、空标签、下划线、标签首尾 `-` 等。
- 同一个 `TARGET` 如果写了多次，`getTLSSNIHostConfig()` 会保留最先命中的配置；当前请求 host 的匹配优先级高于逻辑/默认 host 的匹配。
- 这个选项只改 TLS SNI，不改 DNS 解析、TCP 连接目标、HTTP `Host` 头、代理 `CONNECT` 目标、cookie 作用域、证书校验主机。
- 映射表里未命中的请求会继续使用逻辑/默认主机做 SNI；这不是通配符映射，源码没有 `*.example.com` 之类规则。

示例：

```console
aria2c --tls-sni-host=front.example https://origin.example/file
aria2c --tls-sni-host=origin.example:front.example,redirect.example:redirect-front.example https://origin.example/file
aria2c --tls-sni-host=[2001:db8::1]:front.example https://[2001:db8::1]/file
```

兼容注意：

- 如果 SNI 与证书校验主机不同，`SocketCore::tlsConnect()` 要求 TLS 后端 `supportsSNIHostnameOverride()` 为真。当前 OpenSSL/GnuTLS 后端显式支持；基类默认不支持，WinTLS/AppleTLS 当前没有在头文件里声明覆盖该能力。
- WinTLS 可以设置普通 SNI，但“FakeSNI：SNI 与校验主机不同”会被源码提前拒绝。XP/Win7 若使用原生 SChannel 后端，要优先使用 `--hosts-mapping=IP:HOST` 这种“逻辑主机一致”的方式，或换 OpenSSL/GnuTLS 构建。
- `--check-certificate=false` 能绕过证书校验，但不建议用于生产下载。

和 `--hosts-mapping` 的组合顺序：

- `createHttpTLSHandshakeParams()` 会先用 `HostMapping.cc::getLogicalHostForRequest()` 算出证书校验主机 `verifyHost`，再把当前请求 host 与 `verifyHost` 一起交给 `getTLSSNIHostConfig()` 查 SNI。
- `--hosts-mapping=198.18.0.18:origin.example https://198.18.0.18/file` 这种写法会让 HTTP `Host:`、默认 SNI、证书校验主机都变成 `origin.example`，通常比 FakeSNI 更适合旧 Windows TLS 后端。
- 如果再叠加 `--tls-sni-host=origin.example:front.example`，SNI 会变成 `front.example`，证书仍按 `origin.example` 校验；这才进入 FakeSNI override 能力边界。

### 2.2 `--hosts-mapping`

注册路径：

- `src/OptionHandlerFactory.cc` 注册 `PREF_HOSTS_MAPPING`。
- `src/HostMapping.cc::getMappedAddresses()` 处理 `HOST:IPADDR`。
- `src/HostMapping.cc::getLogicalHostForRequest()` 处理 `IPADDR:HOST`。
- `src/AbstractCommand.cc::resolveHostname()` 在普通 DNS 前优先应用 hosts mapping，并把映射 IP 写入 DNS cache。
- `src/HttpTLSHandshakeParams.cc::createHttpTLSHandshakeParams()` 使用 logical host 作为证书校验主机和默认 SNI。

格式：

```console
aria2c --hosts-mapping=origin.example:198.18.0.18 https://origin.example/file
aria2c --hosts-mapping=198.18.0.18:origin.example https://198.18.0.18/file
aria2c --hosts-mapping=origin.example:[2001:db8::1] https://origin.example/file
aria2c --hosts-mapping=[2001:db8::1]:origin.example https://[2001:db8::1]/file
```

规则：

- 每个条目是 `LEFT:RIGHT`，一边必须是 DNS 主机名，另一边必须是 IP 地址。
- `HOST:IPADDR`：请求 URL 仍是 `HOST`，但 TCP 连接直接去 `IPADDR`，不查 DNS。HTTP `Host`、默认 TLS SNI、证书校验主机仍是 `HOST`。
- `IPADDR:HOST`：URL 写 IP，但逻辑 HTTP/TLS 主机改为 `HOST`；TCP 仍连 IP，`HttpRequest::getBuiltinHeaders()` 生成的 HTTP `Host:` 也会使用这个逻辑主机。适合老系统/代理前置场景。
- IPv6 字面量必须写方括号。`HostMapping.cc` 会拒绝两边都是 IP 或两边都不是 IP 的配置。
- 映射只用于直连 HTTP/HTTPS；通过 HTTP 代理或 HTTPS `CONNECT` 时，代理侧解析仍由代理控制。
- `HOST:IPADDR` 会把映射 IP 写入 aria2 自己的 DNS cache；`IPADDR:HOST` 不需要解析 IP，但会改变后续 HTTP/TLS 逻辑主机。别把它当系统 hosts 文件的全局替代品，范围只在本次 aria2 进程内。

### 2.3 DoH/DoT / 异步 DNS

注册路径：

- `src/OptionHandlerFactory.cc` 在 `ENABLE_ASYNC_DNS` 下注册 `--async-dns`、`--async-dns-mode`、`--async-dns-server`。
- `--async-dns-mode` 在 `ENABLE_SSL` 下允许 `cares|dot|doh`；无 SSL 构建只允许 `cares`。
- `src/AsyncNameResolverMan.cc::resolverModeFromOption()` 选择后端。
- `src/AsyncNameResolverMan.cc::validateAsyncNameResolverConfig()` 在解析期校验后端配置；DoT/DoH 模式没有 server 会直接失败。
- `src/AsyncNameResolverMan.cc::createResolver()` 创建 `AsyncNameResolver`、`AsyncDotNameResolver` 或 `AsyncDohNameResolver`。
- `src/AsyncDnsServerConfig.cc` 解析和校验 DoT/DoH server 格式。
- `src/AsyncDotNameResolver.cc` / `src/AsyncDohNameResolver.cc` 驱动网络状态机并写 `A2_LOG_NETWORK`。

构建边界：

- 没有 `ENABLE_ASYNC_DNS` 的构建不会注册 `--async-dns`、`--async-dns-mode`、`--async-dns-server`；这时只能走同步 `NameResolver`，也没有下面的 DoT/DoH 后端。
- 有异步 DNS 但没有 `ENABLE_SSL` 的构建，`--async-dns-mode` 只接受 `cares`。`dot` / `doh` 不是“会降级”的值，而是参数校验阶段就不接受。
- 因此 Windows 旧系统能不能用 DoT/DoH，先看构建依赖和 TLS 后端；仅在配置文件里填写 `mode` 不能绕过缺失的构建能力。

选项：

```console
aria2c --async-dns=true --async-dns-mode=cares
aria2c --async-dns=true --async-dns-mode=dot --async-dns-server=1.1.1.1,8.8.8.8:853
aria2c --async-dns=true --async-dns-mode=dot --async-dns-server=[2606:4700:4700::1111]:853
aria2c --async-dns=true --async-dns-mode=dot --async-dns-server=1.1.1.1#cloudflare-dns.com
aria2c --async-dns=true --async-dns-mode=doh --async-dns-server=https://1.1.1.1/dns-query
aria2c --async-dns=true --async-dns-mode=doh --async-dns-server=https://[2606:4700:4700::1111]/dns-query
aria2c --async-dns=true --async-dns-mode=doh --async-dns-server=https://1.1.1.1/dns-query#cloudflare-dns.com
```

没有这些独立参数：

- 当前源码没有 `--async-dns-over-https` 或 `--async-dns-over-tls` 这两个字面命令行选项。
- DoH 使用 `--async-dns=true --async-dns-mode=doh --async-dns-server=...`。
- DoT 使用 `--async-dns=true --async-dns-mode=dot --async-dns-server=...`。
- 如果后续要增加别名，也应在 `OptionHandlerFactory.cc` 注册并同步 `usage_text.h`，不能只在文档里写。

`#TLS_HOST` hint：

- DoT/DoH 的 `#TLS_HOST` 是 aria2 自己解析出来的 hint，不会进入 DNS wire query，也不会改变要解析的下载目标域名。
- 对 DoT，`AsyncDotNameResolver::createTLSHandshakeParams()` 把 hint 同时用作 TLS SNI 和证书校验主机；TCP 仍连接 `connectHost:port`。
- 对 DoH，`AsyncDohNameResolver::createTLSHandshakeParams()` 同样把 hint 用作 TLS SNI 和证书校验主机，`createDohRequest()` 还会用它生成 HTTP/1.1 `Host:` 头；URL fragment 不会出现在 request target。
- hint 必须是合法 DNS 主机名，不能是 IP、`localhost`、单标签名或以下划线等非法字符组成的名字。

DoT 规则：

- 解析器能接受 `HOST`、`HOST:PORT`、`IP`、`IP:PORT`、`[IPv6]`、`[IPv6]:PORT`，默认端口 `853`。数值地址后可追加 `#TLS_HOST`，例如 `1.1.1.1#cloudflare-dns.com` 或 `[2606:4700:4700::1111]:853#cloudflare-dns.com`。
- 当前 `AsyncNameResolverMan` 采用直连 DNS server 模式，随后会调用 `validateAsyncDnsDotServerConfigForDirectConnect()`，因此实际可用配置要求 server host 是数值地址。写 `dns.example.org` 这类域名会在配置校验阶段失败，避免解析 DNS server 本身时递归套娃。
- `--async-dns-mode=dot` 必须显式提供 `--async-dns-server`；为空会报 “No async DNS DoT server configured”。
- `AsyncDotNameResolver::createTLSHandshakeParams()` 使用 server 的 TLS host；数值 IP 未写 hint 时证书校验主机回退为 IP 本身，且不会发送 DNS 主机名形式的 SNI。

DoH 规则：

- 解析器能接受 HTTPS URL，例如 `https://1.1.1.1/dns-query`、`https://[2606:4700:4700::1111]/dns-query` 或 `https://dns.example.org/dns-query`，默认端口 `443`。数值 HTTPS URL 可追加 `#TLS_HOST`，例如 `https://1.1.1.1/dns-query#cloudflare-dns.com`。
- 当前直连校验同样要求 URL host 是数值地址；`https://dns.example.org/dns-query` 能被解析，但会被 `validateAsyncDnsDohServerConfigForDirectConnect()` 拒绝。
- `--async-dns-mode=doh` 必须显式提供 `--async-dns-server`；为空会报 “No async DNS DoH server configured”。
- URL 必须有 path，拒绝 userinfo、密码；query 允许作为 path 的一部分发送。fragment 被当作 TLS/HTTP 逻辑主机名，只允许合法 DNS 主机名，不能是 IP、`localhost` 或单标签名。
- `AsyncDohNameResolver.cc::createDohRequest()` 是 HTTP/1.1 fallback 路径，发送 `POST`、`Accept: application/dns-message`、`Content-Type: application/dns-message`、`Connection: close`。
- 写了 `#TLS_HOST` 时，DoH TLS SNI、证书校验主机和 HTTP `Host:` 头都使用该主机；TCP 仍连接 URL 里的数值地址，HTTP request target 不包含 fragment。
- `AsyncNameResolverMan::configureAsyncNameResolverMan()` 会根据 `--enable-http2=true` 且 `--enable-http-pipelining=false` 设置 `dohHttp2_`；有 `HAVE_LIBNGHTTP2` 时，`AsyncDohNameResolver` 才会给 DoH TLS 请求配置 ALPN `h2,http/1.1`。服务端选中 `h2` 时使用 `Http2SingleStreamExchange`；未选中或 TLS 后端无 ALPN 时回落 HTTP/1.1。
- DoH over H2 的请求头由 `createDohHttp2Headers()` 生成：`:method=POST`、`:scheme=https`、`:authority`、`:path`、`accept: application/dns-message`、`content-type: application/dns-message`、`content-length`。DNS query body 通过 `Http2Session::submitRequest(headers, body)` 的 nghttp2 DATA provider 发送，并在 body 读完时置 `NGHTTP2_DATA_FLAG_EOF`。
- HTTP/1.1 响应必须是 HTTP 200，必须有正数且不超过上限的 `Content-Length`，不支持 `Transfer-Encoding`。HTTP/2 响应要求 `:status=200`、stream 正常结束且 DNS message body 不超过上限。当前 DoH 不复用普通下载链路的 H2 连接，也不支持 DoH over H3。

双栈解析：

- `AsyncNameResolverMan::startAsync()` 依据可用地址族启动 A/AAAA 查询；源码先放 IPv6 resolver，再放 IPv4 resolver。
- `configureAsyncNameResolverMan()` 会调用 `net::checkAddrconfig()`，如果本机没有 IPv6 或 `--disable-ipv6=true`，则关闭 AAAA 查询。
- `AsyncNameResolverMan::getStatus()` 只要任一地址族成功就返回成功；只有已启动的地址族全失败才返回失败。`AbstractCommand::resolveHostname()` 会立即使用已成功的地址，不等待仍在查询的另一族。
- `continueAsyncDnsCacheFill()` 会把仍在查询的 resolver 交给后台 `AsyncDnsCacheCommand`；后台后续拿到地址后写入 DNS cache，只有确实新增地址时才尝试唤醒同一下载组创建后续连接。
- `AbstractCommand::resolveHostname()` 拿到解析结果后写入 DNS cache，再由 `selectIPAddress()` / `getLeastUsedActiveAddressFamily()` 在 IPv4/IPv6 间选择。
- 当 cache 中 IPv4/IPv6 都可选时，`selectIPAddress()` 会先看同 host/port 当前 active 连接数，优先选择使用更少的地址族；如果 active 数相同，再用 `FileEntry::getNextAddressFamily()` 在同一下载项内轮换；没有 `FileEntry` 或轮换状态不可用时，才退回按 `cuid` 奇偶选择。
- `InitiateConnectionCommand::createBackupConnectCommand()` 会从 DNS cache 里找 opposite-family 地址，创建 `BackupIPv4ConnectCommand`。异步 DNS 开启且 IPv6 未禁用时，`getBackupConnectionDelay()` 返回 `0ms`，备份连接的延迟阈值降为 0，实际启动仍按 `DownloadEngine` 事件循环调度；否则保持传统 `300ms` 延迟。
- 备份连接胜出时，`ConnectCommand` 会切换到备份 socket。它只在原主 socket 已经通过 `getSocketError()` 报出明确错误时才把原地址标为 bad；如果原地址只是慢，不会因为备份更快就污染 DNS cache。
- 这仍不是完整 RFC 8305 地址排序队列；更准确说是“同时支持 A/AAAA 解析、当前线程用最快可用结果，后台补齐地址给后续线程复用；当两个地址族已在 cache 中时，连接层用现有主/备份 socket 机制做双栈竞速”。

XP/Win7 注意：

- `configure.ac` 的 mingw 注释说明 `getaddrinfo` 依赖 `_WIN32_WINNT >= 0x0501`；源码还保留了 Windows 地址配置探测路径。
- `SocketCore::checkAddrconfig()` 在 mingw 下使用 `GetAdaptersAddresses()`；失败时会保守假设 IPv4/IPv6 都可用。因此旧系统若 IPv6 栈坏或 AAAA 查询拖慢，建议显式加 `--disable-ipv6=true`。
- 默认 `--min-tls-version=TLSv1.2`。老 XP/Win7 的原生 TLS 栈可能不满足现代 TLS/ALPN/SNI 需求；需要兼容时优先考虑 OpenSSL/GnuTLS 构建；涉及 H2 时还必须确认 OpenSSL ALPN 或 GnuTLS ALPN 可用，或只在受控环境降低 TLS 版本。
- DoT/DoH 在旧 Windows 上没有额外的系统 DNS 魔法：DoT/DoH server 连接仍走普通 socket + TLS。数值 DNS server 写法能避免“先解析 DNS server 自己”的递归问题，但证书校验、TLS 版本、IPv6 可达性仍受构建和系统环境影响。

### 2.4 HTTP/2 / `--enable-http2`

注册路径：

- `src/OptionHandlerFactory.cc`：有 `HAVE_LIBNGHTTP2` 时注册为 `BooleanOptionHandler`；没有 `HAVE_LIBNGHTTP2` 时注册为 `UnsupportedFeatureOptionHandler`，设置为 true 会快速失败。
- `src/HttpTLSHandshakeParams.cc::createHttpAlpnProtocols()`：有 `HAVE_LIBNGHTTP2`、`--enable-http2=true` 且 `--enable-http-pipelining=false` 时，ALPN 顺序为 `h2`、`http/1.1`。
- `src/SocketCore.cc::SocketCore::tlsHandshake()` 先检查 `TLSSession::supportsAlpnProtocols()`；支持时调用 `setAlpnProtocols()`，不支持时跳过 ALPN 设置并继续普通 TLS 握手。
- `src/HttpProtocol.cc::decideHttpProtocolFromSelectedAlpn()` 根据服务端选中的 ALPN 判定 HTTP/1.1 或 HTTP/2。
- `src/Http2HeaderBlock.cc::createHttp2HeaderBlockFromHttpRequest()` 把普通 HTTP/1.1 请求行/头转换为 H2 伪头，并过滤 `Connection`、`Keep-Alive`、`Proxy-Connection`、`Transfer-Encoding`、`Upgrade` 等连接级头；`TE` 只允许 `trailers`。
- `src/Http2Session.cc::submitRequest(headers, body)` 在 body 非空时注册 nghttp2 data provider，用 DATA frame 发送请求体；body 为空时只提交 HEADERS。
- `src/HttpRequestCommand.cc` 在 `HTTP_PROTOCOL_H2` 下创建 `Http2MultiplexExchange`、`Http2SocketCoreTransport` 和 `Http2ConnectionContext`，提交首个 stream 后调用 `DownloadEngine::registerActiveHttp2Connection()` 登记 active H2 context。
- `src/DownloadEngine.cc` 维护 active H2 context registry 与 idle H2 pool：key 由 URL 协议/host/port 加已连接的 hostname/address/port 组成；active pool 用弱引用，idle pool 用强引用持有 `Http2ConnectionContext`。exact key 未命中时，会按严格条件尝试 HTTP/2 origin coalescing。
- `src/HttpInitiateConnectionCommand.cc` 在新请求建连前先调用 `DownloadEngine::findActiveHttp2Connection()`，再调用 `DownloadEngine::popIdleHttp2Connection()`；命中后直接在既有 `Http2MultiplexExchange` 上 `submitRequest()` 创建新 stream。
- `src/Http2ResponseCommand.cc` 和 `src/Http2DownloadCommand.cc` 都按 `streamId` 取响应/正文，并在 `executeInternal()` 中调用 `exchange_->pump()` 驱动共享连接；最后一个 active stream 结束后会把连接放入 idle pool。
- `src/Http2ConnectionContext.cc` 持有 `shared_ptr<RequestGroup>`，并在构造/析构时调用 `RequestGroup::increaseStreamConnection()` / `decreaseStreamConnection()` 持有连接计数；H2 stream command 传入 `incNumConnection=false`，避免每个 stream 都重复占用普通连接计数。
- `src/TLSSession.h` 提供 `peerCertificateMatchesHostname()`；OpenSSL/GnuTLS 后端复用握手阶段的 SAN/CN 解析和 `net::verifyHostname()`，WinTLS/AppleTLS 等未实现后端默认返回 false。

限制：

- 仅 HTTPS；依赖 TLS ALPN。
- 依赖 libnghttp2 构建。
- `--enable-http-pipelining=true` 时不会向 ALPN 放入 `h2`，因此 HTTP/2 与 HTTP/1.1 pipelining 仍互斥。
- 首条 H2 连接仍要求当前下载最多 1 个 segment；`HttpRequestCommand.cc` 中如果已有多个 segment，会记录 “HTTP/2 single-stream download does not support pipelined segments. Retrying with one segment.” 并重试为单 segment。
- active registry 只保存仍有 active stream 的 H2 连接；最后一个 stream 完成后，同 origin 连接可进入 idle pool，默认保留 15 秒。
- 复用限定在同一个 `RequestGroup` 内，且必须是 HTTPS、`--enable-http2=true`、无代理或 HTTPS `CONNECT` tunnel、当前请求 0/1 segment。命令行多个流式 URL 默认通常是同一个下载项的镜像 URI；独立下载项之间即使 host 相同，也不会复用这里的 active/idle H2 context。
- same-origin 复用要求 key 完全匹配当前 URL host/port 与已连接地址信息，并通过 TLS socket reuse predicate。
- origin coalescing 只在 same-origin key 未命中后尝试，且要求当前请求不走代理、候选连接也不是代理连接、当前连接实际 peer 地址/端口等于本次目标解析出的地址/端口、目标没有显式 SNI override、当前 TLS peer certificate 覆盖目标 verify host、ALPN 已选中 `h2`。任一条件不满足就走普通新连接。
- 如果 coalesced stream 收到 HTTP 421 Misdirected Request，`HttpSkipResponseCommand.cc` 会先在当前 `RequestGroup` 里记录 `target authority + verifyHost + peer endpoint` 的负缓存，再屏蔽当前 request 后续 coalescing 并立即重试。后续同一下载任务再访问同一目标、同一校验主机、同一 peer endpoint 时，会跳过跨 origin coalescing；同 authority 的直接 H2 复用不受影响。这个负缓存只在当前 `RequestGroup` 内存里生效，不写 session，也不是全局站点黑名单。
- 普通下载链路没有用户可见的“任意 HTTP request body”命令行选项；`Http2MultiplexExchange::submitRequest(HttpRequest&)` 只从 `HttpRequest::createRequest()` 转换 headers。带 body 的 H2 发送入口是 `Http2SingleStreamExchange::submitRequest(headers, body)` / `Http2Session::submitRequest(headers, body)`，当前用户可见用途是 DoH over H2 的 DNS message POST。
- idle pool 命中前会检查 socket 仍打开、未超时、不可读；socket 可读时按保守策略视为可能 EOF/GOAWAY，直接驱逐而不复用。
- `EvictSocketPoolCommand.cc` 会随普通 socket pool 定时扫描一起调用 `DownloadEngine::evictIdleHttp2Connections()`，避免 idle H2 context 长时间强持有 `RequestGroup`。
- active stream 上限使用本地保守上限 `MAX_ACTIVE_HTTP2_STREAMS = 8` 与 peer `SETTINGS_MAX_CONCURRENT_STREAMS` 的较小值；服务端未发 SETTINGS 限制时退回本地 8 条上限。
- 首条 H2 stream 在注册 context 后会 `exchange->flushOutboundData()`；复用路径提交新 stream 后不提前 flush，交给 `Http2ResponseCommand::executeInternal()` / `exchange_->pump()` 统一驱动。
- TLS 后端必须支持 ALPN 才能真正协商 H2；`SocketCore::tlsHandshake()` 会在后端不支持 ALPN 时跳过 ALPN 设置并继续握手，最终退回 HTTP/1.1。OpenSSL 后端按 OpenSSL ALPN 宏启用；GnuTLS 后端通过 `configure.ac` 探测 `gnutls_alpn_set_protocols` / `gnutls_alpn_get_selected_protocol`，只有两者都可用才声明支持 ALPN。

XP/Win7 注意：

- HTTP/2 实际可用性取决于构建是否有 libnghttp2，以及 TLS 后端是否能发送 ALPN。旧 Windows 原生 SChannel/WinTLS 路径通常不能指望 ALPN；启用 `--enable-http2=true` 时会优雅退回 HTTP/1.1，不应因为 ALPN 缺失直接中断 HTTPS 下载。
- 需要兼容 XP/Win7 且确实要使用 H2 时，优先使用带 OpenSSL ALPN 或 GnuTLS ALPN 的构建；如果目标环境只要求能下载，HTTP/1.1 fallback 能保持可用。
- `--hosts-mapping`、`--tls-sni-host` 与 HTTP/2 可以组合，但 FakeSNI 仍受 TLS 后端 SNI override 能力限制，见 2.1。
- 当前源码没有 WinTLS/AppleTLS 的 ALPN 接线；这不是“操作系统新一点就一定能 H2”的问题，而是 `TLSSession` 后端能力声明决定的。后续若给这些后端补 ALPN，必须同步改这里。
- DoH over H2 继承 HTTP/2 的 ALPN 依赖。旧 Windows TLS 后端不能协商 `h2` 时，DoH 会继续走 HTTP/1.1 POST；这属于正常降级，不是解析器失败。

文档状态：

- `src/usage_text.h` 和 `doc/manual-src/en/aria2c.rst` 已同步为“HTTP/2 已实现但仍属实验性”的口径；如果后续实现 H3/ECH，不要复用旧的 HTTP/2 保留名描述。

示例：

```console
aria2c --enable-http2=true --enable-http-pipelining=false https://example.com/file
aria2c --enable-http2=true --log-level=network --console-log-level=network https://example.com/file
# 下面两个 URL 默认是同一个下载项的镜像 URI；不是两个独立下载。
aria2c --enable-http2=true https://example.com/file https://example.com/file?mirror=1
```

### 2.5 `network` 日志级别

`--log-level=network` 和 `--console-log-level=network` 会输出关键网络事件，覆盖 DNS、connect、TLS、HTTP、redirect 和部分网络重试。实现里可见 `A2_LOG_NETWORK(...)` 调用，例如：

- `AsyncNameResolverMan.cc`：DNS 模式、A/AAAA 家族、server 列表。
- `AsyncDotNameResolver.cc` / `AsyncDohNameResolver.cc`：DoT/DoH 连接、失败、解析结果。
- `AbstractCommand.cc`：hosts mapping、DNS cache hit、解析完成。
- `HttpRequestCommand.cc` / `HttpInitiateConnectionCommand.cc` / `DownloadEngine.cc`：HTTPS 连接建立、H2 active context 注册与复用。

### 2.6 ECH / `--enable-ech` / `--ech-config-base64`

注册路径：

- `src/OptionHandlerFactory.cc` 在 SSL 构建下把 `--enable-ech` 注册成布尔开关；无 SSL 构建仍会优雅拒绝。
- `src/OptionHandlerFactory.cc` 注册 `--ech-config-base64=BASE64`，内容是 base64 编码的二进制 ECHConfigList。
- `src/HttpTLSHandshakeParams.cc::createHttpTLSHandshakeParams()` 严格解码 base64，并把 `TLSECHParams` 填进 HTTPS TLS 握手参数。
- `src/SocketCore.cc::SocketCore::tlsHandshake()` 设置 ECHConfigList，并在 required ECH 下要求握手后 `TLSSession::getECHStatus()` 为 accepted。
- `src/LibsslTLSSession.cc` 只在 configure 探测到 OpenSSL ECH API 时调用 `SSL_set1_ech_config_list()` / `SSL_ech_get1_status()`。

语义：

- `--ech-config-base64=BASE64` 会隐式启用 required ECH；也可以显式写 `--enable-ech=true --ech-config-base64=BASE64`。
- `--enable-ech=true` 但没有 `--ech-config-base64` 会失败，不会尝试普通 TLS fallback。
- `--ech-config-base64` 必须是标准 base64，解码结果不能为空；实际 ECHConfigList 结构由 TLS 后端继续校验。
- `--tls-sni-host` 只要让 `TLSSNIHostConfig::overridden` 为 true，就不能和 ECH 混用；这包括显式单值 override，也包括 `TARGET:SNI` 映射命中。映射未命中、普通 SNI 与证书校验主机一致时不触发这个拒绝。当前阶段先避免 outer/inner SNI 语义被 FakeSNI 搅乱。
- TLS 后端不支持 ECHConfigList，或者握手成功但 ECH 没被接受，都会中止本次 HTTPS 下载。

未实现边界：

- 目前没有 HTTPS/SVCB type 65 查询、SVCB `ech` 参数解析、ECH retry config 自动重试，也不会在 DNS SVCB 完成前延迟 TLS ClientHello。
- 当前没有 H3/QUIC discovery；不要把 `--enable-ech` 理解成 HTTP/3 或 HTTPS RR 自动发现开关。
- WinTLS/AppleTLS 当前不支持 ECH；旧 Windows 需要 OpenSSL 且 configure 探测到相应 ECH API 才可能使用。

示例：

```console
aria2c --ech-config-base64=BASE64 https://origin.example/file
aria2c --enable-ech=true --ech-config-base64=BASE64 https://origin.example/file
```

### 2.7 规划中/易误写边界

这些功能不要在文档或帮助里写成已经可用：

- ECH 自动发现：当前只支持手动 `--ech-config-base64`。HTTPS/SVCB 发现、retry config 自动重试、H3 discovery 都还没实现。
- HTTP/3/H3/QUIC：当前有 `--enable-http3[=false]` 禁用壳，只允许默认 `false`；设置为 `true` 会在参数解析阶段失败。源码仍然没有 QUIC 传输层、HTTP/3 request/response command、H3 ALPN 分发或依赖探测，这个参数不表示下载链路已支持 H3，也不要把 `h3` 写进 HTTP 下载 ALPN 列表。
- DoH over H2：这是条件可用能力，不是 H3/ECH 占位。`AsyncDohNameResolver` 会在 `HAVE_LIBNGHTTP2`、`--enable-http2=true` 且 `--enable-http-pipelining=false` 时尝试 ALPN `h2,http/1.1`；TLS 未选中 `h2` 时仍按 HTTP/1.1 POST `application/dns-message` 工作。
- WinTLS/AppleTLS 上的 FakeSNI override：普通 SNI 可用，但 SNI 与证书校验 hostname 不同会被提前拒绝。
- WinTLS/AppleTLS 当前代码里的 HTTP/2 ALPN：没有 ALPN 接口时会降级 HTTP/1.1；GnuTLS 只有在 configure 探测到 ALPN API 时才启用。

### 2.8 XP/Win7 兼容边界

- 旧 Windows 能不能用这些新网络能力，首先取决于构建使用的 TLS 后端和依赖库，不是单看命令行参数名。
- XP/Win7 的安全底线应按“HTTP/1.1 能正常回退、IPv4 可用、证书校验可用”来设计；HTTP/2、FakeSNI override、DoT/DoH 属于依赖构建能力的增强项，不要把它们写成旧系统必然可用。
- HTTP/2 需要 libnghttp2、HTTPS、TLS ALPN 三个条件同时满足。缺 libnghttp2 时 `--enable-http2=true` 会被拒绝；TLS 后端不支持 ALPN 时，`SocketCore::tlsHandshake()` 会清空 ALPN 并继续握手，最终走 HTTP/1.1。
- ECH 需要 OpenSSL ECH API 和手动 ECHConfigList；WinTLS/AppleTLS 当前不支持 ECH。旧系统如果启用但 TLS 后端不支持，会以明确错误失败，不会崩溃。
- FakeSNI override 需要 TLS 后端允许“发送的 SNI”和“证书校验主机”不同。OpenSSL/GnuTLS 当前源码声明支持；WinTLS/AppleTLS 没声明，遇到 override 会提前失败。普通 SNI 与证书校验主机一致时不属于 FakeSNI override。
- DoT/DoH 依赖 SSL 构建和可用的数值 DNS server。老系统如果 IPv6 支持不稳，建议显式 `--disable-ipv6=true`，避免 AAAA 查询或 IPv6 server 连接拖慢。
- 32-bit MinGW 构建中 `OptionHandlerFactory.cc` 默认把 `--disable-ipv6` 设为 `true`，因为 Vista 前后的 IPv6 API 可用性差异容易踩坑；其他构建通常默认 `false`。单一基线里要把这个当作构建差异处理，不要在运行期假设 IPv6 必然可用。
- 默认最低 TLS 版本是 `TLSv1.2`。为了兼容旧系统降低 TLS 版本只适合受控环境；公网下载优先换带 OpenSSL/GnuTLS 的构建，比关证书校验靠谱得多。
- 没有异步 DNS 的构建不存在 `--async-dns-mode=doh|dot` 这条路；无 SSL 构建即使有异步 DNS 也只接受 `cares`。旧系统兼容文档里要把这点写死，别让用户以为 DoH/DoT 会自动降级成普通 DNS。

## 3. 选项总览

### 3.1 Basic Options

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `-d, --dir=<DIR>` | 当前目录 | 下载文件保存目录。 |
| `-i, --input-file=<FILE>` | 无 | 从文件读取 URI；同一下载项的附加选项可写在后续缩进行。 |
| `-l, --log=<LOG>` | 无 | 写日志到文件；`-` 表示标准输出。 |
| `-j, --max-concurrent-downloads=<N>` | `5` | 最大并行下载任务数。 |
| `-V, --check-integrity [true\|false]` | `false` | 下载后校验 piece hash 或 `--checksum`。 |
| `-c, --continue [true\|false]` | `false` | 继续未完成下载。 |
| `-h, --help[=<TAG>\|<KEYWORD>]` | 无 | 显示帮助；可按 tag 或关键字筛选。 |
| `-v, --version` | 无 | 显示版本并退出。 |

### 3.2 HTTP/FTP/SFTP 通用选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--all-proxy=<PROXY>` | 无 | 所有协议通用代理；可用空值覆盖环境变量。 |
| `--all-proxy-user=<USER>` | 无 | all-proxy 用户名。 |
| `--all-proxy-passwd=<PASSWD>` | 无 | all-proxy 密码。 |
| `--http-proxy=<PROXY>` | 无 | HTTP 代理。 |
| `--http-proxy-user=<USER>` | 无 | HTTP 代理用户名。 |
| `--http-proxy-passwd=<PASSWD>` | 无 | HTTP 代理密码。 |
| `--https-proxy=<PROXY>` | 无 | HTTPS 代理。 |
| `--https-proxy-user=<USER>` | 无 | HTTPS 代理用户名。 |
| `--https-proxy-passwd=<PASSWD>` | 无 | HTTPS 代理密码。 |
| `--ftp-proxy=<PROXY>` | 无 | FTP 代理。 |
| `--ftp-proxy-user=<USER>` | 无 | FTP 代理用户名。 |
| `--ftp-proxy-passwd=<PASSWD>` | 无 | FTP 代理密码。 |
| `--no-proxy=<DOMAINS>` | 无 | 逗号分隔主机、域、网络地址/CIDR，不走代理。 |
| `--proxy-method=<METHOD>` | `get` | 代理请求方式：`get` 或 `tunnel`。 |
| `--connect-timeout=<SEC>` | `30` | 建连超时。 |
| `-t, --timeout=<SEC>` | `60` | 读写超时。 |
| `--lowest-speed-limit=<SPEED>` | `0` | 低于速度阈值持续超时则断开。 |
| `-m, --max-tries=<N>` | `5` | 最大尝试次数；`0` 表示无限。 |
| `--retry-wait=<SEC>` | `2` | 重试间隔秒数。 |
| `--max-file-not-found=<NUM>` | `0` | 404/文件不存在达到次数后失败。 |
| `--checksum=<TYPE>=<DIGEST>` | 无 | 指定整文件校验值，例如 `sha-256=...`。 |
| `-o, --out=<FILE>` | 原文件名 | 输出文件名。 |
| `--remote-time [true\|false]` | `false` | 使用远端文件时间戳。 |
| `--reuse-uri [true\|false]` | `true` | 没有未用 URI 时复用已用 URI。 |
| `--server-stat-of=<FILE>` | 无 | 保存 server 性能统计。 |
| `--server-stat-if=<FILE>` | 无 | 读取 server 性能统计。 |
| `--server-stat-timeout=<SEC>` | `86400` | server 性能统计过期时间。 |
| `-s, --split=<N>` | `16` | 单文件分片下载连接数。 |
| `-x, --max-connection-per-server=<NUM>` | `1` | 每服务器最大连接数。 |
| `-k, --min-split-size=<SIZE>` | `2M` | 小于 `2*SIZE` 的范围不继续拆分。 |
| `--stream-piece-selector=<SELECTOR>` | `default` | piece 选择：`default`、`inorder`、`random`、`geom`。 |
| `--uri-selector=<SELECTOR>` | `feedback` | URI 选择：`inorder`、`feedback`、`adaptive`。 |
| `--dry-run [true\|false]` | `false` | 只检查远端资源，不实际下载。 |
| `--netrc-path=<FILE>` | `~/.netrc` | netrc 文件路径。 |
| `-n, --no-netrc [true\|false]` | `false` | 禁用 netrc。 |

### 3.3 TLS/HTTP 新增和证书选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--ca-certificate=<FILE>` | 构建默认 CA | 指定 CA 证书文件，PEM 可包含多个证书。 |
| `--certificate=<FILE>` | 无 | 客户端证书。AppleTLS 可用 Keychain SHA-1 指纹。 |
| `--private-key=<FILE>` | 无 | 客户端私钥。 |
| `--check-certificate [true\|false]` | `true` | 校验证书。 |
| `--min-tls-version=<VERSION>` | `TLSv1.2` | 最低 TLS 版本。 |
| `--tls-sni-host=<HOST\|TARGET:SNI[,TARGET:SNI]...>` | 无 | FakeSNI / SNI 映射，详见 2.1。 |
| `--enable-ech [true\|false]` | `false` | 启用 required ECH；必须配 `--ech-config-base64`。 |
| `--ech-config-base64=<BASE64>` | 无 | base64 编码的二进制 ECHConfigList；会隐式启用 required ECH。 |
| `--enable-http2 [false]` | `false` | 实验性 HTTP/2；需要 libnghttp2、HTTPS、ALPN，详见 2.4。 |
| `--enable-http3 [false]` | `false` | HTTP/3 over QUIC 保留名；当前未实现，设为 true 会失败。 |
| `--hosts-mapping=<HOST:IPADDR[,IPADDR:HOST]...>` | 无 | hosts 映射，详见 2.2。 |

### 3.4 HTTP 专用选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--http-accept-gzip [true\|false]` | `false` | 请求 gzip/deflate 响应并自动解压。 |
| `--http-auth-challenge [true\|false]` | `false` | 等服务器认证挑战后再发认证头。 |
| `--http-no-cache [true\|false]` | `false` | 发送 `Cache-Control: no-cache` 和 `Pragma: no-cache`。 |
| `--http-user=<USER>` | 无 | HTTP 认证用户名。 |
| `--http-passwd=<PASSWD>` | 无 | HTTP 认证密码。 |
| `--referer=<REFERER>` | 无 | 设置 HTTP Referer。 |
| `--enable-http-keep-alive [true\|false]` | `true` | 启用 HTTP/1.1 持久连接。 |
| `--enable-http-pipelining [true\|false]` | `false` | 启用 HTTP/1.1 pipelining；与当前 HTTP/2 路径互斥。 |
| `--header=<HEADER>` | 可重复 | 附加 HTTP 请求头，例如 `--header="X-A: b"`。 |
| `--load-cookies=<FILE>` | 无 | 从 Firefox3/cookie 文件加载 cookie。 |
| `--save-cookies=<FILE>` | 无 | 退出时保存 cookie。 |
| `--use-head [true\|false]` | `false` | 首个请求使用 HEAD。 |
| `--no-want-digest-header [true\|false]` | `false` | 禁用 `Want-Digest` 请求头。 |
| `--content-disposition-default-utf8 [true\|false]` | `false` | 将 Content-Disposition quoted-string 默认按 UTF-8 处理。 |
| `-U, --user-agent=<USER_AGENT>` | `aria2/<version>` | 设置 HTTP(S) User-Agent。 |
| `--max-http-pipelining=<NUM>` | `2` | 源码注册项：最大 HTTP pipelining 请求数。 |

### 3.5 FTP/SFTP 专用选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--ftp-user=<USER>` | `anonymous` | FTP 用户名。 |
| `--ftp-passwd=<PASSWD>` | `ARIA2USER@` | FTP 密码。 |
| `-p, --ftp-pasv [true\|false]` | `true` | FTP 被动模式。 |
| `--ftp-type=<TYPE>` | `binary` | FTP 传输类型：`binary` 或 `ascii`。 |
| `--ftp-reuse-connection [true\|false]` | `true` | 复用 FTP 连接。 |
| `--ssh-host-key-md=<TYPE>=<DIGEST>` | 无 | SFTP 主机密钥指纹，支持 `sha-1`、`md5`。 |

### 3.6 BitTorrent/Metalink 通用选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--select-file=<INDEX>...` | 无 | 选择 torrent/metalink 内文件，支持范围和逗号。 |
| `-S, --show-files [true\|false]` | `false` | 显示 torrent/metalink 文件列表。 |
| `-O, --index-out=<INDEX>=<PATH>` | 无 | 按索引指定输出路径，可重复。 |

### 3.7 BitTorrent 专用选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--bt-detach-seed-only [true\|false]` | `false` | 完成后仅做种任务从 active list 分离。 |
| `--bt-enable-hook-after-hash-check [true\|false]` | `true` | hash 检查后允许触发 hook。 |
| `--bt-enable-lpd [true\|false]` | `false` | 启用 Local Peer Discovery。 |
| `--bt-lpd-interface=<INTERFACE>` | 无 | LPD 使用的接口。 |
| `--bt-exclude-tracker=<URI>[,...]` | 无 | 排除 tracker。 |
| `--bt-tracker=<URI>[,...]` | 无 | 追加 tracker。 |
| `--bt-external-ip=<IPADDRESS>` | 无 | 报告给 tracker/DHT 的外部 IP。 |
| `--bt-force-encryption [true\|false]` | `false` | 需要加密，等价组合见 manual。 |
| `--bt-require-crypto [true\|false]` | `false` | 只接受加密连接。 |
| `--bt-min-crypto-level=plain\|arc4` | `plain` | 最低加密级别。 |
| `--bt-hash-check-seed [true\|false]` | `true` | 完成文件 hash 检查后继续做种。 |
| `--bt-load-saved-metadata [true\|false]` | `false` | 使用已保存 `.torrent` 元数据。 |
| `--bt-max-open-files=<NUM>` | `100` | 每个 BT 下载最多打开文件数。 |
| `--bt-max-peers=<NUM>` | `55` | 每个 torrent 最大 peer 数。 |
| `--bt-metadata-only [true\|false]` | `false` | 只下载元数据。 |
| `--bt-prioritize-piece=head[=<SIZE>],tail[=<SIZE>]` | 无 | 优先下载头/尾 piece。 |
| `--bt-remove-unselected-file [true\|false]` | `false` | 下载完成后删除未选择文件。 |
| `--bt-request-peer-speed-limit=<SPEED>` | `50K` | 当整体下载速度低于该值时向更多 peer 请求。 |
| `--bt-save-metadata [true\|false]` | `false` | 保存磁力链接元数据为 `.torrent`。 |
| `--bt-seed-unverified [true\|false]` | `false` | 不校验也做种已有文件。 |
| `--bt-stop-timeout=<SEC>` | `0` | 下载速度为 0 持续多久后停止 BT。 |
| `--bt-tracker-connect-timeout=<SEC>` | `60` | tracker 建连超时。 |
| `--bt-tracker-interval=<SEC>` | `0` | tracker 请求间隔；`0` 表示按 tracker 响应。 |
| `--bt-tracker-timeout=<SEC>` | `60` | tracker 超时。 |
| `--bt-keep-alive-interval=<SEC>` | `120` | 源码注册项：BT keep-alive 间隔。 |
| `--bt-request-timeout=<SEC>` | `60` | 源码注册项：BT 请求超时。 |
| `--bt-timeout=<SEC>` | `60` | 源码注册项：BT 连接/消息超时。 |
| `--dht-entry-point=<HOST>:<PORT>` | 无 | IPv4 DHT 入口。 |
| `--dht-entry-point6=<HOST>:<PORT>` | 无 | IPv6 DHT 入口。 |
| `--dht-file-path=<PATH>` | 平台默认 | IPv4 DHT 路由表文件。 |
| `--dht-file-path6=<PATH>` | 平台默认 | IPv6 DHT 路由表文件。 |
| `--dht-listen-addr6=<ADDR>` | 无 | IPv6 DHT 绑定地址。 |
| `--dht-listen-addr=<ADDR>` | 无 | 源码注册项：IPv4 DHT 绑定地址。 |
| `--dht-listen-port=<PORT>...` | `6881-6999` | DHT/UDP tracker 监听端口。 |
| `--dht-message-timeout=<SEC>` | `10` | DHT 消息超时。 |
| `--enable-dht [true\|false]` | `true` | 启用 IPv4 DHT。 |
| `--enable-dht6 [true\|false]` | `false` | 启用 IPv6 DHT。 |
| `--enable-peer-exchange [true\|false]` | `true` | 启用 Peer Exchange。 |
| `--follow-torrent=true\|false\|mem` | `true` | 下载到 `.torrent` 后是否继续解析下载。 |
| `--listen-port=<PORT>...` | `6881-6999` | BT TCP 监听端口。 |
| `--max-overall-upload-limit=<SPEED>` | `0` | 全局上传限速。 |
| `-u, --max-upload-limit=<SPEED>` | `0` | 每个 torrent 上传限速。 |
| `--on-bt-download-complete=<COMMAND>` | 无 | BT 完成后执行命令。 |
| `--peer-connection-timeout=<SEC>` | `60` | 源码注册项：peer 建连超时。 |
| `--peer-id-prefix=<PEER_ID_PREFIX>` | `A2-...` | peer id 前缀。 |
| `--peer-agent=<PEER_AGENT>` | `aria2/...` | 扩展握手报告客户端名。 |
| `--seed-ratio=<RATIO>` | `1.0` | 分享率达到后停止做种。 |
| `--seed-time=<MINUTES>` | 无 | 做种时间。 |
| `-T, --torrent-file=<TORRENT_FILE>` | 无 | 指定 `.torrent` 文件。 |

### 3.8 Metalink 专用选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--follow-metalink=true\|false\|mem` | `true` | 下载到 metalink 后是否继续解析下载。 |
| `--metalink-base-uri=<URI>` | 无 | 解析相对 URI 的 base URI。 |
| `-M, --metalink-file=<METALINK_FILE>` | 无 | 指定 `.meta4` / `.metalink` 文件。 |
| `--metalink-language=<LANGUAGE>` | 无 | 按语言筛选。 |
| `--metalink-location=<LOCATION>[,...]` | 无 | 按镜像位置筛选。 |
| `--metalink-os=<OS>` | 无 | 按系统筛选。 |
| `--metalink-version=<VERSION>` | 无 | 按版本筛选。 |
| `--metalink-preferred-protocol=<PROTO>` | `none` | 首选协议：`http`、`https`、`ftp`、`none`。 |
| `--metalink-enable-unique-protocol [true\|false]` | `true` | 同一协议只用一个 mirror。 |

### 3.9 RPC 选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--enable-rpc [true\|false]` | `false` | 启用 JSON-RPC/XML-RPC server。 |
| `--pause [true\|false]` | `false` | 添加后暂停；仅 RPC 启用时有效。 |
| `--pause-metadata [true\|false]` | `false` | 暂停 metadata 下载；仅 RPC 启用时有效。 |
| `--rpc-allow-origin-all [true\|false]` | `false` | 添加 `Access-Control-Allow-Origin: *`。 |
| `--rpc-certificate=<FILE>` | 无 | RPC TLS 证书。 |
| `--rpc-private-key=<FILE>` | 无 | RPC TLS 私钥。 |
| `--rpc-secure [true\|false]` | `false` | RPC 使用 TLS。 |
| `--rpc-listen-all [true\|false]` | `false` | 监听所有网络接口；否则只监听本地。 |
| `--rpc-listen-port=<PORT>` | `6800` | RPC 监听端口。 |
| `--rpc-max-request-size=<SIZE>` | `2M` | RPC 最大请求大小。 |
| `--rpc-save-upload-metadata [true\|false]` | `true` | 保存上传的 torrent/metalink 元数据。 |
| `--rpc-secret=<TOKEN>` | 无 | RPC token，推荐替代旧 user/passwd。 |
| `--rpc-user=<USER>` | 无 | 已废弃 RPC 用户名。 |
| `--rpc-passwd=<PASSWD>` | 无 | 已废弃 RPC 密码。 |

### 3.10 Advanced Options

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `--allow-overwrite [true\|false]` | `false` | 允许覆盖已有文件并从头下载。 |
| `--allow-piece-length-change [true\|false]` | `false` | 允许 piece length 改变继续下载。 |
| `--always-resume [true\|false]` | `true` | 总是尝试断点续传；失败次数受 `--max-resume-failure-tries` 影响。 |
| `--async-dns [true\|false]` | 非 Android 默认 `true`，Android 默认 `false` | 启用异步 DNS。 |
| `--async-dns-mode=<cares\|dot\|doh>` | `cares` | 异步 DNS 后端，详见 2.3。 |
| `--async-dns-server=<SERVER>[,...]` | cares 读系统配置；DoT/DoH 必填 | 指定异步 DNS server，格式随 mode 变化；DoT/DoH 的数值地址可加 `#TLS_HOST`。 |
| `--enable-async-dns6 [true\|false]` | 已废弃 | 旧 IPv6 异步 DNS 开关；当前使用 `--disable-ipv6` 控制。 |
| `--auto-file-renaming [true\|false]` | `true` | 同名文件自动追加 `.1` 到 `.9999`。 |
| `--auto-save-interval=<SEC>` | `60` | 定期保存 `.aria2` 控制文件。 |
| `--conditional-get [true\|false]` | `false` | 本地文件较旧时才下载。 |
| `--conf-path=<PATH>` | 平台默认 | 指定配置文件路径。 |
| `--no-conf [true\|false]` | `false` | 不读取配置文件。 |
| `--console-log-level=<LEVEL>` | `notice` | 控制台日志级别：`debug`、`info`、`notice`、`warn`、`error`、`network`。 |
| `--log-level=<LEVEL>` | `debug` | 文件日志级别，支持 `network`。 |
| `-D, --daemon [true\|false]` | `false` | 以 daemon 运行。 |
| `--deferred-input [true\|false]` | `false` | 延迟读取 input-file，适合超大列表。 |
| `--disable-ipv6 [true\|false]` | Windows 旧构建可能默认 `true`，通常 `false` | 禁用 IPv6 和 AAAA 查询。 |
| `--disk-cache=<SIZE>` | `16M` | 磁盘缓存；`0` 禁用。 |
| `--download-result=<OPT>` | `default` | 下载结果显示方式：`default`、`full`、`hide`。 |
| `--dscp=<DSCP>` | `0` | 出站 IP 包 DSCP。 |
| `--rlimit-nofile=<NUM>` | 无 | 设置打开文件数软限制。 |
| `--enable-color [true\|false]` | `true` | 终端彩色输出。 |
| `--enable-mmap [true\|false]` | `false` | 对文件启用 mmap。 |
| `--event-poll=<POLL>` | 平台默认 | 事件轮询后端，如 `epoll`、`kqueue`、`port`、`poll`、`select`、`libuv`。 |
| `--file-allocation=<METHOD>` | `prealloc` | 文件预分配：`none`、`prealloc`、`trunc`、`falloc`。 |
| `--no-file-allocation-limit=<SIZE>` | `5M` | 小于该大小不预分配。 |
| `--force-save [true\|false]` | `false` | 即使完成也写入 session。 |
| `--save-not-found [true\|false]` | `true` | 文件不存在错误也写入 session。 |
| `--gid=<GID>` | 自动生成 | 手动指定下载 GID。 |
| `--hash-check-only [true\|false]` | `false` | 只做 hash 检查，不继续下载。 |
| `--human-readable [true\|false]` | `true` | 人类可读大小/速度。 |
| `--interface=<INTERFACE>` | 无 | 绑定本地接口/IP/主机。 |
| `--multiple-interface=<INTERFACES>` | 无 | 多接口轮换绑定；会覆盖 `--interface`。 |
| `--keep-unfinished-download-result [true\|false]` | `true` | 保留未完成结果，即使超过 `--max-download-result`。 |
| `--max-download-result=<NUM>` | `1000` | 内存中保留的下载结果数。 |
| `--max-mmap-limit=<SIZE>` | `9223372036854775807` | mmap 最大文件大小。 |
| `--max-resume-failure-tries=<N>` | `0` | `--always-resume=false` 时最大续传失败次数。 |
| `--max-overall-download-limit=<SPEED>` | `0` | 全局下载限速。 |
| `--max-download-limit=<SPEED>` | `0` | 单任务下载限速。 |
| `--on-download-start=<COMMAND>` | 无 | 下载开始 hook。 |
| `--on-download-pause=<COMMAND>` | 无 | 下载暂停 hook。 |
| `--on-download-stop=<COMMAND>` | 无 | 下载停止 hook。 |
| `--on-download-complete=<COMMAND>` | 无 | 下载完成 hook。 |
| `--on-download-error=<COMMAND>` | 无 | 下载错误 hook。 |
| `--optimize-concurrent-downloads [true\|false\|<A>:<B>]` | `false` | 根据速度自动调整并发下载数。 |
| `--piece-length=<LENGTH>` | `4K` | HTTP/FTP piece 长度。 |
| `--show-console-readout [true\|false]` | `true` | 显示控制台进度。 |
| `--stderr [true\|false]` | `false` | 控制台输出重定向到 stderr。 |
| `--summary-interval=<SEC>` | `60` | 进度摘要输出间隔；`0` 禁用。 |
| `-Z, --force-sequential [true\|false]` | `false` | 命令行 URI 顺序下载。 |
| `-P, --parameterized-uri [true\|false]` | `false` | 启用参数化 URI，如 `http://{a,b}/file`。 |
| `-q, --quiet [true\|false]` | `false` | 静默输出。 |
| `--realtime-chunk-checksum [true\|false]` | `true` | 实时校验 chunk。 |
| `--remove-control-file [true\|false]` | `false` | 下载前删除控制文件；配合 overwrite 会从头下。 |
| `--save-session=<FILE>` | 无 | 退出时保存未完成/错误下载。 |
| `--save-session-interval=<SEC>` | `0` | 定期保存 session；`0` 禁用。 |
| `--socket-recv-buffer-size=<SIZE>` | `0` | 设置 socket 接收缓冲区。 |
| `--stop=<SEC>` | `0` | 指定秒数后停止程序。 |
| `--stop-with-process=<PID>` | 无 | 指定进程不存在时停止 aria2。 |
| `--truncate-console-readout [true\|false]` | `true` | 根据终端宽度截断进度输出。 |

## 4. 源码注册但 manual/usage 差异较大的选项

这些选项在 `OptionHandlerFactory.cc` 或 `usage_text.h` 里能看到，但英文 manual 未必有独立完整章节，主线程后续最好再核对：

| 选项 | 来源 | 当前判断 |
| --- | --- | --- |
| `--dns-timeout=<SEC>` | `OptionHandlerFactory.cc`，`NO_DESCRIPTION` | 注册为数字项，默认 `30`，范围 `1..60`；manual 未列出独立说明。 |
| `--startup-idle-time=<SEC>` | `OptionHandlerFactory.cc` | 注册为数字项；manual 未列出独立说明。 |
| `--select-least-used-host [true\|false]` | `OptionHandlerFactory.cc` | 注册项；需主线程确认当前 URI/host 选择语义是否仍暴露给用户。 |
| `--max-http-pipelining=<NUM>` | `OptionHandlerFactory.cc` | 注册项，和 HTTP/1.1 pipelining 相关；manual 未在当前提取列表中出现。 |
| `--dht-listen-addr=<ADDR>` | `OptionHandlerFactory.cc`，`NO_DESCRIPTION` | IPv4 DHT 绑定地址；manual 只详细列了 IPv6 的 `--dht-listen-addr6`。 |
| `--bt-keep-alive-interval=<SEC>` | `OptionHandlerFactory.cc` | 注册项，默认 `120`。 |
| `--bt-request-timeout=<SEC>` | `OptionHandlerFactory.cc` | 注册项。 |
| `--bt-timeout=<SEC>` | `OptionHandlerFactory.cc`，`NO_DESCRIPTION` | 注册项，默认 `60`。 |
| `--peer-connection-timeout=<SEC>` | `OptionHandlerFactory.cc` | 注册项。 |
| `--enable-direct-io [true\|false]` | `usage_text.h` 仅有 `TEXT_ENABLE_DIRECT_IO` | 当前未在 `OptionHandlerFactory.cc` 看到注册，不应当作为可用选项写进正式 help，需主线程确认是否死文案。 |
| `--metalink-servers=<NUM_SERVERS>` | `usage_text.h` 旧文案 | 当前 `OptionHandlerFactory.cc` 没看到对应注册，manual 当前提取列表也没有独立 option。 |

## 5. 常用配置示例

### 5.1 断点续传和并发

```ini
continue=true
max-concurrent-downloads=4
split=8
max-connection-per-server=8
min-split-size=4M
auto-file-renaming=true
```

### 5.2 FakeSNI 加 hosts mapping

```console
aria2c ^
  --hosts-mapping=origin.example:198.18.0.18 ^
  --tls-sni-host=origin.example:front.example ^
  https://origin.example/file
```

注意：这会连接 `198.18.0.18`，HTTP `Host` 和证书校验仍是 `origin.example`，SNI 改成 `front.example`。WinTLS 这类不支持 SNI override 的后端会失败。

### 5.3 IP URL 但保留逻辑主机

```console
aria2c --hosts-mapping=198.18.0.18:origin.example https://198.18.0.18/file
```

这会连接 IP，但 HTTP/TLS 逻辑主机使用 `origin.example`。对 WinTLS/旧 Windows 更友好，因为 SNI 与证书校验主机一致。

### 5.4 DoT / DoH

```console
aria2c --async-dns=true --async-dns-mode=dot --async-dns-server=1.1.1.1,[2606:4700:4700::1111]:853 https://example.com/file
aria2c --async-dns=true --async-dns-mode=doh --async-dns-server=https://1.1.1.1/dns-query https://example.com/file
aria2c --async-dns=true --async-dns-mode=dot --async-dns-server=1.1.1.1#cloudflare-dns.com https://example.com/file
aria2c --async-dns=true --async-dns-mode=doh --async-dns-server=https://1.1.1.1/dns-query#cloudflare-dns.com https://example.com/file
aria2c --enable-http2=true --enable-http-pipelining=false --async-dns=true --async-dns-mode=doh --async-dns-server=https://1.1.1.1/dns-query#cloudflare-dns.com https://example.com/file
```

没有 `--async-dns-over-https` / `--async-dns-over-tls` 独立开关，别在配置文件里写这两个名字。
最后一条会让 DoH 先尝试 HTTP/2；如果构建、TLS ALPN 或服务端不满足条件，会正常回退到 HTTP/1.1 DoH。

### 5.5 双栈 DNS 和连接竞速

```console
aria2c --async-dns=true --disable-ipv6=false --console-log-level=network https://example.com/file
aria2c --async-dns=true --disable-ipv6=false -s 16 -x 16 --console-log-level=network https://example.com/file
```

这会在双栈可用时并发启动 A/AAAA 查询，任一地址族先成功就先建连；后台拿到另一族新地址后会写入 DNS cache 并唤醒后续连接。已有 IPv4/IPv6 两族地址时，异步 DNS 路径会把 opposite-family 备份连接延迟阈值降为 `0ms`，方便主/备份连接尽早竞速；`--disable-ipv6=true` 或无异步 DNS 构建仍保持保守路径。
真正让同一下载任务长期同时使用 IPv4/IPv6 多条连接，还需要 DNS cache 里已有两族地址、文件可分片、`--split>=2`，并且同一 host 的 `--max-connection-per-server` 足够大，例如第二条命令里的 `-x 16`。backup connection 是主/备份竞速，胜者接管 socket，不能等同于长期两条下载流。

### 5.6 网络调试日志

```console
aria2c --log=- --log-level=network --console-log-level=network https://example.com/file
```

## 6. 待补齐/确认

- HTTP/2 目前已有同 origin active/idle 复用、保守 origin coalescing、421 负缓存，以及 DoH over H2 请求体发送说明；后续阶段还要补真实网络端到端测试，并继续完善错误恢复、Range/redirect 行为。
- IPv4/IPv6 目前已有第一阶段双栈竞速：A/AAAA 并发解析、后台 DNS cache 补齐唤醒、异步 DNS 下 `0ms` opposite-family 备份连接。后续若要更完整贴近 RFC 8305，还应实现按地址列表交错排序、连接尝试取消/统计更细粒度、DNS cache TTL 和坏地址恢复策略。
- H3/QUIC 当前只有 `--enable-http3[=false]` 禁用壳和文档边界；后续应先做依赖探测与能力矩阵，不能直接把 `h3` ALPN 写进下载链路。
- `--select-least-used-host`、`--dns-timeout`、`--startup-idle-time`、`--max-http-pipelining`、`--bt-keep-alive-interval`、`--bt-request-timeout`、`--bt-timeout`、`--peer-connection-timeout` 等源码注册项需要确认是否正式对用户公开，还是只用于内部/隐藏帮助。
- `usage_text.h` 的 `--enable-direct-io` 和旧 `--metalink-servers` 文案未在当前 `OptionHandlerFactory.cc` 注册列表中确认到，需要清理或补注册。
- 后续如果给 WinTLS/AppleTLS 补 ALPN 或调整 SNI override 能力，需要同步更新 2.0 能力矩阵和 XP/Win7 边界；当前 OpenSSL 后端有 ALPN 与 SNI override，GnuTLS 有 SNI override 且在探测到 ALPN API 时支持 ALPN，WinTLS 未声明 SNI override，基类 ALPN 默认不支持并会触发 HTTP/1.1 fallback。
