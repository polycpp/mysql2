# Divergences From Upstream

## Implemented With C++ Adaptation

- The API is synchronous and typed first. Callback and `polycpp::Promise` wrappers are available, but they execute the same typed operations rather than introducing a separate JavaScript command queue.
- Rows use `std::variant` values and explicit field lookup rather than mutable JavaScript objects.
- Prepared statements are explicit C++ objects. `execute(sql, values)` also uses a bounded connection-local statement cache for upstream compatibility.
- Pools are synchronous RAII pools rather than async command queues.
- Pool clusters are synchronous typed objects rather than JavaScript EventEmitter command queues, but they keep named pools, wildcard matching, RR/RANDOM/ORDER selection, retry/offline/remove policy, and typed events.
- TLS is implemented through polycpp TLS primitives. Named SSL profiles from `aws-ssl-profiles` are not bundled.
- Single-result `query` and `execute` calls drain unexpected additional result sets and throw; callers that expect multiple result sets must use `query_all` or `execute_all`.
- Native MySQL/MariaDB client SDKs are not linked, even if installed on the system.
- JavaScript parser code generation is replaced with static C++ parsing.
- Node object-mode row streams are adapted to newline-delimited JSON byte streams via `polycpp::stream::Readable`, because current polycpp stream chunks are `Buffer`/text rather than arbitrary row objects.
- LOCAL INFILE never opens a path by default. The caller must provide `ConnectionOptions::local_infile_handler`, which receives the server-requested path and returns explicit `polycpp::Buffer` chunks.

## Deferred Features

- Server mode, replication, and binlog commands are deferred.
- Complete charset id mapping is deferred; common UTF-8, latin1, ascii, and binary ids are handled first.
- Query attributes and named prepared-statement attributes are deferred.
- Cursor fetch APIs are deferred.
- Diagnostic channel tracing is deferred.
- Native object-mode row stream chunks are deferred until polycpp stream supports arbitrary typed payload chunks.

## Security-Driven Behavior Changes

- `mysql_clear_password` requires both TLS and `ConnectionOptions::enable_cleartext_plugin`.
- TLS certificate verification is enabled by `SslOptions::reject_unauthorized`; host/IP identity verification is controlled by `SslOptions::verify_identity`.
- LOCAL INFILE requests throw unless the caller configured an explicit handler.
- Unsupported auth plugins throw instead of silently falling back after authentication starts.

## Unsupported Runtime-Specific Features

- Node.js process tick timing and diagnostic channel hooks are not modeled.
- Node Buffer pooling behavior is not replicated; the port uses `polycpp::Buffer`.
- Connection timeout option is recorded but not fully enforced until a polycpp deadline wrapper is available.
