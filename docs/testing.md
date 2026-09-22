# Kiểm thử trong V++ 1.0

V++ 1.0 có hai lớp hỗ trợ kiểm thử: lệnh CLI `vpp kiểm thử` để discovery/chạy tệp
test và package tùy chọn `kiểm thử` để viết assertion/lifecycle trong mã V++.

Regression của chính repository dùng chương trình `.vi` trong `src/tests/`; CTest đăng ký
`vpp-integration` để chạy các file này và so sánh expected output. Ngoài ra còn một harness C++
rất nhỏ `vpp-rc-internal-hardening` cho các invariant không thể tạo từ source `.vi`: AST có
`ExprId` lỗi/chu trình, IR có operand/control-flow hỏng và metadata bytecode không hợp lệ.

## Discovery của CLI

```text
vpp kiểm thử [<tệp-hoặc-thư-mục>]
```

- Nếu truyền một tệp `.vi`, CLI chỉ chạy tệp đó.
- Nếu truyền một thư mục, CLI quét đệ quy mọi tệp `.vi` bên dưới thư mục.
- Danh sách được sắp theo đường dẫn trước khi chạy để thứ tự ổn định giữa các lần chạy.
- Khi không truyền đường dẫn, thư mục mặc định là `tests`.
- Một tệp đạt khi compile + VM kết thúc thành công. Bất kỳ lỗi compile/runtime không được
  bắt trong tệp đều làm tệp đó thất bại.
- CLI tiếp tục chạy các tệp còn lại và cuối cùng trả summary `đạt/tổng`; exit code chỉ bằng
  0 khi mọi tệp đều đạt.

Discovery không dựa vào hậu tố tên như `_test.vi`: mọi `.vi` trong cây test đều được chạy.
Vì vậy fixture/helper không phải entry test nên đặt ngoài thư mục được truyền cho
`vpp kiểm thử`, hoặc import từ test thay vì đặt cạnh các entry `.vi`.

## Assertion

Import package kiểm thử:

```vpp
nhập kiểm thử;
```

API 1.0:

```vpp
khẳng định đúng(điều kiện, thông điệp = "khẳng định đúng thất bại");
khẳng định sai(điều kiện, thông điệp = "khẳng định sai thất bại");
khẳng định bằng(thực tế, kỳ vọng, thông điệp = "khẳng định bằng thất bại");
khẳng định khác(thực tế, giá trị khác, thông điệp = "khẳng định khác thất bại");
khẳng định rỗng(thực tế, thông điệp = "khẳng định rỗng thất bại");
khẳng định không rỗng(thực tế, thông điệp = "khẳng định không rỗng thất bại");
khẳng định ném lỗi(hành động, lỗi kỳ vọng = rỗng,
                   thông điệp = "khẳng định ném lỗi thất bại");
```

`khẳng định ném lỗi` nhận một callback không tham số. Khi `lỗi kỳ vọng` khác `rỗng`,
giá trị bắt được phải bằng chính xác giá trị kỳ vọng.

## Setup / teardown

`chạy ca kiểm thử(thân, thiếtLập = rỗng, dọnDẹp = rỗng)` cung cấp lifecycle tối thiểu:

1. chạy `thiếtLập` nếu có;
2. chạy `thân`;
3. chạy `dọnDẹp` nếu setup đã hoàn tất;
4. nếu thân ném lỗi, teardown vẫn chạy rồi lỗi gốc được ném lại.

```vpp
nhập kiểm thử;

hàm chuẩn bị() { in "setup"; };
hàm dọn() { in "teardown"; };
hàm ca() {
    khẳng định bằng(2 + 3, 5);
    trả về đúng;
};

hàm main() {
    chạy ca kiểm thử(ca, chuẩn bị, dọn);
};
```

Package `kiểm thử` không được import tự động bởi `gói/chuẩn/main.vi`; mã production chỉ
kéo test API khi import tường minh.
