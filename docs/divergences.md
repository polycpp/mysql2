# Divergences From Upstream

## Deferred Features

- TLS and SSL profile support are deferred; cleartext-only TCP is implemented for v0.
- Compression is deferred.
- Prepared statements and binary protocol are deferred.
- Pool and pool-cluster APIs are deferred.
- Promise, callback, EventEmitter, and stream row APIs are omitted from v0.
- LOCAL INFILE is unsupported and fails closed.
- Server mode, replication, and binlog commands are deferred.
- Complete charset id mapping is deferred; common UTF-8, latin1, ascii, and binary ids are handled first.
- URI parsing is deferred; callers fill `ConnectionOptions` directly.

## Deliberate Behavior Changes

- The API is synchronous and typed rather than callback or Promise based.
- Rows use `std::variant` values and explicit field lookup rather than mutable JavaScript objects.
- Native MySQL/MariaDB client SDKs are not linked, even if installed on the system.
- `mysql_clear_password` is rejected without TLS support rather than exposed as an option.
- JavaScript parser code generation is replaced with static C++ parsing.

## Unsupported Runtime-Specific Features

- Node.js `stream.Readable`, EventEmitter events, process tick timing, and diagnostic channel hooks are not modeled in v0.
- Node Buffer pooling behavior is not replicated; the port uses `polycpp::Buffer`.
- Connection timeout option is recorded but not enforced until a polycpp deadline wrapper is added.
