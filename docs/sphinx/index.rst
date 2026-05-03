mysql2
======

``polycpp::mysql2`` is a C++ companion port of the npm ``mysql2`` package for
polycpp. It implements the MySQL/MariaDB wire protocol directly and does **not**
link the native MySQL or MariaDB client SDKs.

Use this library when you need a MySQL-speaking C++ client that fits the
polycpp ecosystem: ``Buffer``, typed events, ``Promise`` wrappers,
``stream::Readable<T>``, TLS, zlib, crypto, and RAII ownership.

Start Here
----------

.. grid:: 2

   .. grid-item-card:: New to the library
      :margin: 1
      :link: getting-started/quickstart
      :link-type: doc

      Connect, query, execute a prepared statement, enable TLS, and use a pool.

   .. grid-item-card:: Choosing the right API
      :margin: 1
      :link: tutorials/api-concepts
      :link-type: doc

      Understand ``Connection``, ``Pool``, ``RowStream``, ``RawRowView``,
      server mode, and binlog ownership before building larger code.

   .. grid-item-card:: Production configuration
      :margin: 1
      :link: guides/connection-configuration
      :link-type: doc

      Map host/port/socket, URI, TLS, compression, charset, timeout, LOCAL
      INFILE, and connect attributes into typed C++ options.

   .. grid-item-card:: API reference
      :margin: 1
      :link: api/index
      :link-type: doc

      Inspect generated declarations for every public type, method, function,
      event, and constant.

Choose an API
-------------

.. list-table:: Common tasks
   :header-rows: 1
   :widths: 26 32 42

   * - Task
     - Primary API
     - Notes
   * - One ordinary SQL command
     - ``Connection::query`` or ``Pool::query``
     - Use text protocol when values are already trusted SQL or no parameters
       are needed.
   * - User-controlled values
     - ``execute(sql, values)``
     - Uses prepared statements and typed ``Value`` parameters.
   * - Stored procedures or multi-statements
     - ``query_all`` / ``execute_all``
     - Single-result APIs drain and throw when extra result sets appear.
   * - Many independent service requests
     - ``Pool``
     - One-shot pool calls acquire/release internally; checked-out
       ``PoolConnection`` is for multi-command sessions.
   * - Large row reads
     - ``query_stream`` or ``query_each_raw``
     - Choose typed rows for convenience, raw views for one-pass scan speed.
   * - SQL formatting helpers
     - ``escape``, ``escape_id``, ``format``, ``format_named``
     - Prefer prepared statements for values; use formatting deliberately for
       identifiers or trusted dynamic SQL fragments.
   * - MySQL-speaking fixtures
     - ``create_server`` / ``ServerConnection``
     - Server mode is a protocol/test/proxy surface, not a SQL engine.
   * - Binary log reads
     - ``binlog_dump_each`` or ``create_binlog_stream``
     - Replication reads reserve the connection and close transport on terminal
       paths before reconnecting later commands.

Implemented Scope
-----------------

Client protocol
   TCP, Unix socket paths, optional TLS, compression, MySQL protocol v10
   handshake, common auth plugins, connection attributes, URI parsing, ping,
   reset, change-user, transactions, graceful shutdown, and command timeouts.

Query APIs
   Text queries, query attributes, prepared statements, binary result rows,
   cursor fetch, explicit multi-result APIs, typed rows, JSON helpers, SQL
   escaping/formatting helpers, callback overloads, and ``polycpp::Promise``
   wrappers.

High-volume reads
   Pull-based ``RowStream`` typed rows, newline-delimited JSON ``Readable<Buffer>``
   chunks, and ``query_each_raw`` packet-backed scan views.

Pooling and topology
   Synchronous RAII ``Pool``, move-only ``PoolConnection`` checkout handles,
   typed pool events, and ``PoolCluster`` wildcard node selection.

Server and replication surfaces
   Adapted MySQL protocol server mode, TCP or Unix socket listening, optional
   MySQL TLS upgrade, typed command events, OK/ERR/text/binary response writers,
   bounded and streaming binlog reads, GTID parsing, table-map-aware binlog
   parsing, and typed TIME2/DATETIME2/TIMESTAMP2 binlog row values.

Compatibility Model
-------------------

This repo does not claim full JavaScript runtime parity with upstream mysql2.
The port preserves the wire-protocol and API concepts where they make sense in
C++, while adapting dynamic JavaScript behavior into explicit C++ ownership and
polycpp primitives.

Important adaptations:

- JavaScript row objects become typed ``Row`` / ``Value`` containers.
- Node object-mode row streams become ``RowStream`` over
  ``polycpp::stream::Readable<Row>``.
- Binlog object streams become ``BinlogStream`` over
  ``polycpp::stream::Readable<BinlogEvent>``.
- Node diagnostic channels become typed ``event::Trace`` events.
- Security-sensitive boundaries fail closed: unsupported auth plugins,
  clear-password auth without TLS/socket protection, LOCAL INFILE without an
  explicit handler, malformed packets, and unsafe multi-result use.

See ``docs/divergences.md`` in the repository for the audit record.

First Query
-----------

.. code-block:: cpp

   #include <iostream>
   #include <polycpp/mysql2/mysql2.hpp>

   int main() {
       polycpp::mysql2::ConnectionOptions options;
       options.host = "127.0.0.1";
       options.user = "root";
       options.password = "secret";
       options.database = "app";

       auto conn = polycpp::mysql2::create_connection(options);
       auto result = conn.query("SELECT 1 AS one");

       std::cout << std::get<int64_t>(result.rows[0].at("one")) << "\n";
   }

.. toctree::
   :hidden:
   :caption: Getting started

   getting-started/installation
   getting-started/quickstart

.. toctree::
   :hidden:
   :caption: Tutorials

   tutorials/index

.. toctree::
   :hidden:
   :caption: How-to guides

   guides/index

.. toctree::
   :hidden:
   :caption: API reference

   api/index

.. toctree::
   :hidden:
   :caption: Examples

   examples/index
