#include "vpp/native/abi_symbol.h"

#include <iostream>
#include <string>
#include <string_view>

int main() {
    using vietvm::native::nativeAbiSymbol;
    const auto expect = [](std::string_view source, std::string_view expected) {
        const auto actual = nativeAbiSymbol(source);
        if (actual == expected) return true;
        std::cerr << "ABI symbol mismatch: " << std::string(source) << " -> "
                  << actual << " (expected " << expected << ")\n";
        return false;
    };
    bool ok = true;
    ok &= expect("them", "them");
    ok &= expect("tinh_vong42", "tinh_vong42");
    ok &= expect("thêm", "vpp_u_7468c3aa6d");
    ok &= expect("làm mới", "vpp_u_6cc3a06d206de1bb9b69");
    ok &= expect("vpp_u_7468c3aa6d", "vpp_a_7670705f755f37343638633361613664");
    ok &= expect("vpp_a_x", "vpp_a_7670705f615f78");
    ok &= expect("a b", "vpp_u_612062");
    ok &= expect("", "");
    ok &= expect(std::string("a\xC3\x28", 3), "");
    ok &= expect(std::string("\xC0\xAF", 2), "");
    ok &= expect(std::string("\xED\xA0\x80", 3), "");
    ok &= expect(std::string("\xF4\x90\x80\x80", 4), "");
    if (nativeAbiSymbol("thêm") == nativeAbiSymbol("vpp_u_7468c3aa6d")) ok = false;
    return ok ? 0 : 1;
}
