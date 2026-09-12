# Backlog chức năng từ giáo trình Python (100 bài)

> Nguồn: ba ảnh trong `plans/image/`.
> Mục đích: chuyển danh sách bài thực hành thành backlog có thể triển khai cho V++,
> không đánh dấu một bài là hoàn tất chỉ vì cú pháp tương tự đã tồn tại. Mỗi hạng mục
> cần có regression `.vi` và expected output/error trước khi đóng.

## Cách đọc trạng thái

- `Có nền tảng`: primitive công khai đã có trong V++; bài có thể được cài bằng
  vòng lặp/index/hàm thư viện hiện hữu, dù chưa nhất thiết có một helper chuyên dụng.
  Vẫn cần regression hoặc ví dụ riêng trước khi coi bài giáo trình là hoàn tất.
- `Cần hoàn thiện`: có cú pháp hoặc phần MVP, nhưng API/hành vi chưa ổn định.
- `Chưa có`: chưa thể biểu đạt bằng primitive runtime/thư viện công khai hiện có;
  không dùng trạng thái này chỉ vì chưa có một convenience API riêng.
- `Ngoài phạm vi lõi`: có thể làm bằng thư viện/CLI mẫu; không nên mở rộng VM nếu không cần.

## Thứ tự triển khai đề xuất

| Pha | Mục tiêu | Lý do / đầu ra tối thiểu |
|---|---|---|
| P0 | Regression cho primitive đã có | Giữ các ví dụ giáo trình chạy được trên số, hàm, list/map/set/tuple, chuỗi và tệp; không đổi kiến trúc runtime. |
| P1 | Bài collection cài bằng primitive | List literal (kể cả lồng nhau), đọc/ghi index list, đọc index chuỗi, mutation list, map get/set/keys, set và tuple đã có; bổ sung ví dụ/contract cho các thuật toán ghép từ chúng. |
| P2 | API cấp cao và semantics còn thiếu | `join`/tách từ, flatten/slice/chunk, iteration entry của map, HOF `map`/`filter`, Unicode và policy equality sâu. |
| P3 | File và lỗi | File text an toàn, lỗi I/O có vị trí, exception do người dùng định nghĩa. |
| P4 | Tài liệu/tutorial | Tạo `examples/bai-001.vi`… và hướng dẫn học; chỉ sau khi contract P0–P3 ổn định. |

## Hợp đồng hiện có và khoảng trống sau P1

1. **Array/list:** direct compiler nhận literal scalar và list lồng nhau; hỗ trợ đọc/ghi
   index list không âm, kể cả chain trên list lồng nhau; chuỗi hỗ trợ đọc index byte
   không âm. API đi kèm gồm `độ dài`, `thêm`, `xóa tại`,
   `đảo ngược`, `tìm chỉ số`, `xóa trùng`, `sắp xếp`, `tổng list`, `nhỏ nhất list`,
   `lớn nhất list`. Chưa có `for-each`, slice hay equality sâu cho collection.
2. **Map/dictionary:** map literal trực tiếp hiện chứa scalar; `lấy map`, `đặt map`,
   `có khóa`, `xóa khóa`, `khóa map` đã có. `đặt map` có thể lưu list/map theo tham
   chiếu; key được chuẩn hóa về chuỗi và `khóa map` trả theo thứ tự từ điển. Chưa có
   cú pháp duyệt entry hoặc map merge chuyên dụng.
3. **Set:** set MVP là list không trùng lặp, giữ thứ tự xuất hiện đầu tiên; có
   `tập hợp`, `hợp tập`, `giao tập`, `rời nhau`. Số và chuỗi so theo giá trị, còn
   collection theo identity handle; chưa có set runtime độc lập/hash tổng quát.
4. **Tuple:** `thành tuple`/`thành list`, `độ dài` và đọc index đã có; tuple bất biến.
   Chưa có tuple literal hay destructuring/multiple assignment.
5. **Chuỗi:** API đếm/tìm/thay/case/word/Caesar đã có, nhưng `độ dài` và index dùng
   **byte UTF-8**, còn case/whitespace là ASCII. Cần Unicode code point trước khi
   cam kết hành vi tiếng Việt đầy đủ.
6. **Tệp và lỗi:** đọc/ghi tệp cùng đếm dòng/từ đã có; còn contract I/O có span,
   input parse và exception có type do người dùng định nghĩa.

## Ma trận 100 bài

### Nhóm 1 — Input/Output và Toán học cơ bản (1–10)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 1 | Hello World | Có nền tảng | Thêm ví dụ/tutorial nếu cần. |
| 2 | Tổng hai số | Có nền tảng | Regression input/biến/phép `+`. |
| 3 | Chu vi, diện tích hình chữ nhật | Có nền tảng | Regression toán học cơ bản. |
| 4 | Đổi °C sang °F | Có nền tảng | Regression số thực. |
| 5 | Diện tích, chu vi hình tròn | Có nền tảng | Cần hằng số `pi` hoặc ví dụ dùng literal. |
| 6 | Đổi giây thành giờ:phút:giây | Có nền tảng | Regression chia nguyên/modulo. |
| 7 | Tính lũy thừa | Có nền tảng | Chốt API/hàm mũ hoặc dùng vòng lặp. |
| 8 | Điểm trung bình | Có nền tảng | Regression số thực. |
| 9 | Đảo ngược số hai chữ số | Có nền tảng | Regression chia nguyên/modulo. |
| 10 | Hoán đổi hai biến | Có nền tảng | Regression gán và biến tạm. |

### Nhóm 2 — Rẽ nhánh (11–20)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 11 | Kiểm tra chẵn/lẻ | Có nền tảng | Regression `%` và nhánh. |
| 12 | Dương, âm, bằng 0 | Có nền tảng | Regression chuỗi `nếu không`. |
| 13 | Lớn nhất trong ba số | Có nền tảng | Regression so sánh. |
| 14 | Kiểm tra năm nhuận | Có nền tảng | Regression điều kiện kết hợp. |
| 15 | Xếp loại học lực | Có nền tảng | Regression nhánh nhiều cấp. |
| 16 | Kiểm tra tam giác | Có nền tảng | Regression điều kiện biên. |
| 17 | Giải phương trình bậc nhất | Có nền tảng | Chốt biểu diễn trường hợp vô nghiệm/vô số nghiệm. |
| 18 | Tính tiền điện | Có nền tảng | Regression ngưỡng/bậc thang. |
| 19 | Kiểm tra ký tự | Cần hoàn thiện | Đọc index chuỗi đã có theo byte; còn API ký tự/category chuẩn Unicode. |
| 20 | Máy tính đơn giản | Có nền tảng | Regression nhánh theo toán tử và lỗi chia 0. |

### Nhóm 3 — Vòng lặp (21–40)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 21 | In số từ 1 đến n | Có nền tảng | Regression vòng lặp. |
| 22 | Tổng 1…n | Có nền tảng | Regression vòng lặp/tích lũy. |
| 23 | Giai thừa | Có nền tảng | Regression điều kiện biên `0!`. |
| 24 | Bảng cửu chương | Có nền tảng | Regression format output. |
| 25 | Đếm chữ số của số | Có nền tảng | Chốt hành vi số âm/0. |
| 26 | Tổng các chữ số | Có nền tảng | Chốt hành vi số âm. |
| 27 | Kiểm tra nguyên tố | Có nền tảng | Regression `n < 2`. |
| 28 | In nguyên tố nhỏ hơn n | Có nền tảng | Regression vòng lặp lồng nhau. |
| 29 | ƯCLN | Có nền tảng | Regression Euclid. |
| 30 | BCNN | Có nền tảng | Regression số 0 và overflow policy. |
| 31 | Fibonacci | Có nền tảng | Regression số lượng phần tử/biên. |
| 32 | Số hoàn hảo | Có nền tảng | Regression divisor loop. |
| 33 | Tam giác dấu sao | Ngoài phạm vi lõi | Cần tutorial format output, không cần VM mới. |
| 34 | Tam giác cân dấu sao | Ngoài phạm vi lõi | Cần tutorial format output. |
| 35 | Đếm số lần xuất hiện | Có nền tảng | Chuỗi dùng `đếm ký tự`; list dùng vòng lặp index. Chưa có một API `đếm` tổng quát. |
| 36 | Đoán số (`while`) | Có nền tảng | Cần API nhập liệu nếu bài tương tác. |
| 37 | Số Armstrong | Có nền tảng | Cần API lũy thừa hoặc vòng lặp. |
| 38 | `break` và `continue` | Có nền tảng | Regression hai câu lệnh. |
| 39 | Vòng lặp lồng nhau — ma trận số | Có nền tảng | Nested list đã có; thêm regression vòng lặp/format output của ma trận. |
| 40 | Số nguyên tố cùng nhau | Có nền tảng | Tái sử dụng ƯCLN. |

### Nhóm 4 — Chuỗi (41–55)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 41 | Đếm ký tự | Có nền tảng | `đếm ký tự`; hiện đếm byte/subsequence, chưa phải code point Unicode. |
| 42 | Đảo ngược chuỗi | Có nền tảng | `đảo ngược(s)`; hiện đảo byte UTF-8, chưa an toàn cho Unicode đa byte. |
| 43 | Palindrome | Có nền tảng | `là palindrome`; hiện ASCII case-fold, không bỏ dấu câu/khoảng trắng. |
| 44 | Đếm nguyên âm/phụ âm | Cần hoàn thiện | Cần phân loại ký tự và policy tiếng Việt. |
| 45 | Viết hoa chữ cái đầu mỗi từ | Có nền tảng | `viết hoa đầu từ`; ASCII case conversion. |
| 46 | Đếm lần xuất hiện một ký tự | Có nền tảng | `đếm ký tự`; byte/subsequence semantics MVP. |
| 47 | Loại khoảng trắng thừa | Có nền tảng | `chuẩn hóa khoảng trắng`; ASCII whitespace. |
| 48 | Kiểm tra anagram | Có nền tảng | `là anagram`; ASCII case/whitespace normalization. |
| 49 | Tìm từ dài nhất trong câu | Có nền tảng | `từ dài nhất`; byte length and ASCII whitespace. |
| 50 | Thay thế ký tự | Có nền tảng | `thay thế`; thay mọi substring không rỗng. |
| 51 | Nối chuỗi từ danh sách | Có nền tảng | Cài được bằng `độ dài`/index và `nối chuỗi`; chưa có `join` hay kiểm tra list chuỗi chuyên dụng. |
| 52 | Đếm số từ trong câu | Có nền tảng | `đếm từ`; ASCII whitespace. |
| 53 | Kiểm tra chuỗi con | Có nền tảng | `chứa chuỗi`. |
| 54 | Chuyển hoa/thường | Có nền tảng | `chuỗi hoa`/`chuỗi thường`; ASCII-only. |
| 55 | Mã hóa Caesar đơn giản | Có nền tảng | `mã hóa caesar`; ASCII Latin. |

### Nhóm 5 — List (56–70)

> Literal list scalar/lồng nhau, đọc/ghi index lồng nhau và API mutation/search/sort
> đã có regression. Các mục dưới đây chỉ còn thiếu khi cần helper chuyên dụng,
> validation cấu trúc hoặc semantics collection sâu.

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 56 | Tổng và trung bình list | Có nền tảng | `tổng list`, `độ dài` và iteration MVP; trung bình là `tổng list(ds) / độ dài(ds)`. |
| 57 | Lớn nhất/nhỏ nhất | Có nền tảng | `lớn nhất list` / `nhỏ nhất list` cho list số hoặc chuỗi. |
| 58 | Đảo ngược list | Có nền tảng | `đảo ngược(ds)` mutate tại chỗ. |
| 59 | Loại phần tử trùng | Có nền tảng | `xóa trùng(ds)` giữ thứ tự đầu tiên. |
| 60 | Tìm số chẵn/lẻ | Có nền tảng | iteration MVP + `ds[i]`. |
| 61 | Sắp xếp Bubble Sort | Có nền tảng | `sắp xếp(ds)` hoặc tự cài Bubble Sort bằng index đọc/ghi. |
| 62 | Linear Search | Có nền tảng | `tìm chỉ số(ds, value)` hoặc iteration. |
| 63 | Tổng hai ma trận | Có nền tảng | Nested list và index lồng nhau cho phép cài bằng vòng lặp; cần ví dụ riêng và validation shape/chữ nhật. |
| 64 | Làm phẳng list lồng nhau | Cần hoàn thiện | List lồng nhau đã có, nhưng chưa có type test hoặc `flatten` tổng quát; có thể bắt đầu bằng ví dụ cấu trúc biết trước. |
| 65 | Trộn hai list đã sắp xếp | Có nền tảng | `độ dài`, index và `thêm` đủ cho thuật toán; chưa có merge/slice helper. |
| 66 | Phần tử xuất hiện nhiều nhất | Có nền tảng | Bảng tần suất cài được bằng `lấy map`/`đặt map`/`khóa map`; cần chốt tie policy và regression. |
| 67 | Xoay list | Có nền tảng | Có thể tạo list mới bằng index và `thêm`; chưa có slice/rotate helper. |
| 68 | Kiểm tra list con | Có nền tảng | So sánh phần tử scalar bằng vòng lặp index; chưa có sequence/deep-equality helper. |
| 69 | Tổng đường chéo ma trận | Có nền tảng | Nested index cho phép tính trực tiếp; validation ma trận vuông phải nằm trong bài/hàm. |
| 70 | Chia list thành nhóm nhỏ | Có nền tảng | Có thể xây từng nhóm bằng index và `thêm`; chưa có slice/chunk helper. |

### Nhóm 6 — Tuple, Dictionary, Set (71–80)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 71 | Đổi list ↔ tuple | Có nền tảng | `thành tuple`/`thành list`; tuple MVP bất biến và đọc index được. |
| 72 | Đếm từ bằng dictionary | Cần hoàn thiện | `lấy map`/`đặt map`/`khóa map` đã có; còn tokenizer công khai (hoặc scan ASCII thủ công) và ví dụ tần suất từ. |
| 73 | Gộp hai dictionary | Có nền tảng | Duyệt `khóa map` rồi `lấy map`/`đặt map`; chưa có `merge` chuyên dụng, cần chốt policy xung đột. |
| 74 | Key có giá trị lớn nhất | Có nền tảng | `khóa map`, `lấy map` và so sánh đủ để cài; cần policy map rỗng/tie. |
| 75 | Đảo key–value dictionary | Có nền tảng | Có thể tạo map mới qua `khóa map`/`lấy map`/`đặt map`; cần giới hạn giá trị thành key và collision policy. |
| 76 | Giao và hợp hai set | Có nền tảng | `giao tập` / `hợp tập`; set MVP là list unique giữ thứ tự. |
| 77 | Loại trùng bằng set | Có nền tảng | `tập hợp(ds)` hoặc `xóa trùng(ds)`. |
| 78 | Danh bạ điện thoại | Có nền tảng | `lấy map`/`đặt map`/`xóa khóa`; value scalar là đủ cho bài, list/map cũng lưu được theo tham chiếu. |
| 79 | Hai tập hợp rời nhau | Có nền tảng | `rời nhau(a, b)` trên list/set MVP. |
| 80 | Nhóm học sinh theo xếp loại | Có nền tảng | Map value có thể giữ list/map theo tham chiếu; còn helper grouping chuyên dụng. |

### Nhóm 7 — Hàm (81–90)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 81 | Hàm kiểm tra nguyên tố | Có nền tảng | Regression function + loop. |
| 82 | Hàm giai thừa | Có nền tảng | Regression. |
| 83 | Tham số mặc định | Có nền tảng | Regression có/không truyền đối số. |
| 84 | `*args` | Chưa có | Thiết kế variadic argument và ABI bytecode trước khi làm. |
| 85 | Trả về nhiều giá trị | Có nền tảng | Hàm có thể trả list/tuple, caller đọc theo index; chưa có tuple literal hoặc unpacking/multiple assignment. |
| 86 | Lambda | Có nền tảng | Regression lambda không capture. |
| 87 | `map`, `filter` với lambda | Cần hoàn thiện | Phụ thuộc list + HOF + iteration. |
| 88 | Hàm chuyển đổi nhiệt độ | Có nền tảng | Tutorial/regression. |
| 89 | Hàm palindrome | Có nền tảng | `là palindrome` đã có cho case-fold ASCII; cần policy dấu câu/Unicode nếu bài yêu cầu. |
| 90 | Đệ quy tính giai thừa | Có nền tảng | Regression base case và call frame. |

### Nhóm 8 — Đệ quy (91–95)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 91 | Đệ quy tổng 1…n | Có nền tảng | Regression. |
| 92 | Fibonacci thứ n | Có nền tảng | Regression. |
| 93 | Đệ quy đảo chuỗi | Cần hoàn thiện | Index chuỗi (byte) đã có, nhưng chưa có slice; bài đệ quy cần truyền index/độ dài hoặc bổ sung helper. |
| 94 | Đệ quy ƯCLN | Có nền tảng | Regression. |
| 95 | Đệ quy đếm chữ số | Có nền tảng | Chốt số âm/0. |

### Nhóm 9 — File và Exception Handling (96–100)

| Bài | Chức năng | Trạng thái | Công việc còn thiếu |
|---:|---|---|---|
| 96 | Ghi và đọc file văn bản | Có nền tảng | Native đọc/ghi và regression đường dẫn tạm đã có; còn policy encoding/lỗi I/O chi tiết. |
| 97 | Đếm dòng/từ trong file | Có nền tảng | `đếm dòng tệp`/`đếm từ tệp`; tệp rỗng = 0 dòng, token ASCII whitespace. |
| 98 | Xử lý lỗi chia cho 0 | Có nền tảng | Regression loại lỗi/message/catch. |
| 99 | Xử lý nhập sai định dạng | Cần hoàn thiện | Cần API input parse và lỗi chuyển kiểu rõ ràng. |
| 100 | Tự định nghĩa Exception | Chưa có | Thiết kế exception object/type, constructor/message và catch theo loại. |

## Definition of Done cho một bài/chức năng

- Có ví dụ `.vi` chạy từ CLI và expected output hoặc expected diagnostic.
- Có CTest hoặc được đăng ký vào regression runner; tính năng VM/bytecode có unit test opcode liên quan.
- Lỗi có thông điệp xác định và span nguồn khi áp dụng được.
- Tài liệu grammar/API được cập nhật nếu bổ sung cú pháp hoặc hàm stdlib công khai.
- Không mở rộng global compiler/runtime state mà không có reset/lifecycle test.

## Điểm bắt đầu khuyến nghị

Vertical slice collection hiện đã gồm **list literal scalar/lồng nhau, in list,
đọc/ghi index lồng nhau, `độ dài`, mutation/search/sort list, map, set và tuple**.
Đọc/ghi index nhận số nguyên không âm, báo lỗi vượt biên và mutation chỉ áp dụng cho
list; iteration MVP vẫn là `lặp(i = 0; i < độ dài(ds); i++)` cùng `ds[i]`, chưa có
`for-each` hay slice. Map literal trực tiếp vẫn là scalar, còn collection lồng nhau
được gắn vào map qua `đặt map`.

Ưu tiên tiếp theo nên là regression/tutorial cho các bài 63–75 có thể ghép từ
primitive hiện tại, rồi mới cân nhắc helper thực sự thiếu: `join`/tokenizer,
flatten/slice/chunk, map entry iteration/HOF và Unicode.

Regression `src/tests/kiem_tra_collection_bai_63_75.vi` hiện đã bao phủ bài
63–75 bằng primitive P1. Bài 72 dùng danh sách token đã tách sẵn để kiểm tra phần
dictionary; tokenizer câu tự do vẫn thuộc P2. Các thuật toán cần kiểm tra biên index
phải dùng nhánh lồng nhau vì `&&`/`||` hiện không có short-circuit ở VM.

## Ghi chú refactor/common hóa

Runtime hiện dùng chung equality/ordering/unique cho collection, validator đối số
native và index, cùng helper text ASCII. Compiler và VM cũng dùng chung wire format
literal; interpreter thường và JIT dùng evaluator nhị phân chung. Vì vậy các API mới
nên tái sử dụng các contract này thay vì tự tạo biến thể riêng theo từng bài.

Phần native stdlib collection đã được tách khỏi `vm.cpp` sang
`vm_native_collection_helpers`, còn membership/unique/union/intersection/disjoint và
validation kiểu sortable nằm ở `vpp/runtime/collection.h`. Native stdlib chuỗi cũng
được tách sang `vm_native_text_helpers`; các thuật toán chuỗi thuần như count/replace,
longest word, title, palindrome và anagram dùng chung `vpp/core/text`. HTTP không còn
duy trì wrapper lowercase riêng mà dùng trực tiếp contract ASCII của `core/text`.
