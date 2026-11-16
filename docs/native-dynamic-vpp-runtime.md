# Runtime động AOT bằng V++/ASM

Đường mới trên **macOS ARM64**, ngày 01/11/2026, cho phép liên kết chương
trình AOT động với `gói/máy ảo/runtime AOT.vi`. Compiler i32 tự chủ biên dịch
runtime này thành object mã máy. Executable chương trình không liên kết
runtime C++, `libc++`, `libffi`, Security hoặc CoreFoundation.

Đây là đường **thử nghiệm, chọn tường minh** bằng `VPP_AOT_RUNTIME=vpp`.
Mặc định hiện vẫn là runtime C++ để giữ các dịch vụ chưa chuyển xong.

```text
Source → compiler V++ → IR → ASM hàm/nhánh/vòng lặp → mã máy
                                                       ↓
                                      runtime động V++ → primitive ASM → OS
```

Không thay chương trình bằng ảnh bytecode hay chạy qua vòng lặp dispatch.
ASM chỉ đọc bảng metadata, chuyển đổi con trỏ/offset và gọi hàm mã máy.
Giá trị động, cell dùng chung của closure, đối tượng, ngoại lệ và GC dùng
các thuật toán V++ trong `gói/máy ảo`. Không có `vm_step`/`vm_step_body`
trong executable kiểm chứng.

## Dựng và dùng

Cần compiler i32 V++/ASM seed đã dựng trong `build/nativedriver/vpp-native`
và compiler AOT đầy đủ trong `build/native-compiler/vpp-native`.

```sh
./scripts/build-vpp-aot-runtime.sh
VPP_AOT_RUNTIME=vpp \
  ./build/native-compiler/vpp-native build \
  scripts/test/fixtures/native_dynamic_vpp.vi -o build/dynamic-vpp
./build/dynamic-vpp
otool -L build/dynamic-vpp
```

Script nhận tùy chọn `[thư_mục_đầu_ra] [đường_seed]`. Khi dùng thư mục khác,
đặt `VPP_AOT_VPP_OBJECT` thành đường dẫn tuyệt đối tới `runtime.o`.
Script dựng lại compiler i32 từ V++ để có hai intrinsic biên máy mới
ID 76/77 và chế độ `runtime-object`. Chế độ này gộp cả runtime byte/tệp V++
vào object; lệnh `object` thông thường vẫn chỉ xuất object của nguồn.
Clang thực hiện assembly/link; không biên dịch mã nguồn C/C++ ở nhánh này.
Compiler AOT đầy đủ đang dùng để phát chương trình vẫn có runtime host C++.

## Bằng chứng

`vpp-native-dynamic-vpp-runtime` kiểm tra:

- Tràn i32, phép tính float, chuyển kiểu `++`, Unicode và truy cập ký tự.
- List/map có thay đổi, hàm có tham số mặc định, closure giữ cell sau return.
- Khởi tạo đối tượng, đọc/ghi thuộc tính và gọi phương thức.
- Nhánh/vòng lặp, switch, ngoại lệ đi qua hàm mã máy rồi trở lại catch.
- Chu kỳ list và hơn 1 MiB cấp phát; closure, instance và list còn sống qua GC.
- Kết quả trùng golden và runtime C++ độc lập trên cùng nguồn.
- `nm -g` kiểm cả symbol được định nghĩa lẫn import; từ chối C++ và dispatcher.
- `otool -L` từ chối `libc++`, `libstdc++`, `libffi`.
- Cờ compiler-host chưa hỗ trợ phải trả lỗi trước initializer, không fallback.

Hai executable từ `vm_asm.vi` và `native_dynamic_vpp.vi` đã chạy trên macOS
ARM64; dependency còn lại là:

```text
/usr/lib/libSystem.B.dylib
```

Không dùng số benchmark của runtime cũ để mô tả tốc độ đường mới. Các helper
vòng lặp gộp hiện trả về yêu cầu chạy nhánh mã máy thông thường; chưa chuyển
các tối ưu unboxed của runtime C++.

## Linux x86-64 freestanding và mốc tự biên dịch

Trên Ubuntu 24.04 x86-64 trong Docker, `VPP_AOT_RUNTIME=vpp-freestanding`
đã chạy ba fixture `hello`, `vm`, `dynamic` bằng executable ELF không còn
dynamic loader, libc, libcrypto hay C++ runtime. Đây là đường Linux riêng,
độc lập với nhánh macOS ARM64 ở trên.

Compiler viết bằng V++ đã tự biên dịch qua nhiều thế hệ: Stage 6 và Stage 7
giống nhau từng byte, chương trình Stage 7 biên dịch mới trả mã thoát 42.
Chi tiết, SHA-256 và lệnh kiểm chứng: [compiler self-hosting](compiler-self-hosting-status.md).
**Bản thân compiler Linux Stage 7 còn gọi `cc` và liên kết
`libcrypto.so.3`, `libc.so.6`, `ld-linux-x86-64.so.2`.**

## Phần còn lại trước self-host 100%

Runtime mới chưa cung cấp descriptor foreign tổng quát, dịch vụ compiler-host
(`--compiler`, thực thi nguồn/biên dịch lồng nhau), worker/thread hoặc chạy
ảnh DNS provider. Các dịch vụ này chưa dùng được; chương trình có descriptor
foreign hoặc cờ compiler-host trả `error=22` trước initializer.
Không suy ra toàn bộ thư viện chuẩn hoặc compiler đầy đủ đã chạy được trên
runtime mới. Chưa có Stage 2 → Stage 3 của compiler AOT đầy đủ với runtime này.
ABI Linux x86-64 có nhánh freestanding đã thử nghiệm riêng; Windows và macOS
x86-64 chưa tích hợp/kiểm chứng tương đương.

`libSystem` còn cung cấp mmap, I/O, process/socket, đồng hồ, SHA và chuyển đổi
float ở các primitive hiện có. Ngoài việc thay các primitive này, đường
Mach-O động tiêu chuẩn phải tuân thủ bộ nạp Apple: [mã nguồn dyld của Apple](https://github.com/apple-oss-distributions/dyld/blob/main/common/MachOAnalyzer.cpp)
kiểm tra `LC_LOAD_DYLIB` và yêu cầu liên kết ít nhất `libSystem.dylib`.
Vì vậy mục tiêu executable không còn thư viện hệ thống cần một đích và ABI
riêng, chẳng hạn ELF Linux dùng syscall trực tiếp. Không thể đạt mục tiêu đó
chỉ bằng cách xóa `-lSystem` trên đường macOS hiện tại.
Khảo sát [macOS freestanding](native-freestanding-macos.md) trước đó của repo
còn ghi nhận static ARM64 bị kernel release từ chối và dyld nạp `libSystem`
ngay cả khi probe không khai báo dylib; việc xem `otool -L` trống chưa đủ.

Thứ tự tiếp tục: chuyển compiler-host và các primitive còn thiếu sang V++,
đối chiếu corpus toàn ngôn ngữ, tự biên dịch AOT Stage 2/3 và so byte,
rồi hoàn thiện đích ELF/syscall cùng kiểm toán cả symbol lẫn dependency.
