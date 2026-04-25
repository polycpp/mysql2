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
| aws-ssl-profiles | hard | ^1.1.2 | 1.1.2 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 8 | 0 | 0 | deferred or unsupported feature | Only needed for named TLS profile data. TLS itself is implemented through polycpp TLS options. |
| denque | hard | ^2.1.0 | 2.1.0 | Apache-2.0 | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 2 | 0 | 5 | implement private helper in this repo | Upstream uses it for command queues; this synchronous port uses standard containers and an RAII pool. |
| generate-function | hard | ^2.3.1 | 2.3.1 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 1 | 3 | 1 | 13 | deferred or unsupported feature | Upstream uses generated JS parsers for V8 speed. C++ uses static parsers. |
| iconv-lite | hard | ^0.7.2 | 0.7.2 | MIT | package.json license field and existing companion license | permissive | use existing companion license | no | 1 | 16 | 37 | 39 | use existing polycpp companion | Existing `/data/work/lib/iconv-lite` companion is linked for charset decoding. |
| long | hard | ^5.3.2 | 5.3.2 | Apache-2.0 | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 6 | 0 | 20 | implement private helper in this repo | C++ native `int64_t` and `uint64_t` replace JS Long behavior for parsed values. |
| lru.min | hard | ^1.1.4 | 1.1.4 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 4 | 0 | 53 | create separate private polycpp companion repo if reused broadly | Needed for upstream parser and prepared statement caches; this port implements prepared statements without an LRU cache. |
| named-placeholders | hard | ^1.1.6 | 1.1.6 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 1 | 1 | 0 | 7 | implement private helper in this repo | The port implements a small named placeholder formatter locally against C++ value variants. |
| sql-escaper | hard | ^1.3.3 | 1.3.3 | MIT | package.json license field | permissive | permissive dependency ok with notice | no | 0 | 5 | 6 | 57 | implement private helper in this repo | The port implements SQL escape, identifier escape, positional format, and raw SQL locally. |

## Dependency ownership decisions

- `iconv-lite`: use existing polycpp companion and link `polycpp_iconv_lite`.
- `long`: replace with C++ integer types and explicit string fallback behavior.
- `denque`: replace with standard containers and synchronous pool bookkeeping.
- `named-placeholders`: implement small local formatter for `:name` values.
- `sql-escaper`: implement local SQL escaping utilities and test edge bytes.
- `lru.min`: defer until prepared-statement cache is implemented; create a separate companion only if multiple ports need it.
- `generate-function`: omit because C++ parser generation is not needed.
- `aws-ssl-profiles`: defer named TLS profile support; direct TLS options are implemented.
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
- security hints consumed: yes; package is security-sensitive because it handles credentials, crypto, network packets, and SQL escaping
- security-sensitive package: yes

### Node.js API usage

- `Buffer`: heavy packet encoding/decoding; mapped to `polycpp::Buffer`.
- `crypto`: SHA1/SHA256/RSA public encrypt; mapped to `polycpp::crypto`.
- `net`: TCP client; mapped to `polycpp::io::TcpSocket`.
- `tls`: mapped to `polycpp::io::TlsContext`, `polycpp::io::TlsStream`, and `polycpp::ssl::X509Cert`.
- `zlib`: deferred compression.
- `events`, `stream`, `process`, `timers`: Node runtime command and callback mechanics; adapted to synchronous C++ API or deferred for streams.
- `url`: connection URI parsing is deferred.

### JavaScript API usage

- Dynamic object options become `ConnectionOptions`.
- Dynamic rows become `Row` containing `std::variant` values plus field-name index lookup.
- Generated JS parsers are replaced by static C++ text-row parsing.
- Callback and Promise wrappers are omitted.

### Framework object boundary usage

- analyzer-reported target-package framework object accesses: no HTTP framework object boundary
- analyzer-reported dependency framework object accesses: dependency framework object hits are not relevant to a MySQL protocol client boundary
- manual review decision: no polycpp HTTP request/response/header object should be introduced; MySQL packets are modeled as private protocol data

## Porting decisions

- Implement pure MySQL protocol over polycpp TCP sockets.
- Keep the public C++ API synchronous to make integration tests deterministic.
- Fail closed on unsupported auth plugins, LOCAL INFILE, TLS-only cleartext auth, malformed packets, unexpected multi-results in single-result APIs, and server ERR packets.
- Record deferred features explicitly rather than implying upstream parity.
- Reuse `iconv-lite` companion for charset decoding, but document incomplete charset id mapping.

## Analyzer warnings

- `aws-ssl-profiles: no entry points found for aws-ssl-profiles`: accepted because named TLS profiles are deferred while direct TLS options are implemented.
- `is-property: no entry points found for is-property`: accepted because `generate-function` is omitted.
- `safer-buffer: no entry points found for safer-buffer`: accepted because `iconv-lite` companion owns its own dependency strategy.
