# Đóng góp cho V++

Cảm ơn bạn đã muốn đóng góp cho V++ — compiler và virtual machine thử nghiệm cho ngôn ngữ lập trình Việt hoá.

## Chuẩn bị môi trường

- C++ compiler hỗ trợ C++17 (`c++`, GCC hoặc Clang)
- CMake 3.15+ để build theo cấu hình chính thức
- `curl` cho HTTP native và integration test
- Bash trên Linux/macOS để chạy `run_tests.sh`

## Build và kiểm thử

Build nhanh cho môi trường phát triển:

```bash
./scripts/build-vpp-cli.sh
./run_tests.sh
```

Hoặc build qua CMake:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`run_tests.sh` chạy toàn bộ regression suite. Các test HTTP sử dụng server fixture viết bằng V++ tại `src/tests/http_fixture.vi`, chạy ở `127.0.0.1:18080`; không gọi Internet. Không chạy đồng thời nhiều bản test runner trên cùng máy vì fixture dùng cổng cố định này.

Tạo một backend mẫu để thử nghiệm thủ công bằng `vpp khởi tạo backend <tên>`. Template này dùng HTTP server native trên Linux, macOS và Windows.

## Khi thay đổi mã nguồn

1. Giữ mã tương thích C++17 và không thêm warning mới với `-Wall -Wextra -Wpedantic`.
2. Với thay đổi ngôn ngữ/VM/compiler, thêm hoặc cập nhật một file `.vi` trong `src/tests/` cùng output tương ứng trong `src/tests/expected/`.
3. Với thay đổi HTTP, ưu tiên mở rộng fixture `.vi` thay vì gọi dịch vụ Internet.
4. Cập nhật `README.md`, `CHECKLIST.md` hoặc tài liệu trong `docs/` nếu hành vi công khai thay đổi.
5. Chạy `./run_tests.sh` trước khi gửi pull request.

## Pull request

- Mỗi PR nên tập trung vào một thay đổi có thể review được.
- Mô tả rõ vấn đề, cách sửa và kết quả kiểm thử.
- Không commit build artifacts (`bin/`, `build/`, `cmake-build-*`) hoặc file tạm của test.
- Không thay đổi output expected để che lỗi; nêu rõ lý do khi output mong đợi cần cập nhật.

## Báo lỗi

Kèm theo phiên bản `vpp-cli`, hệ điều hành, compiler, lệnh đã chạy, source `.vi` tối giản để tái hiện và output lỗi đầy đủ.
