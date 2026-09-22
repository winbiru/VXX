# Debugging V++ 1.0

V++ cung cấp diagnostic theo source span, stack trace runtime và các lệnh quan sát AST/IR để
phân biệt lỗi cú pháp, semantic, compiler và runtime.

## Bắt đầu từ diagnostic nguồn

Chạy linter trước khi chạy chương trình:

```text
vpp --soát-lỗi src/
```

Output có dạng `tệp:dòng:cột: ...`. VS Code extension dùng cùng diagnostic qua LSP, nên vị trí
lỗi trong editor và CLI phải trỏ về cùng vùng source.

Khi lỗi liên quan cú pháp hoặc import, ưu tiên sửa diagnostic đầu tiên: lỗi parser/import sớm có
thể làm các diagnostic phía sau chỉ là hệ quả.

## Xem AST và IR

```text
vpp --dump-ast src/main.vi
vpp --dump-ir src/main.vi
```

`--dump-ast` giúp kiểm tra parser đã hiểu token/block/call theo cấu trúc nào. `--dump-ir` cho
thấy IR sau optimizer trước direct bytecode emitter. Hai lệnh không chạy VM, nên phù hợp để
khoanh vùng lỗi compiler mà không gây side effect của chương trình.

Khi cần quan sát bytecode đã sinh:

```text
vpp --giải-mã src/main.vi
```

## Stack trace runtime

Runtime giữ frame có cấu trúc gồm file, dòng/cột, function/method và module. Khi một lỗi VM fatal
xảy ra, CLI hiển thị vị trí nguồn và chuỗi caller; frame đệ quy lặp có thể được nén để trace dễ
đọc hơn.

Exception do chương trình chủ động `ném` đi qua function/method/module boundary tới `bắt lỗi`
gần nhất. VM fault như chia cho 0, index sai, property/method không tồn tại hoặc call boundary
không hợp lệ dừng lần chạy hiện tại. Xem `docs/runtime-errors.md` cho phân loại đầy đủ.

## Debug module và package

Nếu import không resolve đúng, kiểm tra lần lượt:

1. target `nhập` có đúng tên/path và không đặt trong dấu nháy;
2. import tương đối được tính từ thư mục module đang import;
3. package đã có dưới `gói/` hoặc `VPP_HOME`;
4. nếu project có `vpp.lock`, dependency cài đặt phải khớp fingerprint đã khóa.

Các lệnh hữu ích:

```text
vpp gói danh sách
vpp gói thông tin <tên>
vpp gói kiểm tra <tên>
vpp gói phục hồi
vpp chẩn đoán
vpp nơi
```

Package bị sửa tay sau khi lock có thể làm `chạy`/`dựng` thất bại trước compiler. Dùng
`vpp gói phục hồi` để lấy lại exact bytes đã khóa hoặc `vpp gói cập nhật`/`vpp gói khóa` khi chủ
ý thay dependency.

## Debug test

Chạy một file test trước khi chạy cả cây:

```text
vpp kiểm thử tests/ca_loi.vi
```

Sau khi ca đơn lẻ ổn định, chạy:

```text
vpp kiểm thử tests
```

Trong repo compiler/runtime, dùng CTest để biết lỗi thuộc unit nào:

```text
ctest --test-dir build --output-on-failure
```

Regression HTTP dùng fixture cục bộ, vì vậy không cần Internet cho full suite chuẩn của repo.

## Khi cần báo lỗi

Một báo cáo tối thiểu nên có phiên bản V++, hệ điều hành, command, file `.vi` nhỏ nhất tái hiện
được lỗi và output diagnostic/stack trace đầy đủ. Nếu nghi lỗi compiler, kèm thêm output AST/IR
nếu chúng giúp chỉ ra bước đầu tiên bắt đầu sai.
