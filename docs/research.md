# Research

- package: mysql2
- npm url: https://www.npmjs.com/package/mysql2
- source url: https://github.com/sidorares/node-mysql2.git
- upstream version basis: 3.22.2
- upstream revision analyzed: 8078ad06f12ec07cef1747210b96925bd27317d3
- upstream default branch: master
- license: MIT
- license evidence: package.json license field and upstream `License` file
- category: database protocol client

## Package purpose

`mysql2` is a Node.js MySQL/MariaDB driver. Upstream implements the MySQL wire protocol in JavaScript, including connection setup, authentication plugins, query execution, prepared statements, pooling, compression, TLS, and parsing for text and binary result sets.

The C++ companion provides a polycpp-native client that preserves the pure protocol implementation model. It does not link against native MySQL or MariaDB client libraries because those introduce license and distribution concerns that do not match the requested porting strategy.

## Runtime assumptions

- browser: upstream is Node.js-only; this companion is C++ server/runtime code.
- node.js: upstream depends on Node Buffer, net, tls, crypto, stream, zlib, timers, process, and EventEmitter. The C++ port replaces those with polycpp or C++ constructs.
- filesystem: upstream uses filesystem-adjacent behavior for SSL profiles and LOCAL INFILE. The first slice does not read files at runtime.
- network: required. The first slice connects over TCP to a MySQL/MariaDB server.
- crypto: required for password authentication tokens and RSA password encryption.
- terminal: not required.

## Dependency summary

- package.json present: yes
- package main: index.js
- package types: typings/mysql/index
- package exports: `.`, `./package.json`, `./promise`, `./promise.js`
- hard dependencies: aws-ssl-profiles, denque, generate-function, iconv-lite, long, lru.min, named-placeholders, sql-escaper
- peer dependencies: @types/node
- optional dependencies: none detected in package.json
- dependency analysis report: `docs/dependency-analysis.md`

## Upstream repo layout summary

Clone path used for analysis: `/data/work/lib/mysql2/.tmp/upstream/node-mysql2`

Top files reviewed:

- `index.js`: CommonJS public callback API and utility exports.
- `promise.js`: Promise wrapper public API.
- `lib/base/connection.js`: socket lifecycle, command queue, packet sequencing, TLS/compression transitions.
- `lib/connection_config.js`: option defaults, client capability flags, charset defaults, SSL profiles.
- `lib/packets/packet.js`: little-endian packet cursor, length-coded integers/strings, numeric/date parsing.
- `lib/packets/handshake.js`: server handshake parser.
- `lib/packets/handshake_response.js`: client handshake response serializer.
- `lib/commands/client_handshake.js`: auth plugin selection and handshake state machine.
- `lib/commands/auth_switch.js`: auth switch and auth-more-data handling.
- `lib/auth_41.js`: mysql_native_password SHA1 token algorithm.
- `lib/auth_plugins/caching_sha2_password.js`: caching_sha2_password SHA256 token and RSA full auth.
- `lib/auth_plugins/sha256_password.js`: RSA public-key auth flow.
- `lib/commands/query.js`: COM_QUERY state machine.
- `lib/packets/resultset_header.js`: OK packet and resultset header parsing.
- `lib/packets/column_definition.js`: field metadata parsing.
- `lib/packets/text_row.js`: text row length-coded values.
- `lib/constants/client.js`, `commands.js`, `types.js`, `field_flags.js`, `server_status.js`, `charset_encodings.js`: protocol constants.

## Entry points used by consumers

- `index.js`: `createConnection`, `connect`, `createPool`, `createPoolCluster`, `escape`, `escapeId`, `format`, `raw`, constants, parser-cache controls.
- `promise.js`: Promise wrapper equivalents.
- `typings/mysql/index.d.ts`: TypeScript public contract.

The C++ first slice maps the core connection and utility helpers. Pooling, promises, stream APIs, and parser cache controls are deferred because they are Node runtime surfaces rather than protocol primitives.

## Important files and why they matter

- `lib/base/connection.js`: establishes packet sequence reset rules and command-phase behavior.
- `lib/commands/client_handshake.js`: defines which auth plugin is attempted directly and which cases fail closed.
- `lib/auth_41.js`: gives the exact mysql_native_password token construction.
- `lib/auth_plugins/caching_sha2_password.js`: required for MySQL 8 default authentication.
- `lib/packets/packet.js`: source of length-coded integer/string parsing and endian behavior.
- `lib/packets/column_definition.js`: maps result metadata needed for C++ typed rows.
- `lib/constants/*.js`: constants that must remain stable for wire compatibility.

## Files likely irrelevant to the C++ port

- Benchmark HTTP examples and website assets.
- Promise wrapper implementation details beyond public API naming.
- Generated JavaScript text/binary parser optimization code that exists to avoid V8 overhead; the C++ port uses static parsing.
- TypeScript declaration internals beyond public option and result shapes.

## Test directories worth mining first

- `test/unit`: packet parser, auth, SQL formatting, and parser behavior that can become C++ unit tests.
- `test/integration`: real server connection, query, error, transaction, charset, and prepared-statement behavior.
- `test/fixtures`: certificates and test data useful when TLS is implemented.
- `benchmarks/unit/fixtures`: performance fixtures only after correctness is established.
- `website/docs/examples`: user-facing examples that can guide C++ docs once features exist.

## Implementation risks discovered from the source layout

- Authentication is security-sensitive and stateful. Unsupported plugins must fail closed rather than silently downgrade.
- TLS and compression alter the transport after the initial handshake; adding them later must preserve packet sequencing.
- Prepared statements use binary protocol and type encoding, not the text protocol implemented in the first slice.
- Charset support is broad. A partial charset id map can decode common UTF-8/latin1 cases but is not full mysql2 parity.
- Upstream uses dynamic JavaScript values and parser generation; the C++ API must define explicit variant behavior.
- LOCAL INFILE is a file exfiltration boundary and is intentionally unsupported until an explicit callback policy exists.

## Companion repo alignment

- companion repos inspected: `/data/work/lib/iconv-lite`, `content-type`, `range-parser`, `picomatch`, `vary`, `jsonwebtoken`, `sequelize`
- CMake target and alias pattern: `polycpp_mysql2` with alias `polycpp::mysql2`, matching existing companion libraries.
- public header layout: `include/polycpp/mysql2/mysql2.hpp` is the public API.
- detail/private header strategy: protocol internals remain in `src/mysql2.cpp`; `include/polycpp/mysql2/detail/aggregator.hpp` exists only as a stable internal include point.
- aggregator header strategy: keep aggregator minimal until reusable internals are intentionally exposed.
- examples strategy: examples should use only public headers and environment-driven database configuration.
- documentation site strategy: Doxygen plus Sphinx, GitHub Pages workflow present and gated so private repos build docs without deploying.
- deliberate deviations from existing companions: this repo depends on another companion (`iconv-lite`) and uses real network e2e tests; many simpler companion ports are pure utility libraries.

## Polycpp ecosystem reuse analysis

- polycpp core paths inspected: `/data/repo/polycpp/include/polycpp/buffer`, `/data/repo/polycpp/include/polycpp/crypto`, `/data/repo/polycpp/include/polycpp/io`, `/data/repo/polycpp/include/polycpp/zlib`, `/data/repo/polycpp/include/polycpp/tls`
- polycpp core types/functions selected: `polycpp::Buffer`, `polycpp::crypto::createHash`, `polycpp::crypto::publicEncrypt`, `polycpp::io::EventContext`, `polycpp::io::TcpSocket`
- polycpp core types/functions rejected: native HTTP request/response/header types are not relevant to MySQL protocol; native MySQL SDKs are not used; polycpp TLS and zlib are deferred until the transport switching behavior is implemented.
- companion libs inspected for reusable APIs: `iconv-lite`, `sequelize`, `jsonwebtoken`, and smaller HTTP utilities.
- companion libs selected for reuse: `polycpp::iconv_lite` for non-core charset decoding.
- companion libs rejected or deferred: no separate `long`, `denque`, `named-placeholders`, `sql-escaper`, or `lru.min` companion is required for the first slice.
- new local abstractions introduced: `Connection`, `ConnectionOptions`, `Field`, `Row`, `QueryResult`, `OkPacket`, and private packet cursor helpers.
- reuse risks or integration gaps: `polycpp::io` is async-first, so this port wraps it with a synchronous API for v0; timeout enforcement is not yet implemented. Full charset parity needs generated MySQL charset id data or deeper `iconv-lite` integration.

## External SDK and native driver strategy

- upstream external services/protocols: MySQL/MariaDB wire protocol over TCP.
- native SDKs/client libraries to use: none.
- SDKs/protocols explicitly not reimplemented: no native MySQL/MariaDB C client library is linked; the MySQL wire protocol is implemented directly from upstream mysql2 behavior.
- adapter/linking strategy: link only polycpp and the existing polycpp iconv-lite companion.
- test environment needs: MariaDB/MySQL server for e2e tests; local validation used MariaDB 10.6.23 on `127.0.0.1:43306`.

## Compatibility foundation review

- downstream dependency role: mysql2 is foundational for ORMs and DB adapters; many downstream packages expect query, escaping, prepared statements, pooling, charset, and auth behavior.
- native substitution risk: using a native SDK would change license, error behavior, protocol details, and feature availability; it is disallowed for this port.
- upstream implementation data to preserve: capability flags, command codes, type codes, field flags, server status flags, auth token algorithms, packet sequencing, and length-coded values.
- generated or vendored data plan: no upstream source is vendored in the first slice; constants are hand-transcribed and reviewed. Full charset id mapping should later be generated from upstream `charset_encodings.js` with license notes.
- compatibility fixture strategy: start with upstream unit fixtures for SQL escaping/auth/packet parsing, then add MariaDB/MySQL e2e coverage for handshake, query, errors, charset, and prepared statements.

## Security and fail-closed review

- security-sensitive behavior: password authentication, RSA password encryption, SQL escaping, LOCAL INFILE, TLS, and server-controlled auth plugin names.
- trust boundary: server packets, SQL/value input from callers, credentials, RSA public keys, and future file streams are untrusted boundaries.
- supported protocol or algorithm matrix: MySQL protocol v10 handshake; `mysql_native_password`; `caching_sha2_password` fast auth and RSA public-key full auth; `sha256_password` RSA auth on auth switch; text protocol query results.
- unsupported behavior and fail-closed policy: `mysql_clear_password` without TLS, LOCAL INFILE, compression, TLS, unsupported auth plugins, and malformed packets throw `polycpp::mysql2::Error`.
- key, secret, credential, or user-controlled input handling: password tokens are computed using polycpp crypto; SQL helpers escape string and buffer values; raw SQL requires explicit `raw()` use.
- misuse cases that must be tested: auth plugin downgrade attempts, malformed length-coded packets, missing named placeholder values, SQL escaping edge bytes, server ERR packets, and unsupported LOCAL INFILE request.

## Core use cases

- Connect to a MySQL/MariaDB server without native client libraries.
- Execute a text SQL query and read rows/fields.
- Execute write statements and inspect affected rows/insert id.
- Escape and format SQL values consistently with mysql2/mysqljs expectations.
- Serve as a foundation for higher-level companions such as Sequelize-style libraries.

## Key features to port first

- Packet framing and length-coded parsing.
- Handshake and auth plugins needed by MariaDB and MySQL 8.
- `COM_QUERY` text protocol.
- OK/ERR/resultset/column/text-row parsing.
- SQL escaping and placeholder formatting.
- Real database e2e tests.

## Features to defer

- TLS transport and SSL profile handling.
- Compression.
- Prepared statements and binary protocol.
- Connection pools and pool clusters.
- Streams/EventEmitter-compatible callbacks.
- LOCAL INFILE.
- Server mode and replication/binlog commands.
- Complete MySQL charset id table.

## v0 scope

- port version: 0.1.0
- supported APIs: `ConnectionOptions`, `Connection`, `create_connection`, `query`, `ping`, `end`, `escape`, `escape_id`, `format`, `format_named`, `raw`
- unsupported APIs: pools, prepared statements, promises, streaming rows, TLS, compression, server mode, binlog/replication, LOCAL INFILE, parser cache controls
- dependency plan: reuse `iconv-lite`; replace `long` with C++ integer types; replace `denque` with standard containers when command queues are added; implement SQL escaping and named placeholders locally; defer SSL profiles, generated parser functions, and LRU prepared-statement cache
- polycpp modules to use: `Buffer`, `crypto`, `io`, and the `iconv-lite` companion
- missing polycpp primitives: synchronous TCP deadline helper, documented TLS stream upgrade recipe for protocol clients, generated MySQL charset id table integration
- versioning note: port versioning is independent from upstream npm versioning; upstream basis is recorded separately and does not imply parity
