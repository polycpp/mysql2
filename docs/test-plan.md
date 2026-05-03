# Test Plan

## Unit Tests

- SQL string escaping for quotes, backslashes, control bytes, and NULL.
- Buffer escaping to hex literals.
- Identifier escaping for qualified and literal identifiers.
- Positional formatting for values and identifiers.
- Named placeholder formatting, including missing value failure.
- Row lookup by index and field name.
- Authentication coverage includes real MySQL 8 `caching_sha2_password`, native-password loopback auth, auth rejection, and TLS-gated `mysql_clear_password` through `tests/e2e/test_server_protocol.cpp`.
- Packet hardening coverage includes malformed binlog packet failures, truncated row events, unsupported row column types, invalid temporal precision, SQL formatting fixture edges, and missing named placeholder failure in `tests/test_protocol_hardening.cpp`.
- Prepared-statement parameter encoding coverage includes real DB execute paths, server-side statement execute event parsing, NULL, signed integers, string, Buffer/binary values, and query attribute execution.
- Binary/text row parser coverage includes numeric, decimal, date/time/datetime, binary string/blob, JSON text, empty binary values, NULL values, and server-side binary result rows.
- SSL profile helper coverage verifies the generated AWS RDS CA bundle is loadable as PEM strings.
- Parser-cache compatibility hooks are covered as no-op static-parser controls.
- Typed `RowStream` coverage verifies row iteration, field metadata exposure, active-stream connection reservation, and cleanup after abandoned streams. NDJSON `Readable<Buffer>` coverage verifies lazy byte serialization over the row stream.
- Typed `BinlogStream` coverage verifies `polycpp::stream::event::Data` delivery for `BinlogEvent` chunks. Replication e2e validates active-stream command rejection, CRC32 checksum negotiation, typed temporal row values, EOF transport close with reconnect-on-next-command behavior, `max_events` transport close, and destroy-before-EOF transport close against a binary-log-enabled server.
- Binlog packet parser coverage includes QueryEvent fixtures, GTID set parsing, stateful TableMap plus WriteRows decoding, Rotate, FormatDescription, Xid, GTID packet, PreviousGTIDs packet, update/delete rows, unknown-event fixtures, TIME2/DATETIME2/TIMESTAMP2 row values, negative TIME2 fractional encoding, malformed packet failures, unsupported column fail-closed behavior, and less-common row column families including integer widths, float/double, YEAR, DATE, TIME, DATETIME, TIMESTAMP, NEWDECIMAL, BIT, BLOB, JSON, ENUM, and SET.
- Adapted server mode loopback coverage creates a `Server`, accepts a `Connection` client over TCP and Unix socket paths, validates handshake auth/connect attributes, dispatches query, ping, statement prepare, and statement execute events, writes text/OK responses, writes a prepared-statement OK packet and binary result rows for a real client `prepare`/`execute`, observes quit, and verifies auth callback rejection returns a MySQL ERR packet.

## Integration Tests

- Environment-driven MariaDB/MySQL test using `MYSQL2_TEST_HOST`, `MYSQL2_TEST_PORT`, `MYSQL2_TEST_USER`, `MYSQL2_TEST_PASSWORD`, and `MYSQL2_TEST_DATABASE`.
- Current e2e coverage connects to MariaDB/MySQL, runs ping, checks typed trace events, selects scalar values, exercises `QueryOptions`/`ExecuteOptions` with command timeouts, sends query attributes when supported, preserves empty binary values as Buffer, creates a temporary table, inserts rows, selects them back, uses prepared statements, prepared-statement query attributes, server-side cursor fetch, and cached execute, tests transactions, resets the connection, changes user state, tests multi-result queries, exercises callback/Promise wrappers, consumes lazy JSON line byte streams and typed row object streams, verifies compression when enabled, optionally uploads LOCAL INFILE data when the server allows it, exercises the RAII pool, exercises a single-node pool cluster, and verifies that command inactivity timeout closes the connection. Local loopback coverage validates the adapted server protocol mode, active-stream command rejection, and abandoned-stream cleanup without requiring an external database.
- TLS e2e is controlled by `MYSQL2_TEST_SSL`, `MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED`, `MYSQL2_TEST_SSL_VERIFY_IDENTITY`, and `MYSQL2_TEST_SSL_CA_FILE`. Loopback server TLS clear-password auth is controlled by `MYSQL2_TEST_SERVER_TLS_CERT_FILE` and `MYSQL2_TEST_SERVER_TLS_KEY_FILE`.
- MySQL 8 coverage runs against a Dockerized MySQL 8.4 server and validates the default `caching_sha2_password` path.
- MySQL 8 coverage validates `CLIENT_QUERY_ATTRIBUTES` for both `COM_QUERY` and `COM_STMT_EXECUTE`.
- Dedicated TLS clear-password auth coverage is in `tests/e2e/test_server_protocol.cpp`; it verifies cleartext is rejected unless `enable_cleartext_plugin` is set and the transport is TLS.
- Server ERR packet coverage is in `tests/e2e/test_server_protocol.cpp`; auth rejection and query ERR packets verify MySQL error code and SQL state propagation.
- Charset coverage is in `tests/e2e/test_real_database.cpp`; it verifies utf8mb4, latin1, binary Buffer preservation, and Shift-JIS/iconv decoding when the server advertises `sjis`.
- Pool contention and wait-timeout recovery coverage is in `tests/e2e/test_real_database.cpp`.
- Stored-procedure multi-result coverage is in `tests/e2e/test_real_database.cpp`.
- Extended real database operations coverage is in `tests/e2e/test_real_database_operations.cpp`; it verifies prepared statement type round trips, statement-cache reuse, transaction/savepoint rollback, post-error connection reuse, compressed protocol negotiation, LOCAL INFILE fail-closed and explicit memory handler paths, and pool `reset_on_release` session cleanup.
- Environment-gated replication e2e coverage runs against a server configured with binary logging, row format, and a replication-capable user.

## Compatibility Tests Adapted From Upstream

- Upstream-style SQL escaping and formatting edge fixtures are covered in `tests/test_protocol_hardening.cpp` and `tests/test_smoke.cpp`.
- Upstream-style auth, ERR packet, column, text row, binary row, prepared statement, stream, pooling, and pool-cluster behavior is covered by a mix of loopback server tests and environment-gated real database tests.
- Additional upstream fixture mining remains useful for breadth, but the current v0 release-blocking surfaces are represented by repo-owned tests.

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
- Optional benchmark tooling builds with `POLYCPP_MYSQL2_BUILD_BENCHMARKS=ON`. Native MySQL C API comparison is explicitly opt-in through `POLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API=ON` and is not linked into the production companion library.
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

Additional validation on May 2, 2026 after adding dedicated test-hardening
executables for malformed packets, less-common binlog row column families,
TLS-gated clear-password auth, server ERR packets, charset matrix coverage,
pool contention/wait-timeout, stored-procedure multi-results, and
upstream-style SQL fixture edges:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
python3 docs/build.py
git diff --check

docker run -d --name polycpp-mysql2-e2e-tests \
  -e MYSQL_ROOT_PASSWORD=polycpp \
  -e MYSQL_DATABASE=polycpp_test \
  mysql:8.4 \
  --server-id=1 \
  --log-bin=mysql-bin \
  --binlog-format=ROW \
  --local-infile=1 \
  --mysqlx=0

docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_real_database'

docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_REPLICATION=1 MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_replication'

mkdir -p build/e2e-tls
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -keyout build/e2e-tls/server-key.pem \
  -out build/e2e-tls/server-cert.pem \
  -subj '/CN=127.0.0.1'

MYSQL2_TEST_SERVER_TLS_CERT_FILE=$PWD/build/e2e-tls/server-cert.pem \
MYSQL2_TEST_SERVER_TLS_KEY_FILE=$PWD/build/e2e-tls/server-key.pem \
build/test_e2e_server_protocol --gtest_filter=mysql2_e2e_server_protocol.tls_clear_password_auth_sends_cleartext_only_when_enabled

docker rm -f polycpp-mysql2-e2e-tests
```

Result: local `ctest` passed `29/29` with 8 expected environment-gated skips;
docs built successfully with warnings as errors; `git diff --check` was clean;
the live MySQL 8.4 real database e2e passed 3 tests in 190 ms; the live MySQL
8.4 replication e2e passed in 144 ms; the TLS clear-password loopback e2e
passed in 56 ms.

Additional validation on May 3, 2026 after adding the extended real database
operations e2e suite and optional benchmark tooling:

```bash
cmake -B build \
  -DPOLYCPP_MYSQL2_BUILD_TESTS=ON \
  -DPOLYCPP_MYSQL2_BUILD_BENCHMARKS=ON \
  -DPOLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure

docker run -d --name polycpp-mysql2-e2e-tests \
  -e MYSQL_ROOT_PASSWORD=polycpp \
  -e MYSQL_DATABASE=polycpp_test \
  mysql:8.4 \
  --server-id=1 \
  --log-bin=mysql-bin \
  --binlog-format=ROW \
  --local-infile=1 \
  --mysqlx=0

docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_real_database_operations'

docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_real_database && MYSQL2_TEST_REPLICATION=1 MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_replication'

docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 libmariadb3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_BENCHMARK_ITERATIONS=20 MYSQL2_BENCHMARK_ROWS=20 build/bench_mysql2'
```

Result: local `ctest` passed `33/33` with 12 expected environment-gated skips;
the extended live MySQL 8.4 operations e2e passed 4 tests in 74 ms; the existing
live MySQL 8.4 real database e2e passed 3 tests in 276 ms; the live MySQL 8.4
replication e2e passed in 198 ms; the opt-in native C API benchmark executable
built and produced CSV comparison output for `text_select_1`, `prepared_add`,
and `fetch_rows`.

Additional validation on May 3, 2026 after expanding Sphinx documentation for
connection configuration, callback/Promise/event/stream adapters, pool
clusters, server protocol mode, binlog/replication reads, benchmarking, and
public option comments:

```bash
python3 docs/build.py
cmake --build build -j2
git diff --check
ctest --test-dir build --output-on-failure
```

Result: docs built successfully with warnings as errors; `cmake --build` passed;
`git diff --check` was clean; local `ctest` passed `33/33` with 12 expected
environment-gated skips.

Additional validation on May 3, 2026 after the final documentation audit added
SQL formatting/escaping guidance, lifecycle and fail-closed semantics, expanded
API summary coverage, and focused e2e testing notes:

```bash
python3 docs/build.py
git diff --check
ctest --test-dir build --output-on-failure
cmake --build build -j2
```

Result: docs built successfully with warnings as errors; `git diff --check` was
clean; local `ctest` passed `33/33` with 12 expected environment-gated skips;
`cmake --build` reported no work to do.
