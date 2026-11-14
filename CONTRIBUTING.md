# Đóng góp cho V++

Cảm ơn bạn đã muốn đóng góp cho V++ — compiler và virtual machine thử nghiệm cho ngôn ngữ lập trình Việt hoá.

## Chuẩn bị môi trường

- macOS ARM64 (Apple Silicon) cho bootstrap đầy đủ hiện tại
- CMake 3.20+, Bash và Xcode Command Line Tools để build Assembly nền tảng
- Python 3 cho kiểm tra mã nguồn và benchmark; Node.js cho kiểm thử extension
- Seed V++/ASM trong `bootstrap/macos-arm64/`, được xác minh khi build

## Build và kiểm thử

Build nhanh cho môi trường phát triển:

```bash
./scripts/macos/build-vpp-cli.sh
./run_tests.sh
```

Hoặc build qua CMake:

```bash
cmake -S . -B build-pure-vpp
cmake --build build-pure-vpp
ctest --test-dir build-pure-vpp --output-on-failure
```

`run_tests.sh` chạy bộ smoke test AOT hiện tại. Các fixture hồi quy cũ vẫn được giữ để chuyển sang compiler native. Linux, Windows và các lệnh của CLI host cũ chưa có hỗ trợ đầy đủ trong bootstrap này.
Corpus kiểm thử nằm trong `scripts/test/`; các script dành riêng cho từng nền tảng được đặt trong `scripts/macos/`, `scripts/linux/` và `scripts/windows/`.

## Khi thay đổi mã nguồn

1. Viết compiler, runtime và thư viện bằng V++; dùng Assembly tại biên giao tiếp với nền tảng. Không đưa mã nguồn hoặc host C/C++ trở lại quy trình build sản phẩm.
2. Với thay đổi ngôn ngữ/VM/compiler, thêm hoặc cập nhật một entry `.vi` trong `scripts/test/` cùng output tương ứng trong `scripts/test/expected/`. Fixture tái sử dụng đặt trong `scripts/test/fixtures/` không cần expected riêng.
3. Với thay đổi HTTP, ưu tiên mở rộng fixture `.vi` thay vì gọi dịch vụ Internet.
4. Mỗi module `.vi` phải import trực tiếp dependency của chính nó; không dựa vào thứ tự import của facade. Giữ `gói/ứng dụng` ở mức lifecycle chung, còn full stack nằm ở `gói/dựng/ứng dụng.vi`. Mỗi câu `nhập` chỉ nhận một file hoặc package/folder và target không dùng dấu nháy, ví dụ `nhập gói.mạng.chính.*;`.
5. Cập nhật `README.md`, `CHECKLIST.md` hoặc tài liệu trong `docs/` nếu hành vi công khai thay đổi.
6. Chạy `./run_tests.sh` trước khi gửi pull request.
7. Đặt application mẫu trong `examples/` và scaffold source trong `templates/`, không đặt chúng trong `scripts/test/`.

## Pull request

- Mỗi PR nên tập trung vào một thay đổi có thể review được.
- Mô tả rõ vấn đề, cách sửa và kết quả kiểm thử.
- Không commit build artifacts (`bin/`, `build/`, `cmake-build-*`) hoặc file tạm của test.
- Không thay đổi output expected để che lỗi; nêu rõ lý do khi output mong đợi cần cập nhật.

## Báo lỗi

Kèm theo phiên bản `vpp-cli`, hệ điều hành, compiler, lệnh đã chạy, source `.vi` tối giản để tái hiện và output lỗi đầy đủ.
