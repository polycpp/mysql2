Testing Against a Real Database
===============================

Unit tests run without an external database server:

.. code-block:: bash

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
test covers the adapted server protocol mode by connecting the port's client
to ``create_server`` and validating handshake, query, ping, text-result, and
quit behavior.
