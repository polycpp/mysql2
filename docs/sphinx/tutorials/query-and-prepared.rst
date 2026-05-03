Query and Prepared Statements
=============================

This tutorial shows the normal request flow for a direct connection.

Create a connection
-------------------

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.host = "127.0.0.1";
   options.port = 3306;
   options.user = "root";
   options.password = "secret";
   options.database = "app";

   auto conn = polycpp::mysql2::create_connection(options);

Run a text query
----------------

.. code-block:: cpp

   auto result = conn.query("SELECT 1 AS one, 'two' AS label");
   auto one = std::get<int64_t>(result.rows[0].at("one"));
   auto label = std::get<std::string>(result.rows[0].at("label"));

Use formatting helpers when interpolating values into SQL text:

.. code-block:: cpp

   auto sql = polycpp::mysql2::format(
       "SELECT * FROM ?? WHERE id = ?",
       {std::string("users"), int64_t{42}});
   auto result = conn.query(sql);

Run a prepared statement
------------------------

Prepared statements use the MySQL binary protocol. Values are sent separately
from SQL text.

.. code-block:: cpp

   auto stmt = conn.prepare("SELECT id, name FROM users WHERE id > ?");
   auto result = conn.execute(stmt, {int64_t{10}});
   conn.close_statement(stmt);

Use one-shot execution when reuse is not needed:

.. code-block:: cpp

   auto result = conn.execute(
       "SELECT ? AS label, ? AS none_value",
       {std::string("prepared"), std::monostate{}});

Attach query attributes
-----------------------

MySQL 8 can accept named query attributes. The port sends them when the server
advertises ``CLIENT_QUERY_ATTRIBUTES``; older servers fail closed if attributes
are requested.

.. code-block:: cpp

   auto result = conn.query(
       "SELECT 1 AS one",
       {{"trace_id", std::string("audit-123")}, {"sampled", true}});

   auto prepared = conn.execute(
       "SELECT ? AS label",
       {std::string("prepared")},
       {{"trace_id", std::string("audit-124")}});

Fetch through a server-side cursor
----------------------------------

.. code-block:: cpp

   auto cursor = conn.execute_cursor("SELECT id FROM users ORDER BY id");
   while (cursor.open()) {
       auto batch = conn.fetch(cursor, 100);
       for (const auto& row : batch.rows) {
           (void)row;
       }
   }
   conn.close_statement(cursor.statement);

Handle result values
--------------------

``Value`` is a variant containing ``std::monostate``, ``bool``,
signed/unsigned integer, ``double``, ``std::string``, ``polycpp::Buffer``, or
``RawSql`` for formatter input.

Binary string and blob columns are returned as ``polycpp::Buffer``. Text
columns are decoded according to field charset metadata where supported.

Transactions
------------

.. code-block:: cpp

   conn.begin_transaction();
   conn.query("INSERT INTO audit_log(message) VALUES ('created')");
   conn.commit();

Use ``rollback`` on error paths.

High-volume scans
-----------------

For one-pass reads where callback-scoped raw packet bytes are enough, use
``query_each_raw`` instead of materializing ``QueryResult`` rows:

.. code-block:: cpp

   std::size_t rows = 0;
   conn.query_each_raw("SELECT id FROM users", [&](const auto& row) {
       const auto& id = row.at("id");
       if (!id.is_null) {
           // id.bytes is valid only during this callback.
       }
       ++rows;
   });

See :doc:`raw-row-scans` for a complete tutorial and
:doc:`/guides/raw-row-scans` for lifetime and parsing rules.

Related guides
--------------

- :doc:`/guides/sql-formatting` for escaping, identifiers, named
  placeholders, and ``raw`` fragments.
- :doc:`/guides/lifecycle-and-safety` for multi-result draining, stream
  connection ownership, timeouts, reset, and change-user behavior.
