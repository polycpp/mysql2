Query and Prepared Statements
=============================

This tutorial focuses on the direct ``Connection`` query surface: text queries,
prepared statements, query attributes, cursors, result handling, transactions,
and choosing the right API for each command shape.

Connection as the command owner
-------------------------------

``Connection`` owns one MySQL protocol session. It stores negotiated server
capabilities, the prepared statement cache, active stream state, connection
attributes, and current transport settings. Use a direct connection when the
session itself matters: transactions, temporary tables, server-side cursors,
query streams, raw scans, ``reset``, or ``change_user``.

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.host = "127.0.0.1";
   options.port = 3306;
   options.user = "root";
   options.password = "secret";
   options.database = "app";

   auto conn = polycpp::mysql2::create_connection(options);

A default-constructed ``Connection`` or ``Connection(options)`` can also be
connected explicitly with ``connect``. Operations that need the transport call
into the same connection state.

Choose the query shape
----------------------

.. list-table:: Direct connection command APIs
   :header-rows: 1
   :widths: 24 38 38

   * - API
     - Use it for
     - Notes
   * - ``query(sql)``
     - Text protocol SQL with one expected result set.
     - Returns retained typed rows in ``QueryResult``.
   * - ``query(QueryOptions)``
     - Text SQL plus query attributes or timeout.
     - Attributes require server capability support.
   * - ``query_all``
     - Stored procedures or multi-statement SQL.
     - Returns every result set and keeps the connection synchronized.
   * - ``execute(sql, values)``
     - One-shot prepared statements.
     - Uses the connection-local statement cache.
   * - ``prepare`` + ``execute(statement)``
     - Reusing one prepared statement intentionally.
     - Caller closes the server statement id.
   * - ``execute_cursor`` + ``fetch``
     - Batch reads from a server-side prepared cursor.
     - Useful when a prepared result is large.
   * - ``query_stream``
     - Pull-based typed rows without retaining all rows.
     - The connection is reserved until the stream completes.
   * - ``query_each_raw``
     - High-throughput one-pass text scans.
     - Values are packet-backed views valid only during the callback.

Run a text query
----------------

``query`` sends ``COM_QUERY`` and parses text-protocol rows into ``Row`` and
``Value`` objects.

.. code-block:: cpp

   auto result = conn.query("SELECT 1 AS one, 'two' AS label");
   auto one = std::get<int64_t>(result.rows[0].at("one"));
   auto label = std::get<std::string>(result.rows[0].at("label"));

``QueryResult`` contains:

- ``fields``: column metadata, including name, type, flags, charset, and
  encoding.
- ``rows``: retained typed rows.
- ``ok``: affected rows, insert id, warnings, info, and server status.

Use ``row.at(index)`` when column order is enough, and ``row.at(name)`` when the
field name is clearer. Parsed rows share one result-level name index, so name
lookup does not rebuild a map per row.

Use text formatting carefully
-----------------------------

Prepared statements should be the default for user-controlled values. Formatting
helpers are useful for identifiers or trusted dynamic SQL fragments.

.. code-block:: cpp

   auto sql = polycpp::mysql2::format(
       "SELECT ?? FROM ?? WHERE id = ?",
       {std::string("email"), std::string("users"), int64_t{42}});
   auto result = conn.query(sql);

Use :doc:`/guides/sql-formatting` before assembling SQL text dynamically. Do
not wrap user input in ``raw``.

Use one-shot prepared execution
-------------------------------

``execute(sql, values)`` prepares the SQL, executes it with binary protocol bind
parameters, and keeps a bounded statement cache for repeated SQL strings. This
matches upstream mysql2's ``execute`` helper concept while using typed C++
values.

.. code-block:: cpp

   auto result = conn.execute(
       "SELECT ? AS label, ? AS none_value",
       {std::string("prepared"), std::monostate{}});

Use one-shot ``execute`` when you want safe parameter binding but do not need to
manage a ``PreparedStatement`` object.

Prepare manually when lifecycle matters
---------------------------------------

Manual preparation gives you a ``PreparedStatement`` containing the server id,
original query, parameter fields, and result fields. Use it when the statement
is executed repeatedly in a tight path or when you want explicit close timing.

.. code-block:: cpp

   auto stmt = conn.prepare("SELECT id, name FROM users WHERE id > ?");
   auto first = conn.execute(stmt, {int64_t{10}});
   auto second = conn.execute(stmt, {int64_t{20}});
   conn.close_statement(stmt);

The statement belongs to this connection. ``reset``, ``change_user``, disconnect,
or reconnect invalidates server-side statement ids and clears the connection's
statement cache. Prepare again after those operations.

Close cached one-shot statements
--------------------------------

``execute(sql, values)`` uses the SQL text as the cache key. If you know a
cached statement is no longer useful, close it explicitly by SQL text:

.. code-block:: cpp

   conn.execute("SELECT ? AS value", {int64_t{1}});
   conn.close_statement("SELECT ? AS value");

This mirrors upstream ``unprepare(sql)`` behavior but keeps the C++ name aligned
with explicit statement closing.

Attach query attributes
-----------------------

Query attributes attach metadata to one SQL statement without changing SQL text
or session state. Typical uses are request ids, tenant ids, audit context, or
trace sampling. The server must advertise ``CLIENT_QUERY_ATTRIBUTES``; older
servers fail closed when attributes are requested.

.. code-block:: cpp

   polycpp::mysql2::QueryOptions traced;
   traced.sql = "SELECT mysql_query_attribute_string('trace_id') AS trace_id";
   traced.attributes = {{"trace_id", std::string("audit-123")}};

   auto result = conn.query(traced);

Prepared execution supports the same attribute map:

.. code-block:: cpp

   polycpp::mysql2::ExecuteOptions prepared;
   prepared.sql = "SELECT ? AS label";
   prepared.values = {std::string("prepared")};
   prepared.attributes = {{"trace_id", std::string("audit-124")}};

   auto result = conn.execute(prepared);

``QueryAttributes`` is an ``std::unordered_map``. Attribute wire order is not a
public contract.

Use command timeouts
--------------------

``QueryOptions::timeout_ms``, ``ExecuteOptions::timeout_ms``, and
``CommandOptions::timeout_ms`` are inactivity deadlines around command reads and
writes. If the timeout fires, the transport closes before the error is reported.

.. code-block:: cpp

   polycpp::mysql2::QueryOptions slow;
   slow.sql = "SELECT SLEEP(2)";
   slow.timeout_ms = 100;

   try {
       conn.query(slow);
   } catch (const polycpp::mysql2::Error&) {
       // conn.connected() is now false.
   }

Closing the transport on timeout is deliberate: it avoids running later commands
on a stream whose packet boundary is unknown.

Fetch through a server-side cursor
----------------------------------

Use a cursor when a prepared result set is large and you want explicit batches.
The cursor object records the prepared statement and result metadata.

.. code-block:: cpp

   auto cursor = conn.execute_cursor("SELECT id FROM users ORDER BY id");
   while (cursor.open()) {
       auto batch = conn.fetch(cursor, 100);
       for (const auto& row : batch.rows) {
           auto id = std::get<int64_t>(row.at("id"));
           (void)id;
       }
   }
   conn.close_statement(cursor.statement);

Close the cursor's statement when the cursor workflow is done.

Handle multiple result sets
---------------------------

Single-result APIs are strict. If ``query`` or ``execute`` receives additional
result sets, mysql2 drains them and throws so the connection remains usable. Use
``query_all`` or ``execute_all`` when multiple results are expected.

.. code-block:: cpp

   options.multiple_statements = true;
   auto multi = polycpp::mysql2::create_connection(options);
   auto results = multi.query_all("SELECT 1 AS first; SELECT 2 AS second");

Stored procedures should also use ``query_all`` unless the caller knows exactly
one result set is returned.

Transactions
------------

Transactions are helpers over SQL transaction commands. Use direct
``Connection`` or a checked-out ``PoolConnection`` so all statements stay on the
same session.

.. code-block:: cpp

   conn.begin_transaction();
   try {
       conn.execute("INSERT INTO audit_log(message) VALUES (?)", {std::string("created")});
       conn.commit();
   } catch (...) {
       conn.rollback();
       throw;
   }

Use savepoints with normal SQL when partial rollback inside a transaction is
needed.

Handle result values
--------------------

``Value`` is a variant containing ``std::monostate``, ``bool``, signed/unsigned
integer, ``double``, ``std::string``, ``polycpp::Buffer``, ``RawSql`` for
formatter input, and binlog temporal value types.

Binary string and blob columns are returned as ``polycpp::Buffer``. Text columns
are decoded according to field charset metadata where supported. DECIMAL values
remain strings unless ``decimal_numbers`` is enabled. See
:doc:`/guides/type-mapping` for conversion policy.

Observe errors and traces
-------------------------

Server ERR packets throw ``polycpp::mysql2::Error`` with code and SQL state.
Trace events expose operation, phase, SQL text, duration, and error string for
connect/query/execute operations.

.. code-block:: cpp

   conn.on(polycpp::mysql2::event::Trace,
       [](const polycpp::mysql2::TraceEvent& event) {
           if (event.phase == "error") {
               (void)event.error;
           }
       });

   try {
       conn.query("SELECT broken");
   } catch (const polycpp::mysql2::Error& error) {
       (void)error.code();
       (void)error.sql_state();
   }

When to leave this API
----------------------

Use ``Pool`` when many independent operations need connection reuse. Use
``query_stream`` for large typed reads. Use ``query_each_raw`` for a one-pass
scan where raw packet bytes are enough. Use binlog APIs for replication packets,
not normal query results.

Related guides
--------------

- :doc:`api-concepts` for the class map and API selection table.
- :doc:`/guides/sql-formatting` for escaping, identifiers, named placeholders,
  and ``raw`` fragments.
- :doc:`/guides/lifecycle-and-safety` for multi-result draining, stream
  connection ownership, timeouts, reset, and change-user behavior.
