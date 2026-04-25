# Divergences From Upstream

## Implemented With C++ Adaptation

- The API is synchronous and typed first. Callback and `polycpp::Promise` wrappers are available, but they execute the same typed operations rather than introducing a separate JavaScript command queue.
- Rows use `std::variant` values and explicit field lookup rather than mutable JavaScript objects.
- Prepared statements are explicit C++ objects. `execute(sql, values)` also uses a bounded connection-local statement cache for upstream compatibility.
- Pools are synchronous RAII pools rather than async command queues.
- Pool clusters are synchronous typed objects rather than JavaScript EventEmitter command queues, but they keep named pools, wildcard matching, RR/RANDOM/ORDER selection, retry/offline/remove policy, and typed events.
- TLS is implemented through polycpp TLS primitives. The upstream `"Amazon RDS"` SSL profile is bundled as CA PEM data generated from `aws-ssl-profiles@1.1.2`.
- Single-result `query` and `execute` calls drain unexpected additional result sets and throw; callers that expect multiple result sets must use `query_all` or `execute_all`.
- Native MySQL/MariaDB client SDKs are not linked, even if installed on the system.
- JavaScript parser code generation is replaced with static C++ parsing.
- Parser-cache controls are exposed as compatibility/audit hooks, but they do not clear generated parser code because no generated parser cache exists.
- Node diagnostic channels are adapted to typed `event::Trace` events on `Connection` for connect/query/execute start, success, and error phases.
- Per-command query/execute inactivity timeout options are adapted to `QueryOptions::timeout_ms`, `ExecuteOptions::timeout_ms`, and `CommandOptions::timeout_ms`. A timeout closes the transport and marks the connection disconnected.
- Replication commands are bounded synchronous operations. `COM_REGISTER_SLAVE` and `COM_BINLOG_DUMP` are available, with typed parsing for Query, Rotate, FormatDescription, and Xid events and raw-byte fallback for unknown binlog event types.
- Node object-mode row streams are adapted to typed `RowStream` rows plus newline-delimited JSON byte streams via `polycpp::stream::Readable`, because current polycpp stream chunks are `Buffer`/text rather than arbitrary row objects.
- LOCAL INFILE never opens a path by default. The caller must provide `ConnectionOptions::local_infile_handler`, which receives the server-requested path and returns explicit `polycpp::Buffer` chunks.
- Query attributes use `QueryAttributes`, an `std::unordered_map<std::string, Value>`. Attribute ordering on the wire is intentionally not a C++ API guarantee.
- Server-side prepared-statement cursors are explicit `StatementCursor` objects. Callers fetch batches with `Connection::fetch(...)` and close the underlying prepared statement when they are done.
- Charset and collation ids are mapped from upstream mysql2 constants and string conversion reuses the `iconv-lite` companion where polycpp Buffer does not already support the encoding. Handshake charset still must fit MySQL's one-byte handshake field.
- Connection attributes are sent during both the initial handshake and `COM_CHANGE_USER` when the server advertises connect-attribute capability.

## Deferred Features

- Server mode is deferred because it requires a separate server-side connection object, handshake/auth dispatcher, and response-writing API rather than extending the client `Connection`.
- GTID binlog dump, continuous replication stream abstraction, and full row/table-map event decoding are deferred.
- Exact Node `Readable` object-mode row chunks are deferred until polycpp stream supports arbitrary typed payload chunks.

## Security-Driven Behavior Changes

- `mysql_clear_password` requires both TLS and `ConnectionOptions::enable_cleartext_plugin`.
- TLS certificate verification is enabled by `SslOptions::reject_unauthorized`; host/IP identity verification is controlled by `SslOptions::verify_identity`.
- LOCAL INFILE requests throw unless the caller configured an explicit handler.
- Unsupported auth plugins throw instead of silently falling back after authentication starts.

## Unsupported Runtime-Specific Features

- Node.js process tick timing is not modeled.
- Node Buffer pooling behavior is not replicated; the port uses `polycpp::Buffer`.
