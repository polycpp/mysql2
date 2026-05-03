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

Contention and release policy
-----------------------------

``connection_limit`` controls the maximum number of live connections. When the
pool is exhausted, ``wait_for_connections`` decides whether callers wait or fail
immediately. ``queue_limit`` limits the number of waiting callers, and
``wait_timeout_ms`` bounds how long a caller can wait. A value of ``0`` waits
without a timeout.

.. code-block:: cpp

   polycpp::mysql2::PoolOptions options;
   options.connection = connection;
   options.connection_limit = 4;
   options.wait_for_connections = true;
   options.queue_limit = 16;
   options.wait_timeout_ms = 5000;
   options.reset_on_release = true;

   auto pool = polycpp::mysql2::create_pool(options);

Use ``reset_on_release`` when checked-out code can leave session state behind.
The pool sends ``COM_RESET_CONNECTION`` before returning a connection to the
idle list. If reset fails, the connection is closed instead of being reused.

Pool events
-----------

Pools emit typed events for lifecycle and contention:

.. code-block:: cpp

   pool.on(polycpp::mysql2::event::ConnectionCreated,
       [](polycpp::mysql2::Connection& conn) {
           (void)conn.connection_id();
       });

   pool.on(polycpp::mysql2::event::Acquire,
       [](polycpp::mysql2::Connection&) {});
   pool.on(polycpp::mysql2::event::Release,
       [](polycpp::mysql2::Connection&) {});
   pool.on(polycpp::mysql2::event::Enqueue, [] {});

Pool clusters
-------------

``PoolCluster`` groups named pools. It supports wildcard matching and
``RoundRobin``, ``Random``, or ``Order`` selection.

.. code-block:: cpp

   auto cluster = polycpp::mysql2::create_pool_cluster();

   polycpp::mysql2::PoolOptions primary = options;
   primary.connection.host = "db-primary";
   cluster.add("primary", primary);

   polycpp::mysql2::PoolOptions replica = options;
   replica.connection.host = "db-replica-1";
   cluster.add("replica-1", replica);

   auto read_pool = cluster.of("replica-*", polycpp::mysql2::PoolSelector::RoundRobin);
   auto rows = read_pool.query("SELECT id FROM users");

   auto write_conn = cluster.get_connection("primary");
   write_conn->query("INSERT INTO jobs(name) VALUES ('example')");

Cluster failure policy
----------------------

``remove_node_error_count`` controls when a failing node is removed or marked
offline. If ``restore_node_timeout_ms`` is greater than zero, the node is kept
offline until the timeout expires. Otherwise it is removed from the cluster.

.. code-block:: cpp

   polycpp::mysql2::PoolClusterOptions cluster_options;
   cluster_options.can_retry = true;
   cluster_options.remove_node_error_count = 3;
   cluster_options.restore_node_timeout_ms = 30000;

   auto cluster = polycpp::mysql2::create_pool_cluster(cluster_options);
   cluster.on(polycpp::mysql2::event::Offline,
       [](const std::string& id) { (void)id; });
   cluster.on(polycpp::mysql2::event::Online,
       [](const std::string& id) { (void)id; });
   cluster.on(polycpp::mysql2::event::Remove,
       [](const std::string& id) { (void)id; });

Shutdown
--------

.. code-block:: cpp

   pool.end();

After ``end`` the pool rejects new checkouts. Existing checked-out handles are
closed when released.
