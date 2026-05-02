# Divergences From Upstream

## Implemented With C++ Adaptation

- Current validation targets polycpp HEAD `40bd73669e8105fcb8641ad6671dfd07141e9eff`.
  It keeps the canonical `events::TypedEvent<"name", Args...>`
  (fixed_string NTTP) form, the non-CRTP `events::EventEmitterForwarder`
  base, and the `events::ErrorEventOf<>` trait. It also exposes typed
  stream data delivery through `polycpp::stream::event::Data`, which mysql2
  uses for both `RowStream` and `BinlogStream`.
- The private `include/polycpp/mysql2/detail/socket_adapter.hpp`
  variant adapter introduced as a workaround in the same migration is
  gone. mysql2 now uses `polycpp::io::PipeSocket` /
  `polycpp::io::PipeAcceptor` / `polycpp::io::StreamSocket` /
  `polycpp::io::StreamAcceptor` directly — those four primitives are
  exported by current polycpp HEAD.
- The API is synchronous and typed first. Callback and `polycpp::Promise` wrappers are available, but they execute the same typed operations rather than introducing a separate JavaScript command queue.
- Rows use `std::variant` values and explicit field lookup rather than mutable JavaScript objects.
- Prepared statements are explicit C++ objects. `execute(sql, values)` also uses a bounded connection-local statement cache for upstream compatibility.
- Pools are synchronous RAII pools rather than async command queues.
- Pool clusters are synchronous typed objects rather than JavaScript EventEmitter command queues, but they keep named pools, wildcard matching, RR/RANDOM/ORDER selection, retry/offline/remove policy, and typed events.
- TLS is implemented through polycpp TLS primitives. The upstream `"Amazon RDS"` SSL profile is bundled as CA PEM data generated from `aws-ssl-profiles@1.1.2`. Server mode uses MySQL in-protocol TLS upgrade after SSLRequest when `ServerOptions::tls` is configured; it is not modeled as a direct `tls.createServer` public surface.
- Single-result `query` and `execute` calls drain unexpected additional result sets and throw; callers that expect multiple result sets must use `query_all` or `execute_all`.
- Native MySQL/MariaDB client SDKs are not linked, even if installed on the system.
- JavaScript parser code generation is replaced with static C++ parsing.
- Parser-cache controls are exposed as compatibility/audit hooks, but they do not clear generated parser code because no generated parser cache exists.
- Node diagnostic channels are adapted to typed `event::Trace` events on `Connection` for connect/query/execute start, success, and error phases.
- Per-command query/execute inactivity timeout options are adapted to `QueryOptions::timeout_ms`, `ExecuteOptions::timeout_ms`, and `CommandOptions::timeout_ms`. A timeout closes the transport and marks the connection disconnected.
- Replication commands are typed C++ operations. `COM_REGISTER_SLAVE`, `COM_BINLOG_DUMP`, and `COM_BINLOG_DUMP_GTID` are available. `Connection::create_binlog_stream(...)` exposes `BinlogStream`, a pull-based `polycpp::stream::Readable<BinlogEvent>` subclass that emits chunks through `polycpp::stream::event::Data`. `BinlogParser` keeps table-map state so TableMap and common WriteRows/UpdateRows/DeleteRows events decode into typed row changes; Query, Rotate, FormatDescription, Xid, GTID, and PreviousGTIDs events are also typed. CRC32 binlog checksums are negotiated through `@master_binlog_checksum` and stripped before typed parsing. TIME2, DATETIME2, and TIMESTAMP2 row values decode into `BinlogTime`, `BinlogDateTime`, and `BinlogTimestamp`. Raw packet/body and row slices are retained for audit and unsupported event families.
- A binlog stream is a replication command stream, not a reusable normal-command phase. EOF, `max_events`, callback stop, or destroy/drop cleanup closes the transport and releases the active stream guard; a later normal command reconnects through the configured connection options. This is stricter than trying to reuse the same socket after replication EOF because real MySQL 8.4 validation showed the socket is not reliably reusable for normal commands after a non-blocking binlog EOF.
- Node object-mode row streams map to `RowStream`, a `polycpp::stream::Readable<Row>` subclass. Result metadata is read when the stream is created; row packets are decoded on `read()` or flowing consumption. The connection remains reserved for that stream until EOF or destroy/drop cleanup drains the remaining packets. `query_stream_json()` is a lazy newline-delimited JSON `polycpp::stream::Readable<Buffer>` adapter for byte-stream consumers.
- LOCAL INFILE never opens a path by default. The caller must provide `ConnectionOptions::local_infile_handler`, which receives the server-requested path and returns explicit `polycpp::Buffer` chunks.
- Query attributes use `QueryAttributes`, an `std::unordered_map<std::string, Value>`. Attribute ordering on the wire is intentionally not a C++ API guarantee.
- Server-side prepared-statement cursors are explicit `StatementCursor` objects. Callers fetch batches with `Connection::fetch(...)` and close the underlying prepared statement when they are done.
- Charset and collation ids are mapped from upstream mysql2 constants and string conversion reuses the `iconv-lite` companion where polycpp Buffer does not already support the encoding. Handshake charset still must fit MySQL's one-byte handshake field.
- Connection attributes are sent during both the initial handshake and `COM_CHANGE_USER` when the server advertises connect-attribute capability.
- Server mode is adapted to explicit `Server` and `ServerConnection` objects rather than reusing the client `Connection`. It supports TCP and Unix socket listening, optional MySQL in-protocol TLS upgrade, server handshake/auth inspection, typed command events including statement prepare/execute, raw packet observation, statement-prepare OK packets, and OK/ERR/text/binary-result writers. It does not imply a SQL execution engine.

## Intentionally Unsupported Or Adapted Features

- Unsupported binlog event families keep their raw event body for audit instead of inventing partial typed structures. Unsupported row column types fail closed rather than producing lossy values.
- Exact JavaScript object identity, prototypes, process tick timing, and Node Buffer pooling are not modeled in the C++ port.

## Security-Driven Behavior Changes

- `mysql_clear_password` requires `ConnectionOptions::enable_cleartext_plugin` and either TLS or `ConnectionOptions::socket_path`.
- TLS certificate verification is enabled by `SslOptions::reject_unauthorized`; host/IP identity verification is controlled by `SslOptions::verify_identity`.
- LOCAL INFILE requests throw unless the caller configured an explicit handler.
- Unsupported auth plugins throw instead of silently falling back after authentication starts.

## Unsupported Runtime-Specific Features

- Node.js process tick timing is not modeled.
- Node Buffer pooling behavior is not replicated; the port uses `polycpp::Buffer`.
