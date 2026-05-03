# mysql2 e2e tests

These tests are checked into the repo so agents and CI can rerun production-grade protocol paths without rediscovering commands.

## Loopback server protocol

Runs without an external database:

```bash
ctest --test-dir build --output-on-failure -R mysql2_e2e_server_protocol
```

TLS clear-password auth is environment-gated because it needs a server certificate and key:

```bash
mkdir -p build/e2e-tls
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -keyout build/e2e-tls/server-key.pem \
  -out build/e2e-tls/server-cert.pem \
  -subj '/CN=127.0.0.1'

MYSQL2_TEST_SERVER_TLS_CERT_FILE=$PWD/build/e2e-tls/server-cert.pem \
MYSQL2_TEST_SERVER_TLS_KEY_FILE=$PWD/build/e2e-tls/server-key.pem \
build/test_e2e_server_protocol --gtest_filter=mysql2_e2e_server_protocol.tls_clear_password_auth_sends_cleartext_only_when_enabled
```

## Real database

Start a local MySQL 8.4 server with row binlogs:

```bash
docker run -d --name polycpp-mysql2-e2e-tests \
  -e MYSQL_ROOT_PASSWORD=polycpp \
  -e MYSQL_DATABASE=polycpp_test \
  mysql:8.4 \
  --server-id=1 \
  --log-bin=mysql-bin \
  --binlog-format=ROW \
  --local-infile=1 \
  --mysqlx=0
```

Run the test binary from a helper container sharing the database container
network namespace:

```bash
docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_real_database'
```

The real database e2e suite covers charset/binary decoding, stored-procedure multi-results, and pool contention/wait-timeout recovery.
The extended operations suite covers prepared statement type matrices,
statement-cache reuse, transaction/savepoint recovery, SQL error recovery,
compressed protocol, LOCAL INFILE policy and memory uploads, and pool
`reset_on_release` session cleanup:

```bash
docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_real_database_operations'
```

## Replication

The replication e2e suite is environment-gated. The MySQL 8.4 container in
the real database section already enables row binlogs and can be reused:

```bash
docker run --rm --network container:polycpp-mysql2-e2e-tests \
  -v /data/work/lib/mysql2:/work \
  -w /work \
  ubuntu:22.04 \
  bash -lc 'apt-get update >/dev/null && apt-get install -y libicu70 libssl3 >/dev/null && MYSQL2_TEST_REPLICATION=1 MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=3306 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=polycpp MYSQL2_TEST_DATABASE=polycpp_test MYSQL2_TEST_TRACE=1 build/test_e2e_replication'
```
