# Đồ họa V++

- `chính.vi`: HTML/SVG và thành phần giao diện.
- `macos.vi`: cửa sổ Cocoa/WKWebView (HTML).
- `desktop.vi`: cửa sổ SDL2 trên Windows/Linux x86-64, hiển thị BMP24,
  thu sự kiện bàn phím và nút chuột. Mã phím SDL là số, tọa độ theo cửa sổ.
- `gpu.vi`: xóa màu và đọc điểm ảnh RGBA8 bằng OpenGL, Vulkan hoặc Metal.

## Linux / Windows x86-64

Cài CMake, GCC/Clang, SDL2 SDK, Vulkan SDK/loader và driver phù hợp.
Trên Windows dùng MinGW-w64 (UCRT64), giữ SDL2.dll và Vulkan loader trong
đường tìm DLL. Có thể tắt từng backend bằng `-DVPP_GRAPHICS_SDL=OFF`
hoặc `-DVPP_GRAPHICS_VULKAN=OFF`.

```sh
cmake -S scripts/graphics -B build-graphics
cmake --build build-graphics
ctest --test-dir build-graphics --output-on-failure
./VPP run examples/do-hoa/desktop.vi
./VPP run examples/do-hoa/gpu.vi
```

Trên Windows thay `./VPP` bằng launcher V++ đã cài. Trên Linux không có màn
hình thật, có thể kiểm tra SDL/OpenGL bằng Xvfb và Mesa, Vulkan bằng
driver phần mềm của Mesa. CTest kiểm tra giá trị đọc lại thực tế.

Nếu dùng SDK đã cài, thay `scripts/graphics` bằng đường dẫn
`<prefix>/share/vpp/scripts/graphics` trong lệnh CMake. Có thể cài helper
vào SDK bằng `cmake --install build-graphics --prefix <prefix>` với
`<prefix>` là thư mục cài đặt có quyền ghi. Truyền đường dẫn helper đã cài
trong `bin` vào API V++.

## macOS ARM64

```sh
cmake --build build-pure-vpp --target vpp-graphics
./VPP run examples/do-hoa/macos.vi
./VPP run examples/do-hoa/gpu.vi
```

Có thể chạy ví dụ cửa sổ trực tiếp từ mã nguồn bằng JIT động:

```sh
./VPP jit --dynamic "$PWD/examples/do-hoa/macos.vi"
```

Chế độ JIT động hỗ trợ nhập gói và lớp. Chế độ JIT mặc định chỉ xử lý
tập con hàm cấp chương trình. JIT vẫn cần thời gian biên dịch mã nguồn
trong bộ nhớ trước khi mở cửa sổ.

Để chạy CTest Metal cần thiết bị Metal khả dụng và cấu hình CMake với
`-DVPP_GRAPHICS_GPU_TESTS=ON`. Helper trả lỗi nếu không lấy được GPU.

## Phạm vi hiện tại

Các helper là executable riêng, liên kết API nền tảng qua ASM. Phần quản lý
tiến trình, JSON sự kiện, màu và BMP nằm trong V++. Windows/Linux hiện
hiển thị ảnh BMP tĩnh, chưa dựng thành phần HTML như WKWebView. GPU API
hiện có thao tác offscreen xóa màu và đọc điểm ảnh, chưa có context tồn tại
qua nhiều lời gọi, shader, mesh, swapchain hay bộ API đầy đủ. OpenGL và
Vulkan hiện nhắm Linux/Windows x86-64, Metal nhắm macOS ARM64.
