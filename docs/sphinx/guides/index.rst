How-to Guides
=============

Guides are focused operational recipes. Use tutorials to understand a workflow;
use these pages when you already know the API surface and need exact policy or
commands.

Guide Map
---------

.. list-table:: How-to topics
   :header-rows: 1
   :widths: 30 45 25

   * - Guide
     - Covers
     - Use it for
   * - :doc:`testing`
     - Unit tests, real database e2e tests, TLS e2e, focused binaries, and
       Docker-oriented reproduction notes.
     - Verifying behavior locally or in CI.
   * - :doc:`connection-configuration`
     - Direct options, URI parameters, timeouts, compression, LOCAL INFILE,
       connect attributes, charsets, large-number, and decimal policies.
     - Translating deployment settings into ``ConnectionOptions``.
   * - :doc:`lifecycle-and-safety`
     - Connect/end/destroy, timeout close behavior, multi-result draining,
       active stream ownership, reset/change-user, statement cache, and
       fail-closed boundaries.
     - Auditing production lifecycle behavior.
   * - :doc:`sql-formatting`
     - Prepared-first guidance, value escaping, identifier escaping,
       positional format, named placeholders, raw fragments, and multi-statement
       risk.
     - Building dynamic SQL deliberately.
   * - :doc:`callbacks-promises-events`
     - Callback overloads, Promise wrappers, typed events, trace events, pool
       events, row streams, and JSON streams.
     - Using Node-like control surfaces through C++/polycpp primitives.
   * - :doc:`tls`
     - Verified TLS, development-only insecure TLS, client certificates, and
       cleartext auth plugin constraints.
     - Connecting securely or testing TLS-sensitive auth paths.
   * - :doc:`type-mapping`
     - ``Value`` alternatives, binary fields, large numbers, decimals, and date
       time policy.
     - Understanding C++ row value conversions.
   * - :doc:`raw-row-scans`
     - When to use raw scans, lifetime rules, numeric parsing, error behavior,
       and limitations.
     - Writing high-throughput scan loops.
   * - :doc:`benchmarking`
     - Build/run commands, workloads, optional native C API comparison, and
       optional upstream JavaScript comparison.
     - Measuring performance changes reproducibly.

.. toctree::
   :maxdepth: 1

   testing
   connection-configuration
   lifecycle-and-safety
   sql-formatting
   callbacks-promises-events
   tls
   type-mapping
   raw-row-scans
   benchmarking
