#include "core/SecretStore.hpp"

namespace abyss {

bool MemorySecretStore::write(const std::string &name, const std::string &secret, std::string &error)
{
    if (name.empty()) {
        error = "Secret name cannot be empty";
        return false;
    }
    secrets_[name] = secret;
    return true;
}

std::optional<std::string> MemorySecretStore::read(const std::string &name) const
{
    if (auto it = secrets_.find(name); it != secrets_.end())
        return it->second;
    return std::nullopt;
}

void MemorySecretStore::remove(const std::string &name)
{
    secrets_.erase(name);
}

std::string streamKeySecretName(const std::string &destinationId)
{
    return "AbyssStream/stream-key/" + destinationId;
}

} // namespace abyss
