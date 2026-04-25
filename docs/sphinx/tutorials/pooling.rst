Pooling
=======

``Pool`` is a synchronous RAII pool. It is intended for C++ services that want
connection reuse without adopting Node's callback queue model.

Create a pool
-------------

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions connection;
   connection.host = "127.0.0.1";
   connection.user = "app";
   connection.password = "secret";
   connection.database = "app";

   polycpp::mysql2::PoolOptions options;
   options.connection = connection;
   options.connection_limit = 10;
   options.wait_timeout_ms = 10000;

   auto pool = polycpp::mysql2::create_pool(options);

One-shot pool queries
---------------------

.. code-block:: cpp

   auto result = pool.query("SELECT 1 AS one");
   auto prepared = pool.execute("SELECT ? AS label", {std::string("pool")});

Checked-out connections
-----------------------

Use ``get_connection`` when several statements must run on the same session,
for example a transaction or temporary table workflow.

.. code-block:: cpp

   auto conn = pool.get_connection();
   conn->begin_transaction();
   conn->query("INSERT INTO jobs(name) VALUES ('example')");
   conn->commit();

The checkout handle returns the connection to the pool when it is destroyed.
Call ``release`` explicitly only when early release is useful.

Shutdown
--------

.. code-block:: cpp

   pool.end();

After ``end`` the pool rejects new checkouts. Existing checked-out handles are
closed when released.
