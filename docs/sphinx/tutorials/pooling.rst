Pooling
=======

This tutorial focuses on ``Pool``, ``PoolConnection``, and ``PoolCluster``.
Use it to decide when a pool should own a command, when to check out a session,
and how to configure contention and failover behavior.

Pool concept
------------

A ``Pool`` owns a bounded set of reusable ``Connection`` objects. It creates
connections on demand up to ``connection_limit``. One-shot ``pool.query`` and
``pool.execute`` acquire a connection, run one command, and release it. A
``PoolConnection`` checkout is a move-only RAII handle that returns the
connection to the pool when released or destroyed.

Use a pool for service code that runs many independent operations. Use a direct
``Connection`` for one-off tools, protocol tests, or workflows where one session
must be controlled outside a pool.

Create a pool
-------------

Start with connection options, then wrap them in ``PoolOptions``:

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions connection;
   connection.host = "127.0.0.1";
   connection.user = "app";
   connection.password = "secret";
   connection.database = "app";

   polycpp::mysql2::PoolOptions options;
   options.connection = connection;
   options.connection_limit = 10;
   options.max_idle = 10;
   options.wait_for_connections = true;
   options.queue_limit = 0;
   options.wait_timeout_ms = 10000;

   auto pool = polycpp::mysql2::create_pool(options);

``max_idle`` is capped to ``connection_limit``. ``queue_limit = 0`` means an
unbounded waiter count. ``wait_timeout_ms = 0`` means wait without a deadline.

Choose pool APIs
----------------

.. list-table:: Pool API choice
   :header-rows: 1
   :widths: 24 38 38

   * - API
     - Use it for
     - Ownership behavior
   * - ``pool.query(sql)``
     - Independent text query.
     - Acquires and releases one connection internally.
   * - ``pool.query_all(sql)``
     - Independent multi-result text query.
     - Acquires and releases one connection internally.
   * - ``pool.execute(sql, values)``
     - Independent one-shot prepared execution.
     - Acquires and releases one connection internally.
   * - ``pool.get_connection()``
     - Multi-command session, transaction, temporary table, session variable,
       cursor, stream, or raw scan workflow.
     - Caller owns a ``PoolConnection`` until release/destruction.
   * - ``pool.end()``
     - Service shutdown.
     - Rejects new checkouts and closes idle/later-released connections.

One-shot pool queries
---------------------

One-shot calls are the easiest way to use a pool. They are appropriate when the
command does not depend on session state created by previous commands.

.. code-block:: cpp

   auto result = pool.query("SELECT 1 AS one");
   auto prepared = pool.execute("SELECT ? AS label", {std::string("pool")});

The connection is returned to the pool after the command finishes or throws.

Checked-out connections
-----------------------

Use ``get_connection`` when several statements must run on the same session.
The checkout handle behaves like a pointer to ``Connection``.

.. code-block:: cpp

   auto conn = pool.get_connection();
   conn->begin_transaction();
   try {
       conn->execute("INSERT INTO jobs(name) VALUES (?)", {std::string("example")});
       conn->commit();
   } catch (...) {
       conn->rollback();
       throw;
   }

The handle returns the connection to the pool when it is destroyed. Call
``release`` explicitly only when early release makes the ownership clearer:

.. code-block:: cpp

   auto conn = pool.get_connection();
   auto result = conn->query("SELECT 1 AS one");
   conn.release();
   (void)result;

Do not retain ``Connection&`` or ``Connection*`` after the ``PoolConnection``
handle is released.

Use reset_on_release for session hygiene
----------------------------------------

A checked-out connection can leave behind user variables, temporary tables,
locks, transaction state, or session variables. ``reset_on_release`` sends
``COM_RESET_CONNECTION`` before returning the connection to idle storage. If
reset fails, the pool closes that connection instead of reusing it.

.. code-block:: cpp

   polycpp::mysql2::PoolOptions options;
   options.connection = connection;
   options.connection_limit = 4;
   options.reset_on_release = true;

   auto pool = polycpp::mysql2::create_pool(options);

Enable this for multi-tenant services or code that checks out arbitrary
connections to user-controlled workflows.

Contention policy
-----------------

When every connection is checked out, the pool uses three options to decide what
happens next:

- ``wait_for_connections = false``: fail immediately.
- ``queue_limit``: maximum number of waiters; ``0`` means unbounded.
- ``wait_timeout_ms``: how long a waiter can block; ``0`` waits indefinitely.

.. code-block:: cpp

   options.connection_limit = 8;
   options.wait_for_connections = true;
   options.queue_limit = 32;
   options.wait_timeout_ms = 2000;

For latency-sensitive services, prefer a finite ``queue_limit`` and
``wait_timeout_ms`` so overload becomes visible instead of turning into an
unbounded request backlog.

Pool events
-----------

Pools emit typed lifecycle events:

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

Use ``Enqueue`` to observe contention. Use ``ConnectionCreated`` to initialize
session settings if they cannot be represented in ``ConnectionOptions``.

Pool clusters
-------------

``PoolCluster`` groups named pools. It is useful for read/write splitting,
replica selection, regional endpoints, or test matrices with multiple servers.
A single ``Pool`` is simpler when all commands target one endpoint.

.. code-block:: cpp

   polycpp::mysql2::PoolClusterOptions cluster_options;
   cluster_options.default_selector = polycpp::mysql2::PoolSelector::RoundRobin;

   auto cluster = polycpp::mysql2::create_pool_cluster(cluster_options);

   polycpp::mysql2::PoolOptions primary = options;
   primary.connection.host = "db-primary";
   cluster.add("primary", primary);

   polycpp::mysql2::PoolOptions replica_a = options;
   replica_a.connection.host = "db-replica-a";
   cluster.add("replica-a", replica_a);

   polycpp::mysql2::PoolOptions replica_b = options;
   replica_b.connection.host = "db-replica-b";
   cluster.add("replica-b", replica_b);

   auto replicas = cluster.of("replica-*", polycpp::mysql2::PoolSelector::RoundRobin);
   auto rows = replicas.query("SELECT id FROM users");

   auto writer = cluster.get_connection("primary");
   writer->execute("INSERT INTO jobs(name) VALUES (?)", {std::string("sync")});

Selectors are:

- ``RoundRobin``: rotate through matching online nodes.
- ``Random``: choose a matching online node randomly.
- ``Order``: choose the first matching online node.

Cluster failure policy
----------------------

``remove_node_error_count`` controls when repeated connection errors affect a
node. If ``restore_node_timeout_ms`` is greater than zero, the node is marked
offline until the timeout expires. If it is zero, the node is removed.

.. code-block:: cpp

   polycpp::mysql2::PoolClusterOptions cluster_options;
   cluster_options.can_retry = true;
   cluster_options.remove_node_error_count = 3;
   cluster_options.restore_node_timeout_ms = 30000;

   auto cluster = polycpp::mysql2::create_pool_cluster(cluster_options);
   cluster.on(polycpp::mysql2::event::Warn,
       [](const polycpp::mysql2::Error& error) { (void)error; });
   cluster.on(polycpp::mysql2::event::Offline,
       [](const std::string& id) { (void)id; });
   cluster.on(polycpp::mysql2::event::Online,
       [](const std::string& id) { (void)id; });
   cluster.on(polycpp::mysql2::event::Remove,
       [](const std::string& id) { (void)id; });

``can_retry`` lets the cluster try another matching online node when one node
fails. It does not make a failed SQL command idempotent; callers still need to
understand whether retrying the application operation is safe.

Shutdown
--------

Call ``end`` during service shutdown:

.. code-block:: cpp

   pool.end();
   cluster.end();

After ``end`` a pool rejects new checkouts. Existing checked-out handles are
closed when released. A cluster ``end`` closes all pools and removes all nodes.

Checklist
---------

- Use one-shot ``pool.query`` / ``pool.execute`` for independent commands.
- Use ``get_connection`` for transactions, temporary tables, session variables,
  cursors, streams, and multi-command workflows.
- Use ``reset_on_release`` when session state must not leak between callers.
- Use finite queue limits and wait timeouts in services.
- Use ``PoolCluster`` only when endpoint selection is part of the design.
