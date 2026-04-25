mysql2
======

C++ companion port of the npm ``mysql2`` package for polycpp.

This library implements the MySQL/MariaDB wire protocol directly. It does
not link the native MySQL or MariaDB client SDKs.

Supported scope
---------------

- TCP and optional TLS connections.
- MySQL protocol handshake and common auth plugins.
- Text queries, query attributes, prepared statements, binary result rows, cursor fetch, and explicit multi-result APIs.
- Typed rows using ``std::variant`` values.
- Transactions, ping, reset, change-user, graceful shutdown, synchronous RAII pools, and pool clusters.
- Callback overloads, ``polycpp::Promise`` wrappers, typed events, connection URI parsing, compression, and LOCAL INFILE handler hooks.
- Query stream adaptation through newline-delimited JSON ``Buffer`` chunks.
- SQL escaping, identifier escaping, positional formatting, and named formatting helpers.

Not full upstream parity
------------------------

The JavaScript server, binlog/replication, diagnostics, parser cache controls,
and native object-mode row stream APIs are intentionally not part of the current
C++ surface. Callback, Promise, EventEmitter, query attributes, cursor fetch,
compression, LOCAL INFILE, URI, and pool-cluster surfaces are implemented with
C++ adaptations. See
``docs/divergences.md`` in the repository for the detailed list.

.. code-block:: cpp

   #include <polycpp/mysql2/mysql2.hpp>

   polycpp::mysql2::ConnectionOptions options;
   options.host = "127.0.0.1";
   options.user = "root";

   auto conn = polycpp::mysql2::create_connection(options);
   auto rows = conn.query("SELECT 1 AS one");

.. grid:: 2

   .. grid-item-card:: Pure protocol
      :margin: 1

      Uses polycpp Buffer, crypto, IO, and TLS primitives instead of a native
      database SDK.

   .. grid-item-card:: Typed C++ API
      :margin: 1

      Adapts dynamic JavaScript rows, callbacks, promises, events, and streams
      into explicit C++ values, result objects, polycpp primitives, and RAII
      lifecycle management.

   .. grid-item-card:: Real e2e tested
      :margin: 1

      The test suite can run against MariaDB/MySQL, including verified TLS.

   .. grid-item-card:: Explicit gaps
      :margin: 1

      Unsupported upstream behavior is documented and fails closed where it
      crosses a security or protocol boundary.

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
