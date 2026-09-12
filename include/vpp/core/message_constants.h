#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "vpp/core/message_cli_constants.h"
#include "vpp/core/message_compiler_constants.h"
#include "vpp/core/message_runtime_constants.h"

namespace vietvm::messages {

inline std::string messageText(std::string_view messageTemplate,
                               std::initializer_list<std::string_view> values = {}) {
    std::string rendered(messageTemplate);
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

inline std::string formatMessage(std::string_view messageTemplate,
                                 std::initializer_list<std::string_view> values = {}) {
    return messageText(messageTemplate, values);
}

} // namespace vietvm::messages
