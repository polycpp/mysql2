Examples
========

Examples are intentionally small programs that exercise one API path at a time.
They are not replacements for the tutorials; use them when you want a runnable
starting point after reading the related topic page.

Build Examples
--------------

Examples are built only when ``POLYCPP_MYSQL2_BUILD_EXAMPLES=ON`` is passed to
CMake.

.. code-block:: bash

   cmake -B build -DPOLYCPP_MYSQL2_BUILD_EXAMPLES=ON
   cmake --build build --target simple_query raw_scan

Environment
-----------

The examples use the same ``MYSQL2_TEST_*`` environment variables as the e2e
tests:

.. code-block:: bash

   export MYSQL2_TEST_HOST=127.0.0.1
   export MYSQL2_TEST_PORT=3306
   export MYSQL2_TEST_USER=root
   export MYSQL2_TEST_PASSWORD=secret
   export MYSQL2_TEST_DATABASE=test

Example Map
-----------

.. list-table:: Runnable examples
   :header-rows: 1
   :widths: 24 42 34

   * - Example
     - What it does
     - Related docs
   * - ``examples/simple_query.cpp``
     - Connects with ``MYSQL2_TEST_*`` options, runs a scalar query, and prints
       the result.
     - :doc:`/getting-started/quickstart`, :doc:`/tutorials/query-and-prepared`
   * - ``examples/raw_scan.cpp``
     - Uses ``Connection::query_each_raw`` to scan a recursive integer result
       set without constructing typed ``Row`` / ``Value`` objects for every row.
     - :doc:`/tutorials/raw-row-scans`, :doc:`/guides/raw-row-scans`

Run ``simple_query``
--------------------

.. code-block:: bash

   ./build/examples/simple_query

Run ``raw_scan``
----------------

.. code-block:: bash

   MYSQL2_EXAMPLE_LIMIT=1000 ./build/examples/raw_scan
