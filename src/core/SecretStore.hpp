#pragma once

#include <map>
#include <optional>
#include <string>

namespace abyss {

// Where stream keys are persisted. The Windows build uses Credential Manager
// (see platform/WindowsCredentialStore); tests and non-Windows builds use the
// in-memory store, which forgets everything when the process exits.
class ISecretStore {
public:
    virtual ~ISecretStore() = default;
    virtual bool write(const std::string &name, const std::string &secret, std::string &error) = 0;
    virtual std::optional<std::string> read(const std::string &name) const = 0;
    virtual void remove(const std::string &name) = 0;
};

class MemorySecretStore final : public ISecretStore {
public:
    bool write(const std::string &name, const std::string &secret, std::string &error) override;
    std::optional<std::string> read(const std::string &name) const override;
    void remove(const std::string &name) override;

private:
    std::map<std::string, std::string> secrets_;
};

// The secret name used for a destination's stream key.
std::string streamKeySecretName(const std::string &destinationId);

} // namespace abyss
