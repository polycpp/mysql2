Installation
============

``polycpp-mysql2`` targets C++20 and builds with CMake 3.20 or newer. It
depends on polycpp and the polycpp ``iconv-lite`` companion library.

CMake FetchContent
------------------

.. code-block:: cmake

   include(FetchContent)

   FetchContent_Declare(
       polycpp_mysql2
       GIT_REPOSITORY https://github.com/polycpp/mysql2.git
       GIT_TAG        master
   )
   FetchContent_MakeAvailable(polycpp_mysql2)

   add_executable(my_app main.cpp)
   target_link_libraries(my_app PRIVATE polycpp::mysql2)

Using local clones
------------------

.. code-block:: bash

   cmake -B build -G Ninja \
       -DPOLYCPP_SOURCE_DIR=/data/repo/polycpp \
       -DPOLYCPP_ICONV_LITE_SOURCE_DIR=/data/work/lib/iconv-lite

Build options
-------------

``POLYCPP_MYSQL2_BUILD_TESTS``
    Build the GoogleTest suite. Defaults to ``ON`` for standalone builds and
    ``OFF`` when consumed via FetchContent.

``POLYCPP_MYSQL2_BUILD_EXAMPLES``
    Build programs under ``examples/``.

``POLYCPP_IO``
    ``asio`` or ``libuv``; inherited from polycpp.

``POLYCPP_SSL_BACKEND``
    ``boringssl`` or ``openssl``; inherited from polycpp.

Verifying the build
-------------------

.. code-block:: bash

   cmake -B build -DCMAKE_BUILD_TYPE=Debug -DPOLYCPP_MYSQL2_BUILD_TESTS=ON
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure

Real database e2e tests are opt-in through ``MYSQL2_TEST_*`` variables. See
:doc:`../guides/testing` for the exact commands.
