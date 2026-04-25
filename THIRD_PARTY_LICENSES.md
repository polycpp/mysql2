# Third-Party Licenses

- Upstream package: mysql2
- Source: https://github.com/sidorares/node-mysql2.git
- License: MIT
- License evidence: upstream package.json and `License` file in npm package 3.22.2
- Upstream copyright notice: Copyright (c) 2016 Andrey Sidorov (sidorares@yandex.ru) and contributors

This port is a C++ implementation based on analysis of the upstream pure JavaScript protocol implementation. It does not vendor upstream dependency source code in this repository.

## Runtime Dependency License Summary

| Package | Version analyzed | License | Port treatment |
|---|---:|---|---|
| aws-ssl-profiles | 1.1.2 | MIT | TLS profiles deferred; no source vendored. |
| denque | 2.1.0 | Apache-2.0 | Replaced by C++ standard containers when needed; no source vendored. |
| generate-function | 2.3.1 | MIT | Omitted; C++ uses static parsers. |
| iconv-lite | 0.7.2 | MIT | Reused through the existing polycpp iconv-lite companion, which carries its own license notice. |
| long | 5.3.2 | Apache-2.0 | Replaced by C++ integer types; no source vendored. |
| lru.min | 1.1.4 | MIT | Deferred with prepared statement/parser cache behavior; no source vendored. |
| named-placeholders | 1.1.6 | MIT | Small C++ formatter implemented locally; no source vendored. |
| sql-escaper | 1.3.3 | MIT | SQL escaping behavior implemented locally; no source vendored. |
| @types/node | peer metadata | MIT | Type-only upstream dependency; not shipped. |
