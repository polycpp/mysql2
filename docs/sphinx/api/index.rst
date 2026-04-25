API Reference
=============

The public API lives in ``include/polycpp/mysql2/mysql2.hpp``.

Core types
----------

``ConnectionOptions``
    Host, port, credentials, database, charset, auth, connect attributes,
    multiple-statement, and TLS options.

``SslOptions``
    TLS enablement, CA/certificate/key material, trust store loading, and
    certificate identity checks.

``Connection``
    Synchronous connection object for connect, query, prepared execution,
    transaction, ping, reset, and shutdown operations.

``PreparedStatement``
    Server-side prepared statement id plus parameter and column metadata.

``StatementCursor``
    Server-side prepared-statement cursor metadata used with ``fetch``.

``QueryAttributes``
    Named query metadata values sent when the server advertises support.

``PoolOptions``, ``Pool``, ``PoolConnection``
    Synchronous RAII connection pooling.

``ServerOptions``, ``ServerHandshakeOptions``, ``ServerAuthInfo``
    Adapted server protocol configuration and parsed client handshake data.

``Server``, ``ServerConnection``
    TCP server listener and per-client protocol connection with typed command
    events and OK/ERR/text-result writers.

``QueryResult``, ``OkPacket``, ``Field``, ``Row``, ``Value``
    Result metadata and typed row values.

Free functions
--------------

``create_connection``
    Construct and connect a ``Connection``.

``create_pool``
    Construct a ``Pool``.

``create_server``
    Construct an adapted MySQL protocol server.

``query``
    One-shot connect/query/end helper.

``escape``, ``escape_id``, ``format``, ``format_named``, ``raw``
    SQL formatting helpers.

``get_charset_number``, ``get_charset_encoding``
    Helpers for MySQL charset/collation ids and encoding names.

Generated namespace reference
-----------------------------

.. doxygennamespace:: polycpp::mysql2
   :members:
