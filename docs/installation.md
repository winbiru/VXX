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
cho từng chương trình. Khi gọi chương trình không có đối số, mặc định thử
backend i32 và tự chọn backend động nếu backend i32 không biên dịch được.
Dùng `vpp jit --dynamic "$PWD/hello.vi"` để chọn trực tiếp kiểu động,
số thực, chuỗi, bộ sưu tập, lớp, ngoại lệ và import module. Chế độ gọi hàm
`--function` và `--raw-program` giữ ABI i32. Khi gọi `vpp-jit` trực tiếp
với import thư viện chuẩn, đặt `VPP_HOME` trỏ tới thư mục cài đặt `share/vpp`. `VPP_JIT_PROFILE=1` in thống
kê hàm nóng ra stderr; ngưỡng hiện tại là 32 lần gọi i32 phù hợp.
`VPP_JIT_TIER=0` tắt tầng thích ứng để so sánh baseline. Ảnh dynamic
giới hạn 1 MiB, nên ưu tiên import module hẹp. macOS ARM64 đã được kiểm thử
thực thi dynamic và tiered; Linux và Windows x86-64 còn cần xác nhận thực thi
trên nền tảng đích.

Hoặc dùng `bash scripts/unix/install-vpp.sh "$HOME/.local"`. Lệnh này không sửa shell profile. Thêm `$HOME/.local/bin` vào `PATH` nếu cần.

Hai seed được kiểm tra SHA-256, chữ ký và thư viện động trước khi build. Đọc [bootstrap/README.md](../bootstrap/README.md) để thay seed. Linux/Windows và các dịch vụ của host cũ (VM CLI, LSP, package tooling) cần được chuyển sang native trước khi được phát hành ở nhánh này. Các bản phát hành cũ và tài liệu lịch sử không mô tả cách build của nhánh hiện tại.


## Cài đặt VDK và chọn phiên bản SDK

Giải nén gói `vdk-<version>-<platform>` vào một thư mục riêng và thêm
`bin` vào PATH. Không trộn các tệp từ hai SDK. Kiểm tra:

```sh
vdk trợ giúp
vdk phiên bản
vdk chẩn đoán
vdk thư viện danh sách
vdk thư viện danh sách --json
vdk cài hiển thị
vdk kiểm tra hiển thị
vdk biên dịch hello.vi --đầu-ra hello
vdk chạy hello.vi
vdk chạy tức thời hello.vi
vdk kiểm tra hello.vi
vdk biên-dịch-máy-ảo hello.vi --đầu-ra hello.vmb
vdk gỡ hiển thị
```

VDK chứa compiler, runtime AOT, VM, JIT, thư viện chuẩn, tài liệu, ví dụ,
template dự án và metadata tô màu editor. `vdk-release.json` mô tả layout
SDK; `vdk.json` tại dự án chỉ định phiên bản cần dùng.

```json
{"version": "1.0.0"}
```

VDK tìm pin gần nhất trong các thư mục cha của thư mục làm việc.
Cài nhiều phiên bản bằng cách giải nén mỗi SDK vào
`~/.vpp/vdk/<version>` trên POSIX hoặc
`%LOCALAPPDATA%/VPP/vdk/<version>` trên Windows. Thứ tự chọn:

1. `--thư-mục-vdk <SDK-root>` ở đầu lệnh; phải khớp pin nếu có.
2. Khi có pin, thư mục phiên bản trong kho SDK của người dùng.
3. `VDK_HOME`, sau đó `VPP_HOME` (SDK root hoặc `share/vpp`).
4. SDK chứa launcher đang chạy.

Ứng viên có manifest sai phiên bản sẽ bị bỏ qua; pin sai JSON, thiếu version
hoặc không có SDK khớp sẽ báo lỗi. Ví dụ:

```sh
vdk --thư-mục-vdk "/opt/vdk/1.0.0" phiên bản
vdk --thư-mục-vdk "/opt/vdk/1.0.0" biên dịch hello.vi --đầu-ra hello
```

`doctor` kiểm tra tệp thực thi, thư mục thư viện, runtime và sự đồng nhất
phiên bản trong metadata. Compiler native hiện chỉ in tên compiler khi dùng
`--version`, vì vậy metadata SDK là nguồn phiên bản dùng cho pin.
Các lệnh ngoài nhóm quản lý được chuyển tới compiler/JIT của SDK đã chọn;
phạm vi lệnh phụ thuộc compiler native đi kèm. Các thành phần C/C++ và
toolchain bên ngoài của VDK cũ không được đưa lại vào SDK mới.


### Cấu hình hiển thị tiếng Việt UTF-8

Chạy `vdk cài hiển thị` để tự nguyện lưu cấu hình UTF-8 cho tài khoản hiện tại.
Nếu terminal chưa nhập được dấu, dùng `vdk cai-hien-thi` hoặc `vdk install-display`.
Trên Windows, VDK dùng code page 65001 và cấu hình profile PowerShell 5.1/7
với UTF-8 BOM; trên Linux/macOS chọn locale UTF-8 đã cài, ưu tiên tiếng Việt,
sau đó C.UTF-8 hoặc locale UTF-8 khác. Không cần sudo hay thay đổi toàn hệ thống.
Mở terminal mới sau khi cài hoặc gỡ. `vdk kiểm tra hiển thị` in mẫu tiếng Việt
và trạng thái profile; font terminal vẫn phải hỗ trợ các ký tự tiếng Việt.
`vdk gỡ hiển thị` chỉ loại bỏ khối cấu hình VDK quản lý.

Các lệnh tiếng Anh cũ (`version`, `doctor`, `library list`, `build`, `run`,
`jit`, `lint`, `vm-build`, `--vdk-home`) vẫn tương thích.
Trên Linux/Windows cần clang/LLD phù hợp để biên dịch AOT.


### Gỡ cài đặt V++/VDK

Chạy `vpp gỡ cài đặt` hoặc `vdk gỡ cài đặt` (alias `uninstall`).
Bản đóng gói và bản cài bằng CMake có danh sách tệp SHA-256 tại
`vpp-install-manifest.tsv`. Chỉ tệp đã cài và còn nguyên nội dung được xóa;
tệp đã sửa, dự án riêng và SDK khác được giữ nguyên. Bản cài không có danh
sách này sẽ từ chối gỡ. Nếu cần, chạy `vdk gỡ hiển thị` trước và tự bỏ
đường dẫn SDK tương ứng khỏi PATH.
