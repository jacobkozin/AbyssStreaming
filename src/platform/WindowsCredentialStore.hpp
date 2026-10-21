#pragma once
#ifdef _WIN32

#include "core/SecretStore.hpp"

namespace abyss {

// Stream keys as generic credentials in Windows Credential Manager
// (Control Panel > Credential Manager > Windows Credentials). Windows
// encrypts them with the user's logon credentials (DPAPI), so they never sit
// in a file Abyss Stream writes, and they can be inspected or deleted by hand.
class WindowsCredentialStore final : public ISecretStore {
public:
    bool write(const std::string &name, const std::string &secret, std::string &error) override;
    std::optional<std::string> read(const std::string &name) const override;
    void remove(const std::string &name) override;
};

} // namespace abyss
#endif
