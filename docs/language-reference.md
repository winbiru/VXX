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
} nếu không {
    in "chưa đạt mức giỏi";
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
được export. Dùng `công khai nhập ...` để re-export:

```vi
công khai nhập toan như toán;
```

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
`gói/lõi/entropy.vi` ghép các khối thành 1–4096 byte; `ngẫu nhiên bảo mật`
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
Hai đối số `rỗng` được chuẩn hóa thành danh sách/từ điển rỗng trước spawn.
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
