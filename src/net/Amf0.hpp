#pragma once

#include "net/Bytes.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace abyss::net {

// AMF0, the serialization RTMP uses for commands ("connect", "publish") and
// stream metadata.
struct Amf0Value;
using Amf0Object = std::map<std::string, Amf0Value>;
using Amf0Array = std::vector<Amf0Value>;

struct Amf0Null {
    bool operator==(const Amf0Null &) const = default;
};

struct Amf0Value {
    std::variant<Amf0Null, double, bool, std::string, std::shared_ptr<Amf0Object>, std::shared_ptr<Amf0Array>> value;

    Amf0Value() = default;
    Amf0Value(std::nullptr_t) {}
    Amf0Value(double number) : value(number) {}
    Amf0Value(int number) : value(static_cast<double>(number)) {}
    Amf0Value(bool flag) : value(flag) {}
    Amf0Value(const char *text) : value(std::string(text)) {}
    Amf0Value(std::string text) : value(std::move(text)) {}
    Amf0Value(Amf0Object object) : value(std::make_shared<Amf0Object>(std::move(object))) {}

    [[nodiscard]] bool isNull() const { return std::holds_alternative<Amf0Null>(value); }
    [[nodiscard]] std::optional<double> number() const;
    [[nodiscard]] std::optional<std::string> string() const;
    [[nodiscard]] const Amf0Object *object() const;
    // Convenience for "obj.code"-style lookups on objects; empty when absent.
    [[nodiscard]] std::string stringField(const std::string &key) const;
};

void amf0Write(Bytes &out, const Amf0Value &value);
// Metadata is sent as an ECMA array (a typed associative array).
void amf0WriteEcmaArray(Bytes &out, const Amf0Object &object);

// Decodes a sequence of AMF0 values (a whole command message). Returns
// nullopt on malformed input instead of throwing.
std::optional<std::vector<Amf0Value>> amf0ReadAll(ByteView data);

} // namespace abyss::net
