#include "vpp/runtime/object.h"

#include "vpp/runtime/heap.h"

#include <unordered_set>
#include <utility>

namespace vietvm::runtime {

// Tạo `RuntimeClass` mới với tên và lớp cha tùy chọn; method table ban đầu rỗng và được bổ sung qua `defineMethod`.
ClassHandle createClass(std::string name, ClassHandle superclass) {
    auto klass = std::make_shared<RuntimeClass>();
    klass->name = std::move(name);
    klass->superclass = std::move(superclass);
    trackRuntimeAllocation(klass);
    return klass;
}

// Đăng ký function id cho một phương thức trên `RuntimeClass`; dữ liệu được lưu trong method table của chính lớp đó.
bool defineMethod(const ClassHandle &klass,
                  std::string name,
                  int functionId,
                  RuntimeMemberVisibility visibility) {
    if (klass == nullptr || name.empty() || functionId < 0) return false;
    klass->methods[std::move(name)] = RuntimeMethod{functionId, visibility};
    return true;
}

// Tìm phương thức theo chuỗi kế thừa; bắt đầu từ lớp được chỉ định và đi dần qua superclass cho tới khi tìm thấy định nghĩa.
std::optional<ResolvedMethod> resolveMethod(const ClassHandle &klass,
                                            std::string_view name) {
    std::unordered_set<const RuntimeClass *> visited;
    for (ClassHandle current = klass; current != nullptr; current = current->superclass) {
        if (!visited.insert(current.get()).second) return std::nullopt;
        const auto found = current->methods.find(std::string(name));
        if (found != current->methods.end()) {
            return ResolvedMethod{
                found->second.functionId,
                current,
                found->second.visibility,
            };
        }
    }
    return std::nullopt;
}

// Tra function id của phương thức trên lớp/chuỗi lớp cha và trả về rỗng nếu không tồn tại.
std::optional<int> lookupMethod(const ClassHandle &klass, std::string_view name) {
    const auto resolved = resolveMethod(klass, name);
    if (!resolved.has_value()) return std::nullopt;
    return resolved->functionId;
}

// Kiểm tra điều kiện của `isSubclassOf`.
bool isSubclassOf(const ClassHandle &klass, const ClassHandle &candidateBase) {
    if (klass == nullptr || candidateBase == nullptr) return false;
    std::unordered_set<const RuntimeClass *> visited;
    for (ClassHandle current = klass; current != nullptr; current = current->superclass) {
        if (!visited.insert(current.get()).second) return false;
        if (current == candidateBase) return true;
    }
    return false;
}

// Tạo instance gắn với một `RuntimeClass`; vùng field của instance bắt đầu rỗng và được cập nhật khi chạy chương trình.
InstanceHandle createInstance(ClassHandle klass) {
    if (klass == nullptr) return nullptr;
    auto instance = std::make_shared<RuntimeInstance>();
    instance->klass = std::move(klass);
    trackRuntimeAllocation(instance);
    return instance;
}

// Ghi một field vào instance theo tên; giá trị được lưu trong map field riêng của từng instance.
bool setInstanceField(const InstanceHandle &instance,
                      std::string name,
                      StackValue value) {
    if (instance == nullptr || name.empty()) return false;
    instance->fields[std::move(name)] = std::move(value);
    return true;
}

// Đọc field theo tên từ instance và trả về trạng thái không có giá trị nếu field chưa được gán.
std::optional<StackValue> getInstanceField(const InstanceHandle &instance,
                                           std::string_view name) {
    if (instance == nullptr) return std::nullopt;
    const auto found = instance->fields.find(std::string(name));
    if (found == instance->fields.end()) return std::nullopt;
    return found->second;
}

// Kiểm tra điều kiện của `hasInstanceField`.
bool hasInstanceField(const InstanceHandle &instance, std::string_view name) {
    if (instance == nullptr) return false;
    return instance->fields.find(std::string(name)) != instance->fields.end();
}

} // namespace vietvm::runtime
