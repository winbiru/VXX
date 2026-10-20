#include "vpp/native/aot_runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "common/vm_native_collection_helpers.h"
#include "common/vm_native_helpers.h"
#include "common/vm_native_m3_helpers.h"
#include "common/vm_native_stdlib_helpers.h"
#include "common/vm_utils.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/bytecode/literal_wire.h"
#include "vpp/core/text.h"
#include "vpp/runtime/literal.h"

namespace vietvm::native::aot {
namespace {
thread_local std::vector<Frame *> nativeFrames;
[[noreturn]] void fail(const std::string &message) {
    throw runtime::RuntimeError("Native AOT: " + message,
                               runtime::RuntimeErrorKind::CallBoundary);
}
Value defaultValue(const std::string &encoded) {
    const auto colon = encoded.find(':');
    if (colon == std::string::npos) fail("invalid default parameter");
    const auto value = encoded.substr(colon + 1);
    switch (encoded[0]) {
        case 'i': return std::stoi(value);
        case 'd': return std::stod(value);
        case 's': return value;
        case 'n': return make_null_value();
        default: fail("invalid default parameter type");
    }
}
bool accessible(runtime::RuntimeMemberVisibility visibility,
                const runtime::ClassHandle &caller, const runtime::ClassHandle &owner) {
    if (visibility == runtime::RuntimeMemberVisibility::Public) return true;
    if (!caller || !owner) return false;
    if (visibility == runtime::RuntimeMemberVisibility::Private) return caller == owner;
    return runtime::isSubclassOf(caller, owner);
}
// Representation primitives used by the V++ compiler itself. Keep the exact
// byte order and validation of the Stage 0 implementation; the generated code
// calls these through the native ABI, without invoking the bytecode VM.
bool representationIntrinsic(Opcode opcode, const std::vector<Value> &args,
                             Value &result, std::string &error) {
    const auto *descriptor = bytecode::intrinsicByOpcode(opcode);
    if (!descriptor) return false;
    const std::string name(descriptor->name);
    if (opcode == OP_VM_TUPLE_FROM_LIST) {
        if (const auto *list = std::get_if<runtime::ListHandle>(&args[0])) {
            result = make_tuple_value(*list ? (*list)->elements : std::vector<Value>{});
        } else error = "bộ: primitive cần danh sách";
        return true;
    }
    if (opcode == OP_VM_STRING_BYTES) {
        const auto *source = std::get_if<std::string>(&args[0]);
        if (!source) { error = name + ": giá trị phải là chuỗi"; return true; }
        std::vector<Value> bytes;
        bytes.reserve(source->size());
        for (unsigned char c : *source) bytes.push_back(make_int_value(c));
        result = make_list_value(std::move(bytes));
        return true;
    }
    if (opcode == OP_VM_STRING_FROM_BYTES || opcode == OP_VM_FLOAT_FROM_BITS) {
        const auto *list = std::get_if<runtime::ListHandle>(&args[0]);
        if (!list || !*list) { error = name + ": primitive cần danh sách"; return true; }
        if (opcode == OP_VM_FLOAT_FROM_BITS && (*list)->elements.size() != 8) {
            error = name + ": cần đúng 8 byte IEEE-754"; return true;
        }
        std::string bytes;
        bytes.reserve((*list)->elements.size());
        for (const auto &value : (*list)->elements) {
            const auto *byte = std::get_if<int>(&value);
            if (!byte || *byte < 0 || *byte > 255) {
                error = name + ": mỗi byte phải là số nguyên trong 0..255";
                return true;
            }
            bytes.push_back(static_cast<char>(*byte));
        }
        if (opcode == OP_VM_STRING_FROM_BYTES) result = make_string_value(std::move(bytes));
        else {
            std::uint64_t bits = 0;
            for (unsigned i = 0; i < 8; ++i)
                bits |= std::uint64_t(static_cast<unsigned char>(bytes[i])) << (i * 8);
            double number;
            static_assert(sizeof(number) == sizeof(bits) && std::numeric_limits<double>::is_iec559);
            std::memcpy(&number, &bits, sizeof(number));
            result = make_float_value(number);
        }
        return true;
    }
    if (opcode == OP_VM_SO_THUC_BITS) {
        double number;
        if (const auto *integer = std::get_if<int>(&args[0])) number = *integer;
        else if (const auto *real = std::get_if<double>(&args[0])) number = *real;
        else { error = name + ": giá trị phải là số"; return true; }
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(number) && std::numeric_limits<double>::is_iec559);
        std::memcpy(&bits, &number, sizeof(bits));
        std::vector<Value> bytes;
        bytes.reserve(8);
        for (unsigned shift = 0; shift < 64; shift += 8)
            bytes.push_back(make_int_value(static_cast<int>((bits >> shift) & 0xffu)));
        result = make_list_value(std::move(bytes));
        return true;
    }
    return false;
}
} // namespace

Program::Program() {
    capabilities = {"system.env.read", "system.process.id",
                               "system.process.parent_id", "system.time.sleep",
                               "system.process.new", "system.process.arg",
                               "system.process.command_line",
                               "system.process.env", "system.process.start",
                               "system.process.poll", "system.process.read",
                               "system.process.wait", "system.process.error",
                               "system.process.close",
                               "system.time.monotonic",
                               "system.time.realtime",
                               "system.time.local",
                               "system.entropy.read",
                               "system.fs.mkdir", "system.fs.remove",
                               "system.fs.exists",
                               "system.fs.stat", "system.fs.lstat",
                               "system.fs.path.kind",
                               "system.fs.dir.open", "system.fs.dir.read",
                               "system.fs.dir.close",
                               "system.net.resolve.open", "system.net.resolve.next",
                               "system.net.resolve.close",
                               "system.net.socket.connect", "system.net.socket.listen",
                               "system.net.socket.accept", "system.net.socket.timeout",
                               "system.net.socket.kind", "system.net.socket.send",
                               "system.net.socket.recv", "system.net.socket.close",
                               "system.file.open", "system.file.read",
                               "system.file.write", "system.file.close",
                               "system.file.error",
                               "system.file.seek", "system.file.tell", "system.file.flush",
                               "system.ffi.error",
                               "system.ffi.layout",
                               "system.user.id", "system.user.effective_id",
                               "system.group.id", "system.group.effective_id"};
    output = [](const std::string &text) { std::cout << text; };
}
const std::string &Program::string(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= strings.size()) fail("StringPool index");
    return strings[static_cast<std::size_t>(index)];
}
Frame::Frame(Program &p, bool root) : program(p), global(root) {
    program.frames.push_back(this);
    nativeId = static_cast<int>(nativeFrames.size());
    nativeFrames.push_back(this);
}
Frame::~Frame() { nativeFrames.pop_back(); program.frames.pop_back(); }
Value Frame::pop() {
    if (stack.empty()) fail("operand stack underflow");
    auto value = std::move(stack.back()); stack.pop_back(); return value;
}
std::vector<Value> Frame::take(int count) {
    if (count < 0 || static_cast<std::size_t>(count) > stack.size()) fail("argument stack underflow");
    std::vector<Value> values(static_cast<std::size_t>(count));
    for (int i = count; i > 0; --i) values[static_cast<std::size_t>(i - 1)] = pop();
    return values;
}
runtime::CellHandle Frame::cell(int slot, bool create) {
    auto found = locals.find(slot);
    if (found != locals.end()) return found->second;
    auto shared = program.globals.find(slot);
    if (shared != program.globals.end()) return shared->second;
    if (!create) return {};
    auto result = std::make_shared<runtime::RuntimeCell>(); result->value = 0;
    (global ? program.globals : locals)[slot] = result;
    return result;
}
Value Frame::load(int slot) {
    auto existing = cell(slot, false);
    if (existing) return existing->value;
    auto result = std::make_shared<runtime::RuntimeCell>(); result->value = 0;
    program.globals[slot] = result;
    return result->value;
}
void Frame::store(int slot, Value value) { cell(slot)->value = std::move(value); }
void Frame::initialize(int slot) {
    auto &scope = global ? program.globals : locals;
    if (!global || !scope.count(slot)) {
        auto value = std::make_shared<runtime::RuntimeCell>(); value->value = 0;
        scope[slot] = std::move(value);
    }
}
void Frame::assign() {
    auto id = pop(); auto value = pop();
    if (std::holds_alternative<std::string>(id) && std::holds_alternative<int>(value)) std::swap(id, value);
    if (!std::holds_alternative<int>(id)) fail("variable ID must be an integer");
    store(std::get<int>(id), std::move(value));
}
void Frame::parameter(int slot, int index, int fallback) {
    Value value;
    if (index == -1) {
        if (!receiver) fail("method receiver missing");
        value = make_instance_value(receiver);
    } else if (index >= 0 && static_cast<std::size_t>(index) < arguments.size()) value = arguments[index];
    else if (fallback >= 0) value = defaultValue(program.string(fallback));
    else fail("required parameter missing");
    auto local = std::make_shared<runtime::RuntimeCell>(); local->value = std::move(value);
    locals[slot] = std::move(local);
}
void Frame::increment(int delta) {
    const auto id = pop();
    if (!std::holds_alternative<int>(id)) fail("increment requires a variable ID");
    const int slot = std::get<int>(id);
    const auto before = load(slot);
    auto after = evaluateBinaryOperator(OP_CONG, before, Value(delta), 0);
    store(slot, after); push(std::move(after));
}
void Frame::binary(int operation) {
    auto right = pop(); auto left = pop();
    push(operation == OP_MODULO ? evaluateModuloOperator(left, right, operation, 0)
                                : evaluateBinaryOperator(operation, left, right, 0));
}
void Frame::index() {
    auto key = pop(); auto container = pop();
    if (auto map = std::get_if<runtime::MapHandle>(&container)) {
        if (!*map) { push(make_null_value()); return; }
        const auto found = (*map)->entries.find(helpers::argToRawString(key));
        push(found == (*map)->entries.end() ? make_null_value() : found->second); return;
    }
    if (!std::holds_alternative<int>(key) || std::get<int>(key) < 0) fail("index out of range");
    const auto offset = static_cast<std::size_t>(std::get<int>(key));
    if (auto list = std::get_if<runtime::ListHandle>(&container)) {
        if (!*list || offset >= (*list)->elements.size()) fail("index out of range");
        push((*list)->elements[offset]); return;
    }
    if (auto tuple = std::get_if<runtime::TupleHandle>(&container)) {
        if (!*tuple || offset >= (*tuple)->elements.size()) fail("index out of range");
        push((*tuple)->elements[offset]); return;
    }
    if (auto string = std::get_if<std::string>(&container)) {
        auto unit = core::utf8CodePointAt(*string, offset);
        if (!unit) fail("index out of range");
        push(*unit); return;
    }
    fail("value cannot be indexed");
}
void Frame::setIndex() {
    auto value = pop(); auto key = pop(); auto container = pop();
    if (auto map = std::get_if<runtime::MapHandle>(&container)) {
        if (!*map) fail("null map");
        (*map)->entries[helpers::argToRawString(key)] = std::move(value); return;
    }
    if (!std::holds_alternative<int>(key) || std::get<int>(key) < 0) fail("index out of range");
    auto list = std::get_if<runtime::ListHandle>(&container);
    const auto offset = static_cast<std::size_t>(std::get<int>(key));
    if (!list || !*list || offset >= (*list)->elements.size()) fail("index out of range");
    (*list)->elements[offset] = std::move(value);
}
void Frame::list(int count, int encoded) {
    if (encoded >= 0) push(make_list_value(runtime::decodeListFromStringPool(program.string(encoded))));
    else push(make_list_value(take(count)));
}
void Frame::map(int count, int encoded) {
    if (encoded >= 0) { push(make_map_value(runtime::decodeMapFromStringPool(program.string(encoded)))); return; }
    auto items = take(count * 2); runtime::MapValue result;
    for (int i = 0; i < count; ++i) {
        if (!std::holds_alternative<std::string>(items[2*i])) fail("map key must be a string");
        result.entries[std::get<std::string>(items[2*i])] = std::move(items[2*i+1]);
    }
    push(make_map_value(std::move(result)));
}
void Frame::closure(int function, int count) {
    auto captures = take(count); auto result = std::make_shared<runtime::RuntimeClosure>();
    result->functionId = function; result->methodOwnerClass = owner;
    for (const auto &slot : captures) result->captures[std::get<int>(slot)] = cell(std::get<int>(slot));
    push(make_closure_value(std::move(result)));
}
Value Program::invoke(int id, std::vector<Value> args, runtime::InstanceHandle receiver,
                      runtime::ClassHandle owner, runtime::ClosureHandle closure) {
    if (id < 0 || static_cast<std::size_t>(id) >= functions.size() || !functions[id].function) fail("function not found");
    const auto &function = functions[id];
    if (args.size() < static_cast<std::size_t>(function.required) || args.size() > static_cast<std::size_t>(function.maximum))
        fail("function arity mismatch: " +
             (function.nameIndex >= 0 ? string(function.nameIndex) : "anonymous #" + std::to_string(id)));
    Frame frame(*this); frame.arguments = std::move(args);
    frame.receiver = std::move(receiver); frame.owner = std::move(owner);
    if (closure) { frame.locals = closure->captures; frame.owner = closure->methodOwnerClass; }
    runtime::RuntimeHeapScope scope(heap);
    function.function(frame.nativeId);
    if (pendingError) { auto error = std::exchange(pendingError, {}); std::rethrow_exception(error); }
    return frame.result;
}
Value Program::invokeName(const std::string &name, std::vector<Value> args) {
    for (std::size_t i = 0; i < functions.size(); ++i)
        if (functions[i].function && functions[i].nameIndex >= 0 &&
            string(functions[i].nameIndex) == name)
            return invoke(static_cast<int>(i), std::move(args));
    fail("function not found: " + name);
}
void Frame::call(int target, int count, int nameIndex, bool indirect) {
    Value callee; if (indirect) callee = pop();
    auto args = take(count);
    if (indirect) {
        auto value = std::move(callee);
        if (auto closure = std::get_if<runtime::ClosureHandle>(&value)) {
            if (!*closure) fail("null closure");
            push(program.invoke((*closure)->functionId, std::move(args), {}, {}, *closure)); return;
        }
        if (auto text = std::get_if<std::string>(&value)) { push(program.invokeName(*text, std::move(args))); return; }
        if (!std::holds_alternative<int>(value)) fail("value is not callable");
        target = std::get<int>(value);
    }
    if (target >= 0 && static_cast<std::size_t>(target) < program.functions.size() && program.functions[target].function) {
        push(program.invoke(target, std::move(args))); return;
    }
    const auto *primitive = bytecode::intrinsicByName(program.string(nameIndex));
    if (!primitive) fail("call target not found");
    for (auto &value : args) push(std::move(value));
    intrinsic(primitive->opcode);
}
void Frame::defineClass(int name, int superclass) {
    const auto &text = program.string(name);
    if (program.classes.count(text)) return;
    runtime::ClassHandle base;
    if (superclass > 0) {
        auto found = program.classes.find(program.string(superclass - 1));
        if (found == program.classes.end()) fail("superclass not found");
        base = found->second;
    }
    program.classes[text] = runtime::createClass(text, base);
}
void Frame::defineMethod(int klass, int name, int function) {
    auto visibility = runtime::RuntimeMemberVisibility::Public;
    if (function < 0) { function = -(function + 1); visibility = runtime::RuntimeMemberVisibility::Private; }
    else if (klass < 0) { klass = -(klass + 1); visibility = runtime::RuntimeMemberVisibility::Protected; }
    auto found = program.classes.find(program.string(klass));
    if (found == program.classes.end() || !runtime::defineMethod(found->second, program.string(name), function, visibility)) fail("method registration failed");
}
void Frame::construct(int name, int count) {
    auto args = take(count); auto found = program.classes.find(program.string(name));
    if (found == program.classes.end()) fail("class not found");
    auto instance = runtime::createInstance(found->second);
    const auto constructor = found->second->methods.find("khởi tạo");
    if (constructor != found->second->methods.end()) {
        if (!accessible(constructor->second.visibility, owner, found->second)) fail("constructor access denied");
        program.invoke(constructor->second.functionId, std::move(args), instance, found->second);
    } else if (!args.empty()) fail("constructor arity mismatch");
    push(make_instance_value(instance));
}
void Frame::property(int name, bool write) {
    Value value; if (write) value = pop();
    auto object = pop(); auto instance = std::get_if<runtime::InstanceHandle>(&object);
    if (!instance || !*instance) fail("expected instance");
    if (write) { runtime::setInstanceField(*instance, program.string(name), std::move(value)); return; }
    auto field = runtime::getInstanceField(*instance, program.string(name));
    if (!field) fail("property not found: " + program.string(name));
    push(*field);
}
void Frame::method(int name, int count, bool super) {
    auto args = take(count); auto object = pop(); auto instance = std::get_if<runtime::InstanceHandle>(&object);
    if (!instance || !*instance) fail("expected instance");
    auto method = runtime::resolveMethod(super ? (owner ? owner->superclass : nullptr) : (*instance)->klass, program.string(name));
    if (!method) fail("method not found: " + program.string(name));
    bool allowed = accessible(method->visibility, owner, method->owner);
    if (!allowed && owner && method->visibility == runtime::RuntimeMemberVisibility::Protected &&
        runtime::isSubclassOf((*instance)->klass, owner) && runtime::isSubclassOf(method->owner, owner)) {
        auto declaration = runtime::resolveMethod(owner, program.string(name));
        allowed = declaration && accessible(declaration->visibility, owner, declaration->owner);
    }
    if (!allowed) fail("method access denied: " + program.string(name));
    push(program.invoke(method->functionId, std::move(args), *instance, method->owner));
}
void Frame::intrinsic(int operation) {
    const auto *descriptor = bytecode::intrinsicByOpcode(operation);
    if (!descriptor) fail("unknown intrinsic");
    auto args = take(static_cast<int>(descriptor->arity)); Value result = make_null_value(); std::string error;
    if (operation == OP_VM_BYTECODE_CHAY || operation == OP_NATIVE_ARTIFACT_CHAY) {
        if (!program.executeArtifact) fail("native artifact compiler unavailable");
        push(program.executeArtifact(args)); return;
    }
    if (operation == OP_VM_KICH_BAN_CHAY) {
        if (!program.executeSource) fail("native embedded compiler unavailable");
        push(program.executeSource(args)); return;
    }
    if (!representationIntrinsic(static_cast<Opcode>(operation), args, result, error) &&
        !helpers::handleNativeCollectionFunction(static_cast<Opcode>(operation), args, result, error) &&
        !helpers::handleNativeFoundationFunction(static_cast<Opcode>(operation), args, result, error) &&
        !helpers::handleNativeM3LibraryFunction(static_cast<Opcode>(operation), args, result, error)) fail("unsupported native intrinsic: " + std::string(descriptor->name));
    if (!error.empty()) throw runtime::LanguageException(make_string_value(error));
    push(std::move(result));
}
void Frame::foreign(int descriptor, int count) {
    if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= program.foreignFunctions.size()) fail("foreign descriptor index");
    auto result = runtime::executeForeignCall(program.foreignFunctions[descriptor], take(count),
        program.capabilities, program.lastForeignError, &program.files);
    program.lastForeignError = result.posixError; push(std::move(result.value));
}
bool Frame::caseMatches(int operand, int index) {
    if (switches.empty()) fail("case outside switch");
    auto &state = switches.back(); bool matches = false;
    if (index >= 0) matches = helpers::argToRawString(state.first) == program.string(index);
    else {
        const auto label = index == -2 ? load(operand) : Value(operand);
        try { matches = as_int(state.first) == as_int(label); } catch (...) { matches = false; }
    }
    if (state.second || !matches) return false;
    state.second = true; return true;
}
bool Frame::defaultMatches() {
    if (switches.empty()) fail("default outside switch");
    if (switches.back().second) return false;
    switches.back().second = true; return true;
}
void Frame::print() { program.output(sv_to_string(pop()) + "\n"); }
void Frame::safepoint() { if (program.heap.needsCollection()) program.collect(); }
void Program::collect() {
    std::vector<Value> roots;
    for (const auto &[id, value] : globals) if (value) roots.push_back(value->value);
    for (const auto &[name, klass] : classes) roots.emplace_back(klass);
    for (const auto *frame : frames) {
        roots.insert(roots.end(), frame->stack.begin(), frame->stack.end());
        roots.insert(roots.end(), frame->arguments.begin(), frame->arguments.end());
        for (const auto &[id, cell] : frame->locals) if (cell) roots.push_back(cell->value);
        if (frame->receiver) roots.emplace_back(frame->receiver);
        if (frame->owner) roots.emplace_back(frame->owner);
        for (const auto &item : frame->switches) roots.push_back(item.first);
    }
    heap.collect(roots);
}
void Program::initialize() {
    runtime::RuntimeHeapScope scope(heap);
    for (auto init : initializers) {
        Frame frame(*this, true); init(frame.nativeId);
        if (pendingError) { auto error = std::exchange(pendingError, {}); std::rethrow_exception(error); }
    }
}
Value Program::run() {
    runtime::RuntimeHeapScope scope(heap); initialize();
    if (!entry) fail("entry point missing");
    Frame frame(*this, true); entry(frame.nativeId);
    if (pendingError) { auto error = std::exchange(pendingError, {}); std::rethrow_exception(error); }
    return frame.result;
}

namespace {
template<class Operation> int boundary(int id, Operation operation) noexcept {
    auto &frame = *nativeFrames[static_cast<std::size_t>(id)];
    if (frame.program.pendingError) return 0;
    try { return operation(frame); }
    catch (...) { frame.program.pendingError = std::current_exception(); return 0; }
}
}

// C ABI boundaries always contain C++ exceptions. Generated machine frames
// route pending language exceptions to native catch labels explicitly.
#define AOT0(name, body) extern "C" int vpp_aot_##name(int id) noexcept { return boundary(id, [](Frame &f) { body; return 0; }); }
#define AOT1(name, body) extern "C" int vpp_aot_##name(int id, int a) noexcept { return boundary(id, [=](Frame &f) { body; return 0; }); }
#define AOT2(name, body) extern "C" int vpp_aot_##name(int id, int a, int b) noexcept { return boundary(id, [=](Frame &f) { body; return 0; }); }
#define AOT3(name, body) extern "C" int vpp_aot_##name(int id, int a, int b, int c) noexcept { return boundary(id, [=](Frame &f) { body; return 0; }); }
AOT1(integer, f.push(a))
AOT1(string, f.push(f.program.string(a)))
AOT1(floating, f.push(std::stod(f.program.string(a))))
AOT0(null, f.push(make_null_value()))
AOT1(load, f.push(f.load(a)))
AOT1(initialize, f.initialize(a))
AOT0(assign, f.assign())
AOT3(parameter, f.parameter(a,b,c))
AOT1(increment, f.increment(a))
AOT1(binary, f.binary(a))
AOT0(negate, f.push(int(!runtime::stackValueTruthy(f.pop()))))
AOT2(list, f.list(a,b))
AOT2(map, f.map(a,b))
AOT0(index, f.index())
AOT0(set_index, f.setIndex())
AOT2(call, f.call(a,b,a<0?-a-1:a))
AOT1(indirect, f.call(0,a,0,true))
AOT2(closure, f.closure(a,b))
AOT2(define_class, f.defineClass(a,b))
AOT3(define_method, f.defineMethod(a,b,c))
AOT2(construct, f.construct(a,b))
AOT2(property, f.property(a,b!=0))
AOT3(method, f.method(a,b,c!=0))
AOT0(print, f.print())
AOT0(return_value, f.result=f.stack.empty()?make_null_value():f.pop())
AOT1(intrinsic, f.intrinsic(a))
AOT2(foreign, f.foreign(a,b))
AOT0(switch_begin, f.switches.emplace_back(f.pop(),false))
AOT0(switch_end, if(f.switches.empty()) fail("switch stack underflow"); f.switches.pop_back())
AOT0(throw_value, throw runtime::LanguageException(f.pop()))
AOT2(try_begin, f.handlers.push_back({a,f.stack.size(),f.switches.size()}))
AOT0(try_end, if(f.handlers.empty()) fail("exception stack underflow"); f.handlers.pop_back())
AOT1(catch_value, {auto value=f.pop(); if(a>=0)f.store(a,std::move(value));})
AOT0(safepoint, f.safepoint())
#undef AOT0
#undef AOT1
#undef AOT2
#undef AOT3
extern "C" int vpp_aot_condition(int id) noexcept {
    return boundary(id, [](Frame &f) { return int(runtime::stackValueTruthy(f.pop())); });
}
extern "C" int vpp_aot_case(int id,int a,int b) noexcept {
    return boundary(id,[=](Frame &f) {return int(f.caseMatches(a,b));});
}
extern "C" int vpp_aot_default(int id) noexcept {
    return boundary(id,[](Frame &f) {return int(f.defaultMatches());});
}
extern "C" int vpp_aot_failed(int id) noexcept {
    return nativeFrames[static_cast<std::size_t>(id)]->program.pendingError ? 1 : 0;
}
extern "C" int vpp_aot_handle_error(int id) noexcept {
    auto &f=*nativeFrames[static_cast<std::size_t>(id)];
    if (!f.program.pendingError || f.handlers.empty()) return -1;
    try { std::rethrow_exception(f.program.pendingError); }
    catch(const runtime::LanguageException &error) {
        const auto handler=f.handlers.back(); f.handlers.pop_back();
        f.stack.resize(handler.stack); f.switches.resize(handler.switches);
        f.push(error.value()); f.program.pendingError={}; return handler.target;
    } catch(...) { return -1; }
}
} // namespace vietvm::native::aot
