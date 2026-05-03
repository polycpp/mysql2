Testing Against a Real Database
===============================

Unit tests run without an external database server:

.. code-block:: bash

   cmake -B build -DPOLYCPP_MYSQL2_BUILD_TESTS=ON
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure

Real database tests are enabled by environment variables:

.. code-block:: bash

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=secret \
   MYSQL2_TEST_DATABASE=test \
   ctest --test-dir build --output-on-failure

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

Current coverage
----------------

The integration test covers connection, ping, text rows, binary values, query
attributes when the server supports them, prepared statements, server-side
cursor fetch, transactions, reset, multi-result queries, compression, LOCAL
INFILE policy, pooling, pool clusters, and optional TLS. The local loopback
tests cover the adapted server protocol mode over TCP and Unix socket paths by
connecting the port's client to ``create_server`` and validating handshake,
query, ping, text-result, and quit behavior.

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
