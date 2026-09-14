#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vpp/compiler/semantic.h"

namespace vietvm::compiler {

// The first IR is deliberately untyped. It provides a stable compiler-stage
// boundary while preserving the dynamic VM contract. Unsupported direct-IR
// regions are diagnostics, independent of any future type-policy ADR.
enum class IrOpcode {
    NoOp,
    Block,
    Import,
    DefineFunction,
    DefineClass,
    Conditional,
    Loop,
    Switch,
    Return,
    Print,
    Break,
    Continue,
    Throw,
    Try,
    Statement,
};

using IrValueId = std::size_t;
inline constexpr IrValueId kInvalidIrValueId = static_cast<IrValueId>(-1);
using IrLambdaId = std::size_t;
inline constexpr IrLambdaId kInvalidIrLambdaId = static_cast<IrLambdaId>(-1);

// Keep the originating expression-arena identity on every IR value. Semantic
// resolution and lowering exchange bindings through this stable identity;
// source spans are diagnostic data and are not unique expression identities.
using AstExprId = vietvm::frontend::ExprId;
inline constexpr AstExprId kInvalidAstExprId = vietvm::frontend::kInvalidExprId;

// Stack-oriented value operations. List/map literals are first-class values:
// they may appear in call arguments and recursively contain other collection
// literals. Structured operations coexist with an explicit marker for syntax
// that the direct emitter cannot compile yet.
enum class IrValueOpcode {
    UnsupportedDirectRegion,
    ConstInt,
    ConstFloat,
    ConstString,
    ConstBool,
    ConstNull,
    MapLiteral,
    ListLiteral,
    Index,
    StoreIndex,
    LoadName,
    LoadProperty,
    StoreName,
    StoreProperty,
    Unary,
    Binary,
    Call,
    CallDynamic,
    Lambda,
};

// Biểu diễn một giá trị trong untyped IR; record lưu opcode, source ExprId, symbol, text và operand ids để codegen dựng giá trị theo đồ thị phụ thuộc.
struct IrValue {
    IrValueId id = kInvalidIrValueId;
    IrValueOpcode opcode = IrValueOpcode::UnsupportedDirectRegion;
    vietvm::frontend::SourceSpan span{};
    AstExprId sourceExprId = kInvalidAstExprId;

    // Optional semantic binding.  It remains -1 until semantic analysis can
    // bind by sourceExprId rather than guessing from a source span.
    int symbolId = -1;

    // Literal spelling, name, or operator depending on opcode.
    std::string text;

    // `gọi tên(...)` has a distinct legacy forward-call contract from the
    // ordinary `tên(...)` form, so lowering preserves that source-level mode.
    bool explicitCall = false;
    CallTargetKind callTarget = CallTargetKind::Invalid;
    IrLambdaId lambdaId = kInvalidIrLambdaId;

    // Evaluation order is source order.  Calls store the callee first and then
    // their arguments.  Stores keep the target expression before the value.
    std::vector<IrValueId> operands;
};

// Lưu metadata một tham số IR gồm tên, symbol id và default-value id; emitter dùng record này để phát binding `OP_PARAM`/default.
struct IrParameter {
    std::string name;
    vietvm::frontend::SourceSpan span{};
    int symbolId = -1;
    bool hasDefault = false;
    IrValueId defaultValue = kInvalidIrValueId;
};

// Biểu diễn một nhánh của IR `chọn`, giữ loại ca/mặc định, label value và child block để codegen tạo control flow.
struct IrSwitchArm {
    vietvm::frontend::AstSwitchArmKind kind =
        vietvm::frontend::AstSwitchArmKind::Case;
    vietvm::frontend::SourceSpan span{};
    vietvm::frontend::SourceSpan labelSpan{};
    IrValueId label = kInvalidIrValueId;
    std::size_t bodyChildIndex = 0;
    bool hasColon = false;
    bool prefixedByCase = false;
};

// Biểu diễn một câu lệnh IR có cấu trúc; record giữ opcode, metadata semantic, expression roots, token nguồn và các instruction con cho block/control flow.
struct IrInstruction {
    IrOpcode opcode = IrOpcode::Statement;
    vietvm::frontend::SourceSpan span{};
    int symbolId = -1;

    // Declaration metadata is explicit IR data. The direct backend must not
    // recover function names or parameter defaults by reparsing source tokens.
    std::string declarationName;
    vietvm::frontend::AstVisibility visibility =
        vietvm::frontend::AstVisibility::Unspecified;
    SemanticVisibility effectiveVisibility = SemanticVisibility::Unspecified;
    vietvm::frontend::AstImportForm importForm =
        vietvm::frontend::AstImportForm::Unstructured;
    vietvm::frontend::AstImportSpec importSpec;
    vietvm::frontend::AstClassForm classForm =
        vietvm::frontend::AstClassForm::Unstructured;
    std::string superclassName;
    int superclassSymbolId = -1;
    vietvm::frontend::AstConditionalForm conditionalForm =
        vietvm::frontend::AstConditionalForm::Unstructured;
    vietvm::frontend::AstLoopForm loopForm =
        vietvm::frontend::AstLoopForm::Unstructured;
    vietvm::frontend::AstSwitchForm switchForm =
        vietvm::frontend::AstSwitchForm::Unstructured;
    vietvm::frontend::AstTryForm tryForm =
        vietvm::frontend::AstTryForm::Unstructured;
    std::string catchVariable;
    vietvm::frontend::SourceSpan catchVariableSpan{};
    int catchSymbolId = -1;
    // Source-level receiver names actually referenced by this method. `mình`
    // is the current instance; `gốc` carries the same instance but method
    // calls dispatch from the defining class's superclass.
    std::vector<std::string> implicitReceiverNames;
    std::vector<IrParameter> parameters;
    std::vector<IrSwitchArm> switchArms;
    std::vector<IrValueId> expressionRoots;
    std::vector<IrInstruction> children;

    // True when this statement still needs the compatibility backend even if
    // some nested expressions or child statements have structured IR.
    bool unsupportedDirectRegion = false;

    // Lossless compatibility payload.  Only top-level instructions own this
    // slice; recursive children are represented structurally and deliberately
    // do not duplicate their parent's source tokens.
    std::vector<vietvm::frontend::Token> tokens;
};

// Lưu lambda đã lowering gồm owner value, source ExprId, tham số, capture và body IR để emitter tạo function ẩn tương ứng.
struct IrLambda {
    IrLambdaId id = kInvalidIrLambdaId;
    IrValueId ownerValue = kInvalidIrValueId;
    AstExprId sourceExprId = kInvalidAstExprId;
    vietvm::frontend::SourceSpan span{};
    std::vector<IrParameter> parameters;
    IrInstruction body;
    std::vector<int> captures;
    std::vector<std::string> captureNames;
};

// Là container gốc của IR sau lowering, sở hữu các instruction top-level, arena giá trị/lambda và bộ đếm vùng direct IR chưa hỗ trợ.
struct IrProgram {
    std::vector<IrValue> values;
    std::vector<IrLambda> lambdas;
    std::vector<IrInstruction> instructions;
    std::size_t unsupportedDirectRegionCount = 0;

    // Trả `IrValue` theo id; accessor kiểm tra id hợp lệ rồi trả con trỏ tới arena `values` mà không sao chép giá trị.
    const IrValue *value(IrValueId id) const noexcept {
        return id < values.size() ? &values[id] : nullptr;
    }

    // Trả `lambda` hiện tại từ trạng thái nội bộ; accessor chỉ đọc dữ liệu để caller/test kiểm tra mà không làm thay đổi đối tượng.
    const IrLambda *lambda(IrLambdaId id) const noexcept {
        return id < lambdas.size() ? &lambdas[id] : nullptr;
    }
};

// Chuyển AST đã có semantic model thành `IrProgram`; quá trình lowering giữ liên kết symbol và đánh dấu vùng direct IR chưa hỗ trợ.
IrProgram lowerToIr(const vietvm::frontend::AstProgram &program,
                    const SemanticModel &semantic);

// Ghép lại token nguồn được lưu trong các lệnh IR theo thứ tự, phục vụ kiểm tra parity và debug quá trình lowering.
std::vector<std::string> materializeIrTokens(const IrProgram &program);

// Tính lại số vùng IR chưa hỗ trợ sau khi IR bị biến đổi, tránh dùng bộ đếm cũ không còn đúng.
std::size_t recomputeUnsupportedDirectRegionCount(IrProgram &program);

// Trả tên ổn định của opcode câu lệnh IR bằng `switch`, chủ yếu dùng cho debug và tooling nội bộ.
const char *irOpcodeName(IrOpcode opcode) noexcept;
// Trả tên ổn định của opcode giá trị IR bằng `switch`, phục vụ dump và kiểm thử compiler.
const char *irValueOpcodeName(IrValueOpcode opcode) noexcept;

} // namespace vietvm::compiler
