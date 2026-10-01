#pragma once

#include <string_view>

// Thông báo cho runtime, VM và các hàm native.
namespace vietvm::messages {

// Nội dung lỗi của VM, bytecode và luồng điều khiển. Các chuỗi ở đây không tự
// thêm tiền tố `Lỗi:`; lớp hiển thị như CLI chịu trách nhiệm gắn tiền tố đó.
inline constexpr std::string_view kRuntimeValueNotNumeric = "toDouble: giá trị không phải số";
inline constexpr std::string_view kVmInvalidIntegerValue = "giá trị không phải số nguyên";
inline constexpr std::string_view kVmDivisionByZero =
    "không thể chia {0} cho {1}";
inline constexpr std::string_view kVmInvalidFloatIndex = "chỉ số số thực không hợp lệ";
inline constexpr std::string_view kVmInvalidStringIndex = "chỉ số chuỗi không hợp lệ";
inline constexpr std::string_view kVmInvalidMapIndex = "chỉ số ánh xạ không hợp lệ";
inline constexpr std::string_view kVmInvalidListIndex = "chỉ số danh sách không hợp lệ";
inline constexpr std::string_view kVmIndexNeedsListOrString = "truy cập chỉ số chỉ áp dụng cho danh sách, bộ hoặc chuỗi";
inline constexpr std::string_view kVmIndexMustBeInteger = "chỉ số phải là số nguyên";
inline constexpr std::string_view kVmIndexOutOfRange = "chỉ số vượt phạm vi";
inline constexpr std::string_view kVmNotEnoughOperands = "không đủ toán hạng cho toán tử";
inline constexpr std::string_view kVmNumericOnlyOperator = "toán tử số chỉ áp dụng cho số";
inline constexpr std::string_view kVmLogicOnlyOperator = "toán tử logic chỉ áp dụng cho số";
inline constexpr std::string_view kVmCannotCompareDifferentTypes = "không thể so sánh hai kiểu dữ liệu khác nhau";
inline constexpr std::string_view kVmUnknownOperator = "Toán tử không xác định";
inline constexpr std::string_view kVmMissingModuloOperands = "thiếu toán hạng cho phép chia dư";
inline constexpr std::string_view kVmModuloByZero = "chia dư cho 0";
inline constexpr std::string_view kVmMissingNegationOperand = "Thiếu toán hạng cho toán tử phủ định !";
inline constexpr std::string_view kVmEmptyStackWhenPrint = "ngăn xếp rỗng khi thực hiện IN";
inline constexpr std::string_view kVmMissingNotOperands = "không đủ toán hạng cho toán tử phủ định";
inline constexpr std::string_view kVmUnknownOpcode = "Mã lệnh không xác định";
inline constexpr std::string_view kVmCannotConvertToFloat = "không thể chuyển '{0}' thành số thực";
inline constexpr std::string_view kVmParseMapLiteral = "phân tích giá trị ánh xạ trực tiếp thất bại: {0}";
inline constexpr std::string_view kVmMapLiteralEncodeInvalid = "Dữ liệu mã hóa của giá trị ánh xạ trực tiếp không hợp lệ";
inline constexpr std::string_view kVmMapLiteralTypeTagInvalid = "Giá trị ánh xạ trực tiếp: thẻ kiểu không hợp lệ";
inline constexpr std::string_view kVmDefaultParamEncodeInvalid = "Dữ liệu mã hóa của tham số mặc định không hợp lệ";
inline constexpr std::string_view kVmDefaultParamTypeInvalid = "Kiểu của tham số mặc định không hợp lệ";
inline constexpr std::string_view kVmContinueMissingUpdate = "BO_QUA: không tìm thấy OP_CAP_NHAT trong vòng lặp";
inline constexpr std::string_view kVmSwitchEmptyStack = "CHON: ngăn xếp rỗng";
inline constexpr std::string_view kVmCaseOutsideSwitch = "CA: Không nằm trong khối CHON";
inline constexpr std::string_view kVmCaseInvalidOperandFormat = "CA: định dạng toán hạng không hợp lệ";
inline constexpr std::string_view kVmDefaultOutsideSwitch = "MAC_DINH: Không nằm trong khối CHON";
inline constexpr std::string_view kVmBreakOutsideSwitch = "THOAT: Không nằm trong khối CHON";
inline constexpr std::string_view kVmAssignNotEnoughOperands = "Không đủ toán hạng để GÁN";
inline constexpr std::string_view kVmVariableIdMustBeInt = "mã biến phải là số nguyên";
inline constexpr std::string_view kVmJumpAddressOutOfRange = "địa chỉ nhảy ngoài phạm vi";
inline constexpr std::string_view kVmJumpIfFalseEmptyStack = "ngăn xếp rỗng khi thực thi OP_JUMP_IF_FALSE";
inline constexpr std::string_view kVmJumpConditionMustBeInt = "điều kiện nhảy phải là số nguyên";
inline constexpr std::string_view kVmNoOpenBlock = "không có khối mở";
inline constexpr std::string_view kVmDefaultParamIndexInvalid = "OP_PARAM_MAC_DINH: chỉ số giá trị mặc định không hợp lệ";
inline constexpr std::string_view kVmIncrementEmptyStack = "OP_CONG_MOT: ngăn xếp rỗng";
inline constexpr std::string_view kVmIncrementCannotIncreaseString = "OP_CONG_MOT: không thể tăng giá trị chuỗi";
inline constexpr std::string_view kVmIncrementUnsupportedType = "OP_CONG_MOT: kiểu dữ liệu chưa được hỗ trợ";
inline constexpr std::string_view kVmDecrementEmptyStack = "OP_TRU_MOT: ngăn xếp rỗng";
inline constexpr std::string_view kVmDecrementUnsupportedType = "OP_TRU_MOT: kiểu dữ liệu chưa được hỗ trợ";
inline constexpr std::string_view kVmFunctionNotFound = "OP_GOI: hàm không tồn tại (mã/chỉ số tên={0}, tên='{1}')";
inline constexpr std::string_view kVmCallArityMismatch = "lời gọi '{0}' nhận {1} đối số nhưng yêu cầu từ {2} đến {3}";
inline constexpr std::string_view kVmCallDepthExceeded = "Độ sâu lời gọi vượt giới hạn {0} khi gọi '{1}'";
inline constexpr std::string_view kVmRequiredArgumentMissing = "thiếu đối số bắt buộc tại vị trí {0}";
inline constexpr std::string_view kVmIndirectCallMissingReference = "OP_GOI_GIAN_TIEP: thiếu tham chiếu hàm";
inline constexpr std::string_view kVmIndirectCallInvalidReference = "OP_GOI_GIAN_TIEP: tham chiếu hàm không hợp lệ";
inline constexpr std::string_view kVmIndirectCallUnsupportedReferenceType = "OP_GOI_GIAN_TIEP: kiểu tham chiếu hàm chưa được hỗ trợ";
inline constexpr std::string_view kVmClosureMissingCaptures = "closure: không đủ slot capture trên ngăn xếp";
inline constexpr std::string_view kVmClosureInvalidCapture = "closure: mã slot capture phải là số nguyên";
inline constexpr std::string_view kVmUnknownThrownValue = "lỗi không xác định";
inline constexpr std::string_view kVmUncaughtException = "ngoại lệ không bắt được: {0}";
inline constexpr std::string_view kVmLogPrefix = "[VM] ";
inline constexpr std::string_view kVmModuleInitializationInvalidState = "trạng thái khởi tạo mô-đun không hợp lệ: {0}";
inline constexpr std::string_view kVmModuleInitializationFailed = "khởi tạo mô-đun thất bại: {0}";
inline constexpr std::string_view kVmObjectInvalidClassNameIndex = "chỉ số tên lớp runtime không hợp lệ";
inline constexpr std::string_view kVmObjectInvalidMemberNameIndex = "chỉ số tên thành viên runtime không hợp lệ";
inline constexpr std::string_view kVmObjectClassNotFound = "lớp runtime không tồn tại: {0}";
inline constexpr std::string_view kVmObjectExpectedInstance = "thao tác thuộc tính/phương thức yêu cầu một đối tượng";
inline constexpr std::string_view kVmObjectPropertyNotFound = "thuộc tính không tồn tại: {0}";
inline constexpr std::string_view kVmObjectMethodNotFound = "phương thức không tồn tại: {0}";
inline constexpr std::string_view kVmObjectPrivateMethodAccess = "không thể gọi phương thức riêng tư: {0}";
inline constexpr std::string_view kVmObjectProtectedMethodAccess = "không thể gọi phương thức bảo vệ: {0}";
inline constexpr std::string_view kVmObjectNotEnoughOperands = "không đủ toán hạng cho thao tác đối tượng";
inline constexpr std::string_view kVmResetWhileCallActive = "không thể reset VM khi lời gọi vẫn đang hoạt động";
inline constexpr std::string_view kVmFixtureStackEmpty = "VM fixture stack is empty";
inline constexpr std::string_view kVmFixtureVariableMissing = "VM fixture variable is missing";
inline constexpr std::string_view kVmBenchmarkResetRequiresIdle = "VM benchmark reset requires an idle VM";
inline constexpr std::string_view kVmFixtureCallStackEmpty = "VM fixture call stack is empty";
inline constexpr std::string_view kVmFixtureSwitchStackEmpty = "VM fixture switch stack is empty";

// Structured runtime diagnostic titles, explanations and suggestions.
inline constexpr std::string_view kRuntimeDiagCategoryArithmetic = "Số học";
inline constexpr std::string_view kRuntimeDiagCategoryType = "Kiểu dữ liệu";
inline constexpr std::string_view kRuntimeDiagCategoryConversion = "Chuyển kiểu";
inline constexpr std::string_view kRuntimeDiagCategoryIndex = "Truy cập phần tử";
inline constexpr std::string_view kRuntimeDiagCategoryCall = "Gọi hàm";
inline constexpr std::string_view kRuntimeDiagCategoryModule = "Mô đun";
inline constexpr std::string_view kRuntimeDiagCategoryObject = "Lớp và đối tượng";
inline constexpr std::string_view kRuntimeDiagCategoryAccess = "Quyền truy cập";
inline constexpr std::string_view kRuntimeDiagCategoryExpression = "Biểu thức";
inline constexpr std::string_view kRuntimeDiagCategoryProgramData = "Dữ liệu chương trình";
inline constexpr std::string_view kRuntimeDiagCategoryClosure = "Hàm đóng";
inline constexpr std::string_view kRuntimeDiagCategoryControlFlow = "Luồng chương trình";
inline constexpr std::string_view kRuntimeDiagCategorySystemLibrary = "Thư viện hệ thống";
inline constexpr std::string_view kRuntimeDiagCategoryInternal = "Nội bộ V++";
inline constexpr std::string_view kRuntimeDiagFallbackOperand = "một số";
inline constexpr std::string_view kRuntimeDiagFallbackZero = "0";
inline constexpr std::string_view kRuntimeDiagFallbackType = "không phù hợp";
inline constexpr std::string_view kRuntimeDiagFallbackNumberTarget = "một con số";
inline constexpr std::string_view kRuntimeDiagFallbackSystemOperation = "hệ thống";

inline constexpr std::string_view kRuntimeDiagDivisionByZeroDescription =
    "Chương trình đang cố lấy {0} chia cho {1}. Số chia bằng 0 nên phép chia không thể cho ra kết quả hợp lệ.";
inline constexpr std::string_view kRuntimeDiagDivisionByZeroSuggestion =
    "Hãy kiểm tra số chia trước khi chia. Nếu số chia bằng 0, hãy xử lý trường hợp đó trước.";
inline constexpr std::string_view kRuntimeDiagModuloByZeroDescription =
    "Chương trình đang tìm phần dư của phép chia cho 0. Không thể tính phần dư khi số chia bằng 0.";
inline constexpr std::string_view kRuntimeDiagModuloByZeroSuggestion =
    "Hãy kiểm tra số chia trước khi dùng phép chia lấy dư và xử lý riêng trường hợp bằng 0.";
inline constexpr std::string_view kRuntimeDiagIntegerRequiredDescription =
    "Chỗ này cần một số nguyên, nhưng chương trình đang đưa vào giá trị thuộc loại '{0}'.";
inline constexpr std::string_view kRuntimeDiagIntegerRequiredSuggestion =
    "Hãy dùng một số nguyên như 0, 1, -2 hoặc chuyển giá trị hiện tại sang số trước khi dùng.";
inline constexpr std::string_view kRuntimeDiagNumericOperandRequiredDescription =
    "Phép tính này cần số ở cả hai bên, nhưng ít nhất một giá trị hiện tại không phải là số.";
inline constexpr std::string_view kRuntimeDiagNumericOperandRequiredSuggestion =
    "Hãy kiểm tra hai giá trị trong phép tính và đổi giá trị không phải số thành số trước khi tính.";
inline constexpr std::string_view kRuntimeDiagComparisonTypeMismatchDescription =
    "Hai giá trị đang được so sánh không cùng loại có thể xếp thứ tự với nhau.";
inline constexpr std::string_view kRuntimeDiagComparisonTypeMismatchSuggestion =
    "Hãy đưa hai giá trị về cùng loại, ví dụ cùng là số hoặc cùng là chuỗi, rồi mới so sánh.";
inline constexpr std::string_view kRuntimeDiagFloatConversionFailedDescription =
    "Chương trình muốn đổi '{0}' thành {1}, nhưng nội dung đó không có dạng số mà V++ hiểu.";
inline constexpr std::string_view kRuntimeDiagFloatConversionFailedSuggestion =
    "Hãy kiểm tra dữ liệu đầu vào. Ví dụ 12, -3 và 4.5 là các cách viết số hợp lệ.";
inline constexpr std::string_view kRuntimeDiagIndexTypeInvalidDescription =
    "Chương trình đang chọn một phần tử bằng vị trí không phải số nguyên nên V++ không biết phải lấy phần tử thứ mấy.";
inline constexpr std::string_view kRuntimeDiagIndexTypeInvalidSuggestion =
    "Hãy dùng số nguyên làm vị trí, ví dụ 0 cho phần tử đầu tiên, 1 cho phần tử thứ hai.";
inline constexpr std::string_view kRuntimeDiagIndexOutOfRangeSizedDescription =
    "Chương trình đang yêu cầu phần tử ở vị trí {0}, nhưng dữ liệu chỉ có {1} phần tử.";
inline constexpr std::string_view kRuntimeDiagIndexOutOfRangeUnknownDescription =
    "Chương trình đang yêu cầu phần tử ở vị trí {0}, nhưng vị trí này không tồn tại trong dữ liệu hiện có.";
inline constexpr std::string_view kRuntimeDiagIndexOutOfRangeSuggestion =
    "Hãy kiểm tra số lượng phần tử trước khi truy cập. Vị trí hợp lệ bắt đầu từ 0 và phải nhỏ hơn số lượng phần tử.";
inline constexpr std::string_view kRuntimeDiagContainerNotIndexableDescription =
    "Bạn đang dùng dấu [] với một giá trị không có các phần tử được đánh số vị trí.";
inline constexpr std::string_view kRuntimeDiagContainerNotIndexableSuggestion =
    "Hãy dùng [] với danh sách, bộ hoặc chuỗi; với đối tượng hãy dùng thuộc tính hay phương thức phù hợp.";
inline constexpr std::string_view kRuntimeDiagFunctionNotFoundDescription =
    "Chương trình muốn gọi hàm '{0}', nhưng không tìm thấy hàm đó trong phạm vi hiện tại.";
inline constexpr std::string_view kRuntimeDiagFunctionNotFoundSuggestion =
    "Hãy kiểm tra tên hàm, phần khai báo hàm và mô đun chứa hàm đã được nhập hay chưa.";
inline constexpr std::string_view kRuntimeDiagInvalidCallableDescription =
    "Chương trình đang cố gọi một giá trị như hàm, nhưng giá trị đó không trỏ tới một hàm có thể chạy.";
inline constexpr std::string_view kRuntimeDiagInvalidCallableSuggestion =
    "Hãy kiểm tra biến đứng ở vị trí lời gọi và bảo đảm nó đang chứa tham chiếu hàm hoặc hàm đóng hợp lệ.";
inline constexpr std::string_view kRuntimeDiagCallArityMismatchDescription =
    "Hàm '{0}' được gọi với {1} giá trị, trong khi hàm này nhận từ {2} đến {3} giá trị.";
inline constexpr std::string_view kRuntimeDiagCallArityMismatchSuggestion =
    "Hãy thêm hoặc bớt giá trị khi gọi hàm để khớp với phần khai báo của hàm.";
inline constexpr std::string_view kRuntimeDiagCallDepthExceededDescription =
    "Hàm '{0}' đã gọi lồng quá {1} lần. Thường là một hàm cứ gọi lại chính nó mà chưa đi tới điều kiện dừng.";
inline constexpr std::string_view kRuntimeDiagCallDepthExceededSuggestion =
    "Hãy kiểm tra điều kiện dừng. Mỗi lần gọi lại phải làm dữ liệu tiến gần hơn tới trường hợp dừng.";
inline constexpr std::string_view kRuntimeDiagRequiredArgumentMissingDescription =
    "Hàm cần một giá trị bắt buộc ở vị trí {0}, nhưng lời gọi không truyền giá trị đó vào.";
inline constexpr std::string_view kRuntimeDiagRequiredArgumentMissingSuggestion =
    "Hãy truyền thêm giá trị còn thiếu hoặc khai báo giá trị mặc định nếu tham số được phép bỏ qua.";
inline constexpr std::string_view kRuntimeDiagModuleInitializationFailedDescription =
    "Mô đun '{0}' chưa khởi tạo xong nên chương trình chưa thể sử dụng nó.";
inline constexpr std::string_view kRuntimeDiagModuleInitializationFailedSuggestion =
    "Hãy xem lỗi đầu tiên xảy ra trong phần khởi tạo của mô đun và các mô đun mà nó phụ thuộc.";
inline constexpr std::string_view kRuntimeDiagClassNotFoundDescription =
    "Chương trình muốn dùng lớp '{0}', nhưng V++ không tìm thấy phần khai báo của lớp này.";
inline constexpr std::string_view kRuntimeDiagClassNotFoundSuggestion =
    "Hãy kiểm tra tên lớp, nơi khai báo lớp và mô đun chứa lớp đã được nhập hay chưa.";
inline constexpr std::string_view kRuntimeDiagObjectRequiredDescription =
    "Chương trình đang cố dùng thuộc tính hoặc phương thức trên một giá trị không phải đối tượng.";
inline constexpr std::string_view kRuntimeDiagObjectRequiredSuggestion =
    "Hãy kiểm tra giá trị đứng trước dấu chấm và bảo đảm nó là một đối tượng được tạo từ lớp.";
inline constexpr std::string_view kRuntimeDiagPropertyNotFoundDescription =
    "Đối tượng hiện tại không có thuộc tính '{0}'.";
inline constexpr std::string_view kRuntimeDiagPropertyNotFoundSuggestion =
    "Hãy kiểm tra chính tả tên thuộc tính và phần khai báo của lớp.";
inline constexpr std::string_view kRuntimeDiagMethodNotFoundDescription =
    "Đối tượng hiện tại không có phương thức '{0}' phù hợp để gọi.";
inline constexpr std::string_view kRuntimeDiagMethodNotFoundSuggestion =
    "Hãy kiểm tra tên phương thức, lớp của đối tượng và các lớp cha của nó.";
inline constexpr std::string_view kRuntimeDiagMemberAccessDeniedDescription =
    "Thành viên '{0}' có tồn tại nhưng đoạn mã hiện tại không được phép sử dụng nó.";
inline constexpr std::string_view kRuntimeDiagMemberAccessDeniedSuggestion =
    "Hãy gọi thành viên từ phạm vi được phép hoặc đổi mức truy cập nếu thiết kế của lớp cho phép.";
inline constexpr std::string_view kRuntimeDiagMissingOperandDescription =
    "VM cần {0} giá trị để thực hiện thao tác này nhưng hiện chỉ có {1}.";
inline constexpr std::string_view kRuntimeDiagMissingOperandSuggestion =
    "Hãy kiểm tra biểu thức gần vị trí báo lỗi; có thể một giá trị đã bị thiếu hoặc bytecode được tạo không đúng.";
inline constexpr std::string_view kRuntimeDiagIncrementTypeInvalidDescription =
    "Chương trình đang tăng thêm 1 cho một giá trị thuộc loại '{0}', loại này không thể tăng như một con số.";
inline constexpr std::string_view kRuntimeDiagIncrementTypeInvalidSuggestion =
    "Hãy dùng ++ với giá trị số hoặc chuyển dữ liệu sang số trước khi tăng.";
inline constexpr std::string_view kRuntimeDiagDecrementTypeInvalidDescription =
    "Chương trình đang giảm 1 trên một giá trị thuộc loại '{0}', loại này không thể giảm như một con số.";
inline constexpr std::string_view kRuntimeDiagDecrementTypeInvalidSuggestion =
    "Hãy dùng -- với giá trị số hoặc chuyển dữ liệu sang số trước khi giảm.";
inline constexpr std::string_view kRuntimeDiagConstantReferenceInvalidDescription =
    "Một câu lệnh đang trỏ tới dữ liệu hằng không tồn tại trong chương trình đã biên dịch.";
inline constexpr std::string_view kRuntimeDiagConstantReferenceInvalidSuggestion =
    "Nếu mã nguồn V++ hợp lệ, hãy biên dịch lại chương trình; nếu lỗi còn lặp lại thì đây có thể là lỗi của trình biên dịch.";
inline constexpr std::string_view kRuntimeDiagLiteralDecodeFailedDescription =
    "V++ đọc được câu lệnh tạo dữ liệu nhưng phần dữ liệu đi kèm bị hỏng hoặc không đúng định dạng.";
inline constexpr std::string_view kRuntimeDiagLiteralDecodeFailedSuggestion =
    "Hãy biên dịch lại từ mã nguồn. Nếu lỗi vẫn xuất hiện với cùng mã nguồn, hãy báo lỗi cho trình biên dịch V++.";
inline constexpr std::string_view kRuntimeDiagClosureCaptureInvalidDescription =
    "Hàm đóng cần giữ lại một biến từ bên ngoài nhưng thông tin về biến đó không còn hợp lệ.";
inline constexpr std::string_view kRuntimeDiagClosureCaptureInvalidSuggestion =
    "Hãy biên dịch lại chương trình. Nếu lỗi vẫn xảy ra, đây có thể là lỗi trong quá trình tạo bytecode cho hàm đóng.";
inline constexpr std::string_view kRuntimeDiagJumpTargetInvalidDescription =
    "Chương trình định nhảy tới vị trí {0} nhưng vị trí đó nằm ngoài phần mã có thể chạy.";
inline constexpr std::string_view kRuntimeDiagJumpTargetInvalidSuggestion =
    "Hãy biên dịch lại chương trình. Nếu mã nguồn hợp lệ mà lỗi vẫn xảy ra, hãy báo lỗi cho trình biên dịch V++.";
inline constexpr std::string_view kRuntimeDiagControlFlowStateInvalidDescription =
    "Một câu lệnh điều khiển đang chạy ở nơi không có cấu trúc tương ứng để nó làm việc.";
inline constexpr std::string_view kRuntimeDiagControlFlowStateInvalidSuggestion =
    "Hãy kiểm tra các khối lặp, chọn, điều kiện và dấu ngoặc khối gần vị trí báo lỗi.";
inline constexpr std::string_view kRuntimeDiagNativeOperationFailedDescription =
    "Tác vụ '{0}' đã được gọi nhưng hệ điều hành hoặc thư viện bên dưới không thực hiện được.";
inline constexpr std::string_view kRuntimeDiagNativeOperationFailedWithDetailDescription =
    "Tác vụ '{0}' đã được gọi nhưng hệ điều hành hoặc thư viện bên dưới không thực hiện được. Chi tiết: {1}";
inline constexpr std::string_view kRuntimeDiagNativeOperationFailedSuggestion =
    "Hãy kiểm tra dữ liệu truyền vào, tệp/đường dẫn, quyền truy cập hoặc kết nối mà tác vụ này cần.";
inline constexpr std::string_view kRuntimeDiagInternalStateDescription =
    "Máy ảo gặp một trạng thái không thể xuất hiện khi bytecode và trạng thái chạy đều hợp lệ.";
inline constexpr std::string_view kRuntimeDiagInternalStateSuggestion =
    "Hãy biên dịch lại chương trình. Nếu lỗi lặp lại, hãy giữ đoạn mã ngắn nhất gây lỗi để báo cho V++.";

// Native-function invocation and file/time diagnostics.
inline constexpr std::string_view kNativeArgumentCount = "{0} yêu cầu {1} tham số";
inline constexpr std::string_view kNativeInvalidArgument = "{0}: {1} không hợp lệ";
inline constexpr std::string_view kNativeFileOpenForReadFailed = "{0}: không thể mở tệp để đọc";
inline constexpr std::string_view kNativeFileOpenForWriteFailed = "{0}: không thể mở tệp để ghi";
inline constexpr std::string_view kNativeFileWriteFailed = "{0}: ghi tệp thất bại";
inline constexpr std::string_view kNativeTimeFormatFailed = "{0}: định dạng thời gian thất bại";
inline constexpr std::string_view kNativeJsonParseTrailingData = "json phân tích: còn dữ liệu sau giá trị JSON";
inline constexpr std::string_view kNativeJsonParseAtPosition = "json phân tích: {0} tại vị trí {1}";
inline constexpr std::string_view kNativeJsonValueMissing = "thiếu giá trị";
inline constexpr std::string_view kNativeJsonValueInvalid = "giá trị không hợp lệ";
inline constexpr std::string_view kNativeJsonUnicodeEscapeTooShort = "escape unicode chưa đủ 4 chữ số";
inline constexpr std::string_view kNativeJsonUnicodeEscapeInvalid = "escape unicode không hợp lệ";
inline constexpr std::string_view kNativeJsonStringMustStartWithQuote = "chuỗi phải bắt đầu bằng dấu nháy";
inline constexpr std::string_view kNativeJsonStringRawControlCharacter = "chuỗi chứa ký tự điều khiển chưa escape";
inline constexpr std::string_view kNativeJsonStringEscapeMissing = "escape chuỗi bị thiếu";
inline constexpr std::string_view kNativeJsonHighSurrogateMissingLow = "surrogate unicode cao thiếu cặp thấp";
inline constexpr std::string_view kNativeJsonLowSurrogateInvalid = "surrogate unicode thấp không hợp lệ";
inline constexpr std::string_view kNativeJsonLowSurrogateWithoutHigh = "surrogate unicode thấp không có cặp cao";
inline constexpr std::string_view kNativeJsonStringEscapeInvalid = "escape chuỗi không hợp lệ";
inline constexpr std::string_view kNativeJsonStringUnterminated = "chuỗi chưa đóng dấu nháy";
inline constexpr std::string_view kNativeJsonNumberDigitMissing = "số bị thiếu chữ số";
inline constexpr std::string_view kNativeJsonNumberInvalid = "số không hợp lệ";
inline constexpr std::string_view kNativeJsonFractionMissing = "phần thập phân bị thiếu";
inline constexpr std::string_view kNativeJsonExponentMissing = "số mũ bị thiếu";
inline constexpr std::string_view kNativeJsonNumberOutOfRange = "số vượt phạm vi";
inline constexpr std::string_view kNativeJsonNumberConversionFailed = "số không thể chuyển đổi";
inline constexpr std::string_view kNativeJsonArraySeparatorMissing = "mảng cần dấu phẩy hoặc dấu ]";
inline constexpr std::string_view kNativeJsonObjectColonMissing = "object cần dấu : sau khóa";
inline constexpr std::string_view kNativeJsonObjectSeparatorMissing = "object cần dấu phẩy hoặc dấu }";
inline constexpr std::string_view kNativeJsonEncodeNonFinite = "json tạo: JSON không hỗ trợ NaN hoặc vô cực";
inline constexpr std::string_view kNativeJsonEncodeStringUtf8Invalid = "json tạo: chuỗi phải là UTF-8 hợp lệ";
inline constexpr std::string_view kNativeJsonEncodeObjectUnsupported = "json tạo: không hỗ trợ lớp hoặc đối tượng runtime";
inline constexpr std::string_view kNativeJsonEncodeSelfReference = "json tạo: phát hiện collection tự tham chiếu";
inline constexpr std::string_view kNativeJsonEncodeObjectKeyUtf8Invalid = "json tạo: khóa object phải là UTF-8 hợp lệ";
inline constexpr std::string_view kNativeJsonParseInputUtf8Invalid = "json phân tích: đầu vào phải là UTF-8 hợp lệ";
inline constexpr std::string_view kNativeListIndexMustBeInteger = "chỉ số danh sách phải là số nguyên";
inline constexpr std::string_view kNativeListIndexOutOfRange = "chỉ số danh sách vượt phạm vi";
inline constexpr std::string_view kNativeLengthTypeInvalid = "độ dài chỉ nhận chuỗi, danh sách hoặc bộ";
inline constexpr std::string_view kNativeListSortTypeInvalid = "sắp xếp chỉ hỗ trợ danh sách toàn số hoặc toàn chuỗi";
inline constexpr std::string_view kNativeListSumTypeInvalid = "tổng danh sách chỉ hỗ trợ phần tử số";
inline constexpr std::string_view kNativeListSumFloatOutOfRange = "tổng danh sách vượt phạm vi số thực";
inline constexpr std::string_view kNativeListSumIntegerOutOfRange = "tổng danh sách vượt phạm vi số nguyên";
inline constexpr std::string_view kNativeListEmptyRejected = "{0} không nhận danh sách rỗng";
inline constexpr std::string_view kNativeListUniformSortableRequired = "{0} chỉ hỗ trợ danh sách toàn số hoặc toàn chuỗi";
inline constexpr std::string_view kNativeToTupleTypeInvalid = "thành bộ chỉ nhận danh sách hoặc bộ";
inline constexpr std::string_view kNativeToListTypeInvalid = "thành danh sách chỉ nhận danh sách hoặc bộ";
inline constexpr std::string_view kNativeStringCountNeedleEmpty = "đếm ký tự cần ký tự không rỗng";
inline constexpr std::string_view kNativeStringReplaceNeedleEmpty = "thay thế không nhận chuỗi cần thay rỗng";
inline constexpr std::string_view kNativeStringSliceNegativeRange = "cắt chuỗi không nhận vị trí hoặc độ dài âm";
inline constexpr std::string_view kNativeSecureRandomWindowsFailed = "ngẫu nhiên bảo mật: BCryptGenRandom thất bại";
inline constexpr std::string_view kNativeSecureRandomAppleFailed = "ngẫu nhiên bảo mật: SecRandomCopyBytes thất bại";
inline constexpr std::string_view kNativeSecureRandomLinuxFailed = "ngẫu nhiên bảo mật: RAND_bytes thất bại";
inline constexpr std::string_view kNativeSecureRandomPlatformUnsupported = "ngẫu nhiên bảo mật: nền tảng chưa được hỗ trợ";
inline constexpr std::string_view kNativeSha256DigestSizeInvalid = "băm sha256: BCrypt trả kích thước digest không hợp lệ";
inline constexpr std::string_view kNativeSha256InputTooLarge = "băm sha256: dữ liệu quá lớn";
inline constexpr std::string_view kNativeSha256WindowsFailed = "băm sha256: BCrypt SHA-256 thất bại";
inline constexpr std::string_view kNativeSha256AppleFailed = "băm sha256: CommonCrypto SHA-256 thất bại";
inline constexpr std::string_view kNativeSha256LinuxFailed = "băm sha256: OpenSSL SHA-256 thất bại";
inline constexpr std::string_view kNativeSha256PlatformUnsupported = "băm sha256: nền tảng chưa được hỗ trợ";
inline constexpr std::string_view kNativeHmacSha256InputTooLarge = "hmac sha256: dữ liệu quá lớn";
inline constexpr std::string_view kNativeHmacSha256DigestSizeInvalid = "hmac sha256: BCrypt trả kích thước digest không hợp lệ";
inline constexpr std::string_view kNativeHmacSha256WindowsFailed = "hmac sha256: BCrypt HMAC thất bại";
inline constexpr std::string_view kNativeHmacSha256KeyTooLarge = "hmac sha256: khóa quá lớn";
inline constexpr std::string_view kNativeHmacSha256LinuxFailed = "hmac sha256: OpenSSL HMAC thất bại";
inline constexpr std::string_view kNativeHmacSha256PlatformUnsupported = "hmac sha256: nền tảng chưa được hỗ trợ";
inline constexpr std::string_view kNativeToIntegerConversionFailed = "thành số nguyên: giá trị không thể chuyển đổi";
inline constexpr std::string_view kNativeToFloatConversionFailed = "thành số thực: giá trị không thể chuyển đổi";
inline constexpr std::string_view kNativeRandomIntBoundsMustBeInteger = "ngẫu nhiên nguyên: giới hạn phải là số nguyên";
inline constexpr std::string_view kNativeRandomIntBoundsInvalid = "ngẫu nhiên nguyên: giới hạn dưới lớn hơn giới hạn trên";
inline constexpr std::string_view kNativeSecureRandomByteCountInvalid = "ngẫu nhiên bảo mật: số byte phải là số nguyên từ 1 đến 4096";
inline constexpr std::string_view kNativeSleepMillisecondsInvalid = "ngủ mili giây: thời lượng phải là số nguyên không âm";
inline constexpr std::string_view kNativeHandleTypeRequired = "{0} chỉ nhận {1}";
inline constexpr std::string_view kNativeHandleTypeRequiredFirstArgument = "{0} chỉ nhận {1} ở đối số đầu tiên";
inline constexpr std::string_view kNativeHandleEmptyInternal = "{0} không thể thao tác trên {1} rỗng nội bộ";
inline constexpr std::string_view kNativeHttpUrlInvalid = "{0}: URL HTTP không hợp lệ: {1}";
inline constexpr std::string_view kNativePathUtf8Invalid = "{0}: đường dẫn UTF-8 không hợp lệ";
inline constexpr std::string_view kNativeStringArgumentRequired = "{0}: {1} phải là chuỗi";
inline constexpr std::string_view kNativeStringArgumentUtf8Invalid = "{0}: {1} phải là UTF-8 hợp lệ";
inline constexpr std::string_view kNativeFilesystemPathUtf8Invalid = "{0}: hệ thống tệp trả về đường dẫn không phải UTF-8";
inline constexpr std::string_view kNativeOperationSystemError = "{0}: {1}";
inline constexpr std::string_view kNativeEnvironmentNameInvalid = "{0}: tên biến môi trường không hợp lệ";
inline constexpr std::string_view kNativeEnvironmentNameUtf8Invalid = "{0}: tên biến môi trường phải là UTF-8 hợp lệ";
inline constexpr std::string_view kNativeStringUtf8Invalid = "{0}: chuỗi phải là UTF-8 hợp lệ";
inline constexpr std::string_view kNativeHttpUrlEmpty = "URL rỗng";
inline constexpr std::string_view kNativeHttpUrlUtf8Invalid = "URL không phải UTF-8 hợp lệ";
inline constexpr std::string_view kNativeHttpUrlWhitespaceInvalid =
    "URL chứa khoảng trắng hoặc ký tự điều khiển; hãy percent-encode trước";
inline constexpr std::string_view kNativeHttpUrlSchemeInvalid =
    "URL phải dùng scheme http:// hoặc https://";
inline constexpr std::string_view kNativeHttpUrlHostMissing = "URL thiếu host";
inline constexpr std::string_view kNativeHttpUrlIpv6HostInvalid = "URL host IPv6 không hợp lệ";
inline constexpr std::string_view kNativeHttpUrlIpv6NeedsBrackets =
    "URL host IPv6 phải đặt trong ngoặc vuông";
inline constexpr std::string_view kNativeHttpUrlPortInvalid = "URL port không hợp lệ";

// Native HTTP client/server diagnostics.
inline constexpr std::string_view kNativeHttpCurlProcessOpenFailed = "{0}: không mở được tiến trình curl";
inline constexpr std::string_view kNativeHttpCurlFailed = "{0}: curl trả về lỗi";
inline constexpr std::string_view kNativeHttpServerWsaStartupFailed = "mang_http_server_open: WSAStartup thất bại";
inline constexpr std::string_view kNativeHttpServerInvalidPort = "mang_http_server_open: cổng không hợp lệ";
inline constexpr std::string_view kNativeHttpServerSocketCreateFailed = "mang_http_server_open: không tạo được ổ cắm mạng (socket)";
inline constexpr std::string_view kNativeHttpServerExclusivePortFailed = "mang_http_server_open: không thể giữ riêng cổng (WSA={0})";
inline constexpr std::string_view kNativeHttpServerBindFailed = "mang_http_server_open: gắn địa chỉ/cổng (bind) thất bại (mã={0})";
inline constexpr std::string_view kNativeHttpServerListenFailed = "mang_http_server_open: lắng nghe kết nối (listen) thất bại";
inline constexpr std::string_view kNativeHttpServerNotFound = "mang_http_server_next: máy chủ không tồn tại";
inline constexpr std::string_view kNativeHttpRequestNotFound = "{0}: yêu cầu không tồn tại";
inline constexpr std::string_view kNativeHttpRequestFieldInvalid = "mang_http_req_field: trường yêu cầu không hợp lệ";
inline constexpr std::string_view kNativeHttpResponseSendFailed = "mang_http_server_send: gửi phản hồi thất bại";
inline constexpr std::string_view kNativeHttpServerListening = "[HTTP] máy chủ mức thấp đang lắng nghe tại 0.0.0.0:{0}";

// These are result payloads rather than thrown diagnostics.  Keep their text
// byte-for-byte compatible with programs that inspect DB_OK| and DB_ERR|.
inline constexpr std::string_view kNativeDbErrorResult = "DB_ERR|{0}";
inline constexpr std::string_view kNativeDbConnectedResult = "DB_OK|connected";
inline constexpr std::string_view kNativeDbAffectedOneResult = "DB_OK|affected=1";
inline constexpr std::string_view kNativeDbQueryResult = "DB_OK|{0}";


} // namespace vietvm::messages
