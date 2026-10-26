#include "web_message.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        const auto unicode = gui::ParseWebMessage(R"({"cmd":"set_dll_path","path":"C:\\测试\\\u6d4b\u8bd5-\ud83e\uddea.dll"})");
        Check(unicode && unicode->path == u8"C:\\测试\\测试-🧪.dll", "UTF-8, Unicode escapes and surrogate pairs round trip");
        const auto literal = gui::ParseWebMessage(R"({"cmd":"set_dll_path","path":"C:\\udc00\\uD800.dll"})");
        Check(literal && literal->path == "C:\\udc00\\uD800.dll", "literal path backslashes are not Unicode escapes");
        const auto escaped = gui::ParseWebMessage("{\r\n\t\"cmd\" : \"init\", \"path\" : \"quote\\\" slash\\/ tab\\t line\\n return\\r back\\b form\\f\" }");
        Check(escaped && escaped->path == "quote\" slash/ tab\t line\n return\r back\b form\f", "JSON escapes and whitespace round trip");
        const auto collision = gui::ParseWebMessage(R"({"note":"cmd","placeholder":"window_close","cmd":"refresh_processes"})");
        Check(collision && collision->command == "refresh_processes", "field-name text in values cannot select a different command");
        const auto defaults = gui::ParseWebMessage(R"({"cmd":"init"})");
        Check(defaults && defaults->pid == -1 && defaults->method == 0 && !defaults->force && defaults->dark && defaults->path.empty(), "existing defaults remain intact");
        const auto fields = gui::ParseWebMessage(R"({"cmd":"init","pid":2147483647,"method":-2147483648,"force":true,"dark":false,"mode":"dll","hex":"00"})");
        Check(fields && fields->pid == std::numeric_limits<int>::max() && fields->method == std::numeric_limits<int>::min() && fields->force && !fields->dark && fields->mode == "dll" && fields->hex == "00", "typed fields preserve integer bounds and booleans");

        const std::vector<std::string> invalid = {
            "", "null", "[]", R"("init")", "{}", R"({"cmd":null})", R"({"cmd":1})", R"({"cmd":""})",
            R"({"meta":{"cmd":"init"}})", R"({"cmd":"init","meta":{"cmd":"window_close"}})",
            R"({"cmd":"init","data":[]})", R"({"cmd":"init","cmd":"window_close"})",
            R"({"cmd":"init","\u0063md":"window_close"})", R"({"cmd":"init","dark":true,"dark":false})",
            R"({"cmd":"init",})", R"({"cmd":"init"} trailing)", R"({"cmd":"init"}{"cmd":"window_close"})",
            R"({/* comment */"cmd":"init"})", R"({"cmd":"init")", R"({"cmd":"init","path":"\q"})",
            R"({"cmd":"init","path":"\ud800"})", R"({"cmd":"init","path":"\udc00"})",
            R"({"cmd":"init","path":"\uD800\u0041"})", R"({"cmd":"init","\uDC00":"ignored"})",
            R"({"cmd":"init","path":"\u0000"})", R"({"cmd":"init","\u0000":true})",
            R"({"cmd":"init","pid":"12"})", R"({"cmd":"init","pid":12abc})",
            R"({"cmd":"init","pid":1.5})", R"({"cmd":"init","pid":1.0})", R"({"cmd":"init","pid":1e0})",
            R"({"cmd":"init","pid":2147483648})", R"({"cmd":"init","pid":-2147483649})",
            R"({"cmd":"init","pid":18446744073709551616})", R"({"cmd":"init","method":true})",
            R"({"cmd":"init","force":"false"})", R"({"cmd":"init","force":truejunk})",
            R"({"cmd":"init","dark":0})", R"({"cmd":"init","path":false})",
            R"({"cmd":"init","mode":null})", R"({"cmd":"init","hex":123})",
            std::string("{\"cmd\":\"init\",\"path\":\"") + char(0xff) + "\"}",
            std::string("{\"cmd\":\"init\",\"path\":\"") + char(0) + "\"}",
        };
        for (std::size_t i = 0; i < invalid.size(); ++i) {
            if (gui::ParseWebMessage(invalid[i])) throw std::runtime_error("Invalid fixture accepted: " + std::to_string(i));
        }
        const std::string prefix = R"({"cmd":"init","path":")", suffix = R"("})";
        auto boundary = prefix + std::string(gui::MaxWebMessageBytes - prefix.size() - suffix.size(), 'a') + suffix;
        Check(gui::ParseWebMessage(boundary).has_value(), "message at size limit is accepted");
        boundary.insert(prefix.size(), 1, 'a');
        Check(!gui::ParseWebMessage(boundary), "oversized message is rejected");
        Check(gui::ParseWebMessage(R"({"cmd":"init"})").has_value(), "invalid messages do not poison the next parse");
        std::cout << "PASS GUI message parser: " << invalid.size() << " invalid fixtures and valid Unicode/type/boundary/recovery cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
