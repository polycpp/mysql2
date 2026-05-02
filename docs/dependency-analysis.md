# Dependency and JavaScript API Analysis

Analyzer command used:

```bash
python3 /data/work/libgen/scripts/analyze-upstream-js.py /data/work/lib/mysql2 /data/work/lib/mysql2/.tmp/npm-package
```

- package: mysql2
- package version: 3.22.2
- package root: `/data/work/lib/mysql2/.tmp/npm-package`
- analyzer json: `.tmp/dependency-analysis.json`
- published npm artifact path: `/data/work/lib/mysql2/.tmp/npm-package`
- published npm artifact analyzed: yes
- include dev dependencies: no
- dependency source install used: npm package analyzer install under libgen workspace
- companion root checked: `/data/work/lib`

## Package entry metadata

- main: `index.js`
- module: none declared
- types: `typings/mysql/index`
- exports: `.`, `./package.json`, `./promise`, `./promise.js`
- bin: none
- missing declared entries in repo clone: none detected
- TypeScript source files detected: 0
- source-vs-published artifact decision: use published npm artifact for runtime entry points and source clone for tests/history

## Direct dependencies

| Package | Kind | Requested | Installed | License | License evidence | License impact | License strategy | Affects repo license | Deps | Source files | Node API calls | JS API calls | Recommendation | Rationale |
|---|---|---|---|---|---|---|---|---|---:|---:|---:|---:|---|---|
| @types/node | peer | >= 8 | not installed | MIT | npm package metadata license field | dev/test-only | dev/test-only, not shipped | no-dev-only | 0 | 0 | 0 | 0 | omit from C++ port | Type-only package for upstream TypeScript declarations. |
| aws-ssl-profiles | hard | ^1.1.2 | 1.1.2 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 8 | 0 | 0 | vendor generated data in this repo | Provides named AWS RDS TLS profile CA PEM data. The port generates `src/aws_rds_ca.inc` from the package artifact, consumes it through `src/aws_rds_ca.cpp`, and records the MIT notice. |
| denque | hard | ^2.1.0 | 2.1.0 | Apache-2.0 | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 2 | 0 | 13 | implement private helper in this repo | Upstream uses it for command queues; this synchronous port uses standard containers and an RAII pool. |
| generate-function | hard | ^2.3.1 | 2.3.1 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 1 | 3 | 2 | 13 | deferred or unsupported feature | Upstream uses generated JS parsers for V8 speed. C++ uses static parsers. |
| iconv-lite | hard | ^0.7.2 | 0.7.2 | MIT | package.json license field and existing companion license | permissive | use existing companion license | no | 1 | 16 | 38 | 55 | use existing polycpp companion | Existing `/data/work/lib/iconv-lite` companion is linked for charset decoding. |
| long | hard | ^5.3.2 | 5.3.2 | Apache-2.0 | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 6 | 0 | 21 | implement private helper in this repo | C++ native `int64_t` and `uint64_t` replace JS Long behavior for parsed values. |
| lru.min | hard | ^1.1.4 | 1.1.4 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 4 | 0 | 71 | create separate private polycpp companion repo if reused broadly | Needed for upstream parser and prepared statement caches; this port implements the required statement-cache behavior locally with standard containers. |
| named-placeholders | hard | ^1.1.6 | 1.1.6 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 1 | 1 | 0 | 8 | implement private helper in this repo | The port implements a small named placeholder formatter locally against C++ value variants. |
| sql-escaper | hard | ^1.3.3 | 1.3.3 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 5 | 8 | 65 | implement private helper in this repo | The port implements SQL escape, identifier escape, positional format, and raw SQL locally. |

## Dependency ownership decisions

- `iconv-lite`: use existing polycpp companion and link `polycpp_iconv_lite`.
- `long`: replace with C++ integer types and explicit string fallback behavior.
- `denque`: replace with standard containers and synchronous pool bookkeeping.
- `named-placeholders`: implement small local formatter for `:name` values.
- `sql-escaper`: implement local SQL escaping utilities and test edge bytes.
- `lru.min`: implement the required prepared-statement cache semantics locally with standard containers; create a separate companion only if multiple ports need a reusable LRU type.
- `generate-function`: omit because C++ parser generation is not needed.
- `aws-ssl-profiles`: generate AWS RDS CA PEM data into `src/aws_rds_ca.inc` and isolate it in `src/aws_rds_ca.cpp`; direct TLS file/PEM options remain supported.
- `@types/node`: omit as type-only upstream metadata.

## License impact summary

- upstream package license: MIT
- repo license decision: MIT with copyright holder `polycpp contributors`
- GPL/AGPL dependencies: none detected
- LGPL/MPL dependencies: none detected
- permissive dependencies requiring notices: mysql2, aws-ssl-profiles, denque, generate-function, iconv-lite, long, lru.min, named-placeholders, sql-escaper
- dev/test-only dependencies excluded from shipped artifacts: @types/node
- dependency license notices to add to `THIRD_PARTY_LICENSES.md`: upstream mysql2 plus analyzed runtime dependency license summary; iconv-lite companion retains its own license

## Transitive dependency summary

- `generate-function` depends on `is-property`.
- `iconv-lite` depends on `safer-buffer`.
- `named-placeholders` depends on `lru.min`.
- No transitive dependency changes the repo license decision because transitive source is not vendored and only `iconv-lite` is linked through an existing companion.

## Runtime API usage

### Target package

- entry points analyzed: `index.js`, `promise.js`, public exports, and protocol files under `lib/`
- source files analyzed by analyzer: published npm JavaScript files under `.tmp/npm-package`
- source files manually inspected: connection config, base connection, packet cursor, handshake packets, query command, auth plugins, constants, parsers
- external imports seen from target: `buffer`, `crypto`, `events`, `net`, `tls`, `stream`, `zlib`, `process`, `timers`, `url`, `node:diagnostics_channel`, and listed runtime dependencies

### Analyzer porting gates

- polycpp reuse hints consumed: yes; Buffer, crypto, IO, JSON, and zlib/TLS hints were reviewed
- Node parity hints consumed: yes; callback, Promise, EventEmitter, stream, Buffer, URL, timer/process, crypto, compression, filesystem, network, and TLS surfaces were reviewed against polycpp APIs
- security hints consumed: yes; package is security-sensitive because it handles credentials, crypto, network packets, and SQL escaping
- security-sensitive package: yes
- polycpp capability snapshot consumed: yes; `/data/repo/polycpp` HEAD `40bd73669e8105fcb8641ad6671dfd07141e9eff` was rechecked on May 2, 2026 before closing the Unix/IPC, server TLS, typed stream, and typed binlog lifecycle gaps.
- transport/listener capability hints consumed: yes; TCP, Unix/IPC path, cross-transport stream wrappers, and MySQL in-protocol TLS upgrade are mapped to current polycpp IO/TLS primitives.

### Node.js API usage

- `Buffer`: heavy packet encoding/decoding; mapped to `polycpp::Buffer`.
- `crypto`: SHA1/SHA256/RSA public encrypt; mapped to `polycpp::crypto`.
- `net`: TCP and Unix socket client/server surfaces; mapped to `polycpp::io::TcpSocket`, `PipeSocket`, `StreamSocket`, `TcpAcceptor`, `PipeAcceptor`, and `StreamAcceptor`.
- `tls`: mapped to `polycpp::io::TlsContext`, `polycpp::io::TlsStream`, and `polycpp::ssl::X509Cert` for MySQL SSLRequest upgrades.
- `zlib`: mapped to `polycpp::zlib` for the MySQL compressed packet protocol.
- `events`: mapped to typed `polycpp::events::EventEmitter` integration on connections, pools, and pool clusters.
- `stream`: Node object-mode row streams map to `RowStream`, a pull-based `polycpp::stream::Readable<Row>` subclass; `createBinlogStream` maps to `BinlogStream`, a pull-based `polycpp::stream::Readable<BinlogEvent>` subclass consumed with `polycpp::stream::event::Data`; `query_stream_json()` exposes lazy newline-delimited JSON `polycpp::stream::Readable<Buffer>` chunks.
- `process`, `timers`: Node runtime command queue mechanics are adapted to synchronous typed execution; connect and command inactivity deadlines use `polycpp::io::Timer`; pool wait timeouts are represented with C++ chrono values.
- `url`: mapped to `polycpp::url` for connection URI parsing.

### Node parity surface usage

- callbacks: implemented as `std::function` overloads over the typed connection, pool, and cluster operations.
- Promise APIs: implemented as `polycpp::Promise` wrappers that settle from the typed implementation.
- EventEmitter APIs: implemented through typed `polycpp::events::EventEmitter` forwarding on connections, pools, and pool clusters.
- streams: query rows use pull-based `RowStream` typed chunks; binlog events use pull-based `BinlogStream` typed chunks consumed with `polycpp::stream::event::Data`; replication stream terminal paths close the transport and subsequent normal commands reconnect through configured options; NDJSON output uses lazy `polycpp::stream::Readable<Buffer>` chunks over the same row stream.
- Buffer and binary data: mapped to `polycpp::Buffer` for packets, binary SQL values, result columns, and LOCAL INFILE chunks.
- URL/timer/process/filesystem APIs: URI parsing uses `polycpp::url`; connect and command inactivity timeouts use `polycpp::io::Timer`; pool wait timeouts use C++ chrono; process APIs are not public; filesystem access is limited to explicit TLS file options and caller-provided LOCAL INFILE buffers.
- crypto/compression/TLS/network/HTTP APIs: crypto uses `polycpp::crypto`; compression uses `polycpp::zlib`; TCP/Unix/TLS uses `polycpp::io` and `polycpp::ssl`; HTTP APIs are not relevant to this protocol driver.

### JavaScript API usage

- Dynamic object options become `ConnectionOptions`.
- Dynamic rows become `Row` containing `std::variant` values plus field-name index lookup.
- Generated JS parsers are replaced by static C++ text-row parsing.
- Callback and `polycpp::Promise` wrappers are provided as compatibility surfaces over the typed synchronous implementation.

### Framework object boundary usage

- analyzer-reported target-package framework object accesses: no HTTP framework object boundary
- analyzer-reported dependency framework object accesses: dependency framework object hits are not relevant to a MySQL protocol client boundary
- manual review decision: no polycpp HTTP request/response/header object should be introduced; MySQL packets are modeled as private protocol data

## Porting decisions

- Implement pure MySQL protocol over polycpp TCP sockets.
- Keep the public C++ API synchronous to make integration tests deterministic.
- Fail closed on unsupported auth plugins, LOCAL INFILE without an explicit handler, cleartext auth without TLS or `socket_path`, malformed packets, unexpected multi-results in single-result APIs, and server ERR packets.
- Record deferred features explicitly rather than implying upstream parity.
- Reuse `iconv-lite` companion for charset decoding and keep upstream mysql2 charset/collation id mappings in this repo.
- Vendor dependency data only when the upstream package is data-oriented and a runtime dependency would otherwise be required. `aws-ssl-profiles` is handled this way; its generated CA bundle has a third-party MIT notice and must be regenerated when the upstream basis changes.

## Analyzer warnings

- `aws-ssl-profiles: no entry points found for aws-ssl-profiles`: accepted for static analysis because the package is data-oriented; CA entries were manually generated from the npm artifact.
- `is-property: no entry points found for is-property`: accepted because `generate-function` is omitted.
- `safer-buffer: no entry points found for safer-buffer`: accepted because `iconv-lite` companion owns its own dependency strategy.
