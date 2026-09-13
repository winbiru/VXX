#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::runtime {

// Ghi lại kết quả một chu kỳ tracing GC để test, benchmark và tooling có thể
// quan sát số object được theo dõi, đánh dấu và quét mà không phụ thuộc log VM.
struct RuntimeHeapStats {
    std::size_t trackedBefore = 0;
    std::size_t marked = 0;
    std::size_t swept = 0;
    std::size_t trackedAfter = 0;
};

// Theo dõi các allocation dạng tham chiếu của một VM và thực hiện mark/sweep trên
// object graph. Registry chỉ giữ weak_ptr; quyền sở hữu thật vẫn nằm trong StackValue.
class RuntimeHeap {
public:
    // Khi heap owner cuối cùng bị hủy, sweep với root rỗng để cắt mọi cycle còn
    // được giữ bởi graph runtime trước khi weak registry biến mất.
    ~RuntimeHeap();

    // Đăng ký một map allocation với heap hiện tại; hàm loại bỏ entry hết hạn và
    // tránh đăng ký trùng cùng một object.
    void track(const MapHandle &value);
    // Đăng ký một list allocation với heap hiện tại theo cùng contract weak registry.
    void track(const ListHandle &value);
    // Đăng ký một tuple allocation với heap hiện tại theo cùng contract weak registry.
    void track(const TupleHandle &value);
    // Đăng ký metadata lớp runtime để GC có thể đi theo cạnh superclass.
    void track(const ClassHandle &value);
    // Đăng ký instance runtime để GC có thể lần theo class và toàn bộ field của object.
    void track(const InstanceHandle &value);

    // Đăng ký toàn bộ object graph đang nằm dưới một StackValue; helper này dùng
    // khi VM nhận giá trị được tạo ngoài active heap scope hoặc khi test dựng graph tay.
    void trackValue(const StackValue &value);

    // Mark từ danh sách root rồi sweep object không reachable bằng cách cắt các
    // cạnh sở hữu nội bộ; bước này cho phép shared_ptr cycle thực sự được giải phóng.
    RuntimeHeapStats collect(const std::vector<StackValue> &roots);

    // Trả số allocation còn sống trong weak registry sau khi loại entry hết hạn.
    std::size_t trackedObjectCount();

private:
    std::vector<std::weak_ptr<MapValue>> maps_;
    std::vector<std::weak_ptr<ListValue>> lists_;
    std::vector<std::weak_ptr<TupleValue>> tuples_;
    std::vector<std::weak_ptr<RuntimeClass>> classes_;
    std::vector<std::weak_ptr<RuntimeInstance>> instances_;
};

// Gắn một heap làm đích đăng ký allocation trên thread hiện tại trong thời gian
// một VM đang chạy; scope lồng nhau khôi phục heap trước đó khi rời hàm.
class RuntimeHeapScope {
public:
    explicit RuntimeHeapScope(RuntimeHeap &heap) noexcept;
    ~RuntimeHeapScope();

    RuntimeHeapScope(const RuntimeHeapScope &) = delete;
    RuntimeHeapScope &operator=(const RuntimeHeapScope &) = delete;

private:
    RuntimeHeap *previous_ = nullptr;
};

// Các factory trong value/object gọi những overload này sau khi tạo handle; nếu
// không có VM heap scope đang hoạt động thì hàm cố ý không làm gì.
void trackRuntimeAllocation(const MapHandle &value);
void trackRuntimeAllocation(const ListHandle &value);
void trackRuntimeAllocation(const TupleHandle &value);
void trackRuntimeAllocation(const ClassHandle &value);
void trackRuntimeAllocation(const InstanceHandle &value);

} // namespace vietvm::runtime
