#include "vpp/native/aarch64_jit.h"

#if (defined(__linux__) || defined(_WIN32)) && (defined(__x86_64__) || defined(_M_X64))

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace vietvm::native {
namespace {

// All expression values live in the native stack frame. No VM pointers,
// callbacks or C++ exceptions enter these generated functions. The first
// 32 bytes of a Windows frame are always reserved for Win64 shadow space.
#ifdef _WIN32
constexpr int kShadow = 32;
#else
constexpr int kShadow = 0;
#endif
constexpr int kStackDepth = 7;
constexpr int kArg0 = kShadow;
constexpr int kArg1 = kShadow + 4;
constexpr int stackOffset(int depth) { return kShadow + 8 + depth * 4; }
constexpr int kFrameBytes = ((kShadow + 8 + kStackDepth * 4 + 7) / 16) * 16 + 8;
static_assert(sizeof(int) == 4 && sizeof(void *) == 8,
              "machine JIT x64 requires 32-bit int and 64-bit pointers");
static_assert(kFrameBytes % 16 == 8,
              "x64 CALL requires a 16-byte aligned stack after the prologue");
static_assert(stackOffset(kStackDepth - 1) + 4 <= kFrameBytes,
              "x64 expression slots must fit the native frame");

bool comparison(int op) {
    switch (op) {
        case OP_SO_SANH_BANG: case OP_KHAC_BANG:
        case OP_LON_HON: case OP_NHO_HON:
        case OP_LON_HON_HOAC_BANG: case OP_NHO_HON_HOAC_BANG:
            return true;
        default: return false;
    }
}
bool binaryOp(int op) {
    return op == OP_CONG || op == OP_TRU || op == OP_NHAN ||
           op == OP_Logic_VA || op == OP_Logic_HOAC || comparison(op);
}
bool evaluateComparison(int op, std::int64_t a, std::int64_t b) {
    switch (op) {
        case OP_SO_SANH_BANG: return a == b;
        case OP_KHAC_BANG: return a != b;
        case OP_LON_HON: return a > b;
        case OP_NHO_HON: return a < b;
        case OP_LON_HON_HOAC_BANG: return a >= b;
        case OP_NHO_HON_HOAC_BANG: return a <= b;
        default: return false;
    }
}

struct CallPatch { std::size_t end; int target; int argc; };
struct Candidate {
    std::vector<std::uint8_t> bytes;
    std::vector<CallPatch> calls;
    int argc = 0;
};

struct Emitter {
    std::vector<std::uint8_t> bytes;
    std::vector<std::size_t> overflowJumps;
    std::vector<CallPatch> calls;
    int depth = 0;

    void emit(std::initializer_list<std::uint8_t> op) {
        bytes.insert(bytes.end(), op.begin(), op.end());
    }
    void u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void displacement(std::size_t end, std::size_t target) {
        const auto distance = static_cast<std::int64_t>(target) -
                              static_cast<std::int64_t>(end);
        if (distance < std::numeric_limits<std::int32_t>::min() ||
            distance > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("machine JIT x64: call/branch beyond rel32 range");
        }
        const auto value = static_cast<std::uint32_t>(static_cast<std::int32_t>(distance));
        for (int i = 0; i < 4; ++i) bytes[end - 4 + i] =
            static_cast<std::uint8_t>(value >> (i * 8));
    }

    // Encoding: mod=10, r/m=100 (SIB follows), SIB [rsp + disp32].
    // All scratch registers are caller-saved, so nested native calls preserve
    // the entire expression stack through these local memory slots.
    void loadEax(int offset) { emit({0x8b, 0x84, 0x24}); u32(offset); }
    void loadEdx(int offset) { emit({0x8b, 0x94, 0x24}); u32(offset); }
    void storeEax(int offset) { emit({0x89, 0x84, 0x24}); u32(offset); }

    void begin(bool parameters) {
        emit({0x48, 0x81, 0xec}); u32(kFrameBytes); // sub rsp, frame; 16-byte CALL alignment
        if (parameters) {
#ifdef _WIN32
            emit({0x89, 0x8c, 0x24}); u32(kArg0); // mov [rsp+arg0], ecx
            emit({0x89, 0x94, 0x24}); u32(kArg1); // mov [rsp+arg1], edx
#else
            emit({0x89, 0xbc, 0x24}); u32(kArg0); // mov [rsp+arg0], edi
            emit({0x89, 0xb4, 0x24}); u32(kArg1); // mov [rsp+arg1], esi
#endif
        }
    }
    void end() { emit({0x48, 0x81, 0xc4}); u32(kFrameBytes); emit({0xc3}); }
    void constant(int value) {
        emit({0xb8}); u32(static_cast<std::uint32_t>(value)); // mov eax,imm32
        storeEax(stackOffset(depth++));
    }
    void parameter(int index) {
        loadEax(index == 0 ? kArg0 : kArg1);
        storeEax(stackOffset(depth++));
    }
    void negate() {
        loadEax(stackOffset(depth - 1));
        emit({0x85, 0xc0, 0x0f, 0x94, 0xc0, 0x0f, 0xb6, 0xc0}); // test; sete; movzx
        storeEax(stackOffset(depth - 1));
    }
    void overflowOn(std::uint8_t condition) {
        emit({0x0f, condition}); u32(0); // near JO (80) or JNE (85)
        overflowJumps.push_back(bytes.size());
    }
    void binary(int op, bool checked) {
        --depth;
        loadEax(stackOffset(depth - 1));
        loadEdx(stackOffset(depth));
        switch (op) {
            case OP_CONG: emit({0x01, 0xd0}); if (checked) overflowOn(0x80); break;
            case OP_TRU: emit({0x29, 0xd0}); if (checked) overflowOn(0x80); break;
            case OP_NHAN: emit({0x0f, 0xaf, 0xc2}); if (checked) overflowOn(0x80); break;
            case OP_Logic_VA: case OP_Logic_HOAC:
                emit({0x85, 0xc0, 0x0f, 0x95, 0xc0, 0x0f, 0xb6, 0xc0});
                emit({0x85, 0xd2, 0x0f, 0x95, 0xc2, 0x0f, 0xb6, 0xd2});
                if (op == OP_Logic_VA) emit({0x21, 0xd0}); // and eax,edx
                else emit({0x09, 0xd0}); // or eax,edx
                break;
            default: {
                emit({0x39, 0xd0}); // cmp eax,edx; signed SETcc al
                std::uint8_t cc = 0;
                switch (op) {
                    case OP_SO_SANH_BANG: cc = 0x94; break;
                    case OP_KHAC_BANG: cc = 0x95; break;
                    case OP_LON_HON: cc = 0x9f; break;
                    case OP_NHO_HON: cc = 0x9c; break;
                    case OP_LON_HON_HOAC_BANG: cc = 0x9d; break;
                    case OP_NHO_HON_HOAC_BANG: cc = 0x9e; break;
                    default: throw std::logic_error("x64 emitter unsupported opcode");
                }
                emit({0x0f, cc, 0xc0, 0x0f, 0xb6, 0xc0});
            }
        }
        storeEax(stackOffset(depth - 1));
    }
    void call(int id, int argc) {
        const int resultSlot = depth - argc;
        if (argc > 0) {
#ifdef _WIN32
            emit({0x8b, 0x8c, 0x24}); // mov ecx,[rsp+arg0]
#else
            emit({0x8b, 0xbc, 0x24}); // mov edi,[rsp+arg0]
#endif
            u32(stackOffset(resultSlot));
        }
        if (argc > 1) {
#ifdef _WIN32
            emit({0x8b, 0x94, 0x24}); // mov edx,[rsp+arg1]
#else
            emit({0x8b, 0xb4, 0x24}); // mov esi,[rsp+arg1]
#endif
            u32(stackOffset(resultSlot + 1));
        }
        emit({0xe8}); u32(0); // call rel32, patched when DAG is laid out
        calls.push_back({bytes.size(), id, argc});
        emit({0x48, 0x63, 0xd0, 0x48, 0x39, 0xd0}); // movsxd rdx,eax; cmp rax,rdx
        overflowOn(0x85); // propagate non-int32 sentinel from a nested call
        storeEax(stackOffset(resultSlot));
        depth = resultSlot + 1;
    }
    void result() {
        loadEax(stackOffset(depth - 1));
        emit({0x48, 0x98}); // cdqe: full signed int32 success result in rax
        end();
    }
    void finish() {
        if (overflowJumps.empty()) return;
        const auto overflow = bytes.size();
        emit({0x48, 0xb8}); // movabs rax,0x100000000 (outside int32 range)
        u32(0); u32(1);
        end();
        for (const auto end : overflowJumps) displacement(end, overflow);
    }
};

std::optional<Candidate> compileCandidate(const std::vector<Instruction> &code,
                                           int argc, bool allowCalls) {
    if (code.empty() || code.size() > 96 || argc < 0 || argc > 2) return std::nullopt;
    std::array<int, 2> paramSlots = {-1, -1};
    std::size_t i = code[0].op == OP_MO_KHOI ? 1 : 0;
    for (int n = 0; n < argc; ++n) {
        if (i + 1 < code.size() && code[i].op == OP_KHOI_TAO &&
            code[i + 1].op == OP_PARAM &&
            code[i].operandIndex == code[i + 1].operandIndex) ++i;
        if (i >= code.size() || code[i].op != OP_PARAM ||
            code[i].operandValue != n || code[i].operandIndex < 0 ||
            (n == 1 && code[i].operandIndex == paramSlots[0])) return std::nullopt;
        paramSlots[n] = code[i++].operandIndex;
    }
    Emitter emitter;
    emitter.begin(true);
    bool returned = false;
    for (; i < code.size(); ++i) {
        const auto &inst = code[i];
        if (inst.op == OP_TRA_VE) {
            if (emitter.depth != 1) return std::nullopt;
            emitter.result();
            returned = true;
            ++i;
            break;
        }
        if (inst.op == OP_BIEN_SO || inst.op == OP_DUNG_GIA_TRI ||
            inst.op == OP_SAI_GIA_TRI || inst.op == OP_TEN_BIEN_GIA_TRI) {
            if (emitter.depth == kStackDepth) return std::nullopt;
            if (inst.op == OP_TEN_BIEN_GIA_TRI) {
                int param = -1;
                for (int n = 0; n < argc; ++n)
                    if (paramSlots[n] == inst.operandIndex) param = n;
                if (param < 0) return std::nullopt;
                emitter.parameter(param);
            } else {
                emitter.constant(inst.op == OP_BIEN_SO ? inst.operand :
                                 inst.op == OP_DUNG_GIA_TRI ? 1 : 0);
            }
        } else if (inst.op == OP_PHU_DINH || inst.op == OP_KHONG) {
            if (emitter.depth < 1) return std::nullopt;
            emitter.negate();
        } else if (inst.op == OP_GOI && allowCalls) {
            if (inst.operandIndex < 0 || inst.operand < 0 || inst.operand > 2 ||
                emitter.depth < inst.operand ||
                (inst.operand == 0 && emitter.depth == kStackDepth)) return std::nullopt;
            emitter.call(inst.operandIndex, inst.operand);
        } else if (binaryOp(inst.op)) {
            if (emitter.depth < 2) return std::nullopt;
            emitter.binary(inst.op, true);
        } else return std::nullopt;
    }
    if (!returned) return std::nullopt;
    for (; i < code.size(); ++i)
        if (i != code.size() - 1 || code[i].op != OP_DONG_KHOI) return std::nullopt;
    emitter.finish();
    return Candidate{std::move(emitter.bytes), std::move(emitter.calls), argc};
}

} // namespace

MachineExpression::MachineExpression(void *memory, std::size_t bytes,
                                     std::size_t instructions) noexcept
    : code_(memory), allocatedBytes_(bytes), instructionCount_(instructions) {}

bool MachineExpression::supported() noexcept { return true; }

MachineExpression::~MachineExpression() {
    if (!code_) return;
#ifdef _WIN32
    VirtualFree(code_, 0, MEM_RELEASE);
#else
    munmap(code_, allocatedBytes_);
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::fromBytes(
    const std::vector<std::uint8_t> &bytes, std::size_t instructions) {
    if (bytes.empty()) return nullptr;
    void *memory = nullptr;
    std::size_t allocation = 0;
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
#else
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) throw std::runtime_error("machine JIT: invalid page size");
    const auto page = static_cast<std::size_t>(pageSize);
#endif
    if (page == 0) throw std::runtime_error("machine JIT x64: invalid page size");
    if (bytes.size() > std::numeric_limits<std::size_t>::max() - (page - 1))
        throw std::overflow_error("machine JIT x64: allocation size overflow");
    allocation = ((bytes.size() + page - 1) / page) * page;
#ifdef _WIN32
    memory = VirtualAlloc(nullptr, allocation, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) throw std::runtime_error("machine JIT x64: VirtualAlloc failed: " +
                                          std::to_string(GetLastError()));
#else
    memory = mmap(nullptr, allocation, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) throw std::runtime_error("machine JIT x64: mmap failed: errno=" +
                                                        std::to_string(errno));
#endif
    std::memcpy(memory, bytes.data(), bytes.size());
#ifdef _WIN32
    DWORD oldProtect = 0;
    if (!VirtualProtect(memory, allocation, PAGE_EXECUTE_READ, &oldProtect) ||
        !FlushInstructionCache(GetCurrentProcess(), memory, bytes.size())) {
        const DWORD error = GetLastError();
        VirtualFree(memory, 0, MEM_RELEASE);
        throw std::runtime_error("machine JIT x64: executable protection/cache failed: " +
                                 std::to_string(error));
    }
#else
    if (mprotect(memory, allocation, PROT_READ | PROT_EXEC) != 0) {
        const int error = errno;
        munmap(memory, allocation);
        throw std::runtime_error("machine JIT x64: mprotect RX failed: errno=" +
                                 std::to_string(error));
    }
#endif
    return std::unique_ptr<MachineExpression>(new MachineExpression(memory, allocation, instructions));
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompile(
    const std::vector<Instruction> &code, std::size_t start,
    const std::vector<bool> &entryPoints) {
    if (entryPoints.size() != code.size())
        throw std::invalid_argument("machine JIT: invalid control-flow entry map");
    if (start >= code.size()) return nullptr;
    std::vector<std::int64_t> values;
    bool operation = false;
    std::size_t last = start;
    for (std::size_t i = start; i < code.size() && i - start < 96; ++i) {
        if (i > start && entryPoints[i]) break;
        const int op = code[i].op;
        if (op == OP_BIEN_SO || op == OP_DUNG_GIA_TRI || op == OP_SAI_GIA_TRI) {
            if (values.size() >= kStackDepth) break;
            values.push_back(op == OP_BIEN_SO ? code[i].operand :
                             op == OP_DUNG_GIA_TRI ? 1 : 0);
        } else if (binaryOp(op)) {
            if (values.size() < 2) break;
            const auto rhs = values.back(); values.pop_back();
            const auto lhs = values.back(); values.pop_back();
            const std::int64_t result = comparison(op) ? evaluateComparison(op, lhs, rhs) :
                op == OP_Logic_VA ? (lhs != 0 && rhs != 0) :
                op == OP_Logic_HOAC ? (lhs != 0 || rhs != 0) :
                op == OP_CONG ? lhs + rhs : op == OP_TRU ? lhs - rhs : lhs * rhs;
            if (result < std::numeric_limits<std::int32_t>::min() ||
                result > std::numeric_limits<std::int32_t>::max()) break;
            values.push_back(result);
            operation = true;
        } else break;
        if (values.size() == 1 && operation) last = i + 1;
    }
    if (last == start) return nullptr;
    Emitter emitter;
    emitter.begin(false);
    for (std::size_t i = start; i < last; ++i) {
        const auto &inst = code[i];
        if (inst.op == OP_BIEN_SO || inst.op == OP_DUNG_GIA_TRI || inst.op == OP_SAI_GIA_TRI)
            emitter.constant(inst.op == OP_BIEN_SO ? inst.operand :
                             inst.op == OP_DUNG_GIA_TRI ? 1 : 0);
        else emitter.binary(inst.op, false); // constants were range-checked above
    }
    emitter.result();
    return fromBytes(emitter.bytes, last - start);
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileBinary(int opcode) {
    if (!binaryOp(opcode)) return nullptr;
    Emitter emitter;
    emitter.begin(true);
    emitter.parameter(0);
    emitter.parameter(1);
    emitter.binary(opcode, false); // VM already handles runtime overflow checks
    emitter.result();
    return fromBytes(emitter.bytes, 1);
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileConditionalBranch() {
#ifdef _WIN32
    // test ecx,ecx; mov rax,rdx; cmovne rax,r8; ret
    return fromBytes({0x85,0xc9,0x48,0x89,0xd0,0x49,0x0f,0x45,0xc0,0xc3}, 1);
#else
    // test edi,edi; mov rax,rsi; cmovne rax,rdx; ret
    return fromBytes({0x85,0xff,0x48,0x89,0xf0,0x48,0x0f,0x45,0xc2,0xc3}, 1);
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileLeaf(
    const std::vector<Instruction> &code, int argc) {
    auto candidate = compileCandidate(code, argc, false);
    return candidate ? fromBytes(candidate->bytes, code.size()) : nullptr;
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileCallBundle(
    const std::unordered_map<int, std::vector<Instruction>> &functions) {
    std::unordered_map<int, Candidate> candidates;
    for (const auto &[id, code] : functions) {
        for (int argc = 0; argc <= 2; ++argc) {
            auto candidate = compileCandidate(code, argc, true);
            if (candidate) { candidates.emplace(id, std::move(*candidate)); break; }
        }
    }
    std::unordered_map<int, unsigned> states;
    std::unordered_map<int, CallEntry> entries;
    std::function<bool(int)> validate = [&](int id) {
        const unsigned state = states[id];
        if (state == 1 || state == 3) return false;
        if (state == 2) return true;
        const auto it = candidates.find(id);
        if (it == candidates.end()) return false;
        states[id] = 1;
        std::size_t depth = 1, count = functions.at(id).size();
        for (const auto &call : it->second.calls) {
            const auto callee = candidates.find(call.target);
            if (callee == candidates.end() || callee->second.argc != call.argc ||
                !validate(call.target)) { states[id] = 3; return false; }
            depth = std::max(depth, entries.at(call.target).depth + 1);
            if (depth > 32 || count > 1000000 - entries.at(call.target).instructionCount) {
                states[id] = 3;
                return false;
            }
            count += entries.at(call.target).instructionCount;
        }
        entries[id] = {0, depth, count, !it->second.calls.empty()};
        states[id] = 2;
        return true;
    };
    std::vector<int> roots;
    for (const auto &[id, candidate] : candidates)
        if (!candidate.calls.empty() && validate(id)) roots.push_back(id);
    if (roots.empty()) return nullptr;
    std::sort(roots.begin(), roots.end());
    std::vector<int> order;
    std::unordered_map<int, bool> included;
    std::function<void(int)> include = [&](int id) {
        if (included[id]) return;
        included[id] = true;
        order.push_back(id);
        for (const auto &call : candidates.at(id).calls) include(call.target);
    };
    for (int root : roots) include(root);
    std::vector<std::uint8_t> bytes;
    for (int id : order) {
        entries.at(id).wordOffset = bytes.size(); // x64 uses byte offsets, not AArch64 words
        const auto &body = candidates.at(id).bytes;
        bytes.insert(bytes.end(), body.begin(), body.end());
    }
    Emitter patcher;
    patcher.bytes = std::move(bytes);
    for (int id : order) {
        const auto base = entries.at(id).wordOffset;
        for (const auto &call : candidates.at(id).calls)
            patcher.displacement(base + call.end, entries.at(call.target).wordOffset);
    }
    auto bundle = fromBytes(patcher.bytes, 0);
    for (int id : order) bundle->callEntries_.emplace(id, entries.at(id));
    return bundle;
}

bool MachineExpression::hasNativeCalls(int id) const noexcept {
    const auto it = callEntries_.find(id);
    return it != callEntries_.end() && it->second.hasCalls;
}
std::size_t MachineExpression::nativeCallDepth(int id) const noexcept {
    const auto it = callEntries_.find(id);
    return it == callEntries_.end() ? 0 : it->second.depth;
}
std::size_t MachineExpression::nativeCallInstructionCount(int id) const noexcept {
    const auto it = callEntries_.find(id);
    return it == callEntries_.end() ? 0 : it->second.instructionCount;
}
std::optional<int> MachineExpression::executeNativeCall(int id, int a, int b) const {
    const auto it = callEntries_.find(id);
    if (it == callEntries_.end()) return std::nullopt;
    using Fn = std::int64_t (*)(int, int);
    const auto *entry = static_cast<std::uint8_t *>(code_) + it->second.wordOffset;
    const auto result = reinterpret_cast<Fn>(const_cast<std::uint8_t *>(entry))(a, b);
    if (result < std::numeric_limits<std::int32_t>::min() ||
        result > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    return static_cast<int>(result);
}
int MachineExpression::execute() const {
    return reinterpret_cast<int (*)()>(code_)();
}
int MachineExpression::execute(int a, int b) const {
    return reinterpret_cast<int (*)(int, int)>(code_)(a, b);
}
std::size_t MachineExpression::executeBranch(int condition, std::size_t falsePc,
                                              std::size_t truePc) const {
    return reinterpret_cast<std::size_t (*)(int, std::size_t, std::size_t)>(code_)(
        condition, falsePc, truePc);
}
std::optional<int> MachineExpression::executeLeaf(int a, int b) const {
    const auto result = reinterpret_cast<std::int64_t (*)(int, int)>(code_)(a, b);
    if (result < std::numeric_limits<std::int32_t>::min() ||
        result > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    return static_cast<int>(result);
}

} // namespace vietvm::native
#endif // Linux/Windows x86-64
