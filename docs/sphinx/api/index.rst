API Reference
=============

The public API lives in ``include/polycpp/mysql2/mysql2.hpp``.

Core types
----------

``ConnectionOptions``
    Host, port, credentials, database, charset, auth, multiple-statement, and
    TLS options.

``SslOptions``
    TLS enablement, CA/certificate/key material, trust store loading, and
    certificate identity checks.

``Connection``
    Synchronous connection object for connect, query, prepared execution,
    transaction, ping, reset, and shutdown operations.

``PreparedStatement``
    Server-side prepared statement id plus parameter and column metadata.

``PoolOptions``, ``Pool``, ``PoolConnection``
    Synchronous RAII connection pooling.

``QueryResult``, ``OkPacket``, ``Field``, ``Row``, ``Value``
    Result metadata and typed row values.

Free functions
--------------

``create_connection``
    Construct and connect a ``Connection``.

``create_pool``
    Construct a ``Pool``.

``query``
    One-shot connect/query/end helper.

``escape``, ``escape_id``, ``format``, ``format_named``, ``raw``
    SQL formatting helpers.

Generated namespace reference
-----------------------------

.. doxygennamespace:: polycpp::mysql2
   :members:
