# Kiến trúc OOP cho thư viện và compiler V++

Các lớp sở hữu **trạng thái và vòng đời**; hàm/module tiếp tục phụ trách những
phép biến đổi không cần trạng thái. CLI và VM bootstrap vẫn dùng C++; compiler
tự chủ triển khai các pass bằng V++.

| Vùng | Lớp | Trách nhiệm |
| --- | --- | --- |
| `gói/biên dịch/bộ biên dịch.vi` | `Bộ Biên Dịch` | Snapshot nguồn dùng chung cho các truy vấn: kiểm tra, AST, bytecode, thống kê, tìm khai báo. |
| `gói/biên dịch/nguồn tệp.vi` | `Bộ Nạp Nguồn` | Quản lý trạng thái DFS, kiểm tra import vòng và thứ tự khởi tạo mô-đun. |
| `gói/biên dịch/bộ nạp.vi` | `Bộ Nạp Artifact` | Xác thực artifact, chuyển bytecode sang ABI VM, lưu ABI đã nạp và thực thi. |
| `gói/biên dịch/trình điều khiển.vi` | `Trình Điều Khiển Biên Dịch` | Điều phối chạy, dựng và nạp artifact theo cấu hình CLI. |
| `gói/biên dịch/phụ thuộc.vi` | `Bộ Phân Tích Phụ Thuộc` | Tái sử dụng một phiên `Bộ Biên Dịch` để kiểm tra mã nguồn và truy vấn import. |
| `gói/biên dịch/đồ thị mô đun.vi` | `Bộ Dựng Đồ Thị Mô Đun` | Giữ trạng thái DFS theo phiên, phát hiện chu trình, bảo toàn cạnh import và thứ tự khởi tạo. |
| `gói/biên dịch/sản phẩm biên dịch.vi` | `Bộ Mã Hóa Artifact` | Kiểm tra ABI, mã hóa/giải mã artifact v1 và đọc biểu diễn bootstrap cũ. |
| `gói/biên dịch/cây cú pháp.vi` | `Bộ Chỉ Mục Khai Báo` | Lập chỉ mục theo tên của AST một lần; truy vấn liên tiếp giữ thứ tự DFS, trả danh sách riêng và hỗ trợ làm mới. |
| `gói/biên dịch/sổ đăng ký.vi` | `Bộ Đăng Ký Hàm` | Quản lý Function ID, StringPool và bảng ký hiệu/export theo từng phiên IR hoặc đồ thị; bảo toàn thứ tự khai trước, hỗ trợ hoãn chuỗi khi liên kết mô-đun. |
| `gói/mạng/http/dịch vụ tài nguyên.vi` | `Bộ Dựng Phản Hồi REST` | Dựng JSON success/error, dữ liệu request và JWT từ trạng thái phản hồi dùng chung. |
| `gói/kiểm thử/bộ kiểm thử.vi` | `Bộ Kiểm Thử` | Gom nhiều `Ca Kiểm Thử`, chạy có hoặc không dừng khi lỗi và trả báo cáo mỗi lần chạy. |
| `gói/bộ sưu tập/bộ đệm danh sách.vi` | `Bộ Đệm Danh Sách` | Cung cấp bộ đệm, `số lượng()` và `trống()` cho ngăn xếp và hàng đợi. |
| `gói/lõi/tài nguyên có trạng thái.vi` | `Tài Nguyên Có Trạng Thái` | Cung cấp vòng đời và kiểm tra đóng cho logger, kết nối DB. |
| `gói/bộ sưu tập/kiểu danh sách.vi` | `Danh Sách` | Kế thừa bộ đệm và giao diện `Bộ Chứa`, kiểm tra chỉ số, sở hữu danh sách ngoài riêng. |
| `gói/văn bản/quét và tách chuỗi.vi` | `Bộ Quét` | Giữ cursor, delimiter literal/regex và vòng đời; hỗ trợ xem trước số mà không tiêu thụ token. |
| `gói/lõi/biểu thức chính quy.vi` | `Biểu Thức Chính Quy` | Phân tích mẫu một lần, dùng lại cây cho khớp, tìm, thay và tách; trạng thái match riêng mỗi lần gọi. |
| `gói/lõi/định dạng.vi` | `Bộ Định Dạng` | Giữ locale và buffer; chỉ nối kết quả sau khi định dạng thành công và kiểm tra giới hạn byte. |
| `gói/lõi/ngẫu nhiên.vi` | `Bộ Sinh Ngẫu Nhiên` | Sở hữu PRNG Park–Miller, snapshot/khôi phục và vòng đời đóng; tách khỏi entropy mật mã. |
| `gói/lõi/tập bản ghi.vi` | `Tập Bản Ghi` | Sở hữu danh sách hàng độc lập kết nối, truy vấn cột/lọc và kiểm tra chỉ số. |
| `gói/dữ liệu/json/json.vi` | `Tài Liệu JSON` | Giữ dữ liệu đã parse; nạp lại lỗi bảo toàn tài liệu trước đó. |
| `gói/dữ liệu/xml/cây tài liệu.vi` | `Phần Tử DOM` | Truy cập và sửa cây XML, điều hướng node con, chia sẻ cây map với API DOM hiện có. |
| `gói/lõi/hệ tệp zip.vi` | `Hệ Tệp ZIP` | Sở hữu handle kho ZIP, liệt kê/giải nén và từ chối sử dụng sau đóng. |
| `gói/lõi/tùy chọn bền vững.vi` | `Kho Tùy Chọn` | Giữ namespace; đọc lại dữ liệu mỗi thao tác để các instance cùng namespace không ghi đè bằng cache cũ. |
| `gói/lõi/quốc tế hóa.vi` | `Gói Tài Nguyên` | Giữ nguồn bundle và locale đã chọn; đổi vùng chỉ được ghi nhận khi hợp lệ. |
| `gói/lõi/thực thi tương tác.vi` | `Phiên Tương Tác` | Sở hữu nguồn/đầu ra REPL, đặt lại và đóng; thực thi lỗi không làm đổi nguồn đã ghi nhận. |
| `gói/lõi/đồng thời.vi` | `Luồng Công Việc` | Sở hữu worker ID, chờ/hủy/trạng thái; đóng hủy và join worker, không tự động đóng khi ra khỏi scope. |
| `gói/lõi/quản lý gói.vi` | `Dự Án Gói` | Chỉnh manifest trên bản sao, xác thực trước khi ghi nhận, truy vấn phụ thuộc, lưu/tải lại. |
| `gói/biên dịch/chương trình.vi` | `Bộ Phân Tích Cú Pháp` | Giữ token theo nguồn; mỗi lần phân tích bắt đầu bằng cursor mới. |
| `gói/biên dịch/từ vựng.vi` | `Bộ Tách Từ Vựng` | Giữ nguồn và cache từ tố thô/token hợp nhất theo từng đối tượng; làm mới bỏ cả hai cache. |
| `gói/biên dịch/biểu diễn trung gian.vi` | `Bộ Hạ IR` | Giữ nguồn và kết quả hạ IR của một phiên; tái sử dụng kết quả đến khi làm mới nguồn. |
| `gói/biên dịch/phát mã.vi` | `Bộ Phát Bytecode` | Giữ nguồn và bytecode chưa liên kết của một phiên; lỗi không ghi cache dở dang. |
| `gói/biên dịch/liên kết.vi` | `Bộ Liên Kết Bytecode` | Giữ nguồn và artifact đã liên kết theo từng đối tượng; API nhiều mô-đun tiếp tục giữ chữ ký cũ. |
| `gói/biên dịch/phiên tự chủ.vi` | `Bộ Biên Dịch Tự Chủ` | Giữ token, AST, IR và bytecode theo nguồn, chạy từng bước khi cần và vô hiệu hóa cache khi làm mới. |
| `gói/biên dịch/bộ sinh mã bản địa.vi` | `Bộ Sinh Mã Bản Địa` | Giữ target ABI chung cho mã lá, Machine IR và object ELF/Mach-O/COFF. |

Các module toán học, vật lý, hóa học, codec, entropy và các encoder ISA tiếp tục
chứa hàm thuần. Trạng thái phiên, buffer, cursor và vòng đời tài nguyên thuộc
lớp. Các lớp tiện ích đã công khai tiếp tục được giữ để tương thích API và
kế thừa/ghi đè phương thức; API hàm của chúng dùng receiver riêng cho mỗi lần
gọi. Không dùng biến đối tượng toàn cục để chia sẻ trạng thái qua import.
Registry kiến trúc và target triple (`đích.vi`), bộ chọn ISA (`mã máy.vi`),
bộ ghi object (`đối tượng.vi`) và các bảng opcode/nội tại dùng hàm cấp mô-đun.
Các hàm trong `chính.vi` tạo trực tiếp phiên compiler khi cần. Các lớp
`Bộ Biên Dịch`, `Bộ Biên Dịch Tự Chủ`, `Bộ Mã Hóa Artifact` và
`Bộ Sinh Mã Bản Địa` tiếp tục giữ snapshot, cache, artifact hoặc target của
từng đối tượng. Giữ các khai báo phạm vi hiện có trên hàm và phương thức:
`riêng tư` giới hạn helper nội bộ, `bảo vệ` cho phép lớp con truy cập và
`công khai` cung cấp API. Phương thức không ghi phạm vi kế thừa phạm vi của
lớp; vì vậy phương thức công khai trong lớp riêng tư vẫn cần ghi tường minh.
Lớp và giao diện khai báo `công khai` hoặc `riêng tư` tường minh.
Các hàm và phương thức cũng khai báo phạm vi tường minh. Khi bổ sung khai báo
cho mã cũ, giữ đúng phạm vi mặc định: hàm mô-đun công khai, phương thức kế thừa
phạm vi của lớp và phương thức giao diện công khai. Không đổi helper riêng tư
thành API công khai.

`opcode.vi` và `nội tại.vi` ghi lại ABI enum/descriptor C++. Công cụ Python
sinh mã trước đây đã được gỡ bỏ; kiểm tra opcode C++/V++ bằng
`ctest --test-dir build -R '^vpp-opcode-abi$'`. Khi sửa ABI phải cập nhật
cả định nghĩa C++ và V++.

Các API thủ tục cũ vẫn nhận/trả map, list hoặc handle như trước. Parser chương
trình và regex dùng chung implementation với lớp để tránh hai thuật toán khác nhau.

## Ví dụ sử dụng

```text
nhập gói/biên dịch/bộ biên dịch;

phiên = Bộ Biên Dịch("hàm chính() { in 42; };", ".");
thống_kê = phiên.thống kê();
bytecode = phiên.bytecode();
```

Để gọi API hiện có, tiếp tục dùng `biên dịch phân tích(mã)`, `biên dịch
bytecode(mã)`, `biên dịch cli(...)`, `biên dịch chạy sản phẩm biên dịch(...)` và các API
collection như trước. Những hàm này chuyển yêu cầu vào đối tượng thích hợp.
Tạo một `Bộ Biên Dịch` khi cần thực hiện nhiều truy vấn trên cùng nguồn để
tránh lặp lại công đoạn phân tích.

Các phần mở rộng có thể gọi trực tiếp:

```text
nhập gói/biên dịch/phụ thuộc;
nhập gói/biên dịch/sản phẩm biên dịch;
nhập gói/mạng/rest;

phân_tích = Bộ Phân Tích Phụ Thuộc("nhập bộ sưu tập;", ".");
imports = phân_tích.danh sách();

codec = Bộ Mã Hóa Artifact(artifact);
văn_bản = codec.tuần tự();

phản_hồi = Bộ Dựng Phản Hồi REST(đúng, "ok");
json = phản_hồi.an toàn("null");
```

Chỉ mục AST có thể được dùng độc lập bằng `Bộ Chỉ Mục Khai Báo(cây)` và
`tìm(tên)`. Lớp `Bộ Biên Dịch` giữ chỉ mục đã lập để tra nhiều khai báo trên
cùng snapshot. Nếu ứng dụng chỉnh sửa danh sách AST sau khi đã lập chỉ mục,
gọi `làm mới()` trước truy vấn kế tiếp.

`Bộ Đăng Ký Hàm` nhận tham số `hoãn_chuỗi` (mặc định `sai`) và cung cấp
`đăng ký IR(ir)`, `đăng ký đồ thị(đồ_thị)`, `kết quả()`, `tìm hàm(mô_đun, tên)`
và `tìm mô đun(định_danh)`. Gọi `đăng ký` lần nữa trên cùng đối tượng sẽ
khởi tạo sổ mới. Các API hàm tự do `biên dịch đăng ký hàm IR thuần`,
`biên dịch đăng ký hàm đồ thị thuần` và `biên dịch đăng ký hàm đồ thị hoãn chuỗi`
được giữ nguyên, đi qua lớp này. Việc cấp Function ID, StringPool và xử lý
alias/export vẫn theo thứ tự emitter/linker C++ để bảo đảm ABI bytecode.

```text
nhập gói/kiểm thử/chính;

bộ = Bộ Kiểm Thử(sai);
ca = Ca Kiểm Thử(hàm() { trả về 1; });
bộ.đăng ký("kiểm tra cơ bản", ca);
báo_cáo = bộ.chạy();
```

`báo_cáo` có `tổng`, `đạt`, `lỗi` và danh sách `kết quả`; mỗi kết quả gồm tên,
trạng thái, giá trị hoặc lỗi. Chạy nhiều lần đặt lại báo cáo cho lần chạy mới
và không bỏ qua setup/teardown của `Ca Kiểm Thử`.

Các hàm `phụ thuộc danh sách`, `biên dịch dựng đồ thị nguồn thuần`,
`biên dịch đồ thị duyệt`, `biên dịch artifact *` và `rest json *` vẫn giữ
chữ ký. `Bộ Dựng Đồ Thị Mô Đun.dựng()` khởi tạo lại DFS trên cùng bảng nguồn
nếu được gọi nhiều lần. Artifact tiếp tục dùng định dạng `VPP bytecode`
phiên bản 1 và `Instruction 4*i32`. Hai chế độ response REST giữ quy tắc cũ:
`an toàn` escape thông báo bằng JSON helper, `object` dùng chuỗi raw như
trước để tương thích; đầu vào cho chế độ `object` cần được kiểm soát.

`Ngăn Xếp` và `Hàng Đợi` kế thừa `Bộ Đệm Danh Sách`; hàng đợi ghi đè
`số lượng()` để trừ vị trí đầu, còn phương thức `trống()` dùng đa hình.
`Bộ Ghi Nhật Ký` dùng `đóng()` của lớp cơ sở; `Kết Nối Cơ Sở Dữ Liệu`
ghi đè `đóng()` để giải phóng kết nối và xoá mật khẩu.

## Kiểm chứng

Kiểm thử hồi quy `src/tests/kiem_tra_kien_truc_oop_compiler.vi` bao phủ
truy vấn snapshot, wrapper API, artifact ABI và kế thừa collection. Các test
logger/DB và bộ kiểm tra pipeline OOP kiểm soát tương thích qua module.
Kiểm thử `src/tests/kiem_tra_lop_mo_rong_compiler.vi` bao phủ các lớp mới,
wrapper cũ và kiểm tra dữ liệu artifact sai cấu trúc/phiên bản.
Kiểm thử `src/tests/kiem_tra_chi_muc_ast_bo_kiem_thu.vi` kiểm tra thứ tự
chỉ mục AST, thao tác làm mới, truy vấn lặp và báo cáo nhiều ca kiểm thử.
Kiểm thử `src/tests/kiem_tra_bo_dang_ky_ham.vi` kiểm tra sổ theo phiên,
chuyển qua lại IR/đồ thị, thứ tự Function ID, hoãn StringPool và báo lỗi trùng.
Kiểm thử `src/tests/kiem_tra_kien_truc_vi.vi` kiểm tra registry, bí danh và
target triple; `kiem_tra_native_machine_vi.vi`, `kiem_tra_native_leaf_vi.vi`
và `kiểm tra object writer.vi` kiểm tra các hàm phát mã và ghi object.
Chạy `ctest --test-dir build -R '^vpp-opcode-abi$'` để đối chiếu
bảng opcode V++ với enum ABI C++ sau khi chỉnh bộ sinh.

Việc chuyển compiler tự chủ sang kiến trúc lớp **không thay thế** việc hoàn
thiện code generation native, closure và FFI; những phần này có kiểm thử
riêng và cần tiếp tục hoàn thiện theo backend thực tế.

## Phiên và adapter thư viện

```text
nhập gói/văn bản/quét và tách chuỗi;
nhập gói/lõi/biểu thức chính quy;
nhập gói/biên dịch/phiên tự chủ;

bộ_quét = Bộ Quét("12 3.5 Việt");
số = bộ_quét.đọc số nguyên();
thực = bộ_quét.đọc số thực();
bộ_quét.đóng();

mẫu = Biểu Thức Chính Quy("([a-z]+)([0-9]+)");
đầu = mẫu.tìm("x12");
sau = mẫu.thay("y34", "$2:$1");

phiên = Bộ Biên Dịch Tự Chủ("hàm chính() { in 42; };");
cây = phiên.cây cú pháp();
ir = phiên.hạ IR();
bytecode = phiên.bytecode();
phiên.làm mới("in 7;");
```

`Bộ Tách Từ Vựng(nguồn = "")` cung cấp `từ tố thô()`, `token()` và
`làm mới(nguồn)`. Hai truy vấn token tái sử dụng cache riêng của đối tượng;
token hợp nhất dùng lại từ tố thô đã tách. `làm mới()` kiểm tra kiểu trước
khi thay nguồn và bỏ cache, nên đối số sai không làm mất kết quả hiện có.
Các danh sách trả về là tham chiếu cache; không sửa chúng giữa các truy vấn.
Các phương thức cũ `biên dịch từ tố hóa thô(nguồn)` và
`biên dịch từ tố hóa(nguồn)` vẫn nhận nguồn trực tiếp và không đổi cache phiên.
Các hàm cùng tên ở cấp mô-đun tạo phiên mới cho mỗi lần gọi.
Kiểm thử `kiem_tra_tu_vung_oop.vi` bao phủ cache, tách trạng thái, chỉ số UTF-8,
nguồn rỗng, phục hồi sau lỗi và tương thích các API này trên interpreter/JIT.

`Bộ Hạ IR(nguồn).hạ()`, `Bộ Phát Bytecode(nguồn).phát()` và
`Bộ Liên Kết Bytecode(nguồn).liên kết()` giữ kết quả theo nguồn của từng
đối tượng. `làm mới(nguồn)` kiểm tra kiểu trước khi thay nguồn và xóa cache.
Kết quả được trả theo tham chiếu; không sửa trực tiếp giữa các truy vấn.
Các phương thức dài tên và hàm mô-đun cũ vẫn xử lý tham số trực tiếp,
không thay cache của phiên.

Danh mục [rà soát toàn thư viện](library-oop-audit.md) liệt kê mọi tệp,
lớp tiện ích, lớp có trạng thái và số khai báo hàm/phương thức.
Bảng báo cáo lịch sử hiện được giữ tĩnh; script Python sinh bảng đã bỏ.
Chạy `ctest --test-dir build -R '^vpp-library-visibility$'` để kiểm tra
phạm vi khai báo kiểu, đã đăng ký trong CTest.

Import trực tiếp module khai báo lớp hoặc facade tương ứng. `Danh Sách` có
trong `gói/bộ sưu tập/chính`; `Bộ Quét` có trong `gói/văn bản/chính`;
`Tài Liệu JSON` có trong `gói/dữ liệu/json/chính`; hai lớp compiler mới có
trong `gói/biên dịch/chính`. DOM, rowset, ZIP, tùy chọn, REPL, worker và
quản lý gói tiếp tục là module tùy chọn, được import khi cần.

`Danh Sách.danh sách()` và `Tập Bản Ghi.danh sách()` trả bản sao danh sách
ngoài; các giá trị lồng nhau vẫn dùng tham chiếu như API collection hiện có.
`Phần Tử DOM.cây()` và `Tài Liệu JSON.giá trị()` cho phép chỉnh dữ liệu đã
nạp trực tiếp; đối tượng node con của DOM nhìn cùng cây với đối tượng cha.
`Dự Án Gói.manifest()` và các kết quả thêm/lưu trả bản sao JSON độc lập.

`Bộ Biên Dịch Tự Chủ` dành cho một đơn vị nguồn; import liên mô-đun tiếp tục
đi qua đồ thị/linker hiện có. Kết quả token, AST, IR và bytecode được giữ
theo tham chiếu trong phiên: không sửa chúng giữa các truy vấn; gọi
`làm mới(nguồn)` để bỏ toàn bộ cache và bắt đầu phiên phân tích mới.
`Bộ Phân Tích Cú Pháp.phân tích()` luôn dựng AST mới và đặt lại cursor.

Các lớp `Bộ Quét`, `Bộ Sinh Ngẫu Nhiên`, `Hệ Tệp ZIP`, `Phiên Tương Tác`
và `Luồng Công Việc` triển khai `Đóng Được`. Gọi `đóng()` tường minh:
lần đầu trả đúng, lần tiếp theo trả sai; thao tác cần tài nguyên sau đóng
báo lỗi. Đây là quản lý vòng đời tường minh, không có destructor tự động.
`Luồng Công Việc.đóng()` có thể chờ callback hợp tác với yêu cầu hủy.
Các lớp khác là dữ liệu hoặc buffer, không cần vòng đời đóng.

`Bộ Sinh Mã Bản Địa(triple)` chia sẻ target cho `lá`, `hàm máy` và `object`.
Phạm vi target/backend vẫn do encoder và object writer hiện có quyết định;
lớp không mở rộng tập lệnh hay ABI được hỗ trợ.

Sáu test `kiem_tra_van_ban_oop`, `kiem_tra_trang_thai_oop`,
`kiem_tra_du_lieu_oop`, `kiem_tra_tai_nguyen_luu_tru_oop`,
`kiem_tra_phien_bien_dich_oop`, `kiem_tra_native_oop` được đăng ký cho
interpreter và JIT trong runner Unix và Windows. Chúng kiểm tra tách trạng
thái instance, lỗi không làm đổi dữ liệu, đóng lặp, tương thích API thủ tục
và byte object trên ba định dạng. Pipeline OOP còn dựng/chạy `Danh Sách`
bằng compiler V++ để kiểm tra kế thừa và phương thức riêng qua import.
