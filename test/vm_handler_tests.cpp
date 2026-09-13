#include <iostream>
#include <string>
#include <vector>

#include "common/vm_native_stdlib_helpers.h"
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
    VM vm(std::vector<Instruction>(5, instruction(OP_DONG_LENH)), {});
    VMRuntimeFixture access(vm);
    access.executeException(instruction(OP_THU, 4, 1, 0));
    expect(access.tryDepth() == 1, "exception handler", "OP_THU must push a try frame");

    access.push(make_string_value("boom"));
    const bool jumped = access.executeException(instruction(OP_NEM));
    expect(jumped, "exception handler", "OP_NEM with a handler must jump to catch");
    expect(access.pc() == 4, "exception handler", "throw must set pc to catch address");
    expect(asString(access.top(), "exception handler") == "boom",
           "exception handler", "thrown value must survive stack unwind");

    access.executeException(instruction(OP_BAT_LOI, 0, 1, 0));
    expect(access.hasVariable(1), "exception handler", "catch must bind the error variable");
    expect(asString(access.variable(1), "exception handler") == "boom",
           "exception handler", "catch variable must contain thrown value");
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
        instruction(static_cast<Opcode>(999)),
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

} // namespace

int main() {
    testValueHandlerState();
    testIndexHandlerState();
    testVariableAndCallFrameHandlerState();
    testCallHandlerState();
    testBranchHandlerState();
    testSwitchAndBlockHandlerState();
    testExceptionHandlerState();
    testLoopControlHandlerState();
    testOutputHandlerUsesSink();
    testFilesystemPredicatesTreatMissingPathAsFalse();
    testRuntimeModuleInitializationRunsOnce();
    testObjectHandlerState();
    testTracingGcKeepsRootsAndCollectsCycles();
    testTracingGcHeapTeardownBreaksRootedCycles();

    if (failures != 0) {
        std::cerr << failures << " VM handler unit test(s) failed\n";
        return 1;
    }
    std::cout << "VM handler unit tests passed\n";
    return 0;
}
