#pragma once

#include <cstdint>
#include <string>
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
using SocketHandle = SOCKET;
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <unistd.h>
using SocketHandle = int;
#endif

class TcpClient {
public:
    TcpClient();
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    TcpClient(TcpClient&& other) noexcept;
    TcpClient& operator=(TcpClient&& other) noexcept;

    void connect(const std::string& host, std::uint16_t port);

    std::size_t send(const void* data, std::size_t length);
    std::size_t send(const std::string& data);

    std::string receive(std::size_t max_bytes = 4096);

    void close();
    bool is_connected() const noexcept;

private:
    SocketHandle socket_;
    bool connected_;

    static SocketHandle invalid_socket() noexcept;
    static void initialize_platform();
    static void close_socket(SocketHandle handle) noexcept;

    void reset_socket() noexcept;
    void ensure_connected() const;
};
