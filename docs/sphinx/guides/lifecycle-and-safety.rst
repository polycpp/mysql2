Lifecycle and Safety Semantics
==============================

This guide summarizes behavior that differs from a naive synchronous wrapper and
matters for production use.

Connection lifecycle
--------------------

``create_connection`` constructs and connects. A default-constructed
``Connection`` or ``Connection(options)`` connects on the first operation that
needs a transport.

.. code-block:: cpp

   polycpp::mysql2::Connection conn(options);
   conn.connect();
   conn.ping();
   conn.end();

``end`` sends ``COM_QUIT`` when possible and closes the transport. ``destroy``
is noexcept and closes through the same safe shutdown path in this port.

Timeouts close the transport
----------------------------

Connect timeouts apply to the initial socket connect. Query, execute, prepare,
and fetch timeouts are inactivity deadlines around command reads/writes. If a
command timeout fires, the transport is closed before the error is reported.
This prevents later commands from reusing a partially read protocol stream.

.. code-block:: cpp

   polycpp::mysql2::QueryOptions query;
   query.sql = "SELECT SLEEP(2)";
   query.timeout_ms = 100;

   try {
       conn.query(query);
   } catch (const polycpp::mysql2::Error&) {
       // conn.connected() is false after the timeout.
   }

A later command can reconnect through the stored connection options when no
active stream owns the connection.

Single-result and multi-result APIs
-----------------------------------

Use ``query`` and ``execute`` only when one result set is expected. If the
server returns additional result sets, these APIs drain the remaining packets
and then throw so the connection stays synchronized.

.. code-block:: cpp

   options.multiple_statements = true;
   auto conn = polycpp::mysql2::create_connection(options);

   auto results = conn.query_all("SELECT 1 AS one; SELECT 2 AS two");

Use ``query_all`` and ``execute_all`` for stored procedures or multi-statement
SQL.

Active streams reserve the connection
-------------------------------------

``query_stream`` reserves the connection until EOF. Calling another command on
the same connection while the stream is active throws. If the stream is dropped
before EOF, cleanup drains remaining row packets when possible.

.. code-block:: cpp

   auto rows = conn.query_stream("SELECT id FROM users");
   while (auto row = rows.read()) {
       (void)row->at("id");
   }

``create_binlog_stream`` is stricter. Replication streams reserve the
connection and every terminal path closes the transport because a MySQL
replication command stream is not safely reusable as a normal command phase.
A later normal command reconnects through the stored options.

State-resetting operations
--------------------------

``reset`` sends ``COM_RESET_CONNECTION`` and clears the prepared statement
cache. ``change_user`` sends ``COM_CHANGE_USER``, preserves the existing
transport/TLS/compression settings, updates user/database/charset attributes,
and clears the prepared statement cache on success.

.. code-block:: cpp

   conn.reset();

   polycpp::mysql2::ConnectionOptions next_user;
   next_user.user = "reporting";
   next_user.password = "secret";
   next_user.database = "reports";
   conn.change_user(next_user);

Prepared statement cache
------------------------

``execute(sql, values)`` uses a bounded connection-local statement cache.
``close_statement(sql)`` invalidates a cached one-shot statement by SQL text.
``close_statement(statement)`` closes an explicit prepared statement id.

.. code-block:: cpp

   auto first = conn.execute("SELECT ? AS value", {int64_t{1}});
   conn.close_statement("SELECT ? AS value");

Fail-closed boundaries
----------------------

The port fails closed for security-sensitive or protocol-sensitive cases:

- unsupported auth plugins;
- ``mysql_clear_password`` without TLS or ``socket_path``;
- TLS certificate or identity verification failure when enabled;
- LOCAL INFILE without an explicit ``local_infile_handler``;
- malformed packets or unsupported lossy binlog row column types;
- missing named placeholder values;
- unexpected extra result sets in single-result APIs.

Compatibility/audit hooks
-------------------------

Parser-cache helpers are present for upstream API compatibility:
``set_max_parser_cache``, ``max_parser_cache``, and ``clear_parser_cache``.
The C++ implementation uses static parsers, so there is no generated JavaScript
parser cache to clear.

For the complete adaptation record, see ``docs/api-mapping.md`` and
``docs/divergences.md`` in the repository.
