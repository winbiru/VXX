#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/vm_native_http_helpers.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/vm.h"
#include "vpp/runtime/vm_fixture.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testVerificationCacheAndInvalidation() {
    const std::vector<Instruction> root = {
        {OP_BIEN_SO, 42, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    VM vm(root, {});
    vm.run();
    expect(vm.verificationPassCount() == 1,
           "lần chạy đầu verify root đúng một lần");

    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 1,
           "chạy lại cùng generation không verify lại");

    vm.setFunctions({{7, {{999, 0, 0, 0}}}}, {});
    vm.resetExecution();
    bool invalidRejected = false;
    try {
        vm.run();
    } catch (const vietvm::runtime::RuntimeError &) {
        invalidRejected = true;
    }
    expect(invalidRejected, "function bytecode lỗi bị verifier từ chối");
    expect(vm.verificationPassCount() == 2,
           "generation mới chỉ mở một verification pass dù function lỗi");

    vm.setFunctions({{7, {{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}}}, {});
    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 3,
           "valid → invalid → valid tạo generation verifier độc lập");

    vm.setFunctions({{7, {{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}}}, {{11, 7}});
    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 4,
           "đổi function metadata làm invalid cache verifier");

    vm.setFunctions({}, {});
    vm.resetExecution();
    vm.run();
    expect(vm.verificationPassCount() == 5,
           "xóa function body làm invalid cache verifier");
}

void testModuleVerificationPrecedesSideEffects() {
    VM vm({{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {});
    std::string output;
    vm.setOutputSink([&](const std::string &value) { output += value; });
    expect(vm.addModuleInitializer(
               "valid-first",
               {{OP_BIEN_SO, 9, 0, 0},
                {OP_IN, 0, 0, 0},
                {OP_DUNG_CHUONG_TRINH, 0, 0, 0}}),
           "thêm module hợp lệ");
    expect(vm.addModuleInitializer("invalid-second", {{999, 0, 0, 0}}),
           "thêm module lỗi để hardening");

    bool rejected = false;
    try {
        vm.run();
    } catch (const vietvm::runtime::RuntimeError &) {
        rejected = true;
    }
    expect(rejected, "module initializer lỗi bị từ chối");
    expect(output.empty(),
           "verifier quét toàn bộ initializer trước side effect module trước đó");
}

void testPeriodicGcKeepsCapacity() {
    VM vm({{OP_DUNG_CHUONG_TRINH, 0, 0, 0}}, {});
    VMRuntimeFixture fixture(vm);
    fixture.reserveStack(4096);
    const std::size_t reserved = fixture.stackCapacity();
    expect(reserved >= 4096, "fixture dự trữ stack capacity cho GC test");

    fixture.collectGarbage();
    expect(fixture.stackCapacity() == reserved,
           "periodic GC không shrink stack capacity");

    fixture.collectGarbageAndTrim();
    expect(fixture.stackCapacity() <= reserved,
           "explicit trim không làm tăng stack capacity");
}

void testHttpJsonExtractionUsesJsonContract() {
    using vietvm::helpers::extractSimpleJsonStringField;

    expect(extractSimpleJsonStringField(
               R"({"name":"V++","escaped":"a\"b\\c","unicode":"Việt \uD83D\uDE80"})",
               "escaped") == "a\"b\\c",
           "JSON helper giải escape quote/backslash");
    expect(extractSimpleJsonStringField(
               R"({"unicode":"Việt \uD83D\uDE80"})", "unicode") ==
               std::string("Việt 🚀"),
           "JSON helper giải Unicode/surrogate pair");
    expect(extractSimpleJsonStringField(
               R"({"nested":{"token":"wrong"},"token":"right"})", "token") ==
               "right",
           "JSON helper chỉ lấy exact top-level key");
    expect(extractSimpleJsonStringField(
               R"({"a.*":"literal"})", "a.*") == "literal",
           "key có ký tự regex được xử lý như JSON key literal");
    expect(extractSimpleJsonStringField(
               R"({"dup":"first","dup":"last"})", "dup") == "last",
           "duplicate key theo parser policy last-write-wins");
    expect(extractSimpleJsonStringField(R"({"token":42})", "token").empty(),
           "value không phải string trả fallback rỗng");
    expect(extractSimpleJsonStringField(R"({"token":"inside"} trailing)", "token").empty(),
           "malformed/trailing JSON bị từ chối");
    expect(extractSimpleJsonStringField(
               R"({"text":"\"token\":\"fake\""})", "token").empty(),
           "không match JSON-looking text bên trong string");
}

} // namespace

int main() {
    testVerificationCacheAndInvalidation();
    testModuleVerificationPrecedesSideEffects();
    testPeriodicGcKeepsCapacity();
    testHttpJsonExtractionUsesJsonContract();

    if (failures != 0) {
        std::cerr << failures << " runtime P0 hardening check(s) failed\n";
        return 1;
    }
    std::cout << "Runtime P0 hardening: PASS\n";
    return 0;
}
