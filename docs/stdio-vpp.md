# Bộ đệm tệp V++

`gói/nhập xuất/bộ đệm tệp.vi` thay phần stdio của libc bằng thuật toán V++.
`đọc tệp`, `ghi tệp`, `ghi nối tệp`, API byte và `Luồng Tệp` dùng chung lớp này.
Các API đó không còn gọi `fopen`, `fread`, `fwrite`, `ferror`, `fseek`, `ftell`
hay `fflush`.

```vi
nhập gói.nhập xuất.chính.*;

hàm chính() {
    tệp = Bộ Đệm Tệp("dữ liệu.bin", "ghi", 4096);
    thử {
        tệp.ghi các byte([0, 255, 65]);
        tệp.đẩy đệm();
    } bắt lỗi(e) {
        thử { tệp.đóng(); } bắt lỗi(lỗi_đóng) {};
        ném e;
    };
    tệp.đóng();
};
```

Constructor nhận đường dẫn UTF-8, chế độ `đọc`/`ghi`/`nối` và kích thước đệm
1..65536 byte (mặc định 4096). Tham số thứ tư tùy chọn là tên thao tác dùng
trong bản ghi lỗi. `Luồng Tệp(đường, chế_độ)` giữ API hiện có.

- `đọc các byte(n)` trả tối đa n byte; n trong 0..65536; EOF trả danh sách rỗng.
- `đọc hết()` trả chuỗi byte còn lại, giữ nguyên NUL và byte nhị phân.
- `ghi các byte(bytes)` kiểm tra toàn bộ đầu vào trước khi chấp nhận byte đầu tiên.
- `ghi(nội_dung)` ghi biểu diễn chuỗi bằng UTF-8.
- `đẩy đệm()` đẩy các byte đang chờ xuống OS; không cam kết đồng bộ xuống đĩa.
- `vị trí()` trả vị trí byte logic, gồm dữ liệu ghi đang chờ và trừ dữ liệu đọc trước.
- `dịch(lệch, mốc=0)` dùng mốc đầu/hiện tại/cuối tương ứng 0/1/2; xóa EOF và
  điều chỉnh phần đọc trước. Offset i64 có thể truyền bằng chuỗi thập phân.
- `đang mở()` và `đóng()` giữ vòng đời; đóng lặp lại an toàn.

Đệm ghi được đẩy khi đầy, khi gọi `đẩy đệm`, khi seek hoặc khi đóng. Ở chế độ
nối, truy vấn vị trí cũng đẩy đệm để lấy vị trí thực sau append. Các byte nối
luôn được ghi ở cuối tệp dù đã seek. Lỗi ghi giữ số byte đã chuyển để lần flush
kế tiếp không ghi trùng tiền tố. Lỗi flush khi đóng vẫn giải phóng handle.

Caller phải đóng stream, kể cả trên đường lỗi. VM reset chỉ thu hồi tài nguyên
OS còn mở; không chạy phương thức V++ để đẩy các buffer bị bỏ quên. Stream
không có cam kết thread-safe.

## Ranh giới ASM và hệ điều hành

`system.file.raw.open/read/write/seek/tell/close` là sáu binding có quyền riêng.
Chúng kiểm tra chữ ký, token thuộc VM, chiều đệm và extent trước khi chạm OS.
Chúng không đi qua bộ phân giải symbol hoặc `libffi`; native chỉ chuyển dữ
liệu giữa biểu diễn VM và ABI máy. V++ giữ buffering, EOF, retry EINTR,
partial write, thông báo lỗi và cleanup cấp thư viện.

`src/runtime/file_machine.cpp` cung cấp primitive bằng ASM:

| Nền tảng | Cơ chế |
| --- | --- |
| macOS ARM64 | `svc #0x80`, số syscall trong x16, lỗi từ carry flag. |
| macOS x86-64 | `syscall`, lớp BSD trong rax, lỗi từ carry flag. |
| Linux ARM64 | `svc #0`, số syscall trong x8, errno âm trong x0. |
| Linux x86-64 | `syscall`, số syscall trong rax, đối số thứ tư trong r10. |
| Windows | Win32 `CreateFileW`, `ReadFile`, `WriteFile`, `SetFilePointerEx`, `CloseHandle`. |
| POSIX khác | Adapter OS hiện có, chờ ASM theo kiến trúc. |

Windows giữ API OS được hỗ trợ; append dùng offset EOF của
[WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile).
Không dùng số syscall nội bộ Windows. Việc dùng syscall trực tiếp Darwin cần
kiểm chứng trên từng phiên bản OS; đây không phải cam kết ABI Darwin ổn định
hoặc tuyên bố executable không nạp libSystem.

FFI stdio cũ còn trong runtime để tương thích binding/bytecode cũ. `libffi`,
TLS provider và runtime C++ của toàn dự án chưa được loại bỏ bởi thay đổi này.

## Kiểm chứng

`kiem_tra_stdio_vpp.vi` kiểm tra dữ liệu nhị phân, flush visibility, biên đệm,
seek tương đối sau read-ahead, EOF/reset, append sau seek, i64, validation và
đóng. Runner chạy trên interpreter/JIT; installed native smoke kiểm tra AOT.
`vpp-file-io-hardening` kiểm tra capability/descriptor, ownership token, extent,
byte sai, short read/write, lỗi syscall và thu hồi tài nguyên. macOS ARM64 đã
chạy thực; các nhánh Linux/Windows và chạy thực macOS x86-64 cần CI tương ứng.
