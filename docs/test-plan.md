# Test Plan

## Unit Tests

- SQL string escaping for quotes, backslashes, control bytes, and NULL.
- Buffer escaping to hex literals.
- Identifier escaping for qualified and literal identifiers.
- Positional formatting for values and identifiers.
- Named placeholder formatting, including missing value failure.
- Row lookup by index and field name.
- Auth token fixtures for mysql_native_password and caching_sha2_password should be added from upstream-compatible vectors.
- Packet cursor fixtures for length-coded integers and malformed packet failures should be added before expanding protocol scope.
- Prepared-statement parameter encoding fixtures should cover NULL, signed/unsigned integers, double, string, and Buffer.
- Binary row parser fixtures should cover numeric, decimal, date/time/datetime, binary string/blob, JSON text, and NULL bitmap behavior.
- SSL profile helper coverage verifies the generated AWS RDS CA bundle is loadable as PEM strings.
- Parser-cache compatibility hooks are covered as no-op static-parser controls.
- Typed `RowStream` coverage verifies row iteration, field metadata exposure, active-stream connection reservation, and cleanup after abandoned streams. NDJSON `Readable<Buffer>` coverage verifies lazy byte serialization over the row stream.
- Typed `BinlogStream` coverage verifies `polycpp::stream::event::Data` delivery for `BinlogEvent` chunks. Replication e2e validates active-stream command rejection, CRC32 checksum negotiation, typed temporal row values, EOF transport close with reconnect-on-next-command behavior, `max_events` transport close, and destroy-before-EOF transport close against a binary-log-enabled server.
- Binlog packet parser coverage includes QueryEvent fixtures, GTID set parsing, stateful TableMap plus WriteRows decoding, Rotate, FormatDescription, Xid, GTID packet, PreviousGTIDs packet, update/delete rows, unknown-event fixtures, TIME2/DATETIME2/TIMESTAMP2 row values, and negative TIME2 fractional encoding. Additional future fixtures should focus on malformed packets and less-common row column families.
- Adapted server mode loopback coverage creates a `Server`, accepts a `Connection` client over TCP and Unix socket paths, validates handshake auth/connect attributes, dispatches query, ping, statement prepare, and statement execute events, writes text/OK responses, writes a prepared-statement OK packet and binary result rows for a real client `prepare`/`execute`, observes quit, and verifies auth callback rejection returns a MySQL ERR packet.

## Integration Tests

- Environment-driven MariaDB/MySQL test using `MYSQL2_TEST_HOST`, `MYSQL2_TEST_PORT`, `MYSQL2_TEST_USER`, `MYSQL2_TEST_PASSWORD`, and `MYSQL2_TEST_DATABASE`.
- Current e2e coverage connects to MariaDB/MySQL, runs ping, checks typed trace events, selects scalar values, exercises `QueryOptions`/`ExecuteOptions` with command timeouts, sends query attributes when supported, preserves empty binary values as Buffer, creates a temporary table, inserts rows, selects them back, uses prepared statements, prepared-statement query attributes, server-side cursor fetch, and cached execute, tests transactions, resets the connection, changes user state, tests multi-result queries, exercises callback/Promise wrappers, consumes lazy JSON line byte streams and typed row object streams, verifies compression when enabled, optionally uploads LOCAL INFILE data when the server allows it, exercises the RAII pool, exercises a single-node pool cluster, and verifies that command inactivity timeout closes the connection. Local loopback coverage validates the adapted server protocol mode, active-stream command rejection, and abandoned-stream cleanup without requiring an external database.
- TLS e2e is controlled by `MYSQL2_TEST_SSL`, `MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED`, `MYSQL2_TEST_SSL_VERIFY_IDENTITY`, and `MYSQL2_TEST_SSL_CA_FILE`.
- MySQL 8 coverage runs against a Dockerized MySQL 8.4 server and validates the default `caching_sha2_password` path.
- MySQL 8 coverage validates `CLIENT_QUERY_ATTRIBUTES` for both `COM_QUERY` and `COM_STMT_EXECUTE`.
- Add a dedicated MySQL 8 TLS clear password auth path test if a server/user is configured to require it.
- Add server ERR packet coverage with invalid SQL and access denied scenarios.
- Add charset coverage for utf8mb4, latin1, binary, and representative non-UTF encodings through `iconv-lite`.
- Add pool contention and wait-timeout coverage.
- Add multi-result stored procedure coverage.
- Environment-gated replication e2e coverage runs against a server configured with binary logging, row format, and a replication-capable user.

## Compatibility Tests Adapted From Upstream

- Adapt upstream SQL escaping and formatting fixtures.
- Adapt upstream auth plugin unit tests.
- Adapt upstream packet parser tests for length-coded numbers, OK packets, ERR packets, column definitions, text rows, and binary rows.
- Adapt upstream integration tests for simple query, insert/update, transactions, errors, charset behavior, prepared statements, TLS, compression, LOCAL INFILE, callbacks, Promise wrappers, streams, pooling, and pool clusters.

## Security and Fail-Closed Tests

- Unsupported auth plugin returns an error.
- `mysql_clear_password` without TLS or `socket_path` returns an error.
- TLS certificate chain failure returns an error when `reject_unauthorized` is true.
- TLS host/IP mismatch returns an error when `verify_identity` is true.
- LOCAL INFILE request without an explicit handler returns an error and with a handler sends only caller-provided buffers.
- Malformed length-coded packet returns an error rather than reading out of bounds.
- Missing named placeholder throws.
- Non-finite floating values escape as `NULL`.
- RSA auth path uses OAEP SHA1 to match upstream caching_sha2_password behavior.
- Single-result APIs drain additional result sets before throwing so the connection is reusable.
- Per-command inactivity timeout closes the transport and marks the connection disconnected.
- Bounded binlog dump, callback-controlled `binlog_dump_each(...)`, and `create_binlog_stream(...)` close the transport when EOF, `max_events`, callback stop, or destroy/drop cleanup ends the replication command stream, so callers do not accidentally reuse a socket that is still in or just left replication packet mode. `binlog_dump_each(...)` and `create_binlog_stream(...)` with `max_events = 0` are the documented continuous-consumption surfaces.
- Server auth callbacks can reject a client by returning an `Error`; accepted clients expose parsed `ServerAuthInfo` without validating passwords implicitly. Rejection is covered by a loopback test that expects error code 1045 / SQL state 28000.

## Release-Blocking Behaviors

- Build and unit tests pass on a clean checkout.
- Real MariaDB e2e passes without TLS.
- Real MariaDB e2e passes with verified TLS.
- Real MySQL 8 e2e passes before claiming MySQL 8 auth parity.
- Real MySQL/MariaDB replication e2e passes before claiming binlog stream lifecycle and temporal row parity.
- Stream adaptation, command timeout behavior, compression, LOCAL INFILE policy, callback/Promise wrappers, EventEmitter/trace behavior, adapted TCP/Unix server mode, bounded/callback/typed-stream binlog behavior, parser-cache compatibility hooks, and SSL profile data remain documented with exact C++ semantics.
- Third-party license notices are complete.
- Documentation builds with `python3 docs/build.py`.
- GitHub repo remains private until production-grade quality and public docs are ready.

## Current validation

Commands run on April 25, 2026:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R server_mode
python3 docs/build.py
docker run -d --name polycpp-mysql2-e2e -e MYSQL_ROOT_PASSWORD=polycpp -e MYSQL_DATABASE=polycpp_test -p 43307:3306 mysql:8.4 --local-infile=1 --mysqlx=0
docker run --rm --network container:polycpp-mysql2-e2e -v /data/work/lib/mysql2:/work -w /work ubuntu:22.04 bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_smoke --gtest_filter=mysql2_integration.query_against_real_database_when_configured'
docker run -d --name polycpp-mysql2-mariadb-e2e -e MARIADB_ROOT_PASSWORD=polycpp -e MARIADB_DATABASE=polycpp_test mariadb:10.6 --local-infile=1
docker run --rm --network container:polycpp-mysql2-mariadb-e2e -v /data/work/lib/mysql2:/work -w /work ubuntu:22.04 bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_smoke --gtest_filter=mysql2_integration.query_against_real_database_when_configured'
docker run -d --name polycpp-mysql2-mariadb-tls-e2e -e MARIADB_ROOT_PASSWORD=polycpp -e MARIADB_DATABASE=polycpp_test -v /data/work/lib/mysql2/build/mariadb-tls:/certs:ro mariadb:10.6 --local-infile=1 --ssl-ca=/certs/ca.pem --ssl-cert=/certs/server-cert.pem --ssl-key=/certs/server-key.pem --require-secure-transport=ON
docker run --rm --network container:polycpp-mysql2-mariadb-tls-e2e -v /data/work/lib/mysql2:/work -w /work ubuntu:22.04 bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_SSL=1 MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED=1 MYSQL2_TEST_SSL_VERIFY_IDENTITY=1 MYSQL2_TEST_SSL_CA_FILE=/work/build/mariadb-tls/ca.pem build/test_smoke --gtest_filter=mysql2_integration.query_against_real_database_when_configured'
```

Additional validation on April 28, 2026 after updating against current polycpp IPC/TLS primitives:

```bash
cmake --build build -j2
timeout 20s build/test_smoke --gtest_filter=server_mode.loopback_query_supports_unix_socket_path --gtest_also_run_disabled_tests
MYSQL2_TEST_SERVER_TLS_CERT_FILE=$PWD/build/mariadb-tls/server-cert.pem MYSQL2_TEST_SERVER_TLS_KEY_FILE=$PWD/build/mariadb-tls/server-key.pem timeout 20s build/test_smoke --gtest_filter=server_mode.loopback_query_supports_tls_upgrade_when_configured --gtest_also_run_disabled_tests
ctest --test-dir build --output-on-failure
```

Service versions used for e2e validation:

- MariaDB 10.6 in Docker, with a generated test CA/server certificate for the verified TLS run.
- MySQL Community Server 8.4.6 in Docker.
- Database e2e commands run from an Ubuntu 22.04 helper container sharing the database container network namespace.

Additional validation on May 1, 2026 after rolling back the same-day
TypedEvent migration in favor of the canonical
`events::TypedEvent<"name", Args...>` (fixed_string NTTP) form, dropping
the private `detail/socket_adapter.hpp` variant adapter, and switching to
`polycpp::io::PipeSocket` / `PipeAcceptor` / `StreamSocket` /
`StreamAcceptor` directly against polycpp HEAD `75bc07df`:

```bash
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_SOURCE_DIR_POLYCPP=/data/work/gitlab-workspace/polycpp
cmake --build build -j$(nproc)
(cd build && ctest --output-on-failure)
```

Result: `100% tests passed, 0 tests failed out of 14` in 0.09 s
(12 ran, 2 skipped — `server_mode.loopback_query_supports_tls_upgrade_when_configured`
needs `MYSQL2_TEST_SERVER_TLS_CERT_FILE`/`MYSQL2_TEST_SERVER_TLS_KEY_FILE`,
`mysql2_integration.query_against_real_database_when_configured` needs
`MYSQL2_TEST_HOST`/`MYSQL2_TEST_USER`).

Additional validation on May 2, 2026 against polycpp HEAD
`40bd73669e8105fcb8641ad6671dfd07141e9eff` after adopting
`polycpp::stream::event::Data` for typed binlog streams and adding explicit
mysql2 stream-wrapper move constructors required by the current non-movable
`polycpp::stream::Readable<T>` facade:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Result: `100% tests passed, 0 tests failed out of 14` in 0.39 s
(12 ran, 2 skipped — `server_mode.loopback_query_supports_tls_upgrade_when_configured`
needs `MYSQL2_TEST_SERVER_TLS_CERT_FILE`/`MYSQL2_TEST_SERVER_TLS_KEY_FILE`,
`mysql2_integration.query_against_real_database_when_configured` needs
`MYSQL2_TEST_HOST`/`MYSQL2_TEST_USER`).

Additional validation on May 2, 2026 after closing binlog checksum,
temporal-row decoding including negative TIME2 fractional values,
replication-stream lifecycle, and parser fixture gaps:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
python3 docs/build.py
git diff --check
docker run -d --name polycpp-mysql2-repl-e2e \
  -e MYSQL_ROOT_PASSWORD=polycpp \
  -e MYSQL_DATABASE=polycpp_test \
  mysql:8.4 \
  --server-id=1 \
  --log-bin=mysql-bin \
  --binlog-format=ROW \
  --local-infile=1 \
  --mysqlx=0
docker run --rm --network container:polycpp-mysql2-repl-e2e \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_REPLICATION=1 MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_smoke --gtest_filter=mysql2_replication.binlog_stream_against_real_database_when_configured'
docker run --rm --network container:polycpp-mysql2-repl-e2e \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_smoke --gtest_filter=mysql2_integration.query_against_real_database_when_configured'
docker rm -f polycpp-mysql2-repl-e2e
```

Result: local `ctest` passed `18/18` with 3 expected environment-gated skips
(TLS loopback, replication e2e, real DB e2e); docs built successfully with
warnings as errors; `git diff --check` was clean; the live MySQL 8.4
replication e2e passed in 203 ms; the live MySQL 8.4 database e2e passed in
47 ms.
