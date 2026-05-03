SQL Formatting and Escaping
===========================

Prefer prepared statements for untrusted values. Use formatting helpers when
SQL text must be assembled dynamically, for example for identifiers or optional
clauses.

Prepared statements first
-------------------------

Prepared statements send values separately from SQL text and are the safer
choice for user-controlled values.

.. code-block:: cpp

   auto result = conn.execute(
       "SELECT id, email FROM users WHERE id = ?",
       {int64_t{42}});

Use ``prepare`` when the statement is reused and ``execute(sql, values)`` for
one-shot execution through the statement cache.

Escape scalar values
--------------------

``escape`` returns SQL literal text for a single value.

.. code-block:: cpp

   auto quoted = polycpp::mysql2::escape(std::string("Ada's laptop"));
   auto null_value = polycpp::mysql2::escape(nullptr);
   auto binary = polycpp::mysql2::escape(
       polycpp::mysql2::Value{polycpp::mysql2::Buffer::from({0x00, 0xff})});

Strings are quoted and control bytes are escaped. ``nullptr`` and
``std::monostate`` become ``NULL``. Non-finite ``double`` values also become
``NULL`` instead of producing invalid SQL.

Escape identifiers
------------------

Use ``escape_id`` for table, schema, or column names.

.. code-block:: cpp

   auto column = polycpp::mysql2::escape_id("users.email");
   // `users`.`email`

   auto literal_identifier = polycpp::mysql2::escape_id("users.email", true);
   // `users.email`

The second argument forbids splitting qualified identifiers on ``.``.

Positional format
-----------------

``format`` replaces ``?`` with escaped values and ``??`` with escaped
identifiers.

.. code-block:: cpp

   auto sql = polycpp::mysql2::format(
       "SELECT ?? FROM ?? WHERE id = ?",
       {std::string("email"), std::string("users"), int64_t{42}});

``??`` treats string values as identifiers. Other values fall back to normal
``escape`` behavior.

``format`` is intentionally simple and scans the SQL text for question marks.
Do not use it for SQL strings that contain literal ``?`` characters in string
literals or comments unless that behavior is intended.

Named placeholders
------------------

``format_named`` replaces ``:name`` outside quoted strings and backtick
identifiers.

.. code-block:: cpp

   auto sql = polycpp::mysql2::format_named(
       "SELECT * FROM users WHERE email = :email AND active = :active",
       {{"email", std::string("ada@example.com")}, {"active", true}});

Missing named values throw ``polycpp::mysql2::Error`` so callers do not
accidentally send partially formatted SQL.

Raw SQL fragments
-----------------

``raw`` explicitly bypasses escaping for a value position. Reserve it for
trusted SQL fragments such as server functions or fixed clauses.

.. code-block:: cpp

   auto sql = polycpp::mysql2::format(
       "INSERT INTO audit_log(created_at) VALUES (?)",
       {polycpp::mysql2::raw("CURRENT_TIMESTAMP")});

Do not wrap user input in ``raw``.

Multiple statements
-------------------

``ConnectionOptions::multiple_statements`` is disabled by default. Leave it off
unless a caller explicitly needs multi-statement SQL. When multiple result sets
are expected, use ``query_all`` or ``execute_all``; single-result APIs drain
extra results and throw.
