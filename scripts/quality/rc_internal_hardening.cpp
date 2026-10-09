#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/storeString.h"
#include "frontend/keywords.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/bytecode/opcode.h"
#include "vpp/bytecode/verifier.h"
#include "vpp/compiler/codegen.h"
#include "vpp/compiler/ir.h"
#include "vpp/compiler/pipeline.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

vietvm::frontend::Token token(const std::string &lexeme) {
    return {vietvm::frontend::TokenKind::Identifier, lexeme, {}};
}

vietvm::bytecode::BytecodeVerificationContext verificationContext(
    std::size_t stringPoolSize,
    std::unordered_set<int> functionIds = {},
    std::size_t foreignDescriptorCount = 0) {
    vietvm::bytecode::BytecodeVerificationContext result;
    result.stringPoolSize = stringPoolSize;
    result.functionIds = std::move(functionIds);
    result.foreignDescriptorCount = foreignDescriptorCount;
    return result;
}

void testMalformedAstReferencesBecomeUnsupportedIr() {
    using namespace vietvm::compiler;
    using namespace vietvm::frontend;

    AstProgram program;
    program.tokens.push_back(token("hỏng"));

    AstStatement statement;
    statement.kind = AstStatementKind::Expression;
    statement.tokenEnd = program.tokens.size();
    statement.expressionRoots.push_back(42);
    program.statements.push_back(std::move(statement));

    IrProgram ir = lowerToIr(program, {});
    expect(ir.instructions.size() == 1 &&
               ir.instructions.front().expressionRoots.size() == 1,
           "AST lỗi vẫn hạ thành vùng IR chẩn đoán được");
    if (!ir.instructions.empty() && !ir.instructions.front().expressionRoots.empty()) {
        const IrValue *value = ir.value(ir.instructions.front().expressionRoots.front());
        expect(value != nullptr &&
                   value->opcode == IrValueOpcode::UnsupportedDirectRegion &&
                   value->sourceExprId == 42,
               "ExprId ngoài arena trở thành UnsupportedDirectRegion");
    }
    expect(ir.unsupportedDirectRegionCount == 1,
           "AST lỗi được tính đúng một unsupported-direct region");

    AstProgram cyclicProgram;
    cyclicProgram.tokens.push_back(token("!"));
    AstExpression cyclic;
    cyclic.id = 0;
    cyclic.kind = AstExpressionKind::Unary;
    cyclic.text = "!";
    cyclic.operand = 0;
    cyclicProgram.expressions.push_back(std::move(cyclic));

    AstStatement cyclicStatement;
    cyclicStatement.kind = AstStatementKind::Expression;
    cyclicStatement.tokenEnd = cyclicProgram.tokens.size();
    cyclicStatement.expressionRoots.push_back(0);
    cyclicProgram.statements.push_back(std::move(cyclicStatement));

    IrProgram cyclicIr = lowerToIr(cyclicProgram, {});
    const IrValue *cyclicValue = cyclicIr.value(0);
    expect(cyclicValue != nullptr &&
               cyclicValue->opcode == IrValueOpcode::UnsupportedDirectRegion,
           "chu trình ExprId bị chặn trước codegen");
}

void testInvalidIrIsRejectedBeforeEmission() {
    using namespace vietvm::compiler;

    IrProgram program;
    IrValue invalidBinary;
    invalidBinary.id = 0;
    invalidBinary.opcode = IrValueOpcode::Binary;
    invalidBinary.text = "+";
    invalidBinary.operands = {1, 2};
    program.values.push_back(std::move(invalidBinary));

    IrInstruction print;
    print.opcode = IrOpcode::Print;
    print.expressionRoots.push_back(0);
    program.instructions.push_back(std::move(print));

    const DirectIrSupport support = analyzeDirectIrSupport(program);
    expect(!support.supported && support.unsupportedRegions == 1,
           "IR có operand ngoài arena bị phân loại không hỗ trợ");

    CompilationRegistryState state;
    bool rejected = false;
    try {
        (void)emitDirectBytecode(state, program, {}, true);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    expect(rejected, "direct codegen từ chối IR lỗi trước khi dereference operand");

    IrProgram malformedControlFlow;
    IrInstruction conditional;
    conditional.opcode = IrOpcode::Conditional;
    conditional.conditionalForm = vietvm::frontend::AstConditionalForm::IfBlock;
    malformedControlFlow.instructions.push_back(std::move(conditional));
    const DirectIrSupport controlFlowSupport = analyzeDirectIrSupport(malformedControlFlow);
    expect(!controlFlowSupport.supported,
           "IR điều kiện thiếu expression/body bị từ chối có kiểm soát");
}

void testBytecodeVerifierRejectsMalformedMetadata() {
    using vietvm::bytecode::verifyBytecode;

    const std::vector<Instruction> valid = {
        {OP_HAM, 0, 7, 0},
        {OP_CHUOI, 0, 1, 0},
        {OP_JUMP, 3, 0, 0},
        {OP_DUNG_CHUONG_TRINH, 0, 0, 0},
    };
    expect(!verifyBytecode(valid, verificationContext(2, {7})).has_value(),
           "verifier chấp nhận bytecode hợp lệ");

    expect(verifyBytecode({{999, 0, 0, 0}}, verificationContext(0)).has_value(),
           "verifier từ chối opcode lạ");
    expect(verifyBytecode(
               {{OP_JUMP, 4, 0, 0}, {OP_DUNG_CHUONG_TRINH, 0, 0, 0}},
               verificationContext(0))
               .has_value(),
           "verifier từ chối jump ngoài phạm vi");
    expect(verifyBytecode({{OP_CHUOI, 0, 2, 0}}, verificationContext(2)).has_value(),
           "verifier từ chối StringPool index ngoài phạm vi");
    expect(verifyBytecode({{OP_HAM, 0, 9, 0}}, verificationContext(1, {7})).has_value(),
           "verifier từ chối function id không có bytecode");
    expect(verifyBytecode({{OP_TAO_DONG_BAO, 9, 1, 0}}, verificationContext(0, {7}))
               .has_value(),
           "verifier từ chối closure tham chiếu function id không tồn tại");

    expect(!verifyBytecode({{OP_FFI_CALL, 1, 0, 0}},
                           verificationContext(0, {}, 1)).has_value(),
           "verifier chấp nhận FFI descriptor hợp lệ");
    expect(verifyBytecode({{OP_FFI_CALL, 1, 1, 0}},
                          verificationContext(0, {}, 1)).has_value(),
           "verifier từ chối foreign descriptor index ngoài phạm vi");
    expect(verifyBytecode({{OP_FFI_CALL, -1, 0, 0}},
                          verificationContext(0, {}, 1)).has_value(),
           "verifier từ chối argc FFI âm");
    expect(verifyBytecode({{OP_FFI_CALL, 1, 0, 1}},
                          verificationContext(0, {}, 1)).has_value(),
           "verifier từ chối operandValue dành riêng của FFI");

    using vietvm::bytecode::ForeignAbiType;
    using vietvm::bytecode::ForeignFunctionDescriptor;
    ForeignFunctionDescriptor binding;
    binding.id = 0;
    binding.library = "system.c";
    binding.symbol = "getentropy";
    binding.capability = "system.entropy.read";
    binding.parameters = {ForeignAbiType::BufferOut, ForeignAbiType::U64};
    binding.result = ForeignAbiType::I32;
    binding.bufferExtents = {{0, 1, 0}};
    std::vector<ForeignFunctionDescriptor> bindings = {binding};
    auto ffiContext = verificationContext(0, {}, bindings.size());
    ffiContext.foreignDescriptors = &bindings;
    const std::vector<Instruction> ffiCall = {{OP_FFI_CALL, 2, 0, 0}};
    const auto accepts = [&]() {
        return !verifyBytecode(ffiCall, ffiContext).has_value();
    };
    const auto rejects = [&]() {
        return verifyBytecode(ffiCall, ffiContext).has_value();
    };
    expect(accepts(), "verifier kiểm tra descriptor FFI có độ dài gắn tham số");
    expect(verifyBytecode({{OP_FFI_CALL, 1, 0, 0}}, ffiContext).has_value(),
           "verifier từ chối argc khác chữ ký trước khi gọi native");

    bindings[0].bufferExtents = {{0, -1, 16}};
    expect(accepts(), "verifier chấp nhận độ dài đệm cố định");
    bindings[0].bufferExtents = {{0, -1, 0}};
    expect(rejects(), "verifier từ chối đệm cố định dài 0");
    bindings[0].bufferExtents = {{0, -1, 65537}};
    expect(rejects(), "verifier từ chối đệm vượt 65536 byte");
    bindings[0].bufferExtents = {{0, 1, 4}};
    expect(rejects(), "verifier từ chối đệm có cả độ dài fixed/bound");
    bindings[0].bufferExtents = {{0, 0, 0}};
    expect(rejects(), "verifier từ chối độ dài đệm tham chiếu đệm");
    bindings[0].bufferExtents = {{0, 2, 0}};
    expect(rejects(), "verifier từ chối length index ngoài phạm vi");
    bindings[0].bufferExtents = {{0, 1, 0}, {0, 1, 0}};
    expect(rejects(), "verifier từ chối extent lặp chỉ số đệm");
    bindings[0].bufferExtents = {{1, -1, 16}};
    expect(rejects(), "verifier từ chối extent trỏ vào số nguyên");
    bindings[0].parameters = {ForeignAbiType::BufferOut, ForeignAbiType::BufferIn};
    bindings[0].bufferExtents = {{0, -1, 16}};
    expect(rejects(), "verifier từ chối thiếu extent của đệm thứ hai");
    bindings[0] = binding;
    bindings[0].id = 7;
    expect(rejects(), "verifier từ chối ID không khớp vị trí descriptor");
    bindings[0] = binding;
    bindings[0].parameters[1] = ForeignAbiType::Void;
    expect(rejects(), "verifier từ chối tham số void");
    bindings[0] = binding;
    bindings[0].abi = "stdcall";
    expect(rejects(), "verifier từ chối ABI chưa hỗ trợ");
    bindings[0] = binding;
    bindings[0].result = ForeignAbiType::BufferOut;
    expect(rejects(), "verifier từ chối return buffer trực tiếp");
}

void testForeignFfiPipeline() {
    using namespace vietvm::compiler;

    const std::string validSource =
        "ngoại thư viện c_hệ_thống = \"system.c\";\n"
        "ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ c_hệ_thống "
        "ký hiệu \"getenv\" abi \"c\" khả năng \"system.env.read\";\n"
        "in getenv(\"HOME\");\n";

    CompilationContext context;
    const auto artifacts = compilePipeline(context, validSource, keywordMap, false);
    expect(context.foreignFunctions.size() == 1,
           "pipeline đăng ký đúng một foreign descriptor");
    bool foundForeignIr = false;
    for (const auto &value : artifacts.ir.values) {
        if (value.opcode == IrValueOpcode::ForeignCall) {
            foundForeignIr = value.foreignDescriptorId == 0 &&
                             value.callTarget == CallTargetKind::ForeignFunction;
        }
    }
    expect(foundForeignIr, "semantic/IR bind getenv thành ForeignCall theo descriptor");

    std::size_t ffiCalls = 0;
    std::size_t legacyEnvCalls = 0;
    for (const auto &instruction : artifacts.bytecode) {
        ffiCalls += instruction.op == OP_FFI_CALL;
        legacyEnvCalls += instruction.op == OP_VM_DOC_BIEN_MOI_TRUONG;
    }
    expect(ffiCalls == 1, "codegen phát đúng một OP_FFI_CALL");
    expect(legacyEnvCalls == 0,
           "lát cắt getenv FFI không đi qua primitive env cũ");

    const std::string exact64Source =
        "ngoại thư viện c = \"system.c\";\n"
        "ngoại hàm nhận_i64(số: i64): i64 từ c "
        "ký hiệu \"vpp_ffi_test_i64\" abi \"c\" khả năng \"test.ffi.int64\";\n"
        "ngoại hàm nhận_u64(số: u64): u64 từ c "
        "ký hiệu \"vpp_ffi_test_u64\" abi \"c\" khả năng \"test.ffi.int64\";\n"
        "in nhận_i64(\"-9223372036854775808\");\n"
        "in nhận_u64(\"18446744073709551615\");\n";
    CompilationContext exact64Context;
    const auto exact64 = compilePipeline(exact64Context, exact64Source,
                                         keywordMap, false);
    using vietvm::bytecode::ForeignAbiType;
    expect(exact64Context.foreignFunctions.size() == 2,
           "pipeline đăng ký đủ hai descriptor i64/u64");
    if (exact64Context.foreignFunctions.size() == 2) {
        const auto &signedDescriptor = exact64Context.foreignFunctions[0];
        const auto &unsignedDescriptor = exact64Context.foreignFunctions[1];
        expect(signedDescriptor.parameters ==
                   std::vector<ForeignAbiType>{ForeignAbiType::I64} &&
               signedDescriptor.result == ForeignAbiType::I64 &&
               unsignedDescriptor.parameters ==
                   std::vector<ForeignAbiType>{ForeignAbiType::U64} &&
               unsignedDescriptor.result == ForeignAbiType::U64 &&
               signedDescriptor.capability == "test.ffi.int64" &&
               unsignedDescriptor.capability == "test.ffi.int64",
               "semantic giữ nguyên width, signedness và capability i64/u64");
    }
    std::size_t exact64IrCalls = 0;
    for (const auto &value : exact64.ir.values) {
        if (value.opcode != IrValueOpcode::ForeignCall) continue;
        expect(value.callTarget == CallTargetKind::ForeignFunction &&
                   (value.foreignDescriptorId == 0 ||
                    value.foreignDescriptorId == 1),
               "IR i64/u64 giữ descriptor ID và foreign call target");
        ++exact64IrCalls;
    }
    std::size_t exact64BytecodeCalls = 0;
    for (const auto &instruction : exact64.bytecode) {
        if (instruction.op != OP_FFI_CALL) continue;
        expect(instruction.operand == 1 &&
                   (instruction.operandIndex == 0 || instruction.operandIndex == 1),
               "bytecode i64/u64 giữ arity 1 và descriptor ID");
        ++exact64BytecodeCalls;
    }
    expect(exact64IrCalls == 2 && exact64BytecodeCalls == 2,
           "pipeline phát hai ForeignCall và hai OP_FFI_CALL cho i64/u64");

    const std::string dnsSource =
        "ngoại thư viện net = \"system.net\";\n"
        "ngoại hàm mở(h: c_chuỗi, p: c_chuỗi, udp: i32): c_dns từ net "
        "ký hiệu \"resolve_open\" khả năng \"system.net.resolve.open\";\n"
        "ngoại hàm kế(h: c_dns): c_chuỗi từ net "
        "ký hiệu \"resolve_next\" khả năng \"system.net.resolve.next\";\n"
        "ngoại hàm đóng(h: c_dns): i32 từ net "
        "ký hiệu \"resolve_close\" khả năng \"system.net.resolve.close\";\n"
        "mã = mở(\"localhost\", \"\", 0);\n"
        "in kế(mã);\n"
        "in đóng(mã);\n";
    CompilationContext dnsContext;
    const auto dnsArtifacts = compilePipeline(dnsContext, dnsSource,
                                               keywordMap, false);
    expect(dnsContext.foreignFunctions.size() == 3,
           "pipeline lưu đủ ba binding resolver VM");
    if (dnsContext.foreignFunctions.size() == 3) {
        const auto &bindings = dnsContext.foreignFunctions;
        expect(bindings[0].parameters ==
                   std::vector<ForeignAbiType>{ForeignAbiType::CString,
                                               ForeignAbiType::CString,
                                               ForeignAbiType::I32} &&
                   bindings[0].result == ForeignAbiType::DnsHandle &&
                   bindings[1].parameters ==
                       std::vector<ForeignAbiType>{ForeignAbiType::DnsHandle} &&
                   bindings[1].result == ForeignAbiType::CString &&
                   bindings[2].parameters ==
                       std::vector<ForeignAbiType>{ForeignAbiType::DnsHandle} &&
                   bindings[2].result == ForeignAbiType::I32,
               "semantic bảo toàn kiểu opaque c_dns qua từng descriptor");
    }
    std::size_t dnsIrCalls = 0;
    for (const auto &value : dnsArtifacts.ir.values) {
        if (value.opcode == IrValueOpcode::ForeignCall) ++dnsIrCalls;
    }
    std::size_t dnsBytecodeCalls = 0;
    for (const auto &instruction : dnsArtifacts.bytecode) {
        if (instruction.op == OP_FFI_CALL) ++dnsBytecodeCalls;
    }
    expect(dnsIrCalls == 3 && dnsBytecodeCalls == 3,
           "resolver đi qua ForeignCall/OP_FFI_CALL, không hạ opcode DNS cũ");

    const std::string socketSource =
        "ngoại thư viện net = \"system.net\";\n"
        "ngoại thư viện c = \"system.c\";\n"
        "ngoại hàm kết_nối(ip: c_chuỗi, cổng: i32, udp: i32, chờ: i32): c_socket từ net "
        "ký hiệu \"socket_connect\" khả năng \"system.net.socket.connect\";\n"
        "ngoại hàm loại(mã: c_socket): i32 từ net "
        "ký hiệu \"socket_kind\" khả năng \"system.net.socket.kind\";\n"
        "ngoại hàm gửi(mã: c_socket, dữ_liệu: c_đệm_vào[đếm], đếm: u64, cờ: i32): i64 từ c "
        "ký hiệu \"send\" khả năng \"system.net.socket.send\";\n"
        "ngoại hàm nhận(mã: c_socket, đệm: c_đệm_ra[đếm], đếm: u64, cờ: i32): i64 từ c "
        "ký hiệu \"recv\" khả năng \"system.net.socket.recv\";\n"
        "ngoại hàm đóng(mã: c_socket): i32 từ c "
        "ký hiệu \"close\" khả năng \"system.net.socket.close\";\n"
        "mã = kết_nối(\"127.0.0.1\", 80, 0, 1000);\n"
        "in loại(mã);\n"
        "in gửi(mã, [65], 1, 0);\n"
        "in nhận(mã, [0], 1, 0);\n"
        "in đóng(mã);\n";
    CompilationContext socketContext;
    const auto socketArtifacts = compilePipeline(socketContext, socketSource,
                                                  keywordMap, false);
    expect(socketContext.foreignFunctions.size() == 5,
           "compiler đăng ký đủ socket connect/kind/send/recv/close");
    if (socketContext.foreignFunctions.size() == 5) {
        const auto &bindings = socketContext.foreignFunctions;
        expect(bindings[0].parameters ==
                   std::vector<ForeignAbiType>{ForeignAbiType::CString,
                       ForeignAbiType::I32, ForeignAbiType::I32, ForeignAbiType::I32} &&
                   bindings[0].result == ForeignAbiType::SocketHandle &&
                   bindings[1].parameters ==
                       std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle} &&
                   bindings[2].parameters ==
                       std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle,
                           ForeignAbiType::BufferIn, ForeignAbiType::U64,
                           ForeignAbiType::I32} &&
                   bindings[3].parameters[1] == ForeignAbiType::BufferOut &&
                   bindings[2].result == ForeignAbiType::I64 &&
                   bindings[3].result == ForeignAbiType::I64 &&
                   bindings[4].parameters ==
                       std::vector<ForeignAbiType>{ForeignAbiType::SocketHandle} &&
                   bindings[4].result == ForeignAbiType::I32 &&
                   bindings[2].capability == "system.net.socket.send" &&
                   bindings[3].capability == "system.net.socket.recv",
               "semantic giữ handle opaque, hướng buffer và quyền socket");
        const auto &inputExtent = bindings[2].bufferExtents;
        const auto &outputExtent = bindings[3].bufferExtents;
        expect(inputExtent.size() == 1 && outputExtent.size() == 1 &&
                   inputExtent[0].parameterIndex == 1 &&
                   inputExtent[0].lengthParameterIndex == 2 &&
                   outputExtent[0].parameterIndex == 1 &&
                   outputExtent[0].lengthParameterIndex == 2,
               "socket send/recv lưu đúng ràng buộc độ dài buffer");
    }
    std::size_t socketIrCalls = 0;
    for (const auto &value : socketArtifacts.ir.values) {
        if (value.opcode == IrValueOpcode::ForeignCall) ++socketIrCalls;
    }
    std::size_t socketBytecodeCalls = 0;
    for (const auto &instruction : socketArtifacts.bytecode) {
        if (instruction.op != OP_FFI_CALL) continue;
        expect(instruction.operandIndex < 5 &&
                   (instruction.operand == 1 || instruction.operand == 4),
               "socket opcode giữ descriptor ID và đúng arity");
        ++socketBytecodeCalls;
    }
    expect(socketIrCalls == 5 && socketBytecodeCalls == 5,
           "compiler phát đủ 5 ForeignCall/OP_FFI_CALL cho socket");

    const std::string extentSource =
        "ngoại thư viện c = \"system.c\";\n"
        "ngoại hàm đồng_hồ(mã: i32, đệm: c_đệm_ra[16]): i32 từ c "
        "ký hiệu \"clock_gettime\" khả năng \"system.time.realtime\";\n"
        "ngoại hàm entropy(đệm: c_đệm_ra[độ_dài], độ_dài: u64): i32 từ c "
        "ký hiệu \"getentropy\" khả năng \"system.entropy.read\";\n";
    CompilationContext extentContext;
    (void)compilePipeline(extentContext, extentSource, keywordMap, false);
    expect(extentContext.foreignFunctions.size() == 2,
           "pipeline giữ hai descriptor có độ dài đệm");
    if (extentContext.foreignFunctions.size() == 2) {
        const auto &fixed = extentContext.foreignFunctions[0].bufferExtents;
        const auto &bound = extentContext.foreignFunctions[1].bufferExtents;
        expect(fixed.size() == 1 && fixed[0].parameterIndex == 1 &&
                   fixed[0].lengthParameterIndex == -1 &&
                   fixed[0].fixedLength == 16 &&
                   bound.size() == 1 && bound[0].parameterIndex == 0 &&
                   bound[0].lengthParameterIndex == 1 &&
                   bound[0].fixedLength == 0,
               "parser/semantic/IR/bytecode bảo toàn fixed/bound buffer extent");
    }

    const auto expectRejected = [](const std::string &source) {
        try {
            CompilationContext rejectedContext;
            (void)compilePipeline(rejectedContext, source, keywordMap, false);
        } catch (const std::exception &) {
            return true;
        }
        return false;
    };

    expect(expectRejected(
               "ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ không_có "
               "ký hiệu \"getenv\" abi \"c\" khả năng \"system.env.read\";"),
           "semantic từ chối alias foreign library chưa khai báo");
    expect(expectRejected(
               "ngoại thư viện c = \"system.c\";"
               "ngoại hàm getenv(tên: chuỗi): c_chuỗi từ c "
               "ký hiệu \"getenv\" abi \"c\" khả năng \"system.env.read\";"),
           "semantic từ chối kiểu ABI tham số chưa hỗ trợ");
    expect(expectRejected(
               "ngoại thư viện c = \"system.c\";"
               "ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ c "
               "ký hiệu \"getenv\" abi \"stdcall\" khả năng \"system.env.read\";"),
           "semantic từ chối ABI ngoài c");
    expect(expectRejected(
               "ngoại thư viện c = \"system.c\";"
               "ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ c "
               "ký hiệu \"getenv\" abi \"c\";"),
           "parser từ chối foreign function thiếu capability");
    expect(expectRejected(
               "ngoại thư viện c = \"system.c\";"
               "ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ c "
               "ký hiệu \"getenv\" abi \"c\" khả năng \"system.env.read\";"
               "in getenv();"),
           "semantic từ chối foreign call sai arity");
    expect(expectRejected(
               "ngoại thư viện c = \"system.c\";"
               "ngoại hàm sai(số: i64): u128 từ c "
               "ký hiệu \"vpp_ffi_test_i64\" abi \"c\" khả năng \"test.ffi.int64\";"),
           "semantic từ chối ABI return 128-bit chưa hỗ trợ");
    expect(expectRejected(
               "ngoại thư viện c = \"system.c\";"
               "ngoại hàm sai(số: void): u64 từ c "
               "ký hiệu \"vpp_ffi_test_u64\" abi \"c\" khả năng \"test.ffi.int64\";"),
           "semantic từ chối tham số void của FFI 64-bit");

    const auto invalidExtent = [&](const std::string &parameters) {
        return expectRejected(
            "ngoại thư viện c = \"system.c\";"
            "ngoại hàm sai(" + parameters + "): i32 từ c "
            "ký hiệu \"getentropy\" khả năng \"system.entropy.read\";");
    };
    expect(invalidExtent("đệm: c_đệm_ra, n: u64"),
           "semantic từ chối đệm thiếu metadata độ dài");
    expect(invalidExtent("đệm: c_đệm_ra[0], n: u64") &&
               invalidExtent("đệm: c_đệm_ra[65537], n: u64"),
           "semantic từ chối độ dài cố định ngoài giới hạn");
    expect(invalidExtent("đệm: c_đệm_ra[sai], n: u64") &&
               invalidExtent("đệm: c_đệm_ra[đệm], n: u64"),
           "semantic từ chối tham chiếu độ dài không hợp lệ");
    expect(invalidExtent("đệm: c_đệm_ra[n], n: f64") &&
               invalidExtent("đệm: c_đệm_ra[n], n: c_chuỗi"),
           "semantic từ chối độ dài tham chiếu số thực hoặc chuỗi");
    expect(invalidExtent("đệm: c_chuỗi[8], n: u64") &&
               invalidExtent("đệm: c_đệm_vào[1.5], n: u64"),
           "parser/semantic từ chối metadata không phù hợp kiểu ABI");
}

void testContextualKeywordInOrdinaryCall() {
    using namespace vietvm::compiler;

    const std::string validSource =
        "hàm ký hiệu vùng lấy(vùng) { trả về vùng; };\n"
        "hàm chính() {\n"
        "  thử { ký hiệu vùng lấy(\"vi-VN\"); } bắt lỗi (e) { trả về sai; };\n"
        "  trả về đúng;\n"
        "};\n";
    try {
        CompilationContext context;
        const auto result = compilePipeline(context, validSource, keywordMap, false);
        expect(result.unsupportedDirectIrRegions == 0,
               "lời gọi tên nhiều từ bắt đầu bằng keyword ngữ cảnh được hỗ trợ trực tiếp");
        std::size_t ordinaryCalls = 0;
        for (const auto &instruction : result.bytecode) {
            ordinaryCalls += instruction.op == OP_GOI;
        }
        // Function bodies live in the compilation registry, not the root
        // bytecode (emitEntryPointCall is disabled for this test).
        for (const auto &[functionId, instructions] : context.functionBytecode) {
            (void)functionId;
            for (const auto &instruction : instructions) {
                ordinaryCalls += instruction.op == OP_GOI;
            }
        }
        expect(ordinaryCalls == 1,
               "lời gọi ký hiệu vùng lấy(...) phát đúng một OP_GOI");
    } catch (const std::exception &error) {
        expect(false, std::string("lời gọi keyword ngữ cảnh thất bại: ") + error.what());
    }

    // A prefix call followed by another expression must not bypass the
    // compatibility statement dispatcher; the whole statement must be read.
    bool rejectedTrailingExpression = false;
    try {
        CompilationContext context;
        (void)compilePipeline(context,
            "hàm ký hiệu vùng lấy(vùng) { trả về vùng; }; "
            "hàm chính() { ký hiệu vùng lấy(1) + 2; };",
            keywordMap, false);
    } catch (const std::exception &) {
        rejectedTrailingExpression = true;
    }
    expect(rejectedTrailingExpression,
           "từ chối biểu thức thừa sau lời gọi thường nhiều từ");

    // The `in` statement still dispatches to printing, not a function call.
    try {
        CompilationContext context;
        const auto result = compilePipeline(context, "in(123);", keywordMap, false);
        bool calledFunction = false;
        for (const auto &instruction : result.bytecode) {
            calledFunction |= instruction.op == OP_GOI;
        }
        expect(!calledFunction, "keyword câu lệnh in vẫn ưu tiên ngữ pháp in");
    } catch (const std::exception &) {
        // Rejection is also safe: a keyword must never be emitted as a call.
    }
}

void testPrimitivePipeline() {
    using namespace vietvm::compiler;
    using namespace vietvm::bytecode;
    const std::unordered_set<int> retiredOpCodes{
        OP_VM_KY_TU_UNICODE,
        OP_VM_MA_DIEM_UNICODE,
        OP_VM_SO_THUC_DAC_BIET,
        OP_VM_TASK_CANCEL,
        OP_VM_TASK_SCHEDULE,
        OP_VM_TASK_STATUS,
        OP_VM_TASK_WAIT,
    };
    // Each serialized VM opcode must have a descriptor or be explicitly retired.
    // An unregistered opcode would bypass intrinsic lowering and arity checking.
    for (int raw = OP_VM_BIEN_DICH_PHAN_TICH; raw <= OP_VM_FLOAT_FROM_BITS; ++raw) {
        if (retiredOpCodes.count(raw)) {
            expect(intrinsicByOpcode(raw) == nullptr && !isKnownOpcode(raw) &&
                       verifyBytecode({{raw, 0, 0, 0}}).has_value(),
                   "retired opcode is rejected: " + std::to_string(raw));
        } else {
            expect(intrinsicByOpcode(raw) != nullptr && isKnownOpcode(raw),
                   "VM opcode has intrinsic descriptor: " + std::to_string(raw));
        }
    }
    for (const auto *retired : {"ky_tu_unicode_vm", "ma_diem_unicode_vm",
                                "so_thuc_dac_biet_vm", "task_vm_schedule",
                                "task_vm_wait", "task_vm_cancel", "task_vm_status"}) {
        expect(intrinsicByName(retired) == nullptr,
               std::string("retired native name not callable: ") + retired);
    }
    std::unordered_set<int> opcodes;
    for (const auto &primitive : kVmIntrinsics) {
        const std::string name(primitive.name);
        expect(opcodes.insert(primitive.opcode).second, name + ": opcode duy nhất");
        expect(isKnownOpcode(primitive.opcode) &&
                   opcodeName(primitive.opcode).find("OP_VM_") == 0,
               name + ": opcode được đăng ký");
        expect(verifyBytecode({{primitive.opcode, 1, 0, 0}}).has_value(),
               name + ": verifier từ chối immediate không hợp lệ");
        std::string arguments;
        for (std::size_t i = 0; i < primitive.arity; ++i) {
            if (i != 0) arguments += ", ";
            arguments += "rỗng";
        }
        for (const std::string prefix : {"in ", "gọi "}) {
            CompilationContext context;
            auto artifacts = compilePipeline(context, prefix + name + "(" + arguments + ");",
                                             keywordMap, false);
            bool found = false;
            for (auto &value : artifacts.ir.values) {
                if (value.opcode != IrValueOpcode::Intrinsic) continue;
                found = value.intrinsicOpcode == primitive.opcode &&
                        value.operands.size() == primitive.arity + 1;
                // Mutating a validated IR must not silently emit a different primitive.
                const int saved = value.intrinsicOpcode;
                value.intrinsicOpcode = -1;
                expect(!analyzeDirectIrSupport(artifacts.ir).supported,
                       name + ": từ chối opcode IR hỏng");
                value.intrinsicOpcode = saved;
            }
            expect(found, name + ": parser/semantic hạ thành IR intrinsic");
            std::size_t emitted = 0;
            for (const auto &instruction : artifacts.bytecode) {
                emitted += instruction.op == primitive.opcode;
                expect(instruction.op != OP_GOI && instruction.op != OP_GOI_GIAN_TIEP,
                       name + ": không dispatch lời gọi theo tên");
            }
            expect(emitted == 1, name + ": phát đúng một primitive");
        }
        bool arityRejected = false;
        try {
            CompilationContext context;
            (void)compilePipeline(context, "in " + name + "(" + arguments +
                (arguments.empty() ? "rỗng" : ", rỗng") + ");", keywordMap, false);
        } catch (const std::exception &) {
            arityRejected = true;
        }
        expect(arityRejected, name + ": sai arity bị từ chối trước runtime");
    }

    CompilationContext context;
    const auto shadowed = compilePipeline(context,
        "hàm do_dai(x) { trả về 42; }; in do_dai([]);", keywordMap, false);
    for (const auto &value : shadowed.ir.values) {
        expect(value.opcode != IrValueOpcode::Intrinsic,
               "hàm thư viện/người dùng được ưu tiên hơn alias primitive không reserved");
    }
}

} // namespace

int main() {
    testMalformedAstReferencesBecomeUnsupportedIr();
    testInvalidIrIsRejectedBeforeEmission();
    testBytecodeVerifierRejectsMalformedMetadata();
    testForeignFfiPipeline();
    testContextualKeywordInOrdinaryCall();
    testPrimitivePipeline();

    if (failures != 0) {
        std::cerr << failures << " RC internal hardening check(s) failed\n";
        return 1;
    }
    std::cout << "RC internal hardening: PASS\n";
    return 0;
}
