#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "vpp/runtime/value.h"

namespace vietvm::runtime {

// Mang kết quả tìm phương thức gồm function id và lớp thực sự định nghĩa method; VM cần defining class để `gốc` bắt đầu lookup đúng superclass.
struct ResolvedMethod {
    int functionId = -1;
    ClassHandle owner;
    RuntimeMemberVisibility visibility = RuntimeMemberVisibility::Public;
};

// Tạo `RuntimeClass` mới với tên và lớp cha tùy chọn; method table ban đầu rỗng và được bổ sung qua `defineMethod`.
ClassHandle createClass(std::string name, ClassHandle superclass = nullptr);
// Đăng ký function id cho một phương thức trên `RuntimeClass`; dữ liệu được lưu trong method table của chính lớp đó.
bool defineMethod(
    const ClassHandle &klass,
    std::string name,
    int functionId,
    RuntimeMemberVisibility visibility = RuntimeMemberVisibility::Public);
// Tìm phương thức theo chuỗi kế thừa; bắt đầu từ lớp được chỉ định và đi dần qua superclass cho tới khi tìm thấy định nghĩa.
std::optional<ResolvedMethod> resolveMethod(const ClassHandle &klass,
                                            std::string_view name);
// Tra function id của phương thức trên lớp/chuỗi lớp cha và trả về rỗng nếu không tồn tại.
std::optional<int> lookupMethod(const ClassHandle &klass, std::string_view name);
// Kiểm tra điều kiện của `isSubclassOf`.
bool isSubclassOf(const ClassHandle &klass, const ClassHandle &candidateBase);

// Tạo instance gắn với một `RuntimeClass`; vùng field của instance bắt đầu rỗng và được cập nhật khi chạy chương trình.
InstanceHandle createInstance(ClassHandle klass);
// Ghi một field vào instance theo tên; giá trị được lưu trong map field riêng của từng instance.
bool setInstanceField(const InstanceHandle &instance,
                      std::string name,
                      StackValue value);
// Đọc field theo tên từ instance và trả về trạng thái không có giá trị nếu field chưa được gán.
std::optional<StackValue> getInstanceField(const InstanceHandle &instance,
                                           std::string_view name);
// Kiểm tra điều kiện của `hasInstanceField`.
bool hasInstanceField(const InstanceHandle &instance, std::string_view name);

} // namespace vietvm::runtime
