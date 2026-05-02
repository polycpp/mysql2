#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <polycpp/mysql2/mysql2.hpp>

namespace polycpp::mysql2::test {

inline bool env_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value && std::string(value) != "0" && std::string(value) != "false";
}

inline ConnectionOptions options_from_env() {
    const char* host = std::getenv("MYSQL2_TEST_HOST");
    if (!host) {
        throw std::runtime_error("Set MYSQL2_TEST_HOST/MYSQL2_TEST_USER to run mysql2 e2e tests");
    }

    ConnectionOptions options;
    options.host = host;
    options.port = std::getenv("MYSQL2_TEST_PORT") ? static_cast<uint16_t>(std::stoi(std::getenv("MYSQL2_TEST_PORT"))) : 3306;
    options.user = std::getenv("MYSQL2_TEST_USER") ? std::getenv("MYSQL2_TEST_USER") : "root";
    options.password = std::getenv("MYSQL2_TEST_PASSWORD") ? std::getenv("MYSQL2_TEST_PASSWORD") : "";
    options.database = std::getenv("MYSQL2_TEST_DATABASE") ? std::getenv("MYSQL2_TEST_DATABASE") : "";
    options.connect_timeout_ms = 5000;
    if (std::getenv("MYSQL2_TEST_SSL")) {
        options.ssl.enabled = true;
        options.ssl.reject_unauthorized = std::getenv("MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED")
            ? std::string(std::getenv("MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED")) != "0"
            : false;
        options.ssl.verify_identity = std::getenv("MYSQL2_TEST_SSL_VERIFY_IDENTITY")
            ? std::string(std::getenv("MYSQL2_TEST_SSL_VERIFY_IDENTITY")) != "0"
            : options.ssl.reject_unauthorized;
        if (std::getenv("MYSQL2_TEST_SSL_CA_FILE")) {
            options.ssl.ca_file = std::getenv("MYSQL2_TEST_SSL_CA_FILE");
        }
    }
    return options;
}

inline uint64_t value_as_u64(const Value& value) {
    if (const auto* u = std::get_if<uint64_t>(&value)) return *u;
    if (const auto* i = std::get_if<int64_t>(&value)) return static_cast<uint64_t>(*i);
    if (const auto* d = std::get_if<double>(&value)) return static_cast<uint64_t>(*d);
    if (const auto* s = std::get_if<std::string>(&value)) return static_cast<uint64_t>(std::stoull(*s));
    throw Error("test expected numeric mysql2 value");
}

inline int64_t value_as_i64(const Value& value) {
    if (const auto* i = std::get_if<int64_t>(&value)) return *i;
    if (const auto* u = std::get_if<uint64_t>(&value)) return static_cast<int64_t>(*u);
    if (const auto* d = std::get_if<double>(&value)) return static_cast<int64_t>(*d);
    if (const auto* s = std::get_if<std::string>(&value)) return std::stoll(*s);
    throw Error("test expected numeric mysql2 value");
}

inline std::string value_as_string(const Value& value) {
    if (const auto* s = std::get_if<std::string>(&value)) return *s;
    if (const auto* b = std::get_if<Buffer>(&value)) return b->toString();
    throw Error("test expected string mysql2 value");
}

inline const Buffer& value_as_buffer(const Value& value) {
    if (const auto* b = std::get_if<Buffer>(&value)) return *b;
    throw Error("test expected Buffer mysql2 value");
}

inline std::string unique_socket_path() {
    static std::atomic<unsigned> counter{0};
    std::ostringstream name;
    name << "polycpp-mysql2-" << std::chrono::steady_clock::now().time_since_epoch().count()
         << "-" << counter++ << ".sock";
    return (std::filesystem::temp_directory_path() / name.str()).string();
}

inline void append_u8(std::vector<uint8_t>& payload, uint8_t value) {
    payload.push_back(value);
}

inline void append_i8(std::vector<uint8_t>& payload, int8_t value) {
    payload.push_back(static_cast<uint8_t>(value));
}

inline void append_u16_le(std::vector<uint8_t>& payload, uint16_t value) {
    payload.push_back(static_cast<uint8_t>(value & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

inline void append_i16_le(std::vector<uint8_t>& payload, int16_t value) {
    append_u16_le(payload, static_cast<uint16_t>(value));
}

inline void append_u24_le(std::vector<uint8_t>& payload, uint32_t value) {
    payload.push_back(static_cast<uint8_t>(value & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
}

inline void append_i24_le(std::vector<uint8_t>& payload, int32_t value) {
    append_u24_le(payload, static_cast<uint32_t>(value) & 0x00ffffffu);
}

inline void append_u24_be(std::vector<uint8_t>& payload, uint32_t value) {
    payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(value & 0xff));
}

inline void append_u32_le(std::vector<uint8_t>& payload, uint32_t value) {
    payload.push_back(static_cast<uint8_t>(value & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

inline void append_i32_le(std::vector<uint8_t>& payload, int32_t value) {
    append_u32_le(payload, static_cast<uint32_t>(value));
}

inline void append_u32_be(std::vector<uint8_t>& payload, uint32_t value) {
    payload.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(value & 0xff));
}

inline void append_u40_be(std::vector<uint8_t>& payload, uint64_t value) {
    for (int i = 4; i >= 0; --i) {
        payload.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

inline void append_u48_le(std::vector<uint8_t>& payload, uint64_t value) {
    for (int i = 0; i < 6; ++i) {
        payload.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

inline void append_u64_le(std::vector<uint8_t>& payload, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        payload.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

inline void append_i64_le(std::vector<uint8_t>& payload, int64_t value) {
    append_u64_le(payload, static_cast<uint64_t>(value));
}

inline void append_float_le(std::vector<uint8_t>& payload, float value) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    append_u32_le(payload, raw);
}

inline void append_double_le(std::vector<uint8_t>& payload, double value) {
    uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    append_u64_le(payload, raw);
}

inline void append_string(std::vector<uint8_t>& payload, const std::string& value) {
    payload.insert(payload.end(), value.begin(), value.end());
}

inline void append_lenenc_int(std::vector<uint8_t>& payload, uint64_t value) {
    if (value < 251) {
        append_u8(payload, static_cast<uint8_t>(value));
    } else if (value <= 0xffff) {
        append_u8(payload, 0xfc);
        append_u16_le(payload, static_cast<uint16_t>(value));
    } else if (value <= 0xffffff) {
        append_u8(payload, 0xfd);
        append_u24_le(payload, static_cast<uint32_t>(value));
    } else {
        append_u8(payload, 0xfe);
        append_u64_le(payload, value);
    }
}

inline Buffer make_binlog_event(uint8_t type, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> payload;
    append_u8(payload, 0x00);
    append_u32_le(payload, 1);
    append_u8(payload, type);
    append_u32_le(payload, 99);
    append_u32_le(payload, static_cast<uint32_t>(19 + body.size()));
    append_u32_le(payload, 1234);
    append_u16_le(payload, 0);
    payload.insert(payload.end(), body.begin(), body.end());
    return Buffer::from(payload.data(), payload.size());
}

inline void append_table_map_prefix(std::vector<uint8_t>& body,
                                    uint64_t table_id,
                                    const std::string& schema,
                                    const std::string& table,
                                    const std::vector<uint8_t>& column_types) {
    append_u48_le(body, table_id);
    append_u16_le(body, 0);
    append_u8(body, static_cast<uint8_t>(schema.size()));
    append_string(body, schema);
    append_u8(body, 0);
    append_u8(body, static_cast<uint8_t>(table.size()));
    append_string(body, table);
    append_u8(body, 0);
    append_lenenc_int(body, column_types.size());
    body.insert(body.end(), column_types.begin(), column_types.end());
}

inline void append_write_rows_prefix(std::vector<uint8_t>& body, uint64_t table_id, std::size_t column_count) {
    append_u48_le(body, table_id);
    append_u16_le(body, 0);
    append_lenenc_int(body, column_count);
    const auto bitmap_bytes = (column_count + 7) / 8;
    for (std::size_t i = 0; i < bitmap_bytes; ++i) {
        const auto remaining = column_count - (i * 8);
        append_u8(body, remaining >= 8 ? 0xff : static_cast<uint8_t>((1u << remaining) - 1u));
    }
}

}  // namespace polycpp::mysql2::test
