# VM và runtime V++ gọi ASM trực tiếp

`gói/máy ảo/chính.vi` là một executable độc lập do compiler native i32 V++
biên dịch. Heap, mark/sweep GC, scalar/collection, closure/cell, khung gọi,
class/instance, visibility, switch và try/catch nằm trong các tệp `.vi`
cùng thư mục. Host ABI đọc/ghi tệp, đường dẫn, môi trường và số thực gọi
primitive ASM, không đi qua `StackValue`, callback C++, VM C++ hay libffi.

```sh
cmake --build build --target vpp-vm-asm
cmake --build build --target vpp-native-cli
build/native-cli/vpp-native vm-build source.vi -o program.vmb
build/native-vm/vpp-vm program.vmb
```

Có seed i32 V++ sẵn thì cấu hình `-DVPP_NATIVE_I32_SEED=/absolute/path/to/vpp-native`
để dùng seed đó dựng compiler và VM kế tiếp mà không chạy Stage 0.

Stage 0 tạo compiler i32 ban đầu khi chưa cung cấp seed. Compiler i32 V++ sau đó biên dịch VM,
runtime và host policy thành mã máy, liên kết cùng primitive ASM hiện có.
Đường này hiện nhắm macOS ARM64 và Linux x86-64. Thư viện hệ thống vẫn
cung cấp `mmap`, I/O và chuyển đổi f64 thông qua lệnh gọi ASM trực tiếp.

Backend `gói/biên dịch/máy ảo ASM.vi` xuất ảnh nhị phân có phiên bản,
không đổi các số opcode hiện hành. StringPool giữ byte UTF-8/NUL; các lệnh
giữ ABI bốn i32 little-endian. Bộ nạp kiểm tra header, kích thước, ID hàm,
tham chiếu StringPool và đích nhánh trước/khi sử dụng. Ảnh có byte dư hoặc
thiếu bị từ chối. GC không di chuyển đối tượng; offset arena không phải
con trỏ OS. Heap có giới hạn khoảng 56 MiB, tách khỏi vùng byte/file runtime.

Compiler đầy đủ cũng có entry dùng được trong VM này:

```sh
build/native-cli/vpp-native vm-build 'gói/biên dịch/dòng lệnh VM.vi' -o compiler.vmb
VPP_HOME="$PWD" build/native-vm/vpp-vm compiler.vmb build source.vi -o program.vmb
```

Đây là đường tự chủ qua ảnh VM. Backend native AOT tổng quát cũ vẫn dùng
runtime C++; việc thêm đường VM ASM chưa chuyển mọi executable AOT sang
runtime mới. Host socket/TLS, process, clock, directory enumeration và
descriptor FFI chưa được triển khai đầy đủ trên đường mới: VM dừng với mã
lỗi và opcode, không chuyển ngầm sang C++. Do đó toàn bộ toolchain chưa
đạt 100% tự chủ. FFI trong ảnh bị backend từ chối trước khi ghi đầu ra.

Kiểm chứng tự động: CTest `vpp-vm-asm` biên dịch VM bằng compiler i32 V++,
chạy ảnh với `PATH` rỗng, đối chiếu ngữ nghĩa runtime, lỗi ảnh hỏng và GC
thu hồi chu trình; kiểm tra symbol để phát hiện phụ thuộc runtime/compiler
C++. Không suy ra tự chủ từ phần mở rộng `.vi` hay chỉ từ output chương trình.

Runtime quản lý bảng ánh xạ theo kiểu khóa: khóa chỉ gồm chuỗi UTF-8 hoặc
số nguyên i32 được sắp thứ tự và tra cứu nhị phân; ánh xạ có khóa hỗn hợp
tiếp tục dùng tìm kiếm tuần tự. Hàm băm định danh chỉ chấp nhận các đối tượng
tham chiếu VM, tương ứng với quy tắc của Stage 0. Regression kiểm tra thứ tự
tra cứu khóa, định danh đối tượng và lỗi khi truyền số nguyên vào hàm băm.

Kiểm thử `vpp-vm-asm-selfhost` là mức xác nhận cao hơn: compiler `.vmb`
phải chạy trên VM mới để tạo lại ảnh compiler với kết quả byte ổn định ở
Stage 2/3. Tại lượt kiểm chứng ngày 27/10/2026, bộ kiểm thử VM/runtime
độc lập đã PASS nhưng bài selfhost chưa hoàn thành: bước tạo compiler
Stage 1 bằng `vpp-native vm-build` hết thời gian sau 180 giây; thử chạy
ảnh Stage 1 đã có trên VM mới cũng vượt ngưỡng quan sát 75 giây. Vì vậy
không coi bootstrap Stage 2/3 là đã được xác nhận trên lượt kiểm thử này.

## Quản lý bộ nhớ và cache chuỗi (27/10/2026)

Heap giữ các offset i32 thay cho con trỏ OS. Mark/sweep duyệt từng
đối tượng reachable qua mark stack liên kết trong header, sau đó quét
heap và gộp khối trống liên tiếp. Allocator sử dụng **10 bucket free-list**
theo tổng kích thước block (gồm header): <=32, <=48, <=64, <=96,
<=128, <=192, <=256, <=512, <=1024 byte và lớn hơn 1024 byte.
Các đầu danh sách nằm ở VM header slot 19..28. Bộ đếm
`free-list-probes` đo số block đã xét, không đếm số bucket trống.

Giá trị số nguyên bất biến -16..255 được intern trong một bảng tại
header slot 17; kết quả `source[i]` ASCII được intern theo byte trong
bảng tại slot 18. `vm_collect()` đánh dấu hai bảng này làm root để
không tạo tham chiếu treo. Do chuỗi có ngữ nghĩa so sánh theo nội dung,
cache ASCII không ảnh hưởng phép so sánh chuỗi; hàm băm định danh vẫn
chỉ áp dụng cho các đối tượng tham chiếu.

Các lời gọi host tĩnh `OP_GOI` với `b < 0` mã hóa StringPool index
theo công thức `-b - 1`. Runtime có fast path cho primitive độ dài
(147, một đối số) và thêm cuối danh sách (148, hai đối số), bỏ qua việc
tạo vector đối số tạm. Lời gọi còn lại dùng đường host chung. Bảng
phân giải tên host đặt tại **state slot 10** (slot 9 đã dùng cho errno),
được cấp phát dạng dữ liệu nguyên không quét tham chiếu GC; 0 nghĩa
chưa phân giải, 1 là tên chưa biết, `primitive_id + 2` là kết quả.
Tất cả giá trị trong bảng được gắn với chỉ số StringPool bất biến.

Đối tượng chuỗi dùng trường thứ tư (word 3) cho cache Unicode:
`-1` là chưa xác định; `-(codepoint_count + 2)` là độ dài đã tính;
giá trị dương trỏ tới bảng offset đã quản lý bằng GC. Với chuỗi từ
128 **byte** trở lên, VM tạo checkpoint mỗi 64 **codepoint** khi cần
`source[i]`, sau đó đọc tối đa 63 codepoint từ checkpoint.
Bảng offset là đối tượng `scan=0` (chỉ chứa số byte offset), được
đánh dấu reachable thông qua đối tượng chuỗi (`scan=1`). Tạo bảng lần
đầu vẫn quét toàn chuỗi; đánh đổi bộ nhớ lấy tốc độ truy cập lặp lại.

## Đo hiệu năng và giới hạn self-hosting

Chạy test runtime đầy đủ và profiler Stage 2:

```sh
cmake -DVPP_STAGE1="$PWD/build/nativedriver/vpp-native" \
  -DVPP_NATIVE_COMPILER="$PWD/build/native-cli/vpp-native" \
  -DVPP_ROOT="$PWD" -DVPP_OUTPUT_DIR="$PWD/build/vm-asm-optimized" \
  -P scripts/quality/check_vm_asm.cmake
python3 scripts/quality/profile_vm_asm_stage2.py --seconds 45 --name local
```

`VPP_VM_PROFILE=1` ghi stderr sau mỗi 250.000 guest instruction,
gồm vị trí function/PC, số allocation, GC và số lượt dò free-list.
`profile_vm_asm_stage2.py` lưu stderr `.log` và số liệu `.json`
trong thư mục build. Timeout của phép đo giới hạn thời gian là dự
kiến và chỉ có nghĩa **Stage 2 chưa hoàn thành trong thời gian đó**.
Bản có fast path và cache tên host quan sát 44,75 triệu guest instruction
trong 45 giây (so với 44 triệu khi chỉ có bucket allocator); mức tăng
nhỏ và mới đo một lần, chưa đủ chứng minh cải thiện hiệu năng ổn định.
Bảng số liệu và giới hạn của phép đo ở `docs/compiler-self-hosting-status.md`.

Stage 2/3 trên VM/ASM phải được kiểm chứng riêng bằng
`scripts/quality/check_vm_asm_selfhost.cmake`: compiler Stage 1
biên dịch chính compiler thành Stage 2; Stage 2 sinh Stage 3 và đối
chiếu byte. Compiler native AOT đã có bootstrap riêng; kết quả đó
không thay thế kiểm chứng compiler chạy qua VM/ASM. Các host ABI
socket/TLS/process/clock, directory và descriptor FFI cùng runtime
AOT tổng quát vẫn là hạng mục cần chuyển tiếp.
