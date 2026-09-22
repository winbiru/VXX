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
vpp --định-dạng src/main.vi
vpp --định-dạng src/ --kiểm-tra
vpp --định-dạng src/ --ghi-tệp
```

`--soát-lỗi` trả diagnostic có file:dòng:cột và exit code phù hợp CI. Formatter giữ comment và
literal, hỗ trợ check mode và write mode; `--lint`, `--check`, `--in-place` là alias tương thích.

## Quan sát compiler

```text
vpp --giải-mã app.vi
vpp --dump-ast app.vi
vpp --dump-ir app.vi
```

`--dump-ast` và `--dump-ir` chỉ in dữ liệu compiler, không chạy VM. `--dump-ir` hiển thị IR sau
optimizer mà direct bytecode emitter nhận.

## REPL và LSP

```text
vpp --repl
vpp --lsp
```

LSP dùng cùng semantic model/diagnostic với tooling và hỗ trợ completion, definition, hover,
rename, format. Range tuân theo UTF-16 theo giao thức LSP.

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
vpp dựng src/main.vi
vpp kiểm thử tests
```

Repo V++ dùng thêm CMake/CTest cho test compiler/runtime C++ và regression ngôn ngữ.
