#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

// Central catalog for messages shown by the CLI, compiler and runtime.
//
// `code` is stable and machine-readable; `text` is the Vietnamese template
// shown to users.  Keep the code when wording changes so automation and LSP
// clients can identify the same diagnostic across releases.
namespace vietvm::messages {

struct MessageDefinition {
    std::string_view code;
    std::string_view text;
};

inline std::string messageText(const MessageDefinition &definition,
                               std::initializer_list<std::string_view> values = {}) {
    std::string rendered(definition.text);
    std::size_t index = 0;
    for (const std::string_view value : values) {
        const std::string placeholder = "{" + std::to_string(index++) + "}";
        std::size_t pos = 0;
        while ((pos = rendered.find(placeholder, pos)) != std::string::npos) {
            rendered.replace(pos, placeholder.size(), value.data(), value.size());
            pos += value.size();
        }
    }
    return rendered;
}

inline std::string formatMessage(const MessageDefinition &definition,
                                 std::initializer_list<std::string_view> values = {}) {
    return "[" + std::string(definition.code) + "] " + messageText(definition, values);
}

inline bool hasMessageCode(std::string_view value) {
    return value.size() > 6 && value.compare(0, 5, "[VPP-") == 0;
}

inline std::string_view messageCodeFromFormatted(std::string_view value) {
    if (!hasMessageCode(value)) return {};
    const std::size_t closingBracket = value.find(']');
    if (closingBracket == std::string_view::npos || closingBracket <= 1) return {};
    return value.substr(1, closingBracket - 1);
}

} // namespace vietvm::messages

#include "vpp/core/message_cli_constants.h"
#include "vpp/core/message_compiler_constants.h"
#include "vpp/core/message_runtime_constants.h"
