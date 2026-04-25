#include <polycpp/mysql2/mysql2.hpp>

#include "aws_rds_ca.hpp"

#include <polycpp/crypto.hpp>
#include <polycpp/iconv_lite/iconv_lite.hpp>
#include <polycpp/io/event_context.hpp>
#include <polycpp/io/tcp_acceptor.hpp>
#include <polycpp/io/tcp_socket.hpp>
#include <polycpp/io/timer.hpp>
#include <polycpp/io/tls_context.hpp>
#include <polycpp/io/tls_stream.hpp>
#include <polycpp/ssl/x509_cert.hpp>
#include <polycpp/url.hpp>
#include <polycpp/zlib.hpp>

#include <algorithm>
#include <any>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <system_error>
#include <type_traits>
#include <utility>

#include <sys/socket.h>

namespace polycpp::mysql2 {
namespace {

namespace client_flag {
constexpr uint32_t LONG_PASSWORD = 0x00000001;
constexpr uint32_t FOUND_ROWS = 0x00000002;
constexpr uint32_t LONG_FLAG = 0x00000004;
constexpr uint32_t CONNECT_WITH_DB = 0x00000008;
constexpr uint32_t COMPRESS = 0x00000020;
constexpr uint32_t ODBC = 0x00000040;
constexpr uint32_t LOCAL_FILES = 0x00000080;
constexpr uint32_t IGNORE_SPACE = 0x00000100;
constexpr uint32_t PROTOCOL_41 = 0x00000200;
constexpr uint32_t SSL = 0x00000800;
constexpr uint32_t IGNORE_SIGPIPE = 0x00001000;
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
constexpr uint8_t INIT_DB = 0x02;
constexpr uint8_t QUERY = 0x03;
constexpr uint8_t FIELD_LIST = 0x04;
constexpr uint8_t CHANGE_USER = 0x11;
constexpr uint8_t BINLOG_DUMP = 0x12;
constexpr uint8_t REGISTER_SLAVE = 0x15;
constexpr uint8_t BINLOG_DUMP_GTID = 0x1e;
constexpr uint8_t PING = 0x0e;
constexpr uint8_t STMT_PREPARE = 0x16;
constexpr uint8_t STMT_EXECUTE = 0x17;
constexpr uint8_t STMT_CLOSE = 0x19;
constexpr uint8_t STMT_FETCH = 0x1c;
constexpr uint8_t RESET_CONNECTION = 0x1f;
}  // namespace command_code

namespace marker {
constexpr uint8_t OK = 0x00;
constexpr uint8_t ERR = 0xff;
constexpr uint8_t EOF_PACKET = 0xfe;
constexpr uint8_t AUTH_MORE_DATA = 0x01;
constexpr uint8_t AUTH_NEXT_FACTOR = 0x02;
}  // namespace marker

namespace server_status_flag {
constexpr uint16_t MORE_RESULTS_EXISTS = 0x0008;
constexpr uint16_t CURSOR_EXISTS = 0x0040;
constexpr uint16_t LAST_ROW_SENT = 0x0080;
}  // namespace server_status_flag

constexpr std::size_t kPacketHeaderLength = 4;
constexpr std::size_t kCompressedPacketHeaderLength = 7;
constexpr std::size_t kMaxPacketPayloadLength = 0x00ffffff;
constexpr std::size_t kMaxCompressedPayloadInputLength = 16777210;
constexpr uint16_t kBinlogDumpNonBlock = 0x01;

std::atomic<std::size_t> g_parser_cache_max{0};

std::size_t normal_packet_count_for_payload(std::size_t payload_length) {
    auto count = payload_length / kMaxPacketPayloadLength + 1;
    if (payload_length > 0 && payload_length % kMaxPacketPayloadLength == 0) {
        ++count;
    }
    return count;
}

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

void append_float_le(std::vector<uint8_t>& out, float value) {
    static_assert(sizeof(float) == 4);
    std::array<uint8_t, 4> bytes{};
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

Buffer encode_string_for_mysql(std::string_view value, const std::string& encoding) {
    const auto text = std::string(value);
    if (encoding.empty() || encoding == "utf8" || encoding == "utf8mb4") {
        return Buffer::from(text);
    }
    if (encoding == "binary") {
        return Buffer::from(text, "latin1");
    }
    if (Buffer::isEncoding(encoding)) {
        return Buffer::from(text, encoding);
    }
    if (iconv_lite::encoding_exists(encoding)) {
        return iconv_lite::encode(text, encoding);
    }
    return Buffer::from(text);
}

void append_encoded_string(std::vector<uint8_t>& out, std::string_view value, const std::string& encoding) {
    append_bytes(out, encode_string_for_mysql(value, encoding));
}

void append_lenenc_encoded_string(std::vector<uint8_t>& out, std::string_view value, const std::string& encoding) {
    const auto encoded = encode_string_for_mysql(value, encoding);
    append_lenenc_buffer(out, encoded);
}

std::string normalize_charset_label(std::string_view charset) {
    std::string normalized;
    normalized.reserve(charset.size());
    for (const auto ch : charset) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

const std::unordered_map<std::string, uint16_t>& charset_number_map() {
    static const std::unordered_map<std::string, uint16_t> map = {
        {"armscii8", 32},
        {"armscii8bin", 64},
        {"armscii8generalci", 32},
        {"ascii", 11},
        {"asciibin", 65},
        {"asciigeneralci", 11},
        {"big5", 1},
        {"big5bin", 84},
        {"big5chineseci", 1},
        {"binary", 63},
        {"cesu8", 33},
        {"cp1250", 26},
        {"cp1250bin", 66},
        {"cp1250croatianci", 44},
        {"cp1250czechcs", 34},
        {"cp1250generalci", 26},
        {"cp1250polishci", 99},
        {"cp1251", 51},
        {"cp1251bin", 50},
        {"cp1251bulgarianci", 14},
        {"cp1251generalci", 51},
        {"cp1251generalcs", 52},
        {"cp1251ukrainianci", 23},
        {"cp1256", 57},
        {"cp1256bin", 67},
        {"cp1256generalci", 57},
        {"cp1257", 59},
        {"cp1257bin", 58},
        {"cp1257generalci", 59},
        {"cp1257lithuanianci", 29},
        {"cp850", 4},
        {"cp850bin", 80},
        {"cp850generalci", 4},
        {"cp852", 40},
        {"cp852bin", 81},
        {"cp852generalci", 40},
        {"cp866", 36},
        {"cp866bin", 68},
        {"cp866generalci", 36},
        {"cp932", 95},
        {"cp932bin", 96},
        {"cp932japaneseci", 95},
        {"dec8", 3},
        {"dec8bin", 69},
        {"dec8swedishci", 3},
        {"eucjp", 12},
        {"eucjpms", 97},
        {"eucjpmsbin", 98},
        {"eucjpmsjapaneseci", 97},
        {"euckr", 19},
        {"euckrbin", 85},
        {"euckrkoreanci", 19},
        {"gb18030", 248},
        {"gb18030bin", 249},
        {"gb18030chineseci", 248},
        {"gb18030unicode520ci", 250},
        {"gb2312", 24},
        {"gb2312bin", 86},
        {"gb2312chineseci", 24},
        {"gbk", 28},
        {"gbkbin", 87},
        {"gbkchineseci", 28},
        {"geostd8", 92},
        {"geostd8bin", 93},
        {"geostd8generalci", 92},
        {"greek", 25},
        {"greekbin", 70},
        {"greekgeneralci", 25},
        {"hebrew", 16},
        {"hebrewbin", 71},
        {"hebrewgeneralci", 16},
        {"hp8", 6},
        {"hp8bin", 72},
        {"hp8englishci", 6},
        {"keybcs2", 37},
        {"keybcs2bin", 73},
        {"keybcs2generalci", 37},
        {"koi8r", 7},
        {"koi8rbin", 74},
        {"koi8rgeneralci", 7},
        {"koi8u", 22},
        {"koi8ubin", 75},
        {"koi8ugeneralci", 22},
        {"latin1", 8},
        {"latin1bin", 47},
        {"latin1danishci", 15},
        {"latin1generalci", 48},
        {"latin1generalcs", 49},
        {"latin1german1ci", 5},
        {"latin1german2ci", 31},
        {"latin1spanishci", 94},
        {"latin1swedishci", 8},
        {"latin2", 9},
        {"latin2bin", 77},
        {"latin2croatianci", 27},
        {"latin2czechcs", 2},
        {"latin2generalci", 9},
        {"latin2hungarianci", 21},
        {"latin5", 30},
        {"latin5bin", 78},
        {"latin5turkishci", 30},
        {"latin7", 41},
        {"latin7bin", 79},
        {"latin7estoniancs", 20},
        {"latin7generalci", 41},
        {"latin7generalcs", 42},
        {"macce", 38},
        {"maccebin", 43},
        {"maccegeneralci", 38},
        {"macintosh", 38},
        {"macroman", 39},
        {"macromanbin", 53},
        {"macromangeneralci", 39},
        {"sjis", 13},
        {"sjisbin", 88},
        {"sjisjapaneseci", 13},
        {"swe7", 10},
        {"swe7bin", 82},
        {"swe7swedishci", 10},
        {"tis620", 18},
        {"tis620bin", 89},
        {"tis620thaici", 18},
        {"ucs2", 35},
        {"ucs2bin", 90},
        {"ucs2croatianci", 149},
        {"ucs2czechci", 138},
        {"ucs2danishci", 139},
        {"ucs2esperantoci", 145},
        {"ucs2estonianci", 134},
        {"ucs2generalci", 35},
        {"ucs2generalmysql500ci", 159},
        {"ucs2german2ci", 148},
        {"ucs2hungarianci", 146},
        {"ucs2icelandicci", 129},
        {"ucs2latvianci", 130},
        {"ucs2lithuanianci", 140},
        {"ucs2persianci", 144},
        {"ucs2polishci", 133},
        {"ucs2romanci", 143},
        {"ucs2romanianci", 131},
        {"ucs2sinhalaci", 147},
        {"ucs2slovakci", 141},
        {"ucs2slovenianci", 132},
        {"ucs2spanish2ci", 142},
        {"ucs2spanishci", 135},
        {"ucs2swedishci", 136},
        {"ucs2turkishci", 137},
        {"ucs2unicode520ci", 150},
        {"ucs2unicodeci", 128},
        {"ucs2vietnameseci", 151},
        {"ujis", 12},
        {"ujisbin", 91},
        {"ujisjapaneseci", 12},
        {"utf16", 54},
        {"utf16bin", 55},
        {"utf16croatianci", 122},
        {"utf16czechci", 111},
        {"utf16danishci", 112},
        {"utf16esperantoci", 118},
        {"utf16estonianci", 107},
        {"utf16generalci", 54},
        {"utf16german2ci", 121},
        {"utf16hungarianci", 119},
        {"utf16icelandicci", 102},
        {"utf16latvianci", 103},
        {"utf16le", 56},
        {"utf16lebin", 62},
        {"utf16legeneralci", 56},
        {"utf16lithuanianci", 113},
        {"utf16persianci", 117},
        {"utf16polishci", 106},
        {"utf16romanci", 116},
        {"utf16romanianci", 104},
        {"utf16sinhalaci", 120},
        {"utf16slovakci", 114},
        {"utf16slovenianci", 105},
        {"utf16spanish2ci", 115},
        {"utf16spanishci", 108},
        {"utf16swedishci", 109},
        {"utf16turkishci", 110},
        {"utf16unicode520ci", 123},
        {"utf16unicodeci", 101},
        {"utf16vietnameseci", 124},
        {"utf32", 60},
        {"utf32bin", 61},
        {"utf32croatianci", 181},
        {"utf32czechci", 170},
        {"utf32danishci", 171},
        {"utf32esperantoci", 177},
        {"utf32estonianci", 166},
        {"utf32generalci", 60},
        {"utf32german2ci", 180},
        {"utf32hungarianci", 178},
        {"utf32icelandicci", 161},
        {"utf32latvianci", 162},
        {"utf32lithuanianci", 172},
        {"utf32persianci", 176},
        {"utf32polishci", 165},
        {"utf32romanci", 175},
        {"utf32romanianci", 163},
        {"utf32sinhalaci", 179},
        {"utf32slovakci", 173},
        {"utf32slovenianci", 164},
        {"utf32spanish2ci", 174},
        {"utf32spanishci", 167},
        {"utf32swedishci", 168},
        {"utf32turkishci", 169},
        {"utf32unicode520ci", 182},
        {"utf32unicodeci", 160},
        {"utf32vietnameseci", 183},
        {"utf8", 33},
        {"utf8bin", 83},
        {"utf8croatianci", 213},
        {"utf8czechci", 202},
        {"utf8danishci", 203},
        {"utf8esperantoci", 209},
        {"utf8estonianci", 198},
        {"utf8general50ci", 253},
        {"utf8generalci", 33},
        {"utf8generalmysql500ci", 223},
        {"utf8german2ci", 212},
        {"utf8hungarianci", 210},
        {"utf8icelandicci", 193},
        {"utf8latvianci", 194},
        {"utf8lithuanianci", 204},
        {"utf8mb3", 33},
        {"utf8mb4", 224},
        {"utf8mb40900aici", 255},
        {"utf8mb40900asci", 305},
        {"utf8mb40900ascs", 278},
        {"utf8mb40900bin", 309},
        {"utf8mb4bin", 46},
        {"utf8mb4croatianci", 245},
        {"utf8mb4cs0900aici", 266},
        {"utf8mb4cs0900ascs", 289},
        {"utf8mb4czechci", 234},
        {"utf8mb4da0900aici", 267},
        {"utf8mb4da0900ascs", 290},
        {"utf8mb4danishci", 235},
        {"utf8mb4depb0900aici", 256},
        {"utf8mb4depb0900ascs", 279},
        {"utf8mb4eo0900aici", 273},
        {"utf8mb4eo0900ascs", 296},
        {"utf8mb4es0900aici", 263},
        {"utf8mb4es0900ascs", 286},
        {"utf8mb4esperantoci", 241},
        {"utf8mb4estonianci", 230},
        {"utf8mb4estrad0900aici", 270},
        {"utf8mb4estrad0900ascs", 293},
        {"utf8mb4et0900aici", 262},
        {"utf8mb4et0900ascs", 285},
        {"utf8mb4generalci", 45},
        {"utf8mb4german2ci", 244},
        {"utf8mb4hr0900aici", 275},
        {"utf8mb4hr0900ascs", 298},
        {"utf8mb4hu0900aici", 274},
        {"utf8mb4hu0900ascs", 297},
        {"utf8mb4hungarianci", 242},
        {"utf8mb4icelandicci", 225},
        {"utf8mb4is0900aici", 257},
        {"utf8mb4is0900ascs", 280},
        {"utf8mb4ja0900ascs", 303},
        {"utf8mb4ja0900ascsks", 304},
        {"utf8mb4la0900aici", 271},
        {"utf8mb4la0900ascs", 294},
        {"utf8mb4latvianci", 226},
        {"utf8mb4lithuanianci", 236},
        {"utf8mb4lt0900aici", 268},
        {"utf8mb4lt0900ascs", 291},
        {"utf8mb4lv0900aici", 258},
        {"utf8mb4lv0900ascs", 281},
        {"utf8mb4persianci", 240},
        {"utf8mb4pl0900aici", 261},
        {"utf8mb4pl0900ascs", 284},
        {"utf8mb4polishci", 229},
        {"utf8mb4ro0900aici", 259},
        {"utf8mb4ro0900ascs", 282},
        {"utf8mb4romanci", 239},
        {"utf8mb4romanianci", 227},
        {"utf8mb4ru0900aici", 306},
        {"utf8mb4ru0900ascs", 307},
        {"utf8mb4sinhalaci", 243},
        {"utf8mb4sk0900aici", 269},
        {"utf8mb4sk0900ascs", 292},
        {"utf8mb4sl0900aici", 260},
        {"utf8mb4sl0900ascs", 283},
        {"utf8mb4slovakci", 237},
        {"utf8mb4slovenianci", 228},
        {"utf8mb4spanish2ci", 238},
        {"utf8mb4spanishci", 231},
        {"utf8mb4sv0900aici", 264},
        {"utf8mb4sv0900ascs", 287},
        {"utf8mb4swedishci", 232},
        {"utf8mb4tr0900aici", 265},
        {"utf8mb4tr0900ascs", 288},
        {"utf8mb4turkishci", 233},
        {"utf8mb4unicode520ci", 246},
        {"utf8mb4unicodeci", 224},
        {"utf8mb4vi0900aici", 277},
        {"utf8mb4vi0900ascs", 300},
        {"utf8mb4vietnameseci", 247},
        {"utf8mb4zh0900ascs", 308},
        {"utf8persianci", 208},
        {"utf8polishci", 197},
        {"utf8romanci", 207},
        {"utf8romanianci", 195},
        {"utf8sinhalaci", 211},
        {"utf8slovakci", 205},
        {"utf8slovenianci", 196},
        {"utf8spanish2ci", 206},
        {"utf8spanishci", 199},
        {"utf8swedishci", 200},
        {"utf8tolowerci", 76},
        {"utf8turkishci", 201},
        {"utf8unicode520ci", 214},
        {"utf8unicodeci", 192},
        {"utf8vietnameseci", 215},
    };
    return map;
}

uint16_t charset_number_for_name(const std::string& charset, uint16_t fallback) {
    const auto normalized = normalize_charset_label(charset);
    if (normalized.empty()) return fallback;
    const auto& map = charset_number_map();
    const auto it = map.find(normalized);
    if (it != map.end()) return it->second;
    throw Error("unsupported charset option: " + charset);
}

const std::vector<std::string>& charset_encoding_table() {
    static const std::vector<std::string> table = {
        "utf8", // 0
        "big5", // 1
        "latin2", // 2
        "dec8", // 3
        "cp850", // 4
        "latin1", // 5
        "hp8", // 6
        "koi8r", // 7
        "latin1", // 8
        "latin2", // 9
        "swe7", // 10
        "ascii", // 11
        "eucjp", // 12
        "sjis", // 13
        "cp1251", // 14
        "latin1", // 15
        "hebrew", // 16
        "utf8", // 17
        "tis620", // 18
        "euckr", // 19
        "latin7", // 20
        "latin2", // 21
        "koi8u", // 22
        "cp1251", // 23
        "gb2312", // 24
        "greek", // 25
        "cp1250", // 26
        "latin2", // 27
        "gbk", // 28
        "cp1257", // 29
        "latin5", // 30
        "latin1", // 31
        "armscii8", // 32
        "cesu8", // 33
        "cp1250", // 34
        "ucs2", // 35
        "cp866", // 36
        "keybcs2", // 37
        "macintosh", // 38
        "macroman", // 39
        "cp852", // 40
        "latin7", // 41
        "latin7", // 42
        "macintosh", // 43
        "cp1250", // 44
        "utf8", // 45
        "utf8", // 46
        "latin1", // 47
        "latin1", // 48
        "latin1", // 49
        "cp1251", // 50
        "cp1251", // 51
        "cp1251", // 52
        "macroman", // 53
        "utf16", // 54
        "utf16", // 55
        "utf16-le", // 56
        "cp1256", // 57
        "cp1257", // 58
        "cp1257", // 59
        "utf32", // 60
        "utf32", // 61
        "utf16-le", // 62
        "binary", // 63
        "armscii8", // 64
        "ascii", // 65
        "cp1250", // 66
        "cp1256", // 67
        "cp866", // 68
        "dec8", // 69
        "greek", // 70
        "hebrew", // 71
        "hp8", // 72
        "keybcs2", // 73
        "koi8r", // 74
        "koi8u", // 75
        "cesu8", // 76
        "latin2", // 77
        "latin5", // 78
        "latin7", // 79
        "cp850", // 80
        "cp852", // 81
        "swe7", // 82
        "cesu8", // 83
        "big5", // 84
        "euckr", // 85
        "gb2312", // 86
        "gbk", // 87
        "sjis", // 88
        "tis620", // 89
        "ucs2", // 90
        "eucjp", // 91
        "geostd8", // 92
        "geostd8", // 93
        "latin1", // 94
        "cp932", // 95
        "cp932", // 96
        "eucjpms", // 97
        "eucjpms", // 98
        "cp1250", // 99
        "utf16", // 100
        "utf16", // 101
        "utf16", // 102
        "utf16", // 103
        "utf16", // 104
        "utf16", // 105
        "utf16", // 106
        "utf16", // 107
        "utf16", // 108
        "utf16", // 109
        "utf16", // 110
        "utf16", // 111
        "utf16", // 112
        "utf16", // 113
        "utf16", // 114
        "utf16", // 115
        "utf16", // 116
        "utf16", // 117
        "utf16", // 118
        "utf16", // 119
        "utf16", // 120
        "utf16", // 121
        "utf16", // 122
        "utf16", // 123
        "utf16", // 124
        "utf8", // 125
        "utf8", // 126
        "utf8", // 127
        "ucs2", // 128
        "ucs2", // 129
        "ucs2", // 130
        "ucs2", // 131
        "ucs2", // 132
        "ucs2", // 133
        "ucs2", // 134
        "ucs2", // 135
        "ucs2", // 136
        "ucs2", // 137
        "ucs2", // 138
        "ucs2", // 139
        "ucs2", // 140
        "ucs2", // 141
        "ucs2", // 142
        "ucs2", // 143
        "ucs2", // 144
        "ucs2", // 145
        "ucs2", // 146
        "ucs2", // 147
        "ucs2", // 148
        "ucs2", // 149
        "ucs2", // 150
        "ucs2", // 151
        "utf8", // 152
        "utf8", // 153
        "utf8", // 154
        "utf8", // 155
        "utf8", // 156
        "utf8", // 157
        "utf8", // 158
        "ucs2", // 159
        "utf32", // 160
        "utf32", // 161
        "utf32", // 162
        "utf32", // 163
        "utf32", // 164
        "utf32", // 165
        "utf32", // 166
        "utf32", // 167
        "utf32", // 168
        "utf32", // 169
        "utf32", // 170
        "utf32", // 171
        "utf32", // 172
        "utf32", // 173
        "utf32", // 174
        "utf32", // 175
        "utf32", // 176
        "utf32", // 177
        "utf32", // 178
        "utf32", // 179
        "utf32", // 180
        "utf32", // 181
        "utf32", // 182
        "utf32", // 183
        "utf8", // 184
        "utf8", // 185
        "utf8", // 186
        "utf8", // 187
        "utf8", // 188
        "utf8", // 189
        "utf8", // 190
        "utf8", // 191
        "cesu8", // 192
        "cesu8", // 193
        "cesu8", // 194
        "cesu8", // 195
        "cesu8", // 196
        "cesu8", // 197
        "cesu8", // 198
        "cesu8", // 199
        "cesu8", // 200
        "cesu8", // 201
        "cesu8", // 202
        "cesu8", // 203
        "cesu8", // 204
        "cesu8", // 205
        "cesu8", // 206
        "cesu8", // 207
        "cesu8", // 208
        "cesu8", // 209
        "cesu8", // 210
        "cesu8", // 211
        "cesu8", // 212
        "cesu8", // 213
        "cesu8", // 214
        "cesu8", // 215
        "utf8", // 216
        "utf8", // 217
        "utf8", // 218
        "utf8", // 219
        "utf8", // 220
        "utf8", // 221
        "utf8", // 222
        "cesu8", // 223
        "utf8", // 224
        "utf8", // 225
        "utf8", // 226
        "utf8", // 227
        "utf8", // 228
        "utf8", // 229
        "utf8", // 230
        "utf8", // 231
        "utf8", // 232
        "utf8", // 233
        "utf8", // 234
        "utf8", // 235
        "utf8", // 236
        "utf8", // 237
        "utf8", // 238
        "utf8", // 239
        "utf8", // 240
        "utf8", // 241
        "utf8", // 242
        "utf8", // 243
        "utf8", // 244
        "utf8", // 245
        "utf8", // 246
        "utf8", // 247
        "gb18030", // 248
        "gb18030", // 249
        "gb18030", // 250
        "utf8", // 251
        "utf8", // 252
        "utf8", // 253
        "utf8", // 254
        "utf8", // 255
        "utf8", // 256
        "utf8", // 257
        "utf8", // 258
        "utf8", // 259
        "utf8", // 260
        "utf8", // 261
        "utf8", // 262
        "utf8", // 263
        "utf8", // 264
        "utf8", // 265
        "utf8", // 266
        "utf8", // 267
        "utf8", // 268
        "utf8", // 269
        "utf8", // 270
        "utf8", // 271
        "utf8", // 272
        "utf8", // 273
        "utf8", // 274
        "utf8", // 275
        "utf8", // 276
        "utf8", // 277
        "utf8", // 278
        "utf8", // 279
        "utf8", // 280
        "utf8", // 281
        "utf8", // 282
        "utf8", // 283
        "utf8", // 284
        "utf8", // 285
        "utf8", // 286
        "utf8", // 287
        "utf8", // 288
        "utf8", // 289
        "utf8", // 290
        "utf8", // 291
        "utf8", // 292
        "utf8", // 293
        "utf8", // 294
        "utf8", // 295
        "utf8", // 296
        "utf8", // 297
        "utf8", // 298
        "utf8", // 299
        "utf8", // 300
        "utf8", // 301
        "utf8", // 302
        "utf8", // 303
        "utf8", // 304
        "utf8", // 305
        "utf8", // 306
        "utf8", // 307
        "utf8", // 308
        "utf8", // 309
    };
    return table;
}

std::string charset_encoding(uint16_t charset) {
    const auto& table = charset_encoding_table();
    if (charset < table.size() && !table[charset].empty()) return table[charset];
    return "utf8";
}

uint8_t handshake_charset_byte(uint16_t charset) {
    if (charset > 255) {
        throw Error("charset number " + std::to_string(charset) +
                    " cannot be used in the one-byte MySQL handshake charset field");
    }
    return static_cast<uint8_t>(charset);
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
                       client_flag::ODBC |
                       client_flag::LOCAL_FILES |
                       client_flag::IGNORE_SPACE |
                       client_flag::PROTOCOL_41 |
                       client_flag::IGNORE_SIGPIPE |
                       client_flag::TRANSACTIONS |
                       client_flag::RESERVED |
                       client_flag::SECURE_CONNECTION |
                       client_flag::MULTI_RESULTS |
                       client_flag::PS_MULTI_RESULTS |
                       client_flag::PLUGIN_AUTH |
                       client_flag::PLUGIN_AUTH_LENENC_CLIENT_DATA |
                       client_flag::SESSION_TRACK |
                       client_flag::CONNECT_ATTRS |
                       client_flag::CLIENT_QUERY_ATTRIBUTES;
    if (!options.database.empty()) {
        desired |= client_flag::CONNECT_WITH_DB;
    }
    if (options.multiple_statements) {
        desired |= client_flag::MULTI_STATEMENTS;
    }
    if (options.ssl.enabled) {
        desired |= client_flag::SSL;
    }
    if (options.compress) {
        desired |= client_flag::COMPRESS;
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

void append_connect_attributes(std::vector<uint8_t>& out, const ConnectionOptions& options, const std::string& encoding) {
    std::map<std::string, std::string> attributes = {
        {"_client_name", "polycpp-mysql2"},
        {"_client_version", "0.1.0"},
    };
    for (const auto& [key, value] : options.connect_attributes) {
        attributes[key] = value;
    }

    std::vector<uint8_t> encoded_attributes;
    for (const auto& [key, value] : attributes) {
        append_lenenc_encoded_string(encoded_attributes, key, encoding);
        append_lenenc_encoded_string(encoded_attributes, value, encoding);
    }
    append_lenenc_int(out, encoded_attributes.size());
    out.insert(out.end(), encoded_attributes.begin(), encoded_attributes.end());
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
    append_u8(out, handshake_charset_byte(options.charset_number));
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
    if (client_flags & client_flag::CONNECT_ATTRS) {
        append_connect_attributes(out, options, "utf8");
    }
    return buffer_from_bytes(out);
}

Buffer build_ssl_request(const ConnectionOptions& options, uint32_t client_flags) {
    std::vector<uint8_t> out;
    out.reserve(32);
    append_u32_le(out, client_flags | client_flag::SSL);
    append_u32_le(out, 0);  // max packet size: 0 means default/no client-side cap.
    append_u8(out, handshake_charset_byte(options.charset_number));
    out.insert(out.end(), 23, 0);
    return buffer_from_bytes(out);
}

Buffer build_change_user_payload(const ConnectionOptions& options,
                                 uint32_t client_flags,
                                 const std::string& auth_plugin_name,
                                 const Buffer& auth_token) {
    std::vector<uint8_t> out;
    out.reserve(64 + options.user.size() + options.database.size() + auth_token.length());
    append_u8(out, command_code::CHANGE_USER);
    append_null_string(out, options.user);
    if (client_flags & client_flag::SECURE_CONNECTION) {
        if (auth_token.length() > 255) {
            throw Error("authentication token is too large for COM_CHANGE_USER packet");
        }
        append_u8(out, static_cast<uint8_t>(auth_token.length()));
        append_bytes(out, auth_token);
    } else {
        append_bytes(out, auth_token);
        append_u8(out, 0);
    }
    append_null_string(out, options.database);
    append_u16_le(out, options.charset_number);
    if (client_flags & client_flag::PLUGIN_AUTH) {
        append_null_string(out, auth_plugin_name);
    }
    if (client_flags & client_flag::CONNECT_ATTRS) {
        append_connect_attributes(out, options, charset_encoding(options.charset_number));
    }
    return buffer_from_bytes(out);
}

uint32_t default_server_capability_flags() {
    return client_flag::LONG_PASSWORD |
           client_flag::FOUND_ROWS |
           client_flag::LONG_FLAG |
           client_flag::PROTOCOL_41 |
           client_flag::TRANSACTIONS |
           client_flag::SECURE_CONNECTION |
           client_flag::MULTI_RESULTS |
           client_flag::PS_MULTI_RESULTS |
           client_flag::PLUGIN_AUTH |
           client_flag::PLUGIN_AUTH_LENENC_CLIENT_DATA |
           client_flag::SESSION_TRACK |
           client_flag::CONNECT_ATTRS;
}

Buffer random_scramble() {
    std::array<uint8_t, 20> bytes{};
    std::random_device rd;
    for (auto& byte : bytes) {
        auto value = rd();
        byte = static_cast<uint8_t>(value & 0xff);
        if (byte == 0) {
            byte = 1;
        }
    }
    return Buffer::from(bytes.data(), bytes.size());
}

Buffer build_server_handshake_payload(const ServerHandshakeOptions& options, const Buffer& scramble) {
    const auto capability_flags = options.capability_flags == 0
        ? default_server_capability_flags()
        : options.capability_flags;
    const auto auth_plugin = options.auth_plugin_name.empty()
        ? std::string("mysql_native_password")
        : options.auth_plugin_name;
    const auto auth1 = scramble.length() >= 8 ? scramble.slice(0, 8) : scramble;
    const auto auth2 = scramble.length() > 8 ? scramble.slice(8, std::min<std::size_t>(20, scramble.length())) : Buffer{};

    std::vector<uint8_t> payload;
    payload.reserve(80 + options.server_version.size() + auth_plugin.size());
    append_u8(payload, options.protocol_version);
    append_null_string(payload, options.server_version);
    append_u32_le(payload, options.connection_id);
    append_bytes(payload, auth1);
    for (std::size_t i = auth1.length(); i < 8; ++i) {
        append_u8(payload, 1);
    }
    append_u8(payload, 0);
    append_u16_le(payload, static_cast<uint16_t>(capability_flags & 0xffff));
    append_u8(payload, options.character_set);
    append_u16_le(payload, options.status_flags);
    append_u16_le(payload, static_cast<uint16_t>((capability_flags >> 16) & 0xffff));
    append_u8(payload, 21);
    payload.insert(payload.end(), 10, 0);
    append_bytes(payload, auth2);
    for (std::size_t i = auth2.length(); i < 12; ++i) {
        append_u8(payload, 1);
    }
    append_u8(payload, 0);
    append_null_string(payload, auth_plugin);
    return buffer_from_bytes(payload);
}

ServerAuthInfo parse_server_handshake_response(const Buffer& payload,
                                               uint32_t server_flags,
                                               const std::string& address,
                                               uint16_t port) {
    PacketCursor cursor(payload);
    ServerAuthInfo info;
    info.address = address;
    info.port = port;
    info.client_flags = cursor.read_u32_le();
    cursor.read_u32_le();  // max packet size
    info.charset_number = cursor.read_u8();
    const auto encoding = charset_encoding(info.charset_number);
    cursor.skip(23);
    info.user = PacketCursor::decode_buffer(buffer_from_string(cursor.read_null_terminated_ascii()), encoding);

    const auto flag_enabled = [&](uint32_t flag) {
        return (info.client_flags & server_flags & flag) != 0;
    };
    if (flag_enabled(client_flag::PLUGIN_AUTH_LENENC_CLIENT_DATA)) {
        const auto length = cursor.read_lenenc_int().value_or(0);
        info.auth_token = cursor.read_buffer(static_cast<std::size_t>(length));
    } else if (flag_enabled(client_flag::SECURE_CONNECTION)) {
        info.auth_token = cursor.read_buffer(cursor.read_u8());
    } else {
        const auto start = cursor.offset();
        auto token = cursor.read_null_terminated_ascii();
        info.auth_token = buffer_from_string(token);
        (void)start;
    }
    if (flag_enabled(client_flag::CONNECT_WITH_DB) && cursor.has_more()) {
        info.database = PacketCursor::decode_buffer(buffer_from_string(cursor.read_null_terminated_ascii()), encoding);
    }
    if (flag_enabled(client_flag::PLUGIN_AUTH) && cursor.has_more()) {
        info.auth_plugin_name = PacketCursor::decode_buffer(buffer_from_string(cursor.read_null_terminated_ascii()), encoding);
    }
    if (flag_enabled(client_flag::CONNECT_ATTRS) && cursor.has_more()) {
        const auto attrs_length = cursor.read_lenenc_int().value_or(0);
        const auto attrs_end = cursor.offset() + static_cast<std::size_t>(attrs_length);
        while (cursor.offset() < attrs_end) {
            auto key = cursor.read_lenenc_string(encoding).value_or("");
            auto value = cursor.read_lenenc_string(encoding).value_or("");
            info.connect_attributes[std::move(key)] = std::move(value);
        }
    }
    return info;
}

void append_packet_bytes(std::vector<uint8_t>& out, const Buffer& payload, uint8_t& sequence_id) {
    std::size_t offset = 0;
    const bool needs_empty_tail = payload.length() > 0 && payload.length() % kMaxPacketPayloadLength == 0;
    do {
        const auto chunk_length = std::min<std::size_t>(kMaxPacketPayloadLength, payload.length() - offset);
        append_u24_le(out, static_cast<uint32_t>(chunk_length));
        append_u8(out, sequence_id++);
        if (chunk_length > 0) {
            out.insert(out.end(), payload.data() + offset, payload.data() + offset + chunk_length);
        }
        offset += chunk_length;
    } while (offset < payload.length());
    if (needs_empty_tail) {
        append_u24_le(out, 0);
        append_u8(out, sequence_id++);
    }
}

Buffer build_ok_payload(const OkPacket& ok) {
    std::vector<uint8_t> payload;
    append_u8(payload, marker::OK);
    append_lenenc_int(payload, ok.affected_rows);
    append_lenenc_int(payload, ok.insert_id);
    append_u16_le(payload, ok.server_status == 0 ? 2 : ok.server_status);
    append_u16_le(payload, ok.warning_count);
    if (!ok.info.empty()) {
        append_encoded_string(payload, ok.info, "utf8");
    }
    return buffer_from_bytes(payload);
}

Buffer build_eof_payload(uint16_t warnings = 0, uint16_t status = 2) {
    std::vector<uint8_t> payload;
    append_u8(payload, marker::EOF_PACKET);
    append_u16_le(payload, warnings);
    append_u16_le(payload, status);
    return buffer_from_bytes(payload);
}

Buffer build_error_payload(uint16_t code, std::string sql_state, const std::string& message) {
    if (sql_state.empty()) {
        sql_state = "HY000";
    }
    if (sql_state.size() != 5) {
        sql_state = "HY000";
    }
    std::vector<uint8_t> payload;
    append_u8(payload, marker::ERR);
    append_u16_le(payload, code);
    append_u8(payload, '#');
    append_string(payload, sql_state);
    append_encoded_string(payload, message, "utf8");
    return buffer_from_bytes(payload);
}

Buffer build_column_definition_payload(Field field) {
    if (field.catalog.empty()) field.catalog = "def";
    if (field.character_set == 0) field.character_set = 224;
    if (field.encoding.empty()) field.encoding = charset_encoding(field.character_set);
    if (field.column_length == 0) field.column_length = 1024;

    std::vector<uint8_t> payload;
    append_lenenc_encoded_string(payload, field.catalog, field.encoding);
    append_lenenc_encoded_string(payload, field.schema, field.encoding);
    append_lenenc_encoded_string(payload, field.table, field.encoding);
    append_lenenc_encoded_string(payload, field.org_table, field.encoding);
    append_lenenc_encoded_string(payload, field.name, field.encoding);
    append_lenenc_encoded_string(payload, field.org_name, field.encoding);
    append_u8(payload, 0x0c);
    append_u16_le(payload, field.character_set);
    append_u32_le(payload, field.column_length);
    append_u8(payload, field.column_type);
    append_u16_le(payload, field.flags);
    append_u8(payload, field.decimals);
    append_u16_le(payload, 0);
    return buffer_from_bytes(payload);
}

std::string server_value_to_string(const Value& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return {};
        } else if constexpr (std::is_same_v<T, bool>) {
            return item ? "1" : "0";
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
            return std::to_string(item);
        } else if constexpr (std::is_same_v<T, double>) {
            if (!std::isfinite(item)) {
                return {};
            }
            std::ostringstream out;
            out << item;
            return out.str();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return item;
        } else if constexpr (std::is_same_v<T, Buffer>) {
            return item.toString("latin1");
        } else if constexpr (std::is_same_v<T, RawSql>) {
            return item.sql;
        }
    }, value);
}

Buffer build_text_row_payload(const Row& row, const std::vector<Field>& fields) {
    std::vector<uint8_t> payload;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto& value = i < row.values.size() ? row.values[i] : Value{std::monostate{}};
        if (std::holds_alternative<std::monostate>(value)) {
            append_u8(payload, 0xfb);
            continue;
        }
        if (const auto* buffer = std::get_if<Buffer>(&value)) {
            append_lenenc_buffer(payload, *buffer);
            continue;
        }
        append_lenenc_encoded_string(payload, server_value_to_string(value),
                                     fields[i].encoding.empty() ? "utf8" : fields[i].encoding);
    }
    return buffer_from_bytes(payload);
}

std::vector<uint8_t> build_text_result_packets(const std::vector<Row>& rows,
                                               const std::vector<Field>& fields,
                                               uint8_t first_sequence) {
    std::vector<uint8_t> out;
    uint8_t sequence = first_sequence;
    std::vector<uint8_t> header;
    append_lenenc_int(header, fields.size());
    append_packet_bytes(out, buffer_from_bytes(header), sequence);
    for (const auto& field : fields) {
        append_packet_bytes(out, build_column_definition_payload(field), sequence);
    }
    append_packet_bytes(out, build_eof_payload(), sequence);
    for (const auto& row : rows) {
        append_packet_bytes(out, build_text_row_payload(row, fields), sequence);
    }
    append_packet_bytes(out, build_eof_payload(), sequence);
    return out;
}

int64_t value_to_i64(const Value& value) {
    return std::visit([](const auto& item) -> int64_t {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return 0;
        } else if constexpr (std::is_same_v<T, bool>) {
            return item ? 1 : 0;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return item;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return static_cast<int64_t>(item);
        } else if constexpr (std::is_same_v<T, double>) {
            return static_cast<int64_t>(item);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return item.empty() ? 0 : std::stoll(item);
        } else if constexpr (std::is_same_v<T, Buffer>) {
            const auto text = item.toString();
            return text.empty() ? 0 : std::stoll(text);
        } else if constexpr (std::is_same_v<T, RawSql>) {
            return item.sql.empty() ? 0 : std::stoll(item.sql);
        }
    }, value);
}

uint64_t value_to_u64(const Value& value) {
    return std::visit([](const auto& item) -> uint64_t {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return 0;
        } else if constexpr (std::is_same_v<T, bool>) {
            return item ? 1 : 0;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<uint64_t>(item);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return item;
        } else if constexpr (std::is_same_v<T, double>) {
            return static_cast<uint64_t>(item);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return item.empty() ? 0 : static_cast<uint64_t>(std::stoull(item));
        } else if constexpr (std::is_same_v<T, Buffer>) {
            const auto text = item.toString();
            return text.empty() ? 0 : static_cast<uint64_t>(std::stoull(text));
        } else if constexpr (std::is_same_v<T, RawSql>) {
            return item.sql.empty() ? 0 : static_cast<uint64_t>(std::stoull(item.sql));
        }
    }, value);
}

double value_to_double(const Value& value) {
    return std::visit([](const auto& item) -> double {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return 0;
        } else if constexpr (std::is_same_v<T, bool>) {
            return item ? 1 : 0;
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
            return static_cast<double>(item);
        } else if constexpr (std::is_same_v<T, double>) {
            return item;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return item.empty() ? 0 : std::stod(item);
        } else if constexpr (std::is_same_v<T, Buffer>) {
            const auto text = item.toString();
            return text.empty() ? 0 : std::stod(text);
        } else if constexpr (std::is_same_v<T, RawSql>) {
            return item.sql.empty() ? 0 : std::stod(item.sql);
        }
    }, value);
}

Buffer value_to_encoded_buffer(const Value& value, const std::string& encoding) {
    return std::visit([&](const auto& item) -> Buffer {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return {};
        } else if constexpr (std::is_same_v<T, Buffer>) {
            return item;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return encode_string_for_mysql(item, encoding);
        } else if constexpr (std::is_same_v<T, RawSql>) {
            return encode_string_for_mysql(item.sql, encoding);
        } else {
            return encode_string_for_mysql(server_value_to_string(value), encoding);
        }
    }, value);
}

struct DateTimeParts {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint32_t micros = 0;
};

uint32_t parse_fraction_micros(std::string_view fraction) {
    uint32_t micros = 0;
    uint32_t scale = 100000;
    for (std::size_t i = 0; i < fraction.size() && i < 6; ++i) {
        const auto ch = fraction[i];
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            throw Error("invalid fractional seconds in MySQL time value");
        }
        micros += static_cast<uint32_t>(ch - '0') * scale;
        scale /= 10;
    }
    return micros;
}

DateTimeParts parse_datetime_parts(std::string value, bool date_only) {
    if (value.size() < 10 || value[4] != '-' || value[7] != '-') {
        throw Error("invalid MySQL date/datetime string for binary row");
    }
    DateTimeParts out;
    out.year = static_cast<uint16_t>(std::stoi(value.substr(0, 4)));
    out.month = static_cast<uint8_t>(std::stoi(value.substr(5, 2)));
    out.day = static_cast<uint8_t>(std::stoi(value.substr(8, 2)));
    if (!date_only && value.size() > 10) {
        const auto time_start = value[10] == 'T' || value[10] == ' ' ? 11 : 10;
        if (value.size() < time_start + 8 || value[time_start + 2] != ':' || value[time_start + 5] != ':') {
            throw Error("invalid MySQL datetime time component for binary row");
        }
        out.hour = static_cast<uint8_t>(std::stoi(value.substr(time_start, 2)));
        out.minute = static_cast<uint8_t>(std::stoi(value.substr(time_start + 3, 2)));
        out.second = static_cast<uint8_t>(std::stoi(value.substr(time_start + 6, 2)));
        if (value.size() > time_start + 8 && value[time_start + 8] == '.') {
            out.micros = parse_fraction_micros(std::string_view(value).substr(time_start + 9));
        }
    }
    return out;
}

void append_binary_datetime_value(std::vector<uint8_t>& out, const Value& value, bool date_only) {
    const auto text = server_value_to_string(value);
    if (text.empty() || text == "0000-00-00" || text == "0000-00-00 00:00:00") {
        append_u8(out, 0);
        return;
    }
    const auto parts = parse_datetime_parts(text, date_only);
    const bool include_time = !date_only && (parts.hour != 0 || parts.minute != 0 || parts.second != 0 || parts.micros != 0);
    if (date_only || !include_time) {
        append_u8(out, 4);
        append_u16_le(out, parts.year);
        append_u8(out, parts.month);
        append_u8(out, parts.day);
        return;
    }
    append_u8(out, parts.micros == 0 ? 7 : 11);
    append_u16_le(out, parts.year);
    append_u8(out, parts.month);
    append_u8(out, parts.day);
    append_u8(out, parts.hour);
    append_u8(out, parts.minute);
    append_u8(out, parts.second);
    if (parts.micros != 0) {
        append_u32_le(out, parts.micros);
    }
}

void append_binary_time_value(std::vector<uint8_t>& out, const Value& value) {
    auto text = server_value_to_string(value);
    if (text.empty() || text == "00:00:00") {
        append_u8(out, 0);
        return;
    }
    bool negative = false;
    if (!text.empty() && text[0] == '-') {
        negative = true;
        text.erase(text.begin());
    }
    const auto first_colon = text.find(':');
    const auto second_colon = first_colon == std::string::npos ? std::string::npos : text.find(':', first_colon + 1);
    if (first_colon == std::string::npos || second_colon == std::string::npos) {
        throw Error("invalid MySQL time string for binary row");
    }
    const auto hours_total = static_cast<uint32_t>(std::stoul(text.substr(0, first_colon)));
    const auto minutes = static_cast<uint8_t>(std::stoul(text.substr(first_colon + 1, second_colon - first_colon - 1)));
    const auto second_part = text.substr(second_colon + 1);
    const auto dot = second_part.find('.');
    const auto seconds = static_cast<uint8_t>(std::stoul(dot == std::string::npos ? second_part : second_part.substr(0, dot)));
    const auto micros = dot == std::string::npos ? 0 : parse_fraction_micros(std::string_view(second_part).substr(dot + 1));
    append_u8(out, micros == 0 ? 8 : 12);
    append_u8(out, negative ? 1 : 0);
    append_u32_le(out, hours_total / 24);
    append_u8(out, static_cast<uint8_t>(hours_total % 24));
    append_u8(out, minutes);
    append_u8(out, seconds);
    if (micros != 0) {
        append_u32_le(out, micros);
    }
}

void append_binary_result_value(std::vector<uint8_t>& out, const Value& value, const Field& field) {
    using namespace constants::column_type;
    switch (field.column_type) {
        case TINY:
            append_u8(out, static_cast<uint8_t>(value_to_i64(value)));
            break;
        case SHORT:
        case YEAR:
            append_u16_le(out, static_cast<uint16_t>(value_to_i64(value)));
            break;
        case LONG:
        case INT24:
            append_u32_le(out, static_cast<uint32_t>(value_to_i64(value)));
            break;
        case LONGLONG:
            append_u64_le(out, field.is_unsigned() ? value_to_u64(value) : static_cast<uint64_t>(value_to_i64(value)));
            break;
        case FLOAT:
            append_float_le(out, static_cast<float>(value_to_double(value)));
            break;
        case DOUBLE:
            append_double_le(out, value_to_double(value));
            break;
        case DATE:
        case NEWDATE:
            append_binary_datetime_value(out, value, true);
            break;
        case DATETIME:
        case TIMESTAMP:
            append_binary_datetime_value(out, value, false);
            break;
        case TIME:
            append_binary_time_value(out, value);
            break;
        default: {
            const auto bytes = value_to_encoded_buffer(value, field.encoding.empty() ? "utf8" : field.encoding);
            append_lenenc_buffer(out, bytes);
            break;
        }
    }
}

Buffer build_binary_row_payload(const Row& row, const std::vector<Field>& fields) {
    std::vector<uint8_t> payload;
    append_u8(payload, 0);
    const std::size_t null_bitmap_length = (fields.size() + 7 + 2) / 8;
    std::vector<uint8_t> null_bitmap(null_bitmap_length, 0);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto& value = i < row.values.size() ? row.values[i] : Value{std::monostate{}};
        if (std::holds_alternative<std::monostate>(value)) {
            const auto bit = i + 2;
            null_bitmap[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        }
    }
    payload.insert(payload.end(), null_bitmap.begin(), null_bitmap.end());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto& value = i < row.values.size() ? row.values[i] : Value{std::monostate{}};
        if (!std::holds_alternative<std::monostate>(value)) {
            append_binary_result_value(payload, value, fields[i]);
        }
    }
    return buffer_from_bytes(payload);
}

std::vector<uint8_t> build_binary_result_packets(const std::vector<Row>& rows,
                                                 const std::vector<Field>& fields,
                                                 uint8_t first_sequence) {
    std::vector<uint8_t> out;
    uint8_t sequence = first_sequence;
    std::vector<uint8_t> header;
    append_lenenc_int(header, fields.size());
    append_packet_bytes(out, buffer_from_bytes(header), sequence);
    for (const auto& field : fields) {
        append_packet_bytes(out, build_column_definition_payload(field), sequence);
    }
    append_packet_bytes(out, build_eof_payload(), sequence);
    for (const auto& row : rows) {
        append_packet_bytes(out, build_binary_row_payload(row, fields), sequence);
    }
    append_packet_bytes(out, build_eof_payload(), sequence);
    return out;
}

std::vector<uint8_t> build_statement_prepare_ok_packets(uint32_t statement_id,
                                                        const std::vector<Field>& parameters,
                                                        const std::vector<Field>& fields,
                                                        uint16_t warning_count,
                                                        uint8_t first_sequence) {
    std::vector<uint8_t> out;
    uint8_t sequence = first_sequence;
    std::vector<uint8_t> ok;
    append_u8(ok, marker::OK);
    append_u32_le(ok, statement_id);
    append_u16_le(ok, static_cast<uint16_t>(fields.size()));
    append_u16_le(ok, static_cast<uint16_t>(parameters.size()));
    append_u8(ok, 0);
    append_u16_le(ok, warning_count);
    append_packet_bytes(out, buffer_from_bytes(ok), sequence);
    if (!parameters.empty()) {
        for (const auto& parameter : parameters) {
            append_packet_bytes(out, build_column_definition_payload(parameter), sequence);
        }
        append_packet_bytes(out, build_eof_payload(), sequence);
    }
    if (!fields.empty()) {
        for (const auto& field : fields) {
            append_packet_bytes(out, build_column_definition_payload(field), sequence);
        }
        append_packet_bytes(out, build_eof_payload(), sequence);
    }
    return out;
}

bool is_server_execute_supported_type(uint8_t type) {
    using namespace constants::column_type;
    switch (type) {
        case NULL_TYPE:
        case TINY:
        case SHORT:
        case LONG:
        case LONGLONG:
        case FLOAT:
        case DOUBLE:
        case VAR_STRING:
        case STRING:
        case VARCHAR:
        case JSON:
            return true;
        default:
            return false;
    }
}

Value read_server_execute_value(PacketCursor& cursor, uint8_t type, const std::string& encoding) {
    using namespace constants::column_type;
    switch (type) {
        case NULL_TYPE:
            return std::monostate{};
        case TINY:
            return static_cast<int64_t>(cursor.read_i8());
        case SHORT:
            return static_cast<int64_t>(cursor.read_i16_le());
        case LONG:
            return static_cast<int64_t>(cursor.read_i32_le());
        case LONGLONG:
            return cursor.read_i64_le();
        case FLOAT:
            return static_cast<double>(cursor.read_float_le());
        case DOUBLE:
            return cursor.read_double_le();
        case VAR_STRING:
        case STRING:
        case VARCHAR:
        case JSON:
            return cursor.read_lenenc_string(encoding).value_or("");
        default:
            return std::monostate{};
    }
}

ServerStatementExecuteInfo parse_server_statement_execute_payload(const Buffer& payload,
                                                                  const std::string& encoding) {
    PacketCursor cursor(payload);
    ServerStatementExecuteInfo info;
    info.raw_payload = payload;
    info.statement_id = cursor.read_u32_le();
    info.flags = cursor.read_u8();
    info.iteration_count = cursor.read_u32_le();

    try {
        std::size_t bind_flag_offset = std::string::npos;
        for (std::size_t i = cursor.offset(); i + 2 < payload.length(); ++i) {
            if (payload[i] == 1 &&
                is_server_execute_supported_type(payload[i + 1]) &&
                payload[i + 2] == 0) {
                bind_flag_offset = i;
                break;
            }
        }
        if (bind_flag_offset == std::string::npos) {
            return info;
        }

        PacketCursor type_cursor(payload);
        type_cursor.skip(bind_flag_offset + 1);
        std::vector<uint8_t> types;
        while (type_cursor.offset() + 1 < payload.length()) {
            const auto type = payload[type_cursor.offset()];
            const auto unsigned_flag = payload[type_cursor.offset() + 1];
            if (!is_server_execute_supported_type(type) || unsigned_flag != 0) {
                break;
            }
            types.push_back(type);
            type_cursor.skip(2);
        }

        PacketCursor value_cursor(payload);
        value_cursor.skip(type_cursor.offset());
        for (const auto type : types) {
            info.values.push_back(read_server_execute_value(value_cursor, type, encoding));
        }
    } catch (const std::exception&) {
        info.values.clear();
    }
    return info;
}

bool is_server_statement_text(const std::string& sql, const std::string& statement_name) {
    auto first = sql.substr(0, sql.find_first_of(" \t\r\n"));
    std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return first == statement_name;
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

OkPacket parse_resultset_end_packet(const Buffer& payload, uint32_t server_flags) {
    if (is_ok_packet(payload)) {
        return parse_ok_packet(payload, server_flags);
    }
    OkPacket ok;
    if (is_eof_packet(payload) && payload.length() >= 5) {
        PacketCursor eof(payload);
        eof.read_u8();
        ok.warning_count = eof.read_u16_le();
        ok.server_status = eof.read_u16_le();
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

Buffer build_stmt_prepare_payload(const std::string& sql, const std::string& encoding) {
    std::vector<uint8_t> payload;
    payload.reserve(sql.size() + 1);
    append_u8(payload, command_code::STMT_PREPARE);
    append_encoded_string(payload, sql, encoding);
    return buffer_from_bytes(payload);
}

void append_bound_value(std::vector<uint8_t>& payload, const Value& value, const std::string& encoding) {
    struct Visitor {
        std::vector<uint8_t>& payload;
        const std::string& encoding;
        void operator()(std::monostate) const {}
        void operator()(bool value) const { append_u8(payload, value ? 1 : 0); }
        void operator()(int64_t value) const { append_u64_le(payload, static_cast<uint64_t>(value)); }
        void operator()(uint64_t value) const { append_u64_le(payload, value); }
        void operator()(double value) const { append_double_le(payload, value); }
        void operator()(const std::string& value) const { append_lenenc_encoded_string(payload, value, encoding); }
        void operator()(const Buffer& value) const { append_lenenc_buffer(payload, value); }
        void operator()(const RawSql&) const { throw Error("raw SQL values cannot be used as prepared statement parameters"); }
    };
    std::visit(Visitor{payload, encoding}, value);
}

std::pair<uint8_t, uint8_t> bound_type(const Value& value) {
    using namespace constants::column_type;
    struct Visitor {
        std::pair<uint8_t, uint8_t> operator()(std::monostate) const { return {NULL_TYPE, 0}; }
        std::pair<uint8_t, uint8_t> operator()(bool) const { return {TINY, 0}; }
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

bool is_null_value(const Value& value) {
    return std::holds_alternative<std::monostate>(value);
}

Buffer build_stmt_execute_payload(uint32_t statement_id,
                                  std::size_t parameter_count,
                                  const std::vector<Value>& values,
                                  const QueryAttributes& attributes,
                                  const std::string& encoding,
                                  uint32_t client_flags,
                                  CursorType cursor_type) {
    if (values.size() != parameter_count) {
        throw Error("prepared statement expected " + std::to_string(parameter_count) +
                    " parameters, got " + std::to_string(values.size()));
    }
    const bool use_query_attributes = (client_flags & client_flag::CLIENT_QUERY_ATTRIBUTES) != 0;
    if (!attributes.empty() && !use_query_attributes) {
        throw Error("server does not support query attributes for COM_STMT_EXECUTE");
    }
    const std::size_t total_params = values.size() + (use_query_attributes ? attributes.size() : 0);
    std::vector<uint8_t> payload;
    payload.reserve(16 + total_params * 16);
    append_u8(payload, command_code::STMT_EXECUTE);
    append_u32_le(payload, statement_id);
    auto cursor_flags = static_cast<uint8_t>(cursor_type);
    if (use_query_attributes) {
        cursor_flags |= 0x08;  // PARAMETER_COUNT_AVAILABLE
    }
    append_u8(payload, cursor_flags);
    append_u32_le(payload, 1); // iteration count
    if (use_query_attributes) {
        append_lenenc_int(payload, total_params);
    }
    if (total_params > 0) {
        std::vector<const Value*> all_values;
        all_values.reserve(total_params);
        for (const auto& value : values) all_values.push_back(&value);
        std::vector<std::string> attribute_names;
        attribute_names.reserve(attributes.size());
        if (use_query_attributes) {
            for (const auto& [name, value] : attributes) {
                attribute_names.push_back(name);
                all_values.push_back(&value);
            }
        }
        const std::size_t null_bitmap_length = (total_params + 7) / 8;
        std::vector<uint8_t> null_bitmap(null_bitmap_length, 0);
        for (std::size_t i = 0; i < all_values.size(); ++i) {
            if (is_null_value(*all_values[i])) {
                null_bitmap[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
            }
        }
        payload.insert(payload.end(), null_bitmap.begin(), null_bitmap.end());
        append_u8(payload, 1); // new-params-bound-flag
        for (std::size_t i = 0; i < all_values.size(); ++i) {
            const auto [type, flags] = bound_type(*all_values[i]);
            append_u8(payload, type);
            append_u8(payload, flags);
            if (use_query_attributes) {
                const auto name = i < values.size() ? std::string_view{} : std::string_view(attribute_names[i - values.size()]);
                append_lenenc_encoded_string(payload, name, encoding);
            }
        }
        for (const auto* value : all_values) {
            if (!is_null_value(*value)) {
                append_bound_value(payload, *value, encoding);
            }
        }
    }
    return buffer_from_bytes(payload);
}

Buffer build_query_payload(const std::string& sql,
                           const QueryAttributes& attributes,
                           const std::string& encoding,
                           uint32_t client_flags) {
    const bool use_query_attributes = (client_flags & client_flag::CLIENT_QUERY_ATTRIBUTES) != 0;
    if (!attributes.empty() && !use_query_attributes) {
        throw Error("server does not support query attributes for COM_QUERY");
    }

    std::vector<uint8_t> payload;
    payload.reserve(1 + sql.size() + attributes.size() * 16);
    append_u8(payload, command_code::QUERY);
    if (use_query_attributes) {
        append_lenenc_int(payload, attributes.size());
        append_lenenc_int(payload, 1);  // parameter_set_count
        if (!attributes.empty()) {
            std::vector<const Value*> values;
            values.reserve(attributes.size());
            std::vector<std::string> names;
            names.reserve(attributes.size());
            for (const auto& [name, value] : attributes) {
                names.push_back(name);
                values.push_back(&value);
            }
            const std::size_t null_bitmap_length = (values.size() + 7) / 8;
            std::vector<uint8_t> null_bitmap(null_bitmap_length, 0);
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (is_null_value(*values[i])) {
                    null_bitmap[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
                }
            }
            payload.insert(payload.end(), null_bitmap.begin(), null_bitmap.end());
            append_u8(payload, 1);  // new_params_bind_flag
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto [type, flags] = bound_type(*values[i]);
                append_u8(payload, type);
                append_u8(payload, flags);
                append_lenenc_encoded_string(payload, names[i], encoding);
            }
            for (const auto* value : values) {
                if (!is_null_value(*value)) {
                    append_bound_value(payload, *value, encoding);
                }
            }
        }
    }
    append_encoded_string(payload, sql, encoding);
    return buffer_from_bytes(payload);
}

void append_u8_length_encoded_string(std::vector<uint8_t>& out, std::string_view value, const std::string& encoding) {
    const auto encoded = encode_string_for_mysql(value, encoding);
    if (encoded.length() > 255) {
        throw Error("COM_REGISTER_SLAVE length-prefixed field exceeds 255 bytes");
    }
    append_u8(out, static_cast<uint8_t>(encoded.length()));
    append_bytes(out, encoded);
}

Buffer build_register_slave_payload(const RegisterSlaveOptions& options, const std::string& encoding) {
    std::vector<uint8_t> payload;
    payload.reserve(32 + options.slave_hostname.size() + options.slave_user.size() + options.slave_password.size());
    append_u8(payload, command_code::REGISTER_SLAVE);
    append_u32_le(payload, options.server_id);
    append_u8_length_encoded_string(payload, options.slave_hostname, encoding);
    append_u8_length_encoded_string(payload, options.slave_user, encoding);
    append_u8_length_encoded_string(payload, options.slave_password, encoding);
    append_u16_le(payload, options.slave_port);
    append_u32_le(payload, options.replication_rank);
    append_u32_le(payload, options.master_id);
    return buffer_from_bytes(payload);
}

uint64_t read_u48_le(PacketCursor& cursor) {
    const auto bytes = cursor.read_buffer(6);
    uint64_t value = 0;
    for (int i = 0; i < 6; ++i) {
        value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
}

std::string trim_ascii(std::string value) {
    auto first = value.begin();
    while (first != value.end() && std::isspace(static_cast<unsigned char>(*first))) {
        ++first;
    }
    auto last = value.end();
    while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1)))) {
        --last;
    }
    return std::string(first, last);
}

std::vector<std::string> split_ascii(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto pos = value.find(delimiter, start);
        if (pos == std::string::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

uint8_t hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
    if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
    throw Error("invalid UUID hex digit in GTID set");
}

std::array<uint8_t, 16> parse_uuid_bytes(const std::string& uuid) {
    std::string hex;
    hex.reserve(32);
    for (const auto ch : uuid) {
        if (ch == '-') continue;
        hex.push_back(ch);
    }
    if (hex.size() != 32) {
        throw Error("invalid UUID length in GTID set");
    }
    std::array<uint8_t, 16> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<uint8_t>((hex_digit_value(hex[i * 2]) << 4) | hex_digit_value(hex[i * 2 + 1]));
    }
    return out;
}

std::string uuid_from_bytes(const Buffer& bytes) {
    if (bytes.length() != 16) {
        throw Error("invalid SID length in encoded GTID set");
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(digits[(bytes[i] >> 4) & 0x0f]);
        out.push_back(digits[bytes[i] & 0x0f]);
    }
    return out;
}

std::vector<BinlogGtidSource> parse_gtid_set_text(const std::string& gtid_set) {
    std::vector<BinlogGtidSource> out;
    const auto trimmed = trim_ascii(gtid_set);
    if (trimmed.empty()) {
        return out;
    }
    for (auto source_text : split_ascii(trimmed, ',')) {
        source_text = trim_ascii(source_text);
        if (source_text.empty()) {
            continue;
        }
        const auto first_colon = source_text.find(':');
        if (first_colon == std::string::npos) {
            throw Error("invalid GTID set source: missing interval list");
        }
        BinlogGtidSource source;
        source.sid = trim_ascii(source_text.substr(0, first_colon));
        (void)parse_uuid_bytes(source.sid);
        for (auto interval_text : split_ascii(source_text.substr(first_colon + 1), ':')) {
            interval_text = trim_ascii(interval_text);
            if (interval_text.empty()) {
                continue;
            }
            const auto dash = interval_text.find('-');
            BinlogGtidInterval interval;
            if (dash == std::string::npos) {
                interval.start = std::stoull(interval_text);
                interval.end = interval.start;
            } else {
                interval.start = std::stoull(interval_text.substr(0, dash));
                interval.end = std::stoull(interval_text.substr(dash + 1));
            }
            if (interval.start == 0 || interval.end < interval.start) {
                throw Error("invalid GTID interval");
            }
            source.intervals.push_back(interval);
        }
        out.push_back(std::move(source));
    }
    return out;
}

Buffer encode_gtid_set(const std::string& gtid_set) {
    const auto sources = parse_gtid_set_text(gtid_set);
    std::vector<uint8_t> out;
    append_u64_le(out, sources.size());
    for (const auto& source : sources) {
        const auto sid = parse_uuid_bytes(source.sid);
        out.insert(out.end(), sid.begin(), sid.end());
        append_u64_le(out, source.intervals.size());
        for (const auto& interval : source.intervals) {
            append_u64_le(out, interval.start);
            append_u64_le(out, interval.end + 1);
        }
    }
    return buffer_from_bytes(out);
}

std::vector<BinlogGtidSource> decode_gtid_set(Buffer encoded) {
    PacketCursor cursor(std::move(encoded));
    std::vector<BinlogGtidSource> out;
    if (!cursor.has_more()) {
        return out;
    }
    const auto source_count = cursor.read_u64_le();
    out.reserve(static_cast<std::size_t>(source_count));
    for (uint64_t i = 0; i < source_count; ++i) {
        BinlogGtidSource source;
        source.sid = uuid_from_bytes(cursor.read_buffer(16));
        const auto interval_count = cursor.read_u64_le();
        source.intervals.reserve(static_cast<std::size_t>(interval_count));
        for (uint64_t j = 0; j < interval_count; ++j) {
            BinlogGtidInterval interval;
            interval.start = cursor.read_u64_le();
            const auto exclusive_end = cursor.read_u64_le();
            interval.end = exclusive_end == 0 ? 0 : exclusive_end - 1;
            source.intervals.push_back(interval);
        }
        out.push_back(std::move(source));
    }
    return out;
}

Buffer build_binlog_dump_payload(const BinlogDumpOptions& options) {
    std::vector<uint8_t> payload;
    if (options.use_gtid) {
        const auto encoded_gtids = encode_gtid_set(options.gtid_set);
        payload.reserve(23 + options.filename.size() + encoded_gtids.length());
        append_u8(payload, command_code::BINLOG_DUMP_GTID);
        append_u16_le(payload, options.flags);
        append_u32_le(payload, options.server_id);
        append_u32_le(payload, static_cast<uint32_t>(options.filename.size()));
        append_string(payload, options.filename);
        append_u64_le(payload, options.binlog_position);
        append_u32_le(payload, static_cast<uint32_t>(encoded_gtids.length()));
        append_bytes(payload, encoded_gtids);
    } else {
        if (options.binlog_position > std::numeric_limits<uint32_t>::max()) {
            throw Error("COM_BINLOG_DUMP binlog_position exceeds 32-bit protocol field; use GTID dump for 64-bit positions");
        }
        payload.reserve(11 + options.filename.size());
        append_u8(payload, command_code::BINLOG_DUMP);
        append_u32_le(payload, static_cast<uint32_t>(options.binlog_position));
        append_u16_le(payload, options.flags);
        append_u32_le(payload, options.server_id);
        append_string(payload, options.filename);
    }
    return buffer_from_bytes(payload);
}

std::string strip_at_nul(std::string value) {
    const auto pos = value.find('\0');
    if (pos != std::string::npos) {
        value.resize(pos);
    }
    return value;
}

std::string binlog_event_name(uint8_t event_type) {
    switch (event_type) {
        case constants::binlog_event_type::QUERY: return "QueryEvent";
        case constants::binlog_event_type::ROTATE: return "RotateEvent";
        case constants::binlog_event_type::FORMAT_DESCRIPTION: return "FormatDescriptionEvent";
        case constants::binlog_event_type::XID: return "XidEvent";
        case constants::binlog_event_type::TABLE_MAP: return "TableMapEvent";
        case constants::binlog_event_type::WRITE_ROWS_V1: return "WriteRowsEventV1";
        case constants::binlog_event_type::UPDATE_ROWS_V1: return "UpdateRowsEventV1";
        case constants::binlog_event_type::DELETE_ROWS_V1: return "DeleteRowsEventV1";
        case constants::binlog_event_type::WRITE_ROWS_V2: return "WriteRowsEventV2";
        case constants::binlog_event_type::UPDATE_ROWS_V2: return "UpdateRowsEventV2";
        case constants::binlog_event_type::DELETE_ROWS_V2: return "DeleteRowsEventV2";
        case constants::binlog_event_type::GTID: return "GtidEvent";
        case constants::binlog_event_type::ANONYMOUS_GTID: return "AnonymousGtidEvent";
        case constants::binlog_event_type::PREVIOUS_GTIDS: return "PreviousGtidsEvent";
        default: return "UnknownEvent";
    }
}

struct BinlogTableMapState {
    std::string schema;
    std::string table;
    uint16_t flags = 0;
    std::vector<uint8_t> column_types;
    std::vector<uint16_t> column_metadata;
    Buffer column_null_bitmap;
};

struct BinlogParserState {
    std::unordered_map<uint64_t, BinlogTableMapState> tables;
};

constexpr uint8_t kColumnTimestamp2 = 0x11;
constexpr uint8_t kColumnDatetime2 = 0x12;
constexpr uint8_t kColumnTime2 = 0x13;

bool bitmap_has_bit(const Buffer& bitmap, std::size_t bit) {
    return bit / 8 < bitmap.length() && (bitmap[bit / 8] & (1u << (bit % 8))) != 0;
}

std::size_t bitmap_byte_length(std::size_t bit_count) {
    return (bit_count + 7) / 8;
}

std::size_t bitmap_set_count(const Buffer& bitmap, std::size_t bit_count) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < bit_count; ++i) {
        if (bitmap_has_bit(bitmap, i)) {
            ++count;
        }
    }
    return count;
}

uint16_t parse_table_map_column_metadata(PacketCursor& cursor, uint8_t type) {
    using namespace constants::column_type;
    switch (type) {
        case FLOAT:
        case DOUBLE:
        case BLOB:
        case TINY_BLOB:
        case MEDIUM_BLOB:
        case LONG_BLOB:
        case JSON:
        case GEOMETRY:
        case kColumnTimestamp2:
        case kColumnDatetime2:
        case kColumnTime2:
            return cursor.read_u8();
        case VARCHAR:
        case BIT:
        case NEWDECIMAL:
        case STRING:
        case ENUM:
        case SET:
            return cursor.read_u16_le();
        default:
            return 0;
    }
}

std::vector<uint16_t> parse_table_map_metadata(const std::vector<uint8_t>& column_types, const Buffer& metadata) {
    PacketCursor cursor(metadata);
    std::vector<uint16_t> out;
    out.reserve(column_types.size());
    for (const auto type : column_types) {
        out.push_back(parse_table_map_column_metadata(cursor, type));
    }
    return out;
}

uint64_t read_le_integer(PacketCursor& cursor, std::size_t byte_length) {
    if (byte_length == 0 || byte_length > 8) {
        throw Error("invalid MySQL integer byte length");
    }
    uint64_t value = 0;
    const auto bytes = cursor.read_buffer(byte_length);
    for (std::size_t i = 0; i < byte_length; ++i) {
        value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
}

int64_t sign_extend(uint64_t value, std::size_t bits) {
    const uint64_t sign_bit = uint64_t{1} << (bits - 1);
    if ((value & sign_bit) == 0) {
        return static_cast<int64_t>(value);
    }
    const uint64_t mask = (~uint64_t{0}) << bits;
    return static_cast<int64_t>(value | mask);
}

uint32_t read_be_integer(const std::vector<uint8_t>& bytes, std::size_t& offset, std::size_t length) {
    uint32_t value = 0;
    for (std::size_t i = 0; i < length; ++i) {
        value = (value << 8) | bytes[offset++];
    }
    return value;
}

std::string read_newdecimal_string(PacketCursor& cursor, uint16_t metadata) {
    static constexpr std::array<int, 10> compressed_bytes = {0, 1, 1, 2, 2, 3, 3, 4, 4, 4};
    const int precision = (metadata >> 8) & 0xff;
    const int scale = metadata & 0xff;
    if (precision <= 0 || scale < 0 || scale > precision) {
        throw Error("invalid NEWDECIMAL metadata in binlog row");
    }
    const int intg = precision - scale;
    const int intg0 = intg / 9;
    const int frac0 = scale / 9;
    const int intg0x = intg - intg0 * 9;
    const int frac0x = scale - frac0 * 9;
    const std::size_t byte_count = static_cast<std::size_t>(intg0 * 4 + compressed_bytes[intg0x] +
                                                            frac0 * 4 + compressed_bytes[frac0x]);
    auto bytes = bytes_from_buffer(cursor.read_buffer(byte_count));
    const bool positive = (bytes[0] & 0x80) != 0;
    bytes[0] ^= 0x80;
    if (!positive) {
        for (auto& byte : bytes) {
            byte ^= 0xff;
        }
    }
    std::size_t offset = 0;
    std::ostringstream out;
    if (!positive) {
        out << '-';
    }
    bool wrote_int = false;
    if (intg0x > 0) {
        out << read_be_integer(bytes, offset, static_cast<std::size_t>(compressed_bytes[intg0x]));
        wrote_int = true;
    }
    for (int i = 0; i < intg0; ++i) {
        const auto group = read_be_integer(bytes, offset, 4);
        if (wrote_int) {
            out.width(9);
            out.fill('0');
        }
        out << group;
        wrote_int = true;
    }
    if (!wrote_int) {
        out << '0';
    }
    if (scale > 0) {
        out << '.';
        for (int i = 0; i < frac0; ++i) {
            const auto group = read_be_integer(bytes, offset, 4);
            out.width(9);
            out.fill('0');
            out << group;
        }
        if (frac0x > 0) {
            const auto group = read_be_integer(bytes, offset, static_cast<std::size_t>(compressed_bytes[frac0x]));
            out.width(frac0x);
            out.fill('0');
            out << group;
        }
    }
    return out.str();
}

std::string format_date_from_packed(uint32_t value) {
    const auto day = value & 0x1f;
    const auto month = (value >> 5) & 0x0f;
    const auto year = value >> 9;
    return format_mysql_datetime(static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day), 0, 0, 0, 0, true);
}

std::string format_time_from_seconds(int64_t total_seconds) {
    const bool negative = total_seconds < 0;
    if (negative) {
        total_seconds = -total_seconds;
    }
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds / 60) % 60;
    const auto seconds = total_seconds % 60;
    return std::string(negative ? "-" : "") + std::to_string(hours) + ":" +
           two_digits(static_cast<uint32_t>(minutes)) + ":" + two_digits(static_cast<uint32_t>(seconds));
}

std::size_t fractional_second_storage_bytes(uint8_t decimals) {
    if (decimals == 0) return 0;
    if (decimals <= 2) return 1;
    if (decimals <= 4) return 2;
    return 3;
}

Value read_binlog_row_value(PacketCursor& cursor, uint8_t type, uint16_t metadata) {
    using namespace constants::column_type;
    switch (type) {
        case TINY:
            return static_cast<int64_t>(cursor.read_i8());
        case SHORT:
            return static_cast<int64_t>(cursor.read_i16_le());
        case INT24:
            return sign_extend(read_le_integer(cursor, 3), 24);
        case LONG:
            return static_cast<int64_t>(cursor.read_i32_le());
        case LONGLONG:
            return cursor.read_i64_le();
        case FLOAT:
            return static_cast<double>(cursor.read_float_le());
        case DOUBLE:
            return cursor.read_double_le();
        case YEAR: {
            const auto raw = cursor.read_u8();
            return static_cast<int64_t>(raw == 0 ? 0 : 1900 + raw);
        }
        case DATE:
        case NEWDATE:
            return format_date_from_packed(cursor.read_u24_le());
        case TIMESTAMP:
            return static_cast<uint64_t>(cursor.read_u32_le());
        case TIME:
            return format_time_from_seconds(sign_extend(read_le_integer(cursor, 3), 24));
        case DATETIME: {
            const auto raw = cursor.read_u64_le();
            const auto seconds = raw % 100;
            const auto minutes = (raw / 100) % 100;
            const auto hours = (raw / 10000) % 100;
            const auto day = (raw / 1000000) % 100;
            const auto month = (raw / 100000000) % 100;
            const auto year = raw / 10000000000ULL;
            return format_mysql_datetime(static_cast<uint16_t>(year),
                                         static_cast<uint8_t>(month),
                                         static_cast<uint8_t>(day),
                                         static_cast<uint8_t>(hours),
                                         static_cast<uint8_t>(minutes),
                                         static_cast<uint8_t>(seconds),
                                         0,
                                         false);
        }
        case NEWDECIMAL:
            return read_newdecimal_string(cursor, metadata);
        case VARCHAR: {
            const auto length = metadata > 255 ? cursor.read_u16_le() : cursor.read_u8();
            return PacketCursor::decode_buffer(cursor.read_buffer(length), "utf8");
        }
        case STRING:
        case VAR_STRING: {
            const auto length = metadata > 255 ? cursor.read_u16_le() : cursor.read_u8();
            return PacketCursor::decode_buffer(cursor.read_buffer(length), "utf8");
        }
        case ENUM:
        case SET: {
            const auto length = std::max<uint16_t>(metadata & 0xff, 1);
            return static_cast<uint64_t>(read_le_integer(cursor, std::min<std::size_t>(length, 8)));
        }
        case BIT: {
            const auto bit_count = static_cast<std::size_t>(((metadata >> 8) * 8) + (metadata & 0xff));
            const auto byte_count = bitmap_byte_length(bit_count);
            const auto bytes = cursor.read_buffer(byte_count);
            if (byte_count <= 8) {
                uint64_t value = 0;
                for (std::size_t i = 0; i < byte_count; ++i) {
                    value = (value << 8) | bytes[i];
                }
                return value;
            }
            return bytes;
        }
        case BLOB:
        case TINY_BLOB:
        case MEDIUM_BLOB:
        case LONG_BLOB:
        case JSON:
        case GEOMETRY: {
            const auto length_size = metadata == 0 ? 1 : metadata;
            const auto length = read_le_integer(cursor, length_size);
            return cursor.read_buffer(static_cast<std::size_t>(length));
        }
        case kColumnTimestamp2:
            return cursor.read_buffer(4 + fractional_second_storage_bytes(static_cast<uint8_t>(metadata)));
        case kColumnDatetime2:
            return cursor.read_buffer(5 + fractional_second_storage_bytes(static_cast<uint8_t>(metadata)));
        case kColumnTime2:
            return cursor.read_buffer(3 + fractional_second_storage_bytes(static_cast<uint8_t>(metadata)));
        case NULL_TYPE:
            return std::monostate{};
        default:
            throw Error("unsupported binlog row column type: " + std::to_string(type));
    }
}

std::vector<Value> read_binlog_row_values(PacketCursor& cursor,
                                          const BinlogTableMapState& table_map,
                                          const Buffer& columns_present_bitmap) {
    const auto selected_count = bitmap_set_count(columns_present_bitmap, table_map.column_types.size());
    const auto null_bitmap = cursor.read_buffer(bitmap_byte_length(selected_count));
    std::vector<Value> values(table_map.column_types.size(), std::monostate{});
    std::size_t selected = 0;
    for (std::size_t i = 0; i < table_map.column_types.size(); ++i) {
        if (!bitmap_has_bit(columns_present_bitmap, i)) {
            continue;
        }
        if (bitmap_has_bit(null_bitmap, selected)) {
            values[i] = std::monostate{};
        } else {
            const auto metadata = i < table_map.column_metadata.size() ? table_map.column_metadata[i] : 0;
            values[i] = read_binlog_row_value(cursor, table_map.column_types[i], metadata);
        }
        ++selected;
    }
    return values;
}

void parse_table_map_event_body(BinlogEvent& event, PacketCursor& body_cursor, BinlogParserState* state) {
    event.table_id = read_u48_le(body_cursor);
    event.table_flags = body_cursor.read_u16_le();
    const auto schema_length = body_cursor.read_u8();
    event.schema = body_cursor.read_ascii(schema_length);
    body_cursor.read_u8();
    const auto table_length = body_cursor.read_u8();
    event.table = body_cursor.read_ascii(table_length);
    body_cursor.read_u8();
    const auto column_count = body_cursor.read_lenenc_int().value_or(0);
    event.column_types.reserve(static_cast<std::size_t>(column_count));
    for (uint64_t i = 0; i < column_count; ++i) {
        event.column_types.push_back(body_cursor.read_u8());
    }
    const auto metadata_length = body_cursor.read_lenenc_int().value_or(0);
    event.column_metadata = parse_table_map_metadata(event.column_types,
                                                     body_cursor.read_buffer(static_cast<std::size_t>(metadata_length)));
    event.column_null_bitmap = body_cursor.read_buffer(bitmap_byte_length(static_cast<std::size_t>(column_count)));

    if (state) {
        BinlogTableMapState table_map;
        table_map.schema = event.schema;
        table_map.table = event.table;
        table_map.flags = event.table_flags;
        table_map.column_types = event.column_types;
        table_map.column_metadata = event.column_metadata;
        table_map.column_null_bitmap = event.column_null_bitmap;
        state->tables[event.table_id] = std::move(table_map);
    }
}

bool is_rows_event(uint8_t event_type) {
    return event_type == constants::binlog_event_type::WRITE_ROWS_V1 ||
           event_type == constants::binlog_event_type::UPDATE_ROWS_V1 ||
           event_type == constants::binlog_event_type::DELETE_ROWS_V1 ||
           event_type == constants::binlog_event_type::WRITE_ROWS_V2 ||
           event_type == constants::binlog_event_type::UPDATE_ROWS_V2 ||
           event_type == constants::binlog_event_type::DELETE_ROWS_V2;
}

bool is_update_rows_event(uint8_t event_type) {
    return event_type == constants::binlog_event_type::UPDATE_ROWS_V1 ||
           event_type == constants::binlog_event_type::UPDATE_ROWS_V2;
}

bool is_delete_rows_event(uint8_t event_type) {
    return event_type == constants::binlog_event_type::DELETE_ROWS_V1 ||
           event_type == constants::binlog_event_type::DELETE_ROWS_V2;
}

bool is_rows_event_v2(uint8_t event_type) {
    return event_type == constants::binlog_event_type::WRITE_ROWS_V2 ||
           event_type == constants::binlog_event_type::UPDATE_ROWS_V2 ||
           event_type == constants::binlog_event_type::DELETE_ROWS_V2;
}

void parse_rows_event_body(BinlogEvent& event, PacketCursor& body_cursor, BinlogParserState* state) {
    event.table_id = read_u48_le(body_cursor);
    event.table_flags = body_cursor.read_u16_le();
    if (is_rows_event_v2(event.header.event_type)) {
        const auto extra_data_length = body_cursor.read_u16_le();
        if (extra_data_length > 2) {
            body_cursor.skip(extra_data_length - 2);
        }
    }
    const auto column_count = body_cursor.read_lenenc_int().value_or(0);
    const auto bitmap_length = bitmap_byte_length(static_cast<std::size_t>(column_count));
    event.columns_present_bitmap = body_cursor.read_buffer(bitmap_length);
    if (is_update_rows_event(event.header.event_type)) {
        event.columns_present_bitmap_after = body_cursor.read_buffer(bitmap_length);
    }

    if (!state) {
        return;
    }
    const auto table = state->tables.find(event.table_id);
    if (table == state->tables.end()) {
        return;
    }
    event.schema = table->second.schema;
    event.table = table->second.table;
    event.column_types = table->second.column_types;
    event.column_metadata = table->second.column_metadata;
    event.column_null_bitmap = table->second.column_null_bitmap;
    while (body_cursor.has_more()) {
        BinlogRowChange change;
        const auto before_start = body_cursor.offset();
        if (is_update_rows_event(event.header.event_type) || is_delete_rows_event(event.header.event_type)) {
            change.before = read_binlog_row_values(body_cursor, table->second, event.columns_present_bitmap);
            change.raw_before = body_cursor.payload().slice(before_start, body_cursor.offset());
        }
        if (!is_delete_rows_event(event.header.event_type)) {
            const auto after_start = body_cursor.offset();
            const auto& after_bitmap = is_update_rows_event(event.header.event_type)
                ? event.columns_present_bitmap_after
                : event.columns_present_bitmap;
            change.after = read_binlog_row_values(body_cursor, table->second, after_bitmap);
            change.raw_after = body_cursor.payload().slice(after_start, body_cursor.offset());
        }
        event.row_changes.push_back(std::move(change));
    }
}

BinlogEvent parse_binlog_event_packet_impl(const Buffer& payload, BinlogParserState* state = nullptr) {
    if (payload.length() == 0) {
        throw Error("empty binlog event packet");
    }
    PacketCursor cursor(payload);
    const auto marker_byte = cursor.read_u8();
    if (marker_byte != marker::OK) {
        throw Error("unexpected binlog event packet marker");
    }

    BinlogEvent event;
    event.raw = payload;
    event.header.timestamp = cursor.read_u32_le();
    event.header.event_type = cursor.read_u8();
    event.header.server_id = cursor.read_u32_le();
    event.header.event_size = cursor.read_u32_le();
    event.header.log_position = cursor.read_u32_le();
    event.header.flags = cursor.read_u16_le();
    event.name = binlog_event_name(event.header.event_type);

    event.body = cursor.read_rest_buffer();
    PacketCursor body_cursor(event.body);
    switch (event.header.event_type) {
        case constants::binlog_event_type::QUERY: {
            body_cursor.read_u32_le(); // slave proxy id
            body_cursor.read_u32_le(); // execution time
            const auto schema_length = body_cursor.read_u8();
            body_cursor.read_u16_le(); // error code
            const auto status_vars_length = body_cursor.read_u16_le();
            event.status_vars = body_cursor.read_buffer(status_vars_length);
            event.schema = body_cursor.read_ascii(schema_length);
            if (body_cursor.has_more()) {
                body_cursor.read_u8(); // schema terminator
            }
            event.query = body_cursor.read_ascii(body_cursor.length() - body_cursor.offset());
            break;
        }
        case constants::binlog_event_type::ROTATE: {
            const auto low = body_cursor.read_u32_le();
            const auto high = body_cursor.read_u32_le();
            event.next_position = (static_cast<uint64_t>(high) << 32) | low;
            event.next_binlog = body_cursor.read_ascii(body_cursor.length() - body_cursor.offset());
            break;
        }
        case constants::binlog_event_type::FORMAT_DESCRIPTION: {
            event.binlog_version = body_cursor.read_u16_le();
            event.server_version = strip_at_nul(body_cursor.read_ascii(50));
            event.create_timestamp = body_cursor.read_u32_le();
            event.event_header_length = body_cursor.read_u8();
            event.event_type_header_lengths = body_cursor.read_rest_buffer();
            break;
        }
        case constants::binlog_event_type::XID: {
            event.xid = body_cursor.read_u64_le();
            break;
        }
        case constants::binlog_event_type::TABLE_MAP:
            parse_table_map_event_body(event, body_cursor, state);
            break;
        case constants::binlog_event_type::GTID:
        case constants::binlog_event_type::ANONYMOUS_GTID:
            event.gtid_flags = body_cursor.read_u8();
            event.gtid_sid = uuid_from_bytes(body_cursor.read_buffer(16));
            event.gtid_sequence_number = body_cursor.read_u64_le();
            break;
        case constants::binlog_event_type::PREVIOUS_GTIDS:
            event.previous_gtids = decode_gtid_set(body_cursor.read_rest_buffer());
            break;
        default:
            if (is_rows_event(event.header.event_type)) {
                parse_rows_event_body(event, body_cursor, state);
            }
            break;
    }
    return event;
}

std::string value_to_string(const Value& value) {
    struct Visitor {
        std::string operator()(std::monostate) const { return "NULL"; }
        std::string operator()(bool value) const { return value ? "true" : "false"; }
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
        connect_socket();
        std::error_code ec;
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
        client_flags_ = client_flags;
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
        compression_active_ = options_.compress && ((client_flags & client_flag::COMPRESS) != 0);
        connected_ = true;
        emitter_.emit(event::Connect, connection_info());
    }

    QueryResult query(const std::string& sql) {
        auto results = query_all(sql, {});
        if (results.size() != 1) {
            throw Error("query returned multiple result sets; use query_all");
        }
        return std::move(results.front());
    }

    QueryResult query(const std::string& sql, const QueryAttributes& attributes) {
        auto results = query_all(sql, attributes);
        if (results.size() != 1) {
            throw Error("query returned multiple result sets; use query_all");
        }
        return std::move(results.front());
    }

    QueryResult query(const QueryOptions& options) {
        return with_operation_timeout(options.timeout_ms, [this, &options] {
            return query(options.sql, options.attributes);
        });
    }

    std::vector<QueryResult> query_all(const std::string& sql) {
        return query_all(sql, {});
    }

    std::vector<QueryResult> query_all(const std::string& sql, const QueryAttributes& attributes) {
        ensure_connected();
        write_packet(build_query_payload(sql, attributes, client_encoding_, client_flags_), 0);
        return read_query_results(false);
    }

    std::vector<QueryResult> query_all(const QueryOptions& options) {
        return with_operation_timeout(options.timeout_ms, [this, &options] {
            return query_all(options.sql, options.attributes);
        });
    }

    PreparedStatement prepare(const std::string& sql) {
        ensure_connected();
        write_packet(build_stmt_prepare_payload(sql, client_encoding_), 0);
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

    PreparedStatement prepare(const std::string& sql, CommandOptions options) {
        return with_operation_timeout(options.timeout_ms, [this, &sql] {
            return prepare(sql);
        });
    }

    PreparedStatement prepare_cached(const std::string& sql) {
        const auto cached = statement_cache_.find(sql);
        if (cached != statement_cache_.end()) {
            touch_lru_key(sql);
            return cached->second;
        }
        auto statement = prepare(sql);
        if (options_.max_prepared_statements == 0) {
            return statement;
        }
        while (statement_cache_.size() >= options_.max_prepared_statements && !statement_lru_.empty()) {
            const auto evict_key = statement_lru_.front();
            statement_lru_.pop_front();
            const auto evict = statement_cache_.find(evict_key);
            if (evict != statement_cache_.end()) {
                const auto evict_statement = evict->second;
                statement_cache_.erase(evict);
                close_statement(evict_statement);
            }
        }
        statement_lru_.push_back(sql);
        statement_cache_[sql] = statement;
        return statement;
    }

    void touch_lru_key(const std::string& sql) {
        erase_lru_key(sql);
        statement_lru_.push_back(sql);
    }

    void erase_lru_key(const std::string& sql) {
        statement_lru_.erase(std::remove(statement_lru_.begin(), statement_lru_.end(), sql), statement_lru_.end());
    }

    QueryResult execute(const PreparedStatement& statement, const std::vector<Value>& values) {
        auto results = execute_all(statement, values, QueryAttributes{});
        if (results.size() != 1) {
            throw Error("execute returned multiple result sets; use execute_all");
        }
        return std::move(results.front());
    }

    QueryResult execute(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes) {
        auto results = execute_all(statement, values, attributes);
        if (results.size() != 1) {
            throw Error("execute returned multiple result sets; use execute_all");
        }
        return std::move(results.front());
    }

    QueryResult execute(const PreparedStatement& statement,
                        const std::vector<Value>& values,
                        const QueryAttributes& attributes,
                        CommandOptions options) {
        return with_operation_timeout(options.timeout_ms, [this, &statement, &values, &attributes] {
            return execute(statement, values, attributes);
        });
    }

    std::vector<QueryResult> execute_all(const PreparedStatement& statement, const std::vector<Value>& values) {
        return execute_all(statement, values, QueryAttributes{});
    }

    std::vector<QueryResult> execute_all(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes) {
        ensure_connected();
        if (statement.id == 0) {
            throw Error("cannot execute an empty prepared statement");
        }
        write_packet(build_stmt_execute_payload(statement.id,
                                                statement.parameters.size(),
                                                values,
                                                attributes,
                                                client_encoding_,
                                                client_flags_,
                                                CursorType::None), 0);
        return read_query_results(true);
    }

    std::vector<QueryResult> execute_all(const PreparedStatement& statement,
                                         const std::vector<Value>& values,
                                         const QueryAttributes& attributes,
                                         CommandOptions options) {
        return with_operation_timeout(options.timeout_ms, [this, &statement, &values, &attributes] {
            return execute_all(statement, values, attributes);
        });
    }

    QueryResult execute(const std::string& sql, const std::vector<Value>& values) {
        auto statement = prepare_cached(sql);
        return execute(statement, values);
    }

    QueryResult execute(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes) {
        auto statement = prepare_cached(sql);
        return execute(statement, values, attributes);
    }

    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values) {
        auto statement = prepare_cached(sql);
        return execute_all(statement, values);
    }

    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes) {
        auto statement = prepare_cached(sql);
        return execute_all(statement, values, attributes);
    }

    QueryResult execute(const ExecuteOptions& options) {
        return with_operation_timeout(options.timeout_ms, [this, &options] {
            return execute(options.sql, options.values, options.attributes);
        });
    }

    std::vector<QueryResult> execute_all(const ExecuteOptions& options) {
        return with_operation_timeout(options.timeout_ms, [this, &options] {
            return execute_all(options.sql, options.values, options.attributes);
        });
    }

    StatementCursor execute_cursor(const PreparedStatement& statement,
                                   const std::vector<Value>& values,
                                   const QueryAttributes& attributes,
                                   CursorType cursor_type) {
        ensure_connected();
        if (statement.id == 0) {
            throw Error("cannot execute an empty prepared statement cursor");
        }
        if (cursor_type == CursorType::None) {
            cursor_type = CursorType::ReadOnly;
        }
        write_packet(build_stmt_execute_payload(statement.id,
                                                statement.parameters.size(),
                                                values,
                                                attributes,
                                                client_encoding_,
                                                client_flags_,
                                                cursor_type), 0);
        auto result = read_query_result(true);
        StatementCursor cursor;
        cursor.statement = statement;
        cursor.fields = result.fields;
        cursor.server_status = result.ok.server_status;
        return cursor;
    }

    StatementCursor execute_cursor(const std::string& sql,
                                   const std::vector<Value>& values,
                                   const QueryAttributes& attributes,
                                   CursorType cursor_type) {
        auto statement = prepare_cached(sql);
        return execute_cursor(statement, values, attributes, cursor_type);
    }

    QueryResult fetch(StatementCursor& cursor, uint32_t row_count) {
        ensure_connected();
        if (cursor.statement.id == 0) {
            throw Error("cannot fetch from an empty prepared statement cursor");
        }
        if (!cursor.open()) {
            QueryResult result;
            result.fields = cursor.fields;
            result.ok.server_status = cursor.server_status;
            return result;
        }
        std::vector<uint8_t> payload;
        payload.reserve(9);
        append_u8(payload, command_code::STMT_FETCH);
        append_u32_le(payload, cursor.statement.id);
        append_u32_le(payload, row_count == 0 ? 1 : row_count);
        write_packet(buffer_from_bytes(payload), 0);

        QueryResult result;
        result.fields = cursor.fields;
        while (true) {
            const auto row_frame = read_packet();
            if (is_err_packet(row_frame.payload)) {
                throw parse_error_packet(row_frame.payload);
            }
            if (is_eof_packet(row_frame.payload)) {
                result.ok = parse_resultset_end_packet(row_frame.payload, server_capability_flags_);
                cursor.server_status = result.ok.server_status;
                break;
            }
            result.rows.push_back(parse_binary_row(row_frame.payload, result.fields, options_));
        }
        return result;
    }

    QueryResult fetch(StatementCursor& cursor, uint32_t row_count, CommandOptions options) {
        return with_operation_timeout(options.timeout_ms, [this, &cursor, row_count] {
            return fetch(cursor, row_count);
        });
    }

    OkPacket register_slave(const RegisterSlaveOptions& options) {
        ensure_connected();
        write_packet(build_register_slave_payload(options, client_encoding_), 0);
        const auto frame = read_packet();
        if (is_err_packet(frame.payload)) {
            throw parse_error_packet(frame.payload);
        }
        if (!is_ok_packet(frame.payload)) {
            throw Error("unexpected packet for COM_REGISTER_SLAVE");
        }
        return parse_ok_packet(frame.payload, server_capability_flags_);
    }

    std::vector<BinlogEvent> binlog_dump(const BinlogDumpOptions& options) {
        if ((options.flags & kBinlogDumpNonBlock) == 0 && options.max_events == 0) {
            throw Error("blocking binlog_dump requires max_events to avoid an unbounded vector read; use binlog_dump_each for a callback-controlled stream");
        }
        std::vector<BinlogEvent> events;
        events.reserve(options.max_events == 0 ? 0 : options.max_events);
        binlog_dump_each(options, [&](const BinlogEvent& event) {
            events.push_back(event);
            return true;
        });
        return events;
    }

    std::size_t binlog_dump_each(const BinlogDumpOptions& options, const BinlogEventCallback& callback) {
        ensure_connected();
        if (!callback) {
            throw Error("binlog_dump_each requires a callback");
        }
        write_packet(build_binlog_dump_payload(options), 0);
        BinlogParserState parser_state;
        std::size_t count = 0;
        while (true) {
            const auto frame = read_packet();
            if (is_err_packet(frame.payload)) {
                throw parse_error_packet(frame.payload);
            }
            if (is_eof_packet(frame.payload)) {
                break;
            }
            const auto event = parse_binlog_event_packet_impl(frame.payload, &parser_state);
            ++count;
            const bool keep_going = callback(event);
            if (!keep_going || (options.max_events != 0 && count >= options.max_events)) {
                close_transport();
                connected_ = false;
                tls_active_ = false;
                compression_active_ = false;
                break;
            }
        }
        return count;
    }

    void close_statement(const PreparedStatement& statement) {
        ensure_connected();
        if (statement.id == 0) {
            return;
        }
        for (auto it = statement_cache_.begin(); it != statement_cache_.end();) {
            if (it->second.id == statement.id) {
                erase_lru_key(it->first);
                it = statement_cache_.erase(it);
            } else {
                ++it;
            }
        }
        std::vector<uint8_t> payload;
        payload.reserve(5);
        append_u8(payload, command_code::STMT_CLOSE);
        append_u32_le(payload, statement.id);
        write_packet(buffer_from_bytes(payload), 0);
    }

    void close_statement(const std::string& sql) {
        const auto it = statement_cache_.find(sql);
        if (it == statement_cache_.end()) {
            return;
        }
        const auto statement = it->second;
        erase_lru_key(sql);
        statement_cache_.erase(it);
        close_statement(statement);
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
        statement_cache_.clear();
        statement_lru_.clear();
    }

    void change_user(ConnectionOptions options) {
        ensure_connected();
        options.host = options_.host;
        options.port = options_.port;
        options.ssl = options_.ssl;
        options.compress = options_.compress;
        if (options.charset.empty()) {
            options.charset = options_.charset;
            options.charset_number = options_.charset_number;
        } else {
            options.charset_number = charset_number_for_name(options.charset, options.charset_number);
        }
        if (options.user.empty()) {
            options.user = options_.user;
        }
        if (options.password.empty()) {
            options.password = options_.password;
        }
        if (options.database.empty()) {
            options.database = options_.database;
        }
        if (options.connect_attributes.empty()) {
            options.connect_attributes = options_.connect_attributes;
        }
        const auto scramble = handshake_.scramble();
        const auto plugin = choose_auth_plugin(handshake_.auth_plugin_name, tls_active_, options.enable_cleartext_plugin || options_.enable_cleartext_plugin);
        const auto token = calculate_auth_token(plugin, options.password, scramble);
        write_packet(build_change_user_payload(options, client_flags_, plugin, token), 0);
        handle_auth_result(plugin, scramble, 1);
        statement_cache_.clear();
        statement_lru_.clear();
        options_.user = std::move(options.user);
        options_.password = std::move(options.password);
        options_.database = std::move(options.database);
        options_.charset = std::move(options.charset);
        options_.charset_number = options.charset_number;
        options_.connect_attributes = std::move(options.connect_attributes);
        options_.enable_cleartext_plugin = options.enable_cleartext_plugin || options_.enable_cleartext_plugin;
        client_encoding_ = charset_encoding(options_.charset_number);
    }

    void end() noexcept {
        if (!connected_ && !transport_is_open()) {
            return;
        }
        const bool was_connected = connected_;
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
        compression_active_ = false;
        try {
            if (was_connected) {
                emitter_.emit(event::End);
            }
            emitter_.emit(event::Close);
        } catch (...) {
        }
    }

    bool connected() const noexcept { return connected_; }
    bool encrypted() const noexcept { return tls_active_; }
    bool compressed() const noexcept { return compression_active_; }
    bool paused() const noexcept { return paused_; }
    const ConnectionOptions& options() const noexcept { return options_; }
    const std::string& server_version() const noexcept { return server_version_; }
    uint32_t connection_id() const noexcept { return connection_id_; }
    uint32_t server_capability_flags() const noexcept { return server_capability_flags_; }
    events::EventEmitter& emitter() noexcept { return emitter_; }

    template <typename Fn>
    std::invoke_result_t<Fn> traced(const std::string& operation, const std::string& sql, Fn&& fn) {
        using Result = std::invoke_result_t<Fn>;
        const bool has_trace_listener = emitter_.listenerCount(event::Trace) > 0;
        const auto start = std::chrono::steady_clock::now();
        if (has_trace_listener) {
            emit_trace(operation, "start", sql, start, {});
        }
        try {
            if constexpr (std::is_void_v<Result>) {
                std::forward<Fn>(fn)();
                if (has_trace_listener) {
                    emit_trace(operation, "success", sql, start, {});
                }
            } else {
                auto result = std::forward<Fn>(fn)();
                if (has_trace_listener) {
                    emit_trace(operation, "success", sql, start, {});
                }
                return result;
            }
        } catch (const std::exception& error) {
            if (has_trace_listener) {
                emit_trace(operation, "error", sql, start, error.what());
            }
            throw;
        } catch (...) {
            if (has_trace_listener) {
                emit_trace(operation, "error", sql, start, "unknown exception");
            }
            throw;
        }
    }

    ConnectionInfo connection_info() const {
        return ConnectionInfo{
            .connection_id = connection_id_,
            .server_version = server_version_,
            .server_capability_flags = server_capability_flags_,
            .encrypted = tls_active_,
        };
    }

    void emit_error(const Error& error) {
        if (emitter_.listenerCount(event::Error_) > 0) {
            emitter_.emit(event::Error_, error);
        }
    }

    void emit_trace(const std::string& operation,
                    const std::string& phase,
                    const std::string& sql,
                    std::chrono::steady_clock::time_point start,
                    const std::string& error) {
        if (emitter_.listenerCount(event::Trace) == 0) {
            return;
        }
        TraceEvent event;
        event.operation = operation;
        event.phase = phase;
        event.sql = sql;
        event.database = options_.database;
        event.user = options_.user;
        event.server_address = options_.host;
        event.server_port = options_.port;
        event.duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        event.error = error;
        emitter_.emit(event::Trace, event);
    }

    void pause() noexcept { paused_ = true; }
    void resume() noexcept { paused_ = false; }

private:
    template <typename Fn>
    std::invoke_result_t<Fn> with_operation_timeout(uint32_t timeout_ms, Fn&& fn) {
        const auto previous = operation_timeout_ms_;
        operation_timeout_ms_ = timeout_ms;
        try {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
                std::forward<Fn>(fn)();
                operation_timeout_ms_ = previous;
            } else {
                auto result = std::forward<Fn>(fn)();
                operation_timeout_ms_ = previous;
                return result;
            }
        } catch (...) {
            operation_timeout_ms_ = previous;
            throw;
        }
    }

    void abort_transport_for_timeout() noexcept {
        std::error_code ignored;
        if (tls_stream_) {
            tls_stream_->close(ignored);
        }
        socket_.close(ignored);
        connected_ = false;
        tls_active_ = false;
        compression_active_ = false;
    }

    std::unique_ptr<io::Timer> arm_operation_timer(bool& timed_out, bool& completed) {
        if (operation_timeout_ms_ == 0) {
            return nullptr;
        }
        auto timer = std::make_unique<io::Timer>(ctx_);
        timer->expiresAfter(std::chrono::milliseconds(operation_timeout_ms_));
        timer->asyncWait([this, &timed_out, &completed](std::error_code error) {
            if (!error && !completed) {
                timed_out = true;
                abort_transport_for_timeout();
            }
        });
        return timer;
    }

    void ensure_connected() {
        if (!connected_) {
            traced("connect", "", [this] { connect(); });
        }
    }

    void connect_socket() {
        ctx_.restart();
        std::error_code ec;
        bool completed = false;
        bool timed_out = false;
        io::Timer timer(ctx_);

        socket_.asyncConnect(options_.host, options_.port, [&](std::error_code error) {
            completed = true;
            ec = error;
            if (options_.connect_timeout_ms != 0) {
                timer.cancel();
            }
        });

        if (options_.connect_timeout_ms != 0) {
            timer.expiresAfter(std::chrono::milliseconds(options_.connect_timeout_ms));
            timer.asyncWait([&](std::error_code error) {
                if (!error && !completed) {
                    timed_out = true;
                    std::error_code ignored;
                    socket_.close(ignored);
                }
            });
        }

        ctx_.run();
        if (timed_out) {
            throw Error("connect timed out after " + std::to_string(options_.connect_timeout_ms) + "ms");
        }
        if (ec) {
            throw Error("connect failed: " + ec.message());
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
            if (!options_.ssl.profile.empty()) {
                for (const auto& ca_pem : ssl_profile_ca_pems(options_.ssl.profile)) {
                    tls_context.addCertificateAuthorityPem(ca_pem);
                }
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
            bool completed = false;
            bool timed_out = false;
            auto timer = arm_operation_timer(timed_out, completed);
            const auto remaining = length - offset;
            if (tls_stream_) {
                tls_stream_->asyncReadSome(data.data() + offset, remaining, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                    completed = true;
                    if (timer) {
                        std::error_code ignored;
                        timer->cancel(ignored);
                    }
                });
            } else {
                socket_.asyncRead(data.data() + offset, remaining, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                    completed = true;
                    if (timer) {
                        std::error_code ignored;
                        timer->cancel(ignored);
                    }
                });
            }
            ctx_.run();
            if (timed_out) {
                throw Error("mysql2 operation timed out after " + std::to_string(operation_timeout_ms_) + "ms");
            }
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
            bool completed = false;
            bool timed_out = false;
            auto timer = arm_operation_timer(timed_out, completed);
            if (tls_stream_) {
                tls_stream_->asyncWrite(data + offset, length - offset, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                    completed = true;
                    if (timer) {
                        std::error_code ignored;
                        timer->cancel(ignored);
                    }
                });
            } else {
                socket_.asyncWrite(data + offset, length - offset, [&](std::error_code error, std::size_t n) {
                    ec = error;
                    bytes = n;
                    completed = true;
                    if (timer) {
                        std::error_code ignored;
                        timer->cancel(ignored);
                    }
                });
            }
            ctx_.run();
            if (timed_out) {
                throw Error("mysql2 operation timed out after " + std::to_string(operation_timeout_ms_) + "ms");
            }
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
        if (compression_active_) {
            return read_compressed_packet();
        }
        return read_uncompressed_packet();
    }

    PacketFrame read_uncompressed_packet() {
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
        if (compression_active_ && sequence_id == 0) {
            compressed_sequence_id_ = 0;
            compressed_plain_buffer_.clear();
            compressed_plain_offset_ = 0;
        }

        std::vector<uint8_t> packets;
        std::size_t offset = 0;
        bool wrote_full_packet = false;
        do {
            const auto remaining = payload.length() - offset;
            const auto chunk_length = std::min<std::size_t>(remaining, kMaxPacketPayloadLength);
            append_u24_le(packets, static_cast<uint32_t>(chunk_length));
            append_u8(packets, sequence_id++);
            if (chunk_length > 0) {
                packets.insert(packets.end(), payload.data() + offset, payload.data() + offset + chunk_length);
            }
            offset += chunk_length;
            wrote_full_packet = chunk_length == kMaxPacketPayloadLength;
        } while (offset < payload.length());

        if (wrote_full_packet) {
            append_u24_le(packets, 0);
            append_u8(packets, sequence_id);
        }

        if (compression_active_) {
            write_compressed_packets(packets);
        } else if (!packets.empty()) {
            write_all(packets.data(), packets.size());
        }
    }

    PacketFrame read_compressed_packet() {
        std::vector<Buffer> parts;
        uint8_t first_sequence = 0;
        bool first = true;
        while (true) {
            const auto frame = read_normal_packet_from_compressed_stream();
            if (first) {
                first_sequence = frame.sequence_id;
                first = false;
            }
            parts.push_back(std::move(frame.payload));
            if (parts.back().length() < kMaxPacketPayloadLength) {
                break;
            }
        }
        return {first_sequence, Buffer::concat(parts)};
    }

    PacketFrame read_normal_packet_from_compressed_stream() {
        while (compressed_plain_buffer_.size() - compressed_plain_offset_ < kPacketHeaderLength) {
            append_next_compressed_payload();
        }

        const auto* header = compressed_plain_buffer_.data() + compressed_plain_offset_;
        const uint32_t length = static_cast<uint32_t>(header[0] | (header[1] << 8) | (header[2] << 16));
        const uint8_t sequence = header[3];
        while (compressed_plain_buffer_.size() - compressed_plain_offset_ < kPacketHeaderLength + length) {
            append_next_compressed_payload();
        }

        Buffer payload;
        if (length > 0) {
            payload = Buffer::from(compressed_plain_buffer_.data() + compressed_plain_offset_ + kPacketHeaderLength, length);
        }
        compressed_plain_offset_ += kPacketHeaderLength + length;
        if (compressed_plain_offset_ > 0 && compressed_plain_offset_ * 2 >= compressed_plain_buffer_.size()) {
            compressed_plain_buffer_.erase(compressed_plain_buffer_.begin(),
                                           compressed_plain_buffer_.begin() + static_cast<std::ptrdiff_t>(compressed_plain_offset_));
            compressed_plain_offset_ = 0;
        }
        return {sequence, std::move(payload)};
    }

    void append_next_compressed_payload() {
        const auto header = read_exact(kCompressedPacketHeaderLength);
        const uint32_t compressed_length = static_cast<uint32_t>(header[0] | (header[1] << 8) | (header[2] << 16));
        const uint32_t uncompressed_length = static_cast<uint32_t>(header[4] | (header[5] << 8) | (header[6] << 16));
        const auto bytes = compressed_length > 0 ? read_exact(compressed_length) : std::vector<uint8_t>{};
        Buffer payload = buffer_from_bytes(bytes);
        if (uncompressed_length != 0) {
            payload = zlib::inflateSync(payload, zlib::Options{});
            if (payload.length() != uncompressed_length) {
                throw Error("malformed compressed MySQL packet: uncompressed length mismatch");
            }
        }
        compressed_plain_buffer_.insert(compressed_plain_buffer_.end(),
                                        payload.data(),
                                        payload.data() + payload.length());
    }

    void write_compressed_packets(const std::vector<uint8_t>& packets) {
        std::size_t offset = 0;
        do {
            const auto remaining = packets.size() - offset;
            const auto chunk_length = std::min<std::size_t>(remaining, kMaxCompressedPayloadInputLength);
            const auto chunk = Buffer::from(packets.data() + offset, chunk_length);
            const auto compressed = zlib::deflateSync(chunk, zlib::Options{});
            const bool use_compressed = compressed.length() < chunk_length;
            const Buffer& body = use_compressed ? compressed : chunk;

            std::vector<uint8_t> header;
            header.reserve(kCompressedPacketHeaderLength);
            append_u24_le(header, static_cast<uint32_t>(body.length()));
            append_u8(header, compressed_sequence_id_++);
            append_u24_le(header, use_compressed ? static_cast<uint32_t>(chunk_length) : 0);
            write_all(header.data(), header.size());
            if (body.length() > 0) {
                write_all(body.data(), body.length());
            }
            offset += chunk_length;
        } while (offset < packets.size());
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
            const bool has_more = (result.ok.server_status & server_status_flag::MORE_RESULTS_EXISTS) != 0;
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
            return handle_local_infile_request(first);
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
        result.ok = parse_resultset_end_packet(fields_end.payload, server_capability_flags_);
        if ((result.ok.server_status & server_status_flag::CURSOR_EXISTS) != 0) {
            return result;
        }

        while (true) {
            const auto row_frame = read_packet();
            if (is_err_packet(row_frame.payload)) {
                throw parse_error_packet(row_frame.payload);
            }
            if (is_eof_packet(row_frame.payload)) {
                result.ok = parse_resultset_end_packet(row_frame.payload, server_capability_flags_);
                break;
            }
            result.rows.push_back(binary_rows
                ? parse_binary_row(row_frame.payload, result.fields, options_)
                : parse_text_row(row_frame.payload, result.fields, options_));
        }
        return result;
    }

    QueryResult handle_local_infile_request(const PacketFrame& request) {
        if (!options_.local_infile_handler) {
            write_packet(Buffer{}, static_cast<uint8_t>(request.sequence_id + 1));
            throw Error("LOCAL INFILE request received but no local_infile_handler is configured");
        }

        const auto path = bytes_to_ascii(request.payload, 1, request.payload.length());
        uint8_t sequence = static_cast<uint8_t>(request.sequence_id + 1);
        try {
            const auto chunks = options_.local_infile_handler(path);
            for (const auto& chunk : chunks) {
                if (chunk.length() == 0) {
                    continue;
                }
                write_packet(chunk, sequence);
                sequence = static_cast<uint8_t>(sequence + normal_packet_count_for_payload(chunk.length()));
            }
            write_packet(Buffer{}, sequence);
        } catch (...) {
            try {
                write_packet(Buffer{}, sequence);
            } catch (...) {
            }
            throw;
        }

        const auto final = read_packet();
        if (is_err_packet(final.payload)) {
            throw parse_error_packet(final.payload);
        }
        if (!is_ok_packet(final.payload)) {
            throw Error("expected OK packet after LOCAL INFILE upload");
        }
        QueryResult result;
        result.ok = parse_ok_packet(final.payload, server_capability_flags_);
        return result;
    }

    ConnectionOptions options_;
    events::EventEmitter emitter_;
    EventContext ctx_;
    io::TcpSocket socket_;
    std::unique_ptr<io::TlsContext> tls_context_;
    std::unique_ptr<io::TlsStream> tls_stream_;
    bool connected_ = false;
    bool tls_active_ = false;
    bool compression_active_ = false;
    bool paused_ = false;
    Handshake handshake_;
    std::string server_version_;
    uint32_t connection_id_ = 0;
    uint32_t server_capability_flags_ = 0;
    uint32_t client_flags_ = 0;
    uint8_t compressed_sequence_id_ = 0;
    std::vector<uint8_t> compressed_plain_buffer_;
    std::size_t compressed_plain_offset_ = 0;
    std::unordered_map<std::string, PreparedStatement> statement_cache_;
    std::deque<std::string> statement_lru_;
    std::string client_encoding_ = "utf8";
    uint32_t operation_timeout_ms_ = 0;
};

Error::Error(const std::string& message) : polycpp::Error(message) {}

Error::Error(uint16_t code, std::string sql_state, std::string message)
    : polycpp::Error(make_error_message(code, sql_state, message)), code_(code), sql_state_(std::move(sql_state)) {}

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

bool StatementCursor::open() const noexcept {
    return statement.id != 0 && (server_status & server_status_flag::CURSOR_EXISTS) != 0 &&
           (server_status & server_status_flag::LAST_ROW_SENT) == 0;
}

JsonValue value_to_json(const Value& value) {
    return std::visit([](const auto& item) -> JsonValue {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return JsonValue(nullptr);
        } else if constexpr (std::is_same_v<T, bool>) {
            return JsonValue(item);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            constexpr int64_t max_safe = 9007199254740991LL;
            constexpr int64_t min_safe = -9007199254740991LL;
            if (item > max_safe || item < min_safe) {
                return JsonValue(std::to_string(item));
            }
            return JsonValue(static_cast<double>(item));
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            constexpr uint64_t max_safe = 9007199254740991ULL;
            if (item > max_safe) {
                return JsonValue(std::to_string(item));
            }
            return JsonValue(static_cast<double>(item));
        } else if constexpr (std::is_same_v<T, double>) {
            return JsonValue(item);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return JsonValue(item);
        } else if constexpr (std::is_same_v<T, Buffer>) {
            return item.toJSON();
        } else if constexpr (std::is_same_v<T, RawSql>) {
            return JsonValue(item.sql);
        }
    }, value);
}

JsonObject Row::to_json_object(const std::vector<Field>& fields) const {
    JsonObject object;
    const auto count = std::min(fields.size(), values.size());
    for (std::size_t i = 0; i < count; ++i) {
        object[fields[i].name] = value_to_json(values[i]);
    }
    return object;
}

std::string row_to_json_line(const Row& row, const std::vector<Field>& fields) {
    return JSON::stringify(JsonValue(row.to_json_object(fields))) + "\n";
}

RowStream::RowStream() = default;

RowStream::RowStream(std::vector<Field> fields, std::vector<Row> rows)
    : fields_(std::move(fields)), rows_(std::move(rows)) {}

bool RowStream::empty() const noexcept { return offset_ >= rows_.size(); }

std::size_t RowStream::size() const noexcept { return rows_.size(); }

const std::vector<Field>& RowStream::fields() const noexcept { return fields_; }

const std::vector<Row>& RowStream::rows() const noexcept { return rows_; }

std::optional<Row> RowStream::read() {
    if (offset_ >= rows_.size()) {
        return std::nullopt;
    }
    return rows_[offset_++];
}

std::vector<Row> RowStream::to_vector() const { return rows_; }

std::vector<Buffer> RowStream::to_json_line_buffers() const {
    std::vector<Buffer> chunks;
    chunks.reserve(rows_.size());
    for (const auto& row : rows_) {
        chunks.push_back(Buffer::from(row_to_json_line(row, fields_)));
    }
    return chunks;
}

JsonValue QueryResult::to_json() const {
    JsonObject object;
    JsonArray rows_json;
    rows_json.reserve(rows.size());
    for (const auto& row : rows) {
        rows_json.emplace_back(row.to_json_object(fields));
    }
    JsonObject ok_json;
    ok_json["affectedRows"] = JsonValue(static_cast<double>(ok.affected_rows));
    ok_json["insertId"] = JsonValue(static_cast<double>(ok.insert_id));
    ok_json["warningCount"] = JsonValue(static_cast<double>(ok.warning_count));
    ok_json["changedRows"] = JsonValue(static_cast<double>(ok.changed_rows));
    ok_json["info"] = JsonValue(ok.info);
    object["rows"] = JsonValue(std::move(rows_json));
    object["ok"] = JsonValue(std::move(ok_json));
    return JsonValue(std::move(object));
}

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

std::string escape(bool value) { return value ? "true" : "false"; }

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

std::string percent_decode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex(value[i + 1]);
            const int lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

bool parse_bool_option(const std::string& value) {
    std::string lower;
    lower.reserve(value.size());
    for (char ch : value) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

ConnectionOptions parse_connection_uri(const std::string& uri) {
    url::URL parsed(uri);
    ConnectionOptions options;
    if (parsed.protocol == "mysqls:") {
        options.ssl.enabled = true;
    } else if (!parsed.protocol.empty() && parsed.protocol != "mysql:") {
        throw Error("unsupported mysql2 connection URI scheme: " + parsed.protocol);
    }
    if (!parsed.hostname.empty()) {
        options.host = percent_decode(parsed.hostname);
        if (options.host.size() >= 2 && options.host.front() == '[' && options.host.back() == ']') {
            options.host = options.host.substr(1, options.host.size() - 2);
        }
    }
    if (!parsed.port.empty()) {
        const auto port = std::stoul(parsed.port);
        if (port > 65535) {
            throw Error("mysql2 connection URI port is out of range");
        }
        options.port = static_cast<uint16_t>(port);
    }
    if (!parsed.username.empty()) {
        options.user = percent_decode(parsed.username);
    }
    if (!parsed.password.empty()) {
        options.password = percent_decode(parsed.password);
    }
    if (!parsed.pathname.empty() && parsed.pathname != "/") {
        options.database = percent_decode(parsed.pathname.front() == '/' ? std::string_view(parsed.pathname).substr(1) : std::string_view(parsed.pathname));
    }
    for (const auto& [key, value] : parsed.searchParams.entries()) {
        if (key == "charset") options.charset = value;
        else if (key == "charsetNumber") options.charset_number = static_cast<uint16_t>(std::stoul(value));
        else if (key == "connectTimeout" || key == "connect_timeout_ms") options.connect_timeout_ms = static_cast<uint32_t>(std::stoul(value));
        else if (key == "maxPreparedStatements" || key == "max_prepared_statements") options.max_prepared_statements = static_cast<std::size_t>(std::stoull(value));
        else if (key == "multipleStatements" || key == "multiple_statements") options.multiple_statements = parse_bool_option(value);
        else if (key == "supportBigNumbers" || key == "support_big_numbers") options.support_big_numbers = parse_bool_option(value);
        else if (key == "bigNumberStrings" || key == "big_number_strings") options.big_number_strings = parse_bool_option(value);
        else if (key == "decimalNumbers" || key == "decimal_numbers") options.decimal_numbers = parse_bool_option(value);
        else if (key == "enableKeepAlive" || key == "enable_keep_alive") options.enable_keep_alive = parse_bool_option(value);
        else if (key == "enableCleartextPlugin" || key == "enable_cleartext_plugin") options.enable_cleartext_plugin = parse_bool_option(value);
        else if (key == "compress") options.compress = parse_bool_option(value);
        else if (key == "ssl") options.ssl.enabled = parse_bool_option(value);
        else if (key == "sslProfile" || key == "ssl.profile") {
            options.ssl.enabled = true;
            options.ssl.profile = value;
        }
        else if (key == "rejectUnauthorized" || key == "ssl.rejectUnauthorized") options.ssl.reject_unauthorized = parse_bool_option(value);
        else if (key == "verifyIdentity" || key == "ssl.verifyIdentity") options.ssl.verify_identity = parse_bool_option(value);
        else if (key == "ssl.ca" || key == "ssl_ca_file") options.ssl.ca_file = value;
        else if (key == "ssl.cert" || key == "ssl_cert_file") options.ssl.cert_file = value;
        else if (key == "ssl.key" || key == "ssl_key_file") options.ssl.key_file = value;
    }
    options.charset_number = charset_number_for_name(options.charset, options.charset_number);
    return options;
}

uint16_t get_charset_number(const std::string& charset) {
    return charset_number_for_name(charset, 224);
}

std::string get_charset_encoding(uint16_t charset_number) {
    return charset_encoding(charset_number);
}

std::vector<std::string> ssl_profile_names() {
    return {"Amazon RDS"};
}

std::vector<std::string> ssl_profile_ca_pems(const std::string& profile) {
    if (profile.empty()) {
        return {};
    }
    if (profile != "Amazon RDS") {
        throw Error("unsupported mysql2 SSL profile: " + profile);
    }
    const auto& certs = detail::aws_rds_ca_certificates();
    return std::vector<std::string>(certs.begin(), certs.end());
}

void set_max_parser_cache(std::size_t max) noexcept {
    g_parser_cache_max.store(max, std::memory_order_relaxed);
}

std::size_t max_parser_cache() noexcept {
    return g_parser_cache_max.load(std::memory_order_relaxed);
}

void clear_parser_cache() noexcept {
    // JavaScript parser generation caches do not exist in this static C++ parser.
}

std::vector<BinlogGtidSource> parse_gtid_set(const std::string& gtid_set) {
    return parse_gtid_set_text(gtid_set);
}

BinlogEvent parse_binlog_event_packet(const Buffer& payload) {
    return parse_binlog_event_packet_impl(payload);
}

class BinlogParser::Impl {
public:
    BinlogParserState state;
};

BinlogParser::BinlogParser() : impl_(std::make_unique<Impl>()) {}

BinlogParser::~BinlogParser() = default;

BinlogParser::BinlogParser(BinlogParser&&) noexcept = default;

BinlogParser& BinlogParser::operator=(BinlogParser&&) noexcept = default;

BinlogEvent BinlogParser::parse(const Buffer& payload) {
    return parse_binlog_event_packet_impl(payload, &impl_->state);
}

void BinlogParser::clear_table_map() {
    impl_->state.tables.clear();
}

template <typename T, typename F>
Promise<T> promise_from(F&& work) {
    return Promise<T>([fn = std::forward<F>(work)](auto resolve, auto reject) mutable {
        try {
            resolve(fn());
        } catch (...) {
            reject(std::any(std::current_exception()));
        }
    });
}

template <typename F>
Promise<void> promise_void_from(F&& work) {
    return Promise<void>([fn = std::forward<F>(work)](auto resolve, auto reject) mutable {
        try {
            fn();
            resolve();
        } catch (...) {
            reject(std::any(std::current_exception()));
        }
    });
}

Connection::Connection() : impl_(new Impl(ConnectionOptions{})) {
    setEmitter_(impl_->emitter());
}

Connection::Connection(ConnectionOptions options) : impl_(new Impl(std::move(options))) {
    setEmitter_(impl_->emitter());
}

Connection::~Connection() {
    if (impl_) {
        impl_->end();
        delete impl_;
    }
}

Connection::Connection(Connection&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {
    if (impl_) {
        setEmitter_(impl_->emitter());
    }
    other.resetEmitter_();
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (impl_) {
            impl_->end();
            delete impl_;
        }
        impl_ = std::exchange(other.impl_, nullptr);
        if (impl_) {
            setEmitter_(impl_->emitter());
        } else {
            resetEmitter_();
        }
        other.resetEmitter_();
    }
    return *this;
}

void Connection::connect() {
    try {
        impl_->traced("connect", "", [this] { impl_->connect(); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::connect(VoidCallback callback) {
    try {
        connect();
        if (callback) callback(nullptr);
    } catch (...) {
        if (callback) callback(std::current_exception());
    }
}

Promise<void> Connection::connect_promise() { return promise_void_from([this] { connect(); }); }

QueryResult Connection::query(const std::string& sql) {
    try {
        return impl_->traced("query", sql, [this, &sql] { return impl_->query(sql); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::query(const std::string& sql, const QueryAttributes& attributes) {
    try {
        return impl_->traced("query", sql, [this, &sql, &attributes] { return impl_->query(sql, attributes); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::query(const QueryOptions& options) {
    try {
        return impl_->traced("query", options.sql, [this, &options] { return impl_->query(options); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::query(const std::string& sql, QueryCallback callback) {
    try {
        auto result = query(sql);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

void Connection::query(const std::string& sql, const QueryAttributes& attributes, QueryCallback callback) {
    try {
        auto result = query(sql, attributes);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

void Connection::query(const QueryOptions& options, QueryCallback callback) {
    try {
        auto result = query(options);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

Promise<QueryResult> Connection::query_promise(const std::string& sql) {
    return promise_from<QueryResult>([this, sql] { return query(sql); });
}

Promise<QueryResult> Connection::query_promise(const std::string& sql, const QueryAttributes& attributes) {
    return promise_from<QueryResult>([this, sql, attributes] { return query(sql, attributes); });
}

Promise<QueryResult> Connection::query_promise(QueryOptions options) {
    return promise_from<QueryResult>([this, options = std::move(options)] { return query(options); });
}

std::vector<QueryResult> Connection::query_all(const std::string& sql) {
    try {
        return impl_->traced("query", sql, [this, &sql] { return impl_->query_all(sql); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

std::vector<QueryResult> Connection::query_all(const std::string& sql, const QueryAttributes& attributes) {
    try {
        return impl_->traced("query", sql, [this, &sql, &attributes] { return impl_->query_all(sql, attributes); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

std::vector<QueryResult> Connection::query_all(const QueryOptions& options) {
    try {
        return impl_->traced("query", options.sql, [this, &options] { return impl_->query_all(options); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::query_all(const std::string& sql, QueryAllCallback callback) {
    try {
        auto result = query_all(sql);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

void Connection::query_all(const std::string& sql, const QueryAttributes& attributes, QueryAllCallback callback) {
    try {
        auto result = query_all(sql, attributes);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

void Connection::query_all(const QueryOptions& options, QueryAllCallback callback) {
    try {
        auto result = query_all(options);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

Promise<std::vector<QueryResult>> Connection::query_all_promise(const std::string& sql) {
    return promise_from<std::vector<QueryResult>>([this, sql] { return query_all(sql); });
}

Promise<std::vector<QueryResult>> Connection::query_all_promise(const std::string& sql, const QueryAttributes& attributes) {
    return promise_from<std::vector<QueryResult>>([this, sql, attributes] { return query_all(sql, attributes); });
}

Promise<std::vector<QueryResult>> Connection::query_all_promise(QueryOptions options) {
    return promise_from<std::vector<QueryResult>>([this, options = std::move(options)] {
        return query_all(options);
    });
}

RowStream Connection::query_stream(const std::string& sql) {
    auto result = query(sql);
    return RowStream(std::move(result.fields), std::move(result.rows));
}

RowStream Connection::query_stream(const QueryOptions& options) {
    auto result = query(options);
    return RowStream(std::move(result.fields), std::move(result.rows));
}

stream::Readable Connection::query_stream_json(const std::string& sql) {
    return stream::Readable::from(query_stream(sql).to_json_line_buffers());
}

stream::Readable Connection::query_stream_json(const QueryOptions& options) {
    return stream::Readable::from(query_stream(options).to_json_line_buffers());
}

PreparedStatement Connection::prepare(const std::string& sql) {
    try {
        return impl_->prepare(sql);
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

PreparedStatement Connection::prepare(const std::string& sql, CommandOptions options) {
    try {
        return impl_->prepare(sql, options);
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::prepare(const std::string& sql, PrepareCallback callback) {
    try {
        auto statement = prepare(sql);
        if (callback) callback(nullptr, std::move(statement));
    } catch (...) {
        if (callback) callback(std::current_exception(), PreparedStatement{});
    }
}

Promise<PreparedStatement> Connection::prepare_promise(const std::string& sql) {
    return promise_from<PreparedStatement>([this, sql] { return prepare(sql); });
}

QueryResult Connection::execute(const PreparedStatement& statement, const std::vector<Value>& values) {
    try {
        return impl_->traced("execute", statement.query, [this, &statement, &values] {
            return impl_->execute(statement, values);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::execute(const PreparedStatement& statement,
                                const std::vector<Value>& values,
                                const QueryAttributes& attributes) {
    try {
        return impl_->traced("execute", statement.query, [this, &statement, &values, &attributes] {
            return impl_->execute(statement, values, attributes);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::execute(const PreparedStatement& statement,
                                const std::vector<Value>& values,
                                const QueryAttributes& attributes,
                                CommandOptions options) {
    try {
        return impl_->traced("execute", statement.query, [this, &statement, &values, &attributes, options] {
            return impl_->execute(statement, values, attributes, options);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::execute(const ExecuteOptions& options) {
    try {
        return impl_->traced("execute", options.sql, [this, &options] {
            return impl_->execute(options);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::execute(const PreparedStatement& statement, const std::vector<Value>& values, QueryCallback callback) {
    try {
        auto result = execute(statement, values);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

void Connection::execute(const PreparedStatement& statement,
                         const std::vector<Value>& values,
                         const QueryAttributes& attributes,
                         QueryCallback callback) {
    try {
        auto result = execute(statement, values, attributes);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

void Connection::execute(const ExecuteOptions& options, QueryCallback callback) {
    try {
        auto result = execute(options);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

Promise<QueryResult> Connection::execute_promise(const PreparedStatement& statement, const std::vector<Value>& values) {
    return promise_from<QueryResult>([this, statement, values] { return execute(statement, values); });
}

Promise<QueryResult> Connection::execute_promise(const PreparedStatement& statement,
                                                 const std::vector<Value>& values,
                                                 const QueryAttributes& attributes) {
    return promise_from<QueryResult>([this, statement, values, attributes] { return execute(statement, values, attributes); });
}

Promise<QueryResult> Connection::execute_promise(ExecuteOptions options) {
    return promise_from<QueryResult>([this, options = std::move(options)] { return execute(options); });
}

std::vector<QueryResult> Connection::execute_all(const PreparedStatement& statement, const std::vector<Value>& values) {
    try {
        return impl_->traced("execute", statement.query, [this, &statement, &values] {
            return impl_->execute_all(statement, values);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

std::vector<QueryResult> Connection::execute_all(const PreparedStatement& statement,
                                                 const std::vector<Value>& values,
                                                 const QueryAttributes& attributes) {
    try {
        return impl_->traced("execute", statement.query, [this, &statement, &values, &attributes] {
            return impl_->execute_all(statement, values, attributes);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

std::vector<QueryResult> Connection::execute_all(const PreparedStatement& statement,
                                                 const std::vector<Value>& values,
                                                 const QueryAttributes& attributes,
                                                 CommandOptions options) {
    try {
        return impl_->traced("execute", statement.query, [this, &statement, &values, &attributes, options] {
            return impl_->execute_all(statement, values, attributes, options);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

std::vector<QueryResult> Connection::execute_all(const ExecuteOptions& options) {
    try {
        return impl_->traced("execute", options.sql, [this, &options] {
            return impl_->execute_all(options);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::execute_all(const PreparedStatement& statement, const std::vector<Value>& values, QueryAllCallback callback) {
    try {
        auto result = execute_all(statement, values);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

void Connection::execute_all(const PreparedStatement& statement,
                             const std::vector<Value>& values,
                             const QueryAttributes& attributes,
                             QueryAllCallback callback) {
    try {
        auto result = execute_all(statement, values, attributes);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

void Connection::execute_all(const ExecuteOptions& options, QueryAllCallback callback) {
    try {
        auto result = execute_all(options);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

Promise<std::vector<QueryResult>> Connection::execute_all_promise(const PreparedStatement& statement, const std::vector<Value>& values) {
    return promise_from<std::vector<QueryResult>>([this, statement, values] { return execute_all(statement, values); });
}

Promise<std::vector<QueryResult>> Connection::execute_all_promise(const PreparedStatement& statement,
                                                                  const std::vector<Value>& values,
                                                                  const QueryAttributes& attributes) {
    return promise_from<std::vector<QueryResult>>([this, statement, values, attributes] {
        return execute_all(statement, values, attributes);
    });
}

Promise<std::vector<QueryResult>> Connection::execute_all_promise(ExecuteOptions options) {
    return promise_from<std::vector<QueryResult>>([this, options = std::move(options)] {
        return execute_all(options);
    });
}

QueryResult Connection::execute(const std::string& sql, const std::vector<Value>& values) {
    try {
        return impl_->traced("execute", sql, [this, &sql, &values] { return impl_->execute(sql, values); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::execute(const std::string& sql,
                                const std::vector<Value>& values,
                                const QueryAttributes& attributes) {
    try {
        return impl_->traced("execute", sql, [this, &sql, &values, &attributes] {
            return impl_->execute(sql, values, attributes);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::execute(const std::string& sql, const std::vector<Value>& values, QueryCallback callback) {
    try {
        auto result = execute(sql, values);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

void Connection::execute(const std::string& sql,
                         const std::vector<Value>& values,
                         const QueryAttributes& attributes,
                         QueryCallback callback) {
    try {
        auto result = execute(sql, values, attributes);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

Promise<QueryResult> Connection::execute_promise(const std::string& sql, const std::vector<Value>& values) {
    return promise_from<QueryResult>([this, sql, values] { return execute(sql, values); });
}

Promise<QueryResult> Connection::execute_promise(const std::string& sql,
                                                 const std::vector<Value>& values,
                                                 const QueryAttributes& attributes) {
    return promise_from<QueryResult>([this, sql, values, attributes] { return execute(sql, values, attributes); });
}

std::vector<QueryResult> Connection::execute_all(const std::string& sql, const std::vector<Value>& values) {
    try {
        return impl_->traced("execute", sql, [this, &sql, &values] { return impl_->execute_all(sql, values); });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

std::vector<QueryResult> Connection::execute_all(const std::string& sql,
                                                 const std::vector<Value>& values,
                                                 const QueryAttributes& attributes) {
    try {
        return impl_->traced("execute", sql, [this, &sql, &values, &attributes] {
            return impl_->execute_all(sql, values, attributes);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::execute_all(const std::string& sql, const std::vector<Value>& values, QueryAllCallback callback) {
    try {
        auto result = execute_all(sql, values);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

void Connection::execute_all(const std::string& sql,
                             const std::vector<Value>& values,
                             const QueryAttributes& attributes,
                             QueryAllCallback callback) {
    try {
        auto result = execute_all(sql, values, attributes);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

Promise<std::vector<QueryResult>> Connection::execute_all_promise(const std::string& sql, const std::vector<Value>& values) {
    return promise_from<std::vector<QueryResult>>([this, sql, values] { return execute_all(sql, values); });
}

Promise<std::vector<QueryResult>> Connection::execute_all_promise(const std::string& sql,
                                                                  const std::vector<Value>& values,
                                                                  const QueryAttributes& attributes) {
    return promise_from<std::vector<QueryResult>>([this, sql, values, attributes] {
        return execute_all(sql, values, attributes);
    });
}

StatementCursor Connection::execute_cursor(const PreparedStatement& statement,
                                           const std::vector<Value>& values,
                                           const QueryAttributes& attributes,
                                           CursorType cursor_type) {
    try {
        return impl_->traced("execute", statement.query, [this, &statement, &values, &attributes, cursor_type] {
            return impl_->execute_cursor(statement, values, attributes, cursor_type);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

StatementCursor Connection::execute_cursor(const std::string& sql,
                                           const std::vector<Value>& values,
                                           const QueryAttributes& attributes,
                                           CursorType cursor_type) {
    try {
        return impl_->traced("execute", sql, [this, &sql, &values, &attributes, cursor_type] {
            return impl_->execute_cursor(sql, values, attributes, cursor_type);
        });
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::fetch(StatementCursor& cursor, uint32_t row_count) {
    try {
        return impl_->fetch(cursor, row_count);
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

QueryResult Connection::fetch(StatementCursor& cursor, uint32_t row_count, CommandOptions options) {
    try {
        return impl_->fetch(cursor, row_count, options);
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

OkPacket Connection::register_slave(const RegisterSlaveOptions& options) {
    try {
        return impl_->register_slave(options);
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::register_slave(const RegisterSlaveOptions& options, OkCallback callback) {
    try {
        auto ok = register_slave(options);
        if (callback) callback(nullptr, ok);
    } catch (...) {
        if (callback) callback(std::current_exception(), OkPacket{});
    }
}

Promise<OkPacket> Connection::register_slave_promise(RegisterSlaveOptions options) {
    return promise_from<OkPacket>([this, options = std::move(options)] { return register_slave(options); });
}

std::vector<BinlogEvent> Connection::binlog_dump(const BinlogDumpOptions& options) {
    try {
        return impl_->binlog_dump(options);
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

std::size_t Connection::binlog_dump_each(const BinlogDumpOptions& options, BinlogEventCallback callback) {
    try {
        return impl_->binlog_dump_each(options, callback);
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::binlog_dump(const BinlogDumpOptions& options, BinlogEventsCallback callback) {
    try {
        auto events = binlog_dump(options);
        if (callback) callback(nullptr, std::move(events));
    } catch (...) {
        if (callback) callback(std::current_exception(), {});
    }
}

Promise<std::vector<BinlogEvent>> Connection::binlog_dump_promise(BinlogDumpOptions options) {
    return promise_from<std::vector<BinlogEvent>>(
        [this, options = std::move(options)] { return binlog_dump(options); });
}

void Connection::close_statement(const PreparedStatement& statement) { impl_->close_statement(statement); }

void Connection::close_statement(const std::string& sql) { impl_->close_statement(sql); }

OkPacket Connection::begin_transaction() { return query("START TRANSACTION").ok; }

void Connection::begin_transaction(OkCallback callback) {
    try {
        auto ok = begin_transaction();
        if (callback) callback(nullptr, ok);
    } catch (...) {
        if (callback) callback(std::current_exception(), OkPacket{});
    }
}

Promise<OkPacket> Connection::begin_transaction_promise() {
    return promise_from<OkPacket>([this] { return begin_transaction(); });
}

OkPacket Connection::commit() { return query("COMMIT").ok; }

void Connection::commit(OkCallback callback) {
    try {
        auto ok = commit();
        if (callback) callback(nullptr, ok);
    } catch (...) {
        if (callback) callback(std::current_exception(), OkPacket{});
    }
}

Promise<OkPacket> Connection::commit_promise() {
    return promise_from<OkPacket>([this] { return commit(); });
}

OkPacket Connection::rollback() { return query("ROLLBACK").ok; }

void Connection::rollback(OkCallback callback) {
    try {
        auto ok = rollback();
        if (callback) callback(nullptr, ok);
    } catch (...) {
        if (callback) callback(std::current_exception(), OkPacket{});
    }
}

Promise<OkPacket> Connection::rollback_promise() {
    return promise_from<OkPacket>([this] { return rollback(); });
}

OkPacket Connection::ping() {
    try {
        return impl_->ping();
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::ping(OkCallback callback) {
    try {
        auto ok = ping();
        if (callback) callback(nullptr, ok);
    } catch (...) {
        if (callback) callback(std::current_exception(), OkPacket{});
    }
}

Promise<OkPacket> Connection::ping_promise() { return promise_from<OkPacket>([this] { return ping(); }); }

void Connection::reset() {
    try {
        impl_->reset();
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::reset(VoidCallback callback) {
    try {
        reset();
        if (callback) callback(nullptr);
    } catch (...) {
        if (callback) callback(std::current_exception());
    }
}

Promise<void> Connection::reset_promise() { return promise_void_from([this] { reset(); }); }

void Connection::change_user(ConnectionOptions options) {
    try {
        impl_->change_user(std::move(options));
    } catch (const Error& error) {
        impl_->emit_error(error);
        throw;
    }
}

void Connection::change_user(ConnectionOptions options, VoidCallback callback) {
    try {
        change_user(std::move(options));
        if (callback) callback(nullptr);
    } catch (...) {
        if (callback) callback(std::current_exception());
    }
}

Promise<void> Connection::change_user_promise(ConnectionOptions options) {
    return promise_void_from([this, options = std::move(options)]() mutable { change_user(std::move(options)); });
}

void Connection::end() { impl_->end(); }

void Connection::end(VoidCallback callback) {
    end();
    if (callback) callback(nullptr);
}

Promise<void> Connection::end_promise() { return promise_void_from([this] { end(); }); }

void Connection::destroy() noexcept { if (impl_) impl_->end(); }

void Connection::pause() noexcept { if (impl_) impl_->pause(); }

void Connection::resume() noexcept { if (impl_) impl_->resume(); }

bool Connection::connected() const noexcept { return impl_ && impl_->connected(); }

bool Connection::encrypted() const noexcept { return impl_ && impl_->encrypted(); }

bool Connection::compressed() const noexcept { return impl_ && impl_->compressed(); }

bool Connection::paused() const noexcept { return impl_ && impl_->paused(); }

const ConnectionOptions& Connection::options() const noexcept { return impl_->options(); }

const std::string& Connection::server_version() const noexcept { return impl_->server_version(); }

uint32_t Connection::connection_id() const noexcept { return impl_->connection_id(); }

uint32_t Connection::server_capability_flags() const noexcept { return impl_->server_capability_flags(); }

class ServerConnection::Impl : public std::enable_shared_from_this<ServerConnection::Impl> {
public:
    Impl(io::TcpSocket socket, EventContext& ctx)
        : ctx_(ctx), socket_(std::move(socket)) {}

    ~Impl() {
        close();
    }

    events::EventEmitter& emitter() noexcept { return emitter_; }

    void set_owner(ServerConnection* owner) noexcept { owner_ = owner; }

    void server_handshake(ServerHandshakeOptions options) {
        if (handshake_started_) {
            return;
        }
        handshake_started_ = true;
        handshake_options_ = std::move(options);
        if (handshake_options_.capability_flags == 0) {
            handshake_options_.capability_flags = default_server_capability_flags();
        }
        if (handshake_options_.auth_plugin_name.empty()) {
            handshake_options_.auth_plugin_name = "mysql_native_password";
        }
        scramble_ = random_scramble();
        uint8_t sequence = 0;
        std::vector<uint8_t> packet;
        append_packet_bytes(packet, build_server_handshake_payload(handshake_options_, scramble_), sequence);
        write_raw(std::move(packet));
        read_packet();
    }

    void write_ok(OkPacket ok) {
        std::vector<uint8_t> packet;
        uint8_t sequence = next_response_sequence_;
        append_packet_bytes(packet, build_ok_payload(ok), sequence);
        next_response_sequence_ = sequence;
        write_raw(std::move(packet));
    }

    void write_error(uint16_t code, const std::string& sql_state, const std::string& message) {
        std::vector<uint8_t> packet;
        uint8_t sequence = next_response_sequence_;
        append_packet_bytes(packet, build_error_payload(code, sql_state, message), sequence);
        next_response_sequence_ = sequence;
        write_raw(std::move(packet));
    }

    void write_columns(const std::vector<Field>& fields) {
        std::vector<uint8_t> out;
        uint8_t sequence = next_response_sequence_;
        std::vector<uint8_t> header;
        append_lenenc_int(header, fields.size());
        append_packet_bytes(out, buffer_from_bytes(header), sequence);
        for (const auto& field : fields) {
            append_packet_bytes(out, build_column_definition_payload(field), sequence);
        }
        append_packet_bytes(out, build_eof_payload(), sequence);
        next_response_sequence_ = sequence;
        write_raw(std::move(out));
    }

    void write_text_row(const Row& row, const std::vector<Field>& fields) {
        write_packet_payload(build_text_row_payload(row, fields));
    }

    void write_binary_row(const Row& row, const std::vector<Field>& fields) {
        write_packet_payload(build_binary_row_payload(row, fields));
    }

    void write_eof(uint16_t warnings, uint16_t status) {
        write_packet_payload(build_eof_payload(warnings, status));
    }

    void write_text_result(const std::vector<Row>& rows, const std::vector<Field>& fields) {
        write_raw(build_text_result_packets(rows, fields, next_response_sequence_));
    }

    void write_binary_result(const std::vector<Row>& rows, const std::vector<Field>& fields) {
        write_raw(build_binary_result_packets(rows, fields, next_response_sequence_));
    }

    void write_statement_prepare_ok(uint32_t statement_id,
                                    const std::vector<Field>& parameters,
                                    const std::vector<Field>& fields,
                                    uint16_t warning_count) {
        write_raw(build_statement_prepare_ok_packets(statement_id,
                                                     parameters,
                                                     fields,
                                                     warning_count,
                                                     next_response_sequence_));
    }

    void close() noexcept {
        connected_ = false;
        std::error_code ec;
        socket_.shutdown(2, ec);
        socket_.close(ec);
    }

    bool connected() const noexcept { return connected_ && socket_.isOpen(); }

    const ServerAuthInfo& auth_info() const noexcept { return auth_info_; }

    std::string remote_address() const { return socket_.remoteAddress(); }

    uint16_t remote_port() const { return socket_.remotePort(); }

private:
    struct Frame {
        uint8_t sequence_id = 0;
        Buffer payload;
    };

    struct ReadState {
        uint8_t first_sequence = 0;
        bool first = true;
        std::vector<Buffer> parts;
    };

    void emit_error(const Error& error) {
        if (emitter_.listenerCount(event::Error_) > 0) {
            emitter_.emit(event::Error_, error);
        }
    }

    void fail(const Error& error) {
        emit_error(error);
        close();
    }

    void read_packet() {
        if (!connected() && handshake_started_) {
            return;
        }
        read_packet_part(std::make_shared<ReadState>());
    }

    void read_packet_part(std::shared_ptr<ReadState> state) {
        auto self = shared_from_this();
        auto header = std::make_shared<std::array<uint8_t, 4>>();
        socket_.asyncRead(header->data(), header->size(), [self, state, header](std::error_code ec, std::size_t n) {
            if (ec) {
                self->close();
                return;
            }
            if (n != header->size()) {
                self->fail(Error("mysql2 server connection ended before packet header was complete"));
                return;
            }
            const auto length = static_cast<uint32_t>((*header)[0] | ((*header)[1] << 8) | ((*header)[2] << 16));
            const auto sequence_id = (*header)[3];
            if (state->first) {
                state->first_sequence = sequence_id;
                state->first = false;
            }
            auto body = std::make_shared<std::vector<uint8_t>>(length);
            if (length == 0) {
                state->parts.push_back(Buffer{});
                self->handle_packet(Frame{state->first_sequence, Buffer::concat(state->parts)});
                return;
            }
            self->socket_.asyncRead(body->data(), body->size(), [self, state, body, length](std::error_code body_ec, std::size_t body_n) {
                if (body_ec) {
                    self->close();
                    return;
                }
                if (body_n != body->size()) {
                    self->fail(Error("mysql2 server connection ended before packet payload was complete"));
                    return;
                }
                state->parts.push_back(Buffer::from(body->data(), body->size()));
                if (length == kMaxPacketPayloadLength) {
                    self->read_packet_part(state);
                    return;
                }
                self->handle_packet(Frame{state->first_sequence, Buffer::concat(state->parts)});
            });
        });
    }

    void handle_packet(const Frame& frame) {
        try {
            if (phase_ == Phase::HandshakeResponse) {
                handle_handshake_response(frame);
            } else {
                handle_command(frame);
            }
        } catch (const Error& error) {
            fail(error);
        } catch (const std::exception& error) {
            fail(Error(error.what()));
        }
    }

    void handle_handshake_response(const Frame& frame) {
        next_response_sequence_ = static_cast<uint8_t>(frame.sequence_id + 1);
        auth_info_ = parse_server_handshake_response(frame.payload,
                                                     handshake_options_.capability_flags,
                                                     remote_address(),
                                                     remote_port());
        if (handshake_options_.auth_callback) {
            auto auth_error = handshake_options_.auth_callback(auth_info_);
            if (auth_error) {
                write_error_and_close(auth_error->code() == 0 ? 1045 : auth_error->code(),
                                      auth_error->sql_state().empty() ? "28000" : auth_error->sql_state(),
                                      auth_error->what());
                return;
            }
        }
        phase_ = Phase::Command;
        connected_ = true;
        write_ok(OkPacket{});
        read_packet();
    }

    void handle_command(const Frame& frame) {
        if (frame.payload.length() == 0) {
            fail(Error("empty mysql2 server command packet"));
            return;
        }
        next_response_sequence_ = static_cast<uint8_t>(frame.sequence_id + 1);
        PacketCursor cursor(frame.payload);
        const auto command = cursor.read_u8();
        bool known = true;
        const auto emit_packet = [&]() {
            if (owner_ && emitter_.listenerCount(event::ServerPacket) > 0) {
                emitter_.emit(event::ServerPacket, *owner_, frame.payload, known, command);
            }
        };

        switch (command) {
            case command_code::QUIT:
                emit_packet();
                if (owner_ && emitter_.listenerCount(event::ServerQuit) > 0) {
                    emitter_.emit(event::ServerQuit, *owner_);
                }
                close();
                return;
            case command_code::PING:
                emit_packet();
                if (owner_ && emitter_.listenerCount(event::ServerPing) > 0) {
                    emitter_.emit(event::ServerPing, *owner_);
                } else {
                    write_ok(OkPacket{});
                }
                break;
            case command_code::INIT_DB: {
                const auto schema = PacketCursor::decode_buffer(cursor.read_rest_buffer(), client_encoding());
                emit_packet();
                if (owner_ && emitter_.listenerCount(event::ServerInitDb) > 0) {
                    emitter_.emit(event::ServerInitDb, *owner_, schema);
                } else {
                    write_ok(OkPacket{});
                }
                break;
            }
            case command_code::FIELD_LIST: {
                const auto table = cursor.read_null_terminated_ascii();
                const auto fields = PacketCursor::decode_buffer(cursor.read_rest_buffer(), client_encoding());
                emit_packet();
                if (owner_ && emitter_.listenerCount(event::ServerFieldList) > 0) {
                    emitter_.emit(event::ServerFieldList, *owner_, table, fields);
                } else {
                    write_error(1287, "HY000", "COM_FIELD_LIST is deprecated and no field_list handler is configured");
                }
                break;
            }
            case command_code::QUERY: {
                const auto sql = PacketCursor::decode_buffer(cursor.read_rest_buffer(), client_encoding());
                emit_packet();
                if (owner_ &&
                    (is_server_statement_text(sql, "PREPARE") || is_server_statement_text(sql, "SET")) &&
                    emitter_.listenerCount(event::ServerStatementPrepare) > 0) {
                    emitter_.emit(event::ServerStatementPrepare, *owner_, sql);
                } else if (owner_ &&
                           is_server_statement_text(sql, "EXECUTE") &&
                           emitter_.listenerCount(event::ServerStatementExecute) > 0) {
                    ServerStatementExecuteInfo info;
                    info.query = sql;
                    emitter_.emit(event::ServerStatementExecute, *owner_, info);
                } else if (owner_ && emitter_.listenerCount(event::ServerQuery) > 0) {
                    emitter_.emit(event::ServerQuery, *owner_, sql);
                } else {
                    write_error(1105, "HY000", "No query handler");
                }
                break;
            }
            case command_code::STMT_PREPARE: {
                const auto sql = PacketCursor::decode_buffer(cursor.read_rest_buffer(), client_encoding());
                emit_packet();
                if (owner_ && emitter_.listenerCount(event::ServerStatementPrepare) > 0) {
                    emitter_.emit(event::ServerStatementPrepare, *owner_, sql);
                } else {
                    write_error(1105, "HY000", "No query handler for prepared statements");
                }
                break;
            }
            case command_code::STMT_EXECUTE: {
                const auto execute_payload = cursor.read_rest_buffer();
                auto info = parse_server_statement_execute_payload(execute_payload, client_encoding());
                emit_packet();
                if (owner_ && emitter_.listenerCount(event::ServerStatementExecute) > 0) {
                    emitter_.emit(event::ServerStatementExecute, *owner_, info);
                } else {
                    write_error(1105, "HY000", "No query handler for execute statements");
                }
                break;
            }
            default:
                known = false;
                emit_packet();
                write_error(1047, "08S01", "Unknown command");
                break;
        }
        read_packet();
    }

    std::string client_encoding() const {
        return auth_info_.charset_number == 0 ? "utf8" : charset_encoding(auth_info_.charset_number);
    }

    void write_error_and_close(uint16_t code, const std::string& sql_state, const std::string& message) {
        std::vector<uint8_t> packet;
        uint8_t sequence = next_response_sequence_;
        append_packet_bytes(packet, build_error_payload(code, sql_state, message), sequence);
        next_response_sequence_ = sequence;
        write_raw(std::move(packet), true);
    }

    void write_packet_payload(const Buffer& payload) {
        std::vector<uint8_t> packet;
        uint8_t sequence = next_response_sequence_;
        append_packet_bytes(packet, payload, sequence);
        next_response_sequence_ = sequence;
        write_raw(std::move(packet));
    }

    void write_raw(std::vector<uint8_t> bytes, bool close_after_write = false) {
        if (!socket_.isOpen()) {
            return;
        }
        auto self = shared_from_this();
        auto data = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
        socket_.asyncWrite(data->data(), data->size(), [self, data, close_after_write](std::error_code ec, std::size_t) {
            if (ec) {
                self->emit_error(Error("mysql2 server socket write failed: " + ec.message()));
                self->close();
                return;
            }
            if (close_after_write) {
                self->close();
            }
        });
    }

    enum class Phase {
        HandshakeResponse,
        Command
    };

    EventContext& ctx_;
    io::TcpSocket socket_;
    events::EventEmitter emitter_;
    ServerConnection* owner_ = nullptr;
    ServerHandshakeOptions handshake_options_;
    ServerAuthInfo auth_info_;
    Buffer scramble_;
    Phase phase_ = Phase::HandshakeResponse;
    uint8_t next_response_sequence_ = 0;
    bool handshake_started_ = false;
    bool connected_ = true;
};

ServerConnection::ServerConnection(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {
    if (impl_) {
        impl_->set_owner(this);
        setEmitter_(impl_->emitter());
    }
}

ServerConnection::~ServerConnection() {
    if (impl_) {
        impl_->close();
    }
}

ServerConnection::ServerConnection(ServerConnection&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (impl_) {
        impl_->set_owner(this);
        setEmitter_(impl_->emitter());
    }
    other.resetEmitter_();
}

ServerConnection& ServerConnection::operator=(ServerConnection&& other) noexcept {
    if (this != &other) {
        if (impl_) {
            impl_->close();
        }
        impl_ = std::move(other.impl_);
        if (impl_) {
            impl_->set_owner(this);
            setEmitter_(impl_->emitter());
        } else {
            resetEmitter_();
        }
        other.resetEmitter_();
    }
    return *this;
}

void ServerConnection::server_handshake(ServerHandshakeOptions options) { impl_->server_handshake(std::move(options)); }

void ServerConnection::write_ok(OkPacket ok) { impl_->write_ok(ok); }

void ServerConnection::write_error(uint16_t code, const std::string& sql_state, const std::string& message) {
    impl_->write_error(code, sql_state, message);
}

void ServerConnection::write_error(const Error& error) {
    impl_->write_error(error.code() == 0 ? 1105 : error.code(),
                       error.sql_state().empty() ? "HY000" : error.sql_state(),
                       error.what());
}

void ServerConnection::write_columns(const std::vector<Field>& fields) {
    impl_->write_columns(fields);
}

void ServerConnection::write_text_row(const Row& row, const std::vector<Field>& fields) {
    impl_->write_text_row(row, fields);
}

void ServerConnection::write_binary_row(const Row& row, const std::vector<Field>& fields) {
    impl_->write_binary_row(row, fields);
}

void ServerConnection::write_eof(uint16_t warnings, uint16_t status) {
    impl_->write_eof(warnings, status);
}

void ServerConnection::write_text_result(const QueryResult& result) {
    impl_->write_text_result(result.rows, result.fields);
}

void ServerConnection::write_text_result(const std::vector<Row>& rows, const std::vector<Field>& fields) {
    impl_->write_text_result(rows, fields);
}

void ServerConnection::write_binary_result(const QueryResult& result) {
    impl_->write_binary_result(result.rows, result.fields);
}

void ServerConnection::write_binary_result(const std::vector<Row>& rows, const std::vector<Field>& fields) {
    impl_->write_binary_result(rows, fields);
}

void ServerConnection::write_statement_prepare_ok(uint32_t statement_id,
                                                  const std::vector<Field>& parameters,
                                                  const std::vector<Field>& fields,
                                                  uint16_t warning_count) {
    impl_->write_statement_prepare_ok(statement_id, parameters, fields, warning_count);
}

void ServerConnection::close() noexcept {
    if (impl_) {
        impl_->close();
    }
}

bool ServerConnection::connected() const noexcept { return impl_ && impl_->connected(); }

const ServerAuthInfo& ServerConnection::auth_info() const noexcept { return impl_->auth_info(); }

std::string ServerConnection::remote_address() const { return impl_ ? impl_->remote_address() : std::string{}; }

uint16_t ServerConnection::remote_port() const { return impl_ ? impl_->remote_port() : 0; }

class ServerImpl : public std::enable_shared_from_this<ServerImpl> {
public:
    explicit ServerImpl(ServerOptions options)
        : options_(std::move(options)), acceptor_(ctx_) {}

    ~ServerImpl() {
        close();
    }

    events::EventEmitter& emitter() noexcept { return emitter_; }

    void listen(std::optional<uint16_t> port = std::nullopt, std::optional<std::string> host = std::nullopt) {
        if (listening_) {
            throw Error("mysql2 server is already listening");
        }
        if (port) {
            options_.port = *port;
        }
        if (host) {
            options_.host = *host;
        }
        ctx_.restart();
        const int family = options_.host.find(':') == std::string::npos ? AF_INET : AF_INET6;
        acceptor_.open(family);
        acceptor_.setReuseAddress(true);
        acceptor_.bind(options_.host, options_.port);
        acceptor_.listen(options_.backlog);
        local_address_ = acceptor_.localAddress();
        local_port_ = acceptor_.localPort();
        listening_ = true;
        next_connection_id_ = options_.handshake.connection_id == 0 ? 1 : options_.handshake.connection_id;
        start_accept();
        thread_ = std::thread([self = shared_from_this()] {
            self->ctx_.run();
        });
    }

    void close() {
        if (!listening_ && !thread_.joinable()) {
            return;
        }
        listening_ = false;
        std::error_code ec;
        acceptor_.close(ec);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& connection : connections_) {
                if (connection) {
                    connection->close();
                }
            }
            connections_.clear();
        }
        ctx_.stop();
        if (thread_.joinable()) {
            if (thread_.get_id() != std::this_thread::get_id()) {
                thread_.join();
            } else {
                thread_.detach();
            }
        }
    }

    bool listening() const noexcept { return listening_; }

    const std::string& address() const noexcept { return local_address_; }

    uint16_t port() const noexcept { return local_port_; }

    std::size_t connection_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return connections_.size();
    }

private:
    void start_accept() {
        auto self = shared_from_this();
        acceptor_.asyncAccept([self](std::error_code ec, io::TcpSocket socket) {
            if (!self->listening_) {
                return;
            }
            if (ec) {
                if (self->emitter_.listenerCount(event::Error_) > 0) {
                    self->emitter_.emit(event::Error_, Error("mysql2 server accept failed: " + ec.message()));
                }
                self->start_accept();
                return;
            }
            auto impl = std::make_shared<ServerConnection::Impl>(std::move(socket), self->ctx_);
            auto connection = std::shared_ptr<ServerConnection>(new ServerConnection(std::move(impl)));
            {
                std::lock_guard<std::mutex> lock(self->mutex_);
                self->connections_.push_back(connection);
            }
            self->emitter_.emit(event::ServerConnectionAccepted, *connection);
            if (self->options_.auto_handshake) {
                auto handshake = self->options_.handshake;
                handshake.connection_id = self->next_connection_id_++;
                connection->server_handshake(std::move(handshake));
            }
            self->start_accept();
        });
    }

    ServerOptions options_;
    EventContext ctx_;
    io::TcpAcceptor acceptor_;
    events::EventEmitter emitter_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<ServerConnection>> connections_;
    std::string local_address_;
    uint16_t local_port_ = 0;
    uint32_t next_connection_id_ = 1;
    std::atomic<bool> listening_{false};
};

Server::Server(ServerOptions options) : impl_(std::make_shared<ServerImpl>(std::move(options))) {
    setEmitter_(impl_->emitter());
}

Server::~Server() {
    if (impl_) {
        impl_->close();
    }
}

Server::Server(Server&& other) noexcept : impl_(std::move(other.impl_)) {
    if (impl_) {
        setEmitter_(impl_->emitter());
    }
    other.resetEmitter_();
}

Server& Server::operator=(Server&& other) noexcept {
    if (this != &other) {
        if (impl_) {
            impl_->close();
        }
        impl_ = std::move(other.impl_);
        if (impl_) {
            setEmitter_(impl_->emitter());
        } else {
            resetEmitter_();
        }
        other.resetEmitter_();
    }
    return *this;
}

void Server::listen() { impl_->listen(); }

void Server::listen(uint16_t port) { impl_->listen(port, std::nullopt); }

void Server::listen(uint16_t port, const std::string& host) { impl_->listen(port, host); }

void Server::close() { impl_->close(); }

bool Server::listening() const noexcept { return impl_ && impl_->listening(); }

std::string Server::address() const { return impl_ ? impl_->address() : std::string{}; }

uint16_t Server::port() const noexcept { return impl_ ? impl_->port() : 0; }

std::size_t Server::connection_count() const noexcept { return impl_ ? impl_->connection_count() : 0; }

class PoolImpl : public std::enable_shared_from_this<PoolImpl> {
public:
    explicit PoolImpl(PoolOptions options) : options_(std::move(options)) {
        if (options_.connection_limit == 0) {
            throw Error("pool connection_limit must be greater than zero");
        }
        if (options_.max_idle > options_.connection_limit) {
            options_.max_idle = options_.connection_limit;
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
            emitter_.emit(event::Acquire, *connection);
            return PoolConnection(shared_from_this(), connection);
        }

        if (connections_.size() < options_.connection_limit) {
            auto connection = std::make_shared<Connection>(options_.connection);
            connection->connect();
            connections_.push_back(std::move(connection));
            emitter_.emit(event::ConnectionCreated, *connections_.back());
            emitter_.emit(event::Acquire, *connections_.back());
            return PoolConnection(shared_from_this(), connections_.back());
        }

        if (!options_.wait_for_connections) {
            throw Error("no mysql2 pool connections are available");
        }
        if (options_.queue_limit != 0 && waiting_count_ >= options_.queue_limit) {
            throw Error("mysql2 pool queue limit reached");
        }
        ++waiting_count_;
        struct WaitingGuard {
            std::size_t& value;
            ~WaitingGuard() { --value; }
        } waiting_guard{waiting_count_};
        emitter_.emit(event::Enqueue);

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
        emitter_.emit(event::Acquire, *connection);
        return PoolConnection(shared_from_this(), connection);
    }

    void release(std::shared_ptr<Connection> connection) noexcept {
        if (!connection) {
            return;
        }
        if (options_.reset_on_release && connection->connected()) {
            try {
                connection->reset();
            } catch (...) {
                connection->end();
                std::lock_guard<std::mutex> lock(mutex_);
                erase_connection(connection);
                available_.notify_one();
                return;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            connection->end();
            return;
        }
        if (idle_.size() >= options_.max_idle) {
            connection->end();
            erase_connection(connection);
            available_.notify_one();
            return;
        }
        idle_.push_back(connection);
        try {
            emitter_.emit(event::Release, *connection);
        } catch (...) {
        }
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

    events::EventEmitter& emitter() noexcept { return emitter_; }

private:
    void erase_connection(const std::shared_ptr<Connection>& connection) {
        connections_.erase(std::remove(connections_.begin(), connections_.end(), connection), connections_.end());
        idle_.erase(std::remove(idle_.begin(), idle_.end(), connection), idle_.end());
    }

    PoolOptions options_;
    events::EventEmitter emitter_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<std::shared_ptr<Connection>> connections_;
    std::vector<std::shared_ptr<Connection>> idle_;
    std::size_t waiting_count_ = 0;
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

Pool::Pool(PoolOptions options) : impl_(std::make_shared<PoolImpl>(std::move(options))) {
    setEmitter_(impl_->emitter());
}

Pool::~Pool() = default;

Pool::Pool(Pool&& other) noexcept : impl_(std::move(other.impl_)) {
    if (impl_) {
        setEmitter_(impl_->emitter());
    }
    other.resetEmitter_();
}

Pool& Pool::operator=(Pool&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (impl_) {
            setEmitter_(impl_->emitter());
        } else {
            resetEmitter_();
        }
        other.resetEmitter_();
    }
    return *this;
}

PoolConnection Pool::get_connection() { return impl_->acquire(); }

void Pool::get_connection(std::function<void(std::exception_ptr, std::shared_ptr<PoolConnection>)> callback) {
    try {
        auto connection = std::make_shared<PoolConnection>(get_connection());
        if (callback) callback(nullptr, std::move(connection));
    } catch (...) {
        if (callback) callback(std::current_exception(), nullptr);
    }
}

Promise<std::shared_ptr<PoolConnection>> Pool::get_connection_promise() {
    return promise_from<std::shared_ptr<PoolConnection>>([this] {
        return std::make_shared<PoolConnection>(get_connection());
    });
}

void Pool::release_connection(PoolConnection& connection) { connection.release(); }

QueryResult Pool::query(const std::string& sql) { return impl_->query(sql); }

void Pool::query(const std::string& sql, QueryCallback callback) {
    try {
        auto result = query(sql);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

Promise<QueryResult> Pool::query_promise(const std::string& sql) {
    return promise_from<QueryResult>([this, sql] { return query(sql); });
}

std::vector<QueryResult> Pool::query_all(const std::string& sql) { return impl_->query_all(sql); }

Promise<std::vector<QueryResult>> Pool::query_all_promise(const std::string& sql) {
    return promise_from<std::vector<QueryResult>>([this, sql] { return query_all(sql); });
}

QueryResult Pool::execute(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute(sql, values);
}

void Pool::execute(const std::string& sql, const std::vector<Value>& values, QueryCallback callback) {
    try {
        auto result = execute(sql, values);
        if (callback) callback(nullptr, std::move(result));
    } catch (...) {
        if (callback) callback(std::current_exception(), QueryResult{});
    }
}

Promise<QueryResult> Pool::execute_promise(const std::string& sql, const std::vector<Value>& values) {
    return promise_from<QueryResult>([this, sql, values] { return execute(sql, values); });
}

std::vector<QueryResult> Pool::execute_all(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute_all(sql, values);
}

Promise<std::vector<QueryResult>> Pool::execute_all_promise(const std::string& sql, const std::vector<Value>& values) {
    return promise_from<std::vector<QueryResult>>([this, sql, values] { return execute_all(sql, values); });
}

void Pool::end() { impl_->end(); }

void Pool::end(VoidCallback callback) {
    end();
    if (callback) callback(nullptr);
}

Promise<void> Pool::end_promise() { return promise_void_from([this] { end(); }); }

std::size_t Pool::total_count() const noexcept { return impl_ ? impl_->total_count() : 0; }

std::size_t Pool::idle_count() const noexcept { return impl_ ? impl_->idle_count() : 0; }

bool wildcard_match(const std::string& pattern, const std::string& value) {
    std::size_t p = 0;
    std::size_t v = 0;
    std::size_t star = std::string::npos;
    std::size_t match = 0;
    while (v < value.size()) {
        if (p < pattern.size() && (pattern[p] == value[v])) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = v;
        } else if (star != std::string::npos) {
            p = star + 1;
            v = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

class PoolClusterImpl : public std::enable_shared_from_this<PoolClusterImpl> {
public:
    explicit PoolClusterImpl(PoolClusterOptions options) : options_(options) {}

    ~PoolClusterImpl() { end(); }

    events::EventEmitter& emitter() noexcept { return emitter_; }

    void add(std::string id, PoolOptions options) {
        if (id.empty()) {
            id = "CLUSTER::" + std::to_string(++last_id_);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) {
            throw Error("pool cluster is closed");
        }
        if (nodes_.find(id) != nodes_.end()) {
            return;
        }
        Node node;
        node.id = id;
        node.pool = std::make_shared<Pool>(std::move(options));
        nodes_.emplace(id, std::move(node));
    }

    void remove(const std::string& pattern) {
        std::vector<std::string> ids;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, node] : nodes_) {
                (void)node;
                if (wildcard_match(pattern, id)) {
                    ids.push_back(id);
                }
            }
        }
        for (const auto& id : ids) {
            remove_node(id, true);
        }
    }

    PoolConnection get_connection(const std::string& pattern, PoolSelector selector) {
        while (true) {
            auto id = select_node(pattern, selector, false);
            if (id.empty()) {
                if (matching_node_ids(pattern, true).empty()) {
                    throw Error("pool cluster does not contain a matching pool");
                }
                throw Error("pool cluster has no online matching pool");
            }
            std::shared_ptr<Pool> pool;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = nodes_.find(id);
                if (it == nodes_.end()) {
                    continue;
                }
                pool = it->second.pool;
            }
            try {
                auto connection = pool->get_connection();
                decrease_error_count(id);
                return connection;
            } catch (const Error& error) {
                increase_error_count(id);
                if (options_.can_retry && !matching_node_ids(pattern, false).empty()) {
                    if (emitter_.listenerCount(event::Warn) > 0) {
                        emitter_.emit(event::Warn, error);
                    }
                    continue;
                }
                throw;
            }
        }
    }

    QueryResult query(const std::string& sql) {
        auto connection = get_connection("*", options_.default_selector);
        return connection->query(sql);
    }

    std::vector<QueryResult> query_all(const std::string& sql) {
        auto connection = get_connection("*", options_.default_selector);
        return connection->query_all(sql);
    }

    QueryResult execute(const std::string& sql, const std::vector<Value>& values) {
        auto connection = get_connection("*", options_.default_selector);
        return connection->execute(sql, values);
    }

    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values) {
        auto connection = get_connection("*", options_.default_selector);
        return connection->execute_all(sql, values);
    }

    void end() noexcept {
        std::map<std::string, std::shared_ptr<Pool>> pools;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ && nodes_.empty()) {
                return;
            }
            closed_ = true;
            for (auto& [id, node] : nodes_) {
                pools[id] = node.pool;
            }
            nodes_.clear();
        }
        for (auto& [id, pool] : pools) {
            (void)id;
            if (pool) {
                pool->end();
            }
        }
    }

    std::size_t node_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return nodes_.size();
    }

private:
    struct Node {
        std::string id;
        std::shared_ptr<Pool> pool;
        std::size_t error_count = 0;
        std::chrono::steady_clock::time_point offline_until{};
    };

    std::vector<std::string> matching_node_ids(const std::string& pattern, bool include_offline) const {
        std::vector<std::string> ids;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, node] : nodes_) {
            if (!wildcard_match(pattern, id)) {
                continue;
            }
            if (!include_offline && node.offline_until != std::chrono::steady_clock::time_point{} && node.offline_until > now) {
                continue;
            }
            ids.push_back(id);
        }
        return ids;
    }

    std::string select_node(const std::string& pattern, PoolSelector selector, bool include_offline) {
        auto ids = matching_node_ids(pattern, include_offline);
        if (ids.empty()) {
            return {};
        }
        switch (selector) {
            case PoolSelector::Order:
                return ids.front();
            case PoolSelector::Random: {
                std::uniform_int_distribution<std::size_t> dist(0, ids.size() - 1);
                return ids[dist(rng_)];
            }
            case PoolSelector::RoundRobin:
            default:
                return ids[round_robin_index_++ % ids.size()];
        }
    }

    void increase_error_count(const std::string& id) {
        std::shared_ptr<Pool> removed_pool;
        bool emit_offline = false;
        bool emit_remove = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodes_.find(id);
            if (it == nodes_.end()) {
                return;
            }
            auto& node = it->second;
            ++node.error_count;
            if (node.error_count < options_.remove_node_error_count) {
                return;
            }
            if (options_.restore_node_timeout_ms > 0) {
                node.offline_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(options_.restore_node_timeout_ms);
                emit_offline = true;
            } else {
                removed_pool = node.pool;
                nodes_.erase(it);
                emit_remove = true;
            }
        }
        if (emit_offline) {
            emitter_.emit(event::Offline, id);
        }
        if (emit_remove) {
            if (removed_pool) {
                removed_pool->end();
            }
            emitter_.emit(event::Remove, id);
        }
    }

    void decrease_error_count(const std::string& id) {
        bool emit_online = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodes_.find(id);
            if (it == nodes_.end()) {
                return;
            }
            auto& node = it->second;
            if (node.error_count > 0) {
                --node.error_count;
            }
            if (node.offline_until != std::chrono::steady_clock::time_point{} &&
                node.offline_until <= std::chrono::steady_clock::now()) {
                node.offline_until = {};
                emit_online = true;
            }
        }
        if (emit_online) {
            emitter_.emit(event::Online, id);
        }
    }

    void remove_node(const std::string& id, bool emit_event) {
        std::shared_ptr<Pool> pool;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = nodes_.find(id);
            if (it == nodes_.end()) {
                return;
            }
            pool = it->second.pool;
            nodes_.erase(it);
        }
        if (pool) {
            pool->end();
        }
        if (emit_event) {
            emitter_.emit(event::Remove, id);
        }
    }

    PoolClusterOptions options_;
    events::EventEmitter emitter_;
    mutable std::mutex mutex_;
    std::map<std::string, Node> nodes_;
    std::mt19937 rng_{std::random_device{}()};
    std::size_t round_robin_index_ = 0;
    std::size_t last_id_ = 0;
    bool closed_ = false;
};

PoolNamespace::PoolNamespace() = default;

PoolNamespace::PoolNamespace(std::shared_ptr<PoolClusterImpl> cluster, std::string pattern, PoolSelector selector)
    : cluster_(std::move(cluster)), pattern_(std::move(pattern)), selector_(selector) {}

PoolConnection PoolNamespace::get_connection() {
    if (!cluster_) {
        throw Error("pool namespace is not bound to a cluster");
    }
    return cluster_->get_connection(pattern_, selector_);
}

QueryResult PoolNamespace::query(const std::string& sql) {
    auto connection = get_connection();
    return connection->query(sql);
}

std::vector<QueryResult> PoolNamespace::query_all(const std::string& sql) {
    auto connection = get_connection();
    return connection->query_all(sql);
}

QueryResult PoolNamespace::execute(const std::string& sql, const std::vector<Value>& values) {
    auto connection = get_connection();
    return connection->execute(sql, values);
}

std::vector<QueryResult> PoolNamespace::execute_all(const std::string& sql, const std::vector<Value>& values) {
    auto connection = get_connection();
    return connection->execute_all(sql, values);
}

PoolCluster::PoolCluster(PoolClusterOptions options)
    : impl_(std::make_shared<PoolClusterImpl>(options)) {
    setEmitter_(impl_->emitter());
}

PoolCluster::~PoolCluster() = default;

PoolCluster::PoolCluster(PoolCluster&& other) noexcept : impl_(std::move(other.impl_)) {
    if (impl_) {
        setEmitter_(impl_->emitter());
    }
    other.resetEmitter_();
}

PoolCluster& PoolCluster::operator=(PoolCluster&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (impl_) {
            setEmitter_(impl_->emitter());
        } else {
            resetEmitter_();
        }
        other.resetEmitter_();
    }
    return *this;
}

void PoolCluster::add(PoolOptions options) { impl_->add({}, std::move(options)); }

void PoolCluster::add(std::string id, PoolOptions options) { impl_->add(std::move(id), std::move(options)); }

void PoolCluster::remove(const std::string& pattern) { impl_->remove(pattern); }

PoolConnection PoolCluster::get_connection(const std::string& pattern) {
    return impl_->get_connection(pattern, PoolSelector::RoundRobin);
}

PoolConnection PoolCluster::get_connection(const std::string& pattern, PoolSelector selector) {
    return impl_->get_connection(pattern, selector);
}

PoolNamespace PoolCluster::of(const std::string& pattern, PoolSelector selector) {
    return PoolNamespace(impl_, pattern, selector);
}

QueryResult PoolCluster::query(const std::string& sql) { return impl_->query(sql); }

std::vector<QueryResult> PoolCluster::query_all(const std::string& sql) { return impl_->query_all(sql); }

QueryResult PoolCluster::execute(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute(sql, values);
}

std::vector<QueryResult> PoolCluster::execute_all(const std::string& sql, const std::vector<Value>& values) {
    return impl_->execute_all(sql, values);
}

void PoolCluster::end() { impl_->end(); }

std::size_t PoolCluster::node_count() const noexcept { return impl_ ? impl_->node_count() : 0; }

Connection create_connection(ConnectionOptions options) {
    Connection connection(std::move(options));
    connection.connect();
    return connection;
}

Connection create_connection(const std::string& uri) {
    return create_connection(parse_connection_uri(uri));
}

Promise<std::shared_ptr<Connection>> create_connection_promise(ConnectionOptions options) {
    return promise_from<std::shared_ptr<Connection>>([options = std::move(options)]() mutable {
        auto connection = std::make_shared<Connection>(std::move(options));
        connection->connect();
        return connection;
    });
}

Pool create_pool(PoolOptions options) {
    return Pool(std::move(options));
}

Pool create_pool(const std::string& uri) {
    PoolOptions options;
    options.connection = parse_connection_uri(uri);
    return create_pool(std::move(options));
}

PoolCluster create_pool_cluster(PoolClusterOptions options) {
    return PoolCluster(options);
}

Server create_server(ServerOptions options) {
    return Server(std::move(options));
}

QueryResult query(ConnectionOptions options, const std::string& sql) {
    auto connection = create_connection(std::move(options));
    return connection.query(sql);
}

Promise<QueryResult> query_promise(ConnectionOptions options, const std::string& sql) {
    return promise_from<QueryResult>([options = std::move(options), sql]() mutable {
        return query(std::move(options), sql);
    });
}

}  // namespace polycpp::mysql2
