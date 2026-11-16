# CLI V++ 1.0

CLI 1.0 dùng tên lệnh tiếng Việt làm bề mặt canonical. Các alias tiếng Anh như `run`, `test`,
`build`, `new` vẫn được giữ để tương thích script cũ.

## VDK: chọn SDK và chuyển tiếp lệnh

`vdk` là CLI quản lý SDK viết bằng V++, được đóng gói cùng compiler native,
runtime, VM và JIT. Các lệnh quản lý:

```text
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

Lệnh tiếng Việt là giao diện chính; alias tiếng Anh vẫn tương thích. Cài hiển thị là tùy chọn, chỉ sửa profile người dùng. Mở terminal mới sau khi cài/gỡ. Nếu chưa gõ được dấu, dùng `vdk cai-hien-thi`. Tùy chọn `--vdk-home` tương đương `--thư-mục-vdk`.

`version` in phiên bản, nền tảng, kiến trúc và thư mục SDK được chọn.
`doctor` kiểm tra các thành phần, metadata và khả năng khởi chạy compiler;
`library list` liệt kê nhóm và ID thư viện, `--json` xuất catalog đầy đủ.

VDK tìm `vdk.json` gần nhất trong cây thư mục dự án để khóa phiên bản.
Xem [cách cài đặt nhiều SDK](installation.md#cài-đặt-vdk-và-chọn-phiên-bản-sdk).
Các lệnh khác được chuyển tới compiler hoặc JIT đi kèm, ví dụ
`vdk biên dịch app.vi --đầu-ra app`, `vdk chạy tức thời app.vi`. Chỉ các lệnh được compiler
native hiện tại hỗ trợ mới chạy được; các lệnh lịch sử dùng C++ ở bên dưới
không đồng nghĩa với hỗ trợ trong SDK V++/ASM.

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
vpp biên dịch máy ảo app.vi --đầu-ra app.vmb
vpp chạy --artifact app.vmb
```

`dựng` chạy compiler pipeline và verifier nhưng chưa ghi artifact `.vbc`; format bytecode chỉ
được public sau khi compatibility/versioning policy 1.0 được freeze. Artifact native `.vmb`
chạy bằng VM Assembly với `vpp chạy --artifact app.vmb` (alias `vpp artifact app.vmb`).
Artifact `.vbc` của bản C++ cũ cần biên dịch lại từ mã nguồn.

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
vpp --định-dạng src/chính.vi
vpp --định-dạng src/ --kiểm-tra
vpp --định-dạng src/ --ghi-tệp
```

`--soát-lỗi` dùng parser/semantic và trình phân giải import V++ native.
`--compiler=vpp` là alias tương thích; compiler C++ đã được loại bỏ.
Lỗi khai báo trùng trả vị trí dòng:cột theo byte UTF-8. Native lint chưa
phát đủ warning và SourceSpan của mọi lỗi ngữ nghĩa. LSP dùng native lint
trên bộ đệm chưa lưu, phân giải import theo thư mục tệp thật và chuyển
vị trí byte sang UTF-16. Native compiler hỗ trợ `lint <snapshot.vi> --base
<directory>` để kiểm tra import của bộ đệm.
Lệnh trả exit code phù hợp CI. Formatter giữ comment và literal, hỗ trợ
check mode và write mode; `--lint`, `--check`, `--in-place` là alias tương thích.

## Quan sát compiler

```text
vpp --giải-mã app.vi
vpp --dump-ast app.vi
vpp --dump-ir app.vi
vpp --dump-ir --compiler=vpp app.vi
```

`--dump-ast` và `--dump-ir` chỉ in dữ liệu compiler, không thực thi mã nguồn.
Các lệnh dùng compiler V++ native; `--compiler=vpp` chọn native tường minh.
Native `--dump-ir` hiển thị stack IR trước codegen; import chỉ ghi metadata
`(chưa liên kết)` trong bản dump cấp tệp.

## Gỡ cài đặt

```text
vpp gỡ cài đặt
vdk gỡ cài đặt
vpp uninstall
```

Trình gỡ chỉ xóa các tệp có trong danh sách cài đặt SHA-256 và chưa bị sửa đổi.
Các dự án và tệp riêng được giữ lại. VDK gỡ phiên bản SDK đang được chọn.
Nếu đã cài cấu hình hiển thị, chạy `vdk gỡ hiển thị` trước khi gỡ SDK;
cấu hình shell/PATH dùng chung không bị tự động xóa.

## REPL và LSP

```text
vpp --repl
vpp --lsp
```

Máy chủ LSP viết bằng V++ hỗ trợ JSON-RPC, đồng bộ toàn bộ tài liệu,
chẩn đoán bộ đệm chưa lưu, định dạng, `vpp/libraryRoots`, thông báo thay đổi
catalog thư viện và shutdown/exit. Chỉ mục ngữ nghĩa lấy định danh biến
và phạm vi từ AST/IR của compiler, chuyển vị trí byte UTF-8 sang UTF-16 của LSP.

| Phương thức LSP | Phạm vi hỗ trợ |
| --- | --- |
| `textDocument/completion` | Gợi ý biến, tham số, hàm, kiểu trong phạm vi và hàm nhập từ mô-đun; dùng chỉ mục hợp lệ gần nhất khi mã đang gõ chưa phân tích được. |
| `textDocument/definition` | Đi đến khai báo của biến, tham số, hàm cục bộ và hàm nhập. |
| `textDocument/hover` | Hiện tên biến, tham số hoặc chữ ký hàm, gồm tham số có mặc định. |
| `textDocument/rename` | Đổi tên biến/tham số và hàm riêng tư trong tệp; kiểm tra tên mới, từ chối tên trùng, dựng lại chỉ mục trên nguồn ứng viên để kiểm tra liên kết ký hiệu trước khi trả sửa đổi. |

Extension VS Code có thể dùng các tính năng này khi đặt `vpp.lsp.enabled=true`.
Nếu không cấu hình máy chủ riêng, extension tìm cặp `vpp-tools` và `vpp-native`
trong workspace hoặc SDK. Chỉ mục được làm mới khi tài liệu mở, sửa, đóng
hoặc tệp thay đổi trên đĩa.

Nếu mã đang gõ có lỗi cú pháp, completion có thể dùng chỉ mục hợp lệ gần
nhất của cùng tài liệu, với `isIncomplete: true`. Definition, hover và rename
không dùng chỉ mục cũ. Nội dung tệp nhập đang mở được ưu tiên cả khi bộ đệm
chưa lưu là chuỗi rỗng.

Giới hạn hiện tại: chưa đổi tên xuyên mô-đun hoặc hàm công khai; chưa phân giải
đầy đủ phương thức OOP và kiểu nhập. Completion dự phòng cần có chỉ mục hợp lệ
từ trước và chưa thay thế cơ chế phục hồi cú pháp đầy đủ; extension vẫn có gợi ý
từ khóa và cú pháp cục bộ. Kiểm tra đổi tên chỉ bao phủ các liên kết đã lập chỉ
mục, chưa tương đương kiểm tra toàn bộ dự án. Tệp nhập mới chưa tồn tại trên đĩa
chưa được phân giải. Một số lỗi ngữ nghĩa chưa có vị trí chi tiết.

## Xuất executable V++/ASM

```text
vpp biên dịch app.vi -o ./app
vpp build app.vi -o ./app
vpp vm-build app.vi -o app.vmb
```

Compiler native phát mã máy thông qua V++/Assembly; bản macOS ARM64 có bộ
phát object và linker native. Linux x86-64 và Windows x86-64 dùng công cụ
lắp ráp/liên kết tương ứng với nền tảng. Không có runtime C/C++ dự phòng.
Compiler từ chối đường đầu ra trùng tệp nguồn.

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

Repo V++ dùng thêm CMake/CTest cho test compiler/runtime V++/Assembly và regression ngôn ngữ.
