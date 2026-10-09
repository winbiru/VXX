# Ranh giới thư viện V++ và primitive VM

Đợt 2026-10-09 (process Windows): đường public `chạy tiến trình` trên
Windows nay luôn chạy thuật toán V++ qua `system.process.*` FFI; đã gỡ
`handleNativeProcessPrimitive`, bộ escape argv, dựng môi trường, đọc pipe
và `CreateProcessW` trùng lặp khỏi `vm_native_helpers.cpp` (401 → 170 dòng).
Opcode tiến trình legacy bị từ chối trên cả POSIX/Windows; chương trình
cần `nhập gói/hệ thống/tiến trình;` và gọi API công khai. Bộ adapter
Win32 trong `foreign.cpp` vẫn quản lý handle, pipe, process group/Job Object
và UTF-16 tại biên hệ điều hành. Đường FFI Windows **chưa được xác minh
trên máy Windows**; CI Windows interpreter/JIT, spawn và timeout là gate còn mở.

Đợt quét cập nhật ngày 2026-10-09: 95 tệp `.vi` trong `gói/`, 935 thân hàm V++;
không còn triển khai dở hoặc `native hook không khả dụng` (kể cả văn bản
chuẩn hóa Unicode NFC). Registry khai báo 51 intrinsic, mỗi intrinsic có
opcode và số đối số cố định; parser, semantic, IR, codegen và VM xử lý các
lời gọi trực tiếp qua opcode. Runtime chỉ dùng `resolveLegacyIntrinsic` để
tra descriptor cho bytecode/lời gọi tương thích cũ khi không tìm thấy ID hàm;
cả nhánh legacy lẫn opcode trực tiếp dùng chung `dispatchRegisteredIntrinsic`
và phân loại lỗi catchable/VM. Đường biên dịch hiện tại phát opcode trực tiếp.
V++ giữ validation, formatting, parsing, protocol và policy ở tầng thư viện.

Chính sách bảo toàn hành vi ngày 2026-10-09: chuyển thuật toán sang `.vi`,
kiểm thử public API trên interpreter/JIT và từng OS, rồi mới cho phép gỡ
helper C++ thật sự không còn caller. Windows directory binding mới gồm
`CreateDirectoryW`/`DeleteFileW`/`RemoveDirectoryW` và
`FindFirstFileW`/`FindNextFileW`/`FindClose`; toàn bộ thuật toán tạo nhiều cấp,
duyệt danh sách và xóa cây vẫn ở `gói/nhập xuất/thư mục.vi`. Bật
`VPP_WINDOWS_DIRECTORY_FFI=1` để kiểm thử; mặc định giữ helper Windows cũ
cho tới khi CI Windows chứng minh tương đương. Fixture Windows kiểm tra
đường cũ, đường FFI và JIT. Thay đổi này mới được build/test trên macOS,
chưa có bằng chứng chạy trên Windows.

Đợt dọn dẹp môi trường ngày 2026-10-09: bỏ nhánh C++ Windows
`OP_VM_DOC_BIEN_MOI_TRUONG` và `validateEnvironmentVariableName` trùng lặp.
`đọc biến môi trường()` trên cả POSIX/Windows dùng V++ để kiểm tra tên,
chọn giá trị mặc định và gọi `system.env.read` FFI. Opcode môi trường cũ
được từ chối trên mọi hệ điều hành; negative fixture kiểm tra interpreter/JIT.
Runtime hardening kiểm tra riêng việc từ chối opcode cũ trên cả POSIX/Windows,
đồng thời kiểm tra `getenv` FFI về quyền, ABI, NUL, biến có/không có và
interpreter/JIT ở cả hai nền tảng. Việc thực thi Windows vẫn chờ CI thật.

Để tiếp tục chuyển phần điều phối OS khỏi helper C++, xem
[kế hoạch FFI/System ABI](system-ffi-migration-plan.md). Kế hoạch liệt kê đủ 51 intrinsic,
phân biệt ứng viên OS với substrate VM/compiler/worker, đầu việc theo từng tầng và
điều kiện gỡ helper. Kể từ 2026-10-08, compiler và VM đã có FFI scalar POSIX
qua `OP_FFI_CALL = 156`/libffi; `gói/hệ thống/môi trường.vi` dùng FFI `getenv`
trên POSIX và Win32 adapter trên Windows. `gói/hệ thống/tiến trình.vi`
cung cấp thêm `mã tiến trình()` và `mã tiến trình cha()` qua FFI `getpid`/
`getppid` trên POSIX với hai quyền tách biệt `system.process.id` và
`system.process.parent_id` giới hạn đúng ABI. `gói/hệ thống/nền tảng.vi` chuyển
`ngủ mili giây()` sang FFI `usleep(u32):i32` trên POSIX, yêu cầu quyền
`system.time.sleep`; Windows cũng có Win32 sleep adapter. ABI scalar còn hỗ trợ
`i64/u64` có boxed `AbiInteger` giữ nguyên 64 bit qua VM, so sánh và hiển thị;
`u32` cũng giữ đầy đủ `UINT32_MAX` qua boxed khi vượt `INT32_MAX`.
Đầu vào nguyên lớn hiện phải truyền bằng chuỗi thập phân hoặc boxed từ FFI.
Foreign buffer trong source phải khai báo độ dài `c_đệm_ra[16]` hoặc
`c_đệm_vào[số_byte]`; compiler chuyển extent sang descriptor, verifier đối
chiếu arity và tính hợp lệ của extent trước khi chạy; runtime kiểm tra đúng
độ dài byte, chiều dữ liệu và giới hạn theo từng capability. AST dump hiển thị
extent để rà soát ABI. Các fixture C++ cũ thiếu metadata vẫn qua allowlist
native chặt; đây chưa phải một API pointer FFI tổng quát.
Boxed `i64/u64` hỗ trợ cộng, trừ, nhân, chia nguyên và chia lấy dư với kiểm tra
overflow/zero; phép trộn có số âm signed và unsigned hoặc boxed với `f64`
bị từ chối để không mất chính xác. Kết quả boxed giữ miền dấu: hai toán hạng
signed trả signed; có unsigned và các toán hạng không âm trả unsigned.
Kiểm thử runtime thử biên `INT64_MIN/MAX`/`UINT64_MAX` và so sánh kết quả
interpreter/JIT. Test runtime dùng C ABI fixture độc lập;
loader đóng thư viện mở động kể cả khi ném lỗi. POSIX file đã dùng FFI stdio;
TCP/UDP socket POSIX và tiến trình con dùng FFI adapter với token VM;
TLS client POSIX nay mở TCP bằng FFI, nâng cấp/gửi/nhận qua ba adapter
`system.net.tls.upgrade/send/recv` với token VM và phiên provider native;
strict POSIX chặn opcode nâng cấp TLS cũ. SecureTransport/OpenSSL vẫn đảm
nhiệm handshake, trust store, hostname verification và mã hóa. Runtime hardening
đã thử handshake thất bại thu hồi fd/token, cấm raw send/recv trên token TLS,
quyền/buffer/ABI và token khác VM; chưa có fixture xác thực TLS end-to-end
và CI Linux/Windows. Windows tiếp tục dùng primitive TLS cũ.
Đồng hồ đơn điệu trên POSIX đã dùng `clock_gettime` qua FFI với
`c_đệm_ra` giới hạn vào 16 byte `timespec` và kiểm tra layout; V++ tự đọc
giây/nano và đổi sang mili giây. UTC trên POSIX gọi `CLOCK_REALTIME` qua
quyền riêng `system.time.realtime`; V++ giải mã epoch có dấu, đổi sang lịch
Gregorian và dựng ISO-8601. Giờ địa phương POSIX chuyển sang `localtime_r`
qua `system.time.local`, lấy chung epoch từ clock, V++ giải mã `struct tm`
và offset thực của timezone/DST. Runtime bắt buộc layout LP64 little-endian
và chuyển pointer trả về thành trạng thái thành công. Windows FFI chuyển
`localtime_r` sang `_localtime64_s`, chép sáu trường lịch ra wire buffer;
V++ tính offset từ lịch và epoch của cùng một snapshot. Helper local clock
Windows và opcode cũ đã gỡ; CI Windows vẫn cần xác minh thực tế.
Entropy bảo mật trên POSIX và Windows đọc `getentropy(c_đệm_ra,u64)` qua quyền
`system.entropy.read`, giới hạn 1–256 byte/call. `gói/lõi/entropy.vi`
điều phối các khối cho mật mã và ngẫu nhiên. Windows FFI adapter gọi
`BCryptGenRandom`; opcode entropy legacy đã bị chặn trên cả hai hệ điều hành.
Linux cần libc có symbol `getentropy` và chưa qua CI thực tế.
`gói/nhập xuất/thư mục.vi` hiện tạo/xóa **một mục** trên POSIX bằng FFI
`mkdir(c_chuỗi,u32):i32` (`system.fs.mkdir`, chỉ cho phép mode 0777) và
`remove(c_chuỗi):i32` (`system.fs.remove`), giữ xử lý EEXIST/ENOENT,
kiểm tra UTF-8/NUL và duyệt cây bằng V++. Cả hai capability đều khóa đúng
symbol, library và ABI; `system.ffi.error` có thêm `eexist`/`enoent`.
`gói/nhập xuất/đường dẫn.vi` sử dụng `access(c_chuỗi, 0):i32` qua quyền
`system.fs.exists` để kiểm tra sự tồn tại theo liên kết tượng trưng; ABI và
chế độ `F_OK` được runtime giới hạn chặt. `stat/lstat` qua hai capability
`system.fs.stat/lstat` riêng với đệm 256 byte, `stat_mode_offset()` xác nhận
offset `st_mode` theo SDK để V++ phân loại file/thư mục đúng quy tắc no-follow.
Trên Windows, opt-in `VPP_WINDOWS_DIRECTORY_FFI=1` hoặc
`VPP_WINDOWS_PATH_FFI=1` chuyển bốn phép hỏi metadata (`tồn tại`, `là tệp`,
`là thư mục`, `là thư mục không theo liên kết`) sang wrapper V++ đọc mã loại
qua Win32 `system.fs.path.kind`; VM chỉ giữ handle tạm và chuyển lỗi Win32.
Opcode C++ tương thích vẫn được giữ cho đến khi CI Windows xác nhận parity.
Liệt kê dùng `opendir/readdir/closedir` qua ba capability riêng; `DIR*`
được cấp token sở hữu VM, kiểm tra token khi đọc/đóng, tự dọn khi reset/hủy,
và tên mục được sao chép ngay trước khi gọi `readdir` lần tiếp theo.
`gói/nhập xuất/tệp.vi` chuyển năm API file POSIX và Windows sang `fopen`/`fread`/`fwrite`/
`fclose`/`ferror` qua FFI. V++ đọc/ghi theo khối và đóng tệp trên nhánh lỗi;
`c_tệp` được VM quản lý bằng token riêng, có cleanup khi VM reset/hủy,
`c_đệm_vào`/`c_đệm_ra` chỉ cấp vùng nhớ tạm cho binding đã kiểm chứng.
Trên cả hai nền tảng, năm opcode file legacy đã bị vô hiệu hóa không phụ thuộc biến môi
trường: đường C++ dùng `ifstream`/`ofstream` đã được gỡ khỏi `vm.cpp`;
chương trình phải dùng năm hàm public V++ trong gói tệp. Biến
`VPP_STRICT_SYSTEM_FFI_FILE=1` tiếp tục kiểm tra API public và diagnostic
opcode legacy trong test. `VPP_STRICT_SYSTEM_FFI_POSIX=1` mở rộng gate
chặn opcode native cũ thuộc env/file/fs/clock/sleep/entropy/DNS/TCP/UDP/TLS socket; các fixture public
được chạy với cả interpreter và JIT, gồm localtime/DST dưới timezone cố định.
Đây là gate theo danh sách opcode, chưa phải bộ đếm mọi native helper.
Windows dùng adapter `_wfopen` UTF-16 và `FILE*` thuộc registry từng VM ngay
tại ranh giới FFI; quản lý khối và xử lý lỗi vẫn ở V++. Fixture file chạy
interpreter/JIT trên Windows là gate bắt buộc, chưa có kết quả Windows CI.
Các thuật toán native Foundation (filesystem, environment, entropy, clock,
sleep) và legacy DNS/TCP/UDP/TLS socket cũng được loại khỏi bản build POSIX.
Opcode cũ thuộc những nhóm này bị từ chối kể cả khi không bật strict gate.
Provider SecureTransport/OpenSSL, socket fd/handle do FFI quản lý và các
primitive tag/identity/platform của VM vẫn giữ lại. Windows vẫn chạy nhánh
primitive cũ cho tới khi có binding Win32 phù hợp.
Đường C++ chạy tiến trình legacy đã được gỡ khỏi `vm_native_helpers.cpp`
trên mọi nền tảng. Gọi trực tiếp
`tien_trinh_chay_vm` hiện báo lỗi hướng dẫn dùng
`gói/hệ thống/tiến trình.vi`; API `chạy tiến trình` vẫn do V++ điều phối qua
`system.process.*` FFI, kể cả multiplex output và deadline. Khi mở strict
gate, chặn từ dispatcher xảy ra trước, với diagnostic strict cũ. Windows vẫn
cần adapter CreateProcessW trong `foreign.cpp`; chưa có CI xác minh đường
mới. POSIX FFI adapter trong `foreign.cpp` vẫn chịu trách nhiệm
về `argv**`, descriptor, token VM và vòng đời child.
Audit hiện ghi nhận 56 foreign bindings, 31 foreign callers, 50 capabilities, không có thân
V++ chưa triển khai. Số liệu 51 intrinsic là baseline của opcode legacy;
không đồng nghĩa tất cả vẫn được thư viện chuẩn POSIX gọi.

## Phần chuyển trong đợt này

| API | Trước | Sau |
| --- | --- | --- |
| `lấy thời gian hiện tại`, `lấy thời gian utc` | C++ đọc clock và ghép ISO-8601 bằng `strftime`/`snprintf` | V++ đệm số, ghép ngày giờ, dấu và offset; UTC POSIX/Windows đọc FFI realtime và đổi epoch sang Gregorian; localtime_r POSIX giải mã DST/offset từ `struct tm`, Windows FFI nhận trường CRT và V++ tự tính offset |
| `độ lệch múi giờ` | Handler native riêng | V++ lấy offset từ snapshot clock |
| `thành list` | C++ sao chép vector list/tuple | V++ kiểm tra kiểu, duyệt phần tử, tạo list mới bằng primitive `them` |
| `regex khớp`, `regex tìm` | C++ từng tham gia engine regex | Parser/matcher và policy full-match/search chạy bằng V++ |
| `regex thay`, `regex tách` | C++ từng tham gia thay/tách | V++ tự ghép capture, thay và tách; không còn regex hook C++ |
| `lam_tron_xuong`, `lam_tron_len` | C++ gọi `std::floor` / `std::ceil` | V++ làm tròn số hữu hạn bằng phép toán IEEE-754, giữ nguyên kiểu int32/double của contract cũ |
| `phụ thuộc kiểm tra tệp` | C++ gọi lại compiler rồi dựng `{hợp lệ, lỗi}` | V++ lấy compiler snapshot, giữ chính xác shape kết quả public; chỉ compiler pipeline còn là primitive |
| HTTP server `Transfer-Encoding: chunked` | Từ chối request dù socket đã nhận được dữ liệu | V++ quét incrementally size line/chunk/trailer, giới hạn body và dựng request body trước khi route |
| `tạo thư mục`, `xóa đường dẫn` | C++ dùng `create_directories`/`remove_all` | V++ duyệt cây bằng stack; primitive chỉ tạo/xóa một mục và đọc trạng thái không theo symlink |
| DB client | C++ chọn client, ghép command và diễn giải kết quả | V++ dựng argv/environment, chọn client dự phòng và ánh xạ kết quả; VM chỉ spawn/read/wait process |
| `socket gửi` | C++ lặp gửi hết stream | V++ giữ vòng gửi hết với byte offset và quy tắc một datagram UDP; primitive chỉ gửi một lần |
| `%f`, `%e`, `%g` | VM định dạng bằng `ostringstream` | V++ giải mã bit binary64, dựng thập phân chính xác, làm tròn ties-to-even và chọn bố cục; VM chỉ trả 8 byte biểu diễn số |

Helper HTTP `splitPathAndQuery` không còn nơi gọi cũng được gỡ khỏi runtime,
header và danh sách nguồn CMake; logic giao thức đã nằm trong gói HTTP V++.

Các tên `lay_thoi_gian_hien_tai`, `lay_thoi_gian_utc`, `do_lech_mui_gio`,
`thanh_list` được giữ dưới dạng hàm V++ trong gói tương ứng. Chúng không còn
được runtime dispatch trực tiếp; chương trình dùng các tên này cần nhập gói.
Chuyển list vẫn là sao chép nông: sửa list ngoài không sửa nguồn, nhưng các
phần tử là tham chiếu vẫn chia sẻ cùng đối tượng. Đối số sai kiểu được ném
từ V++, có thể bắt bằng `thử`/`bắt`.

Clock đã tách thành hai intrinsic `dong_ho_dia_phuong_vm()` và
`dong_ho_utc_vm()`, tương ứng `OP_VM_DONG_HO_DIA_PHUONG` và `OP_VM_DONG_HO_UTC`.
UTC POSIX/Windows nay dùng `system.time.realtime` FFI qua `utc.vi`, rồi đổi epoch
và định dạng ISO hoàn toàn bằng V++. Đã gỡ nhánh `gmtime_s` của helper Windows;
`OP_VM_DONG_HO_UTC` bị từ chối nhưng giữ số hiệu bytecode để báo lỗi rõ ràng.
Local POSIX/Windows cùng qua `địa phương.vi`; cả hai opcode giờ cũ bị chặn.
Opcode sleep và monotonic cũ nay bị từ chối trên POSIX và Windows.
`ngủ mili giây()` dùng `usleep` qua FFI (adapter Windows gọi `Sleep`),
`thời gian đơn điệu mili giây()` dùng `clock_gettime` qua FFI (adapter
Windows gọi `QueryPerformanceCounter`). V++ quản lý thời lượng, deadline và
diễn giải kết quả đồng hồ trên cả hai nền tảng.

## Phân loại primitive còn lại

| Nhóm | Lời gọi tiêu biểu trong `gói/` | Ranh giới |
| --- | --- | --- |
| VM và collection | `loai_cua`, `bam_dinh_danh`, `do_dai`, `them`, `xoa_tai`, `lay_map`, `dat_map`, `co_khoa`, `khoa_map`, `xoa_khoa`, `thanh_tuple` | Đọc tag/identity, cấp phát và sửa cấu trúc VM; giữ primitive. Chuyển tuple công khai và mọi phép tìm kiếm, tổng hợp, tập hợp, sao chép list nằm ở V++. |
| Chuỗi và số máy | `chuoi_bytes_vm`, `chuoi_tu_bytes_vm`, `so_thuc_bits_vm`, `so_thuc_tu_bits_vm` | Chỉ chuyển biểu diễn byte của chuỗi và IEEE-754; Unicode scalar, UTF-8, số thực đặc biệt, parser, định dạng và làm tròn ở V++. |
| File, thư mục, môi trường | `io_doc_file_vm`, `io_doc_bytes_vm`, `io_ghi_file_vm`, `duong_dan_ton_tai_vm`, `tao_thu_muc_vm`, `doc_bien_moi_truong_vm`, `ten_nen_tang_vm` | File và env POSIX/Windows dùng FFI; file legacy bị từ chối cả hai OS. POSIX filesystem dùng FFI; Windows filesystem vẫn có nhánh native dự phòng. |
| Clock, thread | `dong_ho_dia_phuong_vm`, `dong_ho_utc_vm`, `thoi_gian_don_dieu_ms_vm`, `ngu_mili_giay_vm`, `thread_vm_spawn/wait/cancel/status/park` | Local, UTC, monotonic và sleep đều dùng V++/FFI cả POSIX/Windows, đã gỡ các handler C++ tương ứng; worker vẫn ở VM. |
| Socket, TLS, DNS | `socket_*_vm`, `dns_phan_giai_vm` | POSIX TCP/UDP, resolver và TLS client đã dùng FFI có token do VM sở hữu; handshake và crypto TLS qua adapter native SecureTransport/OpenSSL. Opcode cũ còn cho Windows/legacy; HTTP, URL, header/body, DNS dedup/sort và protocol ở V++. |
| Entropy | `getentropy` qua System FFI trên POSIX/Windows; opcode `ngau_nhien_bao_mat_bytes_vm` bị chặn | V++ chia/ghép entropy và kiểm tra trạng thái; Windows FFI adapter chỉ giữ BCryptGenRandom tại biên OS. |
| Process và Database | `system.process.*` trên POSIX và Windows | V++ dựng argv/env, multiplex stdout/stderr, ghép output, xử lý lỗi và wait. Helper C++ process cũ đã gỡ trên cả hai OS; adapter FFI quản lý token VM, `posix_spawnp` hoặc `CreateProcessW`, pipe và handle. Windows chưa đạt gate CI. DB chọn client, dựng argv/env và xử lý kết quả ở V++. |
| Compiler/VM | `bien_dich_phan_tich_vm`, `kich_ban_chay_vm` | Boundary compiler/VM tối thiểu; compiler tạo snapshot hoặc chạy mã; API kiểm tra phụ thuộc diễn giải snapshot ở V++. |
| Regex | không có hook C++ | Parser, matcher, capture, replace và split đều chạy bằng V++. |

Regex hiện không còn phụ thuộc engine C++: parser, matcher, capture, full-match,
search, replace và split nằm trong `gói/lõi/biểu thức chính quy.vi`. Khi cần
Unicode scalar, thư viện tự giải mã UTF-8 từ danh sách byte bằng V++.
Các opcode 110/115/119 (Unicode và số thực đặc biệt) cùng 131–134 (task cũ)
chỉ giữ vị trí enum để ổn định mã opcode; verifier từ chối chúng và compiler
không còn cho gọi các tên intrinsic đó. Các hàm công khai thay thế nằm trong
thư viện V++.

## Kiểm chứng

- Ngày 2026-10-09: `vpp-runtime-p0-hardening` kiểm tra trực tiếp
  **29 opcode System POSIX cũ và 1 opcode môi trường chung hai OS** trong interpreter/JIT: cả hai đường phải
  từ chối cùng chẩn đoán, không đi vào helper C++ đã gỡ. Ca DNS gọi qua
  `OP_GOI` cũ cũng kiểm tra parity; ca JIT `thử/bắt` chuyển sang primitive
  biểu diễn byte vẫn được hỗ trợ để tách kiểm chứng ngoại lệ ngôn ngữ khỏi
  việc cấm API System cũ. Build và CTest hardening riêng đạt PASS.
- Ngày 2026-10-09: audit schema 3 ghi **95 tệp, 935 thân hàm,
  0 unfinished, 56 foreign bindings, 31 callers, 50 capabilities**. Runtime
  hardening bổ sung biên phép toán signed/unsigned 64 bit, overflow, chia 0,
  mixed-sign và đường VM interpreter/JIT. `run_tests.sh` thêm fixture tiến trình
  và localtime/DST vào strict POSIX gate. POSIX TCP/UDP socket, DNS và
  process spawn và TLS client POSIX đã đi qua FFI adapter; TLS provider crypto,
  process/TLS Windows và worker còn native.
  Strict gate cũng chặn opcode socket/process cũ và
  `kiem_tra_ffi_socket.vi` kiểm thử TCP/UDP localhost trên interpreter/JIT
  khi môi trường cho phép bind; HTTP fixture server chạy dưới strict gate để
  bao phủ `listen/accept/recv/send`.
- Windows quoting migration 2026-10-09: `gói/hệ thống/tiến trình.vi` dựng
  và escape dòng lệnh CreateProcessW từ argv trong V++; FFI `command_line`
  nhận chuỗi đã dựng, còn C++ giữ Win32 handle, Unicode conversion và ABI.
  Fixture `kiem_tra_process_quote_windows.vi` kiểm tra case quote/backslash,
  khoảng trắng và Unicode trên cả interpreter/JIT. Windows CI cần kiểm chứng
  end-to-end trước khi xác nhận parity trên Win32.
- POSIX process migration 2026-10-09: `gói/hệ thống/tiến trình.vi` sử dụng
  chín binding `system.process.new/arg/env/start/poll/read/wait/error/close`.
  V++ giữ public result map và drain cả hai pipe qua `poll`/`read`, hỗ trợ
  dữ liệu nhị phân, argv literal, child-only environment và exit code != 0.
  `kiem_tra_ffi_process_spawn.vi` đã khớp **30/30** kỳ vọng trên đường strict POSIX,
  strict interpreter và strict JIT tại macOS; strict từ chối
  `tien_trinh_chay_vm` ở cả hai mode. `vpp-runtime-p0-hardening` bổ sung
  kiểm tra capability/descriptor/token VM, argv/env không hợp lệ, double
  start/close, drain/wait, spawn failure và cleanup token; PASS. C++ adapter
  vẫn gọi `posix_spawnp`/`poll`/`read`/`waitpid` để làm cầu nối cấu trúc POSIX,
  nên đây là migration điều phối tiến trình sang V++, chưa phải generic ABI
  cho mọi API spawn; Windows và TLS chưa đạt gate M7.
  Bổ sung tham số deadline tùy chọn dựa trên clock đơn điệu FFI, trả output
  đã thu thập và hủy/reap child khi quá hạn. POSIX `posix_spawnattr`
  đặt `POSIX_SPAWN_SETPGROUP` để tạo nhóm tiến trình con riêng: close/reset
  gửi SIGKILL cho **cả nhóm** rồi `waitpid` leader, tránh bỏ lại tiến trình
  cháu còn giữ pipe. Runtime hardening kiểm tra group ID độc lập với VM và
  việc reap leader; fixture thêm ca shell sinh child nền rồi timeout.
  Ca output một phần, descendant timeout và SIGTERM (status 143) nâng fixture
  lên **30/30** assertions. Full CTest sau thay đổi process group đạt
  **4/4 PASS** (291,92 giây). Sau đó wrapper giới hạn thời gian chỉnh
  `poll` chỉ chờ số mili giây còn lại, làm tròn lên bằng hàm V++
  `lam_tron_len`. Fixture strict POSIX interpreter/JIT chạy lại:
  **30/30 PASS** mỗi chế độ; audit hiện tại **95 tệp/935 thân hàm/0 unfinished**.
- FFI PID cha ngày 2026-10-08: `kiem_tra_ffi_tien_trinh.vi` kiểm tra PID hiện
  tại và PID cha thực tế qua VM; runtime hardening thử quyền thiếu, quyền PID
  khác, tráo symbol/library/chữ ký ABI và JIT fallback. CTest macOS đầy đủ
  sau thay đổi **4/4 PASS** (integration khoảng 201 giây).
- FFI System ngày 2026-10-08: build `vpp-cli`, runtime/RC hardening thành công;
  CTest macOS đầy đủ **4/4 PASS** (bao gồm integration). Ca
  `kiem_tra_ffi_ngu.vi` kiểm tra thời lượng 0, 2, 1001 ms, số âm và đối số
  không chuyển được; runtime hardening khóa riêng `system.time.sleep` vào
  `usleep(u32):i32` và kiểm tra interpreter/JIT fallback. Chưa có CI Windows
  cho nhánh native dự phòng hoặc kiểm thử sleep bị signal ngắt.
- Ngày 2026-10-08: build macOS thành công; chạy
  `VPP_EXEC=build/bin/vpp ./run_tests.sh` đạt **145 PASS, 0 FAIL** (ca bind
  localhost bị bỏ qua do môi trường hạn chế socket). CTest riêng
  `vpp-rc-internal-hardening` đạt **1/1 PASS**, gồm kiểm tra mỗi opcode VM
  hiện hành có descriptor, opcode đã nghỉ không còn được verifier chấp nhận,
  và parser → semantic → IR → codegen phát đúng opcode/arity. CTest tích hợp
  macOS đạt **4/4 PASS**, gồm `vpp-runtime-p0-hardening` kiểm tra bytecode
  legacy và opcode hiện hành cùng gọi dispatcher, bảo toàn lỗi V++ bắt được và
  lỗi IO runtime. Các helper C++ `startsWith`, `trimCopy` và
  `decodeSimpleEscapes` không còn nơi gọi đã được gỡ khỏi runtime/header.
- Đợt chuyển formatter/DB/socket trước đó: build macOS, các CTest hardening và
  regression qua; 515
  trường hợp `%f/%e/%E/%g/%G` đối chiếu đúng với kết quả thập phân tham chiếu,
  bao gồm làm tròn ties-to-even, âm zero, số dưới chuẩn và biên binary64.
- TCP/UDP localhost gửi đúng bytes Unicode; ép `send` chỉ nhận tối đa 997 byte
  mỗi lần vẫn truyền đủ 589824 byte. Wrapper V++ xử lý cả offset nằm giữa các
  byte của một ký tự UTF-8.
- CRUD SQLite dùng client thật; bảy lần gọi MySQL/PostgreSQL dùng client giả lập
  kiểm tra argv, biến mật khẩu và SQL bootstrap do V++ dựng. `chạy tiến trình`
  giữ argv literal và môi trường con không làm đổi môi trường cha.
- `kiem_tra_goi_thoi_gian.vi`: snapshot cố định cho zero padding, ngày nhuận,
  UTC, offset 0/dương/âm/lẻ phút, giây nhuận; hình dạng snapshot OS và alias.
- `kiem_tra_goi_bo_suu_tap.vi`: list/tuple rỗng, bản sao độc lập, phần tử tham
  chiếu chia sẻ, alias và lỗi sai kiểu.
- `kiem_tra_goi_bang_ma.vi`: kiểm tra UTF-8 quá dài, surrogate, giới hạn
  U+10FFFF, sequence thiếu/không hợp lệ và round-trip raw bytes kể cả NUL.
- `kiem_tra_vm_ieee754.vi`: đối chiếu bits của ±0, subnormal, max finite,
  ±infinity, NaN; kiểm tra `thành số thực` giữ âm zero và từ chối byte lỗi.
- `kiem_tra_goi_regex.vi`: capture 1/2 chữ số, escape, nhóm không khớp, mẫu rỗng,
  anchor, lookahead, delimiter đầu/cuối, chuỗi rỗng và Unicode. Đối chiếu thêm
  800 trường hợp thay/tách với kết quả native trước khi chuyển: trùng từng byte.
- `floor`/`ceil`: đối chiếu 634 đầu vào gồm biên int32, số âm/dương, số rất nhỏ,
  số trên 2^52 và double lớn; 1268 dòng giá trị + kiểu trả về trùng từng byte với
handler native cũ.

Package-manager policy cũng đã bắt đầu rời CLI C++: module `vpp.packages`
đọc/ghi `vpp.json`, parse/so sánh SemVer 2.0, parse range `*`/exact/`^`/`~`/
comparator chain, kiểm tra version conflict, deduplicate dependency và phát hiện
cycle theo thứ tự dependency-first. Các thao tác này dùng JSON/file/collection
API V++. Git, registry, cache, fingerprint và materialization vẫn ở toolchain vì
cần filesystem/process boundary. Production CLI hiện materialize source snapshot
ở C++, chạy policy graph/version/cycle bằng `vpp.packages` trong VM rồi ghép thứ
tự/range đã resolve trở lại source metadata. `package_solver.cpp` vì vậy không còn
giữ SemVer/cycle policy.
- Lỗi chuyển kiểu vẫn là `ném` bắt được trong V++; nếu đi tới biên chương trình
  mà không có `bắt lỗi`, VM khôi phục diagnostic có dữ liệu đầu vào và callsite.
- Chạy bộ kiểm thử với binary mới: `VPP_EXEC=build/bin/vpp ./run_tests.sh`;
  `run_tests.sh` mặc định ưu tiên `bin/vpp-cli` nên có thể chọn binary cũ nếu
  chưa cập nhật bản cài đặt. Kiểm thử socket cần môi trường cho phép bind
  localhost; khi bị giới hạn, script ghi rõ các ca socket đã bỏ qua.
