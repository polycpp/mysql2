Configuring TLS
===============

Enable TLS with ``ConnectionOptions::ssl.enabled``. The client sends MySQL
SSLRequest after the initial handshake, upgrades the existing TCP or Unix
socket transport to TLS, then sends credentials through the encrypted channel.

Verified TLS
------------

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.host = "db.example.com";
   options.user = "app";
   options.password = "secret";
   options.ssl.enabled = true;
   options.ssl.ca_file = "/etc/ssl/certs/db-ca.pem";
   options.ssl.reject_unauthorized = true;
   options.ssl.verify_identity = true;

   auto conn = polycpp::mysql2::create_connection(options);

Development-only insecure TLS
-----------------------------

.. code-block:: cpp

   options.ssl.enabled = true;
   options.ssl.reject_unauthorized = false;
   options.ssl.verify_identity = false;

Do not use this mode for production credentials.

Client certificate
------------------

.. code-block:: cpp

   options.ssl.cert_file = "/etc/mysql/client-cert.pem";
   options.ssl.key_file = "/etc/mysql/client-key.pem";
   options.ssl.key_passphrase = "optional-passphrase";

Cleartext auth plugin
---------------------

``mysql_clear_password`` is accepted only when TLS is active or
``ConnectionOptions::socket_path`` is used, and ``enable_cleartext_plugin`` is
set:

.. code-block:: cpp

   options.ssl.enabled = true;
   options.enable_cleartext_plugin = true;
