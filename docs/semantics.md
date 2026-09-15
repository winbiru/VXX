# Semantics nền tảng V++ 1.0

Tài liệu này khóa các quy tắc giá trị và scope nền tảng đã được compiler/runtime hiện tại
thực thi. Thay đổi các quy tắc dưới đây sau khi phát hành 1.0 phải được xem là thay đổi
semantics của ngôn ngữ và cần regression tương ứng.

## Scope và shadowing

- Biến được tạo bằng phép gán trong thân hàm thuộc scope của hàm gần nhất. Một block điều
  khiển như `nếu`, `lặp` hoặc block lồng không tự tạo một biến mới chỉ vì phép gán nằm bên
  trong block đó.
- Nếu tên đã tồn tại trong lexical scope nhìn thấy được, phép gán cập nhật binding đó.
- Tham số hàm và tham số lambda nằm trong scope callable riêng và có thể shadow tên ở scope
  ngoài. Binding của tham số không thay thế binding cùng tên ở callable bên ngoài.
- Lambda capture biến lexical theo **tham chiếu tới ô nhớ chia sẻ**. Closure và scope tạo ra
  closure cùng đọc/ghi một giá trị; capture không phải snapshot tại thời điểm tạo lambda.
- Ô nhớ capture sống ít nhất tới khi closure cuối cùng tham chiếu tới nó không còn reachable,
  vì vậy closure có thể được trả về khỏi hàm tạo và tiếp tục đọc/ghi biến đã capture.
- Lambda lồng nhau chuyển tiếp cùng ô nhớ khi cùng capture một binding bên ngoài; shadowing
  bằng parameter/local mới vẫn tạo binding riêng và không capture nhầm binding bị che.

Ví dụ:

```vi
hàm vi_du() {
    x = 1;
    nếu (đúng) {
        x = 2;
        y = 3;
    };
    in x; // 2
    in y; // 3

    f = hàm(x) { trả về x + 10; };
    in f(5); // 15
    in x;    // 2
}
```

## `rỗng`

`rỗng` là một giá trị runtime riêng. Nó không đồng nhất với `0`, `0.0`, chuỗi rỗng hoặc
collection rỗng.

- `rỗng == rỗng` là đúng.
- `rỗng == 0` là sai.
- `rỗng` là falsy khi dùng làm điều kiện.
- Chuyển `rỗng` sang chuỗi hiển thị tạo ra `"rỗng"`.

## Truthiness

`nếu`, vòng lặp, `!`, `&&` và `||` dùng cùng một quy tắc truthiness.

| Giá trị | Falsy | Truthy |
| --- | --- | --- |
| Số nguyên | `0` | mọi số khác `0` |
| Số thực | `0.0` | mọi số khác `0.0` |
| Chuỗi | `""` | chuỗi có ít nhất một ký tự |
| `rỗng` | luôn falsy | không có |
| List / map / tuple | handle null hoặc collection rỗng | collection có phần tử |
| Class / instance / closure | handle null | handle hợp lệ |

`&&` và `||` trả về số nguyên boolean `1` hoặc `0`; chúng không trả lại nguyên toán hạng.

## Equality

`==` và `!=` dùng cùng equality contract với collection helpers của runtime.

- `int` và `double` được so sánh sau numeric promotion, vì vậy `1 == 1.0` là đúng.
- Chuỗi so sánh theo nội dung.
- `rỗng` chỉ bằng `rỗng`.
- List, map, tuple, class, instance và closure so sánh theo identity của handle. Hai collection có
  nội dung giống nhau nhưng được tạo riêng vẫn không bằng nhau.
- Hai giá trị thuộc các nhóm kiểu khác nhau và không thuộc cặp numeric `int`/`double` được
  xem là không bằng nhau; `==` trả `0`, `!=` trả `1` thay vì phát sinh lỗi kiểu.

## So sánh thứ tự

`<`, `>`, `<=`, `>=` chỉ hợp lệ cho:

- numeric với numeric; `int` và `double` được numeric promotion;
- string với string, theo thứ tự từ điển của biểu diễn chuỗi runtime.

Các cặp kiểu còn lại không có ordering và tạo runtime error.

## Chuyển đổi kiểu ngầm trong toán tử

- Phép toán số `+`, `-`, `*`, `/` giữa `int` và `double` dùng numeric promotion. Kết quả giữ
  `int` khi cả hai toán hạng là `int`, trừ các trường hợp semantics riêng của phép chia đã
  được runtime định nghĩa; có `double` thì kết quả số dùng `double`.
- `-`, `*`, `/` chỉ nhận numeric. `%` dùng giá trị số nguyên theo contract runtime hiện tại.
- `+` thực hiện cộng số khi cả hai toán hạng numeric. Nếu có toán hạng phi numeric, runtime
  chuyển biểu diễn của hai toán hạng thành chuỗi rồi nối chúng.
- Equality không ép `rỗng`, chuỗi hay collection thành số hoặc boolean.
- Ordering không tự chuyển chuỗi thành số và cũng không tự stringify object/collection.

Regression end-to-end chính cho các quy tắc này nằm ở
`src/tests/kiem_tra_semantics_gia_tri.vi`; helper-level regression nằm trong
`test/runtime_value_tests.cpp`.

## Chuỗi và file I/O

Compiler giải mã escape trong literal chuỗi một lần, giống nhau cho chuỗi độc lập
và chuỗi nằm trong list/map. `ghi tệp` ghi nguyên nội dung chuỗi runtime; nó không
giải mã lại `\n`, `\"` hay `\\` trong dữ liệu JSON đã serialize. Vì vậy đọc lại tệp
giữ nguyên nội dung truyền vào, kể cả dấu gạch chéo và dấu ngoặc kép.

## Module: export, re-export và chu trình import

- Function/class/interface top-level không ghi visibility hoặc ghi `công khai` thuộc bề mặt
  export để giữ tương thích source hiện có. Khai báo `riêng tư`/`bảo vệ` không được export.
- `nhập đường/dẫn.vi;` chỉ đưa export của module đích vào module hiện tại; nó không tự chuyển
  tiếp các tên đó cho module nhập phía ngoài.
- `công khai nhập đường/dẫn.vi;` re-export bề mặt công khai của module đích. Nếu có alias,
  alias là một phần của tên được re-export, ví dụ `công khai nhập toan.vi như toán;` xuất
  `toán.nhân` chứ không làm phẳng thành `nhân`.
- Re-export không bao giờ làm lộ symbol private/protected của dependency.
- Truy cập một symbol biết chắc tồn tại nhưng không thuộc bề mặt export là semantic error thay
  vì rơi xuống dynamic-name fallback.
- Import cycle local `.vi` bị từ chối ở compile time. Diagnostic chứa chuỗi identity/path của
  cycle theo dạng `a.vi -> b.vi -> a.vi`; compiler không âm thầm coi cycle là no-op.
- Import tương đối bên trong module được phân giải từ thư mục chứa module đó, không từ thư mục
  của entry source.

Regression module chính nằm trong `test/module_graph_tests.cpp` và
`src/tests/kiem_tra_module_reexport.vi`.

## Destructor/finalizer trong 1.0

V++ 1.0 **không có destructor/finalizer do người dùng định nghĩa**. Tracing GC chỉ quản lý
bộ nhớ và có quyền thu gom ở thời điểm không quan sát được từ semantics chương trình. Vì vậy:

- chương trình không được phụ thuộc vào thời điểm GC chạy để thực hiện logic hoặc side effect;
- không có callback ngầm khi object/collection/closure bị thu gom;
- tài nguyên ngoài bộ nhớ như file, socket hoặc server phải được đóng bằng API/lifecycle tường
  minh của tài nguyên đó;
- nếu một phiên bản sau bổ sung destructor/finalizer, đó là contract ngôn ngữ mới và phải định
  nghĩa rõ thứ tự, exception behavior, interaction với cycle và shutdown trước khi bật mặc định.

## Type policy và call boundary

V++ 1.0 dùng dynamic typing theo `docs/adr/0001-type-policy.md`. Binding không có kiểu tĩnh
bắt buộc và có thể nhận `StackValue` thuộc loại khác sau mỗi lần gán. Function boundary không
ép kiểu ngầm; compiler chỉ kiểm tra arity khi callable đích được biết chắc, còn dynamic call
được kiểm tra tại runtime. Parameter bắt buộc không bao giờ được tự bù bằng `0`.
