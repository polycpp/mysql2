Quickstart
==========

This quickstart connects to a database, runs a query, executes a prepared
statement, and closes the connection.

Connection
----------

.. code-block:: cpp

   #include <iostream>
   #include <polycpp/mysql2/mysql2.hpp>

   int main() {
       polycpp::mysql2::ConnectionOptions options;
       options.host = "127.0.0.1";
       options.port = 3306;
       options.user = "root";
       options.password = "secret";
       options.database = "app";

       auto conn = polycpp::mysql2::create_connection(options);
       auto result = conn.query("SELECT 1 AS one, 'polycpp' AS label");

       std::cout << std::get<int64_t>(result.rows[0].at("one")) << "\n";
       std::cout << std::get<std::string>(result.rows[0].at("label")) << "\n";
   }

Prepared statement
------------------

.. code-block:: cpp

   auto stmt = conn.prepare("SELECT id, name FROM users WHERE id > ?");
   auto rows = conn.execute(stmt, {int64_t{10}});
   conn.close_statement(stmt);

Use the one-shot helper if you do not need to reuse the server-side statement:

.. code-block:: cpp

   auto rows = conn.execute("SELECT ? AS label", {std::string("prepared")});

Multiple result sets
--------------------

Use ``query_all`` or ``execute_all`` when the SQL can return more than one
result set. Single-result APIs drain extra packets and throw to keep the
connection synchronized.

.. code-block:: cpp

   options.multiple_statements = true;
   auto conn = polycpp::mysql2::create_connection(options);
   auto results = conn.query_all("SELECT 1 AS a; SELECT 2 AS b");

TLS
---

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.host = "db.example.com";
   options.user = "app";
   options.password = "secret";
   options.ssl.enabled = true;
   options.ssl.ca_file = "/etc/ssl/certs/db-ca.pem";

   auto conn = polycpp::mysql2::create_connection(options);

Pool
----

.. code-block:: cpp

   polycpp::mysql2::PoolOptions pool_options;
   pool_options.connection = options;
   pool_options.connection_limit = 10;

   auto pool = polycpp::mysql2::create_pool(pool_options);
   auto result = pool.query("SELECT 1 AS one");

Next steps
----------

- :doc:`../tutorials/api-concepts` for the class map and API selection guide.
- :doc:`../tutorials/index` for task-oriented walkthroughs.
- :doc:`../guides/index` for focused operational recipes.
- :doc:`../guides/connection-configuration` for URI, timeout, compression,
  LOCAL INFILE, and charset options.
- :doc:`../guides/sql-formatting` before assembling SQL text dynamically.
- :doc:`../guides/lifecycle-and-safety` for connection ownership and
  fail-closed behavior.
- :doc:`../api/index` for the public API summary.
