#include "vpp/native/aarch64_jit.h"

// x86-64 Linux/Windows use the separate x64 emitter (System V / Win64 ABI).
#if !((defined(__linux__) || defined(_WIN32)) && (defined(__x86_64__) || defined(_M_X64)))

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
#include <cerrno>
#if defined(__APPLE__)
#include <pthread.h>
#endif
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace vietvm::native {

#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
namespace {

// CSET is CSINC Wd, WZR, WZR, invert(condition). These are the inverse
// AArch64 condition bits, used for signed int32 comparisons.
std::optional<unsigned> csetInverseCondition(int opcode) {
    switch (opcode) {
        case OP_SO_SANH_BANG: return 0x1;             // NE
        case OP_KHAC_BANG: return 0x0;                // EQ
        case OP_LON_HON: return 0xd;                  // LE
        case OP_NHO_HON: return 0xa;                  // GE
        case OP_LON_HON_HOAC_BANG: return 0xb;        // LT
        case OP_NHO_HON_HOAC_BANG: return 0xc;        // GT
        default: return std::nullopt;
    }
}

void emitIntegerComparison(std::vector<std::uint32_t> &words,
                           unsigned leftRegister, unsigned rightRegister,
                           int opcode) {
    const unsigned inverseCondition = *csetInverseCondition(opcode);
    // CMP Wn, Wm; CSET Wn, cond. The result is canonical int32 0 or 1.
    words.push_back(0x6b00001fu | (rightRegister << 16) | (leftRegister << 5));
    words.push_back(0x1a9f07e0u | (inverseCondition << 12) | leftRegister);
}

bool isIntegerLogic(int opcode) {
    return opcode == OP_Logic_VA || opcode == OP_Logic_HOAC;
}

void emitIntegerLogic(std::vector<std::uint32_t> &words,
                      unsigned leftRegister, unsigned rightRegister,
                      int opcode) {
    // CMP Wn,#0; CSET Wn,NE for both operands, yielding canonical booleans.
    for (const unsigned reg : {leftRegister, rightRegister}) {
        words.push_back(0x7100001fu | (reg << 5));
        words.push_back(0x1a9f07e0u | reg);
    }
    const std::uint32_t operands = (rightRegister << 16) |
                                   (leftRegister << 5) | leftRegister;
    words.push_back((opcode == OP_Logic_VA ? 0x0a000000u : 0x2a000000u) |
                    operands); // AND / ORR Wleft,Wleft,Wright
}

bool evaluateIntComparison(int opcode, std::int64_t left, std::int64_t right) {
    switch (opcode) {
        case OP_SO_SANH_BANG: return left == right;
        case OP_KHAC_BANG: return left != right;
        case OP_LON_HON: return left > right;
        case OP_NHO_HON: return left < right;
        case OP_LON_HON_HOAC_BANG: return left >= right;
        case OP_NHO_HON_HOAC_BANG: return left <= right;
        default: return false;
    }
}

// The call bundle uses one MAP_JIT allocation, so every BL can be patched
// using a local PC-relative offset. Function bodies have no heap references,
// callbacks, VM state, or observable effects; an overflow can replay bytecode.
struct CallPatch {
    std::size_t wordIndex;
    int callee;
    int argc;
};

struct CallCandidate {
    std::vector<std::uint32_t> words;
    std::vector<CallPatch> patches;
    int argc = 0;
};

std::uint32_t stackPair(bool push, unsigned first, unsigned second) {
    // STP Xt,Xu,[SP,#-16]! / LDP Xt,Xu,[SP],#16. SP stays 16-byte aligned.
    return (push ? 0xa9bf0000u : 0xa8c10000u) |
           (second << 10) | (31u << 5) | first;
}

void emitMoveW(std::vector<std::uint32_t> &words,
               unsigned dest, unsigned source) {
    words.push_back(0x2a0003e0u | (source << 16) | dest);
}

std::optional<CallCandidate> compileCallCandidate(
    const std::vector<Instruction> &bytecode, int argc) {
    if (argc < 0 || argc > 2 || bytecode.empty() || bytecode.size() > 96) {
        return std::nullopt;
    }
    std::array<int, 2> paramSlots = {-1, -1};
    std::size_t pos = 0;
    if (bytecode[pos].op == OP_MO_KHOI) ++pos;
    for (int index = 0; index < argc; ++index) {
        if (pos + 1 < bytecode.size() &&
            bytecode[pos].op == OP_KHOI_TAO &&
            bytecode[pos + 1].op == OP_PARAM &&
            bytecode[pos].operandIndex == bytecode[pos + 1].operandIndex) {
            ++pos;
        }
        if (pos >= bytecode.size() || bytecode[pos].op != OP_PARAM ||
            bytecode[pos].operandValue != index ||
            bytecode[pos].operandIndex < 0) return std::nullopt;
        const int slot = bytecode[pos++].operandIndex;
        if (index == 1 && slot == paramSlots[0]) return std::nullopt;
        paramSlots[static_cast<std::size_t>(index)] = slot;
    }

    CallCandidate candidate;
    candidate.argc = argc;
    auto &words = candidate.words;
    // Preserve callee-saved parameter registers and LR. Nested calls may use
    // w0/w1 for their own arguments; our parameters live in w19/w20.
    words.push_back(stackPair(true, 19, 20));
    words.push_back(stackPair(true, 29, 30));
    emitMoveW(words, 19, 0);
    emitMoveW(words, 20, 1);

    constexpr unsigned firstRegister = 9;
    constexpr unsigned registerCount = 7;
    unsigned depth = 0;
    std::vector<std::size_t> overflowBranches;
    std::size_t returnBranch = 0;
    bool returned = false;
    for (; pos < bytecode.size(); ++pos) {
        const Instruction &inst = bytecode[pos];
        if (inst.op == OP_TRA_VE) {
            if (depth != 1) return std::nullopt;
            words.push_back(0x93407c00u | ((firstRegister + depth - 1) << 5));
            returnBranch = words.size();
            words.push_back(0x14000000u); // B epilogue
            returned = true;
            ++pos;
            break;
        }
        if (inst.op == OP_BIEN_SO || inst.op == OP_DUNG_GIA_TRI ||
            inst.op == OP_SAI_GIA_TRI || inst.op == OP_TEN_BIEN_GIA_TRI) {
            if (depth == registerCount) return std::nullopt;
            const unsigned dst = firstRegister + depth++;
            if (inst.op == OP_TEN_BIEN_GIA_TRI) {
                int source = -1;
                for (int index = 0; index < argc; ++index) {
                    if (paramSlots[static_cast<std::size_t>(index)] ==
                        inst.operandIndex) source = index;
                }
                if (source < 0) return std::nullopt;
                emitMoveW(words, dst, static_cast<unsigned>(19 + source));
            } else {
                const std::uint32_t value = static_cast<std::uint32_t>(
                    inst.op == OP_BIEN_SO ? inst.operand :
                    inst.op == OP_DUNG_GIA_TRI ? 1 : 0);
                words.push_back(0x52800000u | ((value & 0xffffu) << 5) | dst);
                words.push_back(0x72a00000u | ((value >> 16) << 5) | dst);
            }
            continue;
        }
        if (inst.op == OP_KHONG || inst.op == OP_PHU_DINH) {
            if (!depth) return std::nullopt;
            const unsigned dst = firstRegister + depth - 1;
            words.push_back(0x7100001fu | (dst << 5));
            words.push_back(0x1a9f17e0u | dst);
            continue;
        }
        if (inst.op == OP_GOI) {
            const int count = inst.operand;
            // Negative operandIndex denotes a name, not a stable function ID.
            if (inst.operandIndex < 0 || count < 0 || count > 2 ||
                depth < static_cast<unsigned>(count) ||
                (count == 0 && depth == registerCount)) return std::nullopt;
            const unsigned result = firstRegister + depth - count;
            // Native callees own w9..w15, so preserve all live expression
            // temporaries across BL. Pairing x15 with XZR keeps SP aligned.
            for (const auto pair : {std::pair{9u, 10u}, {11u, 12u},
                                    {13u, 14u}, {15u, 31u}}) {
                words.push_back(stackPair(true, pair.first, pair.second));
            }
            if (count >= 1) emitMoveW(words, 0, result);
            if (count == 2) emitMoveW(words, 1, result + 1);
            candidate.patches.push_back({words.size(), inst.operandIndex, count});
            words.push_back(0x94000000u); // BL resolved after layout
            for (const auto pair : {std::pair{15u, 31u}, {13u, 14u},
                                    {11u, 12u}, {9u, 10u}}) {
                words.push_back(stackPair(false, pair.first, pair.second));
            }
            // All generated functions return signed int32 in X0; any other
            // 64-bit value is the checked-overflow sentinel.
            words.push_back(0x93407c10u); // SXTW X16,W0
            words.push_back(0xeb10001fu); // CMP X0,X16
            overflowBranches.push_back(words.size());
            words.push_back(0x54000001u); // B.NE overflow
            emitMoveW(words, result, 0);
            depth = depth - static_cast<unsigned>(count) + 1;
            continue;
        }
        const bool comparison = csetInverseCondition(inst.op).has_value();
        const bool logic = isIntegerLogic(inst.op);
        if (inst.op != OP_CONG && inst.op != OP_TRU &&
            inst.op != OP_NHAN && !comparison && !logic) return std::nullopt;
        if (depth < 2) return std::nullopt;
        const unsigned right = firstRegister + --depth;
        const unsigned left = firstRegister + depth - 1;
        const std::uint32_t operands = (right << 16) | (left << 5) | left;
        if (inst.op == OP_CONG) {
            words.push_back(0x2b000000u | operands); // ADDS
        } else if (inst.op == OP_TRU) {
            words.push_back(0x6b000000u | operands); // SUBS
        } else if (inst.op == OP_NHAN) {
            words.push_back(0x9b207c00u | operands); // SMULL
            words.push_back(0x93407c00u | (left << 5) | 16u);
            words.push_back(0xeb00001fu | (16u << 16) | (left << 5));
        } else if (comparison) {
            emitIntegerComparison(words, left, right, inst.op);
        } else {
            emitIntegerLogic(words, left, right, inst.op);
        }
        if (!comparison && !logic) {
            overflowBranches.push_back(words.size());
            words.push_back(0x54000000u |
                            (inst.op == OP_NHAN ? 0x1u : 0x6u));
        }
    }
    if (!returned) return std::nullopt;
    for (; pos < bytecode.size(); ++pos) {
        if (pos != bytecode.size() - 1 ||
            bytecode[pos].op != OP_DONG_KHOI) return std::nullopt;
    }
    const std::size_t overflowPc = words.size();
    words.push_back(0xd2c00020u); // MOVZ X0,#1,LSL#32
    const std::size_t epiloguePc = words.size();
    words.push_back(stackPair(false, 29, 30));
    words.push_back(stackPair(false, 19, 20));
    words.push_back(0xd65f03c0u); // RET
    words[returnBranch] |= static_cast<std::uint32_t>(epiloguePc - returnBranch);
    for (const std::size_t branch : overflowBranches) {
        const std::size_t distance = overflowPc - branch;
        if (distance >= (1u << 18)) return std::nullopt;
        words[branch] |= static_cast<std::uint32_t>(distance) << 5;
    }
    return candidate;
}

} // namespace
#endif

MachineExpression::MachineExpression(void *code, std::size_t allocatedBytes,
                                     std::size_t instructionCount) noexcept
    : code_(code), allocatedBytes_(allocatedBytes),
      instructionCount_(instructionCount) {}

bool MachineExpression::supported() noexcept {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

MachineExpression::~MachineExpression() {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    if (code_) munmap(code_, allocatedBytes_);
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompile(
    const std::vector<Instruction> &bytecode, std::size_t start,
    const std::vector<bool> &entryPoints) {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    if (entryPoints.size() != bytecode.size()) {
        throw std::invalid_argument("machine JIT: invalid control-flow entry map");
    }
    // All registers are caller-saved: w9..w15. The generated function is
    // int32_t fn(void), so it never needs a prologue, native stack, or GC roots.
    constexpr unsigned firstRegister = 9;
    constexpr unsigned registerCount = 7;
    constexpr std::size_t maxInstructions = 96;
    std::vector<std::uint32_t> words;
    std::vector<std::int64_t> constantStack;
    unsigned depth = 0;
    bool didOperation = false;
    std::size_t lastComplete = start;
    std::size_t lastWordCount = 0;
    for (std::size_t pos = start;
         pos < bytecode.size() && pos - start < maxInstructions; ++pos) {
        // A branch/handler may enter an expression partway through. In that
        // case the original VM stack operations must remain independently
        // executable at the entry point.
        if (pos > start && entryPoints[pos]) break;
        const Instruction &inst = bytecode[pos];
        if (inst.op == OP_BIEN_SO || inst.op == OP_DUNG_GIA_TRI ||
            inst.op == OP_SAI_GIA_TRI) {
            if (depth == registerCount) break;
            const unsigned dst = firstRegister + depth++;
            const std::int32_t signedValue =
                inst.op == OP_BIEN_SO ? inst.operand : inst.op == OP_DUNG_GIA_TRI ? 1 : 0;
            const std::uint32_t value = static_cast<std::uint32_t>(signedValue);
            constantStack.push_back(signedValue);
            // MOVZ Wd, low16; MOVK Wd, high16, LSL #16.
            words.push_back(0x52800000u | ((value & 0xffffu) << 5) | dst);
            words.push_back(0x72a00000u | ((value >> 16) << 5) | dst);
        } else if (inst.op == OP_CONG || inst.op == OP_TRU ||
                   inst.op == OP_NHAN || csetInverseCondition(inst.op) ||
                   isIntegerLogic(inst.op)) {
            if (depth < 2) break;
            const std::int64_t b = constantStack.back();
            const std::int64_t a = constantStack[constantStack.size() - 2];
            const bool comparison = csetInverseCondition(inst.op).has_value();
            const bool logic = isIntegerLogic(inst.op);
            const std::int64_t calculated = comparison
                ? static_cast<std::int64_t>(evaluateIntComparison(inst.op, a, b))
                : logic ? static_cast<std::int64_t>(
                    inst.op == OP_Logic_VA ? (a != 0 && b != 0) :
                                             (a != 0 || b != 0))
                : inst.op == OP_CONG ? a + b :
                  inst.op == OP_TRU ? a - b : a * b;
            // Avoid taking ownership of expressions for which the VM's signed
            // int32 arithmetic is not defined. Let the regular runtime decide.
            if (calculated < std::numeric_limits<std::int32_t>::min() ||
                calculated > std::numeric_limits<std::int32_t>::max()) break;
            constantStack.pop_back();
            constantStack.back() = calculated;
            const unsigned rhs = firstRegister + --depth;
            const unsigned lhs = firstRegister + depth - 1;
            const std::uint32_t operands = (rhs << 16) | (lhs << 5) | lhs;
            if (inst.op == OP_CONG) words.push_back(0x0b000000u | operands);
            if (inst.op == OP_TRU) words.push_back(0x4b000000u | operands);
            if (inst.op == OP_NHAN) words.push_back(0x1b007c00u | operands);
            if (comparison) emitIntegerComparison(words, lhs, rhs, inst.op);
            if (logic) emitIntegerLogic(words, lhs, rhs, inst.op);
            didOperation = true;
        } else {
            break;
        }
        if (depth == 1 && didOperation) {
            lastComplete = pos + 1;
            lastWordCount = words.size();
        }
    }

    if (lastComplete == start) return nullptr;
    words.resize(lastWordCount);
    // MOV W0, W9; RET. Code has no calls, branch targets, or relocations.
    words.push_back(0x2a0003e0u | (firstRegister << 16));
    words.push_back(0xd65f03c0u);

    return fromWords(words, lastComplete - start);
#else
    (void)bytecode;
    (void)start;
    (void)entryPoints;
    return nullptr;
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileBinary(int opcode) {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    std::vector<std::uint32_t> words;
    switch (opcode) {
        case OP_CONG: words.push_back(0x0b010000u); break; // ADD W0,W0,W1
        case OP_TRU: words.push_back(0x4b010000u); break;  // SUB W0,W0,W1
        case OP_NHAN: words.push_back(0x1b017c00u); break; // MUL W0,W0,W1
        case OP_Logic_VA:
        case OP_Logic_HOAC:
            emitIntegerLogic(words, 0, 1, opcode);
            break;
        default:
            if (!csetInverseCondition(opcode)) return nullptr;
            emitIntegerComparison(words, 0, 1, opcode);
            break;
    }
    words.push_back(0xd65f03c0u); // RET
    return fromWords(words, 1);
#else
    (void)opcode;
    return nullptr;
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileConditionalBranch() {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    // size_t branch(int condition, size_t falsePc, size_t truePc)
    // CBZ W0, false; MOV X0, X2; RET; false: MOV X0, X1; RET.
    // The native branch computes the next VM PC; all stack and type checks
    // stay in the VM before invoking this function.
    return fromWords({0x34000060u, 0xaa0203e0u, 0xd65f03c0u,
                      0xaa0103e0u, 0xd65f03c0u}, 1);
#else
    return nullptr;
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileLeaf(
    const std::vector<Instruction> &bytecode, int argc) {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    if (argc < 0 || argc > 2 || bytecode.empty() || bytecode.size() > 96) {
        return nullptr;
    }
    std::array<int, 2> paramSlots = {-1, -1};
    std::size_t pos = 0;
    if (bytecode[pos].op == OP_MO_KHOI) ++pos;
    // The compiler emits a local initialization immediately before each
    // binding. Accept it only when the binding overwrites that exact slot.
    for (int index = 0; index < argc; ++index) {
        if (pos + 1 < bytecode.size() &&
            bytecode[pos].op == OP_KHOI_TAO &&
            bytecode[pos + 1].op == OP_PARAM &&
            bytecode[pos].operandIndex == bytecode[pos + 1].operandIndex) {
            ++pos;
        }
        if (pos >= bytecode.size() || bytecode[pos].op != OP_PARAM ||
            bytecode[pos].operandValue != index ||
            bytecode[pos].operandIndex < 0) {
            return nullptr;
        }
        const int slot = bytecode[pos++].operandIndex;
        if (index != 0 && slot == paramSlots[0]) return nullptr;
        paramSlots[static_cast<std::size_t>(index)] = slot;
    }

    constexpr unsigned firstRegister = 9;
    constexpr unsigned registerCount = 7;
    std::vector<std::uint32_t> words;
    std::vector<std::size_t> overflowBranches;
    unsigned depth = 0;
    bool hasReturn = false;
    for (; pos < bytecode.size(); ++pos) {
        const Instruction &inst = bytecode[pos];
        if (inst.op == OP_TRA_VE) {
            if (depth != 1) return nullptr;
            // The C ABI returns int64, with a signed int32 success value.
            // A result outside int32 signals a checked overflow.
            words.push_back(0x93407c00u |
                            ((firstRegister + depth - 1) << 5));
            words.push_back(0xd65f03c0u);
            hasReturn = true;
            ++pos;
            break;
        }
        if (inst.op == OP_BIEN_SO || inst.op == OP_DUNG_GIA_TRI ||
            inst.op == OP_SAI_GIA_TRI || inst.op == OP_TEN_BIEN_GIA_TRI) {
            if (depth == registerCount) return nullptr;
            const unsigned dst = firstRegister + depth++;
            if (inst.op == OP_TEN_BIEN_GIA_TRI) {
                int source = -1;
                for (int index = 0; index < argc; ++index) {
                    if (paramSlots[static_cast<std::size_t>(index)] ==
                        inst.operandIndex) {
                        source = index;
                        break;
                    }
                }
                if (source < 0) return nullptr;
                // MOV Wdst, Wsource; incoming w0/w1 are kept intact.
                words.push_back(0x2a0003e0u |
                                (static_cast<unsigned>(source) << 16) | dst);
            } else {
                const std::uint32_t value = static_cast<std::uint32_t>(
                    inst.op == OP_BIEN_SO ? inst.operand :
                    inst.op == OP_DUNG_GIA_TRI ? 1 : 0);
                words.push_back(0x52800000u | ((value & 0xffffu) << 5) | dst);
                words.push_back(0x72a00000u | ((value >> 16) << 5) | dst);
            }
            continue;
        }
        if (inst.op == OP_KHONG || inst.op == OP_PHU_DINH) {
            if (depth < 1) return nullptr;
            const unsigned dst = firstRegister + depth - 1;
            // CMP Wdst,#0; CSET Wdst,EQ. For int32 parameters both V++
            // logical-negation opcodes yield 1 for zero and 0 otherwise.
            words.push_back(0x7100001fu | (dst << 5));
            words.push_back(0x1a9f17e0u | dst);
            continue;
        }
        const bool comparison = csetInverseCondition(inst.op).has_value();
        const bool logic = isIntegerLogic(inst.op);
        if (inst.op != OP_CONG && inst.op != OP_TRU &&
            inst.op != OP_NHAN && !comparison && !logic) return nullptr;
        if (depth < 2) return nullptr;
        const unsigned right = firstRegister + --depth;
        const unsigned left = firstRegister + depth - 1;
        const std::uint32_t operands = (right << 16) | (left << 5) | left;
        if (inst.op == OP_CONG) {
            words.push_back(0x2b000000u | operands); // ADDS Wleft,Wleft,Wright
        } else if (inst.op == OP_TRU) {
            words.push_back(0x6b000000u | operands); // SUBS Wleft,Wleft,Wright
        } else if (inst.op == OP_NHAN) {
            words.push_back(0x9b207c00u | operands); // SMULL Xleft,Wleft,Wright
            // Compare the full product with its sign-extended low 32 bits.
            words.push_back(0x93407c00u | (left << 5) | 16u); // SXTW X16,Wleft
            words.push_back(0xeb00001fu | (16u << 16) | (left << 5));
        } else {
            if (comparison) emitIntegerComparison(words, left, right, inst.op);
            else emitIntegerLogic(words, left, right, inst.op);
        }
        if (!comparison && !logic) {
            // B.VS for add/sub; B.NE for the 64-bit multiply check.
            overflowBranches.push_back(words.size());
            words.push_back(0x54000000u |
                            (inst.op == OP_NHAN ? 0x1u : 0x6u));
        }
    }
    if (!hasReturn) return nullptr;
    // Only the automatically emitted closing block may follow a return.
    for (; pos < bytecode.size(); ++pos) {
        if (pos != bytecode.size() - 1 ||
            bytecode[pos].op != OP_DONG_KHOI) return nullptr;
    }
    const std::size_t overflowPc = words.size();
    // MOVZ X0,#1,LSL#32 is the non-int32 sentinel 0x100000000.
    words.push_back(0xd2c00020u);
    words.push_back(0xd65f03c0u);
    for (const std::size_t branch : overflowBranches) {
        const std::size_t distance = overflowPc - branch;
        words[branch] |= static_cast<std::uint32_t>(distance) << 5;
    }
    return fromWords(words, bytecode.size());
#else
    (void)bytecode;
    (void)argc;
    return nullptr;
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::tryCompileCallBundle(
    const std::unordered_map<int, std::vector<Instruction>> &functions) {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    std::unordered_map<int, CallCandidate> candidates;
    for (const auto &[id, code] : functions) {
        for (int argc = 0; argc <= 2; ++argc) {
            auto candidate = compileCallCandidate(code, argc);
            if (candidate) {
                candidates.emplace(id, std::move(*candidate));
                break;
            }
        }
    }

    std::unordered_map<int, unsigned> states;
    std::unordered_map<int, CallEntry> entries;
    // Cyclic calls and graphs deeper than 32 native frames fall back to the
    // VM. The VM's configurable call-depth guard is checked at invocation.
    std::function<bool(int)> validate = [&](int id) -> bool {
        const unsigned state = states[id];
        if (state == 1 || state == 3) return false;
        if (state == 2) return true;
        const auto it = candidates.find(id);
        if (it == candidates.end()) return false;
        states[id] = 1;
        std::size_t depth = 1;
        std::size_t instructions = functions.at(id).size();
        for (const CallPatch &call : it->second.patches) {
            const auto callee = candidates.find(call.callee);
            if (callee == candidates.end() || callee->second.argc != call.argc ||
                !validate(call.callee)) {
                states[id] = 3;
                return false;
            }
            depth = std::max(depth, 1 + entries.at(call.callee).depth);
            if (depth > 32 || instructions > 1000000 - entries.at(call.callee).instructionCount) {
                states[id] = 3;
                return false;
            }
            instructions += entries.at(call.callee).instructionCount;
        }
        entries[id] = {0, depth, instructions, !it->second.patches.empty()};
        states[id] = 2;
        return true;
    };

    std::vector<int> roots;
    for (const auto &[id, candidate] : candidates) {
        if (!candidate.patches.empty() && validate(id)) roots.push_back(id);
    }
    if (roots.empty()) return nullptr;

    std::vector<int> order;
    std::unordered_map<int, bool> included;
    std::function<void(int)> include = [&](int id) {
        if (included[id]) return;
        included[id] = true;
        order.push_back(id);
        for (const CallPatch &patch : candidates.at(id).patches) include(patch.callee);
    };
    std::sort(roots.begin(), roots.end());
    for (int id : roots) include(id);

    std::vector<std::uint32_t> words;
    for (int id : order) {
        entries.at(id).wordOffset = words.size();
        const auto &body = candidates.at(id).words;
        words.insert(words.end(), body.begin(), body.end());
    }
    // BL has a signed, 26-bit word offset. Patch only after every function
    // entry is fixed in the same executable allocation.
    for (int id : order) {
        const auto &entry = entries.at(id);
        for (const CallPatch &patch : candidates.at(id).patches) {
            const std::int64_t from = static_cast<std::int64_t>(entry.wordOffset + patch.wordIndex);
            const std::int64_t to = static_cast<std::int64_t>(entries.at(patch.callee).wordOffset);
            const std::int64_t displacement = to - from;
            if (displacement < -(1ll << 25) || displacement >= (1ll << 25)) return nullptr;
            words[static_cast<std::size_t>(from)] = 0x94000000u |
                (static_cast<std::uint32_t>(displacement) & 0x03ffffffu);
        }
    }
    auto bundle = fromWords(words, 0);
    for (int id : order) bundle->callEntries_.emplace(id, entries.at(id));
    return bundle;
#else
    (void)functions;
    return nullptr;
#endif
}

bool MachineExpression::hasNativeCalls(int functionId) const noexcept {
    const auto it = callEntries_.find(functionId);
    return it != callEntries_.end() && it->second.hasCalls;
}

std::size_t MachineExpression::nativeCallDepth(int functionId) const noexcept {
    const auto it = callEntries_.find(functionId);
    return it == callEntries_.end() ? 0 : it->second.depth;
}

std::size_t MachineExpression::nativeCallInstructionCount(int functionId) const noexcept {
    const auto it = callEntries_.find(functionId);
    return it == callEntries_.end() ? 0 : it->second.instructionCount;
}

std::optional<int> MachineExpression::executeNativeCall(
    int functionId, int arg0, int arg1) const {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    const auto it = callEntries_.find(functionId);
    if (it == callEntries_.end()) return std::nullopt;
    using Entry = std::int64_t (*)(int, int);
    auto *entry = static_cast<char *>(code_) + it->second.wordOffset * sizeof(std::uint32_t);
    const std::int64_t result = reinterpret_cast<Entry>(entry)(arg0, arg1);
    if (result < std::numeric_limits<std::int32_t>::min() ||
        result > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    return static_cast<int>(result);
#else
    (void)functionId;
    (void)arg0;
    (void)arg1;
    throw std::runtime_error("machine JIT requires supported ARM64 or x86-64 host");
#endif
}

std::unique_ptr<MachineExpression> MachineExpression::fromWords(
    const std::vector<std::uint32_t> &words, std::size_t instructionCount) {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    const std::size_t byteCount = words.size() * sizeof(std::uint32_t);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) throw std::runtime_error("machine JIT: invalid page size");
    const std::size_t allocatedBytes =
        ((byteCount + static_cast<std::size_t>(pageSize) - 1) /
         static_cast<std::size_t>(pageSize)) * static_cast<std::size_t>(pageSize);
#if defined(__APPLE__)
    void *memory = mmap(nullptr, allocatedBytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
#else
    void *memory = mmap(nullptr, allocatedBytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    if (memory == MAP_FAILED) {
        throw std::runtime_error("machine JIT: mmap failed: errno=" +
                                 std::to_string(errno));
    }
    // Hardened macOS requires changing JIT page write protection per thread.
#if defined(__APPLE__)
    pthread_jit_write_protect_np(0);
#endif
    std::memcpy(memory, words.data(), byteCount);
    __builtin___clear_cache(static_cast<char *>(memory),
                            static_cast<char *>(memory) + byteCount);
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
#else
    if (mprotect(memory, allocatedBytes, PROT_READ | PROT_EXEC) != 0) {
        const int error = errno;
        munmap(memory, allocatedBytes);
        throw std::runtime_error("machine JIT: mprotect RX failed: errno=" +
                                 std::to_string(error));
    }
#endif

    return std::unique_ptr<MachineExpression>(
        new MachineExpression(memory, allocatedBytes, instructionCount));
#else
    (void)words;
    (void)instructionCount;
    return nullptr;
#endif
}

int MachineExpression::execute() const {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    using Entry = int (*)();
    return reinterpret_cast<Entry>(code_)();
#else
    throw std::runtime_error("machine JIT requires supported ARM64 or x86-64 host");
#endif
}

int MachineExpression::execute(int left, int right) const {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    using Entry = int (*)(int, int);
    return reinterpret_cast<Entry>(code_)(left, right);
#else
    (void)left;
    (void)right;
    throw std::runtime_error("machine JIT requires supported ARM64 or x86-64 host");
#endif
}

std::size_t MachineExpression::executeBranch(int condition, std::size_t falsePc,
                                             std::size_t truePc) const {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    using Entry = std::size_t (*)(int, std::size_t, std::size_t);
    return reinterpret_cast<Entry>(code_)(condition, falsePc, truePc);
#else
    (void)condition;
    (void)falsePc;
    (void)truePc;
    throw std::runtime_error("machine JIT requires supported ARM64 or x86-64 host");
#endif
}

std::optional<int> MachineExpression::executeLeaf(int arg0, int arg1) const {
#if (defined(__APPLE__) || defined(__linux__)) && defined(__aarch64__)
    using Entry = std::int64_t (*)(int, int);
    const std::int64_t value = reinterpret_cast<Entry>(code_)(arg0, arg1);
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(value);
#else
    (void)arg0;
    (void)arg1;
    throw std::runtime_error("machine JIT requires supported ARM64 or x86-64 host");
#endif
}

} // namespace vietvm::native
#endif // Linux/Windows x86-64 backend is defined in x64_jit.cpp
