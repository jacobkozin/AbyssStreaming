#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace abyss {

// Masks known secrets (stream keys) in log text. libobs never logs a key on
// purpose, but a key pasted into a server URL, or echoed back in a server
// error, would otherwise end up in AbyssStream-obs.log. Thread-safe: libobs
// logs from its own threads.
class LogRedactor {
public:
    static constexpr std::string_view kMask = "********";
    // Shorter strings are ignored so ordinary words are never masked.
    static constexpr size_t kMinimumSecretLength = 4;

    void addSecret(const std::string &secret);
    void removeSecret(const std::string &secret);
    void clear();
    std::string redact(std::string_view text) const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> secrets_;
};

// The process-wide redactor used by the OBS log handler.
LogRedactor &logRedactor();

} // namespace abyss
