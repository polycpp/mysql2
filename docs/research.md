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

`mysql2` is a Node.js MySQL/MariaDB driver. Upstream implements the MySQL wire protocol in JavaScript, including connection setup, authentication plugins, query execution, prepared statements, pooling, compression, TLS, server protocol mode, and parsing for text and binary result sets.

The C++ companion provides a polycpp-native client that preserves the pure protocol implementation model. It does not link against native MySQL or MariaDB client libraries because those introduce license and distribution concerns that do not match the requested porting strategy.

## Runtime assumptions

- browser: upstream is Node.js-only; this companion is C++ server/runtime code.
- node.js: upstream depends on Node Buffer, net, tls, crypto, stream, zlib, timers, process, and EventEmitter. The C++ port replaces those with polycpp or C++ constructs.
- filesystem: upstream uses filesystem-adjacent behavior for SSL profiles, TLS certificates, and LOCAL INFILE. The C++ port reads TLS certificate/key files only when explicitly configured; LOCAL INFILE is allowed only through an explicit caller-provided buffer handler.
- network: required. The supported client connects over TCP or Unix socket paths and can upgrade the connection to TLS; adapted server mode listens over TCP or Unix socket paths using polycpp IO primitives and can perform MySQL in-protocol TLS upgrade when `ServerOptions::tls` is configured.
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
- `lib/server.js` and `lib/commands/server_handshake.js`: upstream server protocol surface, handshake response parsing, command dispatch, and response writing.
- `website/docs/documentation/mysql-server.mdx`: upstream server-mode public behavior.
- `lib/packets/resultset_header.js`: OK packet and resultset header parsing.
- `lib/packets/column_definition.js`: field metadata parsing.
- `lib/packets/text_row.js`: text row length-coded values.
- `lib/constants/client.js`, `commands.js`, `types.js`, `field_flags.js`, `server_status.js`, `charsets.js`, `charset_encodings.js`, `encoding_charset.js`: protocol and charset constants.

## Entry points used by consumers

- `index.js`: `createConnection`, `connect`, `createPool`, `createPoolCluster`, `escape`, `escapeId`, `format`, `raw`, constants, parser-cache controls.
- `promise.js`: Promise wrapper equivalents.
- `typings/mysql/index.d.ts`: TypeScript public contract.

The C++ supported slice maps core connection, query, query attributes, prepared statement, server-side cursor fetch, TLS, pooling, pool clusters, transaction, reset, change-user, compressed packets, LOCAL INFILE handler hooks, callback overloads, Promise wrappers, typed EventEmitter integration, trace events, stream adaptation, URI parsing, charset mapping, bounded register-slave/binlog-dump commands, adapted server protocol mode, parser-cache compatibility hooks, and utility helpers. Parser cache controls are no-op audit hooks because JavaScript code-generation caches are not meaningful in C++.

## Important files and why they matter

- `lib/base/connection.js`: establishes packet sequence reset rules and command-phase behavior.
- `lib/commands/client_handshake.js`: defines which auth plugin is attempted directly and which cases fail closed.
- `lib/auth_41.js`: gives the exact mysql_native_password token construction.
- `lib/auth_plugins/caching_sha2_password.js`: required for MySQL 8 default authentication.
- `lib/packets/packet.js`: source of length-coded integer/string parsing and endian behavior.
- `lib/packets/column_definition.js`: maps result metadata needed for C++ typed rows.
- `lib/constants/*.js`: constants that must remain stable for wire compatibility.
- `lib/commands/server_handshake.js`: establishes server-mode command dispatch expectations and auth callback behavior.

## Files likely irrelevant to the C++ port

- Benchmark HTTP examples and website assets.
- Promise wrapper internals beyond public API naming; the C++ port implements polycpp Promise wrappers directly over typed operations.
- Generated JavaScript text/binary parser optimization code that exists to avoid V8 overhead; the C++ port uses static parsing.
- TypeScript declaration internals beyond public option and result shapes.

## Test directories worth mining first

- `test/unit`: packet parser, auth, SQL formatting, and parser behavior that can become C++ unit tests.
- `test/integration`: real server connection, query, error, transaction, charset, and prepared-statement behavior.
- `test/fixtures`: certificates and test data useful for TLS and protocol fixture coverage.
- `benchmarks/unit/fixtures`: performance fixtures only after correctness is established.
- `website/docs/examples`: user-facing examples that can guide C++ docs once features exist.

## Implementation risks discovered from the source layout

- Authentication is security-sensitive and stateful. Unsupported plugins must fail closed rather than silently downgrade.
- TLS and compression alter the transport after the initial handshake; both are implemented and compression must preserve separate compressed packet sequencing.
- Prepared statements use binary protocol and type encoding; single-result APIs must drain unexpected extra result sets before throwing to keep the connection synchronized.
- Charset support is broad. The port maps upstream mysql2 charset/collation ids and delegates non-core conversions to the `iconv-lite` companion; handshake charset values still must fit MySQL's one-byte handshake field.
- Upstream uses dynamic JavaScript values and parser generation; the C++ API must define explicit variant behavior.
- LOCAL INFILE is a file exfiltration boundary. The port supports it only through an explicit callback policy that returns caller-approved `polycpp::Buffer` chunks.
- Server mode is a separate protocol adapter surface rather than a client feature. It must use `polycpp::io::TcpAcceptor`, `PipeAcceptor`, and `StreamAcceptor` where appropriate, expose auth and command events, and document which response writers are available.

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

- polycpp core paths inspected: `/data/repo/polycpp/include/polycpp/buffer`, `/data/repo/polycpp/include/polycpp/crypto`, `/data/repo/polycpp/include/polycpp/io`, `/data/repo/polycpp/include/polycpp/net`, `/data/repo/polycpp/include/polycpp/zlib`, `/data/repo/polycpp/include/polycpp/tls`
- polycpp capability snapshot: `/data/repo/polycpp` HEAD `103496f2f50aad410dc63415a7f176182fb1ddd3` checked on April 28, 2026 with `git -C /data/repo/polycpp rev-parse HEAD` and targeted `rg` searches for `PipeAcceptor`, `PipeSocket`, `StreamSocket`, `StreamAcceptor`, `tls::Server`, `createServer`, `listen(const std::string`, and `NativeListenHandle`.
- transport/listener capability review: current polycpp provides TCP sockets/listeners, Unix/IPC path sockets/listeners through `PipeSocket`/`PipeAcceptor`, cross-transport `StreamSocket`/`StreamAcceptor`, native-handle adoption, `polycpp::io::TlsStream`, and direct TLS server/listener APIs through `polycpp::tls::Server` / `tls::createServer`. This port uses `StreamSocket`/`StreamAcceptor` for TCP and socket-path parity and uses protocol-internal `TlsStream` upgrade after MySQL SSLRequest rather than a direct `tls::Server`, because MySQL begins in plaintext and upgrades in-band.
- polycpp core types/functions selected: `polycpp::Buffer`, `polycpp::crypto::createHash`, `polycpp::crypto::publicEncrypt`, `polycpp::io::EventContext`, `polycpp::io::TcpSocket`, `polycpp::io::PipeSocket`, `polycpp::io::StreamSocket`, `polycpp::io::TcpAcceptor`, `polycpp::io::PipeAcceptor`, `polycpp::io::StreamAcceptor`, `polycpp::io::TlsContext`, `polycpp::io::TlsStream`, and `polycpp::ssl::X509Cert`
- polycpp core types/functions rejected: native HTTP request/response/header types are not relevant to MySQL protocol; native MySQL SDKs are not used.
- companion libs inspected for reusable APIs: `iconv-lite`, `sequelize`, `jsonwebtoken`, and smaller HTTP utilities.
- companion libs selected for reuse: `polycpp::iconv_lite` for non-core charset decoding.
- companion libs rejected or deferred: no separate `long`, `denque`, `named-placeholders`, `sql-escaper`, or `lru.min` companion is required for the supported scope.
- new local abstractions introduced: `Connection`, `ConnectionOptions`, `SslOptions`, `PreparedStatement`, `StatementCursor`, `QueryAttributes`, `PoolOptions`, `Pool`, `PoolConnection`, `Server`, `ServerConnection`, `ServerOptions`, `ServerHandshakeOptions`, `ServerAuthInfo`, `Field`, `Row`, `QueryResult`, `OkPacket`, and private packet cursor helpers.
- reuse risks or integration gaps: `polycpp::io` is async-first, so this port wraps it with a synchronous API; connect and per-command inactivity timeout enforcement use `polycpp::io::Timer`. `PipeSocket` keeps the event loop alive after connect when referenced, so the synchronous `socket_path` client path explicitly unreferences it after a successful connect. Exact Node `Readable` object-mode row chunks require a future polycpp stream payload model beyond byte/text buffers, so this port exposes a typed `RowStream` and an NDJSON byte stream adapter.

## Node parity surface audit

- callback APIs: upstream callback entry points are preserved as `std::function` overloads for connection, query, execute, prepare, transaction, ping, reset, change-user, register-slave/binlog-dump, end, pool, and pool-cluster APIs; they execute the same typed implementation and receive `std::exception_ptr` on failure.
- Promise APIs: upstream Promise wrapper entry points are mapped to `polycpp::Promise` wrappers over the typed implementation. They settle through polycpp primitives rather than a JavaScript command queue.
- EventEmitter APIs: connection, pool, pool-cluster, server, and server-connection event surfaces are mapped to typed `polycpp::events::EventEmitter` integration. Error-event specializations are registered for connection, pool, cluster, server, and server-connection objects. Node diagnostic channels are adapted to typed `TraceEvent` emissions for connect/query/execute phases.
- stream APIs: upstream object-mode row streams are adapted to typed `RowStream` rows and `polycpp::stream::Readable` byte chunks containing newline-delimited JSON rows because current polycpp streams are byte/text oriented, not arbitrary row object chunks.
- Buffer and binary APIs: packet buffers, binary parameters, binary result columns, and LOCAL INFILE chunks use `polycpp::Buffer` so byte payloads are not downgraded to `std::string`.
- URL, timer, process, and filesystem APIs: connection URI parsing uses `polycpp::url`; connect deadlines use `polycpp::io::Timer`; pool wait behavior uses C++ chrono; process globals are not a public C++ surface; filesystem access is limited to explicitly configured TLS certificate/key paths and LOCAL INFILE caller-provided buffers.
- crypto, compression, TLS, network, and HTTP APIs: auth tokens and RSA encryption use `polycpp::crypto`; compression uses `polycpp::zlib`; client TCP/Unix/TLS transport uses `polycpp::io` and `polycpp::ssl`; server TCP/Unix listening uses `polycpp::io::TcpAcceptor`, `PipeAcceptor`, and `StreamAcceptor`; MySQL in-protocol server TLS uses server-side `polycpp::io::TlsStream`; HTTP APIs are not relevant to the MySQL protocol.
- unsupported Node-specific APIs and audit reason: exact Node `Readable` object-mode row chunks and exact Node `createBinlogStream` object/EventEmitter shape are deferred or adapted because they require object-mode stream/event shapes that are not meaningful as direct C++ API copies.

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
- generated or vendored data plan: protocol constants and charset/collation mappings are transcribed from upstream mysql2 constants; AWS RDS CA PEM data is generated from `aws-ssl-profiles@1.1.2` into `src/aws_rds_ca.inc` and consumed through the isolated `src/aws_rds_ca.cpp` translation unit; MIT license attribution is retained in third-party notices.
- compatibility fixture strategy: start with upstream unit fixtures for SQL escaping/auth/packet parsing, then add MariaDB/MySQL e2e coverage for handshake, query, query attributes, cursor fetch, errors, charset, TLS, prepared statements, multi-results, and pooling.

## Security and fail-closed review

- security-sensitive behavior: password authentication, RSA password encryption, SQL escaping, LOCAL INFILE, TLS, and server-controlled auth plugin names.
- trust boundary: server packets, SQL/value input from callers, credentials, RSA public keys, and future file streams are untrusted boundaries.
- supported protocol or algorithm matrix: MySQL protocol v10 client handshake over TCP or Unix socket paths; adapted server handshake over TCP or Unix socket paths; optional client TLS upgrade; optional MySQL in-protocol server TLS upgrade; compressed packet protocol; `mysql_native_password`; `caching_sha2_password` fast auth plus TLS/socket-path/RSA full auth; `sha256_password` TLS/socket-path/RSA auth; TLS-or-socket-path-gated `mysql_clear_password`; text protocol query results; prepared statement binary protocol; server text/binary result packet writing; explicit LOCAL INFILE handler; classic and GTID binlog dump commands; table-map-aware binlog row parsing; multi-result draining.
- unsupported behavior and fail-closed policy: `mysql_clear_password` without TLS or `socket_path`, LOCAL INFILE without an explicit handler, unsupported auth plugins, malformed packets, and unexpected multiple result sets in single-result APIs throw `polycpp::mysql2::Error`.
- result-set/framing drain policy, if protocol client: single-result APIs drain additional result sets before throwing; `query_all`/`execute_all` expose all result sets; bounded `binlog_dump` closes the connection when `max_events` stops a live replication command stream; `binlog_dump_each` is callback-controlled and closes when the callback returns false.
- binary payload type-mapping policy, if protocol client: wire packets, binary parameters, binary result columns, LOCAL INFILE chunks, and binlog raw event/row slices use `polycpp::Buffer`; scalar row values use `Value`; TIME2/DATETIME2/TIMESTAMP2 binlog values remain raw `Buffer` because `Value` has no temporal binary type.
- stateful parser/session-state policy, if protocol client/server: connection state owns handshake capabilities, sequence ids, compression/TLS flags, prepared statement cache, cursor status, and binlog table-map state; `BinlogParser` exposes explicit table-map state for callers that parse replication packets outside a connection.
- server/listener response writer matrix, if protocol server surface exists: adapted server mode supports TCP and Unix socket listening, optional MySQL TLS upgrade, OK, ERR, EOF, column definitions, text rows/results, binary rows/results, prepared-statement OK metadata, raw packet observation, auth acceptance, and auth rejection ERR packets; it does not implement a SQL engine.
- key, secret, credential, or user-controlled input handling: password tokens are computed using polycpp crypto; SQL helpers escape string and buffer values; raw SQL requires explicit `raw()` use.
- misuse cases that must be tested: auth plugin downgrade attempts, malformed length-coded packets, missing named placeholder values, SQL escaping edge bytes, server ERR packets, and LOCAL INFILE requests without an explicit handler.

## Core use cases

- Connect to a MySQL/MariaDB server without native client libraries.
- Execute a text SQL query and read rows/fields.
- Execute prepared statements with binary protocol parameters.
- Execute write statements and inspect affected rows/insert id.
- Use TLS with certificate chain and host/IP verification.
- Reuse connections through a synchronous RAII pool.
- Escape and format SQL values consistently with mysql2/mysqljs expectations.
- Provide a lightweight adapted MySQL protocol server for tests, proxies, and controlled command handling.
- Serve as a foundation for higher-level companions such as Sequelize-style libraries.

## Key features to port first

- Packet framing and length-coded parsing.
- Handshake and auth plugins needed by MariaDB and MySQL 8.
- `COM_QUERY` text protocol.
- Prepared statement binary protocol.
- TLS transport upgrade after SSLRequest on client connections and configured server connections.
- OK/ERR/resultset/column/text-row parsing.
- Binary row parsing.
- SQL escaping and placeholder formatting.
- Real database e2e tests.

## Features to defer

- Exact Node `Readable` object-mode row chunks; current C++ adapters expose typed `RowStream` rows and newline-delimited JSON `Buffer` chunks.
- Exact Node `createBinlogStream` object/EventEmitter shape; current C++ adapters expose bounded vector reads, callback-controlled reads, GTID dump options, and explicit `BinlogParser` state.
- No additional transport/server primitives are deferred after the current polycpp IPC and TLS-server update; only API shapes listed below remain adapted or deferred.

## v0 scope

- port version: 0.1.0
- supported APIs: `ConnectionOptions`, `ConnectionOptions::socket_path`, `SslOptions`, `TraceEvent`, `CommandOptions`, `QueryOptions`, `ExecuteOptions`, `Connection`, `PreparedStatement`, `StatementCursor`, `QueryAttributes`, `RowStream`, `RegisterSlaveOptions`, `BinlogDumpOptions`, `BinlogEvent`, `BinlogParser`, `BinlogGtidSource`, `PoolOptions`, `Pool`, `PoolConnection`, `PoolCluster`, `PoolNamespace`, `ServerOptions`, `ServerOptions::socket_path`, `ServerTlsOptions`, `ServerHandshakeOptions`, `ServerAuthInfo`, `ServerStatementExecuteInfo`, `Server`, `Server::listen(path)`, `ServerConnection`, `create_connection`, `create_pool`, `create_pool_cluster`, `create_server`, `query`, `query_all`, `execute`, `execute_all`, `execute_cursor`, `fetch`, `register_slave`, `binlog_dump`, `binlog_dump_each`, `parse_binlog_event_packet`, `parse_gtid_set`, `prepare`, `close_statement`, `begin_transaction`, `commit`, `rollback`, `change_user`, `reset`, `ping`, `end`, `destroy`, `pause`, `resume`, callback overloads, `polycpp::Promise` wrappers, typed `EventEmitter` events, server protocol events, trace events, `query_stream`, `query_stream_json`, connection URI parsing, command timeouts, compressed protocol, LOCAL INFILE handler, charset helpers, SSL profile helpers, parser-cache compatibility hooks, `escape`, `escape_id`, `format`, `format_named`, `raw`
- unsupported APIs: exact Node `createBinlogStream` object/EventEmitter shape, exact Node `Readable` object-mode row chunks
- dependency plan: reuse `iconv-lite`; replace `long` with C++ integer types; replace `denque` with standard containers and a synchronous pool; implement SQL escaping and named placeholders locally; implement statement cache in-repo; generate AWS RDS CA PEM data from `aws-ssl-profiles`; expose generated JS parser controls as no-op audit hooks
- polycpp modules to use: `Buffer`, `Promise`, `events`, `stream`, `url`, `zlib`, `crypto`, `io`, `TcpSocket`, `PipeSocket`, `StreamSocket`, `TcpAcceptor`, `PipeAcceptor`, `StreamAcceptor`, `ssl`, TLS, and the `iconv-lite` companion
- missing polycpp primitives: native typed object-mode stream chunks
- versioning note: port versioning is independent from upstream npm versioning; upstream basis is recorded separately and does not imply parity
