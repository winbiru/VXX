#include "vpp/compiler/optimizer.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vietvm::compiler {
namespace {

// Inline only small, side-effect-free direct calls. Doing this before either
// native lowerer runs keeps the C++ oracle and the V++ self-hosted backend in
// agreement, including their call relocation/ABI tests.
bool pureScalarTree(const IrProgram &ir, IrValueId id,
                    const std::unordered_set<int> *parameters,
                    std::size_t &budget, std::size_t depth = 0) {
    if (depth >= 32 || budget == 0) return false;
    --budget;
    const auto *v = ir.value(id);
    if (!v) return false;
    using V = IrValueOpcode;
    if (v->opcode == V::ConstInt || v->opcode == V::ConstFloat ||
        v->opcode == V::ConstBool)
        return v->operands.empty();
    if (v->opcode == V::LoadName)
        return v->operands.empty() && v->symbolId >= 0 &&
            (!parameters || parameters->count(v->symbolId) != 0);
    if (v->opcode == V::Unary && v->operands.size() == 1 &&
        (v->text == "+" || v->text == "-" || v->text == "!"))
        return pureScalarTree(ir, v->operands[0], parameters, budget, depth + 1);
    if (v->opcode == V::Binary && v->operands.size() == 2 &&
        (v->text == "+" || v->text == "-" || v->text == "*" ||
         v->text == "==" || v->text == "!=" || v->text == "<" ||
         v->text == "<=" || v->text == ">" || v->text == ">="))
        return pureScalarTree(ir, v->operands[0], parameters, budget, depth + 1) &&
               pureScalarTree(ir, v->operands[1], parameters, budget, depth + 1);
    return false;
}

void inlineScalarCalls(IrProgram &ir, OptimizationReport &report) {
    std::unordered_map<int, const IrInstruction *> functions;
    for (const auto &inst : ir.instructions) {
        if (inst.opcode == IrOpcode::DefineFunction && inst.symbolId >= 0)
            functions.emplace(inst.symbolId, &inst);
    }
    const auto originalCount = ir.values.size();
    for (IrValueId index = 0; index < originalCount; ++index) {
        // Restrict to one or two parameters: ABI/linkage fixtures with four
        // and ten arguments must still exercise a real native CALL.
        const auto call = ir.values[index];
        if (call.opcode != IrValueOpcode::Call || call.explicitCall ||
            call.callTarget != CallTargetKind::DirectFunction ||
            call.operands.empty()) continue;
        const auto found = functions.find(call.symbolId);
        if (found == functions.end()) continue;
        const auto &function = *found->second;
        const auto argc = call.operands.size() - 1;
        if (argc == 0 || argc > 2 || function.parameters.size() != argc ||
            function.unsupportedDirectRegion || !function.expressionRoots.empty() ||
            function.children.size() != 1)
            continue;
        const auto &block = function.children[0];
        if (block.opcode != IrOpcode::Block || block.unsupportedDirectRegion ||
            !block.expressionRoots.empty() || block.children.size() != 1) continue;
        const auto &ret = block.children[0];
        if (ret.opcode != IrOpcode::Return || ret.unsupportedDirectRegion ||
            !ret.children.empty() || ret.expressionRoots.size() != 1) continue;
        const auto *callee = ir.value(call.operands[0]);
        if (!callee || callee->opcode != IrValueOpcode::LoadName ||
            callee->symbolId != call.symbolId) continue;
        std::unordered_set<int> params;
        std::unordered_map<int, IrValueId> bindings;
        bool valid = true;
        for (std::size_t j = 0; j < argc; ++j) {
            const auto &param = function.parameters[j];
            if (param.hasDefault || param.symbolId < 0 ||
                !params.insert(param.symbolId).second) { valid = false; break; }
            bindings.emplace(param.symbolId, call.operands[j + 1]);
        }
        if (!valid) continue;
        std::size_t calleeBudget = 12;
        if (!pureScalarTree(ir, ret.expressionRoots[0], &params, calleeBudget)) continue;
        for (std::size_t j = 1; j < call.operands.size(); ++j) {
            std::size_t argBudget = 32;
            if (!pureScalarTree(ir, call.operands[j], nullptr, argBudget)) {
                valid = false; break;
            }
        }
        if (!valid) continue;
        // The tree is proven acyclic and small above. Clone into the stable
        // ID arena, substituting parameter loads with caller expressions.
        std::function<IrValueId(IrValueId)> clone = [&](IrValueId id) -> IrValueId {
            IrValue node = *ir.value(id);
            if (node.opcode == IrValueOpcode::LoadName)
                return bindings.at(node.symbolId);
            for (auto &operand : node.operands) operand = clone(operand);
            node.id = ir.values.size();
            ir.values.push_back(std::move(node));
            return ir.values.size() - 1;
        };
        const auto clonedRoot = clone(ret.expressionRoots[0]);
        auto replacement = ir.values[clonedRoot];
        replacement.id = index;
        replacement.span = call.span;
        replacement.sourceExprId = call.sourceExprId;
        ir.values[index] = std::move(replacement);
        ++report.inlinedScalarCalls;
    }
}

bool literalI32(const std::string &spelling, std::int32_t &value) {
    std::int64_t parsed = 0;
    const auto [end,ec] = std::from_chars(spelling.data(),
        spelling.data()+spelling.size(),parsed);
    if(ec!=std::errc{} || end!=spelling.data()+spelling.size() ||
       parsed<std::numeric_limits<std::int32_t>::min() ||
       parsed>std::numeric_limits<std::int32_t>::max())return false;
    value=static_cast<std::int32_t>(parsed);
    return true;
}

// Use well-defined unsigned arithmetic and bit reconstruction for the VM's
// wrapping signed-i32 contract (including INT32_MIN * -1).
std::int32_t fromBits(std::uint32_t bits) {
    std::int32_t value;
    static_assert(sizeof(value)==sizeof(bits));
    std::memcpy(&value,&bits,sizeof(value));
    return value;
}

void specializeValues(IrProgram &program, OptimizationReport &report) {
    // Arena IDs are stable, but a value can reference an earlier or later ID.
    // The bounded fixed-point iteration propagates only proven scalar facts.
    for (std::size_t pass=0;pass<program.values.size();++pass) {
        bool changed=false;
        for(auto &v:program.values) {
            using O=IrValueOpcode;
            IrScalarType type=IrScalarType::Dynamic;
            bool folded=false;
            std::int32_t result=0;
            if(v.opcode==O::ConstInt) {
                if(literalI32(v.text,result)) {type=IrScalarType::I32;folded=true;}
            } else if(v.opcode==O::ConstBool) {
                if(v.text=="đúng"||v.text=="sai") {
                    type=IrScalarType::I32;result=(v.text=="đúng");folded=true;
                }
            } else if(v.opcode==O::ConstFloat) type=IrScalarType::F64;
            else if(v.opcode==O::Unary && v.operands.size()==1) {
                const auto *arg=program.value(v.operands[0]);
                if(arg && (v.text=="+"||v.text=="-"||v.text=="!")) {
                    type=v.text=="!"?IrScalarType::I32:arg->scalarType;
                    if(arg->hasFoldedI32) {
                        folded=true;
                        if(v.text=="+") result=arg->foldedI32;
                        else if(v.text=="-") result=fromBits(0u-static_cast<std::uint32_t>(arg->foldedI32));
                        else result=arg->foldedI32==0;
                    } else if(v.text=="-" && arg->opcode==O::ConstInt &&
                              arg->text=="2147483648") {
                        type=IrScalarType::I32;folded=true;
                        result=std::numeric_limits<std::int32_t>::min();
                    }
                }
            } else if(v.opcode==O::Binary && v.operands.size()==2) {
                const auto *a=program.value(v.operands[0]);
                const auto *b=program.value(v.operands[1]);
                const bool arithmetic=v.text=="+"||v.text=="-"||v.text=="*";
                const bool compare=v.text=="=="||v.text=="!="||v.text=="<"||
                    v.text==">"||v.text=="<="||v.text==">=";
                const bool logic=v.text=="&&"||v.text=="||";
                if(a && b && a->scalarType!=IrScalarType::Dynamic &&
                   b->scalarType!=IrScalarType::Dynamic) {
                    if(compare||logic)type=IrScalarType::I32;
                    else if(arithmetic) type=(a->scalarType==IrScalarType::F64||
                        b->scalarType==IrScalarType::F64)?IrScalarType::F64:IrScalarType::I32;
                    if(a->hasFoldedI32 && b->hasFoldedI32 && type==IrScalarType::I32) {
                        folded=true;
                        const auto x=a->foldedI32,y=b->foldedI32;
                        if(v.text=="+") result=fromBits(std::uint32_t(x)+std::uint32_t(y));
                        else if(v.text=="-") result=fromBits(std::uint32_t(x)-std::uint32_t(y));
                        else if(v.text=="*") result=fromBits(std::uint32_t(x)*std::uint32_t(y));
                        else if(v.text=="==") result=x==y;
                        else if(v.text=="!=") result=x!=y;
                        else if(v.text=="<") result=x<y;
                        else if(v.text==">") result=x>y;
                        else if(v.text=="<=") result=x<=y;
                        else if(v.text==">=") result=x>=y;
                        else if(v.text=="&&") result=x!=0&&y!=0;
                        else if(v.text=="||") result=x!=0||y!=0;
                        else folded=false;
                    }
                }
            }
            if(type!=v.scalarType||folded!=v.hasFoldedI32||
               (folded&&result!=v.foldedI32)) {
                v.scalarType=type;v.hasFoldedI32=folded;v.foldedI32=result;
                changed=true;
            }
        }
        if(!changed)break;
    }
    for(const auto &v:program.values) {
        if(v.scalarType!=IrScalarType::Dynamic)++report.specializedScalars;
        if(v.hasFoldedI32)++report.foldedI32;
    }
}

// Loại bỏ no ops; hàm tìm phần tử tương ứng rồi xóa nó khỏi cấu trúc trạng thái hiện tại.
void removeNoOps(std::vector<IrInstruction> &instructions,
                 OptimizationReport &report) {
    for (IrInstruction &instruction : instructions) {
        removeNoOps(instruction.children, report);
    }

    const auto oldEnd = instructions.end();
    const auto newEnd = std::remove_if(
        instructions.begin(), oldEnd,
        [&](const IrInstruction &instruction) {
            if (instruction.opcode != IrOpcode::NoOp) return false;
            ++report.removedNoOps;
            return true;
        });
    instructions.erase(newEnd, oldEnd);
}

} // namespace

// Tối ưu `IrProgram` tại chỗ; hiện optimizer loại no-op và tính lại metadata vùng direct IR để artifact sau tối ưu vẫn nhất quán.
OptimizationReport optimizeIr(IrProgram &program) {
    OptimizationReport report;
    inlineScalarCalls(program, report);
    specializeValues(program,report);
    removeNoOps(program.instructions, report);
    for (IrLambda &lambda : program.lambdas) {
        removeNoOps(lambda.body.children, report);
    }

    // Passes may remove a statement that carried an unsupported-direct marker, so the
    // public count is derived state rather than an incrementally maintained
    // counter.
    recomputeUnsupportedDirectRegionCount(program);
    return report;
}

} // namespace vietvm::compiler
