# Cài đặt V++ (bản V++/ASM tự chủ)

Bản hiện tại hỗ trợ bootstrap compiler đầy đủ trên **macOS ARM64 (Apple Silicon)**. CMake 3.20+, công cụ Xcode Command Line Tools (`xcode-select --install`) và shell Bash là các điều kiện build. Clang dùng để dịch Assembly của nền tảng và ở bước bootstrap seed cũ; không biên dịch C/C++. Compiler sinh ra dùng đường ARM64/Mach-O trực tiếp.

```sh
cmake -S . -B build-pure-vpp
cmake --build build-pure-vpp --parallel
ctest --test-dir build-pure-vpp --output-on-failure
./VPP phiên bản
./VPP build hello.vi -o hello
./hello
./VPP jit hello.vi
```

Cài đặt tại một prefix tùy chọn:

```sh
cmake --install build-pure-vpp --prefix "$HOME/.local"
"$HOME/.local/bin/vpp" phiên bản
"$HOME/.local/bin/vpp" jit hello.vi
```

### JIT native (thử nghiệm)

Target `vpp-jit` được build cùng compiler. Có thể build riêng bằng
`cmake --build build-pure-vpp --target vpp-jit`. Sau khi cài đặt, dùng
`vpp jit hello.vi` hoặc chạy trực tiếp `vpp-jit hello.vi`.
JIT sinh mã máy trong bộ nhớ, không cần assembler/linker hoặc tệp thực thi
cho từng chương trình. Mặc định hỗ trợ tập i32. Dùng
`vpp jit --dynamic "$PWD/hello.vi"` (đường dẫn tuyệt đối) để chạy kiểu động,
chuỗi, bộ sưu tập, lớp, ngoại lệ và import module. Khi gọi `vpp-jit` trực tiếp,
đặt `VPP_HOME` trỏ tới thư mục cài đặt `share/vpp`. `VPP_JIT_PROFILE=1` in thống
kê hàm nóng ra stderr; ngưỡng hiện tại là 32 lần gọi i32 phù hợp.
`VPP_JIT_TIER=0` tắt tầng thích ứng để so sánh baseline. Ảnh dynamic
giới hạn 1 MiB, nên ưu tiên import module hẹp. macOS ARM64 đã được kiểm thử
thực thi dynamic và tiered; Linux và Windows x86-64 còn cần xác nhận thực thi
trên nền tảng đích.

Hoặc dùng `bash scripts/unix/install-vpp.sh "$HOME/.local"`. Lệnh này không sửa shell profile. Thêm `$HOME/.local/bin` vào `PATH` nếu cần.

Hai seed được kiểm tra SHA-256, chữ ký và thư viện động trước khi build. Đọc [bootstrap/README.md](../bootstrap/README.md) để thay seed. Linux/Windows và các dịch vụ của host cũ (VM CLI, LSP, package tooling) cần được chuyển sang native trước khi được phát hành ở nhánh này. Các bản phát hành cũ và tài liệu lịch sử không mô tả cách build của nhánh hiện tại.
