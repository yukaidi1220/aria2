# aria2 中文使用指南

本文面向直接使用当前分支 `aria2c` 的用户，重点解释“应该怎么配”和“为什么会这样”。如果要查完整参数表，请继续看 [command-line-help.zh-CN.md](command-line-help.zh-CN.md)。

本文按当前源码实现编写，主要核对了 `src/OptionHandlerFactory.cc`、`src/usage_text.h`、`src/option_processing.cc`、`src/AsyncNameResolverMan.cc`、`src/AbstractCommand.cc`、`src/RequestGroup.cc`、`src/FileEntry.cc`、`src/HttpRequestCommand.cc`、`src/HttpSkipResponseCommand.cc`、`src/DownloadCommand.cc` 和 HTTP/2 / DoH / HTTPS RR 相关实现。

## 1. 一句话版本

当前分支是在 aria2 原有 HTTP/FTP/SFTP/BT/Metalink 下载能力上，补强了这些方向：

- 配置加载更可解释：支持当前目录、程序目录、用户目录配置查找顺序，支持 `conf-precedence=command|conf`。
- DNS 更可观测：支持 c-ares、DoT、DoH、multi secure-first，network 日志会说明 query plan、server、endpoint、fallback。
- IPv6 默认启用：`disable-ipv6=false`，但旧系统或 IPv6 不通时仍可显式关闭。
- HTTP/2 仍是实验能力：普通下载由 `enable-http2` 控制；DoH 可用独立的 `enable-doh-http2` 只让 DNS over HTTPS 尝试 H2。
- HTTPS RR / SVCB 默认关闭：只有 `enable-https-rr=true` 才会后台查询 TYPE65 并消费缓存。
- 默认值按常用场景定制：SSL 构建下 `async-dns-mode=multi` 并内置公共 DNS 列表、`uri-selector=adaptive`、`max-file-not-found=10`、`tls-handshake-failover=true` 等已是默认值；配置文件里只需要写与默认不同的项。
- 下载调度更清楚：`split`、`min-split-size`、`piece-length`、`max-connection-per-server` 各管各的，不要混成一个“每个请求固定多大”。
- 低速重试更积极：`lowest-speed-limit` 触发后，已知文件大小时会尝试补足当前下载的 stream 并发上限。

## 2. 推荐配置模板

### 2.1 稳定 HTTP/1.1 下载

适合先排除 H2、DNS、IPv6 的变量。

```ini
continue=true
split=16
max-connection-per-server=16
min-split-size=4M
piece-length=4M
enable-http2=false
enable-http-pipelining=false
disable-ipv6=true
console-log-level=notice
```

说明：

- `split=16` 是目标分片/连接数，不保证一定跑满。
- `max-connection-per-server=16` 按 URL 的 `protocol + hostname` 计数，不按解析出来的 IP 分开计数。
- `min-split-size=4M` 表示小于 `8M` 的剩余范围一般不会继续拆。
- `piece-length=4M` 是内部 piece 边界；实际 HTTP Range 可以跨多个 piece，不等于“每个 Range 固定 4M”。

### 2.2 DoH 走 H2，普通下载不走 H2

适合只想让 DNS over HTTPS 尝试 HTTP/2，但普通 HTTPS 下载仍保持 HTTP/1.1。

```ini
async-dns=true
async-dns-mode=multi
async-dns-server=dot://223.6.6.6,dot://dot.360.cn,https://120.53.53.53/dns-query,https://223.5.5.5/dns-query,https://doh.360.cn/dns-query,tcp://8.8.8.8,udp://119.29.29.29
enable-http2=false
enable-doh-http2=true
enable-http-pipelining=false
```

注意：

- DoH URL 建议带上路径，例如 `https://120.53.53.53/dns-query`。只写 `https://120.53.53.53` 时，HTTP 路径会是 `/`，很多 DoH 服务不会接受。
- `enable-doh-http2=true` 不会给普通 HTTPS 下载启用 H2。
- 构建没有 nghttp2 时，H2 相关开关为 true 会在参数阶段被拒绝。
- `async-dns-mode=multi` 和一组公共 DNS 列表（`dot://dot.360.cn,dot://dot.pub,https://dns.alidns.com/dns-query,tcp://180.184.1.1,udp://119.29.29.29`）已经是默认值；这里写出来只是为了用另一组 server。配置里给了 `async-dns-server` 就整体覆盖默认列表，想回到系统 DNS 用 `async-dns-mode=cares`。

### 2.3 secure DNS 优先，多阶段 fallback

```ini
async-dns=true
async-dns-mode=multi
async-dns-server=dot://223.6.6.6,dot://dot.360.cn,https://223.5.5.5/dns-query,tcp://8.8.8.8,udp://119.29.29.29
console-log-level=network
```

multi 模式的含义：

- `dot://...` 是 DoT。
- `https://...` 是 DoH。
- `tcp://IP` 是 plain DNS over TCP。
- `udp://IP` 或裸 IP 是 plain DNS over UDP。
- 有 DoT/DoH 时，下载域名优先走 secure resolver；secure 全失败后才进入显式 plain DNS；显式 plain 也失败或没有配置时，再进入系统 c-ares。
- plain DNS 默认不直接解析下载域名，除非进入明确 fallback，或用于 DoT/DoH server 域名 bootstrap。

## 3. 配置文件怎么找，谁覆盖谁

启动时先处理几个启动控制项，再决定读哪个配置：

1. 命令行写了 `--no-conf=true`：不读取任何配置文件。
2. 命令行写了 `--conf-path=PATH`：只读取这个路径。相对路径按当前工作目录解析。
3. 没写 `--conf-path` 且没有 `--no-conf=true`：按当前工作目录 `aria2.conf`、程序所在目录 `aria2.conf`、用户目录默认配置的顺序查找。

重复配置的优先级由 `conf-precedence` 控制：

- `conf-precedence=command`：默认值，命令行覆盖配置文件。
- `conf-precedence=conf`：配置文件覆盖重复的命令行选项。

排查配置是否生效时，打开日志：

```ini
console-log-level=network
log-level=network
```

启动日志会打印关键参数最终值和来源，例如：

```text
Option: async-dns=true (source=conf)
Option: enable-http2=false (source=conf)
```

## 4. 分片、piece 和连接数

这几个参数经常被混在一起，实际语义不同：

| 参数 | 管什么 | 常见误解 |
| --- | --- | --- |
| `split` | 单个下载目标最多创建多少个分片/stream command | 不是一定能跑满 |
| `max-connection-per-server` | 同一个 URL `protocol + hostname` 的连接上限 | 不按不同 IP 绕过 |
| `min-split-size` | 剩余范围小到什么程度就不再继续拆 | 不是每个 Range 的固定大小 |
| `piece-length` | 内部 piece 边界和控制文件粒度 | 不等于 HTTP Range 请求大小 |

实际 Range 大小由调度器当时分配到的 segment 决定。它会受文件总长度、已完成范围、未完成空洞、`split`、`min-split-size`、已有 stream 数、服务端 Range 能力和镜像 URL 数量共同影响。

`piece-length=4K` 只表示 split 边界落在 4K 的整数倍上。一个 HTTP Range 可以跨很多个 piece，所以 Range 可能远大于 4K。

文件大小未知时，aria2 还不知道怎么切分，通常只先创建一个请求去拿响应头和长度。拿到长度、确认可 Range 后，才有条件展开多连接。

## 5. 多 URL 是镜像，不一定是多个任务

命令行连续给多个 HTTP/FTP URL 时，默认通常会被当成同一个下载项的多个镜像 URI，而不是多个独立下载任务。它们共用一个 `RequestGroup`，也共用同一套分片、连接数、H2 复用和 URI 选择逻辑。

如果你希望多个 URL 分别作为独立下载任务，常用做法：

```console
aria2c --force-sequential=true https://a.example/file https://b.example/file
```

或者使用 `--input-file`，按 aria2 输入文件格式分成多个下载项。

多 URL 选择由 `uri-selector` 决定：

- `inorder`：按剩余 URI 顺序选择。
- `feedback`：优先参考 server 统计和已用 host。
- `adaptive`：默认值，更偏向动态反馈选择。

`reuse-uri=true` 的含义是：没有未用 URI 时，可以把已经用过且没有错误记录的 URI 再放回候选。它不是“302 复用开关”，也不是“强制同一个 URL 一直用到底”。

## 6. 第一个 URL 坏了会怎样

要看错误类型。

### 6.1 当前 URL 不可恢复错误

例如默认 404、认证失败、部分不可重试 HTTP 错误，会走 `DlAbortEx` 路径。当前 URL 会记录错误、从 in-flight 里移除，并触发 `tryReserved()` 尝试后面的 reserved URI。

这类错误通常不会让整个多 URL 下载立刻失败，前提是还有别的 URL 可试。

### 6.2 可重试错误

例如连接超时、网络问题、部分 5xx、`lowest-speed-limit` 触发，会走 `DlRetryEx` 路径。当前 request 会增加 try count，并被放回 request pool。默认 `max-tries=5`，所以第一个 URL 可能先被重试几次，然后才轮到后面的 URL。

如果把 `max-tries=0` 设为无限重试，一个半死不活的第一个 URL 可能长期拖住启动阶段。需要更快切到后续镜像时，建议：

```ini
max-tries=1
retry-wait=0
reuse-uri=false
uri-selector=inorder
```

### 6.3 任务级失败

少数错误会抛 `DownloadFailureException`，这不是“当前 URL 坏了”，而是任务级失败。源码会把 `RequestGroup` 标记为 halt，不再正常尝试后面的 URI。

## 7. 低速重试怎么工作

`lowest-speed-limit` 默认是 `0`，表示不启用。启用后，下载开始超过启动空闲时间后，如果当前连接速度低于阈值，会抛出 `TOO_SLOW_DOWNLOAD_SPEED` 重试。

当前分支在这个路径上做了补强：

- 会取消当前 CUID 持有的 segment，让未完成范围回到可调度池。
- 已知文件大小时，会尝试补充新的 `CreateRequestCommand`，把 stream command 数补到 `min(总 piece 数, split)`。
- 同时仍会创建当前 CUID 的替代请求。

注意：

- 文件大小未知时不会补并发，因为还不知道范围怎么切。
- 这个补并发只针对 `lowest-speed-limit` 的低速重试，不改变所有 retry 行为。
- 它不是“把当前 Range 从中间切开”的独立功能；实际重新分配仍由 segment/piece 调度器决定。

## 8. HTTP/2 怎么用

普通 HTTPS 下载启用 HTTP/2：

```ini
enable-http2=true
enable-http-pipelining=false
```

前提：

- 构建包含 libnghttp2。
- TLS 后端支持 ALPN。
- 服务端实际选择 `h2`。
- 当前路径是 HTTPS。

边界：

- H2 目前是实验能力。
- H2 active/idle 连接复用限定在同一个 `RequestGroup` 内，不是全局连接池。
- `enable-http-pipelining=true` 时不会向 ALPN 放入 `h2`。
- TLS 后端没有 ALPN 时，握手继续完成，普通下载会回到 HTTP/1.1。

只让 DoH 尝试 H2：

```ini
enable-http2=false
enable-doh-http2=true
enable-http-pipelining=false
```

这只影响 DoH resolver，不启用普通下载 H2。

### TLS 握手失败换地址族重试

`tls-handshake-failover=true`（默认）时，TLS 握手失败（包括证书校验失败）不会直接中止：失败 IP 会被标记为坏地址，失败地址族记一次惩罚（首次 30 秒，连续失败翻倍，上限 300 秒），然后换该 host 的另一个已解析地址重试。

证书校验开启时只换到另一地址族——IPv4 握手失败换 IPv6，IPv6 失败换 IPv4——并且只换一次，对端族近期也失败、或对端族只有非公网 scope 的 IPv6 地址（ULA/link-local 等）时，直接报错。每次重试仍完整校验证书，两族都失败时报告最后一次 TLS 错误。这适合只有一族被中间设备 RST 或注入证书的网络环境。想恢复“握手失败即中止”，显式 `tls-handshake-failover=false`。

## 9. DNS 和 HTTPS RR

`async-dns=false` 时，不启动 async DNS resolver。下载域名解析回到 hosts/cache 后的同步 `getaddrinfo` 路径，`async-dns-mode` 和 `async-dns-server` 不参与下载域名解析。

`enable-https-rr=false` 是默认值。默认不会因为 HTTPS、H2、H3 或 async DNS 开着就额外查询 HTTPS/SVCB TYPE65，也不会消费已有 HTTPS RR cache 改连接目标。

只有显式设置：

```ini
enable-https-rr=true
```

才会在直连 HTTPS 场景启动后台 TYPE65 discovery。它不阻塞首连，cache 命中后可以选择 SVCB endpoint 作为 TCP connect target/port，但 Request origin、HTTP `Host`、TLS SNI 和证书校验主机仍保持原 URL origin。

## 10. IPv4/IPv6 行为

默认：

```ini
disable-ipv6=false
```

这表示 aria2 会允许 AAAA 查询和 IPv6 连接候选。它不是系统 IPv6 总开关。

当前实现的重点：

- A/AAAA 可并发解析，任一地址族先成功即可先建连。
- 后台拿到另一族地址后，会写入 DNS cache 并唤醒后续连接。
- 同一 hostname 的连接数仍受 `max-connection-per-server` 限制，不能靠多个 IPv4/IPv6 地址绕过。
- 如果 IPv6 是 ULA、link-local、site-local 等非公网 scope，且 IPv4 可用，主选址会优先 IPv4，避免把首连押到局域网 IPv6 黑洞。
- 连接失败会给同 host/port/family 加短期降权，后续连接会优先避开明显坏的地址族。

旧 Windows 或网络 IPv6 不稳定时，可以显式：

```ini
disable-ipv6=true
```

## 11. 日志怎么看

排查网络问题建议：

```ini
console-log-level=network
log-level=network
```

常搜关键字：

- `Option:`：确认配置最终值和来源。
- `DNS:`：DNS backend、server、endpoint、fallback。
- `query plan`：当前 DNS resolver 计划。
- `falling back`：DNS 降级阶段。
- `selected host=`：选中的连接目标。
- `TLS: connected remote=`：TLS 建连成功时的实际远端 IP。
- `TLS handshake failed for`：TLS 握手/证书校验失败后的换地址族重试、或不重试的原因。
- `marking bad address`：失败 IP/地址族惩罚记录。
- `Response status` / `Response received`：HTTP 响应和 remote 关联。
- `HTTP2:` / `H2`：HTTP/2 context 注册、复用、stream 行为。
- `Slow download retry scheduled`：低速重试后补充并发请求。

## 12. 当前不要误写成已完成的能力

这些能力有部分代码或能力门，但不要在用户文档里写成完整下载能力：

- HTTP/3 / QUIC：当前主要是依赖探测、能力门和 Alt-Svc parser，还没有完整 H3 下载路径。
- ECH 自动发现：只支持手动 `ech-config-base64`；HTTPS RR cache 里的 `ech` 还没有自动接入 TLS ECH。
- HTTPS RR 完整策略：已有后台 discovery/cache 和直连 HTTPS selected endpoint 使用，但完整 fallback origin、proxy 策略和 H3/ECH 联动仍未闭环。
- HTTP/2 全局复用：当前 H2 active/idle 复用限定在同一个 `RequestGroup`，不是跨任务全局连接池。

