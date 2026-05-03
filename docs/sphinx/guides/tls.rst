Configuring TLS
===============

Enable TLS with ``ConnectionOptions::ssl.enabled``. The client sends MySQL
SSLRequest after the initial handshake, upgrades the existing TCP or Unix
socket transport to TLS, then sends credentials through the encrypted channel.
Unix socket connections do not use TLS, but they still count as a protected
local transport for the cleartext auth plugin gate described below.

The secure production shape is:

- ``ssl.enabled = true``;
- ``reject_unauthorized = true`` so certificate chain validation stays on;
- ``verify_identity = true`` so the server certificate identity is checked
  against the configured host;
- a CA source from the system trust store, ``ca_file``, ``ca_pem``, or an
  explicit bundled profile.

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

When ``verify_identity`` is enabled, configure ``options.host`` with the DNS
name or IP address expected in the certificate. Do not connect through an
unrelated alias and then disable identity verification to make the connection
work.

Bundled CA profiles
-------------------

``SslOptions::profile`` loads a bundled CA profile. The public helper
``ssl_profile_names()`` lists available profile names and
``ssl_profile_ca_pems(profile)`` returns the PEM bundle used by that profile.

.. code-block:: cpp

   options.ssl.enabled = true;
   options.ssl.profile = "Amazon RDS";
   options.ssl.reject_unauthorized = true;
   options.ssl.verify_identity = true;

Use a profile only when it matches the database provider you are connecting to.
Otherwise pass the deployment CA explicitly with ``ca_file`` or ``ca_pem``.

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

The client certificate and key are sent only after the MySQL TLS upgrade. Keep
``reject_unauthorized`` and ``verify_identity`` enabled unless you are testing a
fixture certificate.

Cleartext auth plugin
---------------------

``mysql_clear_password`` is accepted only when TLS is active or
``ConnectionOptions::socket_path`` is used, and ``enable_cleartext_plugin`` is
set:

.. code-block:: cpp

   options.ssl.enabled = true;
   options.enable_cleartext_plugin = true;

If the server requests ``mysql_clear_password`` without TLS or a Unix socket,
the client rejects the authentication path instead of sending the password over
an unprotected TCP connection.

Operational checks
------------------

Before production rollout, verify these points in configuration review or CI:

- ``mysqls://`` URIs are used when TLS is required by deployment policy.
- ``rejectUnauthorized=false`` and ``verifyIdentity=false`` are absent from
  production configuration.
- CA material is rotated through file paths or profiles rather than hard-coded
  into source files unless embedding is an explicit release decision.
- Tests cover the provider CA/profile path and at least one failing certificate
  or identity check.
