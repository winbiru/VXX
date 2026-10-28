# Ranh giới thư viện V++ và primitive VM

## Process, credential và socket VM/ASM (28/10/2026)

VM độc lập đã nhận UID/EUID/GID/EGID dạng u32 (tag 11 cho giá trị lớn
hơn INT32_MAX), process spawn/pipe/poll/read/wait/cleanup (193–202) và
socket TCP/UDP connect/listen/accept/send/recv/close/timeout (185–192).
Policy và quyền sở hữu tài nguyên ở `gói/máy ảo/*.vi`, ranh giới OS
ở `scripts/ASM/vpp_vm_{process,socket}_primitives.inc`; compiler i32
đã cấp tới native ID 74. Cửa sổ control và token ngăn chương trình guest
truy cập trực tiếp FD hoặc con trỏ hệ điều hành.

Kiểm chứng ngày 28/10/2026 tại `build-ci-regression-fix`:
`vpp-native-stage1-bootstrap`, `vpp-native-vpp-runtime`,
`vpp-native-cli-compiler-bootstrap` và `vpp-vm-asm` **4/4 PASS**
(83,48 giây, sau regression resolver). Process harness macOS ARM64 và x86-64/Rosetta **PASS**;
fixture VM bác bỏ UTF-8 sai trong executable/argv, NUL và tên biến môi
trường chứa `=`. Socket guest bác bỏ đối số/token sai, TLS 206–208
fail-closed **PASS**. Hai socket harness cần `bind` đều bị sandbox chặn,
**SKIP runtime** ở lượt này. Lượt chạy trước đã ghi nhận socket IPv6,
40 vòng không rò FD và bytecode guest PASS; không chuyển kết quả lịch sử
đó thành PASS của lượt mới. ASM lắp ráp cho macOS/Linux × ARM64/x86-64;
Linux mới có bằng chứng cross-assembly.

Deadline đơn điệu cho connect/accept và một số nhánh poll EINTR đã có;
chưa xác nhận toàn bộ send/recv hoặc tất cả trường hợp lỗi trên Linux.
Resolver số IPv4/IPv6 (203–205) đã được viết bằng V++ trong
`gói/máy ảo/phân giải máy.vi`, kiểm tra token, NUL, UTF-8, cổng và
các dạng IPv6 biên. Hostname DNS còn thiếu, IPv4-mapped IPv6 chưa hỗ trợ;
`example.com` trả `error=22 opcode=203`. Token resolver chỉ công bố sau
khi đăng ký thành công; `vm_process_new` cũng kiểm tra cấp phát và
append trước khi công bố token. Verified TLS (206–208) và những đường
AOT C/C++ khác còn phải di trú tiếp. Trên VM độc lập, TLS chưa có provider nên trả
`error=22 opcode=206/207/208`; không bỏ xác thực chứng chỉ, hostname hay SNI.
`src/runtime/native/vpp_aot_runtime.c` chỉ còn biên dịch trong chế độ
fallback không chọn `VPP_NATIVE_VPP_RUNTIME`; các đường Stage 0,
linker và runtime khác vẫn cần kiểm chứng riêng trước khi bỏ C/C++.

Trong `vpp-native-vpp-runtime`, kiểm toán đã mở rộng từ `nm -u` sang
`nm -g` (cả symbol được định nghĩa), từ chối dấu vết C++/Stage 0 và
trên macOS kiểm tra thư viện động bằng `otool -L` (libc++, libstdc++,
libffi). Đây là bằng chứng cho **executable fixture** được kiểm tra;
ASM vẫn gọi các dịch vụ của hệ điều hành và kết quả này không chứng minh
toàn bộ compiler/toolchain độc lập với C/C++. Trên Linux, kiểm toán
DT_NEEDED bằng `readelf` hoặc `llvm-readelf` là bắt buộc; thiếu công cụ
kiểm toán khiến bài kiểm thử FAIL. Chưa chạy kiểm toán này trên Linux
thực tế trong lượt hiện tại.

## Thư mục và PID/PPID khỏi adapter VM C++ (28/10/2026)

VM độc lập xử lý opcode 167/168 và 179..183 bằng V++/ASM. Policy,
UTF-8, bản ghi thư mục, token/owner và cleanup nằm trong
`gói/máy ảo/thư mục máy.vi`; syscall và con trỏ OS nằm trong
`scripts/ASM/vpp_vm_directory_primitives.inc`. Đường này không gọi
`executeDirectoryMachinePrimitive` hoặc `executeProcessIdentityPrimitive`.
`mkdir` đã sửa về `[rc, loại lỗi, errno]` và mode `0777` theo umask;
helper cache native `0700` giữ nguyên.

Kiểm chứng macOS ARM64: **10/10 CTest PASS**, cả API chuẩn, GC, UTF-8,
symlink, token stale/khác owner, cleanup lỗi và đối chiếu Stage 0. PID/PPID
còn được so trực tiếp với thông tin của tiến trình cha OS. Quét symbol VM
không thấy cầu nối C++ và `otool -L` chỉ có libSystem. ASM bốn tổ hợp
macOS/Linux × ARM64/x86-64 và ELF relocation ID mới đã kiểm tra; chưa
có kết quả thực thi runner Linux. Chi tiết ở `docs/vm-asm.md`.

Ở mốc chỉ kiểm thử thư mục, UID/GID rộng, spawn/pipe/wait và socket
chưa chuyển; tình trạng mới được ghi ở mục Process/socket phía trên.
Adapter C++ vẫn có caller ở Stage 0/AOT và được giữ làm oracle.

## Chuyển Clock/Sleep/Entropy khỏi adapter VM C++ (28/10/2026)

VM độc lập thực thi opcode 173..176 qua `gói/máy ảo/máy hệ thống.vi`
và `scripts/ASM/vpp_vm_system_primitives.inc`. Kiểm tra kiểu/giới hạn,
timespec, tạo list byte và vòng đọc entropy nằm ở V++; ASM chỉ kiểm tra
offset/con trỏ và gọi OS. Chính sách ngủ có deadline/EINTR vẫn ở thư viện
V++ hiện có. Các hàm `executeTimeClockPrimitive`, `executeTimeSleepPrimitive`,
`executeEntropyMachinePrimitive` của C++ không được gọi trên đường này.

Kiểm thử VM với PATH rỗng, đối chiếu Stage 0 và quét symbol đã PASS;
regression còn kiểm tra đối số sai, GC, biên arena và ELF relocation của
ABI mới. Bốn tổ hợp macOS/Linux × ARM64/x86-64 lắp ráp thành công; thực thi
đã kiểm chứng trên macOS ARM64. Chi tiết và lệnh dựng ở `docs/vm-asm.md`.

Các adapter C++ trên vẫn còn caller ở interpreter Stage 0 và runtime AOT
tổng quát nên được giữ lại. Chưa vô hiệu hóa hay xóa chúng; phụ thuộc OS
libSystem trên Darwin vẫn hiện diện qua lời gọi ASM trực tiếp.

## Bằng chứng đường VM/ASM độc lập (27/10/2026)

`gói/máy ảo/` thực thi heap, GC, collections, closure, class và
exception bằng V++ native i32, gọi primitive ASM cho thao tác OS/byte.
`scripts/quality/check_vm_asm.cmake` kiểm tra runtime với PATH rỗng
và quét undefined symbols để phát hiện cầu nối compiler/runtime C++;
regression bao gồm Unicode cache, GC sâu và bounds checking đã PASS.
Profiler và tối ưu segregated free-list được mô tả trong
`docs/vm-asm.md` và `docs/compiler-self-hosting-status.md`.

Việc VM độc lập chạy được và compiler Stage 1 tạo bytecode fixture khớp
native chưa chứng minh toàn bộ compiler Stage 2/3 hội tụ trên VM/ASM.
Runtime AOT tổng quát, host ABI chưa chuyển và assembler/linker vẫn có
những phụ thuộc bên ngoài phạm vi kiểm tra symbol VM.

## C++ đã vô hiệu hóa sau kiểm tra lời gọi (27/10/2026)

Đối chiếu `src/`, `scripts/`, `test/` và `benchmark/` cho thấy các API
postfix/shunting-yard `convertToPostfix`, `precedence_op`, `associativity_op`
chỉ còn khai báo/định nghĩa hoặc tự gọi nhau. Parser AST/IR hiện dùng đường
riêng. Đã **comment** `src/compiler/support/expression.cpp` khỏi tập nguồn
`vpp-compiler` trong CMake và khóa khai báo tương ứng bằng `#if 0`.

`symbolTable::getOrCreate` và `symbolTable::contains` không có caller trong
cây mã hiện tại; `src/compiler/support/symbolTable.cpp` cũng đã được comment
khỏi CMake, phần khai báo giữ lại dưới `#if 0`. Trong
`src/compiler/support/token_utils.cpp` đã khóa `trim`, `joinNameTokens`,
`isIdentifierLikeToken`, `splitTopLevelFields`, `extractParens`,
`extractBlock`, `extractAssignedVar` cùng khai báo tương ứng. Riêng
`isCallableNamePiece` **vẫn biên dịch** vì codegen Stage 0 còn sử dụng.

Đây là phần dọn mã không có caller trong repository; những API C++ cũ nếu
được ứng dụng bên ngoài liên kết trực tiếp sẽ cần điều chỉnh trước khi nâng
cấp SDK. VM, native object backend, host ABI, compiler Stage 0, linter/
formatter fallback và các adapter hệ điều hành còn có đường gọi thực tế,
vì vậy chưa bị vô hiệu hóa theo tên tệp hoặc theo tiền tố `vm_`.

## Formatter và linter V++ (27/10/2026)

`gói/biên dịch/định dạng mã nguồn.vi` thực hiện định dạng từ token và offset
Unicode của lexer V++, giữ comment, string literal và dấu tiếng Việt.
`vpp-native format <source.vi>` gọi formatter này, CLI mặc định
`--định-dạng` ưu tiên compiler đã bootstrap; `textDocument/formatting` của
LSP cũng dùng native formatter cho cả nội dung chưa lưu (snapshot tệp tạm).
C++ Stage 0 vẫn là đường bootstrap khi không có native compiler và là
oracle độc lập trong CTest `vpp-native-formatter-parity`. Test xác nhận
parity 8 fixture, Unicode, idempotence, file không bị thay đổi khi chỉ xem,
`--kiểm-tra` và `--ghi-tệp`. Binary native mới trong `build/native-compiler`
được ưu tiên trước fixture `build/native-cli` có thể cũ. LSP trả lỗi JSON-RPC
và tiếp tục hoạt động nếu formatter từ chối nguồn lỗi.

`gói/biên dịch/soát lỗi mã nguồn.vi` kiểm tra parser, semantic và đồ thị
import đệ quy qua resolver `.vi`. AST lưu vị trí dòng/cột byte UTF-8 của
khai báo để diagnostic khi khai báo trùng chỉ ra chính lần khai báo thứ hai.
CTest `vpp-native-lint` kiểm tra lỗi cú pháp, semantic, Unicode, import
thiếu/vòng/lỗi cú pháp trong module con và tệp không thay đổi. CLI
`--soát-lỗi` ưu tiên compiler native đã bootstrap; tùy chọn
`--compiler=vpp|cpp` chọn tường minh hoặc so sánh Stage 0. LSP hiện ưu tiên
chẩn đoán từ compiler V++ native, truyền snapshot bộ đệm chưa lưu cùng
`--base` là thư mục nguồn thật để kiểm tra import đệ quy trong bộ nhớ.
Host chỉ chuyển bản ghi dòng/cột UTF-8 byte sang range UTF-16. Không có
compiler native thì LSP tiếp tục dùng Stage 0. CTest
`vpp-native-lsp-diagnostics` kiểm tra bộ đệm chưa lưu khác đĩa, Unicode
surrogate pair, import tương đối và didClose; `vpp-native-lsp-format` vẫn
PASS. Những lỗi ngữ nghĩa khác của `.vi` còn có thể trả vị trí 1:1; chưa
đạt parity warning và semantic của tên xuyên mô-đun của Stage 0.
Kiểm chứng 27/10: `vpp-cli` build PASS, CTest native compiler bootstrap,
formatter parity, linter, LSP diagnostics, disassemble parity và AST/IR
đạt **6/6 PASS**. Fixture riêng của native lint xác nhận `--base`
phân giải module theo thư mục nguồn thật dù snapshot ở thư mục khác.

Quản lý bộ nhớ, VM legacy, host ABI, process/OS adapter, assembler/linker
vẫn là các phụ thuộc C++ cần chuyển tiếp. Các tên intrinsic `máy ảo ...`
trong `.vi` có thể được hạ sang machine opcode/native; phải kiểm tra đường
chạy thực tế trước khi đếm là cuộc gọi vào interpreter C++.

Ở nhánh bootstrap `build --target ... --emit-exe-selfhost`, việc tạo process
cho linker đã được hợp nhất về `vietvm::native::aot::runProcess()` thay cho
đường `fork/execvp/waitpid` lặp lại trong `src/cli/main.cpp`. Host thu và
chuyển tiếp chẩn đoán linker, chuẩn hóa mã thoát (kể cả 127 khi không thể
khởi chạy linker), và chỉ công bố executable sau khi liên kết thành công.
CTest `vpp-native-selfhost-executable` có thêm tình huống linker thiếu nhưng
đầu ra hiện hữu phải giữ nguyên. Đây là việc thu gọn **host ABI C++**;
`compileSourceProgramVpp()` trong bootstrap vẫn đi qua bridge C++/VM và
chưa được coi là chuyển hoàn toàn sang `.vi`/ASM. Compiler V++ Stage 1
đã có đường biên dịch/liên kết riêng trong `gói/biên dịch/bản địa/trình điều khiển.vi`.

CLI có thêm `build --target <host-triple> --emit-exe-vpp <source.vi> <output>`
để gọi trực tiếp `vpp-native build ... -o ...` đã bootstrap, xuất executable
bằng frontend và bộ phát ASM `.vi`. Đường này kiểm tra target
trùng host, khóa dự án và bảo vệ source trước khi thực thi; giới hạn ở
macOS AArch64 và Linux x86-64 mà trình điều khiển Stage 1 đang hỗ trợ.
Khác đường bootstrap Stage 0, thao tác này không gọi
`parseTargetVpp()` hoặc `compileSourceProgramVpp()` qua C++ VM. Kiểm thử
native executable thực hiện so sánh hành vi với Stage 1 ngay cả khi cấu hình
`VPP_NATIVE_CC` trỏ tới linker C không tồn tại. AOT host trong compiler native
vẫn chạy assembler/linker và liên kết runtime C/C++; `otool -L` trên
executable macOS phát hiện `libc++`, `libffi`, `libSystem` và framework TLS.
Vì vậy đường mới giảm phụ thuộc **legacy bridge C++/VM** ở CLI, chưa hoàn tất
mục tiêu chỉ còn V++/ASM ở linker, runtime và quản lý bộ nhớ.

## AST/IR dump native (27/10/2026)

`--dump-ast` và `--dump-ir` đã chuyển sang frontend/parser và IR của `.vi`
khi có compiler V++ đã bootstrap; `--compiler=cpp` giữ Stage 0 để đối
chiếu. Source-only IR bảo toàn import dưới dạng metadata `chưa liên kết`,
không chạy source, không tạo artifact. CTest `vpp-native-ast-ir` kiểm tra
schema, lỗi cú pháp, CLI mặc định/native tường minh và import. Kết quả
AST/IR của V++ có schema khác Stage 0 và chưa mang toàn bộ vị trí nguồn
như diagnostic C++.

Linter và formatter Stage 0 vẫn ở `src/tooling/tooling.cpp`, dispatcher trong
`src/cli/main.cpp`; tình trạng chuyển đổi native được cập nhật ở đầu tài liệu.
`src/cli/native_vpp_codegen.cpp`
giữ các bridge cho đường target legacy, `src/native/aot_driver.cpp` giữ
host ABI/assembler/linker, các adapter OS và `src/runtime/vm.cpp` vẫn còn.
Tên intrinsic `máy ảo ...` trong mã `.vi` là giao diện builtin, có thể
được backend native hạ xuống mã máy/ASM; chỉ riêng tên hàm không chứng
minh quá trình thực thi đã quay lại interpreter C++.

## Xuất raw/object trực tiếp bằng V++ (27/10/2026)

`src/cli/main.cpp` chuyển `build --target ... --emit-raw` sang compiler
native sau bootstrap; việc hạ leaf và encoder ISA nằm trong thư viện `.vi`
cho những kiến trúc đã hỗ trợ. AArch64/x86-64 còn có đường Machine IR tổng
quát khi biểu thức vượt tập leaf. Với `--emit-obj` và
`--emit-obj-selfhost` trên hai kiến trúc này, parser, hạ IR, encoder và
object writer đều chạy trong V++; C++ chỉ chuyển argv và duy trì kiểm tra
project lock/path. `--emit-asm` dùng bộ in assembly V++; object của các
kiến trúc khác giữ đường cũ để tránh thay đổi ABI chưa kiểm chứng.

## Kiểm kê lời gọi VM/C++ và chuyển mặc định sang V++ (27/10/2026)

Trên CLI đã chuyển các đường chạy tệp thường, `test`, batch test và REPL
sang compiler V++ đã bootstrap khi tìm thấy qua cấu hình hoặc cây build.
Native compiler dùng frontend `.vi` và backend ASM; CLI C++ còn quản lý
argv, subprocess, kiểm tra project lock, output và lifecycle tệp tạm. Không
có compiler native hoặc thiếu root toolchain (cấu hình hay cây mã nguồn
đầy đủ) thì vẫn dùng Stage 0 C++/VM;
`--compiler=cpp` cho chạy tệp/REPL vẫn là đường tương thích tường minh.

Các đường còn phụ thuộc C++/VM theo kiểm kê mã nguồn:

| Vị trí | Phạm vi phụ thuộc còn lại |
| --- | --- |
| `src/cli/main.cpp` | Lint/format/dump/disassemble native mặc định khi đã bootstrap; LSP diagnostics/format native mặc định với Stage 0 dự phòng, các tính năng LSP khác và CLI quản lý gói vẫn C++, `--compiler=cpp` |
| `src/cli/native_vpp_codegen.cpp` | Các bridge `executeVppSource()` dùng C++ để chạy compiler `.vi` dạng wrapper ở một số lệnh target cũ |
| `src/native/aot_driver.cpp` | Host ABI, tạo process, assembler/linker và thực thi, vẫn viết C++ |
| `src/runtime/vm.cpp` và `src/runtime/native/vm_native_*.cpp` | Interpreter cũ, kiểm tra opcode/capability, heap/value và các helper native |
| `src/runtime/{foreign,socket_machine,file_machine,directory_machine,path_machine,process_identity,time_clock,time_sleep,entropy_machine,environment_machine}.cpp` | OS adapter và kiểm tra tài nguyên, một phần đã gọi ASM `scripts/ASM/*.S` |

Compiler tự host chưa thay thế runtime giá trị động hoặc host ABI.
Đã chuyển đường thực thi mặc định sang `.vi`/ASM, nhưng executable vẫn liên
kết code C/C++, libc và TLS provider trên nền tảng tương ứng. Cần đối chiếu
parity trên Linux/Windows trước khi thay thế các adapter OS còn lại.

## Trạng thái hiện tại (22/10/2026)

Kiểm kê thư viện chuẩn `gói/**/*.vi`: **0 khai báo `ngoại hàm`**, **0 khai
báo `ngoại thư viện`** (tại mốc trước khi chuyển Process/Resolver/TLS:
16/2). Các module gọi Machine Intrinsics, còn VM thực thi các primitive và
quản lý token tài nguyên. Các mục bên dưới ghi lại những mốc migration lịch
sử; những con số FFI/TLS trong các mục ngày 20–21/10 phản ánh trạng thái ở
thời điểm tương ứng, không phải số liệu hiện tại.

| Nhóm | Machine opcode | Trạng thái |
| --- | --- | --- |
| Socket | 185–192 | API `.vi` gọi Machine Socket, adapter OS còn tồn tại |
| Process | 193–202 | Đã chuyển khai báo FFI sang Machine Process; spawn/pipe/wait vẫn dùng adapter POSIX/Win32 |
| Resolver | 203–205 | Đã chuyển khai báo FFI sang Machine Resolver; `getaddrinfo`/`getnameinfo` vẫn dùng OS |
| TLS | 206–208 | Đã chuyển khai báo FFI sang Machine TLS trên POSIX; SecureTransport/OpenSSL vẫn cung cấp mã hóa, xác minh chứng chỉ và hostname |

Registry hiện có **103 Machine Intrinsics** (110 tên nếu tính alias), đồng
bộ với bảng opcode V++/C++ gồm **193 mục**. Trong `src/` vẫn có **76 tệp
nguồn C/C++**; `foreign.cpp` vẫn chứa nhánh `ffi_call`/`dlopen`/`dlsym` để
đọc bytecode FFI cũ. Binary build mặc định trên macOS vẫn liên kết `libffi`,
`libc++`, `libSystem` và framework TLS của OS. Chưa đạt runtime chỉ có
`.vi`/ASM.

Resolver và TLS Machine ABI trên POSIX không cần macro `VPP_HAS_LIBFFI`;
đường FFI tương thích cũ vẫn giữ kiểm tra ABI riêng. Native AOT đã được
cấp các capability `system.net.tls.upgrade/send/recv` giống Interpreter;
runtime P0 hardening kiểm tra capability, arity, token liên VM, trạng thái
socket, dữ liệu TLS và cleanup khi handshake lỗi. Native AOT đã PASS DNS
(7/7), Process spawn, File I/O và TLS validation với compiler mới
bootstrap ngày 22/10/2026. TLS handshake/certificate/hostname thực tế và
Windows parity vẫn cần kiểm chứng; Windows TLS tiếp tục dùng Schannel VM
cũ. Máy chạy fixture bị hạn chế bind loopback nên kết quả SKIP không
chứng minh kết nối TLS thực đã thành công.

Có thể build POSIX **không liên kết libffi** bằng:

```sh
cmake -S . -B build-machine -DBUILD_TESTS=OFF -DVPP_ENABLE_LEGACY_LIBFFI=OFF
cmake --build build-machine --target vpp-cli -j4
```

Kiểm chứng trên macOS ARM64: binary không còn phụ thuộc `libffi.dylib`
(`otool -L`), **12/12 fixture `.vi`** cho Environment, Clock, UTC,
Local Time, Sleep, Entropy, Process, DNS, File, Directory, Socket error
và TLS validation PASS với cấu hình không libffi. Native AOT no-libffi
cho DNS, Process spawn và TLS validation **3/3 PASS**: biên dịch, chạy và
kiểm tra `otool -L` trên từng executable đều không thấy `libffi.dylib`.
Nguyên nhân trước đây là linker phụ Native AOT liên kết `libffi` vô điều
kiện; hiện `src/native/aot_driver.cpp` truyền trạng thái build xuống
`scripts/quality/native_aot_link/CMakeLists.txt` để liên kết theo cấu hình.
CTest `vpp-machine-no-legacy-libffi` PASS (46,53 giây, 22/10/2026).
Build mặc định tiếp tục liên kết `libffi` cho executable Native AOT DNS;
chạy fixture DNS và kiểm tra dependency đều PASS. `libc++`/`libSystem`/
TLS framework vẫn được liên kết. CI Linux x64 và ARM64 đã được bổ sung bước build và
kiểm tra không libffi, hiện chưa có kết quả runner sau thay đổi. Mặc định
`VPP_ENABLE_LEGACY_LIBFFI=ON` để giữ tương thích bytecode FFI cũ; khi OFF,
những chương trình thật sự sử dụng `ngoại hàm` cũ sẽ không gọi libffi được.
Lỗi Native AOT `máy_môi_trường_đọc` trước đó bắt nguồn từ binary compiler
`build/bin/vpp-native` cũ (19/10/2026) chưa đồng bộ Machine Intrinsics.
Bootstrap lại compiler từ `gói/biên dịch/dòng lệnh bản địa.vi` đã sửa lỗi này.
Đã đồng bộ 16 intrinsic Process/Resolver/TLS và opcode 193–208 vào compiler
`.vi`; import `gói/lõi/chuyển kiểu` được bổ sung trực tiếp cho DNS. Bài
`vpp-native-machine-system-fixtures` dùng compiler Stage 1 **đầy đủ** do
CTest `vpp-native-cli-compiler-bootstrap` tạo từ `native_cli.vi` để xác nhận
12 fixture Native AOT. Compiler Stage 1 dạng leaf trong
`checknativedriver.cmake` chỉ hỗ trợ tập cú pháp nhỏ, không biên dịch được
fixture nhập thư viện hệ thống (mã thoát 12), nên đã sửa dependency CTest
để dùng đúng compiler. Sau khi sửa, CTest **2/2 PASS**
(`vpp-native-cli-compiler-bootstrap` + `vpp-native-machine-system-fixtures`,
611,18 giây trên macOS ARM64, 22/10/2026). Installed three-stage smoke
cũng kiểm tra thêm Process/Environment/Clock/Local Time/Sleep ở Stage 3.
Kiểm tra Stage 0 → Stage 1 → Stage 2 → Stage 3 ngày 22/10/2026
**PASS**, hash compiler Stage 2 và Stage 3 đồng nhất
(`2dda44b7b789240c609cc253b09ff7162734a84a350acde5ac045ebf10b3bdcd`).
Chưa kết luận runtime thuần V++/ASM hay TLS end-to-end đã hoàn tất.
Full regression ngày 22/10/2026: **339 PASS, 0 FAIL**.

## TCP/UDP hostname với DNS V++ và Machine Socket (21/10/2026)

Module mới `gói/mạng/kết nối phân giải tên miền.vi` cung cấp `đầu nối phân giải tên miền vi địa chỉ`,
`đầu nối giao vận tin cậy mở phân giải tên miền vi` và `đầu nối giao vận gói tin mở phân giải tên miền vi`. Một hostname được tra
qua DNS Hosts hoặc DNS Wire Resolver V++ khi đã cấu hình ít nhất một trong
`VPP_DNS_VI_HOSTS_FILE`, `VPP_DNS_VI_SERVER`, `VPP_DNS_VI_SERVERS`,
`VPP_DNS_VI_RESOLV_CONF`. Nếu chưa cấu hình, module từ chối tên máy để
không rơi về OS/getaddrinfo. Địa chỉ IP literal đi thẳng qua parser V++,
IPv4 với số 0 đầu được chuẩn hóa sang decimal dotted-quad.

Danh sách địa chỉ từ DNS V++ được thử tuần tự: kết nối TCP/UDP qua
`máy_socket_kết_nối`, áp timeout qua `máy_socket_timeout`; nếu cấu hình
timeout thất bại thì đóng descriptor trước khi thử địa chỉ khác. API trả
socket handle tương thích với `đầu nối gửi` / `đầu nối nhận` / `đầu nối đóng`
hiện hành. Không có `ngoại hàm` trong connector mới; thao tác socket thấp
nhất vẫn có adapter C++ và ASM như đã ghi ở các mục dưới. Chế độ này
chưa hỗ trợ Windows vì Machine Socket connect chưa có đường thực thi
WinSock tương ứng; TLS vẫn là FFI.

Kiểm thử không cần mạng thật xác nhận địa chỉ literal, chuẩn hóa IPv4,
hosts V++, lỗi thiếu cấu hình (không fallback OS), lỗi cổng/timeout, trên
cả interpreter/JIT. Chưa kiểm chứng trực tiếp kết nối qua DNS Wire với
DNS server thực trong sandbox bị hạn chế bind.

Kiểm chứng macOS ARM64: `cmake --build build -j4` PASS;
CTest Socket DNS V++, Socket Hosts, DNS Config, DNS Search, DNS Hosts và
DNS Hosts Integration **6/6 PASS** (84,29 giây); audit V++/ASM PASS với
**16 ngoại hàm / 2 ngoại thư viện** còn lại. Full regression của đợt Socket
Hosts trước đó kết thúc **339 PASS, 0 FAIL**; do tiếp tục sửa mã trong
khi bộ này chạy, kết quả đó không thay thế cho việc chạy lại toàn bộ
regression sau tất cả thay đổi hiện tại. Linux/Windows runtime và kết nối
DNS thực vẫn chưa được xác minh ở lượt này.

## DNS config độc lập và chặn resolver C theo yêu cầu (21/10/2026)

`gói/mạng/cấu hình phân giải tên miền.vi` nay nhập `gói/mạng/địa chỉ mạng.vi` và dùng trực tiếp
`Bộ Phân Tích IP` để xác thực, so sánh và loại trùng nameserver.
Không còn nhập Socket chỉ để gọi parser; giảm phụ thuộc giữa DNS Config
và Socket, đồng thời giữ nguyên định dạng cấu hình/search/ndots.

Đặt `VPP_SOCKET_VI_ONLY=1` để `đầu nối bộ phân giải danh sách` chỉ chấp nhận
địa chỉ số hợp lệ hoặc hostname có trong tệp `VPP_DNS_VI_HOSTS_FILE`
được cấu hình. Nếu không tìm thấy, hàm báo lỗi `chế độ VI_ONLY` trước
khi gọi `system.net.resolve.open`/`getaddrinfo`. Nhờ đó chương trình
có thể chủ động chặn đường resolver C khi cần kiểm tra ranh giới
V++/Assembly. Mặc định `VPP_SOCKET_VI_ONLY=0` để giữ hành vi tương
thích; chế độ này chưa thực hiện DNS network resolver tự động bên
trong Socket (ứng dụng có thể gọi API DNS V++ đã có để tra cứu trước).

Fixture Socket chạy cả hai cấu hình trên Interpreter/JIT, xác nhận
không gọi OS resolver khi tên vắng trong hosts ở chế độ VI_ONLY,
không trùng IPv6 biểu diễn khác nhau, bỏ IP không hợp lệ và giữ
cổng biên 1/65535. Boundary audit chặn DNS Config phụ thuộc trở lại
Socket hoặc đưa FFI vào module này.

## Socket hostname từ hosts thuần V++ (21/10/2026)

Bộ phân tích IPv4/IPv6 được tách khỏi `gói/mạng/ổ cắm.vi` sang
`gói/mạng/địa chỉ mạng.vi` (`Bộ Phân Tích IP`). `dns_hosts.vi` dùng trực tiếp bộ
phân tích chung, nên không cần nhập module Socket. Socket có thể nhập
DNS Hosts mà không tạo vòng phụ thuộc. Mọi thao tác phân tích địa chỉ
vẫn được thực hiện bằng V++; mã dữ liệu 4/16 byte chuyển sang Machine
Socket ABI hiện có.

Khi đặt `VPP_DNS_VI_HOSTS_FILE` trỏ tới tệp hosts, hàm
`đầu nối bộ phân giải danh sách` ưu tiên địa chỉ IP số, sau đó thử đọc
hostname qua DNS Hosts và File Machine ABI. Tên có trong tệp sẽ không
gọi `getaddrinfo` hoặc cấp token resolver FFI. Giữ thứ tự IP xuất hiện
trong hosts (sau khi loại các địa chỉ trùng), với kết quả `[ip, port]`
khi cổng khác 0 hoặc `ip` khi cổng bằng 0. Không có địa chỉ phù hợp
thì tiếp tục dùng OS resolver để giữ tương thích. Chỉ đọc hosts khi
được cấu hình rõ ràng, chưa tự đọc cấu hình OS mặc định hay tự động
dùng DNS wire resolver cho socket.

Fixture `vpp-socket-hosts-vpp-fixture` kiểm tra hostname, bí danh
không phân biệt hoa/thường, IPv4/IPv6, kết quả có/không có cổng và IP
literal trên Interpreter/JIT, không cần truy cập mạng. Phần nguồn
Socket connect Unix bổ sung kiểm tra tràn khi tính `int64` deadline
từ Machine Monotonic Clock; nhánh lỗi vẫn đóng descriptor.

## Literal IP và deadline kết nối (21/10/2026)

`đầu nối bộ phân giải danh sách` kiểm tra IPv4/IPv6 dạng số bằng parser `.vi`
trước khi gọi `system.net.resolve.open`/`getaddrinfo`. Các literal IPv4
phải đúng dạng thập phân chuẩn (không có số 0 đầu), tránh trường hợp
libc hiểu một cách khác; hostname và địa chỉ khác vẫn dùng OS resolver để
giữ tương thích. Hàm trả về cấu trúc và thứ tự như API resolver hiện hành,
không cấp token FFI khi nhận literal. Đường kết nối TCP/UDP vẫn dùng opcode
192 và token socket được VM quản lý.

Deadline cho `socket_machine::connect` trên Unix đã bỏ `std::chrono` và dùng
`machineMonotonicMilliseconds` trong `time_clock.cpp`, chung đường đọc clock
Machine ABI với `máy_đồng_hồ_đơn_điệu`. Linux ARM64/x86-64 gọi
`SYS_clock_gettime` qua Assembly; Darwin gọi `clock_gettime` libSystem qua
trampoline Assembly vì SDK không công bố syscall số tương ứng. Không thêm
opcode hoặc phụ thuộc thư viện ngoài. Windows giữ WinSock và `std::chrono`.

Phần connect và OS resolver chưa phải mã `.vi`/ASM hoàn toàn: vẫn có adapter
C++ để tạo sockaddr, xử lý poll/setsockopt và quyền sở hữu token; hostname
vẫn dùng getaddrinfo và TLS vẫn qua provider riêng.

## TCP/UDP connect qua Machine ABI (20/10/2026)

Opcode **192** (`máy_socket_kết_nối`, capability
`machine.net.socket.connect`) nhận bốn hoặc mười sáu byte địa chỉ mạng,
cổng 1..65535, cờ UDP 0/1 và timeout 1..300000 ms. Thư viện
`gói/mạng/ổ cắm.vi` tự phân tích IPv4/IPv6 (bao gồm `::` và IPv4 suffix),
quản lý danh sách DNS, retry, timeout public và vòng gửi dữ liệu; không
gọi `inet_pton`/`getaddrinfo` tại primitive connect. Unix ARM64/x86-64
sử dụng syscall `socket`, `fcntl`, `connect`, `poll`/`ppoll`,
`getsockopt(SO_ERROR)`, `setsockopt` và `close` qua Assembly. Windows dùng
WinSock adapter, token có kích thước phù hợp pointer. Descriptor được đóng
khi connect, timeout hoặc thiết lập blocking thất bại. Legacy FFI bytecode
vẫn duy trì tương thích trong runtime.

Ngoại hàm `.vi` giảm **17 → 16**, ngoại thư viện vẫn **2**. IPv6 scope ID
(`%interface`) chưa được biểu diễn trong byte address API. Cần kiểm tra
connect TCP/UDP thực tế trên runner cho phép mạng, đặc biệt Linux và Windows.
Hệ thống hiện vẫn dùng C++ runtime, libc++/libffi và TLS provider; đây là
bước migration ranh giới Socket, chưa đạt mục tiêu runtime thuần `.vi`/ASM.

## TCP listen qua Machine ABI (20/10/2026)

`máy_socket_lắng_nghe` (**191**, capability `machine.net.socket.listen`)
nhận cổng IPv4 1..65535 và backlog 1..1024, trả token listener thuộc VM.
Lớp V++ tiếp tục kiểm tra tham số và xử lý accept/timeout. macOS và Linux
ARM64/x86-64 gọi `socket`, `bind`, `listen` qua syscall 4 tham số trong
Assembly; `setsockopt(SO_REUSEADDR/SO_NOSIGPIPE)` dùng cầu syscall 5 tham
số trên nền tảng hỗ trợ. Windows giữ adapter WinSock với kiểm tra WSAStartup.
Lỗi bind/listen được chuyển thành ngoại lệ bắt được trong V++, mọi socket
tạo mà chưa thể đăng ký token sẽ được đóng.

Ngoại hàm `.vi` giảm **18 → 17**; vẫn còn **2** thư viện ngoại. Chạy
trên macOS sandbox có thể chặn bind loopback, vì vậy chưa thể xem fixture
được SKIP như bằng chứng accept/listen runtime hoạt động; cần runner Linux
và Windows, thêm kiểm thử macOS với quyền bind socket.

## Socket accept qua Machine ABI (20/10/2026)

`máy_socket_chấp_nhận` (opcode **190**, capability
`machine.net.socket.accept`) thay binding `.vi` `socket_chấp_nhận_ffi`.
Primitive chỉ nhận token listener TCP thuộc VM hiện tại và trả về token
socket mới; không trả raw file descriptor. `gói/mạng/ổ cắm.vi` tiếp tục
đặt timeout cho socket mới và đóng nếu cấu hình public API lỗi.
Adapter Machine cũng đặt timeout mặc định 300000 ms và áp dụng
`SO_NOSIGPIPE` trên macOS trước khi đăng ký token; nếu bất cứ bước nào
thất bại thì đóng descriptor vừa accept. Trên macOS ARM64/x86-64 dùng
`SYS_accept` qua `vpp_machine_file_syscall4` Assembly; trên Linux dùng
`SYS_accept4` với flags=0. Windows giữ WinSock adapter và registry token
pointer-sized. Lỗi OS/closed token có thể bắt bằng `bắt lỗi` trong V++;
sai capability, arity và loại listener là lỗi VM.

Ngoại hàm `.vi` giảm **19 → 18**, số khai báo ngoại thư viện giữ **2**.
Bytecode FFI đời cũ vẫn được thực thi qua adapter tương thích. Cross-assembly
Linux và bản build macOS không đồng nghĩa đã xác nhận Linux/Windows runtime;
TLS, DNS, socket connect/listen và process FFI chưa chuyển.

## Raw socket send/recv qua Machine ABI (20/10/2026)

`máy_socket_gửi` (**188**) và `máy_socket_nhận` (**189**) nhận token VM,
danh sách byte, chiều dài và flags=0. VM kiểm tra quyền riêng
`machine.net.socket.send`/`machine.net.socket.recv`, độ dài đệm, byte 0..255,
loại socket, và không cho raw I/O qua listener/TLS. Trên Unix ARM64/x86-64,
`sendto`/`recvfrom` gọi syscall sáu tham số qua Assembly; Linux đặt
`MSG_NOSIGNAL` chống SIGPIPE, macOS tiếp tục dùng `SO_NOSIGPIPE` trên socket
được tạo bằng adapter. Hệ thống khác giữ WinSock/POSIX adapter.
V++ tiếp tục sở hữu việc chia byte UTF-8, partial send và UDP datagram.
Token socket sai/hết hiệu lực và lỗi OS là ngoại lệ V++ có thể `bắt lỗi`;
thiếu capability hay sai kiểu/arity là lỗi VM. Bytecode FFI cũ vẫn được
nhận diện để bảo toàn tương thích.

Sau ba primitive này, ngoại hàm giảm **22 → 19**, ngoại thư viện `.vi`
giảm **3 → 2**. Native TLS send/recv, DNS, connect/listen/accept và
process launch vẫn còn phụ thuộc C/OS native.

## Socket timeout qua machine ABI (20/10/2026)

`đầu nối đặt thời gian chờ` và timeout sau kết nối dùng `máy_socket_timeout`
(opcode **187**, capability `machine.net.socket.timeout`). Interpreter
và AOT kiểm tra token trong registry của từng VM, duration i32 trong miền
1..300000 ms và quyền riêng; trả `1` khi hai tùy chọn nhận/gửi thành công.
Trên macOS/Linux ARM64/x86-64, `SO_RCVTIMEO`/`SO_SNDTIMEO` đi qua syscall
`setsockopt` năm tham số, dùng `vpp_machine_socket_syscall5` trong
`scripts/ASM/vpp_file_primitives.S`. Trên Windows vẫn dùng WinSock, các
nền POSIX chưa có ASM giữ đường dự phòng. Adapter kết nối POSIX cũng dùng
machine timeout này để giảm thêm một phụ thuộc libc.

Tổng `ngoại hàm` còn **21** (Network 11, Process 10); TLS, DNS, connect,
listen, accept và raw send/recv chưa chuyển. Build trên hệ điều hành khác
cần được kiểm chứng riêng; compiler/runtime vẫn còn C++ và libffi.

## Socket kind/close qua machine ABI (20/10/2026)

`gói/mạng/ổ cắm.vi` chuyển hai `ngoại hàm` sang `máy_socket_loại`
(opcode **185**, capability `machine.net.socket.kind`) và
`máy_socket_đóng` (opcode **186**, capability `machine.net.socket.close`).
VM interpreter và native AOT dùng chung `ForeignFileState::sockets` cho
token socket đã tạo qua TCP/UDP FFI, vì vậy cả TLS session, kiểm tra
token thuộc VM, lỗi khi đóng hai lần và dọn dẹp khi VM hủy đều tiếp tục
hoạt động. Trên Unix ARM64/x86-64, đóng raw socket gọi trực tiếp syscall
`close` qua `scripts/ASM/vpp_file_primitives.S` (đã có); socket TLS vẫn
phải đi qua provider TLS để giải phóng phiên và descriptor đúng cách.
Windows hiện dùng `closesocket` của WinSock. Dữ liệu/chính sách trong V++
không được phép truy cập file descriptor thô.

Foreign declarations giảm **24 → 22**, audit giới hạn 22; vẫn còn 12
Network FFI và 10 Process FFI. Đây là chuyển đổi hai primitive, chưa thay
toàn bộ DNS, TCP/UDP send/recv/connect/listen, TLS và process launch.
Compiler/runtime vẫn có C++; TLS vẫn dùng OpenSSL/SecureTransport/Schannel.

## Path/Stat qua machine ABI (20/10/2026)

`gói/nhập xuất/đường dẫn.vi` đã bỏ năm foreign bindings liên quan đến
`access`, `stat`, `lstat`, `stat_mode_offset` và path-kind Windows; không
còn khai báo ngoại thư viện ở module. Các API `đường dẫn tồn tại`,
`đường dẫn loại posix/windows`, `là tệp`, `là thư mục` tiếp tục do V++
triển khai. Opcode **184** (`máy_đường_loại`, capability
`machine.path.kind`) nhận `(đường_dẫn UTF-8, follow i32)`; `follow=0`
không theo symbolic link, `follow=1` theo symbolic link. Giá trị trả về
là `-1` khi không thể lấy trạng thái; trên POSIX là `S_IFMT >> 12`
(`4` thư mục, `8` tệp thường, `10` symbolic link), trên Windows là
`0` khác/reparse, `1` tệp, `2` thư mục. Interpreter và native AOT dùng
cùng một executor, kiểm tra quyền, arity, loại đối số, UTF-8 và byte NUL.

Linux ARM64/x86-64 gọi thẳng `newfstatat` từ Assembly
(`scripts/ASM/vpp_path_primitives.S`), sử dụng `AT_FDCWD` và
`AT_SYMLINK_NOFOLLOW` khi `follow=0`; không cần `stat/lstat` của libc
trong nhánh này. C++ adapter vẫn giải mã `struct stat` theo ABI của host.
macOS ARM64/x86-64 còn Assembly branch đến `stat/lstat` của **libSystem**;
Windows dùng adapter C++/Win32 `CreateFileW`, `GetFileInformationByHandle`.
Quy ước `đường dẫn tồn tại` hiện dùng thống kê có theo symlink; dangling
symlink vì vậy cho kết quả không tồn tại ở API này, nhưng được nhận diện
khi `follow=0`. Khả năng quan sát khi thiếu quyền truy cập có thể khác
`access(F_OK)` cũ và cần kiểm thử chuyên biệt.

Trên macOS ARM64 đã xác nhận hardening Path/Stat và Directory (bao gồm
symlink/dangling symlink), fixture interpreter/JIT, capability native AOT,
cross-assembly cả bốn Unix ABI và audit. Cross-assembly chỉ xác nhận cú pháp
và việc tạo object, **chưa phải kiểm thử Linux/Windows runtime**. Các bài
test hồi quy được ghi nhận sau khi chạy. Số ngoại hàm trong `.vi` giảm
**29 → 24**, thư viện ngoại **6 → 3**; audit giữ budget 24.
`vpp-cli` trên macOS vẫn liên kết `libffi`, `libc++`, `libSystem`, Security
và CoreFoundation; compiler/runtime hiện vẫn sử dụng C++.

## Directory Operations qua machine ABI (20/10/2026)

`gói/nhập xuất/thư mục.vi` đã bỏ 8 foreign bindings: `mkdir`, `remove`,
`opendir`, `readdir`, `closedir`, `last_errno`, `eexist`, `enoent` và hai
khai báo ngoại thư viện. Public API `tạo thư mục`, `liệt kê thư mục`,
`xóa đường dẫn` giữ nguyên. V++ tiếp tục quản lý tạo cha, duyệt cây không
đệ quy, thứ tự sắp xếp, phát hiện symlink và xử lý lỗi.

Năm intrinsic `máy_thư_mục_tạo/xóa/mở/kế/đóng` có opcode **179–183** và
quyền độc lập `machine.dir.mkdir/remove/open/next/close`. `mkdir/remove`
trả `[rc, loại lỗi, errno]`, loại lỗi chuẩn hóa `1` = đã tồn tại,
`2` = không tồn tại, `3` = lỗi khác. `open` trả `[token, errno]`;
`next` trả `[1, tên]`, `[0, ""]` khi EOF, `[-errno, ""]` khi lỗi;
`close` trả `[rc, errno]`. Token được cấp phát toàn cục, nhưng được xác
minh trong state riêng của từng VM/AOT và được đóng khi VM hủy/reset; token
đã đóng hay token từ VM khác bị từ chối. `readdir` phân biệt EOF và lỗi
bằng cách chụp `errno` ngay trong primitive.

macOS/Linux ARM64/x86-64: tạo/xóa sử dụng `SYS_mkdir`/`mkdirat`,
`unlink`/`unlinkat`, `rmdir`/`unlinkat(AT_REMOVEDIR)` trực tiếp qua Assembly
syscall, nên không dùng FFI cho tạo/xóa. Linux iteration nay sử dụng
`openat(O_DIRECTORY|O_CLOEXEC)`, `getdents64` và `close` qua syscall Assembly;
C++ adapter kiểm tra độ dài record, NUL, UTF-8, lưu buffer/tập tin theo token
VM và phân biệt EOF/lỗi. macOS iteration vẫn branch đến
`opendir/readdir/closedir` của **libSystem**; chưa đạt độc lập thư viện
hệ thống ở macOS. Windows vẫn dùng adapter C++/Win32 `CreateDirectoryW`,
`DeleteFileW`, `RemoveDirectoryW`, `FindFirstFileW`, `FindNextFileW`,
`FindClose`, chuyển UTF-8/UTF-16. Windows/Linux runtime CI chưa được xác nhận.

Regression migration gồm hardening (capability, arity, token, Unicode, EOF,
EEXIST/ENOENT, AOT), V++ fixture, và cross-assembly của 4 Unix ABI.
Linux getdents64 mới chưa được xác nhận bằng Linux runtime CI.
Foreign functions **37 → 29**, foreign libraries **8 → 6**;
audit giới hạn 29 và ngăn tái đưa FFI vào module thư mục.

**Đây mới là chuyển một nhóm FFI sang machine ABI**; toàn bộ compiler,
runtime, linker, TLS và nhiều API còn cần chuyển sang `.vi` + ASM.

## Localtime/Timezone qua machine ABI (20/10/2026)

`gói/thời gian/địa phương.vi` không còn khai báo ngoại thư viện
`system.c`/`localtime_r`. Opcode **178** (`máy_đồng_hồ_địa_phương`) nhận
epoch có dấu ở dạng **8 byte little-endian**, trả về **64 byte** với các
trường `tm_sec`, `tm_min`, `tm_hour`, `tm_mday`, `tm_mon`, `tm_year`,
`tm_wday`, `tm_yday`, `tm_isdst` (i32 LE ở offset 0–32) và `tm_gmtoff`
(i64 LE ở offset 40). Runtime kiểm tra `system.time.local`, đúng 1 đối số,
đúng 8 byte và miền 0–255 cho từng byte; native AOT dùng chính executor này.
Các phép đổi Gregorian, offset giờ/phút, kiểm tra ngày và định dạng tiếp tục
ở V++. API `thời gian địa phương từ epoch byte ffi/posix` vẫn tương thích.

Trên macOS/Linux ARM64/x86-64, `scripts/ASM/vpp_localtime_primitives.S`
gọi `localtime_r` công khai của libSystem/libc. C++ adapter tạm thời đóng gói
`struct tm` thành wire layout ổn định, tránh phụ thuộc vào padding của host.
Trên Windows dùng CRT `localtime_s` qua C++ adapter, `tm_gmtoff` trả 0; V++
tính lại chênh lệch địa phương–UTC theo epoch gốc. **Đây chưa phải timezone
thuần V++/ASM độc lập libc/CRT**; để tự chủ hoàn toàn phải thay việc tra cứu
timezone của host bằng TZif/IANA parsing, xử lý TZ/DST và snapshot runtime
trong `.vi`, đồng thời bootstrap dữ liệu timezone. Windows/Linux runtime CI
chưa được xác nhận. macOS ARM64: native hardening, fixture DST interpreter/JIT
6/6, AOT capability isolation PASS; cross-assembly đủ 4 Unix ABI PASS.
Migration giảm foreign functions **38 → 37**, foreign libraries **9 → 8**;
audit có ratchet chặn quay lại `localtime_r` FFI trong `.vi`.

## Environment qua machine ABI (20/10/2026)

`gói/hệ thống/môi trường.vi` đã chuyển `getenv` FFI sang
`máy_môi_trường_đọc` (opcode **177**, quyền `system.env.read`). `.vi` tiếp
tục kiểm tra tên rỗng, dấu `=`/NUL và chọn giá trị mặc định; primitive nhận
một chuỗi tên, trả chuỗi hoặc `rỗng` khi không có biến. macOS/Linux ARM64/
x86-64 sử dụng Assembly bridge gọi **getenv của libc/libSystem** vì kernel
không có syscall getenv; Windows tạm dùng `GetEnvironmentVariableW` cùng
chuyển đổi UTF-8/UTF-16 trong C++ adapter. Không thể coi như đã độc lập libc;
kiến trúc tương lai cần chụp và quản lý environment từ process entry.

macOS ARM64: build PASS; hardening/capability isolation, native AOT,
interpreter/JIT fixture, 4 Unix ABI cross-assembly và regression machine
primitive PASS. Chưa có kết quả runtime Linux/Windows. Foreign declarations
giảm **39 → 38** và foreign libraries **10 → 9**.

## Secure Entropy qua machine ABI (20/10/2026)

`gói/lõi/nguồn ngẫu nhiên.vi` thay `getentropy` FFI bằng `máy_entropy_bytes`
(opcode **176**, quyền `system.entropy.read`). Primitive nhận i32 từ 1 đến
256 và trả list byte 0–255; `.vi` tự kiểm tra 1–4096 byte, chia khối <=256
và ghép lại. Linux ARM64/x86-64 dùng `SYS_getrandom` qua Assembly syscall,
xử lý partial read/EINTR; macOS dùng Assembly gọi `_getentropy` libSystem;
Windows dùng C++ adapter `BCryptGenRandom`. Đường macOS vẫn liên kết libSystem.

macOS ARM64: native hardening, interpreter/JIT fixture, AOT, cross-assembly
hai macOS ABI và regression đều PASS. Runtime Linux/Windows chưa xác nhận.
Foreign declarations giảm **40 → 39** và libraries **11 → 10**.

Tính đến trước migration Directory: **37 foreign functions, 8 foreign libraries**
trong `gói/*.vi`, **73 file C/C++** trong `src/` (tính cả `time_local.cpp`).
Executable macOS vẫn liên kết `libffi`, `libc++`, `libSystem`, Security và
CoreFoundation. Compiler, VM runtime, native AOT runtime, TLS và linker chưa
đạt kiến trúc tự chủ `.vi` + Assembly. Các phần mô tả bên dưới ghi lại những
đợt audit trước và có thể phản ánh baseline cũ hơn.

## Monotonic và realtime clocks qua machine ABI (20/10/2026)

Hai foreign bindings `clock_gettime` trong `gói/thời gian/đơn điệu.vi`
và `gói/thời gian/giờ quốc tế.vi` đã chuyển sang opcode 174–175:
`máy_đồng_hồ_đơn_điệu` (`system.time.monotonic`) và `máy_đồng_hồ_utc`
(`system.time.realtime`). Intrinsic trả 16 byte little-endian
`[signed i64 seconds, signed i64 nanoseconds]`; quy tắc decode, tính
milliseconds, epoch âm và chuyển Gregorian vẫn do `.vi` sở hữu. Các API
công khai, kể cả tên `ffi` cũ, giữ tương thích. Trên Linux ARM64/x86-64,
`SYS_clock_gettime` đi qua ASM syscall; trên Darwin ARM64/x86-64, ASM gọi
`_clock_gettime` public của libSystem vì SDK không xuất syscall tương ứng.
Windows còn adapter tạm thời dùng QPC và FILETIME. Mục này **chưa loại bỏ
libSystem, Windows C++ adapter hoặc runtime C++**. Foreign declarations
giảm 42 → 40; audit ratchet và hardening kiểm tra capability isolation,
arity, byte layout, tính đơn điệu, độ gần wall clock và native AOT.

## Sleep qua machine ABI (20/10/2026)

`gói/hệ thống/nền tảng.vi` đã bỏ ba foreign bindings `usleep`,
`last_errno`, `eintr`. Hàm `ngủ mili giây()` giữ nguyên API, chia đợt
tối đa 999 ms và xử lý `EINTR` bằng deadline của đồng hồ đơn điệu trong `.vi`.
Machine intrinsic `máy_ngủ_mili_giây` (opcode 173, capability
`system.time.sleep`) trả trực tiếp `0` hoặc mã lỗi syscall. ASM gọi
`poll(nfds=0, timeout_ms)` trên macOS ARM64/x86-64 và `nanosleep`
trên Linux ARM64/x86-64; Win32 tạm dùng `Sleep`. Các CPU POSIX chưa có
ASM tiếp tục adapter dự phòng, không thuộc kiến trúc tự chủ cuối cùng.
`EINTR=4` được xác nhận tại compile time trên POSIX. Không còn FFI
snapshot errno trong đường sleep. Ratchet foreign functions giảm 45 → 42.
Đã thêm kiểm thử machine sleep, quyền tách biệt, kiểm tra input và native
AOT dispatcher; cần thực thi Linux/Windows CI và xác minh signal EINTR trên
runner trước khi tuyên bố tương thích đầy đủ.

## Process identity qua machine ABI (20/10/2026)

Sáu API công khai `mã tiến trình`, `mã tiến trình cha`, `mã người dùng`,
`mã người dùng hiệu lực`, `mã nhóm` và `mã nhóm hiệu lực` trong
`gói/hệ thống/tiến trình.vi` đã đổi từ foreign descriptor sang opcode
`OP_VM_MACHINE_PROCESS_*` (167–172). V++ vẫn kiểm tra platform/giữ API;
dispatcher kiểm tra đúng từng capability `system.process.id`,
`system.process.parent_id`, `system.user.*`, `system.group.*` trước khi gọi
machine. PID/PPID trả i32, UID/GID lớn hơn INT32_MAX giữ giá trị bằng
`AbiInteger` unsigned. VM, worker/module VM và native AOT dùng cùng contract.

Trên macOS ARM64/x86-64 và Linux ARM64/x86-64, backend chuyển qua syscall
Assembly `vpp_machine_file_syscall4` dùng chung với tệp thô, không gọi hàm C
`getpid/getppid/getuid/geteuid/getgid/getegid`. Windows dùng Win32 API trong
adapter C++ tạm thời để lấy PID/PPID, giữ từ chối UID/GID; các kiến trúc
POSIX chưa có ASM verified vẫn dùng `syscall()` của libc. Đây là ranh giới
machine ở VM hiện tại, **chưa phải compiler/runtime thuần `.vi` + ASM**.

Kiểm kê nguồn trước đợt này có 51 khai báo `ngoại hàm` trong `gói/*.vi`;
sau khi chuyển sáu hàm còn **45**. Script
`scripts/quality/audit_vi_asm_boundary.sh` đặt ratchet 45, chặn tái sử dụng
binding cũ và liệt kê C/C++, symbol FFI và dependency động. Test
`vpp-process-identity-hardening` bao gồm grant tách biệt, arity, kiểu trả về,
native AOT dispatcher; fixture `kiem_tra_ffi_tien_trinh.vi` giữ 10 dòng `1`
trên interpreter/JIT macOS ARM64. Windows và Linux cần kết quả CI thật.
`foreign.cpp`, native AOT runtime C++, `libffi`, TLS provider và các binding
còn lại vẫn là việc chưa hoàn tất.

## Thay stdio bằng thư viện V++ (20/10/2026)

`gói/nhập xuất/bộ đệm tệp.vi` sở hữu buffering, EOF, partial write, retry
EINTR và cursor logic. API tệp và `Luồng Tệp` dùng chung implementation này,
không gọi stdio C. Sáu binding `system.file.raw.*` giữ descriptor OS trong
registry VM riêng; không dùng libffi. Primitive máy ở `file_machine.cpp`
dùng ASM syscall trên macOS/Linux ARM64/x86-64, Win32 API trên Windows và
adapter POSIX trên các kiến trúc khác. Xem [contract và kiểm chứng](stdio-vpp.md).
Backend stdio cũ chỉ còn phục vụ FFI tương thích; toàn toolchain vẫn còn
runtime C++, libffi cho nhóm FFI khác và TLS provider. Các mục bên dưới là
lịch sử audit trước thay đổi này.

## Kiểm kê self-host compiler (2026-10-12)

`src/include/vpp/bytecode/intrinsic.h` hiện có **51 descriptor opcode**, không
phải 51 hàm C++ cần xóa. Đối chiếu dispatcher `src/runtime/vm.cpp` và các
helper trong `src/runtime/native/` cho thấy:

| Phân nhóm | Số lượng | Đánh giá |
| --- | ---: | --- |
| Legacy OS/file/clock/process/DNS đã bị chặn trên mọi nền tảng | 21 | Giữ descriptor cho lỗi di chuyển và bytecode cũ tới khi khóa chính sách tương thích. |
| Legacy socket/TLS còn fallback Windows | 9 | Còn handler TCP/UDP rollback và Schannel; cần Windows CI và thay TLS trước khi xóa. |
| Compiler + chạy script nhúng | 2 | `bien_dich_phan_tich_vm`, `kich_ban_chay_vm` vẫn gọi `compilePipeline` C++; là mục tiêu của self-host compiler. |
| Worker/scheduler | 5 | Thread spawn/wait/cancel/status/park cần cơ chế VM và isolation native. |
| Biểu diễn collection/object | 9 | Identity, mutation, tuple, length, type, map/list cần substrate VM. |
| Byte/bit IEEE-754 | 4 | Raw string bytes và float bit conversion cần primitive biểu diễn dữ liệu. |
| Tên nền tảng | 1 | Có thể chuyển điều phối sang V++/FFI, vẫn phải gọi host OS. |

Tầng tự biên dịch đã có lexer ở `gói/biên dịch/từ vựng.vi`; bổ sung
`gói/biên dịch/biểu thức.vi` làm parser biểu thức V++ độc lập, tạo AST map/list
với kiểm tra token thừa, thiếu toán hạng và ngoặc, ưu tiên toán tử, lời gọi,
chỉ số. **Chưa nối parser này vào pipeline sản xuất**; bộ phân tích statement,
semantic/IR, phát bytecode và bootstrap stage-2/stage-3 vẫn ở C++. Đây là
bước chuẩn bị để thay compiler native theo từng pass, không phải chuyển mã máy
runtime hay FFI sang V++ thuần. Test tương đương và chạy trên Windows vẫn là
điều kiện trước khi xóa primitive. Cần thêm differential AST/diagnostics với
compiler C++ trước khi chuyển đường mặc định.

`gói/biên dịch/biểu thức.vi` biên dịch độc lập bằng CLI hiện tại. Lỗi
`unsupported direct IR region` trong fixture mới bắt nguồn từ việc dùng `gọi`
(từ khóa V++) làm tên biến. Sau khi đổi thành `kết_quả_gọi`, fixture chạy đủ
35/35 kiểm tra trên interpreter và JIT và đã được đưa vào `run_tests.sh`.
Một lỗi độc lập được tìm thấy khi dùng `nhân` làm biến: hàm nhập từ thư viện
cùng tên khiến phép đọc trả về ID hàm thay vì map vừa gán. Compiler nay báo
lỗi ngữ nghĩa rõ ràng khi gán vào binding hàm. Cho phép biến cùng tên che hàm
vẫn cần symbol-scoped VM slots và kiểm thử bootstrap trước khi triển khai.


**Đợt 2026-10-10 (TCP/UDP Windows):** API công khai `đầu nối giao vận tin cậy mở`,
`đầu nối giao vận gói tin mở`, `đầu nối giao vận tin cậy lắng nghe`, `đầu nối chấp nhận`, `đầu nối gửi`,
`đầu nối nhận`, `đầu nối đặt thời gian chờ` và `đầu nối đóng` nay mặc định điều phối
trong `gói/mạng/ổ cắm.vi` qua `system.net.socket.*` FFI trên Windows.
Adapter WinSock giữ `SOCKET` đúng độ rộng con trỏ trong registry từng VM,
chỉ đưa token `c_socket` cho `.vi`; kiểm tra capability/chữ ký, kích thước
đệm, giới hạn UDP và thu hồi handle khi đóng/reset. Đặt
`VPP_WINDOWS_SOCKET_FFI=0` để đối chiếu nhánh TCP/UDP C++ cũ. Suite Windows
đã bổ sung fixture FFI/legacy ở interpreter/JIT; **chưa thực thi CI Windows**,
nên chưa gỡ backend dự phòng. TLS Windows vẫn dùng Schannel legacy (gồm
socket TCP tạo theo đường legacy cho phiên TLS), trong khi TLS POSIX dùng
`.vi`/FFI với provider mật mã native. Việc này chuyển điều phối sang V++,
không loại bỏ WinSock, TLS provider hay runtime VM bắt buộc.
Token `c_socket` đã đóng hoặc khác VM trả `LanguageException` mà `thử/bắt lỗi`
của V++ có thể xử lý; descriptor/capability/ABI sai vẫn là lỗi VM.
`kiem_tra_ffi_socket_loi.vi` kiểm tra mười đường lỗi API, gồm token giả,
không cần mở socket hay quyền bind localhost trên interpreter và JIT.

**Đợt 2026-10-10 (DNS Windows):** đường public `phân giải tên miền phân giải` và
`đầu nối bộ phân giải danh sách` đã dùng **cùng thuật toán V++ qua ba binding
`system.net.resolve.*`** trên cả Windows và POSIX. Windows adapter tại
`foreign.cpp` khởi tạo Winsock, quản lý token `addrinfo*` theo VM, trả IP dạng
chuỗi và giải phóng khi đóng/reset; không trả con trỏ C cho `.vi`. Fixture
`kiem_tra_ffi_dns.vi` đã chạy đạt trên macOS interpreter/JIT; CI Windows
được bổ sung hai chế độ nhưng **chưa có kết quả thực thi Windows**. Hai handler
DNS C++ (`handleDns`, `socketResolveVm`) đã bị xóa ở Windows; hai opcode DNS
cũ bị chặn ở dispatcher trên mọi nền tảng, cả lời gọi tên legacy lẫn bytecode
trực tiếp. Descriptor vẫn tồn tại chỉ để trả diagnostic di chuyển rõ ràng.
Windows runner kiểm tra thêm từ chối cả hai opcode ở interpreter/JIT. TLS
Windows vẫn đi qua backend Schannel cũ. Chưa xác minh DNS Unicode,
IPv4/IPv6, lỗi resolver và cleanup trên Windows thật.

**Đợt 2026-10-10 (Windows filesystem):** `tạo thư mục`, `liệt kê thư mục`,
`xóa đường dẫn`, `đường dẫn tồn tại`, `là tệp`, `là thư mục` và kiểm tra
thư mục không theo liên kết nay **luôn điều phối bằng V++ qua FFI Win32**.
Đã xóa các handler filesystem C++ cuối cùng khỏi
`vm_native_stdlib_helpers.cpp`; bảy opcode tương thích cũ bị chặn bằng lỗi
CallBoundary trên Windows, kể cả khi gọi theo tên cũ. Hai biến
`VPP_WINDOWS_DIRECTORY_FFI`/`VPP_WINDOWS_PATH_FFI` không còn chọn backend.
Runner Windows kiểm tra public API, cờ cũ và lỗi opcode ở interpreter/JIT;
**chưa có kết quả chạy CI Windows được xác nhận**. Việc xóa fallback khi
chưa có Windows CI tạo rủi ro cần kiểm chứng Unicode, symlink và cleanup.
Đây là migration tầng thư viện; `CreateFileW`, `FindFirstFileW`,
`CreateDirectoryW` và quản lý handle vẫn là adapter native tối thiểu.

Đợt 2026-10-09 (process Windows): đường public `chạy tiến trình` trên
Windows nay luôn chạy thuật toán V++ qua `system.process.*` FFI; đã gỡ
`handleNativeProcessPrimitive`, bộ escape argv, dựng môi trường, đọc pipe
và `CreateProcessW` trùng lặp khỏi `vm_native_helpers.cpp` (401 → 170 dòng).
Opcode tiến trình legacy bị từ chối trên cả POSIX/Windows; chương trình
cần `nhập gói/hệ thống/tiến trình;` và gọi API công khai. Bộ adapter
Win32 trong `foreign.cpp` vẫn quản lý handle, pipe, process group/Job Object
và UTF-16 tại biên hệ điều hành. Đường FFI Windows **chưa được xác minh
trên máy Windows**; CI Windows interpreter/JIT, spawn và timeout là gate còn mở.

Đợt quét cập nhật ngày 2026-10-10: 95 tệp `.vi` trong `gói/`, 936 thân hàm V++;
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
helper C++ thật sự không còn caller. Riêng filesystem Windows đã gỡ helper
trước khi hoàn tất gate CI, nên vẫn phải kiểm tra lại trên máy Windows.
Windows directory binding mới gồm
`CreateDirectoryW`/`DeleteFileW`/`RemoveDirectoryW` và
`FindFirstFileW`/`FindNextFileW`/`FindClose`; toàn bộ thuật toán tạo nhiều cấp,
duyệt danh sách và xóa cây vẫn ở `gói/nhập xuất/thư mục.vi`.
Từ 2026-10-10 chỉ còn FFI; hai biến chuyển backend trước đây không còn tác
dụng. Fixture Windows thử cờ cũ vẫn phải chạy FFI, và kiểm tra interpreter/JIT.
CI Windows vẫn là điều kiện để xác nhận triển khai mới vận hành đúng.

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
`system.entropy.read`, giới hạn 1–256 byte/call. `gói/lõi/nguồn ngẫu nhiên.vi`
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
Trên Windows, mọi lời gọi đều dùng Win32 FFI. Bốn phép hỏi metadata (`tồn tại`, `là tệp`,
`là thư mục`, `là thư mục không theo liên kết`) sang wrapper V++ đọc mã loại
qua Win32 `system.fs.path.kind`; VM chỉ giữ handle tạm và chuyển lỗi Win32.
Descriptor opcode tương thích chỉ giữ để thông báo lỗi chuyển đổi; handler
filesystem C++ đã xóa. CI Windows vẫn cần xác nhận tính tương đương.
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
primitive tag/identity/platform của VM vẫn giữ lại. Windows filesystem và DNS
đã chuyển hoàn toàn sang V++/FFI; chỉ TCP/UDP rollback và Schannel Windows
còn dùng các socket handler C++ cũ.
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
| `chuyển thành danh sách` | C++ sao chép vector list/tuple | V++ kiểm tra kiểu, duyệt phần tử, tạo list mới bằng primitive `them` |
| `biểu thức chính quy khớp`, `biểu thức chính quy tìm` | C++ từng tham gia engine regex | Parser/matcher và policy full-match/search chạy bằng V++ |
| `biểu thức chính quy thay`, `biểu thức chính quy tách` | C++ từng tham gia thay/tách | V++ tự ghép capture, thay và tách; không còn regex hook C++ |
| `làm tròn xuống`, `làm tròn lên` | C++ gọi `std::floor` / `std::ceil` | V++ làm tròn số hữu hạn bằng phép toán IEEE-754, giữ nguyên kiểu int32/double của contract cũ |
| `phụ thuộc kiểm tra tệp` | C++ gọi lại compiler rồi dựng `{hợp lệ, lỗi}` | V++ lấy compiler snapshot, giữ chính xác shape kết quả public; chỉ compiler pipeline còn là primitive |
| HTTP server `Transfer-Encoding: chunked` | Từ chối request dù socket đã nhận được dữ liệu | V++ quét incrementally size line/chunk/trailer, giới hạn body và dựng request body trước khi route |
| `tạo thư mục`, `xóa đường dẫn` | C++ dùng `create_directories`/`remove_all` | V++ duyệt cây bằng stack; primitive chỉ tạo/xóa một mục và đọc trạng thái không theo symlink |
| DB client | C++ chọn client, ghép command và diễn giải kết quả | V++ dựng argv/environment, chọn client dự phòng và ánh xạ kết quả; VM chỉ spawn/read/wait process |
| `đầu nối gửi` | C++ lặp gửi hết stream | V++ giữ vòng gửi hết với byte offset và quy tắc một datagram UDP; primitive chỉ gửi một lần |
| `%f`, `%e`, `%g` | VM định dạng bằng `ostringstream` | V++ giải mã bit binary64, dựng thập phân chính xác, làm tròn ties-to-even và chọn bố cục; VM chỉ trả 8 byte biểu diễn số |

Helper HTTP `splitPathAndQuery` không còn nơi gọi cũng được gỡ khỏi runtime,
header và danh sách nguồn CMake; logic giao thức đã nằm trong gói HTTP V++.

Các tên `lấy thời gian hiện tại`, `lấy giờ quốc tế`, `độ lệch múi giờ`,
`chuyển thành danh sách` được giữ dưới dạng hàm V++ trong gói tương ứng. Chúng không còn
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
| File, thư mục, môi trường | `io_doc_file_vm`, `io_doc_bytes_vm`, `io_ghi_file_vm`, `duong_dan_ton_tai_vm`, `tao_thu_muc_vm`, `doc_bien_moi_truong_vm`, `ten_nen_tang_vm` | File, env, filesystem đều dùng V++/FFI trên POSIX/Windows; opcode C++ filesystem và file cũ bị chặn ở cả hai OS. Chỉ giữ `ten_nen_tang_vm` cho VM identity. |
| Clock, thread | `dong_ho_dia_phuong_vm`, `dong_ho_utc_vm`, `thoi_gian_don_dieu_ms_vm`, `ngu_mili_giay_vm`, `thread_vm_spawn/wait/cancel/status/park` | Local, UTC, monotonic và sleep đều dùng V++/FFI cả POSIX/Windows, đã gỡ các handler C++ tương ứng; worker vẫn ở VM. |
| Socket, TLS, DNS | `socket_*_vm`, `dns_phan_giai_vm` | Resolver public POSIX/Windows chạy V++ với `system.net.resolve.*` và token VM; TCP/UDP trên hai nền tảng đều mặc định do V++ điều phối qua socket FFI. Windows giữ TCP/UDP legacy với `VPP_WINDOWS_SOCKET_FFI=0` đến khi CI thực tế đạt; TLS Windows vẫn dùng Schannel/primitive cũ. TLS POSIX qua FFI, provider native SecureTransport/OpenSSL. HTTP, URL, header/body, DNS dedup/sort và protocol ở V++. |
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
  Windows process adapter/Schannel và worker vẫn cần native.
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
  `làm tròn lên`. Fixture strict POSIX interpreter/JIT chạy lại:
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

### Bổ sung boundary VM độc lập — Process, credential (28/10/2026)

VM standalone gọi `máy tiến trình định danh thô` qua ASM (PID/PPID và
UID/EUID/GID/EGID dạng u32) và `máy tiến trình thô` qua ASM cho
spawn/pipe/poll/read/wait/close. V++ sở hữu token, argv, environment merge,
truyền byte và hạn chót; ASM sở hữu `posix_spawnp` và descriptor POSIX.
Giao diện `193–202` và native compiler primitive ID 73 giữ ABI ổn định.
Fixture `vm_asm_process.vi` chạy trên **macOS ARM64**, bao gồm hai pipe
16.800 byte, Unicode argv, child-only env override, lỗi start và deadline.
Lượt `vpp-vm-asm` sau sửa harness **3/3 PASS**; các kiểm tra socket machine
và native V++ runtime có gate riêng.

Ranh giới chưa chuyển: `src/runtime/foreign.cpp` vẫn phục vụ oracle Stage 0
và Win32; socket, resolver và TLS của standalone VM chưa có provider ASM.
Đường socket POSIX trong Stage 0 vẫn dựa vào lớp `socket_machine` C++/
FFI. TLS vẫn nằm ở crypto provider OS/OpenSSL/SecureTransport với xác thực
chứng chỉ và hostname. Không được dùng trạng thái của Stage 0 để tuyên bố
VM độc lập đã có TCP/UDP/TLS. ARM64 Linux mới qua kiểm tra assembly;
x86-64 process đã có ASM với harness riêng. Native runtime C `vpp_aot_runtime.c` còn
đường fallback và hàm linker tổng quát; `VPP_NATIVE_VPP_RUNTIME` có
`vpp_native_link_spawn_wait` bằng V++/ASM cho nhánh liên kết riêng.
Di trú runtime AOT toàn diện cần xác minh từng caller cùng parity đầu ra,
quyền, lỗi, resource lifetime và các target Linux/Windows.

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
## Socket deadline độc lập VM và kiểm kê AOT (28/10/2026)

Socket VM opcode 185–192 hiện do V++ quản lý token và ASM làm OS boundary.
Đã bổ sung deadline `connect` với O_NONBLOCK/poll + SO_ERROR, timeout
`accept` với poll, và send/recv timeout cho socket mới được accept.
`connect`/`accept` dùng hạn chót `CLOCK_MONOTONIC` và thử lại `poll`
khi gặp EINTR, giới hạn tổng thời gian chờ qua nhiều tín hiệu. Harness
ARM64 và x86-64 gửi SIGALRM trong accept, xác nhận ETIMEDOUT thay vì
đẩy EINTR về VM và không làm mất descriptor listener.
Trước khi trả FD sau connect, ASM khôi phục trạng thái blocking ban đầu;
timeout accept giữ nguyên listener. Harness macOS ARM64/x86-64
đã PASS trong môi trường được cấp quyền bind loopback. Chưa xác minh
deadline blackhole và kết quả thực thi Linux.

`vpp-native-vpp-runtime` và `vpp-aot-platform-primitives-asm` đạt
3/3 CTest (gồm Stage 1 bootstrap). Đường VPP_NATIVE_VPP_RUNTIME đã
chuyển các helper quản lý byte/đường dẫn và spawn/wait sang V++/ASM,
và file `src/runtime/native/vpp_aot_runtime.c` bị loại hoàn toàn khỏi
binary khi macro này được chọn (`#if !defined(VPP_NATIVE_VPP_RUNTIME)`).
Nhánh khác vẫn giữ runtime/linker fallback C; phụ thuộc Stage 0 và OS
linker bên ngoài chưa được loại bỏ. TLS của VM độc lập chưa
có provider xác minh CA/hostname/SNI; opcode 206–208 tiếp tục bị từ
chối rõ ràng cho tới khi có thể kiểm thử handshake thực tế.
