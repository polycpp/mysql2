Testing Against a Real Database
===============================

Unit tests run without an external database server:

.. code-block:: bash

   cmake -B build -DPOLYCPP_MYSQL2_BUILD_TESTS=ON
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure

The local loopback server-mode tests also run without an external MySQL
server:

.. code-block:: bash

   ctest --test-dir build --output-on-failure -R mysql2_e2e_server_protocol

Real database tests are enabled by environment variables. The tests skip
external-server cases when the required variables are absent, so CI jobs should
set them explicitly rather than relying on defaults:

.. code-block:: bash

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=secret \
   MYSQL2_TEST_DATABASE=test \
   ctest --test-dir build --output-on-failure

Required variables are ``MYSQL2_TEST_HOST``, ``MYSQL2_TEST_PORT``,
``MYSQL2_TEST_USER``, ``MYSQL2_TEST_PASSWORD``, and ``MYSQL2_TEST_DATABASE``.
Set ``MYSQL2_TEST_TRACE=1`` when debugging emitted trace events or command
ordering.

TLS e2e
-------

.. code-block:: bash

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=secret \
   MYSQL2_TEST_DATABASE=test \
   MYSQL2_TEST_SSL=1 \
   MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED=1 \
   MYSQL2_TEST_SSL_VERIFY_IDENTITY=1 \
   MYSQL2_TEST_SSL_CA_FILE=/path/to/ca.pem \
   ctest --test-dir build --output-on-failure

Use these TLS variables to exercise the same certificate validation policies
documented in :doc:`tls`. For loopback server-mode clear-password auth, create a
short-lived fixture certificate and run the focused test:

.. code-block:: bash

   mkdir -p build/e2e-tls
   openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
     -keyout build/e2e-tls/server-key.pem \
     -out build/e2e-tls/server-cert.pem \
     -subj '/CN=127.0.0.1'

   MYSQL2_TEST_SERVER_TLS_CERT_FILE=$PWD/build/e2e-tls/server-cert.pem \
   MYSQL2_TEST_SERVER_TLS_KEY_FILE=$PWD/build/e2e-tls/server-key.pem \
   build/test_e2e_server_protocol \
     --gtest_filter=mysql2_e2e_server_protocol.tls_clear_password_auth_sends_cleartext_only_when_enabled

Current coverage
----------------

The integration test covers connection, ping, text rows, binary values, query
attributes when the server supports them, prepared statements, server-side
cursor fetch, transactions, reset, multi-result queries, compression, LOCAL
INFILE policy, pooling, pool clusters, and optional TLS. The local loopback
tests cover the adapted server protocol mode over TCP and Unix socket paths by
connecting the port's client to ``create_server`` and validating handshake,
query, ping, text-result, and quit behavior.

The extended operations binary adds prepared statement type matrices, statement
cache reuse, transaction and savepoint recovery, SQL error recovery, compressed
protocol, LOCAL INFILE memory uploads, and ``reset_on_release`` session
cleanup. Replication tests are gated by ``MYSQL2_TEST_REPLICATION=1`` and cover
bounded dump reads, callback reads, binlog streams, table-map-aware parsing,
GTID parsing, and temporal row values.

Focused e2e binaries
--------------------

The repository keeps focused e2e binaries under ``tests/e2e`` so agents and CI
can rerun protocol-sensitive paths without rediscovering commands:

.. code-block:: bash

   build/test_e2e_server_protocol
   build/test_e2e_real_database
   build/test_e2e_real_database_operations
   MYSQL2_TEST_REPLICATION=1 build/test_e2e_replication

``tests/e2e/README.md`` contains reproducible Docker commands for MySQL 8.4,
row-binlog replication, and loopback TLS clear-password auth.

Docker reproduction
-------------------

For a local MySQL 8.4 server with row binlogs and LOCAL INFILE enabled:

.. code-block:: bash

   docker run -d --name polycpp-mysql2-e2e-tests \
     -e MYSQL_ROOT_PASSWORD=polycpp \
     -e MYSQL_DATABASE=polycpp_test \
     mysql:8.4 \
     --server-id=1 \
     --log-bin=mysql-bin \
     --binlog-format=ROW \
     --local-infile=1 \
     --mysqlx=0

Then run a focused binary against the container:

.. code-block:: bash

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=polycpp \
   MYSQL2_TEST_DATABASE=polycpp_test \
   build/test_e2e_real_database_operations

Use the helper-container commands in ``tests/e2e/README.md`` when host library
versions do not match the build outputs.
