# TLS SNI Host Mapping Functional Test Report

Date: 2026-06-08 02:28:00 +08:00

Branch: `codex/fakesni-alpn-foundation`

Commit: `88820907c9f0b0d67e528228fbaa57d624078325`

GitHub Actions run: [27100434287](https://github.com/yukaidi1220/aria2/actions/runs/27100434287)

## Summary

This round extends `--tls-sni-host` from a single global SNI hostname to an optional comma-separated `TARGET:SNI` mapping list. The test focus was:

- old single-host compatibility;
- `TARGET:SNI` lookup for IPv4 targets;
- mapping miss fallback to the current request host;
- redirect handling where the request host changes after a 302;
- malformed mapping rejection before a TLS ClientHello is sent;
- Windows x86 and x64 CI build artifacts.

Result:

- Windows x86 and x64 CI builds passed.
- New help text is present in both artifacts.
- Local SNI ClientHello capture tests passed 5/5.
- The user's full download command was attempted against the new x64 artifact, but this run hit DNS/name-resolution timeout in the current test environment before any TLS connection was made. This is recorded as an environment/network failure, not a SNI mapping failure.

## Artifacts

Run page:

- [GitHub Actions run 27100434287](https://github.com/yukaidi1220/aria2/actions/runs/27100434287)

Windows x64:

- Name: `aria2-x86_64-w64-mingw32`
- Artifact ID: `7466651723`
- Artifact page: [aria2-x86_64-w64-mingw32](https://github.com/yukaidi1220/aria2/actions/runs/27100434287/artifacts/7466651723)
- API archive: [zip](https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7466651723/zip)
- Digest: `sha256:5d50f672f218fc5db931c1ece0763a35d261135ae454554d21cede3649f6794f`
- Artifact size: `3796704` bytes
- Downloaded binary: `tmp_sni_mapping_test_20260608/x64/aria2c.exe`
- Binary size: `8303616` bytes
- Binary SHA256: `A2CAF6850962823DD3CF910AB3A33DB8036DD958FD0ED6D5CEB010E144BBE778`

Windows x86:

- Name: `aria2-i686-w64-mingw32`
- Artifact ID: `7466635808`
- Artifact page: [aria2-i686-w64-mingw32](https://github.com/yukaidi1220/aria2/actions/runs/27100434287/artifacts/7466635808)
- API archive: [zip](https://api.github.com/repos/yukaidi1220/aria2/actions/artifacts/7466635808/zip)
- Digest: `sha256:00014e3f6a1cde1b670ff6ded369674bc4c1fa8ecc0317348c0657ee68c456d6`
- Artifact size: `3829049` bytes
- Downloaded binary: `tmp_sni_mapping_test_20260608/x86/aria2c.exe`
- Binary size: `8713230` bytes
- Binary SHA256: `4B90149871818B79C42E2DCA45931606D3BECC205A80FE6B308252823D60BC03`

CI jobs:

- [build-windows (x86_64-w64-mingw32)](https://github.com/yukaidi1220/aria2/actions/runs/27100434287/job/79980034985): success
- [build-windows (i686-w64-mingw32)](https://github.com/yukaidi1220/aria2/actions/runs/27100434287/job/79980034981): success
- `build`: skipped by workflow configuration

## Test Environment

Artifact `--version` for both x64 and x86:

- aria2 version: `1.37.0`
- Enabled features: `Async DNS, BitTorrent, Firefox3 Cookie, GZip, HTTPS, Message Digest, Metalink, XML-RPC, SFTP`
- TLS library: `OpenSSL/1.1.1w`
- DNS library: `c-ares/1.34.6`
- x64 target: `x86_64-w64-mingw32`
- x86 target: `i686-w64-mingw32`

Local runtime used for functional tests:

- PowerShell `7.6.1`
- Node.js from `C:\Program Files\nodejs\node.exe`
- OS reported by artifact: `Windows 6.2`

## Test Matrix

| ID | Area | Command summary | Expected | Result |
| --- | --- | --- | --- | --- |
| T01 | CI build | GitHub Actions run `27100434287` | Windows x64/x86 jobs pass | PASS |
| T02 | x64 startup | `x64/aria2c.exe --version` | Starts and reports OpenSSL/c-ares | PASS |
| T03 | x86 startup | `x86/aria2c.exe --version` | Starts and reports OpenSSL/c-ares | PASS |
| T04 | help text x64 | `x64/aria2c.exe --help=#all` | Includes `TARGET:SNI`, `[IPv6]`, redirect note | PASS |
| T05 | help text x86 | `x86/aria2c.exe --help=#all` | Includes same mapping help | PASS |
| T06 | user command x64 | User's full `PETOOL.7z` command | Download completes | ENV FAIL: DNS/name-resolution timeout before TLS |
| T07 | old single host | local TLS ClientHello capture with `--tls-sni-host=front.example` | Captured SNI is `front.example` | PASS |
| T08 | IPv4 target map | local TLS ClientHello capture with `127.0.0.1:front.example` | Captured SNI is `front.example` | PASS |
| T09 | mapping miss fallback | local TLS ClientHello capture with unmatched map | Captured SNI is empty for IP request host | PASS |
| T10 | redirect host change | local HTTP 302 to `https://localhost:<port>/`, map has `localhost:redirect.example` | Captured SNI is `redirect.example` | PASS |
| T11 | malformed mapping | map contains an empty entry | Fails before ClientHello with bad mapping error | PASS |

## User Command Attempt

The user's normal PowerShell command was tested against the x64 artifact:

```powershell
& "../x64/aria2c.exe" --async-dns-server=180.184.1.1 --disable-ipv6=false -s 32 -x 32 --min-split-size=2M -c --file-allocation=none --enable-http-keep-alive=false --summary-interval=0 --console-log-level=notice --log="x64_usercmd.log" --log-level=debug "https://silver.yukaidi.com/f/nEEMF5/PETOOL.7z"
```

Result:

- Exit code: `1`
- No output file was created.
- Log: `tmp_sni_mapping_test_20260608/run_x64_usercmd/x64_usercmd.log`
- Failure point: repeated c-ares name resolution timeout for `silver.yukaidi.com`.
- The run never reached TCP connect or TLS handshake.

Relevant log evidence:

```text
DNS: async resolver servers=180.184.1.1
DNS: start resolving silver.yukaidi.com using c-ares
DNS: query AAAA silver.yukaidi.com using c-ares
DNS: query A silver.yukaidi.com using c-ares
Connection to https://silver.yukaidi.com/f/nEEMF5/PETOOL.7z timed out, retrying
Retries exhausted (max=5)
```

Network probes from the same machine:

- `Resolve-DnsName silver.yukaidi.com -Server 180.184.1.1 -Type A` returned `198.18.0.18`.
- `Resolve-DnsName example.com -Type A` returned `198.18.0.25`.
- `Test-NetConnection example.com -Port 443` succeeded against `198.18.0.25`.
- `Test-NetConnection 180.184.1.1 -Port 53` TCP failed. This does not prove UDP DNS failure, but it confirms the test network is unusual.

## Local SNI Capture Tests

Because outbound DNS/network was unstable, the mapping behavior was verified with a local SNI ClientHello capture harness:

- Script: `tmp_sni_mapping_test_20260608/run_sni_capture_test.js`
- Results: `tmp_sni_mapping_test_20260608/sni_capture/sni_capture_results.json`
- Method:
  - start a local TCP server;
  - let aria2 connect with HTTPS;
  - parse the TLS ClientHello SNI extension;
  - close the socket without completing TLS;
  - treat aria2 exit code `1` as expected for these capture tests.

Results:

```json
[
  {
    "name": "single_host_compat",
    "expectedSNI": "front.example",
    "actualSNI": "front.example",
    "pass": true
  },
  {
    "name": "ipv4_target_mapping",
    "expectedSNI": "front.example",
    "actualSNI": "front.example",
    "pass": true
  },
  {
    "name": "unmatched_mapping_fallback",
    "expectedSNI": "",
    "actualSNI": "",
    "pass": true
  },
  {
    "name": "redirect_after_host_change",
    "expectedSNI": "redirect.example",
    "actualSNI": "redirect.example",
    "pass": true
  },
  {
    "name": "malformed_mapping",
    "expectedSNI": "",
    "actualSNI": "",
    "stdoutHasBadMapping": true,
    "pass": true
  }
]
```

Redirect evidence from `redirect_after_host_change.log`:

```text
Location: https://localhost:<port>/
CUID#7 - Redirecting to https://localhost:<port>/
DNS: start resolving localhost using c-ares
DNS: A localhost -> 127.0.0.1
Creating TLS session
TLS Handshaking
```

The capture result for that same run was:

```text
actualSNI=redirect.example
```

This confirms that after the 302 changed the request host to `localhost`, aria2 used the `localhost:redirect.example` mapping instead of reusing the original request host or the old global single SNI value.

Malformed mapping evidence:

```text
Bad TLS SNI host mapping ''
```

No ClientHello was captured for the malformed mapping case.

## Review

External review was requested from multiple agents. Several returned empty results and were not counted as valid review. The effective review result was:

```text
Findings: 无硬问题。
Residual risk: 主要剩余风险在 Request::getHost() 对 IPv6 是否始终带方括号、大小写和 302 后 host 规范化是否与 helper 完全一致。
```

The IPv6 mapping syntax is covered by unit test (`[2001:db8::1]:front.example` maps request host `2001:db8::1`), but this report did not run a live IPv6 TLS ClientHello capture.

## Limitations

- The report did not run on XP or Windows 7. Compatibility is covered by the single MinGW baseline and Windows x86/x64 CI artifacts.
- The local machine did not have `make`, `bash`, `g++`, `clang++`, `cmake`, or Python in `PATH`, so local autotools/unit-test execution was not possible.
- The user's real download URL could not be completed in this run because DNS/name resolution timed out before TLS.
- Live IPv6 SNI mapping was covered by unit test but not by local ClientHello capture.
