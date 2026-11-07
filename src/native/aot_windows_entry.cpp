#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vpp/native/aot.h"

namespace vietvm::native::aot {
extern "C" int vpp_aot_main(int, char **, const ProgramSpec *) noexcept;

// Generated Windows programs enter through wmain. Keep the shared AOT driver
// and V++ compiler on UTF-8, including characters absent from the system ACP.
extern "C" int vpp_aot_wmain(int argc, wchar_t **argv, const ProgramSpec *spec) noexcept {
    try {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            const int size = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
            if (size <= 0) throw std::runtime_error("Native AOT: invalid Windows argument");
            std::string utf8(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                    utf8.data(), size, nullptr, nullptr) != size)
                throw std::runtime_error("Native AOT: cannot encode Windows argument as UTF-8");
            utf8.pop_back();
            arguments.push_back(std::move(utf8));
        }
        std::vector<char *> utf8Argv;
        utf8Argv.reserve(arguments.size() + 1);
        for (auto &argument : arguments) utf8Argv.push_back(argument.data());
        utf8Argv.push_back(nullptr);
        return vpp_aot_main(argc, utf8Argv.data(), spec);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
} // namespace vietvm::native::aot
#endif
