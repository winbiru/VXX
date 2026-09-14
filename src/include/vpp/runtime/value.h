#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "vpp/core/message_constants.h"

namespace vietvm::runtime {

// Runtime values are deliberately independent from VM execution and bytecode.
// Native adapters may include this header without pulling in VM internals.
using ScalarValue = std::variant<int, double, std::string, std::monostate>;
// Biểu diễn map runtime bằng container khóa–giá trị `StackValue`; giá trị được chia sẻ qua handle để mutation và identity hoạt động đúng.
struct MapValue;
// Biểu diễn list runtime bằng vector `StackValue`; handle chia sẻ cho phép native mutation thay đổi cùng đối tượng mà các tham chiếu khác đang giữ.
struct ListValue;
// Biểu diễn tuple runtime bằng dãy `StackValue`; cấu trúc giữ thứ tự phần tử và được đối xử như collection giá trị theo quy tắc runtime.
struct TupleValue;
// Lưu tên lớp, superclass và method table tại runtime; VM dùng chuỗi superclass để dispatch override và `gốc.method()`.
struct RuntimeClass;
// Lưu class handle và field map riêng của một object; method dispatch dùng class còn đọc/ghi thuộc tính thao tác trên field map này.
struct RuntimeInstance;
// Ô nhớ chia sẻ cho biến bị closure capture; nhiều frame/closure cùng giữ handle này để mutation nhìn thấy cùng một giá trị.
struct RuntimeCell;
// Giá trị hàm đóng gói function id cùng các ô nhớ đã capture để closure sống độc lập với frame tạo ra nó.
struct RuntimeClosure;
using MapHandle = std::shared_ptr<MapValue>;
using ListHandle = std::shared_ptr<ListValue>;
using TupleHandle = std::shared_ptr<TupleValue>;
using ClassHandle = std::shared_ptr<RuntimeClass>;
using InstanceHandle = std::shared_ptr<RuntimeInstance>;
using CellHandle = std::shared_ptr<RuntimeCell>;
using ClosureHandle = std::shared_ptr<RuntimeClosure>;
using StackValue = std::variant<int, double, std::string, std::monostate,
                                MapHandle, ListHandle, TupleHandle,
                                ClassHandle, InstanceHandle, ClosureHandle>;

// Đăng ký allocation vào heap của VM đang hoạt động; các overload được định
// nghĩa trong runtime heap và là no-op khi value được tạo ngoài một VM run.
void trackRuntimeAllocation(const MapHandle &value);
void trackRuntimeAllocation(const ListHandle &value);
void trackRuntimeAllocation(const TupleHandle &value);
void trackRuntimeAllocation(const ClassHandle &value);
void trackRuntimeAllocation(const InstanceHandle &value);
void trackRuntimeAllocation(const ClosureHandle &value);

// Biểu diễn mức truy cập method tại runtime; VM dùng metadata này để chặn lời gọi
// private/protected cả khi kiểu receiver không thể suy luận tĩnh ở semantic.
enum class RuntimeMemberVisibility {
    Public,
    Private,
    Protected,
};

// Lưu function id và visibility của một method runtime; method table giữ record
// này thay vì chỉ giữ id để dispatch và access check dùng chung một nguồn dữ liệu.
struct RuntimeMethod {
    int functionId = -1;
    RuntimeMemberVisibility visibility = RuntimeMemberVisibility::Public;
};

// Biểu diễn map runtime bằng container khóa–giá trị `StackValue`; giá trị được chia sẻ qua handle để mutation và identity hoạt động đúng.
struct MapValue {
    std::map<std::string, StackValue> entries;
};

// Biểu diễn list runtime bằng vector `StackValue`; handle chia sẻ cho phép native mutation thay đổi cùng đối tượng mà các tham chiếu khác đang giữ.
struct ListValue {
    std::vector<StackValue> elements;
};

// Biểu diễn tuple runtime bằng dãy `StackValue`; cấu trúc giữ thứ tự phần tử và được đối xử như collection giá trị theo quy tắc runtime.
struct TupleValue {
    std::vector<StackValue> elements;
};

// Lưu tên lớp, superclass và method table tại runtime; VM dùng chuỗi superclass để dispatch override và `gốc.method()`.
struct RuntimeClass {
    std::string name;
    ClassHandle superclass;
    std::unordered_map<std::string, RuntimeMethod> methods;
};

// Lưu class handle và field map riêng của một object; method dispatch dùng class còn đọc/ghi thuộc tính thao tác trên field map này.
struct RuntimeInstance {
    ClassHandle klass;
    std::unordered_map<std::string, StackValue> fields;
};

// Chứa một StackValue có lifetime độc lập với call frame; closure capture theo tham chiếu bằng cách chia sẻ `CellHandle` này.
struct RuntimeCell {
    StackValue value{std::monostate{}};
};

// Chứa đích function và bảng slot → cell đã capture. Slot giữ nguyên id bytecode để lambda body đọc/ghi qua cơ chế biến hiện có.
struct RuntimeClosure {
    int functionId = -1;
    std::unordered_map<int, CellHandle> captures;
};

// Kiểm tra điều kiện của `isNumeric`.
inline bool isNumeric(const StackValue &value) {
    return std::holds_alternative<int>(value) || std::holds_alternative<double>(value);
}

// Định dạng runtime số thực; hàm chuyển dữ liệu đầu vào thành biểu diễn chuỗi ổn định để hiển thị hoặc ghi log.
inline std::string formatRuntimeFloat(double number) {
    std::ostringstream out;
    if (number == std::floor(number) && !std::isinf(number)) {
        out << std::fixed;
        out.precision(1);
    } else {
        out.precision(10);
    }
    out << number;
    return out.str();
}

// Chuyển scalar runtime thành chuỗi hiển thị; hàm xử lý số, chuỗi và null mà không đi sâu vào collection.
inline std::string scalar_to_string(const ScalarValue &value) {
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return formatRuntimeFloat(std::get<double>(value));
    if (std::holds_alternative<std::string>(value)) {
        return std::string("\"") + std::get<std::string>(value) + "\"";
    }
    return "rỗng";
}

// Chuyển double; hàm chuyển giá trị đầu vào sang kiểu/biểu diễn đích và trả kết quả đã chuẩn hóa.
inline double toDouble(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return static_cast<double>(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return std::get<double>(value);
    throw std::runtime_error(std::string(vietvm::messages::kRuntimeValueNotNumeric));
}

// Chuyển một `StackValue` thành điều kiện luận lý thống nhất của V++. Số 0,
// chuỗi rỗng, `rỗng`, collection rỗng và handle null là sai; các giá trị còn
// lại là đúng. Helper này là nguồn contract chung cho nhánh, `!`, `&&` và `||`.
inline bool stackValueTruthy(const StackValue &value) {
    if (std::holds_alternative<int>(value)) return std::get<int>(value) != 0;
    if (std::holds_alternative<double>(value)) return std::get<double>(value) != 0.0;
    if (std::holds_alternative<std::string>(value)) {
        return !std::get<std::string>(value).empty();
    }
    if (std::holds_alternative<std::monostate>(value)) return false;
    if (std::holds_alternative<MapHandle>(value)) {
        const MapHandle &map = std::get<MapHandle>(value);
        return map != nullptr && !map->entries.empty();
    }
    if (std::holds_alternative<ListHandle>(value)) {
        const ListHandle &list = std::get<ListHandle>(value);
        return list != nullptr && !list->elements.empty();
    }
    if (std::holds_alternative<TupleHandle>(value)) {
        const TupleHandle &tuple = std::get<TupleHandle>(value);
        return tuple != nullptr && !tuple->elements.empty();
    }
    if (std::holds_alternative<ClassHandle>(value)) {
        return std::get<ClassHandle>(value) != nullptr;
    }
    if (std::holds_alternative<InstanceHandle>(value)) {
        return std::get<InstanceHandle>(value) != nullptr;
    }
    return std::get<ClosureHandle>(value) != nullptr;
}

// So sánh hai `StackValue` theo ngữ nghĩa equality của runtime; scalar so theo giá trị còn collection/object dùng identity hoặc quy tắc riêng.
inline bool sameStackValue(const StackValue &left, const StackValue &right) {
    if (isNumeric(left) && isNumeric(right)) return toDouble(left) == toDouble(right);
    if (left.index() != right.index()) return false;
    if (std::holds_alternative<std::string>(left)) {
        return std::get<std::string>(left) == std::get<std::string>(right);
    }
    if (std::holds_alternative<std::monostate>(left)) return true;
    if (std::holds_alternative<MapHandle>(left)) {
        return std::get<MapHandle>(left) == std::get<MapHandle>(right);
    }
    if (std::holds_alternative<ListHandle>(left)) {
        return std::get<ListHandle>(left) == std::get<ListHandle>(right);
    }
    if (std::holds_alternative<TupleHandle>(left)) {
        return std::get<TupleHandle>(left) == std::get<TupleHandle>(right);
    }
    if (std::holds_alternative<ClassHandle>(left)) {
        return std::get<ClassHandle>(left) == std::get<ClassHandle>(right);
    }
    if (std::holds_alternative<InstanceHandle>(left)) {
        return std::get<InstanceHandle>(left) == std::get<InstanceHandle>(right);
    }
    if (std::holds_alternative<ClosureHandle>(left)) {
        return std::get<ClosureHandle>(left) == std::get<ClosureHandle>(right);
    }
    return false;
}

// So sánh thứ tự hai `StackValue` dùng cho sort; hàm chỉ cho phép các cặp kiểu có thứ tự xác định và áp dụng quy tắc numeric/string tương ứng.
inline bool stackValueLess(const StackValue &left,
                           const StackValue &right,
                           bool &comparable) {
    comparable = true;
    if (isNumeric(left) && isNumeric(right)) return toDouble(left) < toDouble(right);
    if (std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right)) {
        return std::get<std::string>(left) < std::get<std::string>(right);
    }
    comparable = false;
    return false;
}

namespace detail {

// Dựng chuỗi biểu diễn `StackValue`; hàm đi đệ quy qua collection và dùng tập active để ngăn vòng tham chiếu vô hạn.
inline std::string stackValueToString(
    const StackValue &value,
    std::unordered_set<const void *> &activeCollections) {
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) return formatRuntimeFloat(std::get<double>(value));
    if (std::holds_alternative<std::string>(value)) return std::get<std::string>(value);
    if (std::holds_alternative<std::monostate>(value)) return "rỗng";

    if (std::holds_alternative<ListHandle>(value)) {
        const ListHandle &list = std::get<ListHandle>(value);
        if (list != nullptr && !activeCollections.insert(list.get()).second) {
            return "<cycle>";
        }
        std::ostringstream out;
        out << "[";
        if (list != nullptr) {
            for (std::size_t index = 0; index < list->elements.size(); ++index) {
                if (index != 0) out << ", ";
                out << stackValueToString(list->elements[index], activeCollections);
            }
            activeCollections.erase(list.get());
        }
        out << "]";
        return out.str();
    }

    if (std::holds_alternative<TupleHandle>(value)) {
        const TupleHandle &tuple = std::get<TupleHandle>(value);
        if (tuple != nullptr && !activeCollections.insert(tuple.get()).second) {
            return "<cycle>";
        }
        std::ostringstream out;
        out << "(";
        if (tuple != nullptr) {
            for (std::size_t index = 0; index < tuple->elements.size(); ++index) {
                if (index != 0) out << ", ";
                out << stackValueToString(tuple->elements[index], activeCollections);
            }
            if (tuple->elements.size() == 1) out << ",";
            activeCollections.erase(tuple.get());
        }
        out << ")";
        return out.str();
    }

    if (std::holds_alternative<ClassHandle>(value)) {
        const ClassHandle &klass = std::get<ClassHandle>(value);
        return klass == nullptr ? "<class>" : "<class " + klass->name + ">";
    }

    if (std::holds_alternative<InstanceHandle>(value)) {
        const InstanceHandle &instance = std::get<InstanceHandle>(value);
        if (instance == nullptr || instance->klass == nullptr) return "<instance>";
        return "<instance " + instance->klass->name + ">";
    }

    if (std::holds_alternative<ClosureHandle>(value)) {
        const ClosureHandle &closure = std::get<ClosureHandle>(value);
        return closure == nullptr ? "<closure>"
                                  : "<closure " + std::to_string(closure->functionId) + ">";
    }

    const MapHandle &map = std::get<MapHandle>(value);
    if (map != nullptr && !activeCollections.insert(map.get()).second) {
        return "<cycle>";
    }
    std::ostringstream out;
    out << "{";
    bool first = true;
    if (map != nullptr) {
        for (const auto &entry : map->entries) {
            if (!first) out << ", ";
            first = false;
            out << "\"" << entry.first << "\": ";
            if (std::holds_alternative<std::string>(entry.second)) {
                // Maps retain their object-like representation for scalar
                // strings, while recursive collections continue through the
                // general StackValue renderer.
                out << "\"" << std::get<std::string>(entry.second) << "\"";
            } else {
                out << stackValueToString(entry.second, activeCollections);
            }
        }
        activeCollections.erase(map.get());
    }
    out << "}";
    return out.str();
}

} // namespace detail

// Các tập hợp là giá trị tham chiếu và có thể tạo chu trình qua thao tác native
// (`thêm(ds, ds)` hoặc `đặt map(m, "self", m)`). Giữ tập phần tử đang được dựng
// chuỗi cho từng lần hiển thị để tránh lặp vô hạn, đồng thời vẫn cho phép các
// tham chiếu lặp nhưng không tạo chu trình được hiển thị bình thường.
inline std::string sv_to_string(const StackValue &value) {
    std::unordered_set<const void *> activeCollections;
    return detail::stackValueToString(value, activeCollections);
}

// Tạo số nguyên giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_int_value(int value) { return StackValue(value); }
// Tạo số thực giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_float_value(double value) { return StackValue(value); }
// Tạo chuỗi giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_string_value(const std::string &value) { return StackValue(value); }
// Tạo null giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_null_value() { return StackValue(std::monostate{}); }
// Tạo ánh xạ giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_map_value(MapValue value) {
    MapHandle handle = std::make_shared<MapValue>(std::move(value));
    trackRuntimeAllocation(handle);
    return StackValue(std::move(handle));
}
// Tạo danh sách giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_list_value(std::vector<StackValue> value) {
    ListHandle handle = std::make_shared<ListValue>(ListValue{std::move(value)});
    trackRuntimeAllocation(handle);
    return StackValue(std::move(handle));
}
// Tạo tuple giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_tuple_value(std::vector<StackValue> value) {
    TupleHandle handle = std::make_shared<TupleValue>(TupleValue{std::move(value)});
    trackRuntimeAllocation(handle);
    return StackValue(std::move(handle));
}
// Tạo lớp giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_class_value(ClassHandle value) {
    return StackValue(std::move(value));
}
// Tạo đối tượng giá trị; hàm dựng giá trị mới từ đầu vào theo định dạng runtime và trả kết quả cho caller.
inline StackValue make_instance_value(InstanceHandle value) {
    return StackValue(std::move(value));
}
// Tạo closure value và đăng ký vào runtime heap để GC có thể lần theo/cắt các cycle đi qua capture cell.
inline StackValue make_closure_value(ClosureHandle value) {
    trackRuntimeAllocation(value);
    return StackValue(std::move(value));
}

} // namespace vietvm::runtime

// Compatibility aliases keep the existing VM/native implementation source
// stable while callers migrate to vietvm::runtime::*.
using ScalarValue = vietvm::runtime::ScalarValue;
using MapValue = vietvm::runtime::MapValue;
using MapHandle = vietvm::runtime::MapHandle;
using TupleHandle = vietvm::runtime::TupleHandle;
using ListHandle = vietvm::runtime::ListHandle;
using ClassHandle = vietvm::runtime::ClassHandle;
using InstanceHandle = vietvm::runtime::InstanceHandle;
using CellHandle = vietvm::runtime::CellHandle;
using ClosureHandle = vietvm::runtime::ClosureHandle;
using StackValue = vietvm::runtime::StackValue;
using vietvm::runtime::isNumeric;
using vietvm::runtime::formatRuntimeFloat;
using vietvm::runtime::make_float_value;
using vietvm::runtime::make_int_value;
using vietvm::runtime::make_map_value;
using vietvm::runtime::make_list_value;
using vietvm::runtime::make_tuple_value;
using vietvm::runtime::make_class_value;
using vietvm::runtime::make_instance_value;
using vietvm::runtime::make_closure_value;
using vietvm::runtime::make_null_value;
using vietvm::runtime::make_string_value;
using vietvm::runtime::scalar_to_string;
using vietvm::runtime::sameStackValue;
using vietvm::runtime::stackValueLess;
using vietvm::runtime::stackValueTruthy;
using vietvm::runtime::sv_to_string;
using vietvm::runtime::toDouble;
