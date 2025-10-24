#include "tcp_client.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

#ifdef _WIN32
int last_socket_error() noexcept {
    return WSAGetLastError();
}

std::string format_network_error(int code) {
    return "WSA error " + std::to_string(code);
}
#else
int last_socket_error() noexcept {
    return errno;
}

std::string format_network_error(int code) {
    return std::strerror(code);
}
#endif

}  // namespace

TcpClient::TcpClient() : socket_(invalid_socket()), connected_(false) {
    initialize_platform();
}

TcpClient::~TcpClient() {
    close();
}

TcpClient::TcpClient(TcpClient&& other) noexcept : socket_(other.socket_), connected_(other.connected_) {
    other.reset_socket();
}

TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        connected_ = other.connected_;
        other.reset_socket();
    }
    return *this;
}

void TcpClient::connect(const std::string& host, std::uint16_t port) {
    if (host.empty()) {
        throw std::invalid_argument("host must not be empty");
    }

    close();

    initialize_platform();

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto service = std::to_string(port);
    struct addrinfo* results = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &results);
    if (rc != 0 || results == nullptr) {
#ifdef _WIN32
        const std::string detail = results == nullptr ? "no addresses resolved" : gai_strerrorA(rc);
#else
        const std::string detail = results == nullptr ? "no addresses resolved" : gai_strerror(rc);
#endif
        throw std::runtime_error("getaddrinfo failed: " + detail);
    }

    SocketHandle handle = invalid_socket();

    for (auto* ptr = results; ptr != nullptr; ptr = ptr->ai_next) {
        handle = static_cast<SocketHandle>(::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol));
        if (handle == invalid_socket()) {
            continue;
        }

        const int connect_result =
#ifdef _WIN32
            ::connect(handle, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
#else
            ::connect(handle, ptr->ai_addr, ptr->ai_addrlen);
#endif

        if (connect_result == 0) {
            break;
        }

        close_socket(handle);
        handle = invalid_socket();
    }

    ::freeaddrinfo(results);

    if (handle == invalid_socket()) {
        const int code = last_socket_error();
        throw std::runtime_error("connect failed: " + format_network_error(code));
    }

    socket_ = handle;
    connected_ = true;
}

std::size_t TcpClient::send(const void* data, std::size_t length) {
    ensure_connected();

    if (data == nullptr && length > 0) {
        throw std::invalid_argument("data pointer must not be null when length > 0");
    }

    const auto* bytes = static_cast<const char*>(data);
    std::size_t total_sent = 0;

    while (total_sent < length) {
        const std::size_t remaining = length - total_sent;
        const int chunk = remaining > static_cast<std::size_t>(std::numeric_limits<int>::max())
                              ? std::numeric_limits<int>::max()
                              : static_cast<int>(remaining);
        const int sent = ::send(socket_, bytes + total_sent, chunk, 0);
        if (sent <= 0) {
            const int code = last_socket_error();
            throw std::runtime_error("send failed: " + format_network_error(code));
        }
        total_sent += static_cast<std::size_t>(sent);
    }

    return total_sent;
}

std::size_t TcpClient::send(const std::string& data) {
    if (data.empty()) {
        return 0;
    }
    return send(data.data(), data.size());
}

std::string TcpClient::receive(std::size_t max_bytes) {
    ensure_connected();

    if (max_bytes == 0) {
        return {};
    }

    std::string buffer;
    buffer.resize(max_bytes);

#ifdef _WIN32
    const int recv_len = max_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())
                             ? std::numeric_limits<int>::max()
                             : static_cast<int>(max_bytes);
    const int received = ::recv(socket_, buffer.data(), recv_len, 0);

    if (received > 0) {
        buffer.resize(static_cast<std::size_t>(received));
        return buffer;
    }

    if (received == 0) {
        close();
        return {};
    }

    const int code = last_socket_error();
    throw std::runtime_error("receive failed: " + format_network_error(code));
#else
    const auto received = ::recv(socket_, buffer.data(), max_bytes, 0);
    if (received > 0) {
        buffer.resize(static_cast<std::size_t>(received));
        return buffer;
    }
    if (received == 0) {
        close();
        return {};
    }
    const int code = last_socket_error();
    throw std::runtime_error("receive failed: " + format_network_error(code));
#endif
}

void TcpClient::close() {
    if (socket_ != invalid_socket()) {
        close_socket(socket_);
    }
    reset_socket();
}

bool TcpClient::is_connected() const noexcept {
    return connected_;
}

SocketHandle TcpClient::invalid_socket() noexcept {
#ifdef _WIN32
    return INVALID_SOCKET;
#else
    return -1;
#endif
}

void TcpClient::initialize_platform() {
#ifdef _WIN32
    struct WinsockInitializer {
        WinsockInitializer() {
            WSADATA data {};
            const int rc = WSAStartup(MAKEWORD(2, 2), &data);
            if (rc != 0) {
                throw std::runtime_error("WSAStartup failed with error " + std::to_string(rc));
            }
        }

        ~WinsockInitializer() {
            WSACleanup();
        }
    };

    static WinsockInitializer initializer;
    (void)initializer;
#endif
}

void TcpClient::close_socket(SocketHandle handle) noexcept {
    if (handle == invalid_socket()) {
        return;
    }
#ifdef _WIN32
    ::closesocket(handle);
#else
    ::close(handle);
#endif
}

void TcpClient::reset_socket() noexcept {
    socket_ = invalid_socket();
    connected_ = false;
}

void TcpClient::ensure_connected() const {
    if (!connected_ || socket_ == invalid_socket()) {
        throw std::logic_error("socket is not connected");
    }
}
