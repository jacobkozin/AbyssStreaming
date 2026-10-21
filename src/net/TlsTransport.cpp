#include "net/Transport.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <windows.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>

#include <algorithm>
#include <cstring>

namespace abyss::net {

namespace {

std::wstring widen(const std::string &text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string tlsErrorText(SECURITY_STATUS status)
{
    switch (status) {
    case SEC_E_UNTRUSTED_ROOT: return "the server's certificate is not trusted";
    case SEC_E_WRONG_PRINCIPAL: return "the server's certificate is for a different host name";
    case SEC_E_CERT_EXPIRED: return "the server's certificate has expired";
    case SEC_E_ALGORITHM_MISMATCH: return "no TLS version or cipher in common with the server";
    case SEC_E_ILLEGAL_MESSAGE: return "the server sent an invalid TLS message (is this really an rtmps:// port?)";
    default: {
        char text[48];
        std::snprintf(text, sizeof(text), "TLS error 0x%08lX", static_cast<unsigned long>(status));
        return text;
    }
    }
}

bool isLocalhost(const std::string &host)
{
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

class SchannelTransport final : public ITransport {
public:
    explicit SchannelTransport(TlsOptions options) : options_(options) {}
    ~SchannelTransport() override { close(); }

    bool connect(const std::string &host, std::uint16_t port, int timeoutMs, std::string &error) override
    {
        close();
        if (options_.allowUntrustedLocalhost && !isLocalhost(host)) {
            error = "Untrusted TLS is only allowed for localhost test servers";
            return false;
        }
        if (!tcp_.connect(host, port, timeoutMs, error))
            return false;

        SCHANNEL_CRED credentials{};
        credentials.dwVersion = SCHANNEL_CRED_VERSION;
        credentials.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO |
                              (options_.allowUntrustedLocalhost ? SCH_CRED_MANUAL_CRED_VALIDATION
                                                                : SCH_CRED_AUTO_CRED_VALIDATION);
        TimeStamp expiry{};
        SECURITY_STATUS status = AcquireCredentialsHandleW(nullptr, const_cast<wchar_t *>(UNISP_NAME_W),
                                                           SECPKG_CRED_OUTBOUND, nullptr, &credentials, nullptr,
                                                           nullptr, &credentials_, &expiry);
        if (status != SEC_E_OK) {
            error = "Could not start TLS (" + tlsErrorText(status) + ")";
            tcp_.close();
            return false;
        }
        haveCredentials_ = true;

        if (!handshake(widen(host), timeoutMs, error)) {
            close();
            return false;
        }
        status = QueryContextAttributesW(&context_, SECPKG_ATTR_STREAM_SIZES, &sizes_);
        if (status != SEC_E_OK) {
            error = "TLS setup failed (" + tlsErrorText(status) + ")";
            close();
            return false;
        }
        return true;
    }

    bool sendAll(ByteView data, std::string &error) override
    {
        Bytes packet(sizes_.cbHeader + sizes_.cbMaximumMessage + sizes_.cbTrailer);
        size_t offset = 0;
        while (offset < data.size()) {
            const size_t take = std::min<size_t>(data.size() - offset, sizes_.cbMaximumMessage);
            std::memcpy(packet.data() + sizes_.cbHeader, data.data() + offset, take);

            SecBuffer buffers[4] = {
                {sizes_.cbHeader, SECBUFFER_STREAM_HEADER, packet.data()},
                {static_cast<unsigned long>(take), SECBUFFER_DATA, packet.data() + sizes_.cbHeader},
                {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER, packet.data() + sizes_.cbHeader + take},
                {0, SECBUFFER_EMPTY, nullptr},
            };
            SecBufferDesc description{SECBUFFER_VERSION, 4, buffers};
            const SECURITY_STATUS status = EncryptMessage(&context_, 0, &description, 0);
            if (status != SEC_E_OK) {
                error = "TLS encryption failed (" + tlsErrorText(status) + ")";
                return false;
            }
            const size_t total = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
            if (!tcp_.sendAll({packet.data(), total}, error))
                return false;
            offset += take;
        }
        return true;
    }

    int receive(std::uint8_t *buffer, size_t size, int timeoutMs, std::string &error) override
    {
        while (plain_.empty()) {
            const int decrypted = decryptBuffered(error);
            if (decrypted < 0)
                return -1;
            if (decrypted > 0)
                break;
            // Need more ciphertext from the socket.
            std::uint8_t chunk[16384];
            const int received = tcp_.receive(chunk, sizeof(chunk), timeoutMs, error);
            if (received <= 0)
                return received;
            incoming_.insert(incoming_.end(), chunk, chunk + received);
        }
        const size_t take = std::min(size, plain_.size());
        std::memcpy(buffer, plain_.data(), take);
        plain_.erase(plain_.begin(), plain_.begin() + static_cast<std::ptrdiff_t>(take));
        return static_cast<int>(take);
    }

    void close() override
    {
        if (haveContext_) {
            sendCloseNotify();
            DeleteSecurityContext(&context_);
            haveContext_ = false;
        }
        if (haveCredentials_) {
            FreeCredentialsHandle(&credentials_);
            haveCredentials_ = false;
        }
        tcp_.close();
        incoming_.clear();
        plain_.clear();
    }

private:
    static constexpr unsigned long kRequestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                                                   ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
                                                   ISC_REQ_STREAM | ISC_REQ_USE_SUPPLIED_CREDS;

    bool sendToken(SecBuffer &token, std::string &error)
    {
        bool ok = true;
        if (token.cbBuffer > 0 && token.pvBuffer)
            ok = tcp_.sendAll({static_cast<const std::uint8_t *>(token.pvBuffer), token.cbBuffer}, error);
        if (token.pvBuffer)
            FreeContextBuffer(token.pvBuffer);
        token.pvBuffer = nullptr;
        token.cbBuffer = 0;
        return ok;
    }

    bool handshake(const std::wstring &host, int timeoutMs, std::string &error)
    {
        unsigned long contextFlags = 0;
        TimeStamp expiry{};
        SecBuffer outToken{0, SECBUFFER_TOKEN, nullptr};
        SecBufferDesc outDescription{SECBUFFER_VERSION, 1, &outToken};

        SECURITY_STATUS status = InitializeSecurityContextW(
            &credentials_, nullptr, const_cast<wchar_t *>(host.c_str()), kRequestFlags, 0, 0, nullptr, 0, &context_,
            &outDescription, &contextFlags, &expiry);
        if (status != SEC_I_CONTINUE_NEEDED) {
            error = "TLS handshake failed (" + tlsErrorText(status) + ")";
            return false;
        }
        haveContext_ = true;
        if (!sendToken(outToken, error))
            return false;

        incoming_.clear();
        bool needMoreData = true;
        while (true) {
            if (needMoreData) {
                std::uint8_t chunk[16384];
                const int received = tcp_.receive(chunk, sizeof(chunk), timeoutMs, error);
                if (received <= 0) {
                    if (received == 0)
                        error = "TLS handshake timed out";
                    return false;
                }
                incoming_.insert(incoming_.end(), chunk, chunk + received);
            }

            SecBuffer inBuffers[2] = {
                {static_cast<unsigned long>(incoming_.size()), SECBUFFER_TOKEN, incoming_.data()},
                {0, SECBUFFER_EMPTY, nullptr},
            };
            SecBufferDesc inDescription{SECBUFFER_VERSION, 2, inBuffers};
            outToken = {0, SECBUFFER_TOKEN, nullptr};
            outDescription = {SECBUFFER_VERSION, 1, &outToken};

            status = InitializeSecurityContextW(&credentials_, &context_, const_cast<wchar_t *>(host.c_str()),
                                                kRequestFlags, 0, 0, &inDescription, 0, nullptr, &outDescription,
                                                &contextFlags, &expiry);

            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                needMoreData = true;
                continue;
            }
            if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK ||
                (FAILED(status) && (contextFlags & ISC_RET_EXTENDED_ERROR))) {
                if (!sendToken(outToken, error))
                    return false;
            }
            if (status == SEC_I_INCOMPLETE_CREDENTIALS) {
                // The server asked for a client certificate; carry on without one.
                needMoreData = false;
                continue;
            }
            if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK) {
                error = "TLS handshake failed (" + tlsErrorText(status) + ")";
                return false;
            }

            // Keep any bytes the handshake did not consume (they may already
            // be the first encrypted application data).
            if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
                Bytes extra(incoming_.end() - inBuffers[1].cbBuffer, incoming_.end());
                incoming_ = std::move(extra);
                needMoreData = false;
            } else {
                incoming_.clear();
                needMoreData = true;
            }
            if (status == SEC_E_OK)
                return true;
        }
    }

    // 1: plaintext added, 0: need more ciphertext, -1: closed or failed.
    int decryptBuffered(std::string &error)
    {
        if (incoming_.empty())
            return 0;
        SecBuffer buffers[4] = {
            {static_cast<unsigned long>(incoming_.size()), SECBUFFER_DATA, incoming_.data()},
            {0, SECBUFFER_EMPTY, nullptr},
            {0, SECBUFFER_EMPTY, nullptr},
            {0, SECBUFFER_EMPTY, nullptr},
        };
        SecBufferDesc description{SECBUFFER_VERSION, 4, buffers};
        const SECURITY_STATUS status = DecryptMessage(&context_, &description, 0, nullptr);
        if (status == SEC_E_INCOMPLETE_MESSAGE)
            return 0;
        if (status == SEC_I_CONTEXT_EXPIRED) {
            error = "The server closed the connection";
            return -1;
        }
        if (status != SEC_E_OK) {
            error = status == SEC_I_RENEGOTIATE ? "The server asked to renegotiate TLS, which is not supported"
                                                : "TLS decryption failed (" + tlsErrorText(status) + ")";
            return -1;
        }

        Bytes extra;
        for (const SecBuffer &buffer : buffers) {
            if (buffer.BufferType == SECBUFFER_DATA && buffer.cbBuffer > 0) {
                const auto *data = static_cast<const std::uint8_t *>(buffer.pvBuffer);
                plain_.insert(plain_.end(), data, data + buffer.cbBuffer);
            } else if (buffer.BufferType == SECBUFFER_EXTRA && buffer.cbBuffer > 0) {
                const auto *data = static_cast<const std::uint8_t *>(buffer.pvBuffer);
                extra.assign(data, data + buffer.cbBuffer);
            }
        }
        incoming_ = std::move(extra);
        return plain_.empty() ? (incoming_.empty() ? 0 : decryptBuffered(error)) : 1;
    }

    void sendCloseNotify()
    {
        DWORD shutdownToken = SCHANNEL_SHUTDOWN;
        SecBuffer tokenBuffer{sizeof(shutdownToken), SECBUFFER_TOKEN, &shutdownToken};
        SecBufferDesc tokenDescription{SECBUFFER_VERSION, 1, &tokenBuffer};
        if (ApplyControlToken(&context_, &tokenDescription) != SEC_E_OK)
            return;
        SecBuffer outToken{0, SECBUFFER_TOKEN, nullptr};
        SecBufferDesc outDescription{SECBUFFER_VERSION, 1, &outToken};
        unsigned long contextFlags = 0;
        TimeStamp expiry{};
        InitializeSecurityContextW(&credentials_, &context_, nullptr, kRequestFlags, 0, 0, nullptr, 0, nullptr,
                                   &outDescription, &contextFlags, &expiry);
        std::string ignored;
        sendToken(outToken, ignored);
    }

    TlsOptions options_;
    TcpTransport tcp_;
    CredHandle credentials_{};
    CtxtHandle context_{};
    bool haveCredentials_{false};
    bool haveContext_{false};
    SecPkgContext_StreamSizes sizes_{};
    Bytes incoming_; // ciphertext not yet decrypted
    Bytes plain_;    // decrypted bytes not yet returned
};

} // namespace

std::unique_ptr<ITransport> makeTlsTransport(const TlsOptions &options)
{
    return std::make_unique<SchannelTransport>(options);
}

} // namespace abyss::net

#else

namespace abyss::net {
std::unique_ptr<ITransport> makeTlsTransport(const TlsOptions &)
{
    return nullptr; // rtmps:// is only supported on Windows (Schannel)
}
} // namespace abyss::net

#endif
