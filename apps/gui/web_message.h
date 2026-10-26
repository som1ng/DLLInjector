#pragma once
#include <cstddef>
#include <optional>
#include <string>

namespace gui {

inline constexpr std::size_t MaxWebMessageBytes = 64 * 1024;

// The WebView sends a flat object. Defaults match the existing host protocol.
struct WebMessage {
    std::string command;
    std::string path;
    std::string mode;
    std::string hex;
    int pid = -1;
    int method = 0;
    bool force = false;
    bool dark = true;
};

// Reject malformed documents and wrong field types before dispatching anything.
std::optional<WebMessage> ParseWebMessage(const std::string& input);

} // namespace gui
