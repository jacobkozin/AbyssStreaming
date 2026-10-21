#include "core/LogRedactor.hpp"

#include <algorithm>

namespace abyss {

void LogRedactor::addSecret(const std::string &secret)
{
    if (secret.size() < kMinimumSecretLength)
        return;
    std::scoped_lock lock(mutex_);
    if (std::find(secrets_.begin(), secrets_.end(), secret) == secrets_.end())
        secrets_.push_back(secret);
    // Longest first, so a key that contains another key is masked whole.
    std::sort(secrets_.begin(), secrets_.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
}

void LogRedactor::removeSecret(const std::string &secret)
{
    std::scoped_lock lock(mutex_);
    secrets_.erase(std::remove(secrets_.begin(), secrets_.end(), secret), secrets_.end());
}

void LogRedactor::clear()
{
    std::scoped_lock lock(mutex_);
    secrets_.clear();
}

std::string LogRedactor::redact(std::string_view text) const
{
    std::string result(text);
    std::scoped_lock lock(mutex_);
    for (const auto &secret : secrets_) {
        size_t pos = 0;
        while ((pos = result.find(secret, pos)) != std::string::npos) {
            result.replace(pos, secret.size(), kMask);
            pos += kMask.size();
        }
    }
    return result;
}

LogRedactor &logRedactor()
{
    static LogRedactor instance;
    return instance;
}

} // namespace abyss
