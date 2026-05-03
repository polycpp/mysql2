API Concepts and Class Guide
============================

This tutorial explains the shape of the C++ API before you choose a specific
query, pool, stream, server, or binlog feature. Read it when porting code from
upstream mysql2 or when deciding which class should own a workflow.

Design model
------------

The port keeps mysql2's pure wire-protocol model, but the public API is C++
first:

- Resource ownership is explicit through C++ objects and RAII.
- Row values are typed ``std::variant`` values, not JavaScript objects.
- Callback and ``polycpp::Promise`` APIs are adapters over the same typed
  operations.
- Event and stream APIs use polycpp typed ``EventEmitter`` and
  ``stream::Readable<T>`` primitives.
- Security-sensitive behavior fails closed instead of silently falling back.

Core object map
---------------

.. list-table:: Public class concepts
   :header-rows: 1
   :widths: 24 38 38

   * - Class or type
     - What it owns
     - Use it when
   * - ``ConnectionOptions``
     - Host, port, socket path, user credentials, database, charset, TLS,
       compression, connect attributes, and LOCAL INFILE policy.
     - You need to describe how a client connection should be established.
   * - ``Connection``
     - One MySQL protocol session, prepared statement cache, stream state,
       negotiated capabilities, and transport lifecycle.
     - A workflow needs one session, transactions, temporary tables, session
       variables, streaming, or explicit lifecycle control.
   * - ``QueryOptions`` / ``ExecuteOptions``
     - SQL text, optional query attributes, values, and command timeout.
     - One command needs attributes or a timeout rather than only SQL text.
   * - ``QueryResult``
     - ``OkPacket``, field metadata, and retained typed rows.
     - You want ordinary materialized query results.
   * - ``Row`` / ``Value`` / ``Field``
     - Row values, field-name lookup, and column metadata.
     - You need typed access by index/name and metadata-driven conversion.
   * - ``PreparedStatement``
     - Server statement id, parameter fields, result fields, and original SQL.
     - You repeatedly execute one statement and control when it is closed.
   * - ``StatementCursor``
     - Server-side prepared-statement cursor state.
     - You need batch fetches without receiving a whole prepared result at once.
   * - ``RowStream``
     - A pull-based ``Readable<Row>`` tied to an active query result.
     - You want typed rows without retaining the whole result in memory.
   * - ``RawRowView`` / ``RawValueView``
     - Callback-scoped packet views.
     - You need high-throughput one-pass scans and can parse/copy bytes inside
       the callback.
   * - ``Pool`` / ``PoolConnection``
     - A bounded set of reusable connections and checkout handles.
     - Service code needs connection reuse and automatic release.
   * - ``PoolCluster`` / ``PoolNamespace``
     - Named pools, wildcard selection, and retry/offline/remove policy.
     - You need read/write splitting or multiple database endpoints.
   * - ``Server`` / ``ServerConnection``
     - A MySQL protocol listener and accepted client protocol sessions.
     - You are writing a protocol fixture, test server, or proxy surface.
   * - ``BinlogStream`` / ``BinlogParser``
     - Replication command stream or standalone table-map-aware parser state.
     - You consume binlog packets or build replication tests/tools.

Choosing a query API
--------------------

.. list-table:: Query API choice
   :header-rows: 1
   :widths: 24 38 38

   * - API
     - Best fit
     - Important behavior
   * - ``query(sql)``
     - Simple text protocol SQL and normal materialized rows.
     - Returns one ``QueryResult``. Extra result sets are drained, then an error
       tells you to use ``query_all``.
   * - ``query(QueryOptions)``
     - Text query with query attributes or command timeout.
     - Timeout closes the transport to avoid reusing a partial packet stream.
   * - ``query_all(...)``
     - Stored procedures or multi-statement SQL.
     - Returns every result set as ``std::vector<QueryResult>``.
   * - ``execute(sql, values)``
     - One-shot prepared execution.
     - Uses a bounded connection-local prepared statement cache.
   * - ``prepare`` + ``execute(statement)``
     - Repeated prepared execution with explicit statement lifecycle.
     - Caller closes the statement with ``close_statement(statement)``.
   * - ``execute_cursor`` + ``fetch``
     - Server-side prepared-statement cursor batches.
     - Cursor owns the prepared statement metadata; close it when done.
   * - ``query_stream``
     - Typed row streaming without retaining all rows.
     - The connection is reserved until EOF or stream cleanup.
   * - ``query_each_raw``
     - One-pass high-throughput text scans.
     - Raw byte views are valid only during the callback.

Choosing an ownership model
---------------------------

Use a direct ``Connection`` when the session matters. Transactions, temporary
tables, session variables, ``change_user``, ``reset``, query streams, raw scans,
and binlog streams all need clear connection ownership.

Use ``Pool::query`` or ``Pool::execute`` for independent one-shot work where the
pool can acquire and release a connection internally. Use
``Pool::get_connection`` when multiple statements must share the same session:

.. code-block:: cpp

   auto checkout = pool.get_connection();
   checkout->begin_transaction();
   try {
       checkout->execute("INSERT INTO jobs(name) VALUES (?)", {std::string("sync")});
       checkout->commit();
   } catch (...) {
       checkout->rollback();
       throw;
   }

Use ``PoolCluster`` only when endpoint selection is part of the problem. A
single ``Pool`` is simpler and easier to reason about for one database endpoint.

Error model
-----------

Protocol and server errors throw ``polycpp::mysql2::Error``. Server ERR packets
carry a numeric code and SQL state:

.. code-block:: cpp

   try {
       conn.query("SELECT broken");
   } catch (const polycpp::mysql2::Error& error) {
       auto code = error.code();
       auto state = error.sql_state();
       (void)code;
       (void)state;
   }

Callback APIs receive ``std::exception_ptr``. Promise APIs reject with the same
underlying exception object wrapped by polycpp's Promise machinery.

Where to go next
----------------

- :doc:`query-and-prepared` for query, execute, attributes, cursors, and
  transactions.
- :doc:`pooling` for pool ownership and pool cluster selection.
- :doc:`raw-row-scans` for scan-oriented packet views.
- :doc:`server-mode` for protocol fixture/server surfaces.
- :doc:`binlog-replication` for replication reads and binlog parser state.
