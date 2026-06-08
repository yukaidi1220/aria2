# aria2c 命令行帮助中文草稿

> 状态：当前分支草稿。本文只整理命令行/配置项说明，不修改源码。
>
> 主要来源：`src/OptionHandlerFactory.cc::OptionHandlerFactory::createOptionHandlers()`、`src/usage_text.h`、`doc/manual-src/en/aria2c.rst`。新增网络能力额外核对了 `src/TLSSNIHostMapping.cc`、`src/HostMapping.cc`、`src/HttpTLSHandshakeParams.cc`、`src/SocketCore.cc`、`src/AsyncNameResolverMan.cc`、`src/AsyncDnsServerConfig.cc`、`src/AsyncDotNameResolver.cc`、`src/AsyncDohNameResolver.cc`、`src/HttpProtocol.cc`、`src/HttpRequestCommand.cc` 等实现文件。

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

## 2. 新增/重点网络能力

### 2.1 FakeSNI / `--tls-sni-host`

注册路径：

- `src/OptionHandlerFactory.cc` 注册 `PREF_TLS_SNI_HOST`，参数格式为 `HOST or TARGET:SNI[,TARGET:SNI]...`。
- `src/TLSSNIHostMapping.cc::getTLSSNIHostConfig()` 解析单一 SNI 或映射表。
- `src/HttpTLSHandshakeParams.cc::createHttpTLSHandshakeParams()` 计算 `sniHost`、证书校验主机 `verifyHost` 和 ALPN 列表。
- `src/SocketCore.cc::SocketCore::tlsConnect()` 校验 SNI 主机格式，调用 `TLSSession::setSNIHostname()`。

语义：

- `--tls-sni-host=front.example`：所有 HTTPS 请求的 TLS ClientHello SNI 都使用 `front.example`。
- `--tls-sni-host=origin.example:front.example,redirect.example:redirect-front.example`：优先按当前请求 host 匹配；没有命中时，再按逻辑/默认 host 回退匹配。重定向后的请求会重新计算这两个 host。
- `TARGET` 可以是 DNS 主机名、IPv4 地址或 `[IPv6]`。IPv6 映射必须加方括号，例如 `[2001:db8::1]:front.example`。
- `SNI` 必须是可用于 TLS SNI 的 DNS 主机名；`isTLSSNIHostname()` 会拒绝 IP、`localhost`、单标签名、空标签、下划线、标签首尾 `-` 等。
- 同一个 `TARGET` 如果写了多次，`getTLSSNIHostConfig()` 会保留最先命中的配置；当前请求 host 的匹配优先级高于逻辑/默认 host 的匹配。
- 这个选项只改 TLS SNI，不改 DNS 解析、TCP 连接目标、HTTP `Host` 头、代理 `CONNECT` 目标、cookie 作用域、证书校验主机。

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

### 2.3 DoH/DoT / 异步 DNS

注册路径：

- `src/OptionHandlerFactory.cc` 在 `ENABLE_ASYNC_DNS` 下注册 `--async-dns`、`--async-dns-mode`、`--async-dns-server`。
- `--async-dns-mode` 在 `ENABLE_SSL` 下允许 `cares|dot|doh`；无 SSL 构建只允许 `cares`。
- `src/AsyncNameResolverMan.cc::resolverModeFromOption()` 选择后端。
- `src/AsyncNameResolverMan.cc::validateAsyncNameResolverConfig()` 在解析期校验后端配置；DoT/DoH 模式没有 server 会直接失败。
- `src/AsyncNameResolverMan.cc::createResolver()` 创建 `AsyncNameResolver`、`AsyncDotNameResolver` 或 `AsyncDohNameResolver`。
- `src/AsyncDnsServerConfig.cc` 解析和校验 DoT/DoH server 格式。
- `src/AsyncDotNameResolver.cc` / `src/AsyncDohNameResolver.cc` 驱动网络状态机并写 `A2_LOG_NETWORK`。

选项：

```console
aria2c --async-dns=true --async-dns-mode=cares
aria2c --async-dns=true --async-dns-mode=dot --async-dns-server=1.1.1.1,8.8.8.8:853
aria2c --async-dns=true --async-dns-mode=dot --async-dns-server=[2606:4700:4700::1111]:853
aria2c --async-dns=true --async-dns-mode=doh --async-dns-server=https://1.1.1.1/dns-query
aria2c --async-dns=true --async-dns-mode=doh --async-dns-server=https://[2606:4700:4700::1111]/dns-query
```

DoT 规则：

- 格式为 `IP`、`IP:PORT`、`[IPv6]`、`[IPv6]:PORT`，默认端口 `853`。
- `--async-dns-mode=dot` 必须显式提供 `--async-dns-server`；为空会报 “No async DNS DoT server configured”。
- `AsyncDnsServerConfig.cc::validateAsyncDnsDotServerConfigForDirectConnect()` 要求 server host 是数值地址，避免解析 DNS 服务器本身时递归套娃。
- `AsyncDotNameResolver::createTLSHandshakeParams()` 使用 server 的 TLS host；数值 IP 默认不会形成普通 DNS SNI。

DoH 规则：

- 格式必须是数值 HTTPS URL，例如 `https://1.1.1.1/dns-query` 或 `https://[2606:4700:4700::1111]/dns-query`，默认端口 `443`。
- `--async-dns-mode=doh` 必须显式提供 `--async-dns-server`；为空会报 “No async DNS DoH server configured”。
- URL 必须有 path，拒绝 userinfo、密码、fragment；query 允许作为 path 的一部分发送。
- `AsyncDohNameResolver.cc::createDohRequest()` 发送 HTTP/1.1 `POST`，`Accept: application/dns-message`、`Content-Type: application/dns-message`、`Connection: close`。
- 当前 DoH resolver 自己不启用 HTTP/2/ALPN；英文 manual 对此已有说明。
- 响应必须是 HTTP 200，必须有正数且不超过上限的 `Content-Length`，不支持 `Transfer-Encoding`。

双栈解析：

- `AsyncNameResolverMan::startAsync()` 依据可用地址族启动 A/AAAA 查询；源码先放 IPv6 resolver，再放 IPv4 resolver。
- `configureAsyncNameResolverMan()` 会调用 `net::checkAddrconfig()`，如果本机没有 IPv6 或 `--disable-ipv6=true`，则关闭 AAAA 查询。
- `AbstractCommand::resolveHostname()` 拿到解析结果后写入 DNS cache，再由 `selectIPAddress()` / `getLeastUsedActiveAddressFamily()` 在 IPv4/IPv6 间选择。
- 这不是完整 RFC 8305 Happy Eyeballs 并发连接实现；更准确说是“同时支持 A/AAAA 解析、当前线程用最快可用结果，后续连接优先选择 active in-flight 较少的地址族；打平时由 `FileEntry` 按 host/port 保存的轮转游标在 IPv4/IPv6 间分散选择地址”。

XP/Win7 注意：

- `configure.ac` 的 mingw 注释说明 `getaddrinfo` 依赖 `_WIN32_WINNT >= 0x0501`；源码还保留了 Windows 地址配置探测路径。
- `SocketCore::checkAddrconfig()` 在 mingw 下使用 `GetAdaptersAddresses()`；失败时会保守假设 IPv4/IPv6 都可用。因此旧系统若 IPv6 栈坏或 AAAA 查询拖慢，建议显式加 `--disable-ipv6=true`。
- 默认 `--min-tls-version=TLSv1.2`。老 XP/Win7 的原生 TLS 栈可能不满足现代 TLS/ALPN/SNI 需求；需要兼容时优先考虑 OpenSSL/GnuTLS 构建，或只在受控环境降低 TLS 版本。

### 2.4 HTTP/2 / `--enable-http2`

注册路径：

- `src/OptionHandlerFactory.cc`：有 `HAVE_LIBNGHTTP2` 时注册为 `BooleanOptionHandler`；没有 `HAVE_LIBNGHTTP2` 时注册为 `UnsupportedFeatureOptionHandler`，设置为 true 会快速失败。
- `src/HttpTLSHandshakeParams.cc::createHttpAlpnProtocols()`：有 `HAVE_LIBNGHTTP2`、`--enable-http2=true` 且 `--enable-http-pipelining=false` 时，ALPN 顺序为 `h2`、`http/1.1`。
- `src/SocketCore.cc::SocketCore::tlsHandshake()` 先检查 `TLSSession::supportsAlpnProtocols()`；支持时调用 `setAlpnProtocols()`，不支持时跳过 ALPN 设置并继续普通 TLS 握手。
- `src/HttpProtocol.cc::decideHttpProtocolFromSelectedAlpn()` 根据服务端选中的 ALPN 判定 HTTP/1.1 或 HTTP/2。
- `src/HttpRequestCommand.cc` 在 `HTTP_PROTOCOL_H2` 下创建 `Http2MultiplexExchange`、`Http2SocketCoreTransport` 和 `Http2ConnectionContext`，提交首个 stream 后调用 `DownloadEngine::registerActiveHttp2Connection()` 登记 active H2 context。
- `src/DownloadEngine.cc` 维护 active H2 context registry 与 same-origin idle H2 pool：key 由 URL 协议/host/port 加已连接的 hostname/address/port 组成；active pool 用弱引用，idle pool 用强引用持有 `Http2ConnectionContext`。
- `src/HttpInitiateConnectionCommand.cc` 在新请求建连前先调用 `DownloadEngine::findActiveHttp2Connection()`，再调用 `DownloadEngine::popIdleHttp2Connection()`；命中后直接在既有 `Http2MultiplexExchange` 上 `submitRequest()` 创建新 stream。
- `src/Http2ResponseCommand.cc` 和 `src/Http2DownloadCommand.cc` 都按 `streamId` 取响应/正文，并在 `executeInternal()` 中调用 `exchange_->pump()` 驱动共享连接；最后一个 active stream 结束后会把连接放入 idle pool。
- `src/Http2ConnectionContext.cc` 持有 `shared_ptr<RequestGroup>`，并在构造/析构时调用 `RequestGroup::increaseStreamConnection()` / `decreaseStreamConnection()` 持有连接计数；H2 stream command 传入 `incNumConnection=false`，避免每个 stream 都重复占用普通连接计数。

限制：

- 仅 HTTPS；依赖 TLS ALPN。
- 依赖 libnghttp2 构建。
- `--enable-http-pipelining=true` 时不会向 ALPN 放入 `h2`，因此 HTTP/2 与 HTTP/1.1 pipelining 仍互斥。
- 首条 H2 连接仍要求当前下载最多 1 个 segment；`HttpRequestCommand.cc` 中如果已有多个 segment，会记录 “HTTP/2 single-stream download does not support pipelined segments. Retrying with one segment.” 并重试为单 segment。
- active registry 只保存仍有 active stream 的 H2 连接；最后一个 stream 完成后，同 origin 连接可进入 idle pool，默认保留 15 秒。
- 复用限定在同一个 `RequestGroup` 内，且必须是 HTTPS、`--enable-http2=true`、无代理或 HTTPS `CONNECT` tunnel、当前请求 0/1 segment。
- 复用还要求 key 完全匹配当前 URL host/port 与已连接地址信息，并通过 TLS socket reuse predicate；这不是 HTTP/2 origin coalescing。
- idle pool 命中前会检查 socket 仍打开、未超时、不可读；socket 可读时按保守策略视为可能 EOF/GOAWAY，直接驱逐而不复用。
- `EvictSocketPoolCommand.cc` 会随普通 socket pool 定时扫描一起调用 `DownloadEngine::evictIdleHttp2Connections()`，避免 idle H2 context 长时间强持有 `RequestGroup`。
- active stream 上限使用本地保守上限 `MAX_ACTIVE_HTTP2_STREAMS = 8` 与 peer `SETTINGS_MAX_CONCURRENT_STREAMS` 的较小值；服务端未发 SETTINGS 限制时退回本地 8 条上限。
- 首条 H2 stream 在注册 context 后会 `exchange->flushOutboundData()`；复用路径提交新 stream 后不提前 flush，交给 `Http2ResponseCommand::executeInternal()` / `exchange_->pump()` 统一驱动。
- TLS 后端必须支持 ALPN 才能真正协商 H2；`SocketCore::tlsHandshake()` 会在后端不支持 ALPN 时跳过 ALPN 设置并继续握手，最终退回 HTTP/1.1。当前源码里只有 OpenSSL 后端声明支持 ALPN。

XP/Win7 注意：

- HTTP/2 实际可用性取决于构建是否有 libnghttp2，以及 TLS 后端是否能发送 ALPN。旧 Windows 原生 SChannel/WinTLS 路径通常不能指望 ALPN；启用 `--enable-http2=true` 时会优雅退回 HTTP/1.1，不应因为 ALPN 缺失直接中断 HTTPS 下载。
- 需要兼容 XP/Win7 且确实要使用 H2 时，优先使用带 OpenSSL 且启用 ALPN 的构建；如果目标环境只要求能下载，HTTP/1.1 fallback 能保持可用。
- `--hosts-mapping`、`--tls-sni-host` 与 HTTP/2 可以组合，但 FakeSNI 仍受 TLS 后端 SNI override 能力限制，见 2.1。

重要差异：

- `doc/manual-src/en/aria2c.rst` 目前仍写着 `--enable-http2` 是保留名、未实现、不启 ALPN、不用 libnghttp2。
- 当前 `usage_text.h` 和源码已经显示 HTTP/2 实现路径存在。因此本中文文档以当前源码为准，并把英文 manual 视为待同步。

示例：

```console
aria2c --enable-http2=true --enable-http-pipelining=false https://example.com/file
aria2c --enable-http2=true --log-level=network --console-log-level=network https://example.com/file
```

### 2.5 `network` 日志级别

`--log-level=network` 和 `--console-log-level=network` 会输出关键网络事件，覆盖 DNS、connect、TLS、HTTP、redirect 和部分网络重试。实现里可见 `A2_LOG_NETWORK(...)` 调用，例如：

- `AsyncNameResolverMan.cc`：DNS 模式、A/AAAA 家族、server 列表。
- `AsyncDotNameResolver.cc` / `AsyncDohNameResolver.cc`：DoT/DoH 连接、失败、解析结果。
- `AbstractCommand.cc`：hosts mapping、DNS cache hit、解析完成。
- `HttpRequestCommand.cc` / `HttpInitiateConnectionCommand.cc` / `DownloadEngine.cc`：HTTPS 连接建立、H2 active context 注册与复用。

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
| `--enable-ech [false]` | `false` | ECH 保留名；当前未实现，设为 true 会失败。 |
| `--enable-http2 [false]` | `false` | 实验性 HTTP/2；需要 libnghttp2、HTTPS、ALPN，详见 2.4。 |
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
| `--async-dns-server=<SERVER>[,...]` | cares 读系统配置；DoT/DoH 必填 | 指定异步 DNS server，格式随 mode 变化。 |
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
```

### 5.5 网络调试日志

```console
aria2c --log=- --log-level=network --console-log-level=network https://example.com/file
```

## 6. 待主线程补齐/确认

- HTTP/2 目前已有同 origin active/idle 复用。后续阶段还要补 HTTP/2 origin coalescing，以及继续完善错误恢复、Range/redirect 行为。
- `doc/manual-src/en/aria2c.rst` 中 `--enable-http2` 仍是“未实现保留名”的旧说法，需要主线程同步英文 manual，否则中文/英文文档会冲突。
- `--select-least-used-host`、`--dns-timeout`、`--startup-idle-time`、`--max-http-pipelining`、`--bt-keep-alive-interval`、`--bt-request-timeout`、`--bt-timeout`、`--peer-connection-timeout` 等源码注册项需要确认是否正式对用户公开，还是只用于内部/隐藏帮助。
- `usage_text.h` 的 `--enable-direct-io` 和旧 `--metalink-servers` 文案未在当前 `OptionHandlerFactory.cc` 注册列表中确认到，需要清理或补注册。
- WinTLS/AppleTLS/GnuTLS 的 ALPN 和 SNI override 能力需要按最终 TLS 后端实现再做矩阵表；当前只能确定 OpenSSL 后端有 ALPN 与 SNI override，GnuTLS 有 SNI override，WinTLS 未声明 SNI override，基类 ALPN 默认不支持并会触发 HTTP/1.1 fallback。
