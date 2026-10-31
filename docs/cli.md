# CLI V++ 1.0

CLI 1.0 dùng tên lệnh tiếng Việt làm bề mặt canonical. Các alias tiếng Anh như `run`, `test`,
`build`, `new` vẫn được giữ để tương thích script cũ.

## Lệnh chung

```text
vpp giúp đỡ
vpp phiên bản
vpp chẩn đoán
vpp nơi
vpp thống kê
```

Chạy trực tiếp file hoặc dùng lệnh tường minh:

```text
vpp app.vi
vpp chạy app.vi
vpp dựng app.vi
```

`dựng` chạy compiler pipeline và verifier nhưng chưa ghi artifact `.vbc`; format bytecode chỉ
được public sau khi compatibility/versioning policy 1.0 được freeze.

## Khởi tạo project

```text
vpp khởi tạo demo
vpp khởi tạo ứng dụng demo-app
vpp khởi tạo backend demo-api
```

Template `ứng dụng` tạo layout schema 1 có `src/`, `tests/`, `gói/`, README và `.gitignore`.

## Kiểm thử

```text
vpp kiểm thử
vpp kiểm thử tests
vpp kiểm thử tests/hoa_don.vi
```

Khi nhận thư mục, CLI quét đệ quy file `.vi` theo thứ tự xác định, chạy toàn bộ và trả summary.
Chi tiết assertion/setup/teardown nằm trong `docs/testing.md`.

## Formatter và linter

```text
vpp --soát-lỗi src/
vpp --soát-lỗi --compiler=vpp src/chính.vi
vpp --soát-lỗi --compiler=cpp src/chính.vi
vpp --định-dạng src/chính.vi
vpp --định-dạng src/ --kiểm-tra
vpp --định-dạng src/ --ghi-tệp
```

`--soát-lỗi` ưu tiên parser/semantic và trình phân giải import V++ native
khi có compiler đã bootstrap; `--compiler=cpp` giữ đường Stage 0 để so sánh,
`--compiler=vpp` yêu cầu compiler native. Lỗi khai báo trùng trả vị trí
file:dòng:cột theo byte UTF-8. Native lint chưa phát đủ warning và
SourceSpan của mọi lỗi ngữ nghĩa. LSP ưu tiên native lint trên bộ đệm chưa lưu,
phân giải import theo thư mục tệp thật và chuyển vị trí byte sang UTF-16;
không có compiler native thì dùng Stage 0. Native compiler cũng hỗ trợ
`lint <snapshot.vi> --base <directory>` để kiểm tra import của bộ đệm.
Lệnh trả exit code phù hợp CI. Formatter giữ comment và literal, hỗ trợ
check mode và write mode; `--lint`, `--check`, `--in-place` là alias tương thích.

## Quan sát compiler

```text
vpp --giải-mã app.vi
vpp --dump-ast app.vi
vpp --dump-ir app.vi
vpp --dump-ir --compiler=vpp app.vi
vpp --dump-ir --compiler=cpp app.vi
```

`--dump-ast` và `--dump-ir` chỉ in dữ liệu compiler, không thực thi mã nguồn.
Mặc định chúng dùng compiler V++ native đã bootstrap; `--compiler=vpp`
chọn native tường minh, `--compiler=cpp` chọn Stage 0. Hai thế hệ có
schema AST/IR khác nhau. Native `--dump-ir` hiển thị stack IR trước codegen;
import chỉ ghi metadata `(chưa liên kết)` trong bản dump cấp tệp.

## REPL và LSP

```text
vpp --repl
vpp --lsp
```

LSP ưu tiên parser/semantic V++ native cho diagnostics và formatter khi có
compiler đã bootstrap. Completion, definition, hover, rename vẫn dùng tooling
Stage 0. Range diagnostic được đổi sang UTF-16 theo giao thức LSP; một số
lỗi semantic V++ chưa có vị trí chi tiết và warning chưa đạt parity Stage 0.

## Xuất executable V++/ASM

```text
vpp build --target aarch64-apple-darwin --emit-exe-vpp app.vi ./app
vpp build --target x86_64-linux-gnu --emit-exe-vpp app.vi ./app
```

`--emit-exe-vpp` yêu cầu compiler `vpp-native` đã bootstrap (có thể chỉ định
`VPP_NATIVE_COMPILER`), và target phải là host đang chạy. CLI kiểm tra source,
project lock, đường đầu ra rồi chuyển trực tiếp cho frontend và bộ phát ASM
của compiler `.vi`; không đi qua bridge `executeVppSource()` của Stage 0.
Host AOT vẫn thực hiện lắp ráp/liên kết và các primitive runtime C/C++ vẫn
được dùng. Bản macOS kiểm chứng còn phụ thuộc `libc++`, `libffi`,
`libSystem` và framework TLS. Hiện hỗ trợ macOS AArch64 và Linux x86-64.
Executable mới thực thi **câu lệnh cấp tệp** theo ngữ nghĩa V++: ví dụ
`in chính();` để gọi và in kết quả hàm `chính`. Giá trị trả về của hàm này
không tự trở thành exit code của tiến trình như ABI `main` i32 ở Stage 0.
`--emit-exe-selfhost` vẫn tồn tại để bootstrap Stage 0 và kiểm thử tương thích;
nhánh này còn phụ thuộc C++/VM. Compiler chỉ thay thế đầu ra sau khi liên kết
thành công; từ chối đường đầu ra trùng tệp nguồn, kể cả symlink.

## Package

```text
vpp gói khởi tạo [tên]
vpp gói thêm <nguồn> [tên]
vpp gói cài đặt [<nguồn> [tên] | --ngoại-tuyến]
vpp gói phát hành <registry-root>
vpp gói đồng bộ
vpp gói cập nhật
vpp gói khóa
vpp gói phục hồi [--ngoại-tuyến]
vpp gói xóa <tên>
vpp gói danh sách
vpp gói thông tin <tên>
vpp gói kiểm tra <tên>
```

Nguồn package có thể là path, Git hoặc filesystem registry. `vpp.lock` pin version/revision và
fingerprint; run/build trong project có lock sẽ từ chối dependency thiếu hoặc lệch fingerprint.
Xem `docs/package-system.md` cho cú pháp source, SemVer/range, cache và offline restore.

## Workflow CI tối thiểu

```text
vpp --định-dạng src/ --kiểm-tra
vpp --soát-lỗi src/
vpp dựng src/chính.vi
vpp kiểm thử tests
```

Repo V++ dùng thêm CMake/CTest cho test compiler/runtime C++ và regression ngôn ngữ.
