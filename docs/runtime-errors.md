# Contract lỗi runtime V++ 1.0

Tài liệu này khóa cách V++ 1.0 phân biệt exception do chương trình chủ động `ném` và lỗi
thực thi của VM. Hai nhóm có cơ chế xử lý khác nhau và không được nhập làm một.

## Exception của ngôn ngữ

`ném biểu_thức;` có thể mang mọi `StackValue`. Giá trị này được chuyển tới `bắt lỗi` gần
nhất theo luồng gọi động, kể cả khi phải đi qua nhiều hàm, method hoặc constructor. Biến
catch nhận lại đúng giá trị đã ném; runtime không stringify giá trị trước khi bind.
Nếu handler nằm trong một hàm, binding catch được ghi vào call frame hiện tại (hoặc shared
capture cell khi biến được closure capture), không rò sang bảng biến global.

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
`RuntimeErrorKind::VmFault`. Chi tiết opcode và program counter là thông tin nội bộ của VM;
người viết V++ nhận vị trí file/dòng/cột từ dấu vết lỗi thay vì phải hiểu bytecode.

Chẩn đoán runtime dùng `RuntimeDiagnosticContext` để thu các dữ kiện thực tế mà VM đang thấy:
opcode đang chạy, toán hạng, kiểu giá trị, chỉ số và kích thước tập hợp, kết quả tra hàm/lớp,
số đối số, độ sâu lời gọi, receiver/member, đích nhảy, trạng thái control-flow và lỗi từ native.
Nơi phát sinh lỗi không chọn sẵn `DivisionByZero`, `PropertyNotFound` hay một mã lỗi cụ thể.
`detectRuntimeDiagnostic()` suy ra loại lỗi từ các dữ kiện này rồi catalog tạo phần `Điều đã
xảy ra` và `Cách sửa`. Cơ chế không phân tích chuỗi `what()` để đoán lỗi.

Mã phân loại chỉ là chi tiết nội bộ phục vụ kiểm thử và mở rộng catalog; CLI không xuất mã lỗi
cho người dùng. Thông báo tập trung vào biểu hiện thực tế, nguyên nhân dễ hiểu, vị trí nguồn và
cách sửa.

Thông báo hướng tới người mới học lập trình: nói cụ thể chương trình đang làm gì, vì sao thao
tác đó không thể tiếp tục và người dùng cần thay đổi gì. Ví dụ chia `10 / 0` phải nói rõ đang
lấy `10` chia cho `0`, số chia bằng `0`, rồi hướng dẫn kiểm tra số chia trước khi thực hiện phép
chia. Các thuật ngữ nội bộ như opcode, program counter hoặc stack slot không xuất hiện trong
phần giải thích dành cho người dùng.

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

## Phạm vi kiểm thử bằng mã nguồn `.vi`

Các lỗi mà người viết V++ có thể tạo trực tiếp từ mã nguồn đều có regression `.vi` riêng.
Bộ hiện tại bao phủ chia cho 0, chia dư cho 0, yêu cầu số nguyên, phép toán cần số, so
sánh khác kiểu, sai kiểu/vượt biên chỉ số, dữ liệu không thể đánh chỉ số, hàm không tồn
tại, gọi giá trị không phải hàm, sai số lượng đối số, đệ quy quá sâu, receiver không phải
đối tượng, thuộc tính/phương thức không tồn tại, quyền truy cập, `++`/`--` sai kiểu, chuyển
số thực thất bại và thao tác native thất bại.

Một số diagnostic chỉ có thể xuất hiện khi bytecode hoặc trạng thái nội bộ VM bị hỏng, nên
không thể tạo trung thực bằng một file `.vi` hợp lệ. Bộ regression hiện chỉ khóa các lỗi có thể
quan sát hoặc tạo ra từ chương trình V++ hợp lệ.

## Regression

- `src/tests/kiem_tra_ngoai_le.vi`: bắt lỗi cục bộ cơ bản.
- `src/tests/kiem_tra_ngoai_le_xuyen_ham.vi`: exception xuyên function boundary, catch gần
  nhất, rethrow, binding catch cục bộ được closure capture đúng và tiếp tục chạy sau catch.
- `src/tests/kiem_tra_loi_chi_so_vuot_pham_vi.vi`: tự phát hiện chỉ số vượt kích thước thật.
- `src/tests/kiem_tra_loi_kieu_chi_so.vi`: tự phát hiện giá trị dùng làm chỉ số không phải số nguyên.
- `src/tests/kiem_tra_loi_phuong_thuc_khong_ton_tai.vi`: tự phát hiện method lookup thất bại.
- `src/tests/kiem_tra_stack_trace.vi`: lỗi số học kèm giá trị thật và dấu vết file/dòng/cột.
- `src/tests/kiem_tra_de_quy_vuot_gioi_han.vi`: lỗi đệ quy quá sâu với diễn giải nguyên nhân và
  hướng sửa cho người mới học.
