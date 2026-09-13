#include <iostream>
#include <string>

#include "frontend/keywords.h"
#include "vpp/compiler/pipeline.h"
#include "vpp/runtime/vm.h"

namespace {

std::string runProgram(const std::string &source) {
    vietvm::compiler::CompilationContext context;
    const vietvm::compiler::CompilationArtifacts artifacts =
        vietvm::compiler::compilePipeline(context, source, keywordMap, true);

    VM vm(artifacts.bytecode, context.stringPool);
    vm.hamBytecodeMap = context.functionBytecode;
    for (const auto &entry : context.functionNameIndices) {
        vm.functionTableByNameIndex[entry.second] = entry.first;
    }

    std::string output;
    vm.setOutputSink([&output](const std::string &text) { output += text; });
    vm.run();
    return output;
}

bool runProgramFailsWith(const std::string &source,
                         const std::string &expectedMessagePart) {
    try {
        (void)runProgram(source);
    } catch (const std::exception &error) {
        return std::string(error.what()).find(expectedMessagePart) != std::string::npos;
    }
    return false;
}

} // namespace

int main() {
    const std::string receiverSource =
        "lớp Counter { "
        "  hàm set(value) { mình.value = value; } "
        "  hàm get() { trả về mình.value; } "
        "  hàm bump(delta) { mình.set(mình.get() + delta); trả về mình.get(); } "
        "} "
        "hàm main() { "
        "  c = Counter(); "
        "  c.set(7); "
        "  in c.get(); "
        "  in c.bump(5); "
        "}";

    const std::string receiverOutput = runProgram(receiverSource);
    if (receiverOutput != "[IN] 7\n[IN] 12\n") {
        std::cerr << "FAIL: implicit receiver end-to-end output mismatch\n"
                  << "actual:\n" << receiverOutput;
        return 1;
    }

    const std::string constructorSource =
        "lớp Box { "
        "  hàm khởi tạo(value, bonus = 1) { mình.set(value + bonus); } "
        "  hàm set(value) { mình.value = value; } "
        "  hàm get() { trả về mình.value; } "
        "} "
        "hàm main() { "
        "  first = Box(10, 2); "
        "  second = Box(20); "
        "  in first.get(); "
        "  in second.get(); "
        "}";

    const std::string constructorOutput = runProgram(constructorSource);
    if (constructorOutput != "[IN] 12\n[IN] 21\n") {
        std::cerr << "FAIL: parameterized constructor end-to-end output mismatch\n"
                  << "actual:\n" << constructorOutput;
        return 1;
    }

    const std::string dynamicPrivateSource =
        "lớp Vault { "
        "  hàm riêng tư secret() { trả về 7; } "
        "} "
        "hàm leak(obj) { trả về obj.secret(); } "
        "hàm main() { v = Vault(); in leak(v); }";
    if (!runProgramFailsWith(dynamicPrivateSource, "phương thức riêng tư")) {
        std::cerr << "FAIL: runtime must reject private access through an untyped parameter\n";
        return 1;
    }

    const std::string dynamicProtectedSource =
        "lớp Base { "
        "  hàm bảo vệ token() { trả về 3; } "
        "} "
        "hàm leak(obj) { trả về obj.token(); } "
        "hàm main() { b = Base(); in leak(b); }";
    if (!runProgramFailsWith(dynamicProtectedSource, "phương thức bảo vệ")) {
        std::cerr << "FAIL: runtime must reject protected access through an untyped parameter\n";
        return 1;
    }

    std::cout << "object model integration tests passed\n";
    return 0;
}
