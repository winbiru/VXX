#include <iostream>
#include <string>
#include <vector>

#include "common/vm_native_stdlib_helpers.h"
#include "vpp/runtime/error.h"
#include "vpp/runtime/object.h"
#include "vpp/runtime/vm_fixture.h"

namespace {

int failures = 0;

void fail(const std::string &name, const std::string &detail) {
    std::cerr << "FAIL: " << name << ": " << detail << '\n';
    ++failures;
}

void expect(bool condition, const std::string &name, const std::string &detail) {
    if (!condition) fail(name, detail);
}

int asInt(const StackValue &value, const std::string &name) {
    if (!std::holds_alternative<int>(value)) {
        fail(name, "expected integer value");
        return 0;
    }
    return std::get<int>(value);
}

std::string asString(const StackValue &value, const std::string &name) {
    if (!std::holds_alternative<std::string>(value)) {
        fail(name, "expected string value");
        return {};
    }
    return std::get<std::string>(value);
}

Instruction instruction(Opcode op, int operand = 0, int operandIndex = 0, int operandValue = 0) {
    return {op, operand, operandIndex, operandValue};
}

void testValueHandlerState() {
    VM vm({}, {});
    VMRuntimeFixture access(vm);
    access.push(make_int_value(7));
    access.push(make_int_value(5));
    access.executeValue(instruction(OP_CONG));

    expect(access.stack().size() == 1, "value handler", "binary operation must consume two values");
    expect(asInt(access.top(), "value handler") == 12,
           "value handler", "7 + 5 must leave 12 on the stack");
}

void testIndexHandlerState() {
    VM vm({}, {});
    VMRuntimeFixture access(vm);
    const StackValue listValue = make_list_value({make_int_value(1), make_int_value(2)});
    const ListHandle list = std::get<ListHandle>(listValue);

    access.push(listValue);
    access.push(make_int_value(1));
    access.executeIndex(instruction(OP_DOC_CHI_SO));
    expect(asInt(access.top(), "index read handler") == 2,
           "index read handler", "list[1] must produce 2");

    access.push(listValue);
    access.push(make_int_value(0));
    access.push(make_int_value(9));
    access.executeIndex(instruction(OP_GAN_CHI_SO));
    expect(asInt(list->elements[0], "index write handler") == 9,
           "index write handler", "list[0] must be mutated to 9");

    access.push(make_string_value(u8"Việt𠀀"));
    access.push(make_int_value(2));
    access.executeIndex(instruction(OP_DOC_CHI_SO));
    expect(std::holds_alternative<std::string>(access.top()) &&
               std::get<std::string>(access.top()) == u8"ệ",
           "index read handler", "UTF-8 string indexing must return a whole code point");
}

void testVariableAndCallFrameHandlerState() {
    VM vm({}, {});
    VMRuntimeFixture access(vm);

    access.push(make_int_value(10));
    access.push(make_int_value(3));
    access.executeVariable(instruction(OP_GAN));
    expect(access.hasVariable(3), "variable handler", "assignment must create variable 3");
    expect(asInt(access.variable(3), "variable handler") == 10,
           "variable handler", "variable 3 must contain 10");

    CallFrame frame;
    frame.args.push_back(make_int_value(41));
    frame.localsIndexed = true;
    access.pushCallFrame(frame);
    access.executeVariable(instruction(OP_PARAM, 0, 0, 0));
    expect(access.currentCallFrame().localsVec.size() == 1,
           "parameter handler", "parameter binding must create local slot 0");
    expect(asInt(access.currentCallFrame().localsVec[0], "parameter handler") == 41,
           "parameter handler", "argument 0 must bind to local slot 0");
}

void testCallHandlerState() {
    VM vm({}, {});
    vm.hamBytecodeMap.emplace(7, std::vector<Instruction>{
        instruction(OP_PARAM, 0, 0, 0),
        instruction(OP_TEN_BIEN_GIA_TRI, 0, 0, 0),
        instruction(OP_BIEN_SO, 1, 0, 0),
        instruction(OP_CONG),
        instruction(OP_TRA_VE),
    });
    VMRuntimeFixture access(vm);
    access.push(make_int_value(41));
    access.executeCall(instruction(OP_GOI, 1, 7, 0));

    expect(access.stack().size() == 1, "call handler", "function return must be pushed to caller stack");
    expect(asInt(access.top(), "call handler") == 42,
           "call handler", "direct function handler must return 42");
}

void testCallRejectsMissingStackArgument() {
    VM vm({}, {});
    vm.hamBytecodeMap.emplace(8, std::vector<Instruction>{
        instruction(OP_PARAM, 0, 0, 0),
        instruction(OP_TEN_BIEN_GIA_TRI, 0, 0, 0),
        instruction(OP_TRA_VE),
    });
    VMRuntimeFixture access(vm);

    bool sawMissingOperand = false;
    try {
        access.executeCall(instruction(OP_GOI, 1, 8, 0));
    } catch (const vietvm::runtime::RuntimeError &error) {
        sawMissingOperand =
            vietvm::runtime::detectRuntimeDiagnostic(error.diagnosticContext()) ==
            vietvm::runtime::RuntimeDiagnosticKind::MissingOperand;
    }

    expect(sawMissingOperand, "call stack operand contract",
           "a call must fail when bytecode requests more arguments than the stack contains");
    expect(access.callDepth() == 0, "call stack operand contract",
           "missing call operands must fail before a temporary call frame is created");
}

void testBranchHandlerState() {
    VM vm({instruction(OP_BIEN_SO), instruction(OP_BIEN_SO), instruction(OP_DUNG_CHUONG_TRINH)}, {});
    VMRuntimeFixture access(vm);
    access.push(make_int_value(0));

    const bool jumped = access.executeBranch(instruction(OP_JUMP_IF_FALSE, 2));
    expect(jumped, "branch handler", "false condition must request a jump");
    expect(access.pc() == 2, "branch handler", "false condition must set pc to target 2");
    expect(access.stack().empty(), "branch handler", "condition must be consumed");
}

void testSwitchAndBlockHandlerState() {
    VM vm({}, {});
    VMRuntimeFixture access(vm);
    access.push(make_int_value(2));
    access.executeSwitch(instruction(OP_CHON));
    expect(access.switchDepth() == 1, "switch handler", "OP_CHON must create a switch frame");
    expect(access.switchSkipping(), "switch handler", "switch starts in skipping mode");

    access.executeSwitch(instruction(OP_CA, 2, -1, 0));
    expect(access.switchMatched(), "switch handler", "matching OP_CA must mark the frame matched");
    expect(!access.switchSkipping(), "switch handler", "matching OP_CA must enable its body");

    VM blockVm({}, {});
    VMRuntimeFixture blockAccess(blockVm);
    blockAccess.executeBlock(instruction(OP_MO_KHOI));
    expect(blockAccess.blockDepth() == 1, "block handler", "open block must increment depth");
    blockAccess.executeBlock(instruction(OP_DONG_KHOI));
    expect(blockAccess.blockDepth() == 0, "block handler", "close block must restore depth");
}

void testExceptionHandlerState() {
    VM vm(std::vector<Instruction>(10, instruction(OP_DONG_LENH)), {});
    VMRuntimeFixture access(vm);
    access.executeBlock(instruction(OP_MO_KHOI));
    access.executeException(instruction(OP_THU, 8, 1, 0));
    expect(access.tryDepth() == 1, "exception handler", "OP_THU must push a try frame");

    // State created after entering `thử` must be removed before control reaches catch.
    access.executeBlock(instruction(OP_MO_KHOI));
    access.push(make_int_value(2));
    access.executeSwitch(instruction(OP_CHON));
    expect(access.blockDepth() == 2 && access.switchDepth() == 1,
           "exception handler", "nested control state must exist before throw");

    access.push(make_string_value("boom"));
    const bool jumped = access.executeException(instruction(OP_NEM));
    expect(jumped, "exception handler", "OP_NEM with a handler must jump to catch");
    expect(access.pc() == 8, "exception handler", "throw must set pc to catch address");
    expect(access.blockDepth() == 1 && access.blockStackDepth() == 1 && access.switchDepth() == 0,
           "exception handler", "throw must restore block/switch state captured by try");
    expect(asString(access.top(), "exception handler") == "boom",
           "exception handler", "thrown value must survive stack unwind");

    access.executeException(instruction(OP_BAT_LOI, 0, 1, 0));
    expect(access.hasVariable(1), "exception handler", "catch must bind the error variable");
    expect(asString(access.variable(1), "exception handler") == "boom",
           "exception handler", "catch variable must contain thrown value");
}

void testFatalRuntimeErrorUnwindsCallFrame() {
    VM vm({}, {});
    vm.hamBytecodeMap.emplace(12, std::vector<Instruction>{
        instruction(OP_BIEN_SO, 1),
        instruction(OP_BIEN_SO, 0),
        instruction(OP_CHIA),
        instruction(OP_TRA_VE),
    });
    VMRuntimeFixture access(vm);

    bool sawTypedRuntimeError = false;
    try {
        access.executeCall(instruction(OP_GOI, 0, 12, 0));
    } catch (const vietvm::runtime::RuntimeError &error) {
        sawTypedRuntimeError = error.kind() == vietvm::runtime::RuntimeErrorKind::VmFault;
    }
    expect(sawTypedRuntimeError, "runtime error contract",
           "fatal VM faults must propagate as typed RuntimeError");
    expect(access.callDepth() == 0, "runtime error contract",
           "failed child VM calls must unwind the caller call frame");
}

void testCallBoundaryArityRuntimeError() {
    VM vm({}, {"i:2"});
    vm.hamBytecodeMap.emplace(21, std::vector<Instruction>{
        instruction(OP_KHOI_TAO, 0, 0, 0),
        instruction(OP_PARAM, 0, 0, 0),
        instruction(OP_KHOI_TAO, 0, 1, 0),
        instruction(OP_PARAM_MAC_DINH, 0, 1, 1),
        instruction(OP_TEN_BIEN_GIA_TRI, 0, 0, 0),
        instruction(OP_TRA_VE),
    });
    VMRuntimeFixture access(vm);

    bool sawCallBoundary = false;
    try {
        access.executeCall(instruction(OP_GOI, 0, 21, 0));
    } catch (const vietvm::runtime::RuntimeError &error) {
        sawCallBoundary = error.kind() == vietvm::runtime::RuntimeErrorKind::CallBoundary;
    }
    expect(sawCallBoundary, "call boundary arity",
           "runtime calls with missing required arguments must raise CallBoundary");
    expect(access.callDepth() == 0, "call boundary arity",
           "arity failure must unwind the temporary caller frame");
}

void testCallDepthLimitRaisesControlledRuntimeError() {
    VM vm({}, {});
    vm.hamBytecodeMap.emplace(31, std::vector<Instruction>{
        instruction(OP_GOI, 0, 31, 0),
        instruction(OP_TRA_VE),
    });
    VMRuntimeFixture access(vm);
    access.setMaxCallDepth(3);

    bool sawCallBoundary = false;
    std::string message;
    try {
        access.executeCall(instruction(OP_GOI, 0, 31, 0));
    } catch (const vietvm::runtime::RuntimeError &error) {
        sawCallBoundary = error.kind() == vietvm::runtime::RuntimeErrorKind::CallBoundary;
        message = error.what();
    }
    expect(sawCallBoundary, "call depth limit",
           "deep recursion must raise a controlled CallBoundary error");
    expect(message.find("Độ sâu lời gọi vượt giới hạn 3") != std::string::npos,
           "call depth limit", "diagnostic must report the configured call-depth limit");
    expect(access.callDepth() == 0, "call depth limit",
           "call-depth failure must unwind every temporary caller frame");
}

// Khóa định dạng stack trace có cấu trúc và xác nhận lỗi được nhận diện từ
// opcode + giá trị thực tế thay vì caller truyền sẵn một mã lỗi.
void testRuntimeErrorFormatsStructuredStackTrace() {
    vietvm::runtime::RuntimeDiagnosticContext context;
    context.opcode = OP_CHIA;
    context.operandTexts = {"10", "0"};
    context.operandNumeric = {true, true};
    vietvm::runtime::RuntimeError error(
        "lỗi thử nghiệm",
        vietvm::runtime::RuntimeErrorKind::VmFault,
        context);
    const vietvm::runtime::RuntimeSourceLocation recursiveFrame{
        "src/tests/trace.vi", "src/tests/trace.vi", "điSâu", 4, 16};
    const vietvm::runtime::RuntimeSourceLocation callerFrame{
        "src/tests/trace.vi", "src/tests/trace.vi", "main", 8, 8};

    error.addFrame(recursiveFrame);
    error.addFrame(recursiveFrame);
    error.addFrame(callerFrame);

    const std::string formatted = vietvm::runtime::formatRuntimeError(error);
    expect(formatted.find("Mã lỗi:") == std::string::npos,
           "runtime diagnostic hides error code",
           "user-facing runtime errors must not expose an error code");
    expect(formatted.find("Điều đã xảy ra: Chương trình đang cố lấy 10 chia cho 0") != std::string::npos,
           "inferred runtime explanation",
           "formatter must infer division by zero from opcode and observed operands");
    expect(formatted.find("Cách sửa: Hãy kiểm tra số chia trước khi chia") != std::string::npos,
           "inferred runtime suggestion",
           "formatter must resolve an actionable suggestion from the inferred failure");
    expect(formatted.find(
               "ở điSâu (src/tests/trace.vi:4:16) [mô đun=src/tests/trace.vi] "
               "[lặp lại 2 khung]") != std::string::npos,
           "structured stack trace",
           "consecutive identical recursive frames must be compressed");
    const std::size_t recursivePosition = formatted.find("ở điSâu");
    const std::size_t callerPosition = formatted.find("ở main");
    expect(recursivePosition != std::string::npos &&
               callerPosition != std::string::npos &&
               recursivePosition < callerPosition,
           "structured stack trace",
           "stack frames must be rendered from failure site toward caller");
}

// Khóa cơ chế tự nhận diện chẩn đoán từ dữ kiện thực thi cho nhiều họ lỗi. Test
// không truyền `RuntimeDiagnosticKind` vào RuntimeError; enum chỉ dùng để kiểm tra
// kết quả mà bộ nhận diện trung tâm suy ra.
void testRuntimeDiagnosticInferenceCoversCommonErrors() {
    using vietvm::runtime::RuntimeDiagnosticContext;
    using vietvm::runtime::RuntimeDiagnosticKind;
    std::vector<std::pair<RuntimeDiagnosticContext, RuntimeDiagnosticKind>> cases;

    auto withOpcode = [](RuntimeDiagnosticContext context, Opcode opcode) {
        context.opcode = static_cast<int>(opcode);
        return context;
    };

    RuntimeDiagnosticContext division;
    division.operandTexts = {"10", "0"};
    division.operandNumeric = {true, true};
    cases.push_back({withOpcode(division, OP_CHIA), RuntimeDiagnosticKind::DivisionByZero});

    RuntimeDiagnosticContext modulo = division;
    cases.push_back({withOpcode(modulo, OP_MODULO), RuntimeDiagnosticKind::ModuloByZero});

    RuntimeDiagnosticContext numeric;
    numeric.operandTexts = {"abc", "2"};
    numeric.operandNumeric = {false, true};
    cases.push_back({withOpcode(numeric, OP_NHAN), RuntimeDiagnosticKind::NumericOperandRequired});

    RuntimeDiagnosticContext compare;
    compare.operandTexts = {"1", "abc"};
    cases.push_back({withOpcode(compare, OP_LON_HON), RuntimeDiagnosticKind::ComparisonTypeMismatch});

    RuntimeDiagnosticContext integer;
    integer.expectsInteger = true;
    integer.actualType = "chuỗi";
    cases.push_back({withOpcode(integer, OP_KHONG), RuntimeDiagnosticKind::IntegerRequired});

    cases.push_back({withOpcode(vietvm::runtime::runtimeConversionFacts("abc", "số thực", false), OP_BIEN_SO_FLOAT),
                     RuntimeDiagnosticKind::FloatConversionFailed});
    cases.push_back({withOpcode(vietvm::runtime::runtimeIndexFacts(0, false, -1, true, "chuỗi"), OP_DOC_CHI_SO),
                     RuntimeDiagnosticKind::IndexTypeInvalid});
    cases.push_back({withOpcode(vietvm::runtime::runtimeIndexFacts(5, true, 3, true), OP_DOC_CHI_SO),
                     RuntimeDiagnosticKind::IndexOutOfRange});
    cases.push_back({withOpcode(vietvm::runtime::runtimeIndexFacts(0, true, -1, false, "đối tượng"), OP_DOC_CHI_SO),
                     RuntimeDiagnosticKind::ContainerNotIndexable});
    cases.push_back({withOpcode(vietvm::runtime::runtimeTargetLookupFacts("tính", false), OP_GOI),
                     RuntimeDiagnosticKind::FunctionNotFound});
    cases.push_back({withOpcode(vietvm::runtime::runtimeTargetLookupFacts("biếnHàm", false), OP_GOI_GIAN_TIEP),
                     RuntimeDiagnosticKind::InvalidCallable});
    cases.push_back({vietvm::runtime::runtimeCallFacts("tính", 3, 1, 2),
                     RuntimeDiagnosticKind::CallArityMismatch});
    cases.push_back({vietvm::runtime::runtimeCallDepthFacts("điSâu", 257, 256),
                     RuntimeDiagnosticKind::CallDepthExceeded});
    cases.push_back({vietvm::runtime::runtimeRequiredArgumentFacts(2),
                     RuntimeDiagnosticKind::RequiredArgumentMissing});
    cases.push_back({vietvm::runtime::runtimeModuleFacts("demo", false),
                     RuntimeDiagnosticKind::ModuleInitializationFailed});
    cases.push_back({withOpcode(vietvm::runtime::runtimeTargetLookupFacts("Con", false), OP_TAO_DOI_TUONG),
                     RuntimeDiagnosticKind::ClassNotFound});
    cases.push_back({withOpcode(vietvm::runtime::runtimeMemberFacts("tên", false, false, true), OP_DOC_THUOC_TINH),
                     RuntimeDiagnosticKind::ObjectRequired});
    cases.push_back({withOpcode(vietvm::runtime::runtimeMemberFacts("tên", true, true, false), OP_DOC_THUOC_TINH),
                     RuntimeDiagnosticKind::PropertyNotFound});
    cases.push_back({withOpcode(vietvm::runtime::runtimeMemberFacts("chạy", true, true, false), OP_GOI_PHUONG_THUC),
                     RuntimeDiagnosticKind::MethodNotFound});
    cases.push_back({withOpcode(vietvm::runtime::runtimeMemberFacts("bíMật", true, true, true, true, false), OP_GOI_PHUONG_THUC),
                     RuntimeDiagnosticKind::MemberAccessDenied});
    cases.push_back({withOpcode(vietvm::runtime::runtimeStackFacts(1, 2), OP_CONG),
                     RuntimeDiagnosticKind::MissingOperand});
    cases.push_back({withOpcode(vietvm::runtime::runtimeTypeFacts("chuỗi"), OP_CONG_MOT),
                     RuntimeDiagnosticKind::IncrementTypeInvalid});
    cases.push_back({withOpcode(vietvm::runtime::runtimeTypeFacts("danh sách"), OP_TRU_MOT),
                     RuntimeDiagnosticKind::DecrementTypeInvalid});
    cases.push_back({withOpcode(vietvm::runtime::runtimeConstantReferenceFacts(false), OP_CHUOI),
                     RuntimeDiagnosticKind::ConstantReferenceInvalid});
    cases.push_back({withOpcode(vietvm::runtime::runtimeLiteralDecodeFacts("dữ liệu hỏng", false), OP_LIST_LITERAL),
                     RuntimeDiagnosticKind::LiteralDecodeFailed});
    cases.push_back({withOpcode(vietvm::runtime::runtimeClosureFacts("capture", false), OP_TAO_DONG_BAO),
                     RuntimeDiagnosticKind::ClosureCaptureInvalid});
    cases.push_back({withOpcode(vietvm::runtime::runtimeJumpFacts(999, false), OP_JUMP),
                     RuntimeDiagnosticKind::JumpTargetInvalid});
    cases.push_back({withOpcode(vietvm::runtime::runtimeControlFacts("ca ngoài chọn", false), OP_CA),
                     RuntimeDiagnosticKind::ControlFlowStateInvalid});
    cases.push_back({vietvm::runtime::runtimeNativeFacts("đọc tệp", "không mở được tệp", false),
                     RuntimeDiagnosticKind::NativeOperationFailed});

    RuntimeDiagnosticContext internal;
    internal.internalInvariantChecked = true;
    internal.internalInvariantValid = false;
    cases.push_back({internal, RuntimeDiagnosticKind::InternalRuntimeState});

    for (const auto &[context, expectedKind] : cases) {
        const RuntimeDiagnosticKind detected = vietvm::runtime::detectRuntimeDiagnostic(context);
        const vietvm::runtime::RuntimeDiagnosticInfo info =
            vietvm::runtime::runtimeDiagnosticInfo(context);
        expect(detected == expectedKind,
               "runtime diagnostic inference",
               "runtime facts must resolve to the expected diagnostic kind");
        expect(!info.category.empty() && !info.explanation.empty() && !info.suggestion.empty(),
               "runtime diagnostic explanation coverage",
               "every inferred runtime failure must have a complete explanation and fix");
    }
}

// Khóa invariant sau lỗi fatal ở child VM: stack/root của caller phải còn nguyên,
// call frame tạm phải được unwind và cùng VM vẫn gọi được function hợp lệ tiếp theo.
void testRuntimeErrorPreservesCallerStateForNextCall() {
    VM vm({}, {});
    vm.hamBytecodeMap.emplace(41, std::vector<Instruction>{
        instruction(OP_BIEN_SO, 1),
        instruction(OP_BIEN_SO, 0),
        instruction(OP_CHIA),
        instruction(OP_TRA_VE),
    });
    vm.hamBytecodeMap.emplace(42, std::vector<Instruction>{
        instruction(OP_BIEN_SO, 7),
        instruction(OP_TRA_VE),
    });
    VMRuntimeFixture access(vm);

    StackValue rootedValue = make_list_value({});
    ListHandle rooted = std::get<ListHandle>(rootedValue);
    rooted->elements.push_back(rootedValue);
    const std::weak_ptr<vietvm::runtime::ListValue> weakRoot = rooted;
    access.trackHeapValue(rootedValue);
    access.setVariable(88, rootedValue);
    rooted.reset();
    rootedValue = make_null_value();

    access.push(make_int_value(1234));
    bool sawVmFault = false;
    try {
        access.executeCall(instruction(OP_GOI, 0, 41, 0));
    } catch (const vietvm::runtime::RuntimeError &error) {
        sawVmFault = error.kind() == vietvm::runtime::RuntimeErrorKind::VmFault;
    }

    expect(sawVmFault, "runtime state after error",
           "the intentionally failing child call must raise VmFault");
    expect(access.callDepth() == 0, "runtime state after error",
           "a failed child call must leave no temporary caller frame");
    expect(access.stack().size() == 1 &&
               asInt(access.top(), "runtime state after error") == 1234,
           "runtime state after error",
           "a zero-argument failed call must preserve unrelated caller stack values");

    access.collectGarbage();
    expect(!weakRoot.expired(), "runtime state after error",
           "a heap graph rooted in caller variables must survive GC after child failure");

    access.executeCall(instruction(OP_GOI, 0, 42, 0));
    expect(access.callDepth() == 0, "runtime reuse after error",
           "a later successful call must also leave the call stack balanced");
    expect(access.stack().size() == 2 &&
               asInt(access.top(), "runtime reuse after error") == 7,
           "runtime reuse after error",
           "the same VM must remain usable for a successful call after a prior VmFault");

    access.eraseVariable(88);
    access.clearStack();
    access.collectGarbage();
    expect(weakRoot.expired(), "runtime state after error",
           "the preserved heap graph must still become collectible after its final root is removed");
}

void testLoopControlHandlerState() {
    VM vm({
        instruction(OP_BO_QUA),
        instruction(OP_BIEN_SO, 99),
        instruction(OP_CAP_NHAT),
        instruction(OP_DUNG_CHUONG_TRINH),
    }, {});
    VMRuntimeFixture access(vm);
    access.setPc(0);
    access.executeLoopControl(instruction(OP_BO_QUA));
    expect(access.pc() == 2, "loop-control handler", "continue must target the nearest update marker");
}

void testOutputHandlerUsesSink() {
    VM vm({}, {});
    VMRuntimeFixture access(vm);
    std::string output;
    access.setOutputSink([&output](const std::string &text) { output += text; });
    access.push(make_int_value(42));
    access.executeOutput(instruction(OP_IN));

    expect(output == "[IN] 42\n",
           "output handler", "OP_IN must emit through the configured sink");
    expect(access.stack().empty(),
           "output handler", "OP_IN must consume the emitted value");
}

void testFilesystemPredicatesTreatMissingPathAsFalse() {
    StackValue result = make_null_value();
    std::string error;
    const std::vector<StackValue> args = {
        make_string_value(".tmp_vpp_path_that_must_not_exist/không-có.txt")
    };

    const bool handled = vietvm::helpers::handleNativeFoundationFunction(
        "là tệp", args, result, error);
    expect(handled, "filesystem predicate", "là tệp must be handled by foundation native layer");
    expect(error.empty(), "filesystem predicate", "a missing path must not be reported as an OS error");
    expect(asInt(result, "filesystem predicate") == 0,
           "filesystem predicate", "là tệp(missing) must return 0");
}

void testFilesystemPathUtf8Boundary() {
    StackValue result = make_null_value();
    std::string error;

    const bool joined = vietvm::helpers::handleNativeFoundationFunction(
        "đường dẫn nối",
        {make_string_value(u8"thư mục"), make_string_value(u8"con𠀀/mẫu.txt")},
        result,
        error);
    expect(joined, "filesystem UTF-8 path", "path join must be handled by foundation native layer");
    expect(error.empty(), "filesystem UTF-8 path", "valid Unicode paths must not produce an error");
    expect(asString(result, "filesystem UTF-8 path") == u8"thư mục/con𠀀/mẫu.txt",
           "filesystem UTF-8 path",
           "path join must preserve Unicode and expose generic separators");

    result = make_null_value();
    error.clear();
    const std::string malformed =
        std::string("bad/") + static_cast<char>(0xc0) + static_cast<char>(0xaf);
    const bool rejected = vietvm::helpers::handleNativeFoundationFunction(
        "đường dẫn tồn tại", {make_string_value(malformed)}, result, error);
    expect(rejected, "filesystem UTF-8 path", "malformed path input must stay inside native dispatch");
    expect(error.find("UTF-8") != std::string::npos,
           "filesystem UTF-8 path",
           "malformed path input must be rejected as an invalid UTF-8 path");
}

void testFoundationStrictIntegerContract() {
    StackValue result = make_null_value();
    std::string error;

    const bool randomRejected = vietvm::helpers::handleNativeFoundationFunction(
        "ngẫu nhiên nguyên",
        {make_float_value(1.5), make_int_value(2)},
        result,
        error);
    expect(randomRejected,
           "strict integer native contract",
           "ngẫu nhiên nguyên must be handled by the foundation native layer");
    expect(error.find("số nguyên") != std::string::npos,
           "strict integer native contract",
           "fractional random bounds must be rejected instead of truncated");

    result = make_null_value();
    error.clear();
    const bool integralDoubleAccepted = vietvm::helpers::handleNativeFoundationFunction(
        "ngẫu nhiên nguyên",
        {make_float_value(5.0), make_float_value(5.0)},
        result,
        error);
    expect(integralDoubleAccepted && error.empty(),
           "strict integer native contract",
           "integral floating-point bounds must remain valid integer inputs");
    expect(asInt(result, "strict integer native contract") == 5,
           "strict integer native contract",
           "equal integral bounds must return that exact value");

    result = make_null_value();
    error.clear();
    const bool sleepRejected = vietvm::helpers::handleNativeFoundationFunction(
        "ngủ mili giây", {make_float_value(0.5)}, result, error);
    expect(sleepRejected,
           "strict integer native contract",
           "ngủ mili giây must be handled by the foundation native layer");
    expect(error.find("số nguyên không âm") != std::string::npos,
           "strict integer native contract",
           "fractional sleep duration must be rejected instead of truncated");
}

void testEnvironmentVariableContract() {
    StackValue result = make_null_value();
    std::string error;

    const bool fallbackHandled = vietvm::helpers::handleNativeFoundationFunction(
        "đọc biến môi trường",
        {make_string_value("__VPP_TEST_BIEN_KHONG_TON_TAI_7F0C4D0A__"),
         make_string_value(u8"mặc định")},
        result,
        error);
    expect(fallbackHandled,
           "environment variable contract",
           "đọc biến môi trường must be handled by the foundation native layer");
    expect(error.empty(),
           "environment variable contract",
           "a valid missing variable name must use the supplied fallback without an error");
    expect(asString(result, "environment variable contract") == u8"mặc định",
           "environment variable contract",
           "a valid missing variable must return the exact fallback value");

    const std::vector<std::string> invalidNames = {
        "",
        "VPP=KHONG_HOP_LE",
        std::string("VPP\0AN", 6),
        std::string("VPP_") + static_cast<char>(0xc0) + static_cast<char>(0xaf)
    };
    for (const std::string &name : invalidNames) {
        result = make_null_value();
        error.clear();
        const bool handled = vietvm::helpers::handleNativeFoundationFunction(
            "đọc biến môi trường",
            {make_string_value(name), make_string_value("fallback")},
            result,
            error);
        expect(handled,
               "environment variable contract",
               "invalid environment names must stay inside native dispatch");
        expect(!error.empty(),
               "environment variable contract",
               "invalid environment names must be rejected before calling the host API");
    }
}

void testTimeContract() {
    StackValue result = make_null_value();
    std::string error;

    const bool localHandled = vietvm::helpers::handleNativeFoundationFunction(
        "lấy thời gian hiện tại", {}, result, error);
    expect(localHandled, "time contract", "local time must be handled by foundation native layer");
    expect(error.empty(), "time contract", "local time must not produce an error");
    const std::string local = asString(result, "time contract");
    expect(local.size() == 25 && local[4] == '-' && local[7] == '-' &&
               local[10] == 'T' && local[13] == ':' && local[16] == ':' &&
               (local[19] == '+' || local[19] == '-') && local[22] == ':',
           "time contract",
           "local time must use ISO-8601 with an explicit UTC offset");

    result = make_null_value();
    error.clear();
    const bool utcHandled = vietvm::helpers::handleNativeFoundationFunction(
        "lấy thời gian utc", {}, result, error);
    expect(utcHandled, "time contract", "UTC time must be handled by foundation native layer");
    expect(error.empty(), "time contract", "UTC time must not produce an error");
    const std::string utc = asString(result, "time contract");
    expect(utc.size() == 20 && utc[4] == '-' && utc[7] == '-' && utc[10] == 'T' &&
               utc[13] == ':' && utc[16] == ':' && utc[19] == 'Z',
           "time contract", "UTC time must use ISO-8601 with a Z suffix");

    result = make_null_value();
    error.clear();
    const bool offsetHandled = vietvm::helpers::handleNativeFoundationFunction(
        "độ lệch múi giờ", {}, result, error);
    expect(offsetHandled, "time contract", "timezone offset must be handled by foundation native layer");
    expect(error.empty(), "time contract", "timezone offset must not produce an error");
    const int offsetMinutes = asInt(result, "time contract");
    expect(offsetMinutes >= -24 * 60 && offsetMinutes <= 24 * 60,
           "time contract", "timezone offset must be represented in minutes");
    if (local.size() == 25) {
        const int sign = local[19] == '-' ? -1 : 1;
        const int suffixMinutes = sign *
            (std::stoi(local.substr(20, 2)) * 60 + std::stoi(local.substr(23, 2)));
        expect(suffixMinutes == offsetMinutes,
               "time contract", "local ISO-8601 suffix must match timezone offset API");
    }

    result = make_null_value();
    error.clear();
    const bool invalidArityHandled = vietvm::helpers::handleNativeFoundationFunction(
        "lấy thời gian hiện tại", {make_int_value(1)}, result, error);
    expect(invalidArityHandled, "time contract", "time function must stay inside native dispatch");
    expect(!error.empty(), "time contract", "time function must reject unexpected arguments");
}

void testRuntimeModuleInitializationRunsOnce() {
    VM vm({
        instruction(OP_CHUOI, 0, 1, 0),
        instruction(OP_IN),
        instruction(OP_DUNG_CHUONG_TRINH),
    }, {"module-init", "entry"});

    const std::vector<Instruction> initializer = {
        instruction(OP_CHUOI, 0, 0, 0),
        instruction(OP_IN),
    };
    expect(vm.addModuleInitializer("module://alpha", initializer),
           "module runtime", "first module registration must succeed");
    expect(!vm.addModuleInitializer("module://alpha", initializer),
           "module runtime", "duplicate module identity must be ignored");
    expect(vm.moduleState("module://alpha") ==
               vietvm::runtime::ModuleState::Uninitialized,
           "module runtime", "registered module starts uninitialized");

    std::string output;
    vm.setOutputSink([&output](const std::string &text) { output += text; });
    vm.run();
    expect(vm.moduleState("module://alpha") ==
               vietvm::runtime::ModuleState::Initialized,
           "module runtime", "successful initializer becomes initialized");
    expect(output == "[IN] module-init\n[IN] entry\n",
           "module runtime", "module initializer runs before entry bytecode");

    vm.run();
    expect(output == "[IN] module-init\n[IN] entry\n",
           "module runtime", "initialized module is never executed twice");

    VM failing({}, {});
    const std::vector<Instruction> invalidInitializer = {
        instruction(OP_BIEN_SO, 1),
        instruction(OP_BIEN_SO, 0),
        instruction(OP_CHIA),
    };
    (void)failing.addModuleInitializer("module://broken", invalidInitializer);
    bool failed = false;
    try {
        failing.run();
    } catch (const std::runtime_error &) {
        failed = true;
    }
    expect(failed && failing.moduleState("module://broken") ==
                         vietvm::runtime::ModuleState::Failed,
           "module runtime", "initializer exception permanently records failed state");

    bool sawModuleFailureKind = false;
    try {
        failing.run();
    } catch (const vietvm::runtime::RuntimeError &error) {
        sawModuleFailureKind =
            error.kind() == vietvm::runtime::RuntimeErrorKind::ModuleInitialization;
    }
    expect(sawModuleFailureKind, "module runtime",
           "rerunning a failed module must report typed module-initialization failure");
}

void testObjectHandlerState() {
    VM vm({}, {"Counter", "value", "add", "read", "Base", "Child", "speak", "callBase"});
    vm.hamBytecodeMap.emplace(7, std::vector<Instruction>{
        instruction(OP_PARAM, 0, 0, 0),
        instruction(OP_PARAM, 0, 1, 1),
        instruction(OP_TEN_BIEN_GIA_TRI, 0, 0, 0),
        instruction(OP_TEN_BIEN_GIA_TRI, 0, 1, 0),
        instruction(OP_CONG),
        instruction(OP_TRA_VE),
    });
    vm.hamBytecodeMap.emplace(8, std::vector<Instruction>{
        instruction(OP_PARAM, 0, 2, -1),
        instruction(OP_TEN_BIEN_GIA_TRI, 0, 2, 0),
        instruction(OP_DOC_THUOC_TINH, 0, 1, 0),
        instruction(OP_TRA_VE),
    });
    vm.hamBytecodeMap.emplace(9, std::vector<Instruction>{
        instruction(OP_BIEN_SO, 11, 0, 0),
        instruction(OP_TRA_VE),
    });
    vm.hamBytecodeMap.emplace(10, std::vector<Instruction>{
        instruction(OP_BIEN_SO, 22, 0, 0),
        instruction(OP_TRA_VE),
    });
    vm.hamBytecodeMap.emplace(11, std::vector<Instruction>{
        instruction(OP_PARAM, 0, 0, -1),
        instruction(OP_TEN_BIEN_GIA_TRI, 0, 0, 0),
        instruction(OP_GOI_PHUONG_THUC, 0, 6, 1),
        instruction(OP_TRA_VE),
    });
    VMRuntimeFixture access(vm);

    access.executeObject(instruction(OP_TAO_LOP, 0, 0, 0));
    access.executeObject(instruction(OP_THEM_PHUONG_THUC, 0, 2, 7));
    access.executeObject(instruction(OP_TAO_DOI_TUONG, 0, 0, 0));
    expect(std::holds_alternative<InstanceHandle>(access.top()),
           "object handler", "construction must push a runtime instance");
    const StackValue instance = access.top();

    access.push(instance);
    access.push(make_int_value(9));
    access.executeObject(instruction(OP_GAN_THUOC_TINH, 0, 1, 0));
    access.push(instance);
    access.executeObject(instruction(OP_DOC_THUOC_TINH, 0, 1, 0));
    expect(asInt(access.top(), "object field handler") == 9,
           "object field handler", "field write followed by read must preserve the value");

    access.push(instance);
    access.push(make_int_value(2));
    access.push(make_int_value(3));
    access.executeObject(instruction(OP_GOI_PHUONG_THUC, 2, 2, 0));
    expect(asInt(access.top(), "object method handler") == 5,
           "object method handler", "bound dispatch must call the registered method function");

    access.executeObject(instruction(OP_THEM_PHUONG_THUC, 0, 3, 8));
    access.push(instance);
    access.executeObject(instruction(OP_GOI_PHUONG_THUC, 0, 3, 0));
    expect(asInt(access.top(), "implicit receiver handler") == 9,
           "implicit receiver handler",
           "bound dispatch must expose the instance through hidden OP_PARAM index -1");

    const ClassHandle base = vietvm::runtime::createClass("Base");
    const ClassHandle child = vietvm::runtime::createClass("Child", base);
    (void)vietvm::runtime::defineMethod(base, "speak", 9);
    (void)vietvm::runtime::defineMethod(child, "speak", 10);
    (void)vietvm::runtime::defineMethod(child, "callBase", 11);
    access.setClass("Base", base);
    access.setClass("Child", child);
    const InstanceHandle childInstance = vietvm::runtime::createInstance(child);
    access.push(make_instance_value(childInstance));
    access.executeObject(instruction(OP_GOI_PHUONG_THUC, 0, 7, 0));
    expect(asInt(access.top(), "super receiver handler") == 11,
           "super receiver handler",
           "gốc dispatch must skip the child override and call the superclass method");
}

void testTracingGcKeepsRootsAndCollectsCycles() {
    VM vm({}, {});
    VMRuntimeFixture access(vm);

    ClassHandle klass = vietvm::runtime::createClass("Node");
    access.setClass("Node", klass);
    StackValue listValue = make_list_value({});
    ListHandle list = std::get<ListHandle>(listValue);
    InstanceHandle instance = vietvm::runtime::createInstance(klass);
    StackValue instanceValue = make_instance_value(instance);
    (void)vietvm::runtime::setInstanceField(instance, "items", listValue);
    list->elements.push_back(instanceValue);

    const std::weak_ptr<vietvm::runtime::ListValue> weakList = list;
    const std::weak_ptr<vietvm::runtime::RuntimeInstance> weakInstance = instance;
    access.trackHeapValue(instanceValue);
    access.setVariable(77, instanceValue);

    list.reset();
    instance.reset();
    listValue = make_null_value();
    instanceValue = make_null_value();

    access.collectGarbage();
    expect(!weakList.expired() && !weakInstance.expired(),
           "tracing GC roots",
           "a cycle reachable through VM variables must survive mark/sweep");
    expect(access.gcStats().marked >= 3 && access.gcStats().swept == 0,
           "tracing GC roots",
           "reachable class, instance and list must all be marked without sweeping");

    access.eraseVariable(77);
    access.collectGarbage();
    expect(weakList.expired() && weakInstance.expired(),
           "tracing GC cycle sweep",
           "an unreachable instance/list cycle must be broken and released");
    expect(access.gcStats().swept >= 1 && access.trackedHeapObjects() == 1,
           "tracing GC cycle sweep",
           "sweep must retain only the class rooted by the VM class table");
}

void testTracingGcHeapTeardownBreaksRootedCycles() {
    std::weak_ptr<vietvm::runtime::ListValue> weakList;
    {
        VM vm({}, {});
        VMRuntimeFixture access(vm);
        StackValue cycleValue = make_list_value({});
        ListHandle cycle = std::get<ListHandle>(cycleValue);
        cycle->elements.push_back(cycleValue);
        weakList = cycle;
        access.trackHeapValue(cycleValue);
        access.setVariable(91, cycleValue);
        cycle.reset();
        cycleValue = make_null_value();
    }
    expect(weakList.expired(),
           "tracing GC heap teardown",
           "destroying the final VM heap owner must break cycles still held by VM roots");
}

// Khóa việc marker/track traversal xử lý graph rất sâu bằng worklist thay vì
// phụ thuộc native C++ recursion; cycle vẫn phải sống khi còn root và được sweep khi bỏ root.
void testTracingGcHandlesDeepGraphWithoutNativeRecursion() {
    constexpr std::size_t kNodeCount = 4096;
    VM vm({}, {});
    VMRuntimeFixture access(vm);

    StackValue rootValue = make_list_value({});
    std::vector<ListHandle> nodes;
    nodes.reserve(kNodeCount);
    nodes.push_back(std::get<ListHandle>(rootValue));
    for (std::size_t i = 1; i < kNodeCount; ++i) {
        StackValue nodeValue = make_list_value({});
        nodes.push_back(std::get<ListHandle>(nodeValue));
    }
    for (std::size_t i = 0; i + 1 < kNodeCount; ++i) {
        nodes[i]->elements.emplace_back(nodes[i + 1]);
    }
    nodes.back()->elements.emplace_back(nodes.front());

    const std::weak_ptr<vietvm::runtime::ListValue> weakFirst = nodes.front();
    const std::weak_ptr<vietvm::runtime::ListValue> weakMiddle = nodes[kNodeCount / 2];
    const std::weak_ptr<vietvm::runtime::ListValue> weakLast = nodes.back();

    access.trackHeapValue(rootValue);
    access.setVariable(101, rootValue);
    nodes.clear();
    rootValue = make_null_value();

    access.collectGarbage();
    expect(!weakFirst.expired() && !weakMiddle.expired() && !weakLast.expired(),
           "tracing GC deep graph",
           "a deeply linked cycle reachable from a VM root must survive collection");
    expect(access.gcStats().marked >= kNodeCount &&
               access.trackedHeapObjects() == kNodeCount,
           "tracing GC deep graph",
           "the iterative marker must visit the complete deep graph without native recursion");

    access.eraseVariable(101);
    access.collectGarbage();
    expect(weakFirst.expired() && weakMiddle.expired() && weakLast.expired(),
           "tracing GC deep graph sweep",
           "removing the final root must release the entire deep cyclic graph");
    expect(access.trackedHeapObjects() == 0,
           "tracing GC deep graph sweep",
           "deep-cycle sweep must leave no tracked list allocation behind");
}

// Tạo một burst lớn các self-cycle không có root để kiểm tra registry/sweep dọn
// toàn bộ allocation liên tục trong một chu kỳ GC mà không để weak entry sống sót.
void testTracingGcSweepsAllocationBurst() {
    constexpr std::size_t kAllocationCount = 1024;
    VM vm({}, {});
    VMRuntimeFixture access(vm);
    std::vector<std::weak_ptr<vietvm::runtime::ListValue>> weakLists;
    weakLists.reserve(kAllocationCount);

    for (std::size_t i = 0; i < kAllocationCount; ++i) {
        StackValue value = make_list_value({});
        ListHandle list = std::get<ListHandle>(value);
        list->elements.push_back(value);
        weakLists.push_back(list);
        access.trackHeapValue(value);
        list.reset();
        value = make_null_value();
    }

    expect(access.trackedHeapObjects() == kAllocationCount,
           "tracing GC allocation burst",
           "all cyclic allocations must remain registered before collection");
    access.collectGarbage();

    bool allReleased = true;
    for (const auto &weak : weakLists) {
        if (!weak.expired()) {
            allReleased = false;
            break;
        }
    }
    expect(allReleased, "tracing GC allocation burst",
           "one collection must release an allocation burst with no live roots");
    expect(access.gcStats().trackedBefore == kAllocationCount &&
               access.gcStats().trackedAfter == 0 &&
               access.trackedHeapObjects() == 0,
           "tracing GC allocation burst",
           "heap stats must report the burst fully removed after sweep");
}

} // namespace

int main() {
    testValueHandlerState();
    testIndexHandlerState();
    testVariableAndCallFrameHandlerState();
    testCallHandlerState();
    testCallRejectsMissingStackArgument();
    testBranchHandlerState();
    testSwitchAndBlockHandlerState();
    testExceptionHandlerState();
    testFatalRuntimeErrorUnwindsCallFrame();
    testCallBoundaryArityRuntimeError();
    testCallDepthLimitRaisesControlledRuntimeError();
    testRuntimeErrorFormatsStructuredStackTrace();
    testRuntimeDiagnosticInferenceCoversCommonErrors();
    testRuntimeErrorPreservesCallerStateForNextCall();
    testLoopControlHandlerState();
    testOutputHandlerUsesSink();
    testFilesystemPredicatesTreatMissingPathAsFalse();
    testFilesystemPathUtf8Boundary();
    testFoundationStrictIntegerContract();
    testEnvironmentVariableContract();
    testTimeContract();
    testRuntimeModuleInitializationRunsOnce();
    testObjectHandlerState();
    testTracingGcKeepsRootsAndCollectsCycles();
    testTracingGcHeapTeardownBreaksRootedCycles();
    testTracingGcHandlesDeepGraphWithoutNativeRecursion();
    testTracingGcSweepsAllocationBurst();

    if (failures != 0) {
        std::cerr << failures << " VM handler unit test(s) failed\n";
        return 1;
    }
    std::cout << "VM handler unit tests passed\n";
    return 0;
}
