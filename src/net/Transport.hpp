#pragma once

#include "net/Bytes.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace abyss::net {

// A byte pipe to an RTMP server: plain TCP, or TCP wrapped in TLS for rtmps.
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool connect(const std::string &host, std::uint16_t port, int timeoutMs, std::string &error) = 0;
    virtual bool sendAll(ByteView data, std::string &error) = 0;
    // > 0: bytes received; 0: nothing arrived before the timeout;
    // < 0: the connection closed or failed (error says which).
    virtual int receive(std::uint8_t *buffer, size_t size, int timeoutMs, std::string &error) = 0;
    virtual void close() = 0;
};

class TcpTransport final : public ITransport {
public:
    TcpTransport();
    ~TcpTransport() override;
    TcpTransport(const TcpTransport &) = delete;
    TcpTransport &operator=(const TcpTransport &) = delete;

    bool connect(const std::string &host, std::uint16_t port, int timeoutMs, std::string &error) override;
    bool sendAll(ByteView data, std::string &error) override;
    int receive(std::uint8_t *buffer, size_t size, int timeoutMs, std::string &error) override;
    void close() override;

    // Bounds how long one send may block, so a stalled connection is noticed.
    void setSendTimeout(int timeoutMs);

private:
    std::intptr_t socket_;
};

struct TlsOptions {
    // Only for local tests against a self-signed server. Refused for any
    // host other than localhost/127.0.0.1, so it cannot weaken real streams.
    bool allowUntrustedLocalhost{false};
};

// rtmps:// support. On Windows this is Schannel, the TLS stack built into
// the OS, so certificates are checked against the Windows trust store.
// Returns nullptr where TLS is not available.
std::unique_ptr<ITransport> makeTlsTransport(const TlsOptions &options = {});
std::unique_ptr<ITransport> makeTransport(bool secure, const TlsOptions &options = {});

} // namespace abyss::net
