#include "net/Transport.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <mutex>

namespace abyss::net {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kNoSocket = INVALID_SOCKET;

void ensureWinsock()
{
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

int lastSocketError() { return WSAGetLastError(); }
void closeNative(NativeSocket s) { closesocket(s); }
#else
using NativeSocket = int;
constexpr NativeSocket kNoSocket = -1;
void ensureWinsock() {}
int lastSocketError() { return errno; }
void closeNative(NativeSocket s) { ::close(s); }
#endif

NativeSocket native(std::intptr_t value) { return static_cast<NativeSocket>(value); }

std::string socketErrorText(int code)
{
#ifdef _WIN32
    switch (code) {
    case WSAECONNREFUSED: return "connection refused (is the server running and the port right?)";
    case WSAETIMEDOUT: return "timed out";
    case WSAECONNRESET: return "connection reset by the server";
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH: return "network unreachable";
    default: return "socket error " + std::to_string(code);
    }
#else
    return std::strerror(code);
#endif
}

void setBlocking(NativeSocket s, bool blocking)
{
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif
}

// Waits until the socket is readable (or writable). 1 ready, 0 timeout, -1 error.
int waitFor(NativeSocket s, bool write, int timeoutMs)
{
#ifdef _WIN32
    WSAPOLLFD fd{};
    fd.fd = s;
    fd.events = write ? POLLWRNORM : POLLRDNORM;
    const int result = WSAPoll(&fd, 1, timeoutMs);
#else
    pollfd fd{};
    fd.fd = s;
    fd.events = write ? POLLOUT : POLLIN;
    const int result = ::poll(&fd, 1, timeoutMs);
#endif
    if (result < 0)
        return -1;
    if (result == 0)
        return 0;
    // A refused connect shows up as an error/hang-up event; a hang-up while
    // reading still has buffered data to deliver.
    if (write && (fd.revents & (POLLERR | POLLHUP)))
        return -1;
    return 1;
}

} // namespace

TcpTransport::TcpTransport() : socket_(static_cast<std::intptr_t>(kNoSocket))
{
    ensureWinsock();
}

TcpTransport::~TcpTransport()
{
    close();
}

bool TcpTransport::connect(const std::string &host, std::uint16_t port, int timeoutMs, std::string &error)
{
    close();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo *addresses = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0 || !addresses) {
        error = "Could not resolve " + host + " (check the server name and your internet connection)";
        return false;
    }

    error.clear();
    for (addrinfo *address = addresses; address; address = address->ai_next) {
        NativeSocket s = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (s == kNoSocket)
            continue;
        setBlocking(s, false);
        const int result = ::connect(s, address->ai_addr, static_cast<int>(address->ai_addrlen));
        bool connected = result == 0;
        if (!connected) {
#ifdef _WIN32
            const bool pending = lastSocketError() == WSAEWOULDBLOCK;
#else
            const bool pending = lastSocketError() == EINPROGRESS;
#endif
            if (pending && waitFor(s, true, timeoutMs) == 1) {
                int socketError = 0;
                socklen_t length = sizeof(socketError);
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socketError), &length);
                connected = socketError == 0;
                if (!connected)
                    error = socketErrorText(socketError);
            } else {
                error = pending ? "timed out" : socketErrorText(lastSocketError());
            }
        }
        if (connected) {
            setBlocking(s, true);
            int noDelay = 1; // RTMP sends many small messages; do not let Nagle batch them
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
            socket_ = static_cast<std::intptr_t>(s);
            freeaddrinfo(addresses);
            setSendTimeout(10000);
            return true;
        }
        closeNative(s);
    }
    freeaddrinfo(addresses);
    error = "Could not connect to " + host + ":" + service + " (" + (error.empty() ? "unknown error" : error) + ")";
    return false;
}

void TcpTransport::setSendTimeout(int timeoutMs)
{
    if (native(socket_) == kNoSocket)
        return;
#ifdef _WIN32
    DWORD value = static_cast<DWORD>(timeoutMs);
    setsockopt(native(socket_), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&value), sizeof(value));
#else
    timeval value{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    setsockopt(native(socket_), SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value));
#endif
}

bool TcpTransport::sendAll(ByteView data, std::string &error)
{
    size_t sent = 0;
    while (sent < data.size()) {
        const int chunk = static_cast<int>(std::min<size_t>(data.size() - sent, 1 << 20));
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL; // a dropped connection must not raise SIGPIPE
#else
        constexpr int flags = 0;
#endif
        const auto result =
            ::send(native(socket_), reinterpret_cast<const char *>(data.data() + sent), chunk, flags);
        if (result <= 0) {
            error = "Lost the connection while sending (" + socketErrorText(lastSocketError()) + ")";
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

int TcpTransport::receive(std::uint8_t *buffer, size_t size, int timeoutMs, std::string &error)
{
    const int ready = waitFor(native(socket_), false, timeoutMs);
    if (ready == 0)
        return 0;
    if (ready < 0) {
        error = "Connection error (" + socketErrorText(lastSocketError()) + ")";
        return -1;
    }
    const auto result = ::recv(native(socket_), reinterpret_cast<char *>(buffer), static_cast<int>(size), 0);
    if (result == 0) {
        error = "The server closed the connection";
        return -1;
    }
    if (result < 0) {
        error = "Connection error (" + socketErrorText(lastSocketError()) + ")";
        return -1;
    }
    return static_cast<int>(result);
}

void TcpTransport::close()
{
    if (native(socket_) != kNoSocket) {
        closeNative(native(socket_));
        socket_ = static_cast<std::intptr_t>(kNoSocket);
    }
}

std::unique_ptr<ITransport> makeTransport(bool secure, const TlsOptions &options)
{
    if (secure)
        return makeTlsTransport(options);
    return std::make_unique<TcpTransport>();
}

} // namespace abyss::net
