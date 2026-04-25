# Divergences From Upstream

## Implemented With C++ Adaptation

- The API is synchronous and typed rather than callback, Promise, or EventEmitter based.
- Rows use `std::variant` values and explicit field lookup rather than mutable JavaScript objects.
- Prepared statements are explicit C++ objects; the upstream LRU statement cache is not implemented yet.
- Pools are synchronous RAII pools rather than async command queues.
- TLS is implemented through polycpp TLS primitives. Named SSL profiles from `aws-ssl-profiles` are not bundled.
- Single-result `query` and `execute` calls drain unexpected additional result sets and throw; callers that expect multiple result sets must use `query_all` or `execute_all`.
- Native MySQL/MariaDB client SDKs are not linked, even if installed on the system.
- JavaScript parser code generation is replaced with static C++ parsing.

## Deferred Features

- Compression is deferred.
- Pool cluster APIs are deferred.
- Promise, callback, EventEmitter, diagnostic channel, and stream row APIs are omitted.
- LOCAL INFILE is unsupported and fails closed until an explicit file callback policy exists.
- Server mode, replication, and binlog commands are deferred.
- Complete charset id mapping is deferred; common UTF-8, latin1, ascii, and binary ids are handled first.
- Connection URI parsing is deferred; callers fill `ConnectionOptions` directly.
- Query attributes and named prepared-statement attributes are deferred.
- Cursor fetch APIs are deferred.

## Security-Driven Behavior Changes

- `mysql_clear_password` requires both TLS and `ConnectionOptions::enable_cleartext_plugin`.
- TLS certificate verification is enabled by `SslOptions::reject_unauthorized`; host/IP identity verification is controlled by `SslOptions::verify_identity`.
- LOCAL INFILE requests throw instead of reading local files.
- Unsupported auth plugins throw instead of silently falling back after authentication starts.

## Unsupported Runtime-Specific Features

- Node.js `stream.Readable`, EventEmitter events, process tick timing, and diagnostic channel hooks are not modeled.
- Node Buffer pooling behavior is not replicated; the port uses `polycpp::Buffer`.
- Connection timeout option is recorded but not fully enforced until a polycpp deadline wrapper is available.
