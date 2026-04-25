#include <polycpp/mysql2/mysql2.hpp>

#include <polycpp/crypto.hpp>
#include <polycpp/iconv_lite/iconv_lite.hpp>
#include <polycpp/io/event_context.hpp>
#include <polycpp/io/tcp_socket.hpp>
#include <polycpp/io/tls_context.hpp>
#include <polycpp/io/tls_stream.hpp>
#include <polycpp/ssl/x509_cert.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace polycpp::mysql2 {
namespace {

namespace client_flag {
constexpr uint32_t LONG_PASSWORD = 0x00000001;
constexpr uint32_t FOUND_ROWS = 0x00000002;
constexpr uint32_t LONG_FLAG = 0x00000004;
constexpr uint32_t CONNECT_WITH_DB = 0x00000008;
constexpr uint32_t LOCAL_FILES = 0x00000080;
constexpr uint32_t PROTOCOL_41 = 0x00000200;
constexpr uint32_t SSL = 0x00000800;
constexpr uint32_t TRANSACTIONS = 0x00002000;
constexpr uint32_t RESERVED = 0x00004000;
constexpr uint32_t SECURE_CONNECTION = 0x00008000;
constexpr uint32_t MULTI_STATEMENTS = 0x00010000;
constexpr uint32_t MULTI_RESULTS = 0x00020000;
constexpr uint32_t PS_MULTI_RESULTS = 0x00040000;
constexpr uint32_t PLUGIN_AUTH = 0x00080000;
constexpr uint32_t CONNECT_ATTRS = 0x00100000;
constexpr uint32_t PLUGIN_AUTH_LENENC_CLIENT_DATA = 0x00200000;
constexpr uint32_t SESSION_TRACK = 0x00800000;
constexpr uint32_t DEPRECATE_EOF = 0x01000000;
constexpr uint32_t CLIENT_QUERY_ATTRIBUTES = 0x08000000;
constexpr uint32_t MULTI_FACTOR_AUTHENTICATION = 0x10000000;
}  // namespace client_flag

namespace command_code {
constexpr uint8_t QUIT = 0x01;
constexpr uint8_t QUERY = 0x03;
constexpr uint8_t PING = 0x0e;
constexpr uint8_t STMT_PREPARE = 0x16;
constexpr uint8_t STMT_EXECUTE = 0x17;
constexpr uint8_t STMT_CLOSE = 0x19;
constexpr uint8_t RESET_CONNECTION = 0x1f;
}  // namespace command_code

namespace marker {
constexpr uint8_t OK = 0x00;
constexpr uint8_t ERR = 0xff;
constexpr uint8_t EOF_PACKET = 0xfe;
constexpr uint8_t AUTH_MORE_DATA = 0x01;
constexpr uint8_t AUTH_NEXT_FACTOR = 0x02;
}  // namespace marker

namespace server_status {
constexpr uint16_t MORE_RESULTS_EXISTS = 0x0008;
}  // namespace server_status

constexpr std::size_t kPacketHeaderLength = 4;
constexpr std::size_t kMaxPacketPayloadLength = 0x00ffffff;

bool is_eof_packet(const Buffer& payload) {
    return payload.length() > 0 && payload[0] == marker::EOF_PACKET && payload.length() < 9;
}

bool is_ok_packet(const Buffer& payload) {
    return payload.length() > 0 && payload[0] == marker::OK;
}

bool is_err_packet(const Buffer& payload) {
    return payload.length() > 0 && payload[0] == marker::ERR;
}

std::string make_error_message(uint16_t code, const std::string& state, const std::string& message) {
    std::ostringstream out;
    out << "MySQL error " << code;
    if (!state.empty()) {
        out << " (" << state << ")";
    }
    if (!message.empty()) {
        out << ": " << message;
    }
    return out.str();
}

std::string to_hex(const Buffer& buffer) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(buffer.length() * 2);
    for (std::size_t i = 0; i < buffer.length(); ++i) {
        const auto b = buffer[i];
        out.push_back(digits[(b >> 4) & 0x0f]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

Buffer buffer_from_string(std::string_view value) {
    return Buffer::from(reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

Buffer buffer_from_bytes(const std::vector<uint8_t>& bytes) {
    return bytes.empty() ? Buffer{} : Buffer::from(bytes.data(), bytes.size());
}

std::vector<uint8_t> bytes_from_buffer(const Buffer& buffer) {
    return std::vector<uint8_t>(buffer.data(), buffer.data() + buffer.length());
}

std::string bytes_to_ascii(const Buffer& buffer, std::size_t start, std::size_t end) {
    if (end < start || start > buffer.length()) {
        return {};
    }
    end = std::min(end, buffer.length());
    return std::string(reinterpret_cast<const char*>(buffer.data() + start), end - start);
}

Buffer sha_digest(const std::string& algorithm, const Buffer& data) {
    auto hash = crypto::createHash(algorithm);
    hash.update(data);
    return hash.digestBuffer();
}

Buffer sha_digest(const std::string& algorithm, std::string_view data) {
    auto hash = crypto::createHash(algorithm);
    hash.update(std::string(data));
    return hash.digestBuffer();
}

Buffer sha_digest(const std::string& algorithm, const std::vector<Buffer>& pieces) {
    auto hash = crypto::createHash(algorithm);
    for (const auto& piece : pieces) {
        hash.update(piece);
    }
    return hash.digestBuffer();
}

Buffer xor_buffers(const Buffer& a, const Buffer& b) {
    if (a.length() != b.length()) {
        throw Error("cannot xor buffers with different lengths");
    }
    auto out = Buffer::allocUnsafe(a.length());
    for (std::size_t i = 0; i < a.length(); ++i) {
        out[i] = static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return out;
}

Buffer xor_rotating(const Buffer& input, const Buffer& seed) {
    if (seed.length() == 0) {
        throw Error("cannot xor with an empty seed");
    }
    auto out = Buffer::allocUnsafe(input.length());
    for (std::size_t i = 0; i < input.length(); ++i) {
        out[i] = static_cast<uint8_t>(input[i] ^ seed[i % seed.length()]);
    }
    return out;
}

Buffer mysql_native_password_token(const std::string& password, const Buffer& scramble) {
    if (password.empty()) {
        return Buffer{};
    }
    const auto stage1 = sha_digest("sha1", password);
    const auto stage2 = sha_digest("sha1", stage1);
    const auto seed = scramble.length() > 20 ? scramble.slice(0, 20) : scramble;
    const auto stage3 = sha_digest("sha1", {seed.slice(0, std::min<std::size_t>(20, seed.length())), stage2});
    return xor_buffers(stage3, stage1);
}

Buffer caching_sha2_password_token(const std::string& password, const Buffer& scramble) {
    if (password.empty()) {
        return Buffer{};
    }
    const auto stage1 = sha_digest("sha256", password);
    const auto stage2 = sha_digest("sha256", stage1);
    const auto stage3 = sha_digest("sha256", {stage2, scramble.length() > 20 ? scramble.slice(0, 20) : scramble});
    return xor_buffers(stage1, stage3);
}

Buffer encrypt_password_with_rsa(const std::string& password, const Buffer& scramble, const std::string& public_key_pem) {
    std::string with_null = password;
    with_null.push_back('\0');
    auto masked = xor_rotating(buffer_from_string(with_null), scramble.length() > 20 ? scramble.slice(0, 20) : scramble);
    crypto::RsaEncryptOptions options;
    options.padding = crypto::constants::kRsaPkcs1OaepPadding;
    options.oaepHash = "sha1";
    return crypto::publicEncrypt(public_key_pem, masked, options);
}

struct PacketFrame {
    uint8_t sequence_id = 0;
    Buffer payload;
};

class PacketCursor {
public:
    explicit PacketCursor(Buffer payload) : payload_(std::move(payload)) {}

    bool has_more() const noexcept { return offset_ < payload_.length(); }
    std::size_t offset() const noexcept { return offset_; }
    std::size_t length() const noexcept { return payload_.length(); }
    const Buffer& payload() const noexcept { return payload_; }

    uint8_t peek_u8() const {
        ensure(1);
        return payload_[offset_];
    }

    uint8_t read_u8() {
        ensure(1);
        return payload_[offset_++];
    }

    uint16_t read_u16_le() {
        ensure(2);
        const auto value = static_cast<uint16_t>(payload_[offset_] | (payload_[offset_ + 1] << 8));
        offset_ += 2;
        return value;
    }

    uint32_t read_u24_le() {
        ensure(3);
        const auto value = static_cast<uint32_t>(payload_[offset_] |
                                                (payload_[offset_ + 1] << 8) |
                                                (payload_[offset_ + 2] << 16));
        offset_ += 3;
        return value;
    }

    uint32_t read_u32_le() {
        ensure(4);
        const auto value = static_cast<uint32_t>(payload_[offset_] |
                                                (payload_[offset_ + 1] << 8) |
                                                (payload_[offset_ + 2] << 16) |
                                                (payload_[offset_ + 3] << 24));
        offset_ += 4;
        return value;
    }

    uint64_t read_u64_le() {
        ensure(8);
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<uint64_t>(payload_[offset_ + i]) << (8 * i);
        }
        offset_ += 8;
        return value;
    }

    int8_t read_i8() {
        return static_cast<int8_t>(read_u8());
    }

    int16_t read_i16_le() {
        return static_cast<int16_t>(read_u16_le());
    }

    int32_t read_i32_le() {
        return static_cast<int32_t>(read_u32_le());
    }

    int64_t read_i64_le() {
        return static_cast<int64_t>(read_u64_le());
    }

    float read_float_le() {
        ensure(4);
        float value = 0;
        std::memcpy(&value, payload_.data() + offset_, 4);
        offset_ += 4;
        return value;
    }

    double read_double_le() {
        ensure(8);
        double value = 0;
        std::memcpy(&value, payload_.data() + offset_, 8);
        offset_ += 8;
        return value;
    }

    Buffer read_buffer(std::size_t length) {
        ensure(length);
        auto out = payload_.slice(offset_, offset_ + length);
        offset_ += length;
        return out;
    }

    Buffer read_rest_buffer() {
        return read_buffer(payload_.length() - offset_);
    }

    std::string read_ascii(std::size_t length) {
        ensure(length);
        std::string out(reinterpret_cast<const char*>(payload_.data() + offset_), length);
        offset_ += length;
        return out;
    }

    std::string read_null_terminated_ascii() {
        const auto start = offset_;
        while (offset_ < payload_.length() && payload_[offset_] != 0) {
            ++offset_;
        }
        const auto end = offset_;
        if (offset_ < payload_.length()) {
            ++offset_;
        }
        return bytes_to_ascii(payload_, start, end);
    }

    std::optional<uint64_t> read_lenenc_int() {
        const auto first = read_u8();
        if (first < 0xfb) {
            return first;
        }
        if (first == 0xfb) {
            return std::nullopt;
        }
        if (first == 0xfc) {
            return read_u16_le();
        }
        if (first == 0xfd) {
            return read_u24_le();
        }
        if (first == 0xfe) {
            return read_u64_le();
        }
        throw Error("invalid length-encoded integer marker");
    }

    std::optional<Buffer> read_lenenc_buffer() {
        const auto length = read_lenenc_int();
        if (!length) {
            return std::nullopt;
        }
        if (*length > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw Error("length-encoded field is too large");
        }
        return read_buffer(static_cast<std::size_t>(*length));
    }

    std::optional<std::string> read_lenenc_string(const std::string& encoding = "utf8") {
        const auto bytes = read_lenenc_buffer();
        if (!bytes) {
            return std::nullopt;
        }
        return decode_buffer(*bytes, encoding);
    }

    void skip(std::size_t length) {
        ensure(length);
        offset_ += length;
    }

    static std::string decode_buffer(const Buffer& buffer, const std::string& encoding) {
        if (buffer.length() == 0) {
            return {};
        }
        if (encoding == "binary") {
            return buffer.toString("latin1");
        }
        if (encoding.empty() || encoding == "utf8" || encoding == "utf8mb4" || encoding == "cesu8") {
            return buffer.toString("utf8");
        }
        if (Buffer::isEncoding(encoding)) {
            return buffer.toString(encoding);
        }
        if (iconv_lite::encoding_exists(encoding)) {
            return iconv_lite::decode(buffer, encoding);
        }
        return buffer.toString("utf8");
    }

private:
    void ensure(std::size_t length) const {
        if (offset_ + length > payload_.length()) {
            throw Error("malformed MySQL packet: unexpected end of packet");
        }
    }

    Buffer payload_;
    std::size_t offset_ = 0;
};

void append_u8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void append_u16_le(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void append_u24_le(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
}

void append_u32_le(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void append_u64_le(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

void append_double_le(std::vector<uint8_t>& out, double value) {
    static_assert(sizeof(double) == 8);
    std::array<uint8_t, 8> bytes{};
    std::memcpy(bytes.data(), &value, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void append_lenenc_int(std::vector<uint8_t>& out, uint64_t value) {
    if (value < 0xfb) {
        append_u8(out, static_cast<uint8_t>(value));
    } else if (value <= 0xffff) {
        append_u8(out, 0xfc);
        append_u16_le(out, static_cast<uint16_t>(value));
    } else if (value <= 0xffffff) {
        append_u8(out, 0xfd);
        append_u24_le(out, static_cast<uint32_t>(value));
    } else {
        append_u8(out, 0xfe);
        for (int i = 0; i < 8; ++i) {
            append_u8(out, static_cast<uint8_t>((value >> (8 * i)) & 0xff));
        }
    }
}

void append_bytes(std::vector<uint8_t>& out, const Buffer& bytes) {
    out.insert(out.end(), bytes.data(), bytes.data() + bytes.length());
}

void append_string(std::vector<uint8_t>& out, std::string_view value) {
    out.insert(out.end(), value.begin(), value.end());
}

void append_null_string(std::vector<uint8_t>& out, std::string_view value) {
    append_string(out, value);
    append_u8(out, 0);
}

void append_lenenc_string(std::vector<uint8_t>& out, std::string_view value) {
    append_lenenc_int(out, value.size());
    append_string(out, value);
}

void append_lenenc_buffer(std::vector<uint8_t>& out, const Buffer& value) {
    append_lenenc_int(out, value.length());
    append_bytes(out, value);
}


uint8_t charset_number_for_name(const std::string& charset, uint8_t fallback) {
    std::string normalized;
    normalized.reserve(charset.size());
    for (const auto ch : charset) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    if (normalized.empty() || normalized == "utf8mb4" || normalized == "utf8mb4unicodeci") return 224;
    if (normalized == "utf8" || normalized == "utf8generalci") return 33;
    if (normalized == "latin1" || normalized == "latin1swedishci") return 8;
    if (normalized == "ascii" || normalized == "asciigeneralci") return 11;
    if (normalized == "binary") return 63;
    if (charset == "utf8mb4") return fallback;
    throw Error("unsupported charset option: " + charset);
}

std::string charset_encoding(uint16_t charset) {
    switch (charset) {
        case 8:
        case 47:
        case 48:
        case 49:
        case 94:
            return "latin1";
        case 11:
        case 65:
            return "ascii";
        case 33:
        case 45:
        case 46:
        case 76:
        case 83:
        case 192:
        case 193:
        case 194:
        case 195:
        case 199:
        case 200:
        case 201:
        case 202:
        case 203:
        case 204:
        case 205:
        case 206:
        case 207:
        case 208:
        case 209:
        case 210:
        case 211:
        case 212:
        case 213:
        case 214:
        case 215:
        case 223:
        case 224:
        case 225:
        case 226:
        case 227:
        case 228:
        case 229:
        case 230:
        case 255:
            return "utf8";
        case 63:
            return "binary";
        default:
            return "utf8";
    }
}

struct Handshake {
    uint8_t protocol_version = 0;
    std::string server_version;
    uint32_t capability_flags = 0;
    uint32_t connection_id = 0;
    Buffer auth_plugin_data1;
    Buffer auth_plugin_data2;
    uint8_t character_set = 0;
    uint16_t status_flags = 0;
    std::string auth_plugin_name = "mysql_native_password";

    Buffer scramble() const {
        auto combined = Buffer::concat({auth_plugin_data1, auth_plugin_data2});
        return combined.length() > 20 ? combined.slice(0, 20) : combined;
    }
};

Handshake parse_handshake(const Buffer& payload) {
    PacketCursor cursor(payload);
    Handshake handshake;
    handshake.protocol_version = cursor.read_u8();
    if (handshake.protocol_version != 10) {
        throw Error("unsupported MySQL protocol version " + std::to_string(handshake.protocol_version));
    }
    handshake.server_version = cursor.read_null_terminated_ascii();
    handshake.connection_id = cursor.read_u32_le();
    handshake.auth_plugin_data1 = cursor.read_buffer(8);
    cursor.skip(1);

    uint32_t capability = cursor.read_u16_le();
    if (cursor.has_more()) {
        handshake.character_set = cursor.read_u8();
        handshake.status_flags = cursor.read_u16_le();
        capability |= static_cast<uint32_t>(cursor.read_u16_le()) << 16;
        handshake.capability_flags = capability;
        const auto auth_plugin_data_length = (capability & client_flag::PLUGIN_AUTH) ? cursor.read_u8() : (cursor.skip(1), 0);
        cursor.skip(10);
        if (capability & client_flag::SECURE_CONNECTION) {
            const std::size_t len = auth_plugin_data_length == 0
                ? 12
                : std::max<std::size_t>(13, static_cast<std::size_t>(auth_plugin_data_length > 8 ? auth_plugin_data_length - 8 : 0));
            if (cursor.has_more()) {
                const auto readable = std::min<std::size_t>(len, payload.length() - cursor.offset());
                handshake.auth_plugin_data2 = cursor.read_buffer(readable);
            }
        }
        if (capability & client_flag::PLUGIN_AUTH) {
            if (cursor.has_more()) {
                handshake.auth_plugin_name = cursor.read_null_terminated_ascii();
                if (handshake.auth_plugin_name.empty()) {
                    handshake.auth_plugin_name = "mysql_native_password";
                }
            }
        }
    } else {
        handshake.capability_flags = capability;
    }

    return handshake;
}

uint32_t build_client_flags(const ConnectionOptions& options, const Handshake& handshake) {
    uint32_t desired = client_flag::LONG_PASSWORD |
                       client_flag::FOUND_ROWS |
                       client_flag::LONG_FLAG |
                       client_flag::PROTOCOL_41 |
                       client_flag::TRANSACTIONS |
                       client_flag::RESERVED |
                       client_flag::SECURE_CONNECTION |
                       client_flag::MULTI_RESULTS |
                       client_flag::PS_MULTI_RESULTS |
                       client_flag::PLUGIN_AUTH |
                       client_flag::PLUGIN_AUTH_LENENC_CLIENT_DATA |
                       client_flag::SESSION_TRACK;
    if (!options.database.empty()) {
        desired |= client_flag::CONNECT_WITH_DB;
    }
    if (options.multiple_statements) {
        desired |= client_flag::MULTI_STATEMENTS;
    }
    if (options.ssl.enabled) {
        desired |= client_flag::SSL;
    }
    desired |= handshake.capability_flags & client_flag::MULTI_FACTOR_AUTHENTICATION;
    auto flags = desired & handshake.capability_flags;
    if (options.multiple_statements) {
        flags |= client_flag::MULTI_STATEMENTS;
    }
    return flags;
}

std::string choose_auth_plugin(const std::string& server_plugin, bool secure_connection, bool enable_cleartext_plugin) {
    if (server_plugin == "mysql_native_password" || server_plugin == "caching_sha2_password") {
        return server_plugin;
    }
    if (server_plugin == "sha256_password" && secure_connection) {
        return server_plugin;
    }
    if (server_plugin == "mysql_clear_password" && secure_connection && enable_cleartext_plugin) {
        return server_plugin;
    }
    if (server_plugin.empty()) {
        return "mysql_native_password";
    }
    return "mysql_native_password";
}

Buffer calculate_auth_token(const std::string& plugin, const std::string& password, const Buffer& scramble) {
    if (plugin == "mysql_native_password") {
        return mysql_native_password_token(password, scramble);
    }
    if (plugin == "caching_sha2_password") {
        return caching_sha2_password_token(password, scramble);
    }
    if (plugin == "sha256_password" || plugin == "mysql_clear_password") {
        std::string clear = password;
        clear.push_back('\0');
        return buffer_from_string(clear);
    }
    return mysql_native_password_token(password, scramble);
}

Buffer build_handshake_response(const ConnectionOptions& options,
                                const Handshake& handshake,
                                uint32_t client_flags,
                                const std::string& auth_plugin_name,
                                const Buffer& auth_token) {
    std::vector<uint8_t> out;
    out.reserve(128 + options.user.size() + options.database.size() + auth_token.length());
    append_u32_le(out, client_flags);
    append_u32_le(out, 0);  // max packet size: 0 means default/no client-side cap.
    append_u8(out, options.charset_number);
    out.insert(out.end(), 23, 0);
    append_null_string(out, options.user);
    if (client_flags & client_flag::PLUGIN_AUTH_LENENC_CLIENT_DATA) {
        append_lenenc_buffer(out, auth_token);
    } else if (client_flags & client_flag::SECURE_CONNECTION) {
        if (auth_token.length() > 255) {
            throw Error("authentication token is too large for secure connection packet");
        }
        append_u8(out, static_cast<uint8_t>(auth_token.length()));
        append_bytes(out, auth_token);
    } else {
        append_bytes(out, auth_token);
        append_u8(out, 0);
    }
    if (client_flags & client_flag::CONNECT_WITH_DB) {
        append_null_string(out, options.database);
    }
    if (client_flags & client_flag::PLUGIN_AUTH) {
        append_null_string(out, auth_plugin_name);
    }
    return buffer_from_bytes(out);
}

Buffer build_ssl_request(const ConnectionOptions& options, uint32_t client_flags) {
    std::vector<uint8_t> out;
    out.reserve(32);
    append_u32_le(out, client_flags | client_flag::SSL);
    append_u32_le(out, 0);  // max packet size: 0 means default/no client-side cap.
    append_u8(out, options.charset_number);
    out.insert(out.end(), 23, 0);
    return buffer_from_bytes(out);
}

bool looks_like_ip_address(const std::string& host) {
    if (host.empty()) {
        return false;
    }
    return std::all_of(host.begin(), host.end(), [](unsigned char c) {
        return std::isdigit(c) || c == '.' || c == ':';
    });
}

Error parse_error_packet(const Buffer& payload) {
    PacketCursor cursor(payload);
    cursor.read_u8();
    const auto code = cursor.read_u16_le();
    std::string state;
    if (cursor.has_more() && cursor.peek_u8() == '#') {
        cursor.skip(1);
        state = cursor.read_ascii(5);
    }
    const auto message = PacketCursor::decode_buffer(cursor.read_rest_buffer(), "utf8");
    return Error(code, state, message);
}

OkPacket parse_ok_packet(const Buffer& payload, uint32_t server_flags) {
    PacketCursor cursor(payload);
    cursor.read_u8();
    OkPacket ok;
    ok.affected_rows = cursor.read_lenenc_int().value_or(0);
    ok.insert_id = cursor.read_lenenc_int().value_or(0);
    if (server_flags & client_flag::PROTOCOL_41) {
        if (cursor.has_more()) ok.server_status = cursor.read_u16_le();
        if (cursor.has_more()) ok.warning_count = cursor.read_u16_le();
    } else if (server_flags & client_flag::TRANSACTIONS) {
        if (cursor.has_more()) ok.server_status = cursor.read_u16_le();
    }
    if (cursor.has_more()) {
        ok.info = PacketCursor::decode_buffer(cursor.read_rest_buffer(), "utf8");
    }
    const std::string needle = "changed: ";
    const auto pos = ok.info.find(needle);
    if (pos != std::string::npos) {
        const auto start = pos + needle.size();
        auto end = start;
        while (end < ok.info.size() && std::isdigit(static_cast<unsigned char>(ok.info[end]))) {
            ++end;
        }
        if (end > start) {
            ok.changed_rows = static_cast<uint64_t>(std::stoull(ok.info.substr(start, end - start)));
        }
    }
    return ok;
}

Field parse_column_definition(const Buffer& payload, const std::string& client_encoding) {
    PacketCursor cursor(payload);
    Field field;
    field.catalog = cursor.read_lenenc_string(client_encoding).value_or("");
    field.schema = cursor.read_lenenc_string(client_encoding).value_or("");
    field.table = cursor.read_lenenc_string(client_encoding).value_or("");
    field.org_table = cursor.read_lenenc_string(client_encoding).value_or("");
    field.name = cursor.read_lenenc_string(client_encoding).value_or("");
    field.org_name = cursor.read_lenenc_string(client_encoding).value_or("");
    if (cursor.has_more()) {
        cursor.skip(1);
        field.character_set = cursor.read_u16_le();
        field.encoding = charset_encoding(field.character_set);
        if (field.encoding == "binary") {
            field.encoding = client_encoding;
        }
        field.column_length = cursor.read_u32_le();
        field.column_type = cursor.read_u8();
        field.flags = cursor.read_u16_le();
        field.decimals = cursor.read_u8();
    }
    return field;
}

bool field_is_blob(uint8_t type) {
    using namespace constants::column_type;
    return type == TINY_BLOB || type == MEDIUM_BLOB || type == LONG_BLOB || type == BLOB ||
           type == GEOMETRY || type == VECTOR || type == BIT;
}

bool field_is_string_like(uint8_t type) {
    using namespace constants::column_type;
    return type == VARCHAR || type == VAR_STRING || type == STRING || type == ENUM || type == SET;
}

bool parse_int64(std::string_view text, int64_t& out) {
    auto first = text.data();
    auto last = text.data() + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parse_uint64(std::string_view text, uint64_t& out) {
    auto first = text.data();
    auto last = text.data() + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parse_double_value(const std::string& text, double& out) {
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size();
}

Value parse_text_value(const Field& field, const Buffer& bytes, const ConnectionOptions& options) {
    using namespace constants::column_type;
    if (field.column_type != JSON &&
        (field_is_blob(field.column_type) || field_is_string_like(field.column_type)) &&
        (field.character_set == 63 || field.is_binary())) {
        return bytes;
    }

    const auto text = PacketCursor::decode_buffer(
        bytes,
        field.column_type == JSON ? "utf8" : (field.encoding.empty() ? "utf8" : field.encoding));
    switch (field.column_type) {
        case TINY:
        case SHORT:
        case LONG:
        case INT24:
        case YEAR: {
            if (field.is_unsigned()) {
                uint64_t value = 0;
                if (parse_uint64(text, value)) return value;
            } else {
                int64_t value = 0;
                if (parse_int64(text, value)) return value;
            }
            return text;
        }
        case LONGLONG: {
            if (options.big_number_strings) {
                return text;
            }
            if (field.is_unsigned()) {
                uint64_t value = 0;
                if (parse_uint64(text, value)) return value;
            } else {
                int64_t value = 0;
                if (parse_int64(text, value)) return value;
            }
            return text;
        }
        case FLOAT:
        case DOUBLE: {
            double value = 0;
            if (parse_double_value(text, value)) return value;
            return text;
        }
        case NEWDECIMAL:
        case DECIMAL: {
            if (options.decimal_numbers) {
                double value = 0;
                if (parse_double_value(text, value)) return value;
            }
            return text;
        }
        default:
            return text;
    }
}

Row parse_text_row(const Buffer& payload, const std::vector<Field>& fields, const ConnectionOptions& options) {
    PacketCursor cursor(payload);
    Row row;
    row.values.reserve(fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        auto bytes = cursor.read_lenenc_buffer();
        if (!bytes) {
            row.values.emplace_back(std::monostate{});
        } else {
            row.values.emplace_back(parse_text_value(fields[i], *bytes, options));
        }
        row.index_by_name.emplace(fields[i].name, i);
    }
    return row;
}

std::string two_digits(uint32_t value) {
    std::string out;
    out.push_back(static_cast<char>('0' + ((value / 10) % 10)));
    out.push_back(static_cast<char>('0' + (value % 10)));
    return out;
}

std::string four_digits(uint32_t value) {
    std::string out;
    out.push_back(static_cast<char>('0' + ((value / 1000) % 10)));
    out.push_back(static_cast<char>('0' + ((value / 100) % 10)));
    out.push_back(static_cast<char>('0' + ((value / 10) % 10)));
    out.push_back(static_cast<char>('0' + (value % 10)));
    return out;
}

std::string format_mysql_datetime(uint16_t year,
                                  uint8_t month,
                                  uint8_t day,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second,
                                  uint32_t micros,
                                  bool date_only) {
    std::string out = four_digits(year) + "-" + two_digits(month) + "-" + two_digits(day);
    if (!date_only) {
        out += " " + two_digits(hour) + ":" + two_digits(minute) + ":" + two_digits(second);
        if (micros != 0) {
            std::string frac = std::to_string(1000000 + micros).substr(1);
            while (!frac.empty() && frac.back() == '0') frac.pop_back();
            out += "." + frac;
        }
    }
    return out;
}

std::string parse_binary_datetime(PacketCursor& cursor, uint8_t column_type) {
    const auto length = cursor.read_u8();
    if (length == 0) {
        return column_type == constants::column_type::DATE ? "0000-00-00" : "0000-00-00 00:00:00";
    }
    if (length != 4 && length != 7 && length != 11) {
        throw Error("malformed binary date/datetime value");
    }
    const auto year = cursor.read_u16_le();
    const auto month = cursor.read_u8();
    const auto day = cursor.read_u8();
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint32_t micros = 0;
    if (length >= 7) {
        hour = cursor.read_u8();
        minute = cursor.read_u8();
        second = cursor.read_u8();
    }
    if (length == 11) {
        micros = cursor.read_u32_le();
    }
    return format_mysql_datetime(year, month, day, hour, minute, second, micros, column_type == constants::column_type::DATE);
}

std::string parse_binary_time(PacketCursor& cursor) {
    const auto length = cursor.read_u8();
    if (length == 0) {
        return "00:00:00";
    }
    if (length != 8 && length != 12) {
        throw Error("malformed binary time value");
    }
    const bool negative = cursor.read_u8() != 0;
    const auto days = cursor.read_u32_le();
    const auto hours = cursor.read_u8();
    const auto minutes = cursor.read_u8();
    const auto seconds = cursor.read_u8();
    uint32_t micros = 0;
    if (length == 12) {
        micros = cursor.read_u32_le();
    }
    std::string out = negative ? "-" : "";
    out += std::to_string(days * 24 + hours) + ":" + two_digits(minutes) + ":" + two_digits(seconds);
    if (micros != 0) {
        std::string frac = std::to_string(1000000 + micros).substr(1);
        while (!frac.empty() && frac.back() == '0') frac.pop_back();
        out += "." + frac;
    }
    return out;
}

Value parse_binary_value(PacketCursor& cursor, const Field& field, const ConnectionOptions& options) {
    using namespace constants::column_type;
    switch (field.column_type) {
        case TINY:
            return field.is_unsigned() ? Value{static_cast<uint64_t>(cursor.read_u8())}
                                       : Value{static_cast<int64_t>(cursor.read_i8())};
        case SHORT:
        case YEAR:
            return field.is_unsigned() ? Value{static_cast<uint64_t>(cursor.read_u16_le())}
                                       : Value{static_cast<int64_t>(cursor.read_i16_le())};
        case LONG:
        case INT24:
            return field.is_unsigned() ? Value{static_cast<uint64_t>(cursor.read_u32_le())}
                                       : Value{static_cast<int64_t>(cursor.read_i32_le())};
        case LONGLONG:
            if (field.is_unsigned()) {
                return cursor.read_u64_le();
            }
            return cursor.read_i64_le();
        case FLOAT:
            return static_cast<double>(cursor.read_float_le());
        case DOUBLE:
            return cursor.read_double_le();
        case DATE:
        case DATETIME:
        case TIMESTAMP:
        case NEWDATE:
            return parse_binary_datetime(cursor, field.column_type);
        case TIME:
            return parse_binary_time(cursor);
        case DECIMAL:
        case NEWDECIMAL:
        case VARCHAR:
        case VAR_STRING:
        case STRING:
        case JSON:
        case ENUM:
        case SET:
        case TINY_BLOB:
        case MEDIUM_BLOB:
        case LONG_BLOB:
        case BLOB:
        case GEOMETRY:
        case VECTOR:
        case BIT: {
            const auto bytes = cursor.read_lenenc_buffer();
            if (!bytes) return std::monostate{};
            return parse_text_value(field, *bytes, options);
        }
        case NULL_TYPE:
            return std::monostate{};
        default: {
            const auto bytes = cursor.read_lenenc_buffer();
            if (!bytes) return std::monostate{};
            return PacketCursor::decode_buffer(*bytes, field.encoding.empty() ? "utf8" : field.encoding);
        }
    }
}

Row parse_binary_row(const Buffer& payload, const std::vector<Field>& fields, const ConnectionOptions& options) {
    PacketCursor cursor(payload);
    if (cursor.read_u8() != 0) {
        throw Error("malformed binary row packet");
    }
    const std::size_t null_bitmap_length = (fields.size() + 7 + 2) / 8;
    const auto null_bitmap = cursor.read_buffer(null_bitmap_length);
    Row row;
    row.values.reserve(fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto bit = i + 2;
        const bool is_null = (null_bitmap[bit / 8] & (1u << (bit % 8))) != 0;
        if (is_null) {
            row.values.emplace_back(std::monostate{});
        } else {
            row.values.emplace_back(parse_binary_value(cursor, fields[i], options));
        }
        row.index_by_name.emplace(fields[i].name, i);
    }
    return row;
}

std::vector<Field> read_definition_packets(auto& read_packet, std::size_t count, const std::string& client_encoding) {
    std::vector<Field> definitions;
    definitions.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto frame = read_packet();
        if (is_err_packet(frame.payload)) {
            throw parse_error_packet(frame.payload);
        }
        if (is_eof_packet(frame.payload) || is_ok_packet(frame.payload)) {
            break;
        }
        definitions.push_back(parse_column_definition(frame.payload, client_encoding));
    }
    const auto end = read_packet();
    if (!is_eof_packet(end.payload) && !is_ok_packet(end.payload)) {
        throw Error("expected EOF/OK packet after prepared statement definitions");
    }
    return definitions;
}

Buffer build_stmt_prepare_payload(const std::string& sql) {
    std::vector<uint8_t> payload;
    payload.reserve(sql.size() + 1);
    append_u8(payload, command_code::STMT_PREPARE);
    append_string(payload, sql);
    return buffer_from_bytes(payload);
}

void append_bound_value(std::vector<uint8_t>& payload, const Value& value) {
    struct Visitor {
        std::vector<uint8_t>& payload;
        void operator()(std::monostate) const {}
        void operator()(int64_t value) const { append_u64_le(payload, static_cast<uint64_t>(value)); }
        void operator()(uint64_t value) const { append_u64_le(payload, value); }
        void operator()(double value) const { append_double_le(payload, value); }
        void operator()(const std::string& value) const { append_lenenc_string(payload, value); }
        void operator()(const Buffer& value) const { append_lenenc_buffer(payload, value); }
        void operator()(const RawSql&) const { throw Error("raw SQL values cannot be used as prepared statement parameters"); }
    };
    std::visit(Visitor{payload}, value);
}

std::pair<uint8_t, uint8_t> bound_type(const Value& value) {
    using namespace constants::column_type;
    struct Visitor {
        std::pair<uint8_t, uint8_t> operator()(std::monostate) const { return {NULL_TYPE, 0}; }
        std::pair<uint8_t, uint8_t> operator()(int64_t) const { return {LONGLONG, 0}; }
        std::pair<uint8_t, uint8_t> operator()(uint64_t) const { return {LONGLONG, 0x80}; }
        std::pair<uint8_t, uint8_t> operator()(double) const { return {DOUBLE, 0}; }
        std::pair<uint8_t, uint8_t> operator()(const std::string&) const { return {VAR_STRING, 0}; }
        std::pair<uint8_t, uint8_t> operator()(const Buffer&) const { return {BLOB, 0}; }
        std::pair<uint8_t, uint8_t> operator()(const RawSql&) const {
            throw Error("raw SQL values cannot be used as prepared statement parameters");
        }
    };
    return std::visit(Visitor{}, value);
}

Buffer build_stmt_execute_payload(uint32_t statement_id, std::size_t parameter_count, const std::vector<Value>& values) {
    if (values.size() != parameter_count) {
        throw Error("prepared statement expected " + std::to_string(parameter_count) +
                    " parameters, got " + std::to_string(values.size()));
    }
    std::vector<uint8_t> payload;
    payload.reserve(16 + values.size() * 16);
    append_u8(payload, command_code::STMT_EXECUTE);
    append_u32_le(payload, statement_id);
    append_u8(payload, 0);     // CURSOR_TYPE_NO_CURSOR
    append_u32_le(payload, 1); // iteration count
    if (!values.empty()) {
        const std::size_t null_bitmap_length = (values.size() + 7) / 8;
        std::vector<uint8_t> null_bitmap(null_bitmap_length, 0);
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (std::holds_alternative<std::monostate>(values[i])) {
                null_bitmap[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
            }
        }
        payload.insert(payload.end(), null_bitmap.begin(), null_bitmap.end());
        append_u8(payload, 1); // new-params-bound-flag
        for (const auto& value : values) {
            const auto [type, flags] = bound_type(value);
            append_u8(payload, type);
            append_u8(payload, flags);
        }
        for (const auto& value : values) {
            if (!std::holds_alternative<std::monostate>(value)) {
                append_bound_value(payload, value);
            }
        }
    }
    return buffer_from_bytes(payload);
}

std::string value_to_string(const Value& value) {
    struct Visitor {
        std::string operator()(std::monostate) const { return "NULL"; }
        std::string operator()(int64_t value) const { return std::to_string(value); }
        std::string operator()(uint64_t value) const { return std::to_string(value); }
        std::string operator()(double value) const {
            if (!std::isfinite(value)) return "NULL";
            std::ostringstream out;
            out.precision(17);
            out << value;
            return out.str();
        }
        std::string operator()(const std::string& value) const { return escape(value); }
        std::string operator()(const Buffer& value) const { return "X'" + to_hex(value) + "'"; }
        std::string operator()(const RawSql& value) const { return value.sql; }
    };
    return std::visit(Visitor{}, value);
}

}  // namespace

class Connection::Impl {
public:
    explicit Impl(ConnectionOptions options) : options_(std::move(options)), socket_(ctx_) {
        options_.charset_number = charset_number_for_name(options_.charset, options_.charset_number);
    }

    void connect() {
        if (connected_) {
            return;
        }
        ctx_.restart();
        std::error_code ec;
        socket_.asyncConnect(options_.host, options_.port, [&](std::error_code error) { ec = error; });
        ctx_.run();
        if (ec) {
            throw Error("connect failed: " + ec.message());
        }
        socket_.setNoDelay(true, ec);
        if (ec) {
            throw Error("failed to set TCP_NODELAY: " + ec.message());
        }
        if (options_.enable_keep_alive) {
            socket_.setKeepAlive(true, ec);
            if (ec) {
                throw Error("failed to set TCP keepalive: " + ec.message());
            }
        }

        const auto hello = read_packet();
        if (is_err_packet(hello.payload)) {
            throw parse_error_packet(hello.payload);
        }
        handshake_ = parse_handshake(hello.payload);
        server_version_ = handshake_.server_version;
        connection_id_ = handshake_.connection_id;
        server_capability_flags_ = handshake_.capability_flags;
        client_encoding_ = charset_encoding(options_.charset_number);

        auto client_flags = build_client_flags(options_, handshake_);
        if (options_.ssl.enabled) {
            if ((client_flags & client_flag::SSL) == 0) {
                throw Error("server does not advertise SSL support");
            }
            write_packet(build_ssl_request(options_, client_flags), 1);
            upgrade_to_tls();
        }

        const auto scramble = handshake_.scramble();
        const auto plugin = choose_auth_plugin(handshake_.auth_plugin_name, tls_active_, options_.enable_cleartext_plugin);
        auto token = calculate_auth_token(plugin, options_.password, scramble);
        write_packet(build_handshake_response(options_, handshake_, client_flags, plugin, token), tls_active_ ? 2 : 1);
        handle_auth_result(plugin, scramble, tls_active_ ? 3 : 2);
        connected_ = true;
    }

    QueryResult query(const std::string& sql) {
        auto results = query_all(sql);
        if (results.size() != 1) {
            throw Error("query returned multiple result sets; use query_all");
        }
        return std::move(results.front());
    }

    std::vector<QueryResult> query_all(const std::string& sql) {
        ensure_connected();
        std::vector<uint8_t> payload;
        payload.reserve(sql.size() + 1);
        append_u8(payload, command_code::QUERY);
        append_string(payload, sql);
        write_packet(buffer_from_bytes(payload), 0);
        return read_query_results(false);
    }

    PreparedStatement prepare(const std::string& sql) {
        ensure_connected();
        write_packet(build_stmt_prepare_payload(sql), 0);
        const auto frame = read_packet();
        if (is_err_packet(frame.payload)) {
            throw parse_error_packet(frame.payload);
        }
        PacketCursor cursor(frame.payload);
        if (cursor.read_u8() != 0) {
            throw Error("unexpected packet for COM_STMT_PREPARE");
        }
        PreparedStatement statement;
        statement.query = sql;
        statement.id = cursor.read_u32_le();
        const auto column_count = cursor.read_u16_le();
        const auto parameter_count = cursor.read_u16_le();
        cursor.skip(1); // reserved filler
        if (cursor.has_more()) {
            cursor.read_u16_le(); // warning count
        }
        if (parameter_count > 0) {
            auto next = [this]() { return read_packet(); };
            statement.parameters = read_definition_packets(next, parameter_count, client_encoding_);
        }
        if (column_count > 0) {
            auto next = [this]() { return read_packet(); };
            statement.columns = read_definition_packets(next, column_count, client_encoding_);
        }
        return statement;
    }

    QueryResult execute(const PreparedStatement& statement, const std::vector<Value>& values) {
        auto results = execute_all(statement, values);
        if (results.size() != 1) {
            throw Error("execute returned multiple result sets; use execute_all");
        }
        return std::move(results.front());
    }

    std::vector<QueryResult> execute_all(const PreparedStatement& statement, const std::vector<Value>& values) {
        ensure_connected();
        if (statement.id == 0) {
            throw Error("cannot execute an empty prepared statement");
        }
        write_packet(build_stmt_execute_payload(statement.id, statement.parameters.size(), values), 0);
        return read_query_results(true);
    }

    QueryResult execute(const std::string& sql, const std::vector<Value>& values) {
        auto statement = prepare(sql);
        try {
            auto result = execute(statement, values);
            close_statement(statement);
            return result;
        } catch (...) {
            close_statement(statement);
            throw;
        }
    }

    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values) {
        auto statement = prepare(sql);
        try {
            auto results = execute_all(statement, values);
            close_statement(statement);
            return results;
        } catch (...) {
            close_statement(statement);
            throw;
        }
    }

    void close_statement(const PreparedStatement& statement) {
        ensure_connected();
        if (statement.id == 0) {
            return;
        }
        std::vector<uint8_t> payload;
        payload.reserve(5);
        append_u8(payload, command_code::STMT_CLOSE);
        append_u32_le(payload, statement.id);
        write_packet(buffer_from_bytes(payload), 0);
    }

    OkPacket begin_transaction() {
        return query("START TRANSACTION").ok;
    }

    OkPacket commit() {
        return query("COMMIT").ok;
    }

    OkPacket rollback() {
        return query("ROLLBACK").ok;
    }

    OkPacket ping() {
        ensure_connected();
        write_packet(Buffer::from({command_code::PING}), 0);
        const auto frame = read_packet();
        if (is_err_packet(frame.payload)) {
            throw parse_error_packet(frame.payload);
        }
        if (!is_ok_packet(frame.payload)) {
            throw Error("unexpected packet for COM_PING");
        }
        return parse_ok_packet(frame.payload, server_capability_flags_);
    }

    void reset() {
        ensure_connected();
        write_packet(Buffer::from({command_code::RESET_CONNECTION}), 0);
        const auto frame = read_packet();
        if (is_err_packet(frame.payload)) {
            throw parse_error_packet(frame.payload);
        }
        if (!is_ok_packet(frame.payload)) {
            throw Error("unexpected packet for COM_RESET_CONNECTION");
        }
    }

    void end() noexcept {
        if (!connected_ && !transport_is_open()) {
            return;
        }
        try {
            if (transport_is_open()) {
                if (connected_) {
                    write_packet(Buffer::from({command_code::QUIT}), 0);
                }
                close_transport();
            }
        } catch (...) {
        }
        connected_ = false;
        tls_active_ = false;
    }

    bool connected() const noexcept { return connected_; }
    bool encrypted() const noexcept { return tls_active_; }
    const ConnectionOptions& options() const noexcept { return options_; }
    const std::string& server_version() const noexcept { return server_version_; }
    uint32_t connection_id() const noexcept { return connection_id_; }
    uint32_t server_capability_flags() const noexcept { return server_capability_flags_; }

private:
    void ensure_connected() {
        if (!connected_) {
            connect();
        }
    }

    void configure_tls_context(io::TlsContext& tls_context) {
        tls_context.setDefaultOptions();
        if (!options_.ssl.key_passphrase.empty()) {
            const auto passphrase = options_.ssl.key_passphrase;
            tls_context.setPasswordCallback([passphrase]() { return passphrase; });
        }
        if (options_.ssl.reject_unauthorized) {
            tls_context.setVerifyMode(1);
            if (options_.ssl.load_default_verify_paths) {
                tls_context.loadDefaultVerifyPaths();
            }
            if (!options_.ssl.ca_pem.empty()) {
                tls_context.addCertificateAuthorityPem(options_.ssl.ca_pem);
            }
            if (!options_.ssl.ca_file.empty()) {
                tls_context.loadVerifyFile(options_.ssl.ca_file);
            }
        } else {
            tls_context.setVerifyMode(0);
            tls_context.setVerifyCallbackAcceptAll();
        }
        if (!options_.ssl.cert_pem.empty()) {
            tls_context.useCertificateChainPem(options_.ssl.cert_pem);
        }
        if (!options_.ssl.cert_file.empty()) {
            tls_context.useCertificateChainFile(options_.ssl.cert_file);
        }
        if (!options_.ssl.key_pem.empty()) {
            tls_context.usePrivateKeyPem(options_.ssl.key_pem);
        }
        if (!options_.ssl.key_file.empty()) {
            tls_context.usePrivateKeyFile(options_.ssl.key_file);
        }
    }

    void upgrade_to_tls() {
        tls_context_ = std::make_unique<io::TlsContext>(io::TlsContext::Method::kTLSClient);
        configure_tls_context(*tls_context_);
        tls_stream_ = std::make_unique<io::TlsStream>(std::move(socket_), *tls_context_);
        if (!looks_like_ip_address(options_.host)) {
            tls_stream_->sslConnection().setHostname(options_.host);
        }

        ctx_.restart();
        std::error_code ec;
        tls_stream_->asyncHandshake(false, [&](std::error_code error) { ec = error; });
        ctx_.run();
        if (ec) {
            throw Error("TLS handshake failed: " + ec.message());
        }

        if (options_.ssl.reject_unauthorized) {
            const auto verify_result = tls_stream_->sslConnection().getVerifyResult();
            if (verify_result != 0) {
                throw Error("TLS certificate verification failed: " +
                            ssl::SslConnection::verifyErrorString(verify_result));
            }
            if (options_.ssl.verify_identity && !looks_like_ip_address(options_.host)) {
                auto* peer = tls_stream_->sslConnection().peerCertificateHandle();
                if (peer == nullptr) {
                    throw Error("TLS peer did not provide a certificate");
                }
                auto cert = ssl::X509Cert::fromHandle(peer);
                if (!cert.checkHost(options_.host)) {
                    throw Error("TLS certificate host mismatch for " + options_.host);
                }
            } else if (options_.ssl.verify_identity && looks_like_ip_address(options_.host)) {
                auto* peer = tls_stream_->sslConnection().peerCertificateHandle();
                if (peer == nullptr) {
                    throw Error("TLS peer did not provide a certificate");
                }
                auto cert = ssl::X509Cert::fromHandle(peer);
                if (!cert.checkIP(options_.host)) {
                    throw Error("TLS certificate IP mismatch for " + options_.host);
                }
            }
        }
        tls_active_ = true;
    }

    bool transport_is_open() const noexcept {
        return tls_stream_ ? tls_stream_->isOpen() : socket_.isOpen();
    }

    void close_transport() noexcept {
        std::error_code ec;
        if (tls_stream_) {
            try {
                ctx_.restart();
                tls_stream_->asyncShutdown([&](std::error_code error) { ec = error; });
                ctx_.run();
            } catch (...) {
            }
            tls_stream_->close(ec);
            tls_stream_.reset();
            tls_context_.reset();
            return;
        }
        socket_.shutdown(2, ec);
        socket_.close(ec);
    }

    std::vector<uint8_t> read_exact(std::size_t length) {
        std::vector<uint8_t> data(length);
        std::size_t offset = 0;
        while (offset < length) {
            ctx_.restart();
            std::error_code ec;
            std::size_t bytes = 0;
            const auto remaining = length - offset;
            if (tls_stream_) {
                tls_stream_->asyncReadSome(data.data() + offset, remaining, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                });
            } else {
                socket_.asyncRead(data.data() + offset, remaining, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                });
            }
            ctx_.run();
            if (ec) {
                throw Error("socket read failed: " + ec.message());
            }
            if (bytes == 0) {
                throw Error("socket read ended before the requested packet bytes were available");
            }
            offset += bytes;
        }
        return data;
    }

    void write_all(const uint8_t* data, std::size_t length) {
        std::size_t offset = 0;
        while (offset < length) {
            ctx_.restart();
            std::error_code ec;
            std::size_t bytes = 0;
            if (tls_stream_) {
                tls_stream_->asyncWrite(data + offset, length - offset, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                });
            } else {
                socket_.asyncWrite(data + offset, length - offset, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                });
            }
            ctx_.run();
            if (ec) {
                throw Error("socket write failed: " + ec.message());
            }
            if (bytes == 0) {
                throw Error("socket write ended before the full packet was written");
            }
            offset += bytes;
        }
    }

    PacketFrame read_packet() {
        std::vector<Buffer> parts;
        uint8_t first_sequence = 0;
        bool first = true;
        while (true) {
            const auto header = read_exact(kPacketHeaderLength);
            const uint32_t length = static_cast<uint32_t>(header[0] | (header[1] << 8) | (header[2] << 16));
            const uint8_t sequence = header[3];
            if (first) {
                first_sequence = sequence;
                first = false;
            }
            auto payload = Buffer{};
            if (length > 0) {
                const auto bytes = read_exact(length);
                payload = buffer_from_bytes(bytes);
            }
            parts.push_back(std::move(payload));
            if (length < kMaxPacketPayloadLength) {
                break;
            }
        }
        return {first_sequence, Buffer::concat(parts)};
    }

    void write_packet(const Buffer& payload, uint8_t sequence_id) {
        std::size_t offset = 0;
        bool wrote_full_packet = false;
        do {
            const auto remaining = payload.length() - offset;
            const auto chunk_length = std::min<std::size_t>(remaining, kMaxPacketPayloadLength);
            std::vector<uint8_t> packet;
            packet.reserve(kPacketHeaderLength + chunk_length);
            append_u24_le(packet, static_cast<uint32_t>(chunk_length));
            append_u8(packet, sequence_id++);
            if (chunk_length > 0) {
                packet.insert(packet.end(), payload.data() + offset, payload.data() + offset + chunk_length);
            }
            write_all(packet.data(), packet.size());
            offset += chunk_length;
            wrote_full_packet = chunk_length == kMaxPacketPayloadLength;
        } while (offset < payload.length());

        if (wrote_full_packet) {
            std::vector<uint8_t> terminator;
            append_u24_le(terminator, 0);
            append_u8(terminator, sequence_id);
            write_all(terminator.data(), terminator.size());
        }
    }

    void handle_auth_result(const std::string& initial_plugin, const Buffer& scramble, uint8_t expected_sequence) {
        std::string current_plugin = initial_plugin;
        Buffer current_scramble = scramble;
        uint8_t next_client_sequence = expected_sequence;
        while (true) {
            const auto frame = read_packet();
            next_client_sequence = static_cast<uint8_t>(frame.sequence_id + 1);
            if (is_ok_packet(frame.payload)) {
                return;
            }
            if (is_err_packet(frame.payload)) {
                throw parse_error_packet(frame.payload);
            }
            PacketCursor cursor(frame.payload);
            const auto tag = cursor.read_u8();
            if (tag == marker::EOF_PACKET || tag == marker::AUTH_NEXT_FACTOR) {
                current_plugin = cursor.read_null_terminated_ascii();
                current_scramble = cursor.read_rest_buffer();
                const auto response = auth_switch_response(current_plugin, current_scramble);
                write_packet(response, next_client_sequence);
                continue;
            }
            if (tag == marker::AUTH_MORE_DATA) {
                const auto data = cursor.read_rest_buffer();
                if (current_plugin == "caching_sha2_password") {
                    if (data.length() == 1 && data[0] == 3) {
                        continue;  // Server sends final OK as the next packet.
                    }
                    if (data.length() == 1 && data[0] == 4) {
                        if (options_.password.empty()) {
                            write_packet(Buffer{}, next_client_sequence);
                        } else if (tls_active_) {
                            std::string clear = options_.password;
                            clear.push_back('\0');
                            write_packet(buffer_from_string(clear), next_client_sequence);
                        } else if (!options_.server_public_key_pem.empty()) {
                            write_packet(encrypt_password_with_rsa(options_.password, current_scramble, options_.server_public_key_pem), next_client_sequence);
                        } else {
                            write_packet(Buffer::from({0x02}), next_client_sequence);
                            const auto key_frame = read_packet();
                            PacketCursor key_cursor(key_frame.payload);
                            if (key_cursor.read_u8() != marker::AUTH_MORE_DATA) {
                                if (is_err_packet(key_frame.payload)) throw parse_error_packet(key_frame.payload);
                                throw Error("expected server public key during caching_sha2_password authentication");
                            }
                            const auto public_key = PacketCursor::decode_buffer(key_cursor.read_rest_buffer(), "utf8");
                            write_packet(encrypt_password_with_rsa(options_.password, current_scramble, public_key), static_cast<uint8_t>(key_frame.sequence_id + 1));
                        }
                        continue;
                    }
                    throw Error("invalid AuthMoreData packet for caching_sha2_password");
                }
                if (current_plugin == "sha256_password") {
                    const auto public_key = !options_.server_public_key_pem.empty()
                        ? options_.server_public_key_pem
                        : PacketCursor::decode_buffer(data, "utf8");
                    write_packet(encrypt_password_with_rsa(options_.password, current_scramble, public_key), next_client_sequence);
                    continue;
                }
                throw Error("unexpected AuthMoreData packet for auth plugin " + current_plugin);
            }
            throw Error("unexpected packet during authentication");
        }
    }

    Buffer auth_switch_response(const std::string& plugin, const Buffer& plugin_data) const {
        if (plugin == "mysql_native_password") {
            return mysql_native_password_token(options_.password, plugin_data);
        }
        if (plugin == "caching_sha2_password") {
            return caching_sha2_password_token(options_.password, plugin_data.length() > 20 ? plugin_data.slice(0, 20) : plugin_data);
        }
        if (plugin == "sha256_password") {
            if (options_.password.empty()) {
                return Buffer{};
            }
            if (tls_active_) {
                std::string clear = options_.password;
                clear.push_back('\0');
                return buffer_from_string(clear);
            }
            if (!options_.server_public_key_pem.empty()) {
                return encrypt_password_with_rsa(options_.password, plugin_data, options_.server_public_key_pem);
            }
            return Buffer::from({0x01});
        }
        if (plugin == "mysql_clear_password") {
            if (!tls_active_ || !options_.enable_cleartext_plugin) {
                throw Error("mysql_clear_password requires TLS and enable_cleartext_plugin");
            }
            std::string clear = options_.password;
            clear.push_back('\0');
            return buffer_from_string(clear);
        }
        throw Error("unsupported authentication plugin: " + plugin);
    }

    std::vector<QueryResult> read_query_results(bool binary_rows) {
        std::vector<QueryResult> results;
        while (true) {
            auto result = read_query_result(binary_rows);
            const bool has_more = (result.ok.server_status & server_status::MORE_RESULTS_EXISTS) != 0;
            results.push_back(std::move(result));
            if (!has_more) {
                break;
            }
        }
        return results;
    }

    QueryResult read_query_result(bool binary_rows) {
        const auto first = read_packet();
        if (is_err_packet(first.payload)) {
            throw parse_error_packet(first.payload);
        }
        if (is_ok_packet(first.payload)) {
            QueryResult result;
            result.ok = parse_ok_packet(first.payload, server_capability_flags_);
            return result;
        }
        if (first.payload.length() > 0 && first.payload[0] == 0xfb) {
            throw Error("LOCAL INFILE request is not supported by this port");
        }

        PacketCursor header(first.payload);
        const auto field_count = header.read_lenenc_int();
        if (!field_count) {
            throw Error("unexpected NULL field count in resultset header");
        }
        QueryResult result;
        result.fields.reserve(static_cast<std::size_t>(*field_count));
        for (std::size_t i = 0; i < *field_count; ++i) {
            const auto field_frame = read_packet();
            if (is_err_packet(field_frame.payload)) {
                throw parse_error_packet(field_frame.payload);
            }
            result.fields.push_back(parse_column_definition(field_frame.payload, client_encoding_));
        }
        const auto fields_end = read_packet();
        if (!is_eof_packet(fields_end.payload) && !is_ok_packet(fields_end.payload)) {
            throw Error("expected EOF/OK packet after column definitions");
        }

        while (true) {
            const auto row_frame = read_packet();
            if (is_err_packet(row_frame.payload)) {
                throw parse_error_packet(row_frame.payload);
            }
            if (is_eof_packet(row_frame.payload)) {
                if (row_frame.payload.length() >= 5) {
                    PacketCursor eof(row_frame.payload);
                    eof.read_u8();
                    result.ok.warning_count = eof.read_u16_le();
                    result.ok.server_status = eof.read_u16_le();
                }
                break;
            }
            result.rows.push_back(binary_rows
                ? parse_binary_row(row_frame.payload, result.fields, options_)
                : parse_text_row(row_frame.payload, result.fields, options_));
        }
        return result;
    }

    ConnectionOptions options_;
    EventContext ctx_;
    io::TcpSocket socket_;
    std::unique_ptr<io::TlsContext> tls_context_;
    std::unique_ptr<io::TlsStream> tls_stream_;
    bool connected_ = false;
    bool tls_active_ = false;
    Handshake handshake_;
    std::string server_version_;
    uint32_t connection_id_ = 0;
    uint32_t server_capability_flags_ = 0;
    std::string client_encoding_ = "utf8";
};

Error::Error(const std::string& message) : std::runtime_error(message) {}

Error::Error(uint16_t code, std::string sql_state, std::string message)
    : std::runtime_error(make_error_message(code, sql_state, message)), code_(code), sql_state_(std::move(sql_state)) {}

uint16_t Error::code() const noexcept { return code_; }

const std::string& Error::sql_state() const noexcept { return sql_state_; }

bool Field::is_unsigned() const noexcept { return (flags & constants::field_flags::UNSIGNED) != 0; }

bool Field::is_binary() const noexcept { return (flags & constants::field_flags::BINARY) != 0; }

const Value& Row::at(std::size_t index) const {
    if (index >= values.size()) {
        throw std::out_of_range("mysql2 row index is out of range");
    }
    return values[index];
}

const Value& Row::at(const std::string& name) const {
    const auto it = index_by_name.find(name);
    if (it == index_by_name.end()) {
        throw std::out_of_range("mysql2 row field does not exist: " + name);
    }
    return at(it->second);
}

bool QueryResult::has_rows() const noexcept { return !fields.empty(); }

RawSql raw(std::string sql) { return RawSql{std::move(sql)}; }

std::string escape(const Value& value) { return value_to_string(value); }

std::string escape(std::nullptr_t) { return "NULL"; }

std::string escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (const auto ch : value) {
        switch (ch) {
            case '\0': out += "\\0"; break;
            case '\b': out += "\\b"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case 0x1a: out += "\\Z"; break;
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\\'"; break;
            case '"': out += "\\\""; break;
            default: out.push_back(ch); break;
        }
    }
    out.push_back('\'');
    return out;
}

std::string escape(const char* value) { return value == nullptr ? "NULL" : escape(std::string(value)); }

std::string escape(double value) {
    if (!std::isfinite(value)) return "NULL";
    std::ostringstream out;
    out.precision(17);
    out << value;
    return out.str();
}

std::string escape(int64_t value) { return std::to_string(value); }

std::string escape(uint64_t value) { return std::to_string(value); }

std::string escape_id(const std::string& identifier, bool forbid_qualified) {
    std::string out;
    out.reserve(identifier.size() + 2);
    out.push_back('`');
    for (const auto ch : identifier) {
        if (!forbid_qualified && ch == '.') {
            out += "`.`";
        } else if (ch == '`') {
            out += "``";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('`');
    return out;
}

std::string format(const std::string& sql, const std::vector<Value>& values) {
    std::string out;
    out.reserve(sql.size() + values.size() * 8);
    std::size_t value_index = 0;
    for (std::size_t i = 0; i < sql.size(); ++i) {
        if (sql[i] == '?' && value_index < values.size()) {
            if (i + 1 < sql.size() && sql[i + 1] == '?') {
                if (const auto* text = std::get_if<std::string>(&values[value_index])) {
                    out += escape_id(*text);
                } else {
                    out += escape(values[value_index]);
                }
                ++i;
                ++value_index;
            } else {
                out += escape(values[value_index++]);
            }
        } else {
            out.push_back(sql[i]);
        }
    }
    return out;
}

std::string format_named(const std::string& sql, const std::unordered_map<std::string, Value>& values) {
    std::string out;
    out.reserve(sql.size() + values.size() * 8);
    enum class Quote { none, single, dbl, backtick } quote = Quote::none;
    for (std::size_t i = 0; i < sql.size(); ++i) {
        const char ch = sql[i];
        if (quote == Quote::none) {
            if (ch == '\'') quote = Quote::single;
            else if (ch == '"') quote = Quote::dbl;
            else if (ch == '`') quote = Quote::backtick;
            if (ch == ':' && i + 1 < sql.size() && (std::isalpha(static_cast<unsigned char>(sql[i + 1])) || sql[i + 1] == '_')) {
                std::size_t end = i + 2;
                while (end < sql.size() && (std::isalnum(static_cast<unsigned char>(sql[end])) || sql[end] == '_')) {
                    ++end;
                }
                const auto name = sql.substr(i + 1, end - i - 1);
                const auto it = values.find(name);
                if (it == values.end()) {
                    throw Error("missing named placeholder value: " + name);
                }
                out += escape(it->second);
                i = end - 1;
                continue;
            }
        } else if (quote == Quote::single && ch == '\'' && (i == 0 || sql[i - 1] != '\\')) {
            quote = Quote::none;
        } else if (quote == Quote::dbl && ch == '"' && (i == 0 || sql[i - 1] != '\\')) {
            quote = Quote::none;
        } else if (quote == Quote::backtick && ch == '`') {
            quote = Quote::none;
        }
        out.push_back(ch);
    }
    return out;
}

Connection::Connection() : impl_(new Impl(ConnectionOptions{})) {}

Connection::Connection(ConnectionOptions options) : impl_(new Impl(std::move(options))) {}

Connection::~Connection() {
    if (impl_) {
        impl_->end();
        delete impl_;
    }
}

Connection::Connection(Connection&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (impl_) {
            impl_->end();
            delete impl_;
        }
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

void Connection::connect() { impl_->connect(); }

QueryResult Connection::query(const std::string& sql) { return impl_->query(sql); }

std::vector<QueryResult> Connection::query_all(const std::string& sql) { return impl_->query_all(sql); }

PreparedStatement Connection::prepare(const std::string& sql) { return impl_->prepare(sql); }

QueryResult Connection::execute(const PreparedStatement& statement, const std::vector<Value>& values) {
    return impl_->execute(statement, values);
}

std::vector<QueryResult> Connection::execute_all(const PreparedStatement& statement, const std::vector<Value>& values) {
    return impl_->execute_all(statement, values);
}

QueryResult Connection::execute(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute(sql, values);
}

std::vector<QueryResult> Connection::execute_all(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute_all(sql, values);
}

void Connection::close_statement(const PreparedStatement& statement) { impl_->close_statement(statement); }

OkPacket Connection::begin_transaction() { return impl_->begin_transaction(); }

OkPacket Connection::commit() { return impl_->commit(); }

OkPacket Connection::rollback() { return impl_->rollback(); }

OkPacket Connection::ping() { return impl_->ping(); }

void Connection::reset() { impl_->reset(); }

void Connection::end() { impl_->end(); }

bool Connection::connected() const noexcept { return impl_ && impl_->connected(); }

bool Connection::encrypted() const noexcept { return impl_ && impl_->encrypted(); }

const ConnectionOptions& Connection::options() const noexcept { return impl_->options(); }

const std::string& Connection::server_version() const noexcept { return impl_->server_version(); }

uint32_t Connection::connection_id() const noexcept { return impl_->connection_id(); }

uint32_t Connection::server_capability_flags() const noexcept { return impl_->server_capability_flags(); }

class PoolImpl : public std::enable_shared_from_this<PoolImpl> {
public:
    explicit PoolImpl(PoolOptions options) : options_(std::move(options)) {
        if (options_.connection_limit == 0) {
            throw Error("pool connection_limit must be greater than zero");
        }
    }

    ~PoolImpl() {
        end();
    }

    PoolConnection acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) {
            throw Error("pool is closed");
        }

        if (!idle_.empty()) {
            auto connection = idle_.back();
            idle_.pop_back();
            return PoolConnection(shared_from_this(), connection);
        }

        if (connections_.size() < options_.connection_limit) {
            auto connection = std::make_shared<Connection>(options_.connection);
            connection->connect();
            connections_.push_back(std::move(connection));
            return PoolConnection(shared_from_this(), connections_.back());
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(options_.wait_timeout_ms);
        while (!closed_ && idle_.empty()) {
            if (options_.wait_timeout_ms == 0) {
                available_.wait(lock);
            } else if (available_.wait_until(lock, deadline) == std::cv_status::timeout) {
                throw Error("timed out waiting for a pooled mysql2 connection");
            }
        }
        if (closed_) {
            throw Error("pool is closed");
        }
        auto connection = idle_.back();
        idle_.pop_back();
        return PoolConnection(shared_from_this(), connection);
    }

    void release(std::shared_ptr<Connection> connection) noexcept {
        if (!connection) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            connection->end();
            return;
        }
        idle_.push_back(connection);
        available_.notify_one();
    }

    void end() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ && connections_.empty()) {
            return;
        }
        closed_ = true;
        idle_.clear();
        for (auto& connection : connections_) {
            if (connection) {
                connection->end();
            }
        }
        connections_.clear();
        available_.notify_all();
    }

    QueryResult query(const std::string& sql) {
        auto connection = acquire();
        return connection->query(sql);
    }

    std::vector<QueryResult> query_all(const std::string& sql) {
        auto connection = acquire();
        return connection->query_all(sql);
    }

    QueryResult execute(const std::string& sql, const std::vector<Value>& values) {
        auto connection = acquire();
        return connection->execute(sql, values);
    }

    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values) {
        auto connection = acquire();
        return connection->execute_all(sql, values);
    }

    std::size_t total_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return connections_.size();
    }

    std::size_t idle_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return idle_.size();
    }

private:
    PoolOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<std::shared_ptr<Connection>> connections_;
    std::vector<std::shared_ptr<Connection>> idle_;
    bool closed_ = false;
};

PoolConnection::PoolConnection(std::shared_ptr<PoolImpl> pool, std::shared_ptr<Connection> connection)
    : pool_(std::move(pool)), connection_(connection) {}

PoolConnection::~PoolConnection() {
    release();
}

PoolConnection::PoolConnection(PoolConnection&& other) noexcept
    : pool_(std::move(other.pool_)), connection_(std::move(other.connection_)) {}

PoolConnection& PoolConnection::operator=(PoolConnection&& other) noexcept {
    if (this != &other) {
        release();
        pool_ = std::move(other.pool_);
        connection_ = std::move(other.connection_);
    }
    return *this;
}

Connection& PoolConnection::get() {
    if (!connection_) {
        throw Error("pooled connection has been released");
    }
    return *connection_;
}

const Connection& PoolConnection::get() const {
    if (!connection_) {
        throw Error("pooled connection has been released");
    }
    return *connection_;
}

Connection* PoolConnection::operator->() { return &get(); }

const Connection* PoolConnection::operator->() const { return &get(); }

Connection& PoolConnection::operator*() { return get(); }

const Connection& PoolConnection::operator*() const { return get(); }

PoolConnection::operator bool() const noexcept { return static_cast<bool>(connection_); }

void PoolConnection::release() {
    if (pool_ && connection_) {
        pool_->release(connection_);
        connection_.reset();
        pool_.reset();
    }
}

Pool::Pool(PoolOptions options) : impl_(std::make_shared<PoolImpl>(std::move(options))) {}

Pool::~Pool() = default;

Pool::Pool(Pool&&) noexcept = default;

Pool& Pool::operator=(Pool&&) noexcept = default;

PoolConnection Pool::get_connection() { return impl_->acquire(); }

QueryResult Pool::query(const std::string& sql) { return impl_->query(sql); }

std::vector<QueryResult> Pool::query_all(const std::string& sql) { return impl_->query_all(sql); }

QueryResult Pool::execute(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute(sql, values);
}

std::vector<QueryResult> Pool::execute_all(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute_all(sql, values);
}

void Pool::end() { impl_->end(); }

std::size_t Pool::total_count() const noexcept { return impl_ ? impl_->total_count() : 0; }

std::size_t Pool::idle_count() const noexcept { return impl_ ? impl_->idle_count() : 0; }

Connection create_connection(ConnectionOptions options) {
    Connection connection(std::move(options));
    connection.connect();
    return connection;
}

Pool create_pool(PoolOptions options) {
    return Pool(std::move(options));
}

QueryResult query(ConnectionOptions options, const std::string& sql) {
    auto connection = create_connection(std::move(options));
    return connection.query(sql);
}

}  // namespace polycpp::mysql2
