#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "vm/instruction.h"

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
            return {"Số học",
                    "Chương trình đang cố lấy " + runtimeDiagnosticOperand(context, 0, "một số") +
                        " chia cho " + runtimeDiagnosticOperand(context, 1, "0") +
                        ". Số chia bằng 0 nên phép chia không thể cho ra kết quả hợp lệ.",
                    "Hãy kiểm tra số chia trước khi chia. Nếu số chia bằng 0, hãy xử lý trường hợp đó trước."};
        case RuntimeDiagnosticKind::ModuloByZero:
            return {"Số học",
                    "Chương trình đang tìm phần dư của phép chia cho 0. Không thể tính phần dư khi số chia bằng 0.",
                    "Hãy kiểm tra số chia trước khi dùng phép chia lấy dư và xử lý riêng trường hợp bằng 0."};
        case RuntimeDiagnosticKind::IntegerRequired:
            return {"Kiểu dữ liệu",
                    "Chỗ này cần một số nguyên, nhưng chương trình đang đưa vào giá trị thuộc loại '" +
                        (context.actualType.empty() ? std::string("không phù hợp") : context.actualType) + "'.",
                    "Hãy dùng một số nguyên như 0, 1, -2 hoặc chuyển giá trị hiện tại sang số trước khi dùng."};
        case RuntimeDiagnosticKind::NumericOperandRequired:
            return {"Kiểu dữ liệu",
                    "Phép tính này cần số ở cả hai bên, nhưng ít nhất một giá trị hiện tại không phải là số.",
                    "Hãy kiểm tra hai giá trị trong phép tính và đổi giá trị không phải số thành số trước khi tính."};
        case RuntimeDiagnosticKind::ComparisonTypeMismatch:
            return {"Kiểu dữ liệu",
                    "Hai giá trị đang được so sánh không cùng loại có thể xếp thứ tự với nhau.",
                    "Hãy đưa hai giá trị về cùng loại, ví dụ cùng là số hoặc cùng là chuỗi, rồi mới so sánh."};
        case RuntimeDiagnosticKind::FloatConversionFailed:
            return {"Chuyển kiểu",
                    "Chương trình muốn đổi '" + context.conversionInput + "' thành " +
                        (context.conversionTarget.empty() ? std::string("một con số") : context.conversionTarget) +
                        ", nhưng nội dung đó không có dạng số mà V++ hiểu.",
                    "Hãy kiểm tra dữ liệu đầu vào. Ví dụ 12, -3 và 4.5 là các cách viết số hợp lệ."};
        case RuntimeDiagnosticKind::IndexTypeInvalid:
            return {"Truy cập phần tử",
                    "Chương trình đang chọn một phần tử bằng vị trí không phải số nguyên nên V++ không biết phải lấy phần tử thứ mấy.",
                    "Hãy dùng số nguyên làm vị trí, ví dụ 0 cho phần tử đầu tiên, 1 cho phần tử thứ hai."};
        case RuntimeDiagnosticKind::IndexOutOfRange:
            return {"Truy cập phần tử",
                    "Chương trình đang yêu cầu phần tử ở vị trí " + std::to_string(context.index) +
                        (context.containerSize >= 0
                            ? ", nhưng dữ liệu chỉ có " + std::to_string(context.containerSize) + " phần tử."
                            : ", nhưng vị trí này không tồn tại trong dữ liệu hiện có."),
                    "Hãy kiểm tra số lượng phần tử trước khi truy cập. Vị trí hợp lệ bắt đầu từ 0 và phải nhỏ hơn số lượng phần tử."};
        case RuntimeDiagnosticKind::ContainerNotIndexable:
            return {"Truy cập phần tử",
                    "Bạn đang dùng dấu [] với một giá trị không có các phần tử được đánh số vị trí.",
                    "Hãy dùng [] với danh sách, bộ hoặc chuỗi; với đối tượng hãy dùng thuộc tính hay phương thức phù hợp."};
        case RuntimeDiagnosticKind::FunctionNotFound:
            return {"Gọi hàm",
                    "Chương trình muốn gọi hàm '" + context.targetName + "', nhưng không tìm thấy hàm đó trong phạm vi hiện tại.",
                    "Hãy kiểm tra tên hàm, phần khai báo hàm và mô đun chứa hàm đã được nhập hay chưa."};
        case RuntimeDiagnosticKind::InvalidCallable:
            return {"Gọi hàm",
                    "Chương trình đang cố gọi một giá trị như hàm, nhưng giá trị đó không trỏ tới một hàm có thể chạy.",
                    "Hãy kiểm tra biến đứng ở vị trí lời gọi và bảo đảm nó đang chứa tham chiếu hàm hoặc hàm đóng hợp lệ."};
        case RuntimeDiagnosticKind::CallArityMismatch:
            return {"Gọi hàm",
                    "Hàm '" + context.targetName + "' được gọi với " + std::to_string(context.argumentCount) +
                        " giá trị, trong khi hàm này nhận từ " + std::to_string(context.minimumArguments) +
                        " đến " + std::to_string(context.maximumArguments) + " giá trị.",
                    "Hãy thêm hoặc bớt giá trị khi gọi hàm để khớp với phần khai báo của hàm."};
        case RuntimeDiagnosticKind::CallDepthExceeded:
            return {"Gọi hàm",
                    "Hàm '" + context.targetName + "' đã gọi lồng quá " +
                        std::to_string(context.maximumCallDepth) +
                        " lần. Thường là một hàm cứ gọi lại chính nó mà chưa đi tới điều kiện dừng.",
                    "Hãy kiểm tra điều kiện dừng. Mỗi lần gọi lại phải làm dữ liệu tiến gần hơn tới trường hợp dừng."};
        case RuntimeDiagnosticKind::RequiredArgumentMissing:
            return {"Gọi hàm",
                    "Hàm cần một giá trị bắt buộc ở vị trí " + std::to_string(context.requiredArgumentIndex) +
                        ", nhưng lời gọi không truyền giá trị đó vào.",
                    "Hãy truyền thêm giá trị còn thiếu hoặc khai báo giá trị mặc định nếu tham số được phép bỏ qua."};
        case RuntimeDiagnosticKind::ModuleInitializationFailed:
            return {"Mô đun",
                    "Mô đun '" + context.moduleName + "' chưa khởi tạo xong nên chương trình chưa thể sử dụng nó.",
                    "Hãy xem lỗi đầu tiên xảy ra trong phần khởi tạo của mô đun và các mô đun mà nó phụ thuộc."};
        case RuntimeDiagnosticKind::ClassNotFound:
            return {"Lớp và đối tượng",
                    "Chương trình muốn dùng lớp '" + context.targetName + "', nhưng V++ không tìm thấy phần khai báo của lớp này.",
                    "Hãy kiểm tra tên lớp, nơi khai báo lớp và mô đun chứa lớp đã được nhập hay chưa."};
        case RuntimeDiagnosticKind::ObjectRequired:
            return {"Lớp và đối tượng",
                    "Chương trình đang cố dùng thuộc tính hoặc phương thức trên một giá trị không phải đối tượng.",
                    "Hãy kiểm tra giá trị đứng trước dấu chấm và bảo đảm nó là một đối tượng được tạo từ lớp."};
        case RuntimeDiagnosticKind::PropertyNotFound:
            return {"Lớp và đối tượng",
                    "Đối tượng hiện tại không có thuộc tính '" + context.memberName + "'.",
                    "Hãy kiểm tra chính tả tên thuộc tính và phần khai báo của lớp."};
        case RuntimeDiagnosticKind::MethodNotFound:
            return {"Lớp và đối tượng",
                    "Đối tượng hiện tại không có phương thức '" + context.memberName + "' phù hợp để gọi.",
                    "Hãy kiểm tra tên phương thức, lớp của đối tượng và các lớp cha của nó."};
        case RuntimeDiagnosticKind::MemberAccessDenied:
            return {"Quyền truy cập",
                    "Thành viên '" + context.memberName + "' có tồn tại nhưng đoạn mã hiện tại không được phép sử dụng nó.",
                    "Hãy gọi thành viên từ phạm vi được phép hoặc đổi mức truy cập nếu thiết kế của lớp cho phép."};
        case RuntimeDiagnosticKind::MissingOperand:
            return {"Biểu thức",
                    "VM cần " + std::to_string(context.requiredOperands) + " giá trị để thực hiện thao tác này nhưng hiện chỉ có " +
                        std::to_string(context.availableOperands) + ".",
                    "Hãy kiểm tra biểu thức gần vị trí báo lỗi; có thể một giá trị đã bị thiếu hoặc bytecode được tạo không đúng."};
        case RuntimeDiagnosticKind::IncrementTypeInvalid:
            return {"Kiểu dữ liệu",
                    "Chương trình đang tăng thêm 1 cho một giá trị thuộc loại '" + context.actualType + "', loại này không thể tăng như một con số.",
                    "Hãy dùng ++ với giá trị số hoặc chuyển dữ liệu sang số trước khi tăng."};
        case RuntimeDiagnosticKind::DecrementTypeInvalid:
            return {"Kiểu dữ liệu",
                    "Chương trình đang giảm 1 trên một giá trị thuộc loại '" + context.actualType + "', loại này không thể giảm như một con số.",
                    "Hãy dùng -- với giá trị số hoặc chuyển dữ liệu sang số trước khi giảm."};
        case RuntimeDiagnosticKind::ConstantReferenceInvalid:
            return {"Dữ liệu chương trình",
                    "Một câu lệnh đang trỏ tới dữ liệu hằng không tồn tại trong chương trình đã biên dịch.",
                    "Nếu mã nguồn V++ hợp lệ, hãy biên dịch lại chương trình; nếu lỗi còn lặp lại thì đây có thể là lỗi của trình biên dịch."};
        case RuntimeDiagnosticKind::LiteralDecodeFailed:
            return {"Dữ liệu chương trình",
                    "V++ đọc được câu lệnh tạo dữ liệu nhưng phần dữ liệu đi kèm bị hỏng hoặc không đúng định dạng.",
                    "Hãy biên dịch lại từ mã nguồn. Nếu lỗi vẫn xuất hiện với cùng mã nguồn, hãy báo lỗi cho trình biên dịch V++."};
        case RuntimeDiagnosticKind::ClosureCaptureInvalid:
            return {"Hàm đóng",
                    "Hàm đóng cần giữ lại một biến từ bên ngoài nhưng thông tin về biến đó không còn hợp lệ.",
                    "Hãy biên dịch lại chương trình. Nếu lỗi vẫn xảy ra, đây có thể là lỗi trong quá trình tạo bytecode cho hàm đóng."};
        case RuntimeDiagnosticKind::JumpTargetInvalid:
            return {"Luồng chương trình",
                    "Chương trình định nhảy tới vị trí " + std::to_string(context.jumpTarget) + " nhưng vị trí đó nằm ngoài phần mã có thể chạy.",
                    "Hãy biên dịch lại chương trình. Nếu mã nguồn hợp lệ mà lỗi vẫn xảy ra, hãy báo lỗi cho trình biên dịch V++."};
        case RuntimeDiagnosticKind::ControlFlowStateInvalid:
            return {"Luồng chương trình",
                    "Một câu lệnh điều khiển đang chạy ở nơi không có cấu trúc tương ứng để nó làm việc.",
                    "Hãy kiểm tra các khối lặp, chọn, điều kiện và dấu ngoặc khối gần vị trí báo lỗi."};
        case RuntimeDiagnosticKind::NativeOperationFailed:
            return {"Thư viện hệ thống",
                    "Tác vụ '" + (context.nativeOperation.empty() ? std::string("hệ thống") : context.nativeOperation) +
                        "' đã được gọi nhưng hệ điều hành hoặc thư viện bên dưới không thực hiện được." +
                        (context.detail.empty() ? std::string() : " Chi tiết: " + context.detail),
                    "Hãy kiểm tra dữ liệu truyền vào, tệp/đường dẫn, quyền truy cập hoặc kết nối mà tác vụ này cần."};
        case RuntimeDiagnosticKind::InternalRuntimeState:
            return {"Nội bộ V++",
                    "Máy ảo gặp một trạng thái không thể xuất hiện khi bytecode và trạng thái chạy đều hợp lệ.",
                    "Hãy biên dịch lại chương trình. Nếu lỗi lặp lại, hãy giữ đoạn mã ngắn nhất gây lỗi để báo cho V++."};
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
