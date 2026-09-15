# ADR 0001: Type policy V++ 1.0

- Trạng thái: Chấp nhận
- Ngày: 14/09/2026

## Quyết định

V++ 1.0 dùng **dynamic typing**. Mỗi binding giữ một `StackValue` runtime và có thể nhận
giá trị thuộc kiểu khác ở lần gán sau. Compiler không gắn kiểu tĩnh bắt buộc cho symbol,
parameter, field, expression hoặc return value; IR 1.0 tiếp tục là IR không kiểu.

Semantic analysis vẫn được phép kiểm tra các invariant không cần suy luận kiểu: scope/name
resolution, visibility, inheritance/interface contract và arity khi callable đích đã biết.
Các phép toán, index, field động và native boundary kiểm tra loại `StackValue` khi chạy.

## Call boundary

- Function/method/constructor có `N` parameter nhận tối đa `N` đối số.
- Parameter không có default là bắt buộc theo vị trí. Số đối số tối thiểu bằng vị trí của
  parameter bắt buộc cuối cùng cộng một; vì vậy default ở trước parameter bắt buộc không làm
  parameter phía sau trở thành tùy chọn.
- Thiếu parameter có default thì runtime dùng default đã compile.
- Thiếu parameter bắt buộc hoặc truyền dư đối số là lỗi call boundary; VM không tự bù `0`.
- Direct call, constructor và method mà semantic biết chắc đích được kiểm tra khi compile.
  Imported/dynamic/indirect call được kiểm tra tại runtime sau khi resolve callable.
- Native function giữ arity contract riêng tại native adapter nhưng phải báo lỗi thay vì tự
  bỏ/bù đối số.
- Arity không tạo type constraint: cùng function có thể nhận số ở lần gọi này và chuỗi,
  collection hoặc instance ở lần gọi khác nếu thân hàm hỗ trợ giá trị đó.

## Value boundary

Không có implicit static cast tại function boundary. Đối số và return value đi qua bằng
`StackValue` nguyên trạng. Numeric promotion, truthiness, equality và string concatenation
tuân theo `docs/semantics.md`; lỗi loại giá trị của operator/native là runtime error.

## Hệ quả

Typed IR/static checker không thuộc contract bắt buộc của 1.0. Nếu sau này bổ sung gradual
typing, annotation phải là lớp contract mới và không được âm thầm thay đổi chương trình dynamic
hợp lệ của 1.0. Reflection/generic cũng không được dùng để giả định type tĩnh chưa tồn tại.

Regression chính: `src/tests/kiem_tra_kieu_dong_call_boundary.vi` cùng các chương trình `.vi`
kiểm tra arity và call boundary trong `src/tests/`.
