#ifdef _WIN32
#include "platform/WindowsCredentialStore.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

namespace abyss {

namespace {

std::wstring widen(const std::string &utf8)
{
    if (utf8.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

std::string windowsErrorText(DWORD code)
{
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::string text = "Windows error " + std::to_string(code);
    if (buffer) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
        std::string message(static_cast<size_t>(length > 0 ? length - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, message.data(), length, nullptr, nullptr);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            message.pop_back();
        text += ": " + message;
    }
    return text;
}

} // namespace

bool WindowsCredentialStore::write(const std::string &name, const std::string &secret, std::string &error)
{
    if (name.empty()) {
        error = "Credential name cannot be empty";
        return false;
    }
    if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        error = "The stream key is too long to store";
        return false;
    }

    std::wstring target = widen(name);
    std::wstring user = L"Abyss Stream";
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.UserName = user.data();
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE; // this user, this PC; never roams

    if (!CredWriteW(&credential, 0)) {
        error = "Could not save the stream key to Windows Credential Manager (" + windowsErrorText(GetLastError()) + ")";
        return false;
    }
    return true;
}

std::optional<std::string> WindowsCredentialStore::read(const std::string &name) const
{
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(widen(name).c_str(), CRED_TYPE_GENERIC, 0, &credential))
        return std::nullopt;
    std::string secret(reinterpret_cast<const char *>(credential->CredentialBlob), credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return secret;
}

void WindowsCredentialStore::remove(const std::string &name)
{
    CredDeleteW(widen(name).c_str(), CRED_TYPE_GENERIC, 0);
}

} // namespace abyss
#endif
