#include "web_message.h"
#include <jsoncons/json.hpp>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace gui {
namespace {

bool ReadCodeUnit(const std::string& input, std::size_t offset, unsigned& value) {
    if (offset + 4 > input.size()) return false;
    value = 0;
    for (std::size_t i = offset; i < offset + 4; ++i) {
        const char c = input[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        value = value * 16 + digit;
    }
    return true;
}

// jsoncons 1.10.0 can silently drop an escaped lone low surrogate. Validate
// escape pairs before decoding; JSON syntax and UTF-8 remain the library's job.
bool ValidUnicodeEscapes(const std::string& input) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '\\') continue;
        if (++i == input.size()) return false;
        if (input[i] != 'u') continue; // Includes a literal escaped backslash.
        unsigned first;
        if (!ReadCodeUnit(input, i + 1, first)) return false;
        i += 4;
        if (first >= 0xdc00 && first <= 0xdfff) return false;
        if (first >= 0xd800 && first <= 0xdbff) {
            unsigned second;
            if (i + 6 >= input.size() || input[i + 1] != '\\' || input[i + 2] != 'u' ||
                !ReadCodeUnit(input, i + 3, second) || second < 0xdc00 || second > 0xdfff) return false;
            i += 6;
        }
    }
    return true;
}

// Check keys before the DOM decoder can silently discard duplicates. The
// reader's depth limit disallows nested containers in this flat protocol.
class UniqueKeys : public jsoncons::json_filter {
public:
    explicit UniqueKeys(jsoncons::json_visitor& target) : jsoncons::json_filter(target) {}
private:
    bool visit_key(const jsoncons::string_view& value, const jsoncons::ser_context& context,
                   std::error_code& ec) override {
        if (value.find('\0') != jsoncons::string_view::npos ||
            !keys_.emplace(value.data(), value.size()).second) {
            throw std::runtime_error("Invalid or duplicate message key");
        }
        return destination().key(value, context, ec);
    }
    std::unordered_set<std::string> keys_;
};

bool ReadField(const jsoncons::json& object, const char* key, std::string& result) {
    if (!object.contains(key)) return true;
    const auto& value = object.at(key);
    if (!value.is_string()) return false;
    result = value.as<std::string>();
    return true;
}

bool ReadField(const jsoncons::json& object, const char* key, bool& result) {
    if (!object.contains(key)) return true;
    const auto& value = object.at(key);
    if (!value.is_bool()) return false;
    result = value.as<bool>();
    return true;
}

bool ReadField(const jsoncons::json& object, const char* key, int& result) {
    if (!object.contains(key)) return true;
    const auto& value = object.at(key);
    if (value.is_uint64()) {
        const auto number = value.as<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return false;
        result = static_cast<int>(number);
    } else if (value.is_int64()) {
        const auto number = value.as<std::int64_t>();
        if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return false;
        result = static_cast<int>(number);
    } else {
        return false;
    }
    return true;
}

} // namespace

std::optional<WebMessage> ParseWebMessage(const std::string& input) {
    if (input.size() > MaxWebMessageBytes || !ValidUnicodeEscapes(input)) return std::nullopt;
    try {
        jsoncons::json_decoder<jsoncons::json> decoder;
        UniqueKeys filter(decoder);
        jsoncons::json_options options;
        options.max_nesting_depth(1).err_handler([](jsoncons::json_errc, const jsoncons::ser_context&) { return false; });
        jsoncons::json_string_reader reader(input, filter, options);
        reader.read();
        const auto object = decoder.get_result();
        if (!object.is_object()) return std::nullopt;
        for (const auto& field : object.object_range()) {
            if (field.value().is_string() && field.value().as_string_view().find('\0') != jsoncons::string_view::npos)
                return std::nullopt;
        }

        WebMessage result;
        if (!ReadField(object, "cmd", result.command) || result.command.empty() ||
            !ReadField(object, "path", result.path) || !ReadField(object, "mode", result.mode) ||
            !ReadField(object, "hex", result.hex) || !ReadField(object, "pid", result.pid) ||
            !ReadField(object, "method", result.method) || !ReadField(object, "force", result.force) ||
            !ReadField(object, "dark", result.dark)) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace gui
