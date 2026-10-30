#include "vpp/native/aot_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <map>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

#include "common/vm_native_collection_helpers.h"
#include "common/vm_native_helpers.h"
#include "common/vm_native_m3_helpers.h"
#include "common/vm_native_stdlib_helpers.h"
#include "common/vm_utils.h"
#include "vpp/bytecode/intrinsic.h"
#include "vpp/bytecode/literal_wire.h"
#include "vpp/core/text.h"
#include "vpp/runtime/literal.h"
#include "vpp/runtime/process_identity.h"
#include "vpp/runtime/time_sleep.h"
#include "vpp/runtime/time_clock.h"
#include "vpp/runtime/entropy_machine.h"
#include "vpp/runtime/environment_machine.h"
#include "vpp/runtime/time_local.h"
#include "vpp/runtime/directory_machine.h"
#include "vpp/runtime/path_machine.h"

namespace vietvm::native::aot {
namespace {
std::uint64_t cpuNanoseconds() {
#if defined(_WIN32)
    FILETIME created{}, ended{}, kernel{}, user{};
    if(GetThreadTimes(GetCurrentThread(),&created,&ended,&kernel,&user)) {
        ULARGE_INTEGER k{},u{};
        k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
        return (k.QuadPart+u.QuadPart)*100;
    }
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec current{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&current)==0)
        return std::uint64_t(current.tv_sec)*1000000000ull+current.tv_nsec;
#endif
    return static_cast<std::uint64_t>(double(std::clock())*1000000000.0/CLOCKS_PER_SEC);
}
struct CpuProfiler {
    bool enabled=[] {
        const char *value=std::getenv("VPP_AOT_CPU_PROFILE");
        return value && std::strcmp(value,"1")==0;
    }();
    struct Entry { std::uint64_t calls=0, inclusive=0, exclusive=0; };
    struct Active {const char *name;std::uint64_t start, children=0;};
    std::map<std::string,Entry> entries;
    std::vector<Active> active;
};
thread_local CpuProfiler cpuProfiler;
struct CpuScope {
    bool enabled=cpuProfiler.enabled;
    explicit CpuScope(const char *name) {
        if(enabled)cpuProfiler.active.push_back({name,cpuNanoseconds(),0});
    }
    ~CpuScope() {
        if(!enabled)return;
        const auto end=cpuNanoseconds();
        const auto current=cpuProfiler.active.back();cpuProfiler.active.pop_back();
        const auto total=end>=current.start?end-current.start:0;
        auto &entry=cpuProfiler.entries[current.name];
        ++entry.calls;entry.inclusive+=total;
        entry.exclusive+=total>=current.children?total-current.children:0;
        if(!cpuProfiler.active.empty())cpuProfiler.active.back().children+=total;
    }
};
thread_local std::vector<Frame *> nativeFrames;
// Local bindings are heap-backed because closures can retain them after a
// call. Reuse only cells owned exclusively by the finished frame; captured
// cells stay alive with their original identity. Keep the cache thread-local
// and bounded so recursive calls and independent Programs cannot share live
// bindings or retain arbitrarily many allocations.
struct LocalFrameCache {
    static constexpr std::size_t maxMaps = 32;
    static constexpr std::size_t maxCells = 256;
    static constexpr std::size_t maxStacks = 16;
    static constexpr std::size_t maxStackCapacity = 512;
    std::array<std::unordered_map<int, runtime::CellHandle>, maxMaps> maps;
    std::array<runtime::CellHandle, maxCells> cells;
    std::array<std::vector<Value>, maxStacks> stacks;
    std::size_t mapCount = 0, cellCount = 0, stackCount = 0;
    std::uint64_t reusedMaps = 0, reusedCells = 0, newCells = 0, reusedStacks = 0;
};
thread_local LocalFrameCache localFrameCache;

runtime::CellHandle createLocalCell() {
    auto &cache = localFrameCache;
    if (cache.cellCount) {
        ++cache.reusedCells;
        return std::move(cache.cells[--cache.cellCount]);
    }
    ++cache.newCells;
    return std::make_shared<runtime::RuntimeCell>();
}

runtime::CellHandle createFrameCell(bool global) {
    return global ? std::make_shared<runtime::RuntimeCell>() : createLocalCell();
}
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
                               "system.net.tls.upgrade", "system.net.tls.send",
                               "system.net.tls.recv",
                               "system.net.socket.connect", "system.net.socket.listen",
                               "system.net.socket.accept", "system.net.socket.timeout",
                               "system.net.socket.kind", "system.net.socket.send",
                               "system.net.socket.recv", "system.net.socket.close",
                               "system.file.open", "system.file.read",
                               "system.file.write", "system.file.close",
                               "system.file.error",
                               "system.file.seek", "system.file.tell", "system.file.flush",
                               "machine.file.open", "machine.file.read",
                               "machine.file.write", "machine.file.close",
                               "machine.file.seek", "machine.file.tell",
                               "machine.file.error", "machine.file.remove",
                               "machine.dir.mkdir", "machine.dir.remove",
                               "machine.dir.open", "machine.dir.next", "machine.dir.close",
                               "machine.path.kind",
                               "machine.net.socket.kind", "machine.net.socket.close",
                               "machine.net.socket.timeout",
                               "machine.net.socket.send", "machine.net.socket.recv",
                               "machine.net.socket.accept",
                               "machine.net.socket.listen",
                               "machine.net.socket.connect",
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
    CpuScope profile("frame.register");
    if (!global && localFrameCache.stackCount) {
        stack.swap(localFrameCache.stacks[--localFrameCache.stackCount]);
        ++localFrameCache.reusedStacks;
    }
    if (!global && localFrameCache.mapCount) {
        locals.swap(localFrameCache.maps[--localFrameCache.mapCount]);
        ++localFrameCache.reusedMaps;
    }
    program.frames.push_back(this);
    nativeId = static_cast<int>(nativeFrames.size());
    nativeFrames.push_back(this);
}
Frame::~Frame() {
    CpuScope profile("frame.unregister");
    nativeFrames.pop_back(); program.frames.pop_back();
    if (!global) {
        // The operand stack belongs to this finished activation. Clearing it
        // releases any object/closure handles before the scalar buffers are
        // recycled. Only small capacities are kept across invocations.
        stack.clear();
        if (stack.capacity() <= LocalFrameCache::maxStackCapacity &&
            localFrameCache.stackCount < LocalFrameCache::maxStacks)
            stack.swap(localFrameCache.stacks[localFrameCache.stackCount++]);
        if (locals.size() <= LocalFrameCache::maxCells) {
            for (auto &[slot, cell] : locals) {
                if (cell && cell.use_count() == 1 &&
                    localFrameCache.cellCount < LocalFrameCache::maxCells) {
                    // Drop object references before caching, including values
                    // registered with this Program's tracing heap.
                    cell->value = Value(0);
                    localFrameCache.cells[localFrameCache.cellCount++] = std::move(cell);
                }
            }
        }
        locals.clear();
        if (locals.bucket_count() <= 512 &&
            localFrameCache.mapCount < LocalFrameCache::maxMaps)
            locals.swap(localFrameCache.maps[localFrameCache.mapCount++]);
    }
}
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
    auto result = createFrameCell(global); result->value = 0;
    (global ? program.globals : locals)[slot] = result;
    return result;
}
void Frame::invalidateBinding(int slot) {
    auto &entry = bindingCache[static_cast<std::uint32_t>(slot) & (bindingCache.size() - 1)];
    if (entry.cell && entry.slot == slot) entry.cell = nullptr;
}
void Frame::rememberBinding(int slot, runtime::RuntimeCell *cell) {
    bindingCache[static_cast<std::uint32_t>(slot) & (bindingCache.size() - 1)] = {slot, cell};
}
Value &Frame::valueAt(int slot) {
    const auto &entry = bindingCache[static_cast<std::uint32_t>(slot) & (bindingCache.size() - 1)];
    if (entry.cell && entry.slot == slot) return entry.cell->value;
    if (auto it = locals.find(slot); it != locals.end()) {
        rememberBinding(slot, it->second.get());
        return it->second->value;
    }
    if (auto it = program.globals.find(slot); it != program.globals.end()) {
        rememberBinding(slot, it->second.get());
        return it->second->value;
    }
    auto result = createFrameCell(global); result->value = 0;
    auto *ptr = result.get();
    (global ? program.globals : locals)[slot] = result;
    rememberBinding(slot, ptr);
    return ptr->value;
}
Value Frame::load(int slot) {
    CpuScope profile("cell.load");
    return valueAt(slot);
}
void Frame::store(int slot, Value value) {
    CpuScope profile("cell.store");
    const auto &entry = bindingCache[static_cast<std::uint32_t>(slot) & (bindingCache.size() - 1)];
    if (entry.cell && entry.slot == slot) {
        entry.cell->value = std::move(value);
        return;
    }
    if (auto it = locals.find(slot); it != locals.end()) {
        it->second->value = std::move(value);
        rememberBinding(slot, it->second.get());
        return;
    }
    if (auto it = program.globals.find(slot); it != program.globals.end()) {
        it->second->value = std::move(value);
        rememberBinding(slot, it->second.get());
        return;
    }
    auto cell = createFrameCell(global);
    cell->value = std::move(value);
    auto *ptr = cell.get();
    (global ? program.globals : locals)[slot] = std::move(cell);
    rememberBinding(slot, ptr);
}
void Frame::initialize(int slot) {
    auto &scope = global ? program.globals : locals;
    if (!global || !scope.count(slot)) {
        auto value = createFrameCell(global); value->value = 0;
        invalidateBinding(slot);
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
    CpuScope profile("frame.parameter");
    Value value;
    if (index == -1) {
        if (!receiver) fail("method receiver missing");
        value = make_instance_value(receiver);
    } else if (index >= 0 && static_cast<std::size_t>(index) < arguments.size()) value = arguments[index];
    else if (fallback >= 0) value = defaultValue(program.string(fallback));
    else fail("required parameter missing");
    auto local = createLocalCell(); local->value = std::move(value);
    invalidateBinding(slot);
    locals[slot] = std::move(local);
}
void Frame::increment(int delta) {
    const auto id = pop();
    if (!std::holds_alternative<int>(id)) fail("increment requires a variable ID");
    incrementSlot(std::get<int>(id), delta);
}
void Frame::incrementSlot(int slot, int delta) {
    CpuScope profile("integer.slot_increment");
    // The VM converts variable ++/-- through as_int (including truncating
    // doubles) and stores an i32. Regular binary addition differs here.
    Value &binding = valueAt(slot);
    if (auto *integer = std::get_if<int>(&binding)) {
        *integer = wrapRuntimeInt32(std::int64_t{*integer} + delta);
        push(*integer);
        return;
    }
    const int current = as_int(binding, delta > 0 ? OP_CONG_MOT : OP_TRU_MOT, -1);
    const int after = wrapRuntimeInt32(std::int64_t{current} + delta);
    binding = after;
    push(after);
}
void Frame::binary(int operation) {
    CpuScope profile("binary.type_and_value");
    auto right = pop(); auto left = pop();
    // The common i32 path does not need dynamic numeric coercion, string
    // conversion or ABI-integer checks. Preserve the VM's wrapping semantics.
    if (const auto *a = std::get_if<int>(&left)) {
        if (const auto *b = std::get_if<int>(&right)) {
            switch (operation) {
                case OP_CONG:
                    push(wrapRuntimeInt32(std::int64_t{*a} + *b)); return;
                case OP_TRU:
                    push(wrapRuntimeInt32(std::int64_t{*a} - *b)); return;
                case OP_NHAN:
                    push(wrapRuntimeInt32(std::int64_t{*a} * *b)); return;
                case OP_SO_SANH_BANG: push(make_int_value(*a == *b)); return;
                case OP_KHAC_BANG: push(make_int_value(*a != *b)); return;
                case OP_LON_HON: push(make_int_value(*a > *b)); return;
                case OP_NHO_HON: push(make_int_value(*a < *b)); return;
                case OP_LON_HON_HOAC_BANG: push(make_int_value(*a >= *b)); return;
                case OP_NHO_HON_HOAC_BANG: push(make_int_value(*a <= *b)); return;
                case OP_Logic_VA: push(make_int_value(*a != 0 && *b != 0)); return;
                case OP_Logic_HOAC: push(make_int_value(*a != 0 || *b != 0)); return;
                default: break;
            }
        }
    }
    // Mixed i32/f64 and pure f64 arithmetic share the VM's numeric promotion
    // rule. Evaluate primitive scalars directly and keep the dynamic helper
    // for division (which supplies diagnostic context), ABI integers, strings
    // and other objects. NaN and signed-zero comparisons use CPU semantics.
    const double *leftReal = std::get_if<double>(&left);
    const double *rightReal = std::get_if<double>(&right);
    if (leftReal || rightReal) {
        const int *leftInt = std::get_if<int>(&left);
        const int *rightInt = std::get_if<int>(&right);
        if ((leftReal || leftInt) && (rightReal || rightInt)) {
            const double a = leftReal ? *leftReal : static_cast<double>(*leftInt);
            const double b = rightReal ? *rightReal : static_cast<double>(*rightInt);
            switch (operation) {
                case OP_CONG: push(a + b); return;
                case OP_TRU: push(a - b); return;
                case OP_NHAN: push(a * b); return;
                case OP_SO_SANH_BANG: push(make_int_value(a == b)); return;
                case OP_KHAC_BANG: push(make_int_value(a != b)); return;
                case OP_LON_HON: push(make_int_value(a > b)); return;
                case OP_NHO_HON: push(make_int_value(a < b)); return;
                case OP_LON_HON_HOAC_BANG: push(make_int_value(a >= b)); return;
                case OP_NHO_HON_HOAC_BANG: push(make_int_value(a <= b)); return;
                case OP_Logic_VA: push(make_int_value(a != 0.0 && b != 0.0)); return;
                case OP_Logic_HOAC: push(make_int_value(a != 0.0 || b != 0.0)); return;
                default: break;
            }
        }
    }
    push(operation == OP_MODULO ? evaluateModuloOperator(left, right, operation, 0)
                                : evaluateBinaryOperator(operation, left, right, 0));
}
void Frame::updateIntegerSlot(int source, int immediate, int operation, int target) {
    CpuScope profile("integer.slot_update");
    // Loop-carried scalar: a same-slot update can work against the cell's
    // in-place variant. This avoids two hash lookups, a Value copy and a
    // second ABI boundary, while retaining captured-cell aliasing.
    if(source==target) {
        runtime::RuntimeCell *cell=nullptr;
        if(const auto it=locals.find(source);it!=locals.end())cell=it->second.get();
        else if(const auto it=program.globals.find(source);it!=program.globals.end())
            cell=it->second.get();
        if(cell) {
            if(auto *integer=std::get_if<int>(&cell->value)) {
                const std::int64_t a=*integer,b=immediate;
                switch(operation) {
                    case OP_CONG: *integer=wrapRuntimeInt32(a+b);return;
                    case OP_TRU: *integer=wrapRuntimeInt32(a-b);return;
                    case OP_NHAN: *integer=wrapRuntimeInt32(a*b);return;
                    default: break;
                }
            }
            if(auto *real=std::get_if<double>(&cell->value)) {
                switch(operation) {
                    case OP_CONG: *real+=immediate;return;
                    case OP_TRU: *real-=immediate;return;
                    case OP_NHAN: *real*=immediate;return;
                    default: break;
                }
            }
        }
    }
    const Value &before = valueAt(source);
    Value after;
    if (const auto *integer = std::get_if<int>(&before)) {
        const std::int64_t a = *integer, b = immediate;
        switch (operation) {
            case OP_CONG: after = wrapRuntimeInt32(a + b); break;
            case OP_TRU: after = wrapRuntimeInt32(a - b); break;
            case OP_NHAN: after = wrapRuntimeInt32(a * b); break;
            default: fail("unsupported fused integer operation");
        }
    } else {
        CpuScope fallback("dynamic.integer_slot_update");
        // Variable bindings are dynamically typed. The fast path may be
        // reached after a re-assignment to float/string; preserve semantics.
        after = evaluateBinaryOperator(operation, before, Value(immediate), 0);
    }
    store(target, std::move(after));
}
void Frame::updateNumericSlots(int leftSlot, int rightSlot, int operation, int targetSlot) {
    CpuScope profile("numeric.slots_update");
    // Preserve source evaluation order and cell aliasing (including closures).
    // Plain scalars live inside StackValue; they require no heap allocation.
    const Value &left=valueAt(leftSlot), &right=valueAt(rightSlot);
    Value result;
    bool specialized=false;
    if(const auto *a=std::get_if<int>(&left)) {
        if(const auto *b=std::get_if<int>(&right)) {
            const std::int64_t x=*a,y=*b;
            switch(operation) {
                case OP_CONG:result=wrapRuntimeInt32(x+y);specialized=true;break;
                case OP_TRU:result=wrapRuntimeInt32(x-y);specialized=true;break;
                case OP_NHAN:result=wrapRuntimeInt32(x*y);specialized=true;break;
                default:break;
            }
        }
    }
    if(!specialized) {
        const auto *leftReal=std::get_if<double>(&left);
        const auto *rightReal=std::get_if<double>(&right);
        if(leftReal || rightReal) {
            const auto *leftInt=std::get_if<int>(&left);
            const auto *rightInt=std::get_if<int>(&right);
            if((leftReal || leftInt) && (rightReal || rightInt)) {
                const double a=leftReal?*leftReal:static_cast<double>(*leftInt);
                const double b=rightReal?*rightReal:static_cast<double>(*rightInt);
                switch(operation) {
                    case OP_CONG:result=a+b;specialized=true;break;
                    case OP_TRU:result=a-b;specialized=true;break;
                    case OP_NHAN:result=a*b;specialized=true;break;
                    default:break;
                }
            }
        }
    }
    if(!specialized) {
        CpuScope fallback("dynamic.numeric_slots_update");
        result=evaluateBinaryOperator(operation,left,right,0);
    }
    store(targetSlot,std::move(result));
}
bool Frame::compareIntegerSlot(int source, int immediate, int operation) {
    CpuScope profile("integer.slot_compare");
    const Value &before=valueAt(source);
    // Ordered floating-point comparisons match the VM's numeric conversion;
    // NaN remains unordered. Booleans use the VM's i32 representation.
    if(const auto *integer=std::get_if<int>(&before)) {
        switch(operation) {
            case OP_SO_SANH_BANG:return *integer==immediate;
            case OP_KHAC_BANG:return *integer!=immediate;
            case OP_LON_HON:return *integer>immediate;
            case OP_NHO_HON:return *integer<immediate;
            case OP_LON_HON_HOAC_BANG:return *integer>=immediate;
            case OP_NHO_HON_HOAC_BANG:return *integer<=immediate;
            default:break;
        }
    }
    if(const auto *real=std::get_if<double>(&before)) {
        const double rhs=static_cast<double>(immediate);
        switch(operation) {
            case OP_SO_SANH_BANG:return *real==rhs;
            case OP_KHAC_BANG:return *real!=rhs;
            case OP_LON_HON:return *real>rhs;
            case OP_NHO_HON:return *real<rhs;
            case OP_LON_HON_HOAC_BANG:return *real>=rhs;
            case OP_NHO_HON_HOAC_BANG:return *real<=rhs;
            default:break;
        }
    }
    CpuScope fallback("dynamic.integer_slot_compare");
    return runtime::stackValueTruthy(evaluateBinaryOperator(operation,before,Value(immediate),0));
}
bool Frame::compareNumericSlots(int leftSlot, int rightSlot, int operation) {
    CpuScope profile("numeric.slots_compare");
    const Value &left=valueAt(leftSlot), &right=valueAt(rightSlot);
    const auto compare=[&](auto a,auto b) {
        switch(operation) {
            case OP_SO_SANH_BANG:return a==b;
            case OP_KHAC_BANG:return a!=b;
            case OP_LON_HON:return a>b;
            case OP_NHO_HON:return a<b;
            case OP_LON_HON_HOAC_BANG:return a>=b;
            case OP_NHO_HON_HOAC_BANG:return a<=b;
            default:fail("unsupported fused numeric comparison");
        }
    };
    if(const auto *a=std::get_if<int>(&left)) {
        if(const auto *b=std::get_if<int>(&right))return compare(*a,*b);
    }
    if(const auto *a=std::get_if<double>(&left)) {
        if(const auto *b=std::get_if<double>(&right))return compare(*a,*b);
    }
    const auto *leftReal=std::get_if<double>(&left);
    const auto *rightReal=std::get_if<double>(&right);
    if(leftReal || rightReal) {
        const auto *leftInt=std::get_if<int>(&left);
        const auto *rightInt=std::get_if<int>(&right);
        if((leftReal || leftInt) && (rightReal || rightInt)) {
            const double a=leftReal?*leftReal:static_cast<double>(*leftInt);
            const double b=rightReal?*rightReal:static_cast<double>(*rightInt);
            return compare(a,b);
        }
    }
    CpuScope fallback("dynamic.numeric_slots_compare");
    return runtime::stackValueTruthy(evaluateBinaryOperator(operation,left,right,0));
}
bool Frame::runCountedScalarLoop(int counterSlot, int bound, int valueSlot,
                                 int step, int threshold) {
    CpuScope profile("native.counted_scalar_loop");
    if (counterSlot == valueSlot) return false;
    // Bound the amount of work between safepoints, including large loops.
    // Each completed chunk commits its scalar values and increment results
    // before collection, matching the VM's observable state at a safepoint.
    // Guards must be read-only. A failed specialization runs the original
    // bytecode, which may not have evaluated the value binding yet.
    const auto counterCell = cell(counterSlot, false);
    if (!counterCell) return false;
    const auto *counter = std::get_if<int>(&counterCell->value);
    if (!counter) return false;
    const std::int64_t iterations = std::int64_t(bound) - *counter;
    if (iterations > 1'000'000) return false;
    if (iterations <= 0) return true;
    const auto valueCell = cell(valueSlot, false);
    // Captured slots can share the same cell even when their IDs differ.
    if (!valueCell || valueCell == counterCell ||
        (!std::holds_alternative<int>(valueCell->value) &&
         !std::holds_alternative<double>(valueCell->value))) return false;
    auto &counterValue = counterCell->value;
    auto &numberValue = valueCell->value;
    const std::int64_t initialCounter = *counter;
    // OP_CONG_MOT pushes each new induction value. Preserve those observable
    // stack results, even when the arithmetic loop body stays unboxed.
    for (std::int64_t completed = 0; completed < iterations;) {
        const auto batch = std::min<std::int64_t>(4096, iterations - completed);
        stack.reserve(stack.size() + static_cast<std::size_t>(batch));
        if (auto *number = std::get_if<int>(&numberValue)) {
            std::uint32_t bits = static_cast<std::uint32_t>(*number);
            for (std::int64_t n = 0; n < batch; ++n) {
                bits += static_cast<std::uint32_t>(step);
                std::int32_t signedValue;
                std::memcpy(&signedValue, &bits, sizeof(bits));
                if (signedValue >= threshold)
                    bits -= static_cast<std::uint32_t>(threshold);
            }
            std::int32_t result;
            std::memcpy(&result, &bits, sizeof(result));
            *number = result;
        } else if (auto *number = std::get_if<double>(&numberValue)) {
            double result = *number;
            for (std::int64_t n = 0; n < batch; ++n) {
                result += static_cast<double>(step);
                if (result >= static_cast<double>(threshold))
                    result -= static_cast<double>(threshold);
            }
            *number = result;
        }
        const auto previous = completed;
        completed += batch;
        counterValue = static_cast<int>(initialCounter + completed);
        for (std::int64_t n = previous + 1; n <= completed; ++n)
            stack.emplace_back(static_cast<int>(initialCounter + n));
        safepoint();
    }
    return true;
}
bool Frame::runSimpleScalarLoop(int counterSlot, int bound, int valueSlot,
                                int operand, int operation) {
    CpuScope profile("native.simple_scalar_loop");
    if(counterSlot==valueSlot ||
       (operation!=OP_CONG && operation!=OP_TRU && operation!=OP_NHAN))return false;
    const auto counterCell=cell(counterSlot,false);
    if(!counterCell)return false;
    const auto *counter=std::get_if<int>(&counterCell->value);
    if(!counter)return false;
    const std::int64_t iterations=std::int64_t(bound)-*counter;
    // Guard excessively large stacks, while running ordinary long scalar
    // loops in chunks to keep collection opportunities bounded.
    if(iterations>1'000'000)return false;
    if(iterations<=0)return true;
    const auto valueCell=cell(valueSlot,false);
    if(!valueCell || valueCell==counterCell ||
       (!std::holds_alternative<int>(valueCell->value) &&
        !std::holds_alternative<double>(valueCell->value)))return false;
    auto &counterValue=counterCell->value;
    auto &numberValue=valueCell->value;
    const std::int64_t initialCounter=*counter;
    for(std::int64_t completed=0;completed<iterations;) {
        const auto batch=std::min<std::int64_t>(4096,iterations-completed);
        stack.reserve(stack.size()+static_cast<std::size_t>(batch));
        if(auto *number=std::get_if<int>(&numberValue)) {
            std::uint32_t bits=static_cast<std::uint32_t>(*number);
            const auto rhs=static_cast<std::uint32_t>(operand);
            // Modulo-2^32 operations permit exact induction folding.
            // F64 requires sequential rounding below.
            if(operation==OP_CONG)
                bits+=rhs*static_cast<std::uint32_t>(batch);
            else if(operation==OP_TRU)
                bits-=rhs*static_cast<std::uint32_t>(batch);
            else {
                std::uint32_t power=1,base=rhs;
                auto exponent=static_cast<std::uint32_t>(batch);
                while(exponent) {
                    if(exponent&1u)power*=base;
                    base*=base;
                    exponent>>=1u;
                }
                bits*=power;
            }
            std::int32_t result;
            std::memcpy(&result,&bits,sizeof(result));
            *number=result;
        } else if(auto *number=std::get_if<double>(&numberValue)) {
            double result=*number;
            const double rhs=static_cast<double>(operand);
            for(std::int64_t n=0;n<batch;++n) {
                switch(operation) {
                    case OP_CONG:result+=rhs;break;
                    case OP_TRU:result-=rhs;break;
                    case OP_NHAN:result*=rhs;break;
                    default:break;
                }
            }
            *number=result;
        }
        const auto previous=completed;
        completed+=batch;
        counterValue=static_cast<int>(initialCounter+completed);
        // Match the original OP_CONG_MOT operand-stack side effect.
        for(std::int64_t n=previous+1;n<=completed;++n)
            stack.emplace_back(static_cast<int>(initialCounter+n));
        safepoint();
    }
    return true;
}
bool Frame::runNumericAddLoop(int counterSlot, int bound, int leftSlot,
                              int rightSlot, int resultSlot) {
    CpuScope profile("native.numeric_add_loop");
    if(counterSlot==resultSlot)return false;
    const auto counterCell=cell(counterSlot,false);
    if(!counterCell)return false;
    const auto *counter=std::get_if<int>(&counterCell->value);
    if(!counter)return false;
    const std::int64_t initialCounter=*counter;
    const std::int64_t iterations=std::int64_t(bound)-initialCounter;
    if(iterations>1'000'000)return false;
    if(iterations<=0)return true;

    // Every guard is read-only. The bytecode path must still handle missing
    // cells, objects, dynamic reassignment and unusual captured aliases.
    const auto leftCell=cell(leftSlot,false);
    const auto rightCell=cell(rightSlot,false);
    const auto resultCell=cell(resultSlot,false);
    if(!leftCell||!rightCell||!resultCell||resultCell==counterCell)
        return false;
    const auto *leftInt=std::get_if<int>(&leftCell->value);
    const auto *rightInt=std::get_if<int>(&rightCell->value);
    const auto *leftReal=std::get_if<double>(&leftCell->value);
    const auto *rightReal=std::get_if<double>(&rightCell->value);
    if((!leftInt&&!leftReal)||(!rightInt&&!rightReal))return false;
    // Assigning a promoted f64 into an aliased i32 source would invalidate
    // the cached typed pointer. Fall back to dynamic bytecode in that case.
    if((resultCell==leftCell&&leftInt&&rightReal)||
       (resultCell==rightCell&&rightInt&&leftReal))return false;

    for(std::int64_t completed=0;completed<iterations;) {
        const auto batch=std::min<std::int64_t>(4096,iterations-completed);
        stack.reserve(stack.size()+static_cast<std::size_t>(batch));
        // Keep the result in a native register through each chunk. For
        // captured aliases, read the running result instead of repeatedly
        // boxing/unboxing the same RuntimeCell on every iteration.
        if(leftInt&&rightInt) {
            int result=0;
            if(leftCell==resultCell)result=*leftInt;
            else if(rightCell==resultCell)result=*rightInt;
            for(std::int64_t n=0;n<batch;++n) {
                const int induction=static_cast<int>(initialCounter+completed+n);
                const int a=leftCell==counterCell?induction:
                    leftCell==resultCell?result:*leftInt;
                const int b=rightCell==counterCell?induction:
                    rightCell==resultCell?result:*rightInt;
                result=wrapRuntimeInt32(std::int64_t(a)+b);
            }
            resultCell->value=result;
        } else {
            double result=0.0;
            if(leftCell==resultCell)result=*leftReal;
            else if(rightCell==resultCell)result=*rightReal;
            for(std::int64_t n=0;n<batch;++n) {
                const int induction=static_cast<int>(initialCounter+completed+n);
                const double a=leftReal?
                    (leftCell==resultCell?result:*leftReal):
                    static_cast<double>(leftCell==counterCell?induction:*leftInt);
                const double b=rightReal?
                    (rightCell==resultCell?result:*rightReal):
                    static_cast<double>(rightCell==counterCell?induction:*rightInt);
                result=a+b;
            }
            resultCell->value=result;
        }
        const auto previous=completed;
        completed+=batch;
        counterCell->value=static_cast<int>(initialCounter+completed);
        for(std::int64_t n=previous+1;n<=completed;++n)
            stack.emplace_back(static_cast<int>(initialCounter+n));
        safepoint();
    }
    return true;
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
    CpuScope callProfile("invoke.total");
    const FunctionEntry *function = nullptr;
    {
        CpuScope profile("invoke.validate");
        if (id < 0 || static_cast<std::size_t>(id) >= functions.size() || !functions[id].function) fail("function not found");
        function = &functions[id];
        if (args.size() < static_cast<std::size_t>(function->required) || args.size() > static_cast<std::size_t>(function->maximum))
            fail("function arity mismatch: " +
                 (function->nameIndex >= 0 ? string(function->nameIndex) : "anonymous #" + std::to_string(id)));
    }
    Frame frame = [&]() -> Frame {
        CpuScope profile("invoke.frame_create");
        return Frame(*this);
    }();
    {
        CpuScope profile("invoke.bind_arguments");
        frame.arguments = std::move(args);
        frame.receiver = std::move(receiver); frame.owner = std::move(owner);
        if (closure) { frame.locals = closure->captures; frame.owner = closure->methodOwnerClass; }
    }
    runtime::RuntimeHeapScope scope(heap);
    {
        CpuScope profile("invoke.native_function");
        function->function(frame.nativeId);
    }
    if (pendingError) { auto error = std::exchange(pendingError, {}); std::rethrow_exception(error); }
    {
        CpuScope profile("invoke.return_value");
        return std::move(frame.result);
    }
}
Value Program::invokeName(const std::string &name, std::vector<Value> args) {
    for (std::size_t i = 0; i < functions.size(); ++i)
        if (functions[i].function && functions[i].nameIndex >= 0 &&
            string(functions[i].nameIndex) == name)
            return invoke(static_cast<int>(i), std::move(args));
    fail("function not found: " + name);
}
void Frame::call(int target, int count, int nameIndex, bool indirect) {
    CpuScope callProfile("frame.call_total");
    Value callee; if (indirect) callee = pop();
    auto args = [&]() {
        CpuScope profile("frame.call_arguments");
        return take(count);
    }();
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
    if (!primitive) fail("call target not found: " + program.string(nameIndex));
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
    if (runtime::isFileMachinePrimitive(operation)) {
        push(runtime::executeFileMachinePrimitive(static_cast<Opcode>(operation),
            args, program.capabilities, program.machineFiles));
        return;
    }
    if (runtime::isDirectoryMachinePrimitive(operation)) {
        push(runtime::executeDirectoryMachinePrimitive(static_cast<Opcode>(operation),
            args, program.capabilities, program.machineDirectories));
        return;
    }
    if (runtime::isPathMachinePrimitive(operation)) {
        push(runtime::executePathMachinePrimitive(static_cast<Opcode>(operation),
            args, program.capabilities));
        return;
    }
    if (runtime::isSocketMachinePrimitive(operation)) {
        push(runtime::executeSocketMachinePrimitive(static_cast<Opcode>(operation),
            args, program.capabilities, program.files));
        return;
    }
    if (runtime::isProcessMachinePrimitive(operation)) {
        push(runtime::executeProcessMachinePrimitive(static_cast<Opcode>(operation),
            args, program.capabilities, program.files));
        return;
    }
    if (runtime::isResolverMachinePrimitive(operation)) {
        push(runtime::executeResolverMachinePrimitive(static_cast<Opcode>(operation),
            args, program.capabilities, program.files));
        return;
    }
    if (runtime::isTlsMachinePrimitive(operation)) {
        push(runtime::executeTlsMachinePrimitive(static_cast<Opcode>(operation),
            args, program.capabilities, program.files));
        return;
    }
    if (runtime::isProcessIdentityPrimitive(operation)) {
        push(runtime::executeProcessIdentityPrimitive(
            static_cast<Opcode>(operation), args, program.capabilities));
        return;
    }
    if (runtime::isTimeSleepPrimitive(operation)) {
        push(runtime::executeTimeSleepPrimitive(
            static_cast<Opcode>(operation), args, program.capabilities));
        return;
    }
    if (runtime::isTimeClockPrimitive(operation)) {
        push(runtime::executeTimeClockPrimitive(
            static_cast<Opcode>(operation), args, program.capabilities));
        return;
    }
    if (runtime::isEntropyMachinePrimitive(operation)) {
        push(runtime::executeEntropyMachinePrimitive(
            static_cast<Opcode>(operation), args, program.capabilities));
        return;
    }
    if (runtime::isEnvironmentMachinePrimitive(operation)) {
        push(runtime::executeEnvironmentMachinePrimitive(
            static_cast<Opcode>(operation), args, program.capabilities));
        return;
    }
    if (runtime::isTimeLocalPrimitive(operation)) {
        push(runtime::executeTimeLocalPrimitive(
            static_cast<Opcode>(operation), args, program.capabilities));
        return;
    }
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
    CpuScope profile("heap.collect");
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
// AOT operations without a value result return their error status directly.
// The code generator can branch on this status without making a second ABI
// call to vpp_aot_failed. Value-producing condition/case helpers still use
// boundary() so their boolean results retain their original meaning.
template<class Operation> int boundaryChecked(int id, Operation operation) noexcept {
    auto &frame = *nativeFrames[static_cast<std::size_t>(id)];
    if (frame.program.pendingError) return 1;
    try { operation(frame); return 0; }
    catch (...) { frame.program.pendingError = std::current_exception(); return 1; }
}
}

// C ABI boundaries always contain C++ exceptions. Generated machine frames
// route pending language exceptions to native catch labels explicitly.
#define AOT0(name, body) extern "C" int vpp_aot_##name(int id) noexcept { CpuScope profile("abi." #name);return boundaryChecked(id, [](Frame &f) { body; }); }
#define AOT1(name, body) extern "C" int vpp_aot_##name(int id, int a) noexcept { CpuScope profile("abi." #name);return boundaryChecked(id, [=](Frame &f) { body; }); }
#define AOT2(name, body) extern "C" int vpp_aot_##name(int id, int a, int b) noexcept { CpuScope profile("abi." #name);return boundaryChecked(id, [=](Frame &f) { body; }); }
#define AOT3(name, body) extern "C" int vpp_aot_##name(int id, int a, int b, int c) noexcept { CpuScope profile("abi." #name);return boundaryChecked(id, [=](Frame &f) { body; }); }
#define AOT4(name, body) extern "C" int vpp_aot_##name(int id, int a, int b, int c, int d) noexcept { CpuScope profile("abi." #name);return boundaryChecked(id, [=](Frame &f) { body; }); }
AOT1(integer, f.push(a))
AOT1(string, f.push(f.program.string(a)))
AOT1(floating, f.push(std::stod(f.program.string(a))))
AOT0(null, f.push(make_null_value()))
AOT1(load, f.push(f.load(a)))
AOT1(initialize, f.initialize(a))
AOT0(assign, f.assign())
// Fold a known variable ID into the store: no temporary boxed ID or
// second ABI crossing. The value is still consumed from the operand stack.
AOT1(assign_slot, f.store(a,f.pop()))
AOT2(store_integer, f.store(a,Value(b)))
AOT3(parameter, f.parameter(a,b,c))
AOT1(increment, f.increment(a))
AOT1(binary, f.binary(a))
AOT2(increment_slot, f.incrementSlot(a,b))
AOT4(update_integer_slot, f.updateIntegerSlot(a,b,c,d))
AOT4(update_numeric_slots, f.updateNumericSlots(a,b,c,d))
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
AOT1(return_slot, f.result=f.load(a))
AOT1(return_integer, f.result=Value(a))
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
#undef AOT4
extern "C" int vpp_aot_compare_integer_slot(int id,int slot,int immediate,int operation) noexcept {
    CpuScope profile("abi.compare_integer_slot");
    auto &f=*nativeFrames[static_cast<std::size_t>(id)];
    if(f.program.pendingError)return 2;
    try {return f.compareIntegerSlot(slot,immediate,operation)?1:0;}
    catch(...) {f.program.pendingError=std::current_exception();return 2;}
}
extern "C" int vpp_aot_compare_numeric_slots(int id,int left,int right,int operation) noexcept {
    CpuScope profile("abi.compare_numeric_slots");
    auto &f=*nativeFrames[static_cast<std::size_t>(id)];
    if(f.program.pendingError)return 2;
    try {return f.compareNumericSlots(left,right,operation)?1:0;}
    catch(...) {f.program.pendingError=std::current_exception();return 2;}
}
extern "C" int vpp_aot_counted_scalar_loop(int id,int counter,int bound,
                                            int value,int step,int threshold) noexcept {
    CpuScope profile("abi.counted_scalar_loop");
    auto &f=*nativeFrames[static_cast<std::size_t>(id)];
    if(f.program.pendingError)return 2;
    try {return f.runCountedScalarLoop(counter,bound,value,step,threshold)?0:1;}
    catch(...) {f.program.pendingError=std::current_exception();return 2;}
}
extern "C" int vpp_aot_simple_scalar_loop(int id,int counter,int bound,
                                           int value,int operand,int operation) noexcept {
    CpuScope profile("abi.simple_scalar_loop");
    auto &f=*nativeFrames[static_cast<std::size_t>(id)];
    if(f.program.pendingError)return 2;
    try {return f.runSimpleScalarLoop(counter,bound,value,operand,operation)?0:1;}
    catch(...) {f.program.pendingError=std::current_exception();return 2;}
}
extern "C" int vpp_aot_numeric_add_loop(int id,int counter,int bound,
                                          int left,int right,int result) noexcept {
    CpuScope profile("abi.numeric_add_loop");
    auto &f=*nativeFrames[static_cast<std::size_t>(id)];
    if(f.program.pendingError)return 2;
    try {return f.runNumericAddLoop(counter,bound,left,right,result)?0:1;}
    catch(...) {f.program.pendingError=std::current_exception();return 2;}
}
extern "C" int vpp_aot_condition(int id) noexcept {
    CpuScope profile("abi.condition");
    return boundary(id, [](Frame &f) { return int(runtime::stackValueTruthy(f.pop())); });
}
extern "C" int vpp_aot_case(int id,int a,int b) noexcept {
    CpuScope profile("abi.case");
    return boundary(id,[=](Frame &f) {return int(f.caseMatches(a,b));});
}
extern "C" int vpp_aot_default(int id) noexcept {
    CpuScope profile("abi.default");
    return boundary(id,[](Frame &f) {return int(f.defaultMatches());});
}
extern "C" int vpp_aot_failed(int id) noexcept {
    CpuScope profile("abi.failed");
    return nativeFrames[static_cast<std::size_t>(id)]->program.pendingError ? 1 : 0;
}
extern "C" int vpp_aot_handle_error(int id) noexcept {
    CpuScope profile("abi.handle_error");
    auto &f=*nativeFrames[static_cast<std::size_t>(id)];
    if (!f.program.pendingError || f.handlers.empty()) return -1;
    try { std::rethrow_exception(f.program.pendingError); }
    catch(const runtime::LanguageException &error) {
        const auto handler=f.handlers.back(); f.handlers.pop_back();
        f.stack.resize(handler.stack); f.switches.resize(handler.switches);
        f.push(error.value()); f.program.pendingError={}; return handler.target;
    } catch(...) { return -1; }
}
void dumpAotCpuProfile() {
    if(!cpuProfiler.enabled)return;
    std::cerr<<"VPP_AOT_CPU_PROFILE unit=cpu_ns inclusive=may_overlap instrumented=1\n";
    for(const auto &[name,entry]:cpuProfiler.entries)
        std::cerr<<"VPP_AOT_CPU_PROFILE "<<name<<" calls="<<entry.calls
                 <<" inclusive_ns="<<entry.inclusive<<" self_ns="<<entry.exclusive<<'\n';
    std::cerr<<"VPP_AOT_CPU_PROFILE local_cells_new="<<localFrameCache.newCells
             <<" local_cells_reused="<<localFrameCache.reusedCells
             <<" local_maps_reused="<<localFrameCache.reusedMaps
             <<" operand_stacks_reused="<<localFrameCache.reusedStacks<<'\n';
    // vm_alloc lives in the separately compiled V++ VM and is not reachable
    // from this runtime's ABI. Never imply that zero means zero allocations.
    std::cerr<<"VPP_AOT_CPU_PROFILE vm_alloc=unavailable_in_cpp_aot\n";
}
} // namespace vietvm::native::aot
