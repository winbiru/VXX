#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "vm/instruction.h"
#include "vpp/core/message_constants.h"

namespace vietvm::runtime {

// Phân loại nội bộ do bộ chẩn đoán suy ra từ trạng thái thực thi. Nơi phát sinh
// lỗi không truyền giá trị enum này; nó chỉ cung cấp các dữ kiện mà VM quan sát được.
enum class RuntimeDiagnosticKind {
    None,
    DivisionByZero,
    ModuloByZero,
    IntegerRequired,
    NumericOperandRequired,
    ComparisonTypeMismatch,
    FloatConversionFailed,
    IndexTypeInvalid,
    IndexOutOfRange,
    ContainerNotIndexable,
    FunctionNotFound,
    InvalidCallable,
    CallArityMismatch,
    CallDepthExceeded,
    RequiredArgumentMissing,
    ModuleInitializationFailed,
    ClassNotFound,
    ObjectRequired,
    PropertyNotFound,
    MethodNotFound,
    MemberAccessDenied,
    MissingOperand,
    IncrementTypeInvalid,
    DecrementTypeInvalid,
    ConstantReferenceInvalid,
    LiteralDecodeFailed,
    ClosureCaptureInvalid,
    JumpTargetInvalid,
    ControlFlowStateInvalid,
    NativeOperationFailed,
    InternalRuntimeState,
};

// Các dữ kiện thực tế thu được tại thời điểm VM thất bại. Chúng mô tả trạng thái
// hệ thống chứ không gắn sẵn tên lỗi; bộ chẩn đoán dùng chúng để tự nhận diện lỗi.
struct RuntimeDiagnosticContext {
    int opcode = -1;
    std::vector<std::string> operandTexts;
    std::vector<bool> operandNumeric;
    std::string actualType;

    int availableOperands = -1;
    int requiredOperands = -1;
    bool expectsInteger = false;

    bool hasIndex = false;
    bool indexIsInteger = true;
    long long index = 0;
    long long containerSize = -1;
    bool containerIndexable = true;

    std::string targetName;
    bool targetLookupAttempted = false;
    bool targetExists = true;
    int argumentCount = -1;
    int minimumArguments = -1;
    int maximumArguments = -1;
    int callDepth = -1;
    int maximumCallDepth = -1;
    int requiredArgumentIndex = -1;

    std::string moduleName;
    bool moduleInitializationAttempted = false;
    bool moduleInitializationSucceeded = true;

    bool receiverChecked = false;
    bool receiverIsObject = true;
    std::string memberName;
    bool memberLookupAttempted = false;
    bool memberExists = true;
    bool accessChecked = false;
    bool accessAllowed = true;

    bool conversionAttempted = false;
    bool conversionSucceeded = true;
    std::string conversionInput;
    std::string conversionTarget;

    bool constantReferenceChecked = false;
    bool constantReferenceValid = true;
    bool literalDecodeAttempted = false;
    bool literalDecodeSucceeded = true;
    bool closureCaptureChecked = false;
    bool closureCaptureValid = true;
    bool jumpTargetChecked = false;
    bool jumpTargetValid = true;
    long long jumpTarget = -1;
    bool controlStateChecked = false;
    bool controlStateValid = true;

    bool nativeOperationAttempted = false;
    bool nativeOperationSucceeded = true;
    std::string nativeOperation;
    std::string detail;

    bool internalInvariantChecked = false;
    bool internalInvariantValid = true;
};

// Nội dung giải thích dành cho người học sau khi hệ thống đã tự nhận diện loại lỗi.
struct RuntimeDiagnosticInfo {
    std::string category;
    std::string explanation;
    std::string suggestion;
};

// Kiểm tra biểu diễn số 0 trong dữ kiện toán học mà không cần phân tích message lỗi.
inline bool runtimeDiagnosticIsZero(const std::string &value) {
    return value == "0" || value == "0.0" || value == "-0" || value == "-0.0";
}

// Trả dữ kiện ở vị trí yêu cầu và dùng nội dung dự phòng nếu VM chưa thu được giá trị đó.
inline std::string runtimeDiagnosticOperand(const RuntimeDiagnosticContext &context,
                                            std::size_t index,
                                            std::string fallback = "giá trị chưa xác định") {
    return index < context.operandTexts.size() && !context.operandTexts[index].empty()
        ? context.operandTexts[index]
        : std::move(fallback);
}

// Ghi số giá trị VM hiện có và số giá trị thao tác cần; hệ thống dùng chênh lệch
// này để tự nhận ra trường hợp thiếu dữ liệu trên ngăn xếp.
inline RuntimeDiagnosticContext runtimeStackFacts(int available, int required) {
    RuntimeDiagnosticContext context;
    context.availableOperands = available;
    context.requiredOperands = required;
    return context;
}

// Ghi kết quả tra tên hàm hoặc lớp; tên đối tượng được tra là dữ kiện, còn việc
// phân biệt "không tìm thấy hàm" hay "không tìm thấy lớp" dựa vào opcode sau đó.
inline RuntimeDiagnosticContext runtimeTargetLookupFacts(std::string name, bool exists) {
    RuntimeDiagnosticContext context;
    context.targetName = std::move(name);
    context.targetLookupAttempted = true;
    context.targetExists = exists;
    return context;
}

// Ghi số đối số thực tế và giới hạn của callable; bộ chẩn đoán tự xác định khi
// nào các con số này tạo thành một lời gọi không hợp lệ.
inline RuntimeDiagnosticContext runtimeCallFacts(std::string name,
                                                 int actual,
                                                 int minimum,
                                                 int maximum) {
    RuntimeDiagnosticContext context;
    context.targetName = std::move(name);
    context.argumentCount = actual;
    context.minimumArguments = minimum;
    context.maximumArguments = maximum;
    return context;
}

// Ghi độ sâu lời gọi vừa quan sát và giới hạn cấu hình để bộ chẩn đoán nhận biết
// vòng gọi quá sâu mà không cần caller gắn tên lỗi đệ quy.
inline RuntimeDiagnosticContext runtimeCallDepthFacts(std::string name,
                                                      int depth,
                                                      int maximum) {
    RuntimeDiagnosticContext context;
    context.targetName = std::move(name);
    context.callDepth = depth;
    context.maximumCallDepth = maximum;
    return context;
}

// Ghi vị trí tham số bắt buộc mà frame không tìm thấy giá trị tương ứng.
inline RuntimeDiagnosticContext runtimeRequiredArgumentFacts(int index) {
    RuntimeDiagnosticContext context;
    context.requiredArgumentIndex = index;
    return context;
}

// Ghi kết quả khởi tạo mô đun; formatter chỉ nhận dữ kiện thành công/thất bại
// và không yêu cầu nơi gọi chọn trước một loại diagnostic.
inline RuntimeDiagnosticContext runtimeModuleFacts(std::string name, bool succeeded) {
    RuntimeDiagnosticContext context;
    context.moduleName = std::move(name);
    context.moduleInitializationAttempted = true;
    context.moduleInitializationSucceeded = succeeded;
    return context;
}

// Ghi trạng thái receiver/member/quyền truy cập của một thao tác OOP; opcode sẽ
// quyết định hệ thống đang đọc thuộc tính, gọi phương thức hay tạo đối tượng.
inline RuntimeDiagnosticContext runtimeMemberFacts(std::string member,
                                                   bool receiverIsObject,
                                                   bool lookupAttempted,
                                                   bool memberExists,
                                                   bool accessChecked = false,
                                                   bool accessAllowed = true) {
    RuntimeDiagnosticContext context;
    context.receiverChecked = true;
    context.receiverIsObject = receiverIsObject;
    context.memberName = std::move(member);
    context.memberLookupAttempted = lookupAttempted;
    context.memberExists = memberExists;
    context.accessChecked = accessChecked;
    context.accessAllowed = accessAllowed;
    return context;
}

// Ghi dữ liệu của phép chuyển kiểu đã thất bại để bộ chẩn đoán tự tạo lời giải
// thích chứa chính giá trị mà người dùng đã đưa vào.
inline RuntimeDiagnosticContext runtimeConversionFacts(std::string input,
                                                       std::string target,
                                                       bool succeeded) {
    RuntimeDiagnosticContext context;
    context.conversionAttempted = true;
    context.conversionSucceeded = succeeded;
    context.conversionInput = std::move(input);
    context.conversionTarget = std::move(target);
    return context;
}

// Ghi chỉ số, kích thước và khả năng đánh chỉ số của dữ liệu; từ các quan sát
// này hệ thống phân biệt sai kiểu chỉ số, vượt biên và dùng [] sai loại dữ liệu.
inline RuntimeDiagnosticContext runtimeIndexFacts(long long index,
                                                  bool indexIsInteger,
                                                  long long containerSize,
                                                  bool containerIndexable,
                                                  std::string actualType = {}) {
    RuntimeDiagnosticContext context;
    context.hasIndex = true;
    context.index = index;
    context.indexIsInteger = indexIsInteger;
    context.containerSize = containerSize;
    context.containerIndexable = containerIndexable;
    context.actualType = std::move(actualType);
    return context;
}

// Ghi thất bại của lời gọi hàm native cùng tên thao tác và chi tiết do lớp dưới
// trả về; hệ thống dùng dữ kiện này để giải thích lỗi tệp, mạng hoặc nền tảng.
inline RuntimeDiagnosticContext runtimeNativeFacts(std::string operation,
                                                   std::string detail,
                                                   bool succeeded) {
    RuntimeDiagnosticContext context;
    context.nativeOperationAttempted = true;
    context.nativeOperationSucceeded = succeeded;
    context.nativeOperation = std::move(operation);
    context.detail = std::move(detail);
    return context;
}

// Ghi kết quả kiểm tra tham chiếu vào bảng hằng của bytecode.
inline RuntimeDiagnosticContext runtimeConstantReferenceFacts(bool valid) {
    RuntimeDiagnosticContext context;
    context.constantReferenceChecked = true;
    context.constantReferenceValid = valid;
    return context;
}

// Ghi kết quả giải mã literal cùng chi tiết kỹ thuật để lời giải thích có thể
// phân biệt dữ liệu nguồn của chương trình bị hỏng với lỗi người dùng thông thường.
inline RuntimeDiagnosticContext runtimeLiteralDecodeFacts(std::string detail,
                                                          bool succeeded) {
    RuntimeDiagnosticContext context;
    context.literalDecodeAttempted = true;
    context.literalDecodeSucceeded = succeeded;
    context.detail = std::move(detail);
    return context;
}

// Ghi kết quả kiểm tra biến capture của hàm đóng.
inline RuntimeDiagnosticContext runtimeClosureFacts(std::string detail, bool valid) {
    RuntimeDiagnosticContext context;
    context.closureCaptureChecked = true;
    context.closureCaptureValid = valid;
    context.detail = std::move(detail);
    return context;
}

// Ghi đích nhảy mà VM vừa kiểm tra để hệ thống tự nhận ra địa chỉ ngoài phạm vi.
inline RuntimeDiagnosticContext runtimeJumpFacts(long long target, bool valid) {
    RuntimeDiagnosticContext context;
    context.jumpTargetChecked = true;
    context.jumpTargetValid = valid;
    context.jumpTarget = target;
    return context;
}

// Ghi trạng thái của cấu trúc điều khiển như chọn, vòng lặp hoặc block; caller
// chỉ cho biết trạng thái hợp lệ hay không và bộ chẩn đoán tự phân loại lỗi.
inline RuntimeDiagnosticContext runtimeControlFacts(std::string detail, bool valid) {
    RuntimeDiagnosticContext context;
    context.controlStateChecked = true;
    context.controlStateValid = valid;
    context.detail = std::move(detail);
    return context;
}

// Ghi kiểu dữ liệu thực tế của một thao tác tăng/giảm hoặc kiểm tra kiểu khác.
inline RuntimeDiagnosticContext runtimeTypeFacts(std::string actualType) {
    RuntimeDiagnosticContext context;
    context.actualType = std::move(actualType);
    return context;
}

// Tự suy ra loại lỗi từ opcode và trạng thái thực tế. Thứ tự kiểm tra ưu tiên
// dữ kiện cụ thể hơn để một lỗi không bị nhận nhầm thành lỗi tổng quát của opcode.
inline RuntimeDiagnosticKind detectRuntimeDiagnostic(
    const RuntimeDiagnosticContext &context) {
    if (context.moduleInitializationAttempted && !context.moduleInitializationSucceeded) {
        return RuntimeDiagnosticKind::ModuleInitializationFailed;
    }
    if (context.maximumCallDepth >= 0 && context.callDepth > context.maximumCallDepth) {
        return RuntimeDiagnosticKind::CallDepthExceeded;
    }
    if (context.requiredArgumentIndex >= 0) {
        return RuntimeDiagnosticKind::RequiredArgumentMissing;
    }
    if (context.argumentCount >= 0 && context.minimumArguments >= 0 &&
        context.maximumArguments >= 0 &&
        (context.argumentCount < context.minimumArguments ||
         context.argumentCount > context.maximumArguments)) {
        return RuntimeDiagnosticKind::CallArityMismatch;
    }
    if (context.nativeOperationAttempted && !context.nativeOperationSucceeded) {
        return RuntimeDiagnosticKind::NativeOperationFailed;
    }
    if (context.constantReferenceChecked && !context.constantReferenceValid) {
        return RuntimeDiagnosticKind::ConstantReferenceInvalid;
    }
    if (context.literalDecodeAttempted && !context.literalDecodeSucceeded) {
        return RuntimeDiagnosticKind::LiteralDecodeFailed;
    }
    if (context.closureCaptureChecked && !context.closureCaptureValid) {
        return RuntimeDiagnosticKind::ClosureCaptureInvalid;
    }
    if (context.jumpTargetChecked && !context.jumpTargetValid) {
        return RuntimeDiagnosticKind::JumpTargetInvalid;
    }
    if (context.controlStateChecked && !context.controlStateValid) {
        return RuntimeDiagnosticKind::ControlFlowStateInvalid;
    }
    if (context.internalInvariantChecked && !context.internalInvariantValid) {
        return RuntimeDiagnosticKind::InternalRuntimeState;
    }
    if (context.availableOperands >= 0 && context.requiredOperands >= 0 &&
        context.availableOperands < context.requiredOperands) {
        return RuntimeDiagnosticKind::MissingOperand;
    }
    if (context.conversionAttempted && !context.conversionSucceeded) {
        return RuntimeDiagnosticKind::FloatConversionFailed;
    }
    if (context.hasIndex) {
        if (!context.indexIsInteger) return RuntimeDiagnosticKind::IndexTypeInvalid;
        if (!context.containerIndexable) return RuntimeDiagnosticKind::ContainerNotIndexable;
        if (context.index < 0 ||
            (context.containerSize >= 0 && context.index >= context.containerSize)) {
            return RuntimeDiagnosticKind::IndexOutOfRange;
        }
    }
    if (context.receiverChecked && !context.receiverIsObject) {
        return RuntimeDiagnosticKind::ObjectRequired;
    }
    if (context.memberLookupAttempted && !context.memberExists) {
        if (context.opcode == OP_DOC_THUOC_TINH || context.opcode == OP_GAN_THUOC_TINH) {
            return RuntimeDiagnosticKind::PropertyNotFound;
        }
        return RuntimeDiagnosticKind::MethodNotFound;
    }
    if (context.accessChecked && !context.accessAllowed) {
        return RuntimeDiagnosticKind::MemberAccessDenied;
    }
    if (context.targetLookupAttempted && !context.targetExists) {
        if (context.opcode == OP_TAO_LOP || context.opcode == OP_THEM_PHUONG_THUC ||
            context.opcode == OP_TAO_DOI_TUONG) {
            return RuntimeDiagnosticKind::ClassNotFound;
        }
        if (context.opcode == OP_GOI_GIAN_TIEP) {
            return RuntimeDiagnosticKind::InvalidCallable;
        }
        return RuntimeDiagnosticKind::FunctionNotFound;
    }
    // Với ++/--, opcode đã cho biết ý định cụ thể hơn yêu cầu số nguyên tổng quát.
    // Ưu tiên chẩn đoán tăng/giảm sai kiểu để người học biết chính xác thao tác nào sai.
    if (context.opcode == OP_CONG_MOT && !context.actualType.empty()) {
        return RuntimeDiagnosticKind::IncrementTypeInvalid;
    }
    if (context.opcode == OP_TRU_MOT && !context.actualType.empty()) {
        return RuntimeDiagnosticKind::DecrementTypeInvalid;
    }
    if (context.expectsInteger && !context.actualType.empty() &&
        context.actualType != "số nguyên" && context.actualType != "số thực") {
        return RuntimeDiagnosticKind::IntegerRequired;
    }

    if ((context.opcode == OP_CHIA || context.opcode == OP_CHIA_GAN) &&
        context.operandTexts.size() >= 2 && runtimeDiagnosticIsZero(context.operandTexts[1])) {
        return RuntimeDiagnosticKind::DivisionByZero;
    }
    if ((context.opcode == OP_MODULO || context.opcode == OP_MODULO_GAN) &&
        context.operandTexts.size() >= 2 && runtimeDiagnosticIsZero(context.operandTexts[1])) {
        return RuntimeDiagnosticKind::ModuloByZero;
    }
    if ((context.opcode == OP_TRU || context.opcode == OP_NHAN ||
         context.opcode == OP_CHIA || context.opcode == OP_TRU_GAN ||
         context.opcode == OP_NHAN_GAN || context.opcode == OP_CHIA_GAN) &&
        context.operandNumeric.size() >= 2 &&
        (!context.operandNumeric[0] || !context.operandNumeric[1])) {
        return RuntimeDiagnosticKind::NumericOperandRequired;
    }
    if ((context.opcode == OP_LON_HON || context.opcode == OP_NHO_HON ||
         context.opcode == OP_LON_HON_HOAC_BANG || context.opcode == OP_NHO_HON_HOAC_BANG) &&
        context.operandTexts.size() >= 2) {
        return RuntimeDiagnosticKind::ComparisonTypeMismatch;
    }
    return RuntimeDiagnosticKind::None;
}

// Dựng lời giải thích từ loại lỗi đã được hệ thống suy ra và các dữ kiện thực tế.
inline RuntimeDiagnosticInfo runtimeDiagnosticInfo(
    RuntimeDiagnosticKind kind,
    const RuntimeDiagnosticContext &context) {
    switch (kind) {
        case RuntimeDiagnosticKind::DivisionByZero:
            return {messages::messageText(messages::kRuntimeDiagCategoryArithmetic),
                    messages::formatMessage(
                        messages::kRuntimeDiagDivisionByZeroDescription,
                        {runtimeDiagnosticOperand(
                             context, 0, std::string(messages::kRuntimeDiagFallbackOperand)),
                         runtimeDiagnosticOperand(
                             context, 1, std::string(messages::kRuntimeDiagFallbackZero))}),
                    messages::messageText(messages::kRuntimeDiagDivisionByZeroSuggestion)};
        case RuntimeDiagnosticKind::ModuloByZero:
            return {messages::messageText(messages::kRuntimeDiagCategoryArithmetic),
                    messages::messageText(messages::kRuntimeDiagModuloByZeroDescription),
                    messages::messageText(messages::kRuntimeDiagModuloByZeroSuggestion)};
        case RuntimeDiagnosticKind::IntegerRequired:
            return {messages::messageText(messages::kRuntimeDiagCategoryType),
                    messages::formatMessage(
                        messages::kRuntimeDiagIntegerRequiredDescription,
                        {context.actualType.empty()
                            ? messages::kRuntimeDiagFallbackType
                            : std::string_view(context.actualType)}),
                    messages::messageText(messages::kRuntimeDiagIntegerRequiredSuggestion)};
        case RuntimeDiagnosticKind::NumericOperandRequired:
            return {messages::messageText(messages::kRuntimeDiagCategoryType),
                    messages::messageText(messages::kRuntimeDiagNumericOperandRequiredDescription),
                    messages::messageText(messages::kRuntimeDiagNumericOperandRequiredSuggestion)};
        case RuntimeDiagnosticKind::ComparisonTypeMismatch:
            return {messages::messageText(messages::kRuntimeDiagCategoryType),
                    messages::messageText(messages::kRuntimeDiagComparisonTypeMismatchDescription),
                    messages::messageText(messages::kRuntimeDiagComparisonTypeMismatchSuggestion)};
        case RuntimeDiagnosticKind::FloatConversionFailed:
            return {messages::messageText(messages::kRuntimeDiagCategoryConversion),
                    messages::formatMessage(
                        messages::kRuntimeDiagFloatConversionFailedDescription,
                        {context.conversionInput,
                         context.conversionTarget.empty()
                            ? messages::kRuntimeDiagFallbackNumberTarget
                            : std::string_view(context.conversionTarget)}),
                    messages::messageText(messages::kRuntimeDiagFloatConversionFailedSuggestion)};
        case RuntimeDiagnosticKind::IndexTypeInvalid:
            return {messages::messageText(messages::kRuntimeDiagCategoryIndex),
                    messages::messageText(messages::kRuntimeDiagIndexTypeInvalidDescription),
                    messages::messageText(messages::kRuntimeDiagIndexTypeInvalidSuggestion)};
        case RuntimeDiagnosticKind::IndexOutOfRange:
            return {messages::messageText(messages::kRuntimeDiagCategoryIndex),
                    context.containerSize >= 0
                        ? messages::formatMessage(
                            messages::kRuntimeDiagIndexOutOfRangeSizedDescription,
                            {std::to_string(context.index), std::to_string(context.containerSize)})
                        : messages::formatMessage(
                            messages::kRuntimeDiagIndexOutOfRangeUnknownDescription,
                            {std::to_string(context.index)}),
                    messages::messageText(messages::kRuntimeDiagIndexOutOfRangeSuggestion)};
        case RuntimeDiagnosticKind::ContainerNotIndexable:
            return {messages::messageText(messages::kRuntimeDiagCategoryIndex),
                    messages::messageText(messages::kRuntimeDiagContainerNotIndexableDescription),
                    messages::messageText(messages::kRuntimeDiagContainerNotIndexableSuggestion)};
        case RuntimeDiagnosticKind::FunctionNotFound:
            return {messages::messageText(messages::kRuntimeDiagCategoryCall),
                    messages::formatMessage(messages::kRuntimeDiagFunctionNotFoundDescription, {context.targetName}),
                    messages::messageText(messages::kRuntimeDiagFunctionNotFoundSuggestion)};
        case RuntimeDiagnosticKind::InvalidCallable:
            return {messages::messageText(messages::kRuntimeDiagCategoryCall),
                    messages::messageText(messages::kRuntimeDiagInvalidCallableDescription),
                    messages::messageText(messages::kRuntimeDiagInvalidCallableSuggestion)};
        case RuntimeDiagnosticKind::CallArityMismatch:
            return {messages::messageText(messages::kRuntimeDiagCategoryCall),
                    messages::formatMessage(
                        messages::kRuntimeDiagCallArityMismatchDescription,
                        {context.targetName, std::to_string(context.argumentCount),
                         std::to_string(context.minimumArguments), std::to_string(context.maximumArguments)}),
                    messages::messageText(messages::kRuntimeDiagCallArityMismatchSuggestion)};
        case RuntimeDiagnosticKind::CallDepthExceeded:
            return {messages::messageText(messages::kRuntimeDiagCategoryCall),
                    messages::formatMessage(
                        messages::kRuntimeDiagCallDepthExceededDescription,
                        {context.targetName, std::to_string(context.maximumCallDepth)}),
                    messages::messageText(messages::kRuntimeDiagCallDepthExceededSuggestion)};
        case RuntimeDiagnosticKind::RequiredArgumentMissing:
            return {messages::messageText(messages::kRuntimeDiagCategoryCall),
                    messages::formatMessage(
                        messages::kRuntimeDiagRequiredArgumentMissingDescription,
                        {std::to_string(context.requiredArgumentIndex)}),
                    messages::messageText(messages::kRuntimeDiagRequiredArgumentMissingSuggestion)};
        case RuntimeDiagnosticKind::ModuleInitializationFailed:
            return {messages::messageText(messages::kRuntimeDiagCategoryModule),
                    messages::formatMessage(
                        messages::kRuntimeDiagModuleInitializationFailedDescription,
                        {context.moduleName}),
                    messages::messageText(messages::kRuntimeDiagModuleInitializationFailedSuggestion)};
        case RuntimeDiagnosticKind::ClassNotFound:
            return {messages::messageText(messages::kRuntimeDiagCategoryObject),
                    messages::formatMessage(messages::kRuntimeDiagClassNotFoundDescription, {context.targetName}),
                    messages::messageText(messages::kRuntimeDiagClassNotFoundSuggestion)};
        case RuntimeDiagnosticKind::ObjectRequired:
            return {messages::messageText(messages::kRuntimeDiagCategoryObject),
                    messages::messageText(messages::kRuntimeDiagObjectRequiredDescription),
                    messages::messageText(messages::kRuntimeDiagObjectRequiredSuggestion)};
        case RuntimeDiagnosticKind::PropertyNotFound:
            return {messages::messageText(messages::kRuntimeDiagCategoryObject),
                    messages::formatMessage(messages::kRuntimeDiagPropertyNotFoundDescription, {context.memberName}),
                    messages::messageText(messages::kRuntimeDiagPropertyNotFoundSuggestion)};
        case RuntimeDiagnosticKind::MethodNotFound:
            return {messages::messageText(messages::kRuntimeDiagCategoryObject),
                    messages::formatMessage(messages::kRuntimeDiagMethodNotFoundDescription, {context.memberName}),
                    messages::messageText(messages::kRuntimeDiagMethodNotFoundSuggestion)};
        case RuntimeDiagnosticKind::MemberAccessDenied:
            return {messages::messageText(messages::kRuntimeDiagCategoryAccess),
                    messages::formatMessage(messages::kRuntimeDiagMemberAccessDeniedDescription, {context.memberName}),
                    messages::messageText(messages::kRuntimeDiagMemberAccessDeniedSuggestion)};
        case RuntimeDiagnosticKind::MissingOperand:
            return {messages::messageText(messages::kRuntimeDiagCategoryExpression),
                    messages::formatMessage(
                        messages::kRuntimeDiagMissingOperandDescription,
                        {std::to_string(context.requiredOperands), std::to_string(context.availableOperands)}),
                    messages::messageText(messages::kRuntimeDiagMissingOperandSuggestion)};
        case RuntimeDiagnosticKind::IncrementTypeInvalid:
            return {messages::messageText(messages::kRuntimeDiagCategoryType),
                    messages::formatMessage(messages::kRuntimeDiagIncrementTypeInvalidDescription, {context.actualType}),
                    messages::messageText(messages::kRuntimeDiagIncrementTypeInvalidSuggestion)};
        case RuntimeDiagnosticKind::DecrementTypeInvalid:
            return {messages::messageText(messages::kRuntimeDiagCategoryType),
                    messages::formatMessage(messages::kRuntimeDiagDecrementTypeInvalidDescription, {context.actualType}),
                    messages::messageText(messages::kRuntimeDiagDecrementTypeInvalidSuggestion)};
        case RuntimeDiagnosticKind::ConstantReferenceInvalid:
            return {messages::messageText(messages::kRuntimeDiagCategoryProgramData),
                    messages::messageText(messages::kRuntimeDiagConstantReferenceInvalidDescription),
                    messages::messageText(messages::kRuntimeDiagConstantReferenceInvalidSuggestion)};
        case RuntimeDiagnosticKind::LiteralDecodeFailed:
            return {messages::messageText(messages::kRuntimeDiagCategoryProgramData),
                    messages::messageText(messages::kRuntimeDiagLiteralDecodeFailedDescription),
                    messages::messageText(messages::kRuntimeDiagLiteralDecodeFailedSuggestion)};
        case RuntimeDiagnosticKind::ClosureCaptureInvalid:
            return {messages::messageText(messages::kRuntimeDiagCategoryClosure),
                    messages::messageText(messages::kRuntimeDiagClosureCaptureInvalidDescription),
                    messages::messageText(messages::kRuntimeDiagClosureCaptureInvalidSuggestion)};
        case RuntimeDiagnosticKind::JumpTargetInvalid:
            return {messages::messageText(messages::kRuntimeDiagCategoryControlFlow),
                    messages::formatMessage(
                        messages::kRuntimeDiagJumpTargetInvalidDescription,
                        {std::to_string(context.jumpTarget)}),
                    messages::messageText(messages::kRuntimeDiagJumpTargetInvalidSuggestion)};
        case RuntimeDiagnosticKind::ControlFlowStateInvalid:
            return {messages::messageText(messages::kRuntimeDiagCategoryControlFlow),
                    messages::messageText(messages::kRuntimeDiagControlFlowStateInvalidDescription),
                    messages::messageText(messages::kRuntimeDiagControlFlowStateInvalidSuggestion)};
        case RuntimeDiagnosticKind::NativeOperationFailed:
            return {messages::messageText(messages::kRuntimeDiagCategorySystemLibrary),
                    context.detail.empty()
                        ? messages::formatMessage(
                            messages::kRuntimeDiagNativeOperationFailedDescription,
                            {context.nativeOperation.empty()
                                ? messages::kRuntimeDiagFallbackSystemOperation
                                : std::string_view(context.nativeOperation)})
                        : messages::formatMessage(
                            messages::kRuntimeDiagNativeOperationFailedWithDetailDescription,
                            {context.nativeOperation.empty()
                                ? messages::kRuntimeDiagFallbackSystemOperation
                                : std::string_view(context.nativeOperation),
                             context.detail}),
                    messages::messageText(messages::kRuntimeDiagNativeOperationFailedSuggestion)};
        case RuntimeDiagnosticKind::InternalRuntimeState:
            return {messages::messageText(messages::kRuntimeDiagCategoryInternal),
                    messages::messageText(messages::kRuntimeDiagInternalStateDescription),
                    messages::messageText(messages::kRuntimeDiagInternalStateSuggestion)};
        case RuntimeDiagnosticKind::None:
            return {};
    }
    return {};
}

// Chạy cả bước nhận diện và bước diễn giải để formatter chỉ cần truyền trạng thái thực thi.
inline RuntimeDiagnosticInfo runtimeDiagnosticInfo(
    const RuntimeDiagnosticContext &context) {
    return runtimeDiagnosticInfo(detectRuntimeDiagnostic(context), context);
}

} // namespace vietvm::runtime
