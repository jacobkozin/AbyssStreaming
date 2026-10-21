#include "net/Amf0.hpp"

#include <bit>
#include <cstring>

namespace abyss::net {

namespace {

enum Marker : std::uint8_t {
    kNumber = 0x00,
    kBoolean = 0x01,
    kString = 0x02,
    kObject = 0x03,
    kNull = 0x05,
    kUndefined = 0x06,
    kEcmaArray = 0x08,
    kObjectEnd = 0x09,
    kStrictArray = 0x0A,
    kDate = 0x0B,
    kLongString = 0x0C,
};

void writeDouble(Bytes &out, double value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(bits >> shift));
}

void writeKey(Bytes &out, const std::string &key)
{
    putU16(out, static_cast<std::uint16_t>(key.size()));
    putString(out, key);
}

void writeObjectBody(Bytes &out, const Amf0Object &object)
{
    for (const auto &[key, item] : object) {
        writeKey(out, key);
        amf0Write(out, item);
    }
    putU16(out, 0);
    putU8(out, kObjectEnd);
}

class Reader {
public:
    explicit Reader(ByteView data) : data_(data) {}
    [[nodiscard]] bool done() const { return pos_ >= data_.size(); }

    bool read(Amf0Value &out, int depth = 0)
    {
        if (depth > 32) // malicious nesting
            return false;
        std::uint8_t marker = 0;
        if (!take(&marker, 1))
            return false;
        switch (marker) {
        case kNumber: {
            std::uint8_t raw[8];
            if (!take(raw, 8))
                return false;
            std::uint64_t bits = 0;
            for (std::uint8_t b : raw)
                bits = (bits << 8) | b;
            out = std::bit_cast<double>(bits);
            return true;
        }
        case kBoolean: {
            std::uint8_t flag = 0;
            if (!take(&flag, 1))
                return false;
            out = flag != 0;
            return true;
        }
        case kString:
        case kLongString: {
            std::string text;
            if (!readString(text, marker == kLongString))
                return false;
            out = std::move(text);
            return true;
        }
        case kNull:
        case kUndefined: out = nullptr; return true;
        case kEcmaArray: {
            std::uint8_t count[4];
            if (!take(count, 4))
                return false;
            [[fallthrough]];
        }
        case kObject: {
            Amf0Object object;
            if (!readPairs(object, depth))
                return false;
            out = std::move(object);
            return true;
        }
        case kStrictArray: {
            std::uint8_t raw[4];
            if (!take(raw, 4))
                return false;
            const std::uint32_t count = readU32(raw);
            if (count > data_.size()) // each element needs at least one byte
                return false;
            auto array = std::make_shared<Amf0Array>();
            for (std::uint32_t i = 0; i < count; ++i) {
                Amf0Value item;
                if (!read(item, depth + 1))
                    return false;
                array->push_back(std::move(item));
            }
            out.value = std::move(array);
            return true;
        }
        case kDate: {
            std::uint8_t skip[10];
            if (!take(skip, 10))
                return false;
            out = nullptr;
            return true;
        }
        default: return false;
        }
    }

private:
    bool take(std::uint8_t *dest, size_t count)
    {
        if (data_.size() - pos_ < count)
            return false;
        std::memcpy(dest, data_.data() + pos_, count);
        pos_ += count;
        return true;
    }

    bool readString(std::string &out, bool longForm)
    {
        std::uint8_t raw[4];
        if (!take(raw, longForm ? 4 : 2))
            return false;
        const std::uint32_t length = longForm ? readU32(raw) : readU16(raw);
        if (data_.size() - pos_ < length)
            return false;
        out.assign(reinterpret_cast<const char *>(data_.data() + pos_), length);
        pos_ += length;
        return true;
    }

    bool readPairs(Amf0Object &object, int depth)
    {
        while (true) {
            std::string key;
            if (!readString(key, false))
                return false;
            if (key.empty() && pos_ < data_.size() && data_[pos_] == kObjectEnd) {
                ++pos_;
                return true;
            }
            Amf0Value item;
            if (!read(item, depth + 1))
                return false;
            object[std::move(key)] = std::move(item);
        }
    }

    ByteView data_;
    size_t pos_{0};
};

} // namespace

std::optional<double> Amf0Value::number() const
{
    if (const auto *n = std::get_if<double>(&value))
        return *n;
    return std::nullopt;
}

std::optional<std::string> Amf0Value::string() const
{
    if (const auto *s = std::get_if<std::string>(&value))
        return *s;
    return std::nullopt;
}

const Amf0Object *Amf0Value::object() const
{
    if (const auto *o = std::get_if<std::shared_ptr<Amf0Object>>(&value))
        return o->get();
    return nullptr;
}

std::string Amf0Value::stringField(const std::string &key) const
{
    if (const auto *o = object()) {
        if (auto it = o->find(key); it != o->end())
            return it->second.string().value_or("");
    }
    return {};
}

void amf0Write(Bytes &out, const Amf0Value &value)
{
    if (value.isNull()) {
        putU8(out, kNull);
    } else if (const auto *number = std::get_if<double>(&value.value)) {
        putU8(out, kNumber);
        writeDouble(out, *number);
    } else if (const auto *flag = std::get_if<bool>(&value.value)) {
        putU8(out, kBoolean);
        putU8(out, *flag ? 1 : 0);
    } else if (const auto *text = std::get_if<std::string>(&value.value)) {
        if (text->size() > 0xFFFF) {
            putU8(out, kLongString);
            putU32(out, static_cast<std::uint32_t>(text->size()));
        } else {
            putU8(out, kString);
            putU16(out, static_cast<std::uint16_t>(text->size()));
        }
        putString(out, *text);
    } else if (const auto *object = value.object()) {
        putU8(out, kObject);
        writeObjectBody(out, *object);
    } else if (const auto *array = std::get_if<std::shared_ptr<Amf0Array>>(&value.value)) {
        putU8(out, kStrictArray);
        putU32(out, static_cast<std::uint32_t>((*array)->size()));
        for (const auto &item : **array)
            amf0Write(out, item);
    }
}

void amf0WriteEcmaArray(Bytes &out, const Amf0Object &object)
{
    putU8(out, kEcmaArray);
    putU32(out, static_cast<std::uint32_t>(object.size()));
    writeObjectBody(out, object);
}

std::optional<std::vector<Amf0Value>> amf0ReadAll(ByteView data)
{
    Reader reader(data);
    std::vector<Amf0Value> values;
    while (!reader.done()) {
        Amf0Value value;
        if (!reader.read(value))
            return std::nullopt;
        values.push_back(std::move(value));
    }
    return values;
}

} // namespace abyss::net
