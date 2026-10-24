# Tham chiếu ngôn ngữ V++ 1.0

Tài liệu này mô tả bề mặt ngôn ngữ V++ 1.0 đang được compiler/runtime triển khai.
Quy tắc chi tiết về scope, equality, truthiness, closure, module và lỗi runtime nằm trong
`docs/semantics.md` và `docs/runtime-errors.md`.

## Chương trình tối thiểu

```vi
hàm chính() {
    in "Xin chào V++";
};
```

`chính` là hàm vào chương trình chuẩn của V++. Compiler tự gọi `chính()` khi chạy
một chương trình. Tên cũ `main` chỉ còn là alias tương thích ngược cho mã nguồn cũ.

Ứng dụng theo hướng đối tượng có thể đặt lớp `Chính` ngay trong tệp nguồn khởi chạy,
không cần khai báo `hàm chính()` riêng:

```vi
lớp Chính {
    hàm công khai chạy() {
        in "Xin chào từ ứng dụng V++";
    };
};
```

Khi thực thi, compiler ưu tiên `hàm chính()`, tiếp đến alias cũ `main()`. Nếu
không có hai hàm đó nhưng có lớp `Chính`, runtime tự tạo `Chính()` rồi gọi
`chạy()` trên đối tượng. `chạy()` phải công khai và không yêu cầu tham số;
`khởi tạo()` cũng không được yêu cầu tham số. Phương thức `chạy()` có thể được
kế thừa từ lớp cha. Lớp `Chính` khai báo mà không có phương thức `chạy()` hợp lệ
sẽ gây lỗi. Những script chỉ chạy câu lệnh top-level vẫn hoạt động như trước.

Tệp `chính.vi` của **gói thư viện** chỉ tập hợp/export API, không bắt buộc có
lớp `Chính` hay hàm khởi chạy. `chính.vi` của **ứng dụng** có thể chọn một trong
hai kiểu điểm vào ở trên.

V++ dùng dấu `;` để kết thúc phần lớn statement. Identifier hỗ trợ Unicode, vì vậy tên tiếng
Việt có dấu có thể dùng trực tiếp.

## Giá trị và toán tử

Các giá trị nền tảng gồm số nguyên, số thực, chuỗi, boolean `đúng`/`sai`, `rỗng`, list, map,
tuple, set, function/closure, class và instance.

```vi
số = 42;
tỷLệ = 3.5;
tên = "V++";
cóHiệuLực = đúng;
khôngCó = rỗng;
danhSách = [1, 2, 3];
dữLiệu = {"tên": "V++", "phiên bản": 1};
```

Toán tử chính: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`,
`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `++`, `--`.

`+` cộng số khi hai toán hạng là số; nếu có toán hạng phi số, runtime nối biểu diễn chuỗi.
Chi tiết promotion, equality, ordering và truthiness được khóa trong `docs/semantics.md`.

## Điều kiện và vòng lặp

```vi
nếu (điểm >= 8) {
    in "giỏi";
} hoặc nếu (điểm >= 5) {
    in "đạt";
} hoặc {
    in "chưa đạt";
};

tổng = 0;
lặp(i = 0; i < 10; i++) {
    nếu (i == 5) {
        bỏ qua;
    };
    tổng += i;
};
```

`thoát;` rời vòng `lặp` hoặc nhánh `chọn` gần nhất đang bao quanh câu lệnh;
`bỏ qua;` chuyển sang lượt lặp tiếp theo. Khi lồng `lặp` và `chọn`,
`thoát;` chỉ kết thúc cấu trúc gần nhất, sau đó chương trình tiếp tục
thực thi các lệnh bên ngoài cấu trúc đó.

```vi
chọn (mã) {
    ca 200: {
        in "thành công";
    }
    ca 404: {
        in "không tìm thấy";
    }
    mặc định: {
        in "khác";
    }
}
```

## Hàm, tham số mặc định và lambda

```vi
hàm cộng(a, b = 10) {
    trả về a + b;
};

nhânĐôi = hàm(x) {
    trả về x * 2;
};

in cộng(5);
in nhânĐôi(6);
```

V++ dùng dynamic typing. Compiler kiểm tra số đối số khi đích gọi được biết chắc; các lời gọi
động được kiểm tra lại ở runtime. Closure capture binding lexical bằng ô nhớ dùng chung, vì
vậy mutation sau khi tạo closure vẫn quan sát được từ closure.

## Module và import

```vi
nhập lõi;
nhập feature_math như toán;
nhập gói/mạng/rest;
```

Mỗi câu `nhập` nhận một target module/package. Import tương đối được phân giải từ thư mục của
module đang import. Alias tạo namespace, ví dụ `toán.tổng(2, 3)`.

Khai báo top-level mặc định/công khai nằm trong bề mặt export; `riêng tư` và `bảo vệ` không
được export. File điểm vào gói `chính.vi` tự chuyển tiếp các thành phần công khai
từ các module được nhập trực tiếp (không có alias):

```vi
// gói/hệ thống/chính.vi
nhập gói/hệ thống/môi trường;
nhập gói/hệ thống/lệnh tiến trình;
```

Người dùng chỉ cần `nhập gói/hệ thống/chính;` để sử dụng lớp `Lệnh Tiến Trình`.
Import bên trong file thông thường vẫn chỉ dùng nội bộ; cú pháp `công khai nhập`
cũ tiếp tục được hỗ trợ cho mã hiện có. Import với alias không tự chuyển tiếp,
để tránh vô tình mở rộng API của gói. Tên lớp và tên file có thể có khoảng trắng.

Import cycle local bị từ chối ở compile time và diagnostic chứa chuỗi module gây chu trình.

## Gọi thư viện C bằng System FFI (POSIX)

Khai báo binding ở cấp module bằng `ngoại thư viện` và `ngoại hàm`:

```vi
ngoại thư viện c_hệ_thống = "system.c";
ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ c_hệ_thống
    ký hiệu "getenv" abi "c" khả năng "system.env.read";

in getenv("HOME");
```

`system.c` là tên thư viện logic được runtime ánh xạ tới symbol của tiến trình
trên POSIX. Khai báo chỉ có hiệu lực khi VM host cấp đúng **capability**;
`system.env.read` chỉ được phép gọi `getenv(c_chuỗi):c_chuỗi`, không cấp quyền
gọi libc tùy ý. Không được xem chuỗi capability bất kỳ trong source là quyền
truy cập OS. Đích Windows hiện chưa có backend FFI tương đương.

MVP dùng `abi "c"` với kiểu tham số `i32`, `u32`, `i64`, `u64`, `f64`,
`c_chuỗi`; kết quả còn nhận `void`/`rỗng`. Số V++ mặc định là `int32`;
`i64/u64` được runtime giữ chính xác dưới dạng số ABI boxed; kết quả `u32`
vượt `INT32_MAX` cũng trả về boxed. Hiện có thể
truyền số lớn cho tham số `u32/i64/u64` dưới dạng **chuỗi thập phân** không dấu
cách, hoặc truyền lại số boxed trả từ FFI; ví dụ `f("18446744073709551615")`
với `f` khai báo tham số `u64`. `c_chuỗi` cấm byte NUL bên trong.
`c_đệm_ra` là kiểu tham số bộ đệm ghi ra có giới hạn: nhận danh sách V++
(1–65536 phần tử), tạo vùng nhớ native tạm và sao chép byte trả về vào danh
sách sau lời gọi. Runtime giới hạn kiểu này trong những binding được xác minh,
bao gồm `clock_gettime(i32, c_đệm_ra):i32` của `system.c` với capability
`system.time.monotonic` (chỉ `CLOCK_MONOTONIC`) hoặc `system.time.realtime`
(chỉ `CLOCK_REALTIME`). Đệm phải đúng 16 byte, target phải có layout
`timespec` POSIX LP64 little-endian tương thích. Hai quyền được kiểm tra
độc lập. Binding thứ ba là `getentropy(c_đệm_ra,u64):i32` với quyền
`system.entropy.read`: vùng đệm 1–256 byte, kích thước `u64` phải khớp
**chính xác** vùng đệm và target có `size_t` 64-bit. Khi `getentropy` báo lỗi,
runtime không chép vùng đệm có thể chỉ ghi một phần về danh sách V++.
`gói/lõi/nguồn ngẫu nhiên.vi` ghép các khối thành 1–4096 byte; `ngẫu nhiên bảo mật`
và `ngẫu nhiên nguyên` dùng đường này trên POSIX. Windows giữ primitive VM.

Trên POSIX, thư viện tệp dùng `fopen(c_chuỗi,c_chuỗi):c_tệp`,
`fread(c_đệm_ra,u64,u64,c_tệp):u64`,
`fwrite(c_đệm_vào,u64,u64,c_tệp):u64`,
`fclose(c_tệp):i32` và `ferror(c_tệp):i32`, tương ứng năm quyền
`system.file.open/read/write/close/error`. `c_đệm_vào` nhận danh sách byte
1–65536 phần tử và sao chép vào vùng nhớ native chỉ đọc; `c_đệm_ra` sao chép
dữ liệu sau `fread`. `size=1`, `count` phải khớp chính xác độ dài đệm và target
có `size_t` 64-bit. `c_tệp` là mã định danh sở hữu bởi một VM, được đóng hoặc
dọn khi VM kết thúc/reset; không phải địa chỉ `FILE*` tùy ý. Chế độ mở chỉ nhận
`rb`, `wb`, `ab`. Public wrapper dùng chunk 4096 byte và giữ lỗi có thể bắt bằng
`thử/bắt lỗi`. Quy tắc kiểm tra hồi quy trên POSIX dùng
`VPP_STRICT_SYSTEM_FFI_FILE=1` để vô hiệu hóa opcode tệp cũ; Windows hiện vẫn
dùng primitive native.

`access(c_chuỗi,i32):i32` chỉ được phép dùng với `system.fs.exists` và
`F_OK=0` để kiểm tra đường dẫn tồn tại có đi theo symbolic link. Các quyền
`system.fs.mkdir` và `system.fs.remove` chỉ mở binding đã xác minh tương ứng.
`stat(c_chuỗi,c_đệm_ra):i32` và `lstat(c_chuỗi,c_đệm_ra):i32`
dùng quyền `system.fs.stat`/`system.fs.lstat` riêng. Buffer phải đúng 256 byte,
runtime kiểm tra `struct stat` trên target POSIX LP64 và
`stat_mode_offset():i32` qua `system.ffi.layout` trả offset `st_mode` từ SDK,
để thư viện V++ đọc `mode` và phân loại đường dẫn theo hoặc không theo symlink.
`opendir(c_chuỗi):c_thư_mục`, `readdir(c_thư_mục):c_mục_thư_mục`,
`closedir(c_thư_mục):i32` có quyền `system.fs.dir.open/read/close` độc lập.
`c_thư_mục` chỉ là token VM sở hữu, được dọn khi reset/hủy; kết quả
`c_mục_thư_mục` là chuỗi tên đã sao chép hoặc `rỗng` ở EOF, không lộ `dirent*`.

`localtime_r(c_đệm_vào,c_đệm_ra):c_cờ_con_trỏ` qua `system.time.local`
nhận `time_t` little-endian 8 byte và `struct tm` output đúng 64 byte;
runtime kiểm tra layout offset fields LP64. `c_cờ_con_trỏ` trả 1 khi thành công,
0 khi pointer null, không lộ địa chỉ native và không sao chép output khi lỗi.
`gói/thời gian/địa phương.vi` dùng binding này để dựng ISO-8601 với offset
thay đổi theo DST; Windows tiếp tục dùng primitive đồng hồ cũ.

`gói/hệ thống/tiến trình.vi` cung cấp `chạy tiến trình(chương_trình,
đối_số = rỗng, môi_trường = rỗng, giới_hạn_mili_giây = rỗng)`.
Hai đối số `rỗng` được chuẩn hóa chuyển thành danh sách/từ điển rỗng trước spawn.
`đối_số` là danh sách chuỗi argv literal, không chạy shell ngầm. `môi_trường`
là từ điển chuỗi được ghi đè lên môi trường kế thừa của tiến trình con.
Hàm trả từ điển có các khóa `"khởi chạy"` (bool), `"mã thoát"` (int),
`"đầu ra"` (stdout) và `"lỗi"` (stderr hoặc thông báo lỗi). Trên POSIX,
chín binding `system.process.new/arg/env/start/poll/read/wait/error/close`
giữ token trong VM, để V++ đồng thời đọc hai pipe theo từng khối 4096 byte.
Exit code của tiến trình chết vì tín hiệu là `128 + số tín hiệu` (ví dụ
SIGTERM trả 143). `giới_hạn_mili_giây` là số nguyên dương tùy chọn; khi
quá hạn, hàm trả `"mã thoát": -1`, stdout/stderr đã thu được và nối thông
báo `chạy tiến trình: quá thời hạn` vào `"lỗi"`, đồng thời gửi SIGKILL
cho nhóm tiến trình riêng và thu hồi leader; tiến trình tự tách nhóm không
thuộc phạm vi này. Windows hiện dùng opcode tương thích và chưa hỗ trợ
tham số giới hạn thời gian này.

Pointer/handle tùy ý, quan hệ độ dài bộ đệm tổng quát và truyền struct native
by-value chưa hỗ trợ. Số ABI boxed có so sánh, hiển thị và phép cộng/trừ/
nhân/chia/dư nguyên có kiểm tra tràn số, nhưng chưa có constructor công khai;
chuyển thành `f64` chỉ chấp nhận miền nguyên chính xác `[-2^53, 2^53]`.

Binding sai chữ ký thực tế của hàm C có thể làm sập tiến trình; nên sử dụng
những binding System đã kiểm chứng trong thư viện chuẩn. Chi tiết capability,
POSIX `errno` và lộ trình xem [kế hoạch System FFI](system-ffi-migration-plan.md).

## Lớp, kế thừa và interface

```vi
giao diện CóTên {
    hàm tên();
}

lớp Nền {
    hàm bảo vệ mãNộiBộ() {
        trả về "SP";
    };
}

lớp công khai SảnPhẩm kế thừa Nền triển khai CóTên {
    hàm khởi tạo(tên) {
        mình.tênSảnPhẩm = tên;
    };

    hàm công khai tên() {
        trả về mình.tênSảnPhẩm;
    };

    hàm công khai mã() {
        trả về gốc.mãNộiBộ();
    };
}

sảnPhẩm = SảnPhẩm("Cà phê");
in sảnPhẩm.tên();
```

`mình` là receiver hiện tại. `gốc` bắt đầu lookup từ superclass và có thể dùng để gọi method
hoặc constructor lớp cha. Class chỉ kế thừa một class; một class có thể `triển khai` nhiều
interface, còn interface có thể kế thừa nhiều interface. Interface là contract compile-time
trong 1.0.

Method hỗ trợ `công khai`, `bảo vệ`, `riêng tư`. Field hiện là thuộc tính động trên instance.
Constructor dùng tên `khởi tạo` và có thể có tham số mặc định.
Lớp con khai báo constructor riêng và gọi `gốc.khởi tạo(...)` khi cần khởi tạo
lớp cha. Lời gọi nối tiếp và truy cập trên giá trị tạm được hỗ trợ, ví dụ
`nút.các con()[0].tên()` hoặc `danh_sách[0].trường = giá_trị`.
Closure được tạo trong phương thức giữ receiver và quyền truy cập của lớp
khai báo, kể cả khi được gọi sau khi phương thức đã trả về.

## Exception của ngôn ngữ

```vi
thử {
    ném "dữ liệu không hợp lệ";
} bắt lỗi (e) {
    in e;
};
```

`ném` có thể mang bất kỳ giá trị runtime nào. `bắt lỗi` bắt exception do chương trình ném;
lỗi VM fatal như chia cho 0 hoặc bytecode/state không hợp lệ tuân theo contract riêng trong
`docs/runtime-errors.md`.

## Collection và Unicode

List/map hỗ trợ literal, index và mutation. Stdlib cung cấp helper cho set/tuple, map, text,
JSON, file, path, config, time, HTTP và các nhóm tiện ích khác.

Thư viện chuẩn chọn API theo bản chất thành phần: chuỗi, toán học và mã hóa
tiếp tục dùng hàm/module; các tài nguyên và trạng thái được bọc bằng lớp V++.
Các điểm vào `chính.vi` tự xuất lại các lớp công khai của module đã nhập,
trong khi API hàm trước đây vẫn dùng được qua đường dẫn module hiện có.

| Thành phần | Import điểm vào | Lớp / API |
| --- | --- | --- |
| HTTP Client | `gói/mạng/http/chính` | `Khách HTTP(url_gốc)`: `tạo URL`, `yêu cầu`, `lấy`, `gửi`, `cập nhật`, `xóa`, `đóng` |
| Database Connection | `gói/dữ liệu/cơ sở dữ liệu/chính` | `Kết Nối Cơ Sở Dữ Liệu(driver, url, tài_khoản, mật_khẩu)`: `thông tin`, `đang kết nối`, `kết nối`, `truy vấn`, `ping`, `đóng` |
| File Stream | `gói/nhập xuất/chính` | `Luồng Tệp(đường, chế_độ)`: `đang mở`, `đọc các byte`, `đọc hết`, `ghi các byte`, `ghi`, `vị trí`, `dịch`, `đẩy đệm`, `đóng` |
| Logger | `gói/lõi/chính` | `Bộ Ghi Nhật Ký(đích, đường, mức_tối_thiểu)`: `đổi mức`, `ghi`, `thông tin`, `cảnh báo`, `lỗi`, `đóng` |
| Collections | `gói/bộ sưu tập/chính` | `Ngăn Xếp`, `Hàng Đợi`, `Tập Hợp`, `Từ Điển` triển khai `Bộ Chứa` (`số lượng`, `trống`) |
| Compiler AST | `gói/biên dịch/chính` | `Nút Cú Pháp`, `Nút Lớp`, `Nút Hàm`, `Truy Cập Cú Pháp`, `Khách Duyệt Cú Pháp`; factory `tạo nút cú pháp` |
| Application | `gói/ứng dụng/chính` | `Bộ Điều Khiển Ứng Dụng(tên, phiên_bản)` triển khai `Vòng Đời Ứng Dụng`: `banner`, `chạy`, `dừng`, `đang chạy` |
| Process | `gói/hệ thống/chính` | `Lệnh Tiến Trình(chương_trình)`: `thêm đối số`, `đặt biến`, `đặt thời hạn`, `chạy`, `kết quả gần nhất` |
| TCP Socket | `gói/mạng/chính` | `Ổ Cắm TCP(tên_máy, cổng, thời_gian_chờ = 5000)`: `kết nối`, `gửi`, `nhận`, `đặt thời gian chờ`, `đang mở`, `đóng` |
| Văn bản | `gói/văn bản/chính` | `Bộ Nối Văn Bản(phân_cách, tiền_tố, hậu_tố, giá_trị_rỗng)`: `thêm`, `gộp`, `kết quả`, `số phần`, `độ dài` |
| Phân trang | `gói/dữ liệu/chính` | `Bộ Phân Trang(trang, kích_thước)`: `đặt trang`, `đặt kích thước`, `độ dời`, `tổng trang` |
| Đường dẫn | `gói/nhập xuất/chính` | `Đường Dẫn(đường)`: `giá trị`, `tên`, `cha`, `nối`, `tồn tại`, `là tệp`, `là thư mục` |
| Đo thời gian | `gói/thời gian/chính` | `Bộ Bấm Giờ` triển khai `Đo Thời Gian`: `bắt đầu`, `dừng`, `đọc mili giây`, `đặt lại` |
| Sự kiện | `gói/lõi/chính` | `Bộ Phát Sự Kiện` triển khai `Đóng Được`: `đăng ký`, `hủy`, `phát`, `số người nghe`, `đóng` |
| Service Registry | `gói/lõi/chính` | `Bộ Nạp Dịch Vụ`: `đăng ký`, `tìm`, `tải` |
| Luồng dữ liệu | `gói/lõi/chính` | `Luồng Truy Vấn(nguồn)` triển khai `Đóng Được`: `ánh xạ`, `lọc`, `bỏ`, `giới hạn`, `phân biệt`, `gom danh sách`, `đếm`, `đóng` |
| Cấu hình | `gói/lõi/chính` | `Cấu Hình Tệp(đường_dẫn)`: `tải lại`, `lấy`, `có` |
| Kiểm thử | `gói/kiểm thử/chính` | `Ca Kiểm Thử(thân, thiết_lập, dọn_dẹp)`: `chạy`, `số lần chạy` |

Hai giao diện AST được khai báo tại `gói/biên dịch/giao diện cú pháp.vi`;
`gói/biên dịch/nút cú pháp.vi` chỉ chứa các lớp và hàm triển khai. Điểm vào
`gói/biên dịch/chính` tái xuất cả hai phần để giữ API import hiện có.

`Đóng Được` là giao diện tài nguyên ở `gói/lõi/giao diện tài nguyên.vi`,
được `Luồng Tệp`, `Bộ Ghi Nhật Ký`, `Khách HTTP`, `Kết Nối Cơ Sở Dữ Liệu`
và `Ổ Cắm TCP`, `Bộ Phát Sự Kiện`, `Luồng Truy Vấn` triển khai.
`đóng()` gọi nhiều lần an toàn, nhưng mỗi lớp
giữ kiểu kết quả cũ để tương thích. Người dùng cần chủ động gọi `đóng()`;
hiện chưa có destructor/finalizer theo vòng đời instance V++.

`Vòng Đời Ứng Dụng` được khai báo trong `gói/ứng dụng/giao diện.vi`,
phần hiện thực ở `gói/ứng dụng/bộ điều khiển.vi`. Lớp bảo vệ trạng thái
chạy/dừng và sử dụng các helper `chạy ứng dụng`, `dừng ứng dụng` cũ.
`Lệnh Tiến Trình` tạo cấu hình argv/env/timeout riêng cho từng instance;
`chạy()` đồng bộ và trả map kết quả của `chạy tiến trình`. `Ổ Cắm TCP`
chỉ kết nối khi gọi `kết nối()` và sở hữu một handle TCP; không hỗ trợ
TLS, UDP hoặc tái kết nối sau khi `đóng()`. Những trường hợp này tiếp tục
dùng API socket/module chuyên biệt.

Ví dụ import và tạo đối tượng:

```vi
nhập gói/mạng/http/chính;
nhập gói/bộ sưu tập/chính;

hàm chính() {
    khách = Khách HTTP("https://example.com/api");
    in khách.tạo URL("/items");
    ngăn = Ngăn Xếp();
    ngăn.đẩy(42);
    in ngăn.rút();
};
```

`Khách HTTP` sử dụng HTTP transport hiện hành cho mỗi request (không duy trì
socket qua các request). `Kết Nối Cơ Sở Dữ Liệu` giữ trạng thái logic: backend
vẫn gọi chương trình DB bên ngoài cho từng truy vấn, không hỗ trợ transaction
hay connection pooling. Mật khẩu lưu trong instance, không được ghi vào log.
`Luồng Tệp` mở handle trong constructor và giữ cursor đến khi `đóng()`.
`dịch(lệch, mốc = 0)` dùng độ lệch byte, mốc 0 = đầu, 1 = hiện tại, 2 = cuối;
`vị trí()` trả số nguyên ABI 64-bit. `đọc các byte(đếm)` nhận 0..65536 byte,
`đọc hết()` đọc từ cursor hiện tại tới EOF. `đẩy đệm()` flush tệp đang ghi;
chế độ `ghi` truncate ngay khi mở, chế độ `nối` luôn ghi ở cuối. Đóng lặp
được chấp nhận; thao tác I/O sau khi đóng ném lỗi. VM thu hồi handle còn mở
khi reset/hủy, không có finalizer theo thời điểm GC cho từng instance.
`Bộ Ghi Nhật Ký` dùng đích `tệp` hoặc `bảng điều khiển`, lọc mức
`thông tin` < `cảnh báo` < `lỗi`. `Nút Cú Pháp` là adapter OOP cho AST dạng
map, nhận cả cây snapshot (`thành viên`) và cây compiler V++ (`phương thức`,
`câu lệnh`, `thân`). `các con()` bao gồm cả khối thân hàm, biểu thức,
điều kiện, hai nhánh `nếu`, các nhánh `chọn`, `thử`/`bắt lỗi`, lời gọi,
chỉ số, collection và giá trị mặc định của tham số. Với từ điển, khóa và
giá trị đều là nút con. `duyệt(khách)` gọi `khách.thăm(nút)` theo thứ tự
cha trước con, kể cả các biểu thức nằm sâu trong phương thức. Parser/IR
vẫn sử dụng map nội bộ; dữ liệu con sai kiểu sẽ báo lỗi tường minh.

Chuỗi nền tảng xử lý length/reverse/index/slice theo code point UTF-8. Các API text 1.0 có
case/normalization dành cho tiếng Việt và NFC theo phạm vi được mô tả trong roadmap; malformed
UTF-8 được kiểm tra tại các native boundary quan trọng.

## Tài liệu liên quan

- `docs/semantics.md`: scope, truthiness, equality, coercion, module, closure và finalizer policy.
- `docs/runtime-errors.md`: exception, VM fault, call boundary, module initialization và stack trace.
- `docs/package-system.md`: manifest, lockfile, SemVer, cache/offline, Git và registry.
- `docs/testing.md`: `vpp kiểm thử` và package `kiểm thử`.
- `docs/cli.md`: contract CLI 1.0.
- `docs/grammar.bnf`: mô tả BNF tham khảo; language reference này là tài liệu người dùng ưu tiên.
