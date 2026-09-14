# Contract lỗi runtime V++ 1.0

Tài liệu này khóa cách V++ 1.0 phân biệt exception do chương trình chủ động `ném` và lỗi
thực thi của VM. Hai nhóm có cơ chế xử lý khác nhau và không được nhập làm một.

## Exception của ngôn ngữ

`ném biểu_thức;` có thể mang mọi `StackValue`. Giá trị này được chuyển tới `bắt lỗi` gần
nhất theo luồng gọi động, kể cả khi phải đi qua nhiều hàm, method hoặc constructor. Biến
catch nhận lại đúng giá trị đã ném; runtime không stringify giá trị trước khi bind.

Khi chuyển tới handler, VM unwind trạng thái tạm được tạo sau lúc vào `thử`:

- data stack trở về độ sâu của `thử`, sau đó giá trị exception được đặt lên stack cho
  `OP_BAT_LOI`;
- block, `chọn`, loop-control và if/else control stack trở về snapshot của `thử`;
- call frame của child VM đã kết thúc vì exception luôn được pop trước khi truyền lên caller.

Các side effect đã xảy ra trước `ném` không có transaction rollback. Thay đổi trên object/
collection dùng chung vẫn tồn tại; global/class state của child VM cũng được commit về caller
trước khi exception tiếp tục unwind.

Nếu không còn handler V++, runtime phát `LanguageException` ra host với diagnostic ổn định:
`Lỗi không bắt được: <giá trị>`.

## Lỗi thực thi của VM

Lỗi như chia cho 0, opcode/operand sai, truy cập index ngoài phạm vi, lỗi native hoặc vi phạm
invariant runtime là lỗi fatal của lần chạy hiện tại. Chúng dùng `RuntimeError` với
`RuntimeErrorKind::VmFault`. Khi lỗi gắn với opcode, message giữ tên lệnh và program counter
để CLI/test có thể xác định vị trí bytecode.

Trong V++ 1.0, `bắt lỗi` chỉ bắt giá trị sinh bởi `ném`; nó không bắt `RuntimeError`. Một
`RuntimeError` dừng lần `run()` hiện tại. Caller host không được resume VM từ program counter
đang lỗi. Nếu lỗi phát sinh trong child VM, call frame của caller vẫn được unwind trước khi
lỗi truyền ra ngoài, tránh để lại frame treo.

## Lỗi call boundary

Thiếu đối số bắt buộc hoặc truyền dư đối số dùng `RuntimeErrorKind::CallBoundary`. VM suy ra
biên arity từ `OP_PARAM`/`OP_PARAM_MAC_DINH`; receiver ngầm của method không được tính vào số
đối số nguồn. Runtime không còn tự bù `0` cho parameter bắt buộc bị thiếu. Direct call mà
semantic đã biết chắc callable được chặn sớm lúc compile; dynamic/indirect/imported call vẫn
được kiểm tra tại runtime sau khi callable thật được resolve.

## Lỗi khởi tạo module

Module initializer thất bại làm `ModuleTable` chuyển module sang trạng thái `failed`. Lần thử
khởi tạo lại module đã failed phát `RuntimeErrorKind::ModuleInitialization`; module không được
chạy initializer lần hai trong cùng VM. Lỗi gốc phát sinh bên trong initializer vẫn được truyền
lên sau khi trạng thái module đã được đánh dấu failed.

## Regression

- `src/tests/kiem_tra_ngoai_le.vi`: bắt lỗi cục bộ cơ bản.
- `src/tests/kiem_tra_ngoai_le_xuyen_ham.vi`: exception xuyên function boundary, catch gần
  nhất, rethrow và tiếp tục chạy sau catch.
- `test/vm_handler_tests.cpp`: unwind data/control stack, typed fatal runtime error và cleanup
  call frame.
- `test/vm_opcode_smoke_tests.cpp`: uncaught exception và các runtime fault opcode cơ bản.
