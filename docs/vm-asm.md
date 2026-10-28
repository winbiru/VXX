# VM và runtime V++ gọi ASM trực tiếp

## Trạng thái sau bootstrap đầy đủ và DNS V++ (28/10/2026)

**Stage 1 → Stage 2 → Stage 3 đã khớp từng byte trên VM/ASM macOS ARM64**,
SHA-256 `8235556272902985abff7be472bbfea7fceab8f4772d8938a944fa0987ff6923`.
Stage 3 đã sinh và chạy một chương trình riêng. Bộ ghi ảnh dùng khối
khoảng 16 KiB để tránh giữ toàn bộ list byte đầu ra bên cạnh IR.
Chi tiết thời gian và artifact ở `docs/compiler-self-hosting-status.md`.

Opcode resolver 203–205 hiện dùng **DNS/hosts bằng V++** cho hostname:
VM nạp `dns-provider.vmb` bên cạnh executable, hoặc đường dẫn được cấu hình
qua `VPP_VM_DNS_PROVIDER`. Provider chạy bytecode V++ trên chính interpreter
V++/ASM, không chạy compiler ngoài, không gọi VM C++ hoặc `getaddrinfo`.
Nó tái sử dụng thư viện DNS UDP/TCP, parser gói tin và cấu hình V++ hiện có.
Không có cấu hình riêng thì đọc `/etc/hosts` và `/etc/resolv.conf` bằng V++.
Các biến `VPP_DNS_VI_*` tiếp tục được hỗ trợ; chưa tích hợp nguồn cấu hình
DNS riêng của macOS như split DNS qua SystemConfiguration/mDNS.

Provider và chương trình chính có state/stack riêng nhưng dùng chung arena.
Header slot 2 giữ root chương trình, slot 4 giữ provider, slot 10 giữ đường
executable; GC đánh dấu cả ba. Địa chỉ trả về được giữ trong bảng resolver
của caller. Lỗi giao vận OS bên trong provider được chuyển thành ngoại lệ
guest để thư viện DNS thực hiện retry/failover; lỗi đối số và OOM vẫn fatal.
Đường thoát đóng tài nguyên của cả hai state.

Build target `vpp-vm-asm` tạo cả executable và ảnh provider. Khi phân phối,
giữ hai tệp cùng thư mục. Có i32 seed và compiler đầy đủ sẵn thì truyền
`VPP_NATIVE_I32_SEED` và biến môi trường `VPP_NATIVE_COMPILER` để không
bootstrap lại compiler đầy đủ qua Stage 0 khi tạo provider.

Kiểm chứng mạng riêng với loopback được cho phép đã PASS DNS A/AAAA,
TCP fallback với frame chia nhỏ, NXDOMAIN, sai ID/câu hỏi/pointer nén.
Hosts và root GC được kiểm tra cả trong sandbox không cho phép bind.
Socket close trả đúng **0** như Machine ABI Stage 0; wrapper V++ trả
boolean thành công. Trả 1 trước đây làm wrapper DNS thử đóng socket lần hai.

macOS CTest **4/4 PASS, 267,45 giây**; Linux x86-64 CTest **5/5 PASS,
381,00 giây** trong container linux/amd64 trên Docker engine ARM64.
Linux chạy thật các syscall process/socket/file/clock/entropy và audit
AOT bằng `nm`/`readelf`; CPU x86-64 được mô phỏng. Harness process/socket
Linux ARM64 PASS riêng. Các kết quả trước ghi SKIP/cross-assembly bên dưới
là lịch sử, không thay thế kết quả mới hoặc kiểm chứng Linux ARM64 đầy đủ.

Mật mã bản ghi TLS và HKDF nay có triển khai V++, xem `docs/tls-vpp.md`.
**TLS có xác thực CA/hostname/SNI chưa hoàn tất**; 206–208 vẫn fail-closed.
C++ Stage 0/fallback và thư viện OS vẫn được giữ; không suy ra toàn bộ AOT
tổng quát đã loại bỏ mọi mã C/C++ từ các fixture này.

## Mốc trước: process, credential u32, numeric resolver và native AOT (28/10/2026)

Lượt CTest `build-ci-regression-fix`, lọc `^vpp-(vm-asm|native-vpp-runtime)$`,
đạt **4/4 PASS** trong 83,48 giây (tính cả dependency bootstrap Stage 1
và native CLI). Process harness ARM64/x86-64 trên macOS PASS với spawn,
đồng thời stdout/stderr, môi trường riêng, wait, signal và cleanup.
Guest VM từ chối executable/argv có UTF-8 lỗi, executable chứa NUL và
environment key chứa `=` trước khi gọi POSIX. UID/EUID/GID/EGID được
giữ dưới dạng unsigned u32; tag 11 biểu diễn các bit từ 0x80000000 đến
0xFFFFFFFF và có so sánh/thể hiện thập phân; số học u32 tổng quát chưa có.

Socket VM đã hỗ trợ TCP/UDP, IPv4/IPv6 và timeout; resource token chỉ
được đánh dấu hoạt động khi đăng ký thành công để tránh double-close
trong nhánh lỗi cấp phát. Fixture socket guest lỗi đối số/token và
TLS 206–208 fail-closed PASS. Hai socket harness cần bind địa chỉ
loopback đều **SKIP do sandbox** ở lượt này; những PASS socket runtime
trước đó là bằng chứng lịch sử riêng. Linux chỉ mới cross-assemble.

Fixture native AOT qua `VPP_NATIVE_VPP_RUNTIME` PASS với kiểm toán toàn
bộ global symbol (`nm -g`) và thư viện liên kết trên Darwin (`otool -L`),
không tìm thấy các dấu hiệu C++/Stage 0 bị cấm trong executable đã thử.
OS libc/libSystem, assembler và linker vẫn là phụ thuộc thực tế; các
đường fallback C/C++ tiếp tục tồn tại. Resolver 203–205 hiện có provider
V++ cho **địa chỉ IPv4/IPv6 dạng số**; phân giải hostname/DNS chưa được
nối vào VM. Verified TLS 206–208 chưa có provider nên TLS từ chối an toàn
thay vì gửi dữ liệu TCP không mã hóa. Kiểm toán Linux AOT hiện bắt buộc
có `readelf`/`llvm-readelf` nhưng chưa chạy trên Linux thực tế.
Chưa có bằng chứng Stage 2→Stage 3
đầy đủ thông qua VM/ASM; xem `docs/compiler-self-hosting-status.md`.

`gói/máy ảo/chính.vi` là một executable độc lập do compiler native i32 V++
biên dịch. Heap, mark/sweep GC, scalar/collection, closure/cell, khung gọi,
class/instance, visibility, switch và try/catch nằm trong các tệp `.vi`
cùng thư mục. Host ABI đọc/ghi tệp, thư mục, PID/PPID, đường dẫn, môi trường, đồng hồ, ngủ,
entropy và số thực gọi
primitive ASM, không đi qua `StackValue`, callback C++, VM C++ hay libffi.

```sh
cmake --build build --target vpp-vm-asm
cmake --build build --target vpp-native-cli
build/native-cli/vpp-native vm-build source.vi -o program.vmb
build/native-vm/vpp-vm program.vmb
```

Có seed i32 V++ sẵn thì cấu hình `-DVPP_NATIVE_I32_SEED=/absolute/path/to/vpp-native`
để dùng seed đó dựng compiler và VM kế tiếp mà không chạy Stage 0.

Stage 0 tạo compiler i32 ban đầu khi chưa cung cấp seed. Compiler i32 V++ sau đó biên dịch VM,
runtime và host policy thành mã máy, liên kết cùng primitive ASM hiện có.
Đường này hiện nhắm macOS ARM64 và Linux x86-64. Thư viện hệ thống vẫn
cung cấp `mmap`, I/O và chuyển đổi f64 thông qua lệnh gọi ASM trực tiếp.

Backend `gói/biên dịch/máy ảo ASM.vi` xuất ảnh nhị phân có phiên bản,
không đổi các số opcode hiện hành. StringPool giữ byte UTF-8/NUL; các lệnh
giữ ABI bốn i32 little-endian. Bộ nạp kiểm tra header, kích thước, ID hàm,
tham chiếu StringPool và đích nhánh trước/khi sử dụng. Ảnh có byte dư hoặc
thiếu bị từ chối. GC không di chuyển đối tượng; offset arena không phải
con trỏ OS. Heap có giới hạn khoảng 56 MiB, tách khỏi vùng byte/file runtime.

Compiler đầy đủ cũng có entry dùng được trong VM này:

```sh
build/native-cli/vpp-native vm-build 'gói/biên dịch/dòng lệnh VM.vi' -o compiler.vmb
VPP_HOME="$PWD" build/native-vm/vpp-vm compiler.vmb build source.vi -o program.vmb
```

Đây là đường tự chủ qua ảnh VM. Trên POSIX, VM độc lập đã thực thi
spawn/pipe/wait (193–202), UID/GID dạng u32 và socket TCP/UDP (185–192)
với policy V++ và primitive ASM. Resolver hostname (203–205), TLS xác
thực (206–208), đồng hồ địa phương/TZ và descriptor FFI chưa triển khai
đầy đủ ở đường này: VM trả lỗi rõ ràng, không chuyển ngầm sang C++.
Không được coi socket TCP thô là kết nối TLS. FFI trong ảnh bị backend
từ chối trước khi ghi đầu ra. Backend AOT có runtime V++/ASM cho những
target được hỗ trợ, song vẫn còn linker/Stage 0 và runtime C fallback.

Kiểm chứng tự động: CTest `vpp-vm-asm` biên dịch VM bằng compiler i32 V++,
chạy ảnh với `PATH` rỗng, đối chiếu ngữ nghĩa runtime, lỗi ảnh hỏng và GC
thu hồi chu trình; kiểm tra symbol để phát hiện phụ thuộc runtime/compiler
C++. Không suy ra tự chủ từ phần mở rộng `.vi` hay chỉ từ output chương trình.

Runtime quản lý bảng ánh xạ theo kiểu khóa: khóa chỉ gồm chuỗi UTF-8 hoặc
số nguyên i32 được sắp thứ tự và tra cứu nhị phân; ánh xạ có khóa hỗn hợp
tiếp tục dùng tìm kiếm tuần tự. Hàm băm định danh chỉ chấp nhận các đối tượng
tham chiếu VM, tương ứng với quy tắc của Stage 0. Regression kiểm tra thứ tự
tra cứu khóa, định danh đối tượng và lỗi khi truyền số nguyên vào hàm băm.

Kiểm thử `vpp-vm-asm-selfhost` là mức xác nhận cao hơn: compiler `.vmb`
phải chạy trên VM mới để tạo lại ảnh compiler với kết quả byte ổn định ở
Stage 2/3. Tại lượt kiểm chứng ngày 27/10/2026, bộ kiểm thử VM/runtime
độc lập đã PASS nhưng bài selfhost chưa hoàn thành: bước tạo compiler
Stage 1 bằng `vpp-native vm-build` hết thời gian sau 180 giây; thử chạy
ảnh Stage 1 đã có trên VM mới cũng vượt ngưỡng quan sát 75 giây. Vì vậy
không coi bootstrap Stage 2/3 là đã được xác nhận trên lượt kiểm thử này.

## Quản lý bộ nhớ và cache chuỗi (27/10/2026)

Heap giữ các offset i32 thay cho con trỏ OS. Mark/sweep duyệt từng
đối tượng reachable qua mark stack liên kết trong header, sau đó quét
heap và gộp khối trống liên tiếp. Allocator sử dụng **10 bucket free-list**
theo tổng kích thước block (gồm header): <=32, <=48, <=64, <=96,
<=128, <=192, <=256, <=512, <=1024 byte và lớn hơn 1024 byte.
Các đầu danh sách nằm ở VM header slot 19..28. Bộ đếm
`free-list-probes` đo số block đã xét, không đếm số bucket trống.

Giá trị số nguyên bất biến -16..255 được intern trong một bảng tại
header slot 17; kết quả `source[i]` ASCII được intern theo byte trong
bảng tại slot 18. `vm_collect()` đánh dấu hai bảng này làm root để
không tạo tham chiếu treo. Do chuỗi có ngữ nghĩa so sánh theo nội dung,
cache ASCII không ảnh hưởng phép so sánh chuỗi; hàm băm định danh vẫn
chỉ áp dụng cho các đối tượng tham chiếu.

Các lời gọi host tĩnh `OP_GOI` với `b < 0` mã hóa StringPool index
theo công thức `-b - 1`. Runtime có fast path cho primitive độ dài
(147, một đối số) và thêm cuối danh sách (148, hai đối số), bỏ qua việc
tạo vector đối số tạm. Lời gọi còn lại dùng đường host chung. Bảng
phân giải tên host đặt tại **state slot 10** (slot 9 đã dùng cho errno),
được cấp phát dạng dữ liệu nguyên không quét tham chiếu GC; 0 nghĩa
chưa phân giải, 1 là tên chưa biết, `primitive_id + 2` là kết quả.
Tất cả giá trị trong bảng được gắn với chỉ số StringPool bất biến.

Đối tượng chuỗi dùng trường thứ tư (word 3) cho cache Unicode:
`-1` là chưa xác định; `-(codepoint_count + 2)` là độ dài đã tính;
giá trị dương trỏ tới bảng offset đã quản lý bằng GC. Với chuỗi từ
128 **byte** trở lên, VM tạo checkpoint mỗi 64 **codepoint** khi cần
`source[i]`, sau đó đọc tối đa 63 codepoint từ checkpoint.
Bảng offset là đối tượng `scan=0` (chỉ chứa số byte offset), được
đánh dấu reachable thông qua đối tượng chuỗi (`scan=1`). Tạo bảng lần
đầu vẫn quét toàn chuỗi; đánh đổi bộ nhớ lấy tốc độ truy cập lặp lại.

## Đo hiệu năng và giới hạn self-hosting

Chạy test runtime đầy đủ và profiler Stage 2:

```sh
cmake -DVPP_STAGE1="$PWD/build/nativedriver/vpp-native" \
  -DVPP_NATIVE_COMPILER="$PWD/build/native-cli/vpp-native" \
  -DVPP_ROOT="$PWD" -DVPP_OUTPUT_DIR="$PWD/build/vm-asm-optimized" \
  -P scripts/quality/check_vm_asm.cmake
python3 scripts/quality/profile_vm_asm_stage2.py --seconds 45 --name local
```

`VPP_VM_PROFILE=1` ghi stderr sau mỗi 250.000 guest instruction,
gồm vị trí function/PC, số allocation, GC và số lượt dò free-list.
`profile_vm_asm_stage2.py` lưu stderr `.log` và số liệu `.json`
trong thư mục build. Timeout của phép đo giới hạn thời gian là dự
kiến và chỉ có nghĩa **Stage 2 chưa hoàn thành trong thời gian đó**.
Bản có fast path và cache tên host quan sát 44,75 triệu guest instruction
trong 45 giây (so với 44 triệu khi chỉ có bucket allocator); mức tăng
nhỏ và mới đo một lần, chưa đủ chứng minh cải thiện hiệu năng ổn định.
Bảng số liệu và giới hạn của phép đo ở `docs/compiler-self-hosting-status.md`.

Stage 2/3 trên VM/ASM phải được kiểm chứng riêng bằng
`scripts/quality/check_vm_asm_selfhost.cmake`: compiler Stage 1
biên dịch chính compiler thành Stage 2; Stage 2 sinh Stage 3 và đối
chiếu byte. Compiler native AOT đã có bootstrap riêng; kết quả đó
không thay thế kiểm chứng compiler chạy qua VM/ASM. Các host ABI
socket/TLS/process/time-local và descriptor FFI cùng runtime
AOT tổng quát vẫn là hạng mục cần chuyển tiếp.

## Thư mục và PID/PPID bằng V++/ASM (28/10/2026)

`gói/máy ảo/thư mục máy.vi` thực hiện vòng đời tài nguyên thư mục,
kiểm tra đường dẫn UTF-8, phân loại lỗi và giải mã bản ghi thư mục.
`scripts/ASM/vpp_vm_directory_primitives.inc` giữ ranh giới OS và con trỏ
64 bit. Các opcode có sẵn nay hoạt động trên VM độc lập:

| Opcode | Kết quả |
| --- | --- |
| 167, 168 | PID, PPID dạng i32 |
| 179 | `mkdir` → `[rc, loại lỗi, errno]`, quyền `0777` áp dụng umask |
| 180 | Xóa file/thư mục/symlink → `[rc, loại lỗi, errno]` |
| 181 | Mở thư mục → `[token, errno]` |
| 182 | Đọc → `[1, tên]`, `[0, ""]` khi EOF, `[-errno, ""]` khi lỗi OS |
| 183 | Đóng → `[rc, errno]` |

Đường `mkdir` cũ của VM trả scalar sai ABI và dùng quyền `0700` của
helper tạo thư mục cache. API thư mục mới giữ đúng giao thức thư viện
chuẩn; helper cache của compiler tiếp tục giữ quyền riêng `0700`.
Đường dẫn dài vượt bộ đệm được trả lỗi OS `ENAMETOOLONG`; NUL/UTF-8
không hợp lệ vẫn là lỗi đối số.

Linux gọi `openat`, `getdents64`, `close`, `mkdirat`, `unlinkat`,
`getpid`, `getppid` bằng syscall. V++ duyệt bản ghi `getdents64`, kiểm
tra độ dài và NUL trước khi đọc tên. Darwin gọi `opendir`, `readdir`,
`closedir` qua ASM/libSystem; ASM chỉ sao chép bản ghi OS có giới hạn,
còn framing và UTF-8 được kiểm tra bằng V++. Xóa symlink không duyệt
vào target; thư viện chuẩn vẫn quyết định thứ tự xóa cây.

State slot 11 giữ bảng tài nguyên sống qua GC. Con trỏ `DIR*`/descriptor
OS nằm trong control block không quét tham chiếu; guest chỉ nhận token.
Bộ đếm token tại VM header slot 29 dùng tag thấp 0 theo modulo 128,
tách khỏi token file có tag 64..127. Slot trống được dùng lại, token
không tái sử dụng; lookup kiểm tra owner và close thu hồi token trước
khi gọi OS. VM đóng mọi thư mục còn mở ở cả đường thành công và lỗi.

ABI riêng của compiler i32 thêm ID 71 `máy tiến trình định danh thô`
và ID 72 `máy thư mục thô`, tổng cộng 73 mục. Object Mach-O/ELF và
giới hạn CALL được cập nhật cùng nhau; opcode và định dạng `.vmb` giữ nguyên.

`check_vm_asm_directory.cmake` được chạy trong CTest `vpp-vm-asm`:
API chuẩn, cây sâu, tên Unicode, symlink có dấu `/` cuối, EOF lặp,
GC, đóng/mở 100 lần, lỗi OS và 10 ca đối số/token sai được đối chiếu
Stage 0. Harness native kiểm tra bản ghi Linux/Darwin giả lập bị cắt,
thiếu NUL/UTF-8 sai, nhiều bản ghi, owner khác VM, cleanup khi lỗi và
biên offset. Khi có Python 3, kiểm thử kiểm tra mode theo umask và
so PID/PPID với `Popen.pid`/PID của tiến trình cha. VM chạy với PATH rỗng.

Release macOS ARM64: **10/10 CTest liên quan PASS**; sau bổ sung ca đường
dẫn dài, `vpp-vm-asm` tiếp tục PASS. Log ở
`build-ci-regression-fix/directory-validation.log` và `directory-final-vm.log`.
ASM lắp ráp được cho macOS/Linux ARM64/x86-64; decoder Linux và phát ELF
đã kiểm tra trên host ARM64. Thực thi syscall Linux còn cần runner Linux.

Tại thời điểm kiểm thử thư mục, UID/GID và spawn/pipe/wait chưa được bổ sung;
các thay đổi tiếp theo nằm ở mục Process/credential bên dưới. Socket/TLS,
timezone và runtime AOT tổng quát vẫn cần các lượt migration riêng;
kết quả này không xác nhận compiler Stage 2/3 hội tụ.

## Credential u32 và POSIX process trên VM V++/ASM (28/10/2026)

`máy tiến trình định danh thô` (native ID 71) lấy PID/PPID và bốn credential
qua biên ASM. UID, EUID, GID và EGID được biểu diễn bằng tag `11` của VM:
**unsigned 32 bit**, kể cả giá trị > `INT32_MAX`; PID/PPID giữ i32.
Trường hợp này đã có fixture directory/system trên macOS ARM64. Fixture
native tổng hợp kiểm tra riêng `INT32_MAX`, `0x80000000`, `0xc0000000`,
`0xfffffffe` và `0xffffffff`: so sánh thứ tự hai chiều, `>=`/`<=`,
khác biệt giữa `u32(0xffffffff)` và `i32(-1)`, cùng định dạng thập phân.
So sánh `u32` với kiểu không phải số trả lỗi kiểu; phép toán số học `u32`
chưa được hỗ trợ. Chưa có credential > `INT32_MAX` từ kernel thật hoặc
kiểm tra thực thi trên Linux.

`gói/máy ảo/tiến trình máy.vi` điều phối các opcode **193–202**: token sở hữu
bởi VM, argv UTF-8, môi trường con, hai kênh output, poll, read, wait và
cleanup. Primitive **ID 73** `máy tiến trình thô` gọi
`vpp_native_os_process(operation, control_offset)` trong
`scripts/ASM/vpp_vm_process_primitives.inc`. Control block 1024 byte thuộc
arena; ASM kiểm tra offset rồi đổi thành con trỏ 64-bit, dùng `posix_spawnp`,
`pipe`, `fcntl`, `poll`, `read`, `waitpid`, `kill` và file actions POSIX.
Nhóm tiến trình con dùng `POSIX_SPAWN_SETPGROUP` để cleanup khi timeout
không để lại con đang nắm pipe. `close` huỷ token trước khi giải phóng FD;
VM tự đóng các process còn sống cả ở đường lỗi và đường thoát bình thường.

Môi trường con được xây bằng V++ dưới dạng `KEY=VALUE`, được ASM hợp nhất
với `environ` của cha: giữ biến kế thừa không bị ghi đè, bỏ bản kế thừa
có cùng tên rồi thêm override. Cha không bị sửa `environ`. Giới hạn ABI:
**128** argv, **128** biến ghi đè, tối đa **4095** byte/chuỗi và **4095**
biến kế thừa để tránh tràn vùng scratch `envp` có cấp phát giới hạn.
Không thay đổi số opcode công khai hoặc ảnh `.vmb`.

`check_vm_asm_process.cmake` và `vm_asm_process.vi` kiểm tra executable
thất bại, argv có khoảng trắng/Unicode, stdout/stderr đồng thời, mã thoát
khác 0, môi trường override và `close` khi child còn chạy. Ca stress tạo
**16.800 byte trên mỗi pipe** và ca timeout 25 ms, kiểm tra thoát/reap
không bị treo. Chạy trên macOS ARM64 với `PATH` rỗng. Lượt kiểm thử sau
khi hợp nhất môi trường và thêm process: `vpp-vm-asm` cùng hai dependency
bootstrap **3/3 PASS**; ca stress/timeout cũng PASS khi chạy độc lập.

Harness process ASM dùng chung hiện kiểm tra native ARM64 và
x86-64/Rosetta trên macOS: `spawn`, argv Unicode, môi trường con,
stdout/stderr đồng thời, exit/signal, `poll` bị `SIGALRM`/`EINTR`,
`wait`, 40 vòng không rò FD và cleanup child. Trên Linux x86-64/ARM64,
ASM process được cross-assemble và harness sẵn sàng chạy trên host phù hợp;
chưa có bằng chứng thực thi Linux. Lỗi mở process trên đường VM
được biểu diễn `errno=<số>`, chưa có chuỗi `strerror` như Stage 0. Trường
hợp cleanup gặp tín hiệu bất thường và token của hai VM còn cần fixture
so sánh riêng trước khi tuyên bố đầy đủ Stage 0 parity.
Opcode resolver **203–205** đã chạy trên VM độc lập cho IP dạng số;
hostname DNS vẫn dùng provider riêng của Stage 0/AOT và bị VM độc lập
từ chối với `error=22 opcode=203`. TLS **206–208** tiếp tục từ chối
với `error=22`. TLS production phải kiểm tra CA chain, hostname và SNI;
không hạ mức xác thực để di trú.
`src/runtime/native/vpp_aot_runtime.c` vẫn là runtime/fallback C cho các
đường AOT không chọn V++/ASM; `VPP_NATIVE_VPP_RUNTIME` đã có helper
spawn/wait V++/ASM riêng cho linker. Việc VM có primitive process chưa chứng
minh mọi AOT production hoặc compiler Stage 2→3 đã bỏ runtime C/C++.

## Socket TCP/UDP bằng V++/ASM (28/10/2026)

`gói/máy ảo/ổ cắm máy.vi` sở hữu token, kiểm tra kiểu và miền đối số,
giữ control block qua GC ở state slot 13, đóng socket còn sống khi VM
thoát hoặc gặp lỗi. Opcode **185–192** đi qua host ABI V++ tới native
primitive **ID 74** `máy ổ cắm thô`; tổng bảng intrinsic native i32 hiện
có **75 ID (0..74)**. Không lộ descriptor OS hay con trỏ 64-bit cho guest.

`scripts/ASM/vpp_vm_socket_primitives.inc` và
`vpp_vm_socket_x86_64.inc` gọi socket/connect/bind/listen/accept,
send/recv, setsockopt và close. Bốn target macOS/Linux × ARM64/x86-64
đã lắp ráp thành công. Địa chỉ IPv4/IPv6 sử dụng layout sockaddr riêng
cho mỗi OS (AF_INET6 = 30 trên Darwin, 10 trên Linux); Linux gửi
với `MSG_NOSIGNAL`, macOS đặt `SO_NOSIGPIPE`. x86-64 căn stack trước
khi gọi libc từ helper ASM. Khi cấp phát guest token thất bại, caller
vẫn sở hữu FD và đóng đúng một lần.

Harness `vm_asm_socket_harness.c` đã **PASS** trên macOS ARM64 và
x86-64/Rosetta, gồm TCP, UDP, IPv6 loopback, accept, timeout,
send/recv, sai offset, mở/đóng 40 vòng và đối chiếu số FD.
Ngày 28/10/2026, `connect()` dùng `fcntl(O_NONBLOCK)` + `poll(POLLOUT)`
theo timeout millisecond, đọc `SO_ERROR` để phân biệt kết nối thành công
với lỗi bất đồng bộ, sau đó khôi phục cờ FD. `accept()` dùng
`poll(POLLIN)` trước khi nhận kết nối; hết hạn trả `ETIMEDOUT` mà không
đóng hay ghi đè FD listener. Socket được accept kế thừa timeout
send/recv qua `setsockopt`. Harness kiểm chứng thêm flag blocking sau
connect, timeout accept 25 ms, listener còn nguyên và `SO_RCVTIMEO`
trên FD được accept. Hai kiến trúc đã chạy thực tế với quyền loopback.
`poll()` trên connect/accept nay thử lại khi `EINTR`, lấy thời gian còn lại
từ hạn chót `CLOCK_MONOTONIC` (không cộng lại toàn bộ timeout sau signal).
Connect bị ngắt trong lúc bắt đầu cũng được coi là đang chờ và kiểm tra
`SO_ERROR`. Harness ARM64/x86-64 gửi `SIGALRM` giữa `accept`, kiểm tra
`ETIMEDOUT`, giới hạn thời gian thực và listener còn nguyên; cả hai PASS.
`vm_asm_socket.vi` chạy trên VM V++/ASM đã **PASS** 40 vòng UDP
qua loopback. Phép thử socket yêu cầu môi trường cho phép bind tới
127.0.0.1; nếu sandbox từ chối bằng EPERM/EACCES, CMake gate ghi SKIP
thay vì xác nhận thành công. Các fixture đối số sai/token stale và
TLS chưa được xác minh vẫn chạy không cần mạng.

**Còn thiếu:** resolver hostname/DNS 203–205, TLS 206–208 với CA/hostname/SNI,
kiểm thử socket trên runner Linux thật và đầy đủ parity lỗi mạng.
Kiểm thử loopback chưa chứng minh thời hạn `connect` trên mạng bị blackhole
hoặc mọi trường hợp `EINTR` khi thực hiện `connect`.
Tầng socket đơn thuần không tự mã hóa dữ liệu.

## Resolver IP dạng số và vòng đời token (28/10/2026)

`gói/máy ảo/phân giải máy.vi` thực hiện `RESOLVE_OPEN` (203),
`RESOLVE_NEXT` (204), `RESOLVE_CLOSE` (205) trong V++ standalone VM.
Parser nhận IPv4 đủ bốn octet và IPv6 tám nhóm hoặc dạng `::` hợp lệ.
`NEXT` trả một địa chỉ rồi `rỗng`. Token tăng đơn điệu và không được
sử dụng sau `CLOSE`. Bảng tài nguyên giữ ở state slot 14, bộ đếm token
ở slot 15 trong state 16 slot i32; bảng được GC đánh dấu từ state root.
Token chỉ được công bố sau khi cấp phát và chèn thành công.

Regression `check_vm_asm_resolver.cmake` kiểm tra 40 vòng mở/đọc/đóng,
IPv6 biên (`::`, `::1`, `1::`), IPv4 rút gọn và số 0 đầu, IPv6 sai,
token không tồn tại, cổng sai, UTF-8 lỗi và NUL. Đối số không hợp lệ
trả `error=2`; hostname và địa chỉ số không được chấp nhận trả
`error=22 opcode=203`. VM hiện ghi opcode của process/socket/resolver
để chẩn đoán thao tác gây lỗi.

Đây là provider **numeric-only**: chưa giải `example.com`, chưa hỗ trợ
IPv4-mapped IPv6 và chưa chuẩn hóa chuỗi theo `getnameinfo()`.
TLS 206–208 vẫn fail-closed, không có CA-chain/hostname/SNI provider.
Lượt `vpp-vm-asm` mới PASS trong 49,04 giây; cả nhóm CTest liên quan
đạt **4/4 PASS** trong 83,48 giây. Hai socket harness cần bind vẫn SKIP
do sandbox; chưa có xác nhận thực thi syscall trên Linux.

## Đồng hồ, ngủ và entropy bằng V++/ASM (28/10/2026)

`gói/máy ảo/máy hệ thống.vi` thay phần kiểm tra đối số, dựng giá trị VM,
kiểm tra timespec và lặp đọc entropy vốn nằm ở adapter C++ trên đường cũ.
`host ABI.vi` chuyển trực tiếp bốn opcode hiện có tới các hàm này:

| Opcode | Chức năng trên VM độc lập |
| --- | --- |
| 173 | Ngủ một đợt 0..999 ms, trả errno của chính lần gọi |
| 174 | Đồng hồ đơn điệu, 16 byte seconds/nanoseconds little-endian |
| 175 | Đồng hồ UTC, cùng dạng timespec 16 byte |
| 176 | Entropy 1..256 byte; V++ xử lý EINTR và lần đọc thiếu byte |

Chia thời gian ngủ theo đợt và giữ deadline vẫn do `gói/hệ thống/nền tảng.vi`
thực hiện. API entropy 4096 byte của thư viện chuẩn tiếp tục ghép nhiều
khối bằng V++. Không thay số opcode hay định dạng ảnh `.vmb`.

`scripts/ASM/vpp_vm_system_primitives.inc` đổi offset arena thành con trỏ
64 bit sau khi kiểm tra giới hạn. Linux gọi trực tiếp `clock_gettime`,
`nanosleep`, `getrandom` bằng syscall; Darwin dùng syscall `poll` cho ngủ
và API libSystem `clock_gettime`/`getentropy` qua ASM. Đây vẫn là phụ thuộc
OS, không có adapter giá trị C++ trên đường VM này.

ABI riêng của compiler i32 thêm ba mục ở cuối bảng: 68 `máy đồng hồ thô`,
69 `máy ngủ thô`, 70 `máy entropy thô`. Các ID cũ giữ nguyên. Bộ phát mã,
object Mach-O và ELF đều nhận đủ 71 mục; kiểm thử ELF phát relocation cho
mục entropy cuối bảng ngay cả trên host ARM64.

CTest `vpp-vm-asm` chạy thêm `check_vm_asm_system.cmake`: API chuẩn, kích
thước/miền byte, nanoseconds, ngủ thực, dữ liệu sống qua GC, tám trường hợp
đối số sai và biên offset của primitive ASM. Khi có `VPP_EXEC`, ca dương
và các ca âm được đối chiếu với `run --compiler=cpp`. VM và chương trình
kiểm tra biên chạy với `PATH` rỗng. Quét symbol VM vẫn kiểm tra cầu nối C++.

Lượt Release trên macOS ARM64: **7/7 CTest PASS** (68,75 giây), gồm bootstrap,
VM/ASM, ELF, native runtime và opcode ABI; ASM lắp ráp được cho cả ARM64 và
x86-64 trên macOS/Linux. Log ở `build-ci-regression-fix/system-validation.log`
và `system-asm-check.log`. Linux mới được kiểm tra lắp ráp và phát ELF;
cần runner Linux để xác nhận syscall thực trên nền tảng đó.

Các adapter C++ vẫn được giữ cho Stage 0, oracle và runtime AOT tổng quát.
Kết quả này mở rộng VM độc lập, chưa chuyển runtime mặc định của mọi
executable AOT và chưa xác nhận compiler Stage 2/3 hội tụ.
