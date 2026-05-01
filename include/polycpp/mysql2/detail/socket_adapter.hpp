#pragma once

// Internal-only adapter. polycpp HEAD removed the bundled
// polycpp/io/{stream,pipe}_{socket,acceptor}.hpp helpers that mysql2 relied on
// to abstract over TCP vs. AF_UNIX transports. We re-implement the small slice
// mysql2 actually uses on top of asio (TCP path delegates to the still-public
// polycpp::io::TcpSocket / TcpAcceptor; AF_UNIX path is a thin asio
// local::stream_protocol wrapper).
//
// This is private to the mysql2 port — do not include from public headers.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

#include <asio.hpp>

#include <polycpp/io/event_context.hpp>
#include <polycpp/io/tcp_acceptor.hpp>
#include <polycpp/io/tcp_socket.hpp>
#include <polycpp/io/detail/asio/event_context_tmpl.hpp>

namespace polycpp::mysql2::detail::transport {

namespace asio_local = ::asio::local;

class PipeSocket;
class PipeAcceptor;

class PipeSocket {
public:
    explicit PipeSocket(::polycpp::EventContext& ctx)
        : socket_(::polycpp::io::ContextAccess::native(ctx)) {}

    PipeSocket(PipeSocket&&) noexcept = default;
    PipeSocket& operator=(PipeSocket&&) noexcept = default;
    PipeSocket(const PipeSocket&) = delete;
    PipeSocket& operator=(const PipeSocket&) = delete;

    void asyncConnect(const std::string& path,
                      std::function<void(std::error_code)> handler) {
        socket_.async_connect(asio_local::stream_protocol::endpoint(path),
                              [h = std::move(handler)](const std::error_code& ec) {
                                  h(ec);
                              });
    }

    void asyncReadSome(void* buf, std::size_t maxLen,
                       std::function<void(std::error_code, std::size_t)> handler) {
        socket_.async_read_some(::asio::buffer(buf, maxLen),
                                [h = std::move(handler)](const std::error_code& ec,
                                                         std::size_t n) { h(ec, n); });
    }

    void asyncRead(void* buf, std::size_t len,
                   std::function<void(std::error_code, std::size_t)> handler) {
        ::asio::async_read(socket_, ::asio::buffer(buf, len),
                           [h = std::move(handler)](const std::error_code& ec,
                                                    std::size_t n) { h(ec, n); });
    }

    void asyncWrite(const void* data, std::size_t len,
                    std::function<void(std::error_code, std::size_t)> handler) {
        ::asio::async_write(socket_, ::asio::buffer(data, len),
                            [h = std::move(handler)](const std::error_code& ec,
                                                     std::size_t n) { h(ec, n); });
    }

    bool isOpen() const { return socket_.is_open(); }

    void close(std::error_code& ec) {
        if (socket_.is_open()) {
            socket_.close(ec);
        } else {
            ec.clear();
        }
    }

    void shutdown(int type, std::error_code& ec) {
        ::asio::socket_base::shutdown_type st;
        switch (type) {
            case 0: st = ::asio::socket_base::shutdown_receive; break;
            case 1: st = ::asio::socket_base::shutdown_send; break;
            default: st = ::asio::socket_base::shutdown_both; break;
        }
        if (socket_.is_open()) {
            socket_.shutdown(st, ec);
        } else {
            ec.clear();
        }
    }

    void unref() { /* asio local socket has no ref-count semantics */ }

    std::string remotePath() const {
        std::error_code ec;
        auto ep = socket_.remote_endpoint(ec);
        return ec ? std::string{} : ep.path();
    }

    asio_local::stream_protocol::socket& native() { return socket_; }

private:
    friend class PipeAcceptor;
    explicit PipeSocket(asio_local::stream_protocol::socket sock)
        : socket_(std::move(sock)) {}

    asio_local::stream_protocol::socket socket_;
};

class PipeAcceptor {
public:
    explicit PipeAcceptor(::polycpp::EventContext& ctx)
        : ctx_(::polycpp::io::ContextAccess::native(ctx)),
          acceptor_(::polycpp::io::ContextAccess::native(ctx)) {}

    PipeAcceptor(PipeAcceptor&&) noexcept = default;
    PipeAcceptor& operator=(PipeAcceptor&&) noexcept = default;
    PipeAcceptor(const PipeAcceptor&) = delete;
    PipeAcceptor& operator=(const PipeAcceptor&) = delete;

    void open() {
        std::error_code ec;
        acceptor_.open(asio_local::stream_protocol(), ec);
        if (ec) {
            throw std::system_error(ec, "PipeAcceptor::open");
        }
    }

    void bind(const std::string& path) {
        // Remove any stale socket file. asio refuses to bind otherwise.
        std::error_code ignored;
        ::asio::local::stream_protocol::endpoint ep(path);
        std::remove(path.c_str());
        acceptor_.bind(ep, ignored);
        if (ignored) {
            throw std::system_error(ignored, "PipeAcceptor::bind");
        }
        bound_path_ = path;
    }

    void listen(int backlog) {
        std::error_code ec;
        acceptor_.listen(backlog == 0 ? ::asio::socket_base::max_listen_connections : backlog, ec);
        if (ec) {
            throw std::system_error(ec, "PipeAcceptor::listen");
        }
    }

    std::string localPath() const { return bound_path_; }

    void asyncAccept(std::function<void(std::error_code, PipeSocket)> handler) {
        auto sock = std::make_shared<asio_local::stream_protocol::socket>(ctx_);
        acceptor_.async_accept(*sock,
                               [sock, h = std::move(handler)](const std::error_code& ec) {
                                   h(ec, PipeSocket(std::move(*sock)));
                               });
    }

    void close(std::error_code& ec) {
        if (acceptor_.is_open()) {
            acceptor_.close(ec);
        } else {
            ec.clear();
        }
        if (!bound_path_.empty()) {
            std::remove(bound_path_.c_str());
        }
    }

private:
    ::asio::io_context& ctx_;
    asio_local::stream_protocol::acceptor acceptor_;
    std::string bound_path_;
};

// Tagged union of a TcpSocket or a PipeSocket. Mimics the surface that the old
// polycpp `io::StreamSocket` provided to mysql2.
class StreamSocket {
public:
    explicit StreamSocket(::polycpp::EventContext& ctx)
        : variant_(::polycpp::io::TcpSocket(ctx)) {}

    StreamSocket(::polycpp::io::TcpSocket sock)
        : variant_(std::move(sock)) {}

    StreamSocket(PipeSocket sock)
        : variant_(std::move(sock)) {}

    StreamSocket(StreamSocket&&) noexcept = default;
    StreamSocket& operator=(StreamSocket&&) noexcept = default;
    StreamSocket(const StreamSocket&) = delete;
    StreamSocket& operator=(const StreamSocket&) = delete;

    bool isTcp() const noexcept { return std::holds_alternative<::polycpp::io::TcpSocket>(variant_); }
    bool isPipe() const noexcept { return std::holds_alternative<PipeSocket>(variant_); }

    ::polycpp::io::TcpSocket* tcp() noexcept {
        return std::get_if<::polycpp::io::TcpSocket>(&variant_);
    }
    const ::polycpp::io::TcpSocket* tcp() const noexcept {
        return std::get_if<::polycpp::io::TcpSocket>(&variant_);
    }
    PipeSocket* pipe() noexcept {
        return std::get_if<PipeSocket>(&variant_);
    }
    const PipeSocket* pipe() const noexcept {
        return std::get_if<PipeSocket>(&variant_);
    }

    /// Move out the underlying TcpSocket — used by the TLS upgrade path.
    /// UB if the StreamSocket does not currently hold a TcpSocket.
    ::polycpp::io::TcpSocket releaseTcp() {
        return std::move(std::get<::polycpp::io::TcpSocket>(variant_));
    }

    bool isOpen() const {
        return std::visit([](const auto& s) { return s.isOpen(); }, variant_);
    }

    void close(std::error_code& ec) {
        std::visit([&](auto& s) { s.close(ec); }, variant_);
    }

    void shutdown(int type, std::error_code& ec) {
        std::visit([&](auto& s) { s.shutdown(type, ec); }, variant_);
    }

    void unref() {
        std::visit([](auto& s) { s.unref(); }, variant_);
    }

    void setNoDelay(bool enable, std::error_code& ec) {
        if (auto* t = tcp()) {
            t->setNoDelay(enable, ec);
        } else {
            ec.clear();
        }
    }

    void setKeepAlive(bool enable, std::error_code& ec) {
        if (auto* t = tcp()) {
            t->setKeepAlive(enable, ec);
        } else {
            ec.clear();
        }
    }

    void asyncRead(void* buf, std::size_t len,
                   std::function<void(std::error_code, std::size_t)> handler) {
        std::visit([&](auto& s) { s.asyncRead(buf, len, std::move(handler)); }, variant_);
    }

    void asyncReadSome(void* buf, std::size_t maxLen,
                       std::function<void(std::error_code, std::size_t)> handler) {
        std::visit([&](auto& s) { s.asyncReadSome(buf, maxLen, std::move(handler)); }, variant_);
    }

    void asyncWrite(const void* data, std::size_t len,
                    std::function<void(std::error_code, std::size_t)> handler) {
        std::visit([&](auto& s) { s.asyncWrite(data, len, std::move(handler)); }, variant_);
    }

private:
    std::variant<::polycpp::io::TcpSocket, PipeSocket> variant_;
};

// Tagged union of a TcpAcceptor or a PipeAcceptor. Mimics the old
// polycpp `io::StreamAcceptor`.
class StreamAcceptor {
public:
    StreamAcceptor(::polycpp::io::TcpAcceptor acceptor)
        : variant_(std::move(acceptor)) {}

    StreamAcceptor(PipeAcceptor acceptor)
        : variant_(std::move(acceptor)) {}

    StreamAcceptor(StreamAcceptor&&) noexcept = default;
    StreamAcceptor& operator=(StreamAcceptor&&) noexcept = default;
    StreamAcceptor(const StreamAcceptor&) = delete;
    StreamAcceptor& operator=(const StreamAcceptor&) = delete;

    void asyncAccept(std::function<void(std::error_code, StreamSocket)> handler) {
        if (auto* tcp = std::get_if<::polycpp::io::TcpAcceptor>(&variant_)) {
            tcp->asyncAccept([h = std::move(handler)](std::error_code ec,
                                                       ::polycpp::io::TcpSocket sock) {
                h(ec, StreamSocket(std::move(sock)));
            });
            return;
        }
        auto& pipe = std::get<PipeAcceptor>(variant_);
        pipe.asyncAccept([h = std::move(handler)](std::error_code ec, PipeSocket sock) {
            h(ec, StreamSocket(std::move(sock)));
        });
    }

    void close(std::error_code& ec) {
        if (auto* tcp = std::get_if<::polycpp::io::TcpAcceptor>(&variant_)) {
            tcp->close(ec);
            return;
        }
        std::get<PipeAcceptor>(variant_).close(ec);
    }

private:
    std::variant<::polycpp::io::TcpAcceptor, PipeAcceptor> variant_;
};

}  // namespace polycpp::mysql2::detail::transport
