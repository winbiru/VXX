# Toolchain V++ tự chủ trên macOS ARM64: khảo sát và lộ trình

Ngày khảo sát: 17/10/2026. Đích được chọn: macOS thử nghiệm, bỏ cả
`libSystem`, giữ kiến trúc ARM64 hiện có.

## Kết luận có thể kiểm chứng

Viết runtime, linker và signer bằng V++ có thể bỏ compiler C/C++, nguồn
runtime C, `clang`, `ld` và chương trình `codesign` khỏi quá trình dựng.
Điều đó **chưa đủ** để loại `libSystem` khỏi tiến trình trên macOS ARM64
với kernel release hiện hành.

Có ba chốt độc lập trong mã nguồn Apple:

1. XNU `parse_machfile` từ chối `MH_EXECUTE` ARM64 không có `MH_DYLDLINK`
   trên cấu hình release. Nhánh cho phép static x86-64 không áp dụng cho
   native ARM64. Cấu hình DEVELOPMENT/DEBUG có nhánh khác.
2. XNU `load_dylinker` chỉ chấp nhận đường dẫn `/usr/lib/dyld` ở cấu hình
   release; đặt `LC_LOAD_DYLINKER` tới loader V++ riêng không giải quyết
   được rào cản này.
3. `MachOFile::forEachDependentDylib` của dyld tự thêm `libSystem.B.dylib`
   khi executable không khai báo dylib. `dyldMain` còn kiểm tra trạng thái
   libdyld/libSystem trước khi chuyển tới entrypoint.

Nguồn chính: [XNU mach_loader.c](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/mach_loader.c),
[dyld MachOFile.cpp](https://github.com/apple-oss-distributions/dyld/blob/main/common/MachOFile.cpp),
[dyldMain.cpp](https://github.com/apple-oss-distributions/dyld/blob/main/dyld/dyldMain.cpp).
Đây là mã nguồn nhánh `main` tại thời điểm khảo sát, không khẳng định đó là
đúng snapshot nguồn của kernel đang chạy.
[Apple QA1118](https://developer.apple.com/library/archive/qa/qa1118/_index.html)
và [tài liệu Swift về static SDK](https://www.swift.org/documentation/articles/static-linux-getting-started.html)
cũng mô tả việc Apple không hỗ trợ executable tĩnh và không bảo đảm ABI
syscall Darwin trực tiếp. QA1118 là tài liệu lưu trữ; kết luận về máy này
còn dựa trên thử nghiệm thực bên dưới.

## Probe thực trên máy hiện tại

Trong khảo sát ngày 17/10/2026, một probe Python (nay đã được gỡ khỏi repo) tạo bốn Mach-O ARM64
bằng thư viện chuẩn Python, gồm segment, entrypoint, `LC_BUILD_VERSION`,
`LC_CODE_SIGNATURE` và CodeDirectory SHA-256 v0x20400. Không gọi compiler,
assembler, linker hoặc công cụ ký để **sinh** các tệp này. Tùy chọn
`--verify-signatures` chỉ chạy `codesign --verify --strict` để kiểm tra
chữ ký đã được probe tạo. Python là công cụ khảo sát; đây chưa phải
compiler, linker hay signer V++ production.

Mã máy của probe gọi trực tiếp `write` và `exit`: số syscall trong X16,
đối số trong X0–X2, trap `SVC #0x80`; không gọi hàm từ dylib. Kết quả
đúng là stdout `V++ direct syscall probe\n` và mã thoát `52`.

Môi trường đo: macOS **26.6.2**, Darwin **25.6.0**, ARM64,
`xnu-12377.161.14~5/RELEASE_ARM64_T6030`.

| Trường hợp | Khai báo dylib | Kết quả thực |
| --- | --- | --- |
| `dynamic-no-imports`: LC_MAIN, `/usr/lib/dyld` | 0 | In đúng thông báo, exit 52; trace cho thấy `libSystem.B.dylib` và `libsystem_c.dylib` được nạp. |
| `static-no-dyld`: LC_UNIXTHREAD, không có dyld | 0 | Bị SIGKILL (`returncode = -9`), không hoàn tất probe. |
| `private-loader`: MH_DYLINKER riêng | 0 | Chữ ký được xác minh; không chạy độc lập như executable. |
| `custom-loader-main`: trỏ tới MH_DYLINKER riêng đã tạo | 0 | Bị SIGKILL (`returncode = -9`), không hoàn tất probe. |

Cả bốn tệp đều qua `codesign --verify --strict`. Kết quả SIGKILL phù hợp
với các chốt trong mã nguồn XNU; riêng việc bị kill không xác định đầy đủ
nguyên nhân nếu thử trên kernel khác hoặc với một định dạng Mach-O khác.
Probe giữ môi trường chạy tối thiểu, không kế thừa các biến `DYLD_*` từ
tiến trình cha, rồi bật `DYLD_PRINT_LIBRARIES=1` để lưu trace. Phân tích
load command và quan sát thư viện lúc chạy là hai phép kiểm tra riêng.

Lượt đo đầu được lưu tại
`build-p0-release/freestanding-macos-probe/report.json`, cùng executable
và các tệp `*.dyld.log`. Thư mục build là artifact cục bộ, không phải
nguồn cần đưa vào Git. Probe Python gốc đã bị xóa khi loại bỏ công cụ Python;
thử nghiệm này hiện chưa có lệnh tái chạy tương đương.

Trong phép thử cũ, mã thoát `0` chỉ có nghĩa probe chạy thành công,
**không** có nghĩa đã đạt độc lập. Cờ `--require-no-libsystem` khi đó trả
mã `2` nếu static executable chưa thực sự chạy được. Probe cũng từ chối
thay thế image đã có trong thư mục đầu ra.

## Môi trường cần có để đạt yêu cầu nghiêm ngặt

Nhánh nghiên cứu cần một môi trường Darwin/XNU DEVELOPMENT hoặc DEBUG
riêng, cho phép nạp Mach-O ARM64 tĩnh. Điều này là **điều kiện cần** theo
mã nguồn đã đọc, chưa phải bằng chứng rằng chỉ đổi cấu hình kernel là đủ:
code signing, AMFI, entrypoint và tương thích phần cứng vẫn phải được
kiểm chứng trong môi trường đó. Một VM Linux chạy trên Mac không được
tính là đạt yêu cầu macOS.

Giữ máy phát triển hiện tại làm host và kiểm chứng trong guest/máy thử
riêng khi có môi trường Darwin phù hợp. Khảo sát này không thay kernel,
SIP, AMFI, boot arguments, `/usr/lib/dyld` hoặc thư viện hệ thống trên
host. Viết thêm API trong repo không thay thế được điều kiện nền tảng này.

## Thứ tự thực hiện sau khi mở được chốt nền tảng

| Mốc | Thay đổi cụ thể | Điều kiện hoàn thành |
| --- | --- | --- |
| 0. Nạp executable | Chạy lại static probe trên môi trường Darwin đã chọn. | Chữ ký hợp lệ, stdout đúng, exit 52, không dyld và không libSystem. Chưa đạt trên host hiện tại. |
| 1. ABI hệ điều hành | Thêm primitive backend cho địa chỉ 64 bit, load/store byte/i32/usize và syscall Darwin; giữ handle i32 ở API cao hơn nếu cần. | Không ép pointer 64 bit xuống i32; kiểm thử lỗi syscall, carry flag và quy tắc bảo toàn thanh ghi. |
| 2. Runtime V++ | Chuyển byte buffer, cấp phát, argv/env, file và directory sang `.vi`; startup và lớp syscall tối thiểu được backend phát thẳng thành mã máy. | Compiler chạy được lexer/parser/IR/codegen bằng runtime mới, không liên kết `vpp_aot_runtime.c`, libc hay CRT. |
| 3. Linker và signer V++ | Giải symbol/BRANCH26 giữa các object; dựng MH_EXECUTE, segment, entrypoint; SHA-256 và chữ ký ad-hoc do V++ thực hiện. | Sinh executable trực tiếp; không khởi chạy `clang`, `ld`, `codesign` hoặc trình dịch khác. |
| 4. Cache tự chủ | Chuyển hash, fingerprint compiler, đọc/ghi cache và công bố nguyên tử sang runtime V++; thay CommonCrypto và thao tác file C. | Cold/warm/disabled tạo cùng artifact; phát hiện cache hỏng, đổi compiler và đổi chữ ký hàm. |
| 5. Bootstrap tự chủ | Phát hành seed compiler V++ đã kiểm tra hash cùng nguồn runtime/linker; dựng Stage 2 và Stage 3 trong môi trường chỉ có seed và nguồn V++. | Không cần compiler/VM C++; không truy cập nguồn C/header/SDK; không gọi tiến trình toolchain ngoài. |
| 6. Kiểm chứng | Giữ Mach-O deterministic; đối chiếu Stage 2/3 và chương trình đa module, I/O, cache, lỗi đầu vào. | SHA-256 toàn artifact Stage 2 = Stage 3; quan sát lúc chạy không có libSystem/libc/libc++; executable lỗi không ghi đè artifact cũ. |

Ở mốc 1, i32 subset hiện tại chưa biểu diễn pointer/usize đúng miền ARM64;
không thể chỉ thay tên hàm `malloc` bằng hàm V++ rồi coi như đã có allocator.
Startup phải tự nhận trạng thái entrypoint và argv; file I/O phải giữ
ngữ nghĩa short read/write, lỗi và tài nguyên. Các vùng text/data cần
quyền riêng phù hợp; không lấy vùng RWX làm điều kiện mặc định.

Ở mốc 3, định dạng object và cache hiện có tiếp tục hữu ích. Phần cần
thay ở `driver.vi` là lời gọi `liên_kết_đối_tượng` ra C; phần cuối
`vpp_aot_runtime.c` hiện còn chạy `/usr/bin/clang`. Runtime mới phải được
link như mã V++/mã máy nội bộ, không bọc lại runtime C dưới tên khác.

Ở mốc 5, seed binary là đầu vào bootstrap cần công bố rõ. Tự dựng bằng
seed V++ mà không cần C++ khác với chứng minh lịch sử tạo seed chưa từng
qua C++. Muốn thu hẹp chuỗi tin cậy từ đầu, cần thêm bootstrap seed nhỏ,
nguồn và biểu diễn mã máy có thể kiểm tra độc lập; đó là một mốc riêng.

## Phạm vi không được đánh đồng

Các mốc trên đầu tiên áp dụng cho compiler native i32 subset đã có.
Độc lập của subset chưa đồng nghĩa toàn bộ compiler production, VM,
OOP/lambda/GC, stdlib và FFI hiện hành đã được chuyển khỏi C/C++.

Không có `LC_LOAD_DYLIB`, `otool -L` trống, hay một syscall probe chạy
thành công đều không đủ để công bố “không libSystem”. Điều kiện đó phải
được xác nhận khi tiến trình thực sự chạy. Trên host release đã đo,
**mục tiêu hoàn toàn không libSystem chưa đạt**; chốt kế tiếp là môi
trường nạp executable ARM64 tĩnh, trước khi triển khai thay toàn runtime.
