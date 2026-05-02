# API Mapping

| Upstream symbol | C++ symbol | Status | Notes |
|---|---|---|---|
| `mysql.createConnection(options)` | `polycpp::mysql2::create_connection(ConnectionOptions)` | adapted | Creates and connects a typed C++ connection. |
| `mysql.createConnection(uri)` | `polycpp::mysql2::create_connection(std::string)`, `parse_connection_uri()` | adapted | Uses `polycpp::url` and maps known query options to typed `ConnectionOptions`. |
| `mysql.connect(options)` | `polycpp::mysql2::create_connection(ConnectionOptions)` | adapted | Alias behavior is represented by the same factory. |
| `Connection#query(sql, callback)` | `Connection::query(...)`, callback overloads, `query_promise(...)` | adapted | Synchronous typed return is primary; callback and `polycpp::Promise<QueryResult>` wrappers are provided for Node parity. If multiple result sets are returned, packets are drained and an error tells callers to use `query_all`. |
| query attributes | `Connection::query(sql, QueryAttributes)`, `query_all(sql, QueryAttributes)`, callback and Promise overloads | adapted | Sends `CLIENT_QUERY_ATTRIBUTES` metadata for `COM_QUERY` when the server advertises support. Attribute ordering follows `std::unordered_map` iteration and is not public API. |
| multiple result callback shape | `Connection::query_all(const std::string&)` | adapted | Returns `std::vector<QueryResult>` for multi-statement and stored-procedure style results. |
| `Connection#execute(sql, values, callback)` | `Connection::execute(sql, values)`, callback overloads, `execute_promise(...)` | adapted | Uses a connection-local prepared statement cache and binary protocol parameters. |
| prepared statement query attributes | `Connection::execute(..., QueryAttributes)`, `execute_all(..., QueryAttributes)`, callback and Promise overloads | adapted | Sends named attributes through `COM_STMT_EXECUTE` when the server advertises support. |
| multi-result execute | `Connection::execute_all(...)` | adapted | Returns all result sets for prepared execution. |
| `Connection#prepare(sql, callback)` | `Connection::prepare(const std::string&)`, callback overload, `prepare_promise(...)` | adapted | Returns `PreparedStatement` metadata. |
| `PreparedStatementInfo#execute(values)` | `Connection::execute(statement, values)` | adapted | Explicit connection-owned execution. |
| `PreparedStatementInfo#close()` | `Connection::close_statement(statement)` | adapted | Sends `COM_STMT_CLOSE`. |
| `unprepare(sql)` | `Connection::close_statement(sql)` | adapted | Closes and removes a cached prepared statement when present. |
| server-side prepared statement cursors | `Connection::execute_cursor(...)`, `Connection::fetch(...)`, `StatementCursor` | adapted | Uses `COM_STMT_EXECUTE` cursor flags and `COM_STMT_FETCH`. The cursor object records server status and field metadata. |
| `createPool`, `Pool` | `polycpp::mysql2::create_pool(PoolOptions)`, `Pool` | adapted | Synchronous RAII pool with callback/Promise wrappers and typed acquire/connection/release/enqueue events. |
| pool checkout/release | `Pool::get_connection()`, `PoolConnection` | adapted | Move-only RAII checkout handle releases on destruction. |
| `createPoolCluster`, `PoolCluster` | `polycpp::mysql2::create_pool_cluster(...)`, `PoolCluster`, `PoolNamespace` | adapted | Implements named pools, wildcard matching, RR/RANDOM/ORDER selection, retry/offline/remove behavior, and typed events. |
| `Connection#beginTransaction` | `Connection::begin_transaction()` | adapted | Executes `START TRANSACTION`. |
| `Connection#commit` | `Connection::commit()` | adapted | Executes `COMMIT`. |
| `Connection#rollback` | `Connection::rollback()` | adapted | Executes `ROLLBACK`. |
| `Connection#ping(callback)` | `Connection::ping()` | adapted | Synchronous OK packet return. |
| `Connection#reset(callback)` | `Connection::reset()` | adapted | Sends `COM_RESET_CONNECTION`. |
| `Connection#changeUser(options)` | `Connection::change_user(ConnectionOptions)` | adapted | Sends `COM_CHANGE_USER`; clears prepared statement cache on success. |
| `Connection#end()` | `Connection::end()` | adapted | Sends COM_QUIT when connected, then closes transport. |
| `Connection#destroy()` | `Connection::destroy()` | adapted | Closes the transport through the same safe shutdown path as `end()`. |
| `Connection#pause()` / `resume()` | `Connection::pause()` / `resume()` | adapted | Records paused state for API compatibility. The current command execution path is synchronous, so there is no background packet pump to pause. |
| `socketPath` option | `ConnectionOptions::socket_path` | adapted | Connects through `polycpp::io::PipeSocket` via `polycpp::io::StreamSocket`. URI parsing also accepts `socketPath` / `socket_path` query parameters. |
| `ssl` option object | `ConnectionOptions::ssl` | adapted | Supports TLS enablement, CA/cert/key files or PEM, default trust store loading, certificate verification, host/IP identity checks, and `SslOptions::profile = "Amazon RDS"` CA data from `aws-ssl-profiles@1.1.2`. |
| `enableCleartextPlugin` | `ConnectionOptions::enable_cleartext_plugin` | adapted | Requires TLS or `socket_path` before `mysql_clear_password` can be used. |
| `compress` option | `ConnectionOptions::compress` | adapted | Implements the MySQL compressed packet protocol using `polycpp::zlib`. |
| `infileStreamFactory` | `ConnectionOptions::local_infile_handler` | adapted | Requires an explicit caller-provided callback returning `polycpp::Buffer` chunks; no filesystem path is opened implicitly. |
| `mysql.escape(value)` | `polycpp::mysql2::escape(Value)` | adapted | Uses C++ variant values; buffers become hex literals. |
| `mysql.escapeId(identifier)` | `polycpp::mysql2::escape_id(std::string)` | adapted | Preserves qualified identifier handling. |
| `mysql.format(sql, values)` | `polycpp::mysql2::format(sql, values)` | adapted | Supports `?` values and `??` identifiers. |
| `mysql.raw(sql)` | `polycpp::mysql2::raw(sql)` | adapted | Explicit raw SQL variant bypasses escaping. |
| `namedPlaceholders` option | `polycpp::mysql2::format_named(sql, map)` | adapted | Separate helper rather than connection option. |
| `connectAttributes` | `ConnectionOptions::connect_attributes` | adapted | Merged with default client name/version attributes and sent during handshake and `COM_CHANGE_USER`. |
| `connectTimeout` | `ConnectionOptions::connect_timeout_ms` | adapted | Enforced around the initial TCP or Unix socket connect using `polycpp::io::Timer`. |
| `QueryOptions.timeout` / command inactivity timeout | `QueryOptions::timeout_ms`, `ExecuteOptions::timeout_ms`, `CommandOptions::timeout_ms` | adapted | Enforced around synchronous command reads/writes using `polycpp::io::Timer`; timeout closes the transport and marks the connection disconnected, matching mysql2's fatal timeout behavior. |
| `charset`, `charsetNumber` | `ConnectionOptions::charset`, `charset_number`, `get_charset_number`, `get_charset_encoding` | adapted | Uses upstream mysql2 charset/collation ids and `iconv-lite` companion encoding support for non-core string conversions. |
| `ConnectionConfig` | `ConnectionOptions` | adapted | Typed subset of host, port, user, password, database, charset, auth key, SSL, connect attributes, and flags. |
| `PoolConfig` | `PoolOptions` | adapted | Connection options plus connection limit and wait timeout. |
| `FieldPacket` / `ColumnDefinition` | `Field` | adapted | Public immutable metadata struct. |
| text/binary row object | `Row` | adapted | Variant vector plus name lookup; no JS object prototype behavior. |
| `Query#stream()` object-mode rows | `Connection::query_stream(...)`, `Connection::query_stream_json(...)` | adapted | `query_stream(...)` exposes `RowStream`, a pull-based `polycpp::stream::Readable<Row>` subclass that decodes row packets on stream consumption. `query_stream_json(...)` exposes a lazy newline-delimited JSON `polycpp::stream::Readable<Buffer>` adapter over the same row stream. |
| `ResultSetHeader` / OK packet | `OkPacket` | adapted | Affected rows, insert id, status, warnings, info. |
| `Types`, `FieldFlags` constants | `polycpp::mysql2::constants::*` | direct | Protocol constants exposed for result interpretation. |
| `createBinlogStream`, `_registerSlave`, `_binlogDump` | `Connection::register_slave(...)`, `Connection::create_binlog_stream(...)`, `Connection::binlog_dump(...)`, `Connection::binlog_dump_each(...)`, `BinlogStream`, `BinlogParser`, `parse_binlog_event_packet(...)`, `parse_gtid_set(...)` | adapted | Provides replication command support for classic and GTID dumps. `create_binlog_stream(...)` exposes a pull-based `BinlogStream`, a `polycpp::stream::Readable<BinlogEvent>` subclass whose chunks are delivered through `polycpp::stream::event::Data`. Bounded vector reads, callback-controlled reads, explicit parser state, CRC32 checksum negotiation, and typed TIME2/DATETIME2/TIMESTAMP2 row values remain available. Query, Rotate, FormatDescription, Xid, GTID, PreviousGTIDs, TableMap, and common row events are typed. |
| `createServer`, `Server#listen`, `Server#close` | `create_server(ServerOptions)`, `Server::listen(...)`, `Server::close()` | adapted | TCP and Unix socket listeners are built on `polycpp::io::TcpAcceptor`, `PipeAcceptor`, and `StreamAcceptor`. |
| `Connection#serverHandshake(args)` in server mode | `ServerConnection::server_handshake(ServerHandshakeOptions)` | adapted | Sends a MySQL protocol v10 server greeting, parses the client handshake response, exposes `ServerAuthInfo`, and supports an auth callback that can fail closed with an ERR packet. |
| MySQL server-side TLS upgrade after SSLRequest | `ServerOptions::tls`, `ServerTlsOptions` | adapted | Advertises `CLIENT_SSL` when configured with certificate/key material and upgrades the accepted stream with server-side `polycpp::io::TlsStream`. This is MySQL in-protocol TLS, not a direct `tls.createServer` replacement. |
| server command events: `query`, `ping`, `quit`, `init_db`, `field_list`, `stmt_prepare`, `stmt_execute`, `packet` | typed `event::ServerQuery`, `ServerPing`, `ServerQuit`, `ServerInitDb`, `ServerFieldList`, `ServerStatementPrepare`, `ServerStatementExecute`, `ServerPacket` | adapted | Event callbacks receive a `ServerConnection&` and typed payloads. `stmt_execute` receives `ServerStatementExecuteInfo` with parsed id/flags/iteration, raw payload, and best-effort values. Handlers write explicit OK/ERR/result responses where needed. |
| server response writers: `writeOk`, `writeError`, `writeTextResult`, `writeBinaryRow`, `writeColumns`, `writeEof` | `ServerConnection::write_ok`, `write_error`, `write_text_result`, `write_binary_result`, `write_binary_row`, `write_columns`, `write_eof`, `write_statement_prepare_ok` | adapted | Provides OK, ERR, column, EOF, text row/result, binary row/result, and prepared-statement OK packet writers. A SQL execution engine is not implied by server mode. |
| parser cache controls | `set_max_parser_cache(...)`, `max_parser_cache()`, `clear_parser_cache()` | adapted | Compatibility hooks only. The static C++ parser has no generated parser cache to clear. |
| `node:diagnostics_channel` tracing | `event::Trace` / `TraceEvent` | adapted | Emits typed start/success/error events for connect/query/execute through the existing `polycpp::events::EventEmitter` surface. |

## Framework Object Boundary Review

- Upstream reads or mutates framework/request/response/context objects: no HTTP or web framework objects are involved; upstream reads and writes MySQL connection, socket, command, packet, and config objects.
- Upstream fields or methods read: connection options, SSL options, server capability flags, auth plugin names, packet buffers, field metadata, and command state.
- Upstream fields or methods written: connection authorization state, packet sequence ids, command queues, auth plugin state, parser caches, result arrays, and pool queues.
- C++ adapter boundary: public API exposes `ConnectionOptions`, `SslOptions`, `Connection`, `PreparedStatement`, `PoolOptions`, `Pool`, `Server`, `ServerConnection`, `QueryResult`, `Field`, `Row`, and `Value`; packet sequencing and auth state remain private in `src/mysql2.cpp`.
- Partial mutation risk on validation failure: connection setup throws before marking the connection connected; unsupported auth and malformed packets fail closed. Single-result APIs drain unexpected additional result sets before throwing so the connection remains synchronized.

No polycpp HTTP request, response, or header type should be introduced for this package. The relevant ecosystem boundary is polycpp Buffer, crypto, IO, TLS, and companion charset decoding.

## Node parity surface review

- Callback APIs: supported as `std::function` overloads for public connection, pool, and cluster operations; callbacks receive `std::exception_ptr` plus typed result values.
- Promise APIs: supported through `polycpp::Promise` wrappers for connect, query, execute, prepare, transaction helpers, ping, reset, change-user, end, register-slave/binlog-dump, pool queries, and cluster queries.
- EventEmitter APIs: supported through typed `polycpp::events::EventEmitter` forwarding on `Connection`, `Pool`, `PoolCluster`, `Server`, and `ServerConnection`; event names are exposed as constants.
- Stream APIs: `Query#stream()` is adapted to `Connection::query_stream(...)` for pull-based typed `RowStream` rows and `Connection::query_stream_json(...)`, a lazy `polycpp::stream::Readable<Buffer>` of newline-delimited JSON chunks. `createBinlogStream` maps to `Connection::create_binlog_stream(...)`, a pull-based typed `BinlogStream` whose data chunks are consumed with `polycpp::stream::event::Data`. A connection with an active query stream rejects new commands until the stream reaches EOF or is destroyed/dropped, at which point remaining packets are drained to keep the protocol synchronized. A connection with an active binlog stream rejects new commands until EOF, `max_events`, or destruction; callback-controlled binlog dump reads reject overlapping commands until EOF, `max_events`, or callback stop. Every terminal replication-stream path closes the transport and a later command reconnects through the configured options.
- Buffer and binary APIs: mapped to `polycpp::Buffer` for wire packets, binary SQL values, binary result columns, and LOCAL INFILE chunk uploads.
- URL, timer, process, and filesystem APIs: URI parsing maps to `polycpp::url`; connect and command inactivity deadlines use `polycpp::io::Timer`; pool wait settings use C++ chrono; process APIs are not public; filesystem access is limited to explicit TLS file options and caller-provided LOCAL INFILE data.
- Crypto, compression, TLS, network, and HTTP APIs: auth crypto maps to `polycpp::crypto`; compression maps to `polycpp::zlib`; client TCP/Unix/TLS maps to `polycpp::io`/`polycpp::ssl`; server TCP/Unix listening maps to `polycpp::io::TcpAcceptor`, `PipeAcceptor`, and `StreamAcceptor`; MySQL in-protocol server TLS maps to server-side `polycpp::io::TlsStream`; HTTP is not part of this driver boundary.
- Unsupported or non-meaningful Node-specific APIs and audit reason: exact JavaScript object identity/prototype behavior is not modeled. Query row object-mode chunks map to `RowStream`; binlog object-mode chunks map to `BinlogStream`.
