# V++ compiler self-hosting: parser, semantic, IR và bytecode bootstrap

## Sửa hồi quy integration và công cụ Windows (30/10/2026)

Hai log CI cho thấy integration đang chọn native AOT sau khi compiler V++
được bootstrap, trong khi golden chẩn đoán và các cờ JIT/GC thuộc interpreter.
`run_tests.sh` và runner PowerShell nay chọn `run --compiler=cpp` tường minh;
batch worker có lựa chọn `--compiler=cpp|vpp`. CLI chạy nguồn thông thường
vẫn ưu tiên compiler V++, không thay mặc định hoặc thêm fallback tự động.
Các gate native compiler/AOT vẫn chạy riêng. Test `vpp-integration-backend`
đặt đường native compiler không tồn tại để xác nhận interpreter được chọn
đúng, giữ UTF-8/stack trace và không thay hành vi mặc định.

Resolver nguồn V++ đã sửa trường hợp tìm import lên gốc ổ đĩa rồi nối
`C:/` với `/import.vi`, tạo `C://import.vi` và làm parser đường tương đối
báo lỗi. Bộ chuẩn hóa tiêu thụ dấu phân cách dư sau root, nhận diện đường
drive/UNC khi kiểm thử trên POSIX và không đọc chuỗi vượt biên ở root.
Fixture `native_source_paths.vi` kiểm tra 10 trường hợp drive/UNC/POSIX,
bao gồm dấu phân cách Windows và tên tiếng Việt.

Formatter C++ oracle và V++ không giữ CR cuối line comment; block comment
được chuẩn hóa xuống dòng mà không sửa nội dung string literal. Gate
formatter mở rộng thành 11 fixture, thêm CRLF và comment ở EOF. LSP
Windows mở stdin/stdout ở binary mode để Content-Length đo đúng byte,
và host bỏ chuyển đổi CRLF của stdout formatter trước khi tạo JSON.

Kiểm chứng trên macOS: integration **1/1 PASS (140,17 giây)**; formatter/
LSP cùng bootstrap **4/4 PASS (7,36 giây)**; lint/backend-selection cùng
bootstrap **3/3 PASS (9,23 giây)**; backend-selection/quantum/AOT parity cùng bootstrap
**4/4 PASS (67,90 giây)**. Các bộ đếm này là các lượt lọc riêng, có dependency
trùng nhau. Quantum SDK tùy chọn vẫn SKIP khi chưa cài.

Lượt kiểm chứng bổ sung đúng tám nhóm thất bại trong log Windows, cùng
bootstrap và backend-selection, đạt **10/10 PASS trên macOS (135,47 giây)**:
directory, entropy, formatter, lint, hai ca LSP, quantum và integration.
Log ở `build-ci-regression-fix/windows-regression-recheck.log`. Hai harness
Python `check_quantum.py` và `check_integration_backend.py` nay đọc stdout/
stderr bằng UTF-8 tường minh, tránh phụ thuộc code page Windows. Trước sửa,
cả hai tái hiện `UnicodeDecodeError` dưới locale ASCII; sau sửa, backend
PASS và quantum đạt **5 PASS, 5 SKIP** (SDK tùy chọn), với `LC_ALL=C`,
`PYTHONUTF8=0`, `PYTHONCOERCECLOCALE=0`. Đây là kiểm chứng tính độc lập
với locale trên macOS, chưa thay thế chạy Windows thực tế.

Ubuntu linux/amd64 trong Docker trên engine ARM64: VM/ASM và công cụ
**7/7 PASS (377,69 giây)** với `--parallel 2`; log ở
`build-linux-x86-validation/ci-repair-ctest.log`. Với libffi được bật như CI,
toàn bộ integration đạt **357 PASS, 0 FAIL**, gồm interpreter, JIT, GC và
HTTP loopback; log ở `build-linux-x86-validation/ci-repair-integration.log`.
Lint/backend-selection cùng bootstrap kiểm chứng thêm **3/3 PASS (15,61 giây)**.
CI Linux cài SQLite CLI tường minh vì fixture CRUD dùng chương trình sqlite3,
không chỉ thư viện SQLite. Chưa tái hiện được lỗi
VM/ASM trong đoạn log CI bị thiếu phần chẩn đoán. Workflow Linux/Windows
nay giữ LastTest.log/LastTestsFailed.log khi thất bại. Chưa chạy MSVC/CRT
trên Windows thực tế trong lượt này; các trường hợp đường Windows và CRLF
đã chạy như dữ liệu trên macOS/Linux, LSP Windows cần xác nhận lại ở CI.

## Stage 2 → Stage 3 qua VM/ASM đã hội tụ (28/10/2026)

Compiler đầy đủ đã tự biên dịch qua VM V++/ASM thành **Stage 2 và Stage 3**.
Ảnh Stage 1, Stage 2 và Stage 3 đều dài **857.938 byte** và khớp từng byte:

```text
8235556272902985abff7be472bbfea7fceab8f4772d8938a944fa0987ff6923
```

Lượt chạy có profiler trên macOS ARM64: Stage 2 **588,51 giây**, Stage 3
**605,99 giây**, mỗi bước quan sát 363 triệu guest instruction. Stage 3
tiếp tục biên dịch một chương trình riêng và VM thực thi kết quả, in
`STAGE 3 VM OK`. Bằng chứng tại
`build-ci-regression-fix/vm-asm-selfhost/verification.json`, hai tệp
`profile-stage{2,3}-bounded-writer.{json,log}` và các ảnh `compiler-stage*.vmb`.
Đây là lượt kiểm chứng trực tiếp qua profiler và phép so byte; không gọi
kết quả này là một lượt CTest selfhost đã chạy. CTest tương ứng đã được
nâng timeout từng stage lên 900 giây và kiểm tra thêm chương trình do Stage 3 tạo.

Sửa chặn bootstrap nằm ở `gói/biên dịch/máy ảo ASM.vi`: bộ ghi hiện tuần tự
hóa vào khối khoảng 16 KiB thay vì giữ cả ảnh dưới dạng list byte rồi tạo
list khối thứ hai cùng tồn tại với IR. API mã hóa vào bộ nhớ vẫn còn để
giữ tương thích. Lỗi `error=1` và ảnh 448 KiB ở các mục cũ là **kết quả
trước sửa**, không phải trạng thái hiện tại.

VM hiện chạy resolver hostname bằng thư viện DNS/hosts V++, qua ảnh
`dns-provider.vmb` cùng thư mục executable hoặc `VPP_VM_DNS_PROVIDER`.
Không gọi libc resolver/C++ callback. HKDF-SHA256, ChaCha20-Poly1305 và
codec bản ghi TLS đã có mã V++ và vector kiểm thử; handshake/X.509/CA/SNI
vẫn còn thiếu, nên opcode TLS 206–208 vẫn bị từ chối. Xem `docs/tls-vpp.md`.

Regression: macOS **4/4 PASS (267,45 giây)**; Ubuntu 24.04 linux/amd64
trong Docker trên engine aarch64 (có lớp mô phỏng CPU) **5/5 PASS
(381,00 giây)**, gồm native ELF, runtime AOT, CLI bootstrap và VM/ASM.
Process/socket harness Linux ARM64 cũng đã chạy PASS. Đây chưa phải
kiểm chứng toàn bộ VM/compiler trên Linux ARM64 hay Windows.

Các mục bên dưới giữ lịch sử các mốc kiểm chứng trước đó.

## Mốc trước: VM process/UID-GID/socket và biên AOT (28/10/2026)

VM độc lập đã có process spawn/pipe/wait, credential u32 và socket
TCP/UDP IPv4/IPv6 bằng V++/ASM. Native intrinsic mới ID 74 bổ sung
socket boundary; compiler/object writer cập nhật tổng thành 75 ID.
Sửa ASM x86-64 căn stack tại lời gọi libc lồng nhau, số AF_INET6 trên
Darwin và đăng ký token trước/sau cấp phát để không đóng trùng FD.

MacOS ARM64 và x86-64/Rosetta: harness socket và guest VM đã từng **PASS**,
gồm 40 vòng không rò FD. Ở lượt CTest cuối ngày 28/10,
`vpp-native-stage1-bootstrap`, `vpp-native-vpp-runtime`,
`vpp-native-cli-compiler-bootstrap`, `vpp-vm-asm` **4/4 PASS**
(83,48 giây sau cập nhật resolver). Process harness hai kiến trúc và fixture UTF-8/NUL/env
không hợp lệ đều PASS; socket guest lỗi đối số/token và TLS fail-closed
PASS nhưng hai harness socket cần `bind` được **SKIP** do sandbox.
Lớp đăng ký socket chỉ kích hoạt token sau khi công bố tài nguyên thành
công, tránh đóng FD lần hai ở cleanup khi cấp phát thất bại.
Script `check_aot_primitives.sh` cross-assemble socket và tập
primitive AOT macOS/Linux hai kiến trúc (cùng ABI 0/1) **PASS**.
Kiểm chứng trên Linux ở mức assembly/object, chưa chạy syscall thật.

Runtime V++/ASM vẫn phụ thuộc OS linker và một số công cụ Stage 0;
`vpp_aot_runtime.c` được giữ cho fallback khi không chọn
`VPP_NATIVE_VPP_RUNTIME`. Resolver 203–205 đã có phần địa chỉ IPv4/IPv6
dạng số, kiểm tra NUL/UTF-8, opcode và vòng đời token; hostname DNS
và verified TLS 206–208 chưa có provider trên VM độc lập.
`vm_process_new` chỉ công bố token sau khi đăng ký tài nguyên thành công.
Kiểm toán fixture native AOT hiện dùng
`nm -g` cho cả symbol được định nghĩa và `otool -L` trên macOS để phát
hiện thư viện C++/FFI; trên Linux thiếu `readelf`/`llvm-readelf` là lỗi
kiểm toán thay vì bỏ qua. Chưa chạy thực tế trên Linux; không suy ra
mọi executable đều độc lập C++.
Stage 2→3 **trên VM/ASM** vẫn chưa chứng minh (bootstrap native AOT là
phép kiểm chứng riêng).

## Bổ sung thư mục và PID/PPID trên VM độc lập (28/10/2026)

Opcode 167/168 và 179..183 nay đi qua policy V++ và ASM trực tiếp,
gồm tạo/xóa/mở/đọc/đóng thư mục và lấy PID/PPID. ABI `mkdir` được sửa
về list ba trường và quyền theo umask. Runtime giữ token theo owner,
đóng tài nguyên còn sống khi thoát/lỗi và kiểm tra bản ghi/UTF-8 bằng V++.
Compiler i32 thêm hai ABI riêng 71/72; bảng symbol/relocation có 73 mục.

**10/10 CTest liên quan PASS** trên macOS ARM64, cùng kiểm tra parser
getdents Linux bằng dữ liệu giả lập, ASM bốn tổ hợp CPU/OS và ELF.
Chi tiết, log và giới hạn thực thi Linux ở `docs/vm-asm.md`.
Đây là trạng thái tại mốc kiểm chứng thư mục trước khi bổ sung process,
credential u32 và socket. Stage 2/3 qua VM/ASM vẫn chưa được xác nhận.

## Bổ sung host hệ thống thuần V++/ASM (28/10/2026)

VM độc lập đã nhận opcode ngủ, đồng hồ đơn điệu, UTC và entropy (173..176)
qua policy V++ và primitive ASM trực tiếp. Trước thay đổi, fixture hệ thống
dừng ở `error=22 opcode=174`; cùng fixture nay chạy được với `PATH` rỗng.
Lượt CTest liên quan đạt **7/7 PASS**, gồm đối chiếu VM C++, đối số sai,
GC, bounds và native bootstrap. ABI riêng của compiler i32 thêm ba mục
68..70; opcode/ảnh guest không đổi. Chi tiết ở `docs/vm-asm.md`.

Đây là mở rộng chức năng VM, chưa phải kết quả tự biên dịch compiler
Stage 2/3 và chưa thay toàn bộ runtime AOT C++.

## Sửa bootstrap x86-64 và kiểm thử CI (28/10/2026)

Lỗi `biên dịch ngữ nghĩa: trùng khai báo 'jump'` khi dựng Stage 1
trên Linux x86-64 xuất phát từ nhánh phát đối số stack của opcode CALL.
Đoạn này phải ghi ModR/M `0x84` cho `mov [rsp+disp32], eax`; logic chọn
JE/JNE chỉ thuộc nhánh nhảy điều kiện. Khôi phục byte MOV cũng loại bỏ
khai báo `jump` thừa, giữ nguyên kiểm tra khai báo trùng của frontend.

Harness ELF x86-64 kiểm tra cả hai lần ghi đối số thứ 7 và 8 lên stack,
với displacement 0 và 8, trên mọi host. Trên Linux x86-64, ca kiểm thử
tiếp tục liên kết và thực thi lời gọi tám tham số qua relocation PLT32.
Trên macOS ARM64, harness kiểm tra byte và cấu trúc ELF, không thực thi
mã máy x86-64.

Các mô-đun và fixture dùng helper từ điển, chuyển kiểu, mã máy nền và
toán học phải nhập trực tiếp mô-đun sở hữu helper. Không dựa vào import
gián tiếp tình cờ có trong namespace của Stage 0. Kiểm thử bảo toàn tệp
đầu ra khi build thất bại dùng biểu thức sai cú pháp cho cả Stage 0 và
Stage 1; phép chia không còn là ví dụ cú pháp chưa được hỗ trợ.
Parser lời gọi tường minh dùng cùng quy tắc tên với khai báo hàm,
cho phép `gọi in lời chào();`; fixture hiện có cho kết quả `5`,
`xin chào`, `6` đúng như Stage 0. Các fixture REST dùng đường dẫn
hiện tại `gói/mạng/http/dịch vụ tài nguyên`.

Kiểm chứng tại `build-ci-regression-fix/` trên macOS ARM64, Release:

- `vpp-native-stage1-bootstrap` và `vpp-native-elf-x86-writer`: **2/2 PASS**.
- Harness ELF qua đường Stage 0 không dùng compiler Stage 1: **PASS**;
  trước sửa tái hiện đúng lỗi `jump` trong log Linux.
- `checknativeprogram.cmake` qua Stage 0 và Stage 1: **PASS**, gồm 128
  đối số, lỗi cú pháp và bảo toàn executable đã có.
- DNS hosts integration, raw machine V++ parity và quantum bridge:
  **PASS**; các ca Qiskit tùy chọn bỏ qua khi chưa cài SDK.
- Lượt CTest cuối: **11/11 PASS**, 143,11 giây, gồm cả bootstrap CLI,
  AST/IR, OOP và library visibility. Log ở
  `build-ci-regression-fix/validation.log`; `git diff --check` sạch.

Lượt integration mở rộng chưa PASS: sau khi HTTP fixture khởi động
được, còn gặp `thread_vm_spawn` chưa được AOT hỗ trợ, lỗi arity của
thư viện nhật ký và một số phụ thuộc chưa được nhập trực tiếp. Kết
quả quan sát trước khi dừng lượt chạy nằm tại
`build-ci-regression-fix/integration-observations/partial-run.json`;
đây không phải kết quả của toàn bộ suite.

Stage 2/3 qua VM/ASM vẫn chưa được xác nhận. Lượt đo trước đó ở
`build/vm-asm-optimized/profile-stage2-direct-write.json` kết thúc
với `exit:1` sau 564,38 giây và VM báo `error=1`. Tệp Stage 2 mới
ghi được 448 KiB là ảnh dở dang, không được dùng làm compiler cho
Stage 3 hay bằng chứng hội tụ.

## Tối ưu VM/runtime thuần V++ (27/10/2026)

`gói/máy ảo/bộ nhớ.vi` dùng stack đánh dấu liên kết ngay trong header
heap cho mark/sweep: mỗi block còn sống được duyệt một lần, rồi toàn
bộ heap được quét một lần để thu hồi. Cách này thay thế việc quét lặp
toàn heap theo từng lớp tham chiếu; không tạo thêm đối tượng guest khi GC.
`gói/máy ảo/giá trị.vi` bổ sung tra cứu ô nhớ với khóa i32 trực tiếp,
được `runtime.vi` và `thực thi.vi` dùng để tránh cấp phát `vm_int`
tạm thời cho mỗi lần LOAD biến. Binary-search trên scope có khóa i32
vẫn giữ thứ tự và cách xử lý khóa cũ.

Kiểm thử `scripts/quality/check_vm_asm.cmake` chạy được cả đồ thị sống
12.000 lớp tham chiếu, kiểm thử GC chu trình, closure, OOP, exception,
ABI host và giới hạn phụ thuộc C++; toàn bộ PASS. Compiler Stage 1
`.vmb` chạy trên VM V++/ASM tạo ảnh fixture trùng byte với native
compiler, SHA-256 `a162af05c9c7464cf0a3e048a880e7a219188851b06992b5ba8114f6e4692498`.
Thử tái biên dịch compiler đầy đủ thành Stage 2 bằng VM trước tối ưu
vượt thời gian giới hạn 360 giây; Stage 2/3 chưa được xác nhận.

## Profiler và cải thiện hiệu năng VM/ASM (27/10/2026)

VM độc lập có profiler nội bộ, **mặc định tắt**. Đặt `VPP_VM_PROFILE=1`
để ghi một dòng stderr sau mỗi 250.000 guest instruction:

```text
vm-profile: steps=250000 function=136 pc=480 allocations=... gc=... free-list-probes=...
```

`steps` là số lệnh **trong khoảng lấy mẫu**, không phải tổng cộng;
`function` và `pc` là vị trí guest đang chạy ở ranh giới lấy mẫu, không
phải CPU sampling profiler. `allocations`, `gc` và `free-list-probes`
là bộ đếm cộng dồn từ lúc bật profiler. Profiler giúp khoanh vùng nhưng
không đo trực tiếp tỷ lệ thời gian CPU của từng hàm.

Để chạy phép đo có giới hạn thời gian, lưu log cùng báo cáo JSON:

```sh
python3 scripts/quality/profile_vm_asm_stage2.py --seconds 45 --name local
```

Script mặc định dùng `build/vm-asm-optimized/vpp-vm`, ảnh
`build/vm-asm-selfhost/compiler-stage1.vmb` và nguồn
`gói/biên dịch/dòng lệnh VM.vi`; hỗ trợ `--vm`, `--seed`, `--source`,
`--output-dir` để thay đổi đầu vào. Mọi phép đo dưới đây dùng cùng ảnh
Stage 1 và máy macOS ARM64, thời gian quan sát khoảng 45 giây. Các số
liệu so sánh là một lượt đo cục bộ cho từng phiên bản, chưa phải benchmark
đa lượt với sai số thống kê.

| Phiên bản VM | Guest instructions đã quan sát | Cấp phát | GC | Lượt dò free-list | Stage 2 trong 45 giây |
| --- | ---: | ---: | ---: | ---: | --- |
| Trước cache UTF-8 | 29.750.000 | 19.839.107 | 720 | 106.229.833 | Chưa xong |
| UTF-8 length + sparse index | 43.500.000 | 13.241.534 | 548 | 133.222.165 | Chưa xong |
| Thêm cache ký tự ASCII | 43.500.000 | 13.108.464 | 544 | 128.390.227 | Chưa xong |
| Thêm 10 bucket free-list | 44.000.000 | 13.254.933 | 551 | 13.253.854 | Chưa xong |
| Fast path `OP_GOI` cho độ dài/thêm | 44.500.000 | 13.526.755 | 562 | 13.490.436 | Chưa xong |
| Thêm cache tên host trong StringPool | 44.750.000 | 13.673.098 | 568 | 13.677.586 | Chưa xong |

Tối ưu `vm_length()` ghi nhớ số codepoint; `vm_index()` trên chuỗi
>= 128 byte dùng checkpoint mỗi 64 codepoint và tái sử dụng chuỗi
ASCII một ký tự. Các chuỗi Unicode vẫn được truy cập theo **codepoint**,
không theo byte. `vm_alloc()` phân nhóm các khối trống theo kích thước
và `vm_collect()` gộp khối liền kề trước khi đưa về bucket. Cache số
nguyên -16..255, cache ASCII và chỉ mục chuỗi đều được GC bảo vệ.

Lệnh gọi host tĩnh `OP_GOI` tới `độ dài` và `thêm` (kể cả các
bí danh cùng primitive) xử lý trực tiếp trên guest stack khi đúng số
đối số. Các lời gọi khác giữ đường kiểm tra số đối số của `vm_host()`.
`vm_host_cached()` lưu mã primitive theo **StringPool index** trong
state slot 10, giải tên lần đầu rồi tái sử dụng. Bảng cache chứa số
nguyên thô (`scan=0`), được state giữ sống qua GC; slot 9 vẫn dành
cho mã lỗi I/O. Test chạy cả tên đầy đủ lẫn alias và lời gọi host
khác để kiểm tra cache phân giải đúng tên.

Sau các thay đổi, số lượt dò free-list tại mốc 45 giây giảm khoảng
**9,7 lần** so với phiên bản cache ASCII nhưng chưa phân bucket. Số
guest instruction xử lý trong cùng thời gian chỉ tăng khoảng **1%**:
nghẽn cấp phát đã giảm nhưng compiler vẫn dành nhiều lượt thực thi ở
lexer/Unicode (đặc biệt các function ID 136, 39, 129, 133). Cần tối ưu
tiếp theo số liệu; không suy diễn Stage 2 đã hội tụ từ throughput tăng.
Fast path và cache tên host tăng số guest instruction quan sát từ
44 lên 44,75 triệu trong một lần đo 45 giây (**khoảng 1,7%**), nhưng
chưa đủ để kết luận tăng tốc ổn định vì chưa có nhiều lượt đo.

Regression `scripts/quality/check_vm_asm.cmake` đã **PASS** sau khi
phân bucket: GC chu trình/đồ thị sống sâu, closure/OOP/exception,
host ABI và kiểm tra symbol C++, cùng test ASCII, UTF-8 2/3/4 byte,
ranh giới chỉ mục 63/64, giữ chuỗi sau áp lực cấp phát và lỗi
out-of-range `error=4`. Các số liệu và log profiler nằm trong
`build/vm-asm-optimized/profile-stage2-*.log` (thư mục build không
là bằng chứng Stage 2/3 hội tụ).

Sau cache host, regression VM/ASM vẫn **PASS**, bao gồm fixture host
cache mới. Compiler Stage 1 `.vmb` chạy trên VM/ASM hiện tại biên dịch
fixture `src/tests/fixtures/vm_asm.vi` byte-for-byte trùng với native
compiler: SHA-256
`a162af05c9c7464cf0a3e048a880e7a219188851b06992b5ba8114f6e4692498`;
ảnh sinh ra cũng chạy đúng. Stage 2/3 của **compiler đầy đủ** chưa
được xác nhận qua đường VM/ASM.

## Xem AST và IR bằng compiler V++ (27/10/2026)

`vpp --dump-ast <source.vi>` và `vpp --dump-ir <source.vi>` chuyển đến
compiler đã bootstrap khi có sẵn. Có thể chọn `--compiler=vpp` để bắt buộc
đường native hoặc `--compiler=cpp` để đối chiếu Stage 0; compiler native
nhận trực tiếp `dump-ast`/`dump-ir`. Frontend, duyệt cây và hạ IR nằm trong
`gói/biên dịch/hiển thị cây và IR.vi`; CLI C++ chỉ chuẩn hóa đường dẫn và
chạy compiler con. Bản dump Stage 1 hiển thị AST dạng map và stack IR của
V++ nên định dạng khác Stage 0; không yêu cầu trùng byte giữa hai thế hệ.

`dump-ir` ở cấp tệp giữ import thành metadata `(chưa liên kết)`; bước
liên kết nhiều mô-đun thuộc compiler đầy đủ. Dump không chạy `chính()`.
CTest `vpp-native-ast-ir` kiểm tra cả hai chế độ, lỗi cú pháp, lớp, vòng lặp,
lambda, Unicode, import metadata, nguồn không bị sửa và việc không thực thi.


## Xuất mã máy qua compiler V++ (27/10/2026)

Sau bootstrap, `vpp build --target ... --emit-raw` chuyển trực tiếp tới
compiler `.vi` cho các kiến trúc đã có encoder leaf. Các hàm i32 đơn giản
dùng `biên dịch máy hạ lá` và `biên dịch mã máy lá`, giữ byte ABI cũ; hàm
phức tạp trên AArch64/x86-64 dùng Machine IR tổng quát, từ chối raw có
relocation `CALL` chưa liên kết. Tệp đầu ra chỉ được ghi sau khi phát mã
thành công. `--emit-obj` và `--emit-obj-selfhost` trên AArch64/x86-64 cùng
gọi `tạo đối tượng hàm` trong compiler V++ và ghi object trực tiếp. Các
đường này không cần wrapper V++ do C++ sinh hoặc bridge `executeVppSource`.

`--emit-asm` dùng bộ in assembly leaf bằng V++, hoặc bộ in Machine IR trên
AArch64/x86-64 cho hàm phức tạp. Object của kiến trúc chưa có backend tương
ứng tiếp tục đi qua Stage 0. C++ vẫn điều phối lệnh, kiểm tra khóa dự án và thực
hiện ABI host; assembler/linker tạo executable vẫn do host gọi. Vì vậy
chuyển xuất raw/object sang V++ không đồng nghĩa đã loại bỏ toàn bộ C++
hoặc VM trong toolchain.

## Điều phối nguồn mặc định qua compiler V++ (27/10/2026)

`vpp run <source.vi>`, `vpp build <source.vi>` và `vpp <source.vi>` nay
tự chọn compiler V++ native đã bootstrap nếu tìm thấy qua
`VPP_NATIVE_COMPILER` hoặc `<VPP_HOME>/bin/vpp-native` (cũng hỗ trợ
`VPP_AOT_ROOT`). CLI trong build tree ưu tiên bản bootstrap chính
`native-cli/vpp-native`, rồi thử `native-compiler/vpp-native` từ CTest.
Nếu cây mã nguồn có đủ thư viện `.vi` và script liên kết ASM thì có thể
tự tìm compiler lẫn root toolchain từ vị trí executable mà không cần xuất
`VPP_HOME`/`VPP_AOT_ROOT`. Đường `run` sử dụng backend ASM V++ và host linker;
đường `build` xác thực và sinh bytecode bằng compiler V++, ghi artifact
tạm rồi xóa khi hoàn tất. `vpp build <source.vi> -o <output.vbc>` cũng
chuyển tới compiler V++ để giữ artifact. CLI C++ chỉ điều phối process
ở các đường này.

Khi chưa có compiler V++ native, Stage 0 tiếp tục dùng compiler C++/VM
để bootstrap. `--compiler=cpp` giữ đường cũ tường minh;
`--compiler=vpp` yêu cầu compiler native và báo lỗi nếu vắng mặt.
Nếu `VPP_NATIVE_COMPILER` được đặt nhưng trỏ tới tệp không tồn tại thì
CLI báo lỗi thay vì tự động chạy VM. Lint/format, package management và
các cầu nối OS còn cần migration.

`vpp test <file.vi|directory>` và `--batch-run-tests` nay thực thi từng tệp
qua compiler V++ native khi có sẵn; bộ điều phối file và phép so khớp
output vẫn ở C++. `vpp --repl`/`vpp repl` dùng `embed` của compiler native,
ghi snippet ra tệp tạm và truyền `--base` là cwd để nhập mô-đun tương đối
không sai thư mục. REPL mới chạy snippet V++ trực tiếp, không chèn cú pháp
`nhập "stdlib"` cũ (native parser không chấp nhận import dạng chuỗi);
`--repl --compiler=cpp` vẫn chạy REPL cũ khi cần tương thích. Với Stage 0
chưa có compiler hoặc root toolchain, CLI có thể dùng đường VM để bootstrap.
Chế độ batch nay gom stdout và stderr của compiler con vào tệp `.output`
để giữ diagnostic của ca kiểm thử âm tính, đồng thời ghi mã thoát riêng
vào `.status`. Đường `run` thường vẫn giữ stderr ở terminal; stdout trước
khi lỗi cũng được chuyển lên CLI thay vì bị bỏ.
Kiểm thử CLI bổ sung parity đường mặc định và shim phát dấu xác nhận
đã thực sự gọi native compiler (kể cả build, run, đường tệp trực tiếp).

## Backend AOT tổng quát và điều phối CLI bằng V++ (26/10/2026)

Compiler native từ Stage 1 dùng `gói/biên dịch/asm chương trình.vi` để hạ
ABI bytecode thành Machine IR, phát byte/relocation bằng encoder V++ và
tạo nguồn ASM. Backend giữ đường xử lý lỗi, nhánh quay ngược/safepoint,
switch, try/catch, closure, OOP, tham số mặc định và metadata FFI. Chuỗi
được quote theo byte UTF-8 với escape octal ba chữ số, giữ đúng NUL và độ
dài byte trong ABI. Danh sách 103 opcode intrinsic được gate ABI đối chiếu
với registry C++; không coi opcode ngoài danh sách là intrinsic hợp lệ.

`gói/biên dịch/dòng lệnh máy.vi` nay đọc nguồn/artifact, chọn pipeline
`build`/`run`/`artifact`/`embed`, kiểm tra wire, phát ASM và quyết định thao
tác host. `src/native/aot_driver.cpp` nhận yêu cầu đã chuẩn bị; C++ giữ kiểm
tra danh tính đường dẫn, tạo tệp tạm, gọi assembler/linker và chạy process.
API `compileAssembly` liên kết ASM do V++ cung cấp, không tạo lại Snapshot
hoặc gọi bộ phát ASM C++ trên các đường CLI này. Artifact được chạy ngay
trong compiler native cũng dùng backend V++. Không thêm opcode hay đổi ABI.

`gói/biên dịch/lắp ráp.vi` ghép các dòng byte theo cây cân bằng, và metadata
chương trình được ghi theo khối, tránh sao chép lại toàn bộ chuỗi ASM sau
mỗi byte/bản ghi khi biên dịch compiler lớn.

Kiểm chứng macOS ARM64:

- **9/9 CTest PASS** (132,04 giây): bootstrap CLI, opcode ABI, full AOT
  parity, object CLI, CLI/artifact/embed, closure, OOP, FFI và assembler.
- Oracle `native_aot_cpp_oracle.cpp` cung cấp cùng wire cho C++ và V++,
  bao phủ các helper, toàn bộ intrinsic, nhánh/exception, chuỗi có NUL,
  tham số mặc định, hàm vô danh và metadata FFI. Object sau assembler trùng
  byte; nhãn nhánh tạm C++ được chuyển sang tiền tố private của Darwin để
  không làm khác symbol table không cần thiết.
- `check_native_aot_bootstrap.cmake` PASS Stage 0 → 1 → 2 → 3, các probe
  native, FFI, package import/re-export, file/directory, TLS, socket lỗi,
  UTC, entropy, môi trường, đồng hồ và process spawn.
- SHA-256 Stage 2/3 trùng nhau:
  `f8880cde94086f25b942cad82d9ea2e152b09a945677a3bd70e470cfc406ec94`.
  Artifact và `verification.json` ở `build-p0-release/native-aot-vpp-bootstrap/`.
- Benchmark ba kernel qua Stage 3, 15 vòng, correctness PASS; cả ba object
  trùng byte với bản tối ưu trước khi chuyển backend AOT. Dữ liệu ở
  `build-benchmark/native-aot-vpp-20261026/`. Chuyển backend compiler không
  được diễn giải thành mức tăng tốc thực thi kernel.

**Phụ thuộc còn lại:** runtime giá trị động và host ABI vẫn viết bằng C++/ASM;
assembler/linker hệ thống vẫn được gọi. Bộ phát ASM C++ được giữ cho Stage 0,
oracle và callback artifact của executable thông thường chưa nhúng compiler
V++; symbol này vẫn có trong binary liên kết. CLI quản lý gói/công cụ cũ
cũng còn C++. Vì vậy đã chuyển backend và điều phối CLI ở Stage 1+, chưa
loại toàn bộ C++ khỏi binary hoặc quá trình bootstrap ban đầu.

## Bước trước: chính sách CLI và đường object khỏi bridge Stage 0 (26/10/2026)

`gói/biên dịch/dòng lệnh máy.vi` sở hữu việc phân tích/kiểm tra tham số
`build`, `run`, `artifact`, `embed`, trợ giúp và chế độ xuất object.
`gói/biên dịch/dòng lệnh bản địa.vi` nhập mô-đun này vào compiler native.
`src/native/aot_driver.cpp` chuyển argv thành danh sách rồi gọi parser V++;
host tiếp tục kiểm tra danh tính tệp (kể cả symbolic/hard link) và thực hiện
các thao tác ABI còn lại. Tùy chọn thiếu, lặp hoặc mâu thuẫn bị từ chối
trước khi mở đầu ra.

Compiler native đã bootstrap có thể nhận trực tiếp:

```sh
cmake --build build-p0-release --target vpp-native-cli -j 6
build-p0-release/native-cli/vpp-native build --target aarch64-apple-darwin \
  --emit-obj-selfhost vpp_cycle benchmark/native/kernels.vi /tmp/vpp_cycle.o
```

Đường này gọi `tạo đối tượng hàm` và ghi byte bằng V++ ngay trong process
compiler: không sinh wrapper nguồn, không gọi `executeVppSource`, không
truyền object dưới dạng bản ghi hex qua C++, không cần khởi chạy lại Stage 0.
Với compiler V++ được tìm thấy qua `VPP_NATIVE_COMPILER` hoặc
`<VPP_HOME>/bin/vpp-native`, CLI tương thích `vpp-cli build --emit-obj-selfhost`
chuyển tiếp lệnh trước khi phân tích target qua bridge. Lỗi của compiler
đã cấu hình được trả về, không tự chuyển về Stage 0. Nếu chưa có compiler
native, đường Stage 0 cũ vẫn phục vụ bootstrap.

Target CMake `vpp-native-cli` nay tạo compiler tổng quát cùng entrypoint
với CTest/bootstrap, thay vì cài CLI i32 subset dưới cùng tên.
`benchmark/native/run.sh` ưu tiên compiler native ở build mặc định nếu có,
và vẫn cho chọn executable qua `VPP_EXEC`.

Kiểm chứng macOS ARM64: bootstrap CLI, kiểm thử CLI/artifact/embed, closure/OOP
và đường object mới đều PASS (4/4 CTest; 73,27 giây). Kiểm thử object so
trùng byte 12/12 object (ba kernel trên AArch64 macOS/Linux và x86-64
Linux/Windows), chạy với `PATH` rỗng và bỏ các biến gốc toolchain. Kiểm thử
còn xác nhận CLI tương thích chuyển tiếp trước bridge, lỗi tham số giữ đầu
ra cũ và từ chối ghi đè nguồn qua đường trực tiếp/symbolic/hard link.

Compiler mới tự biên dịch Stage 1 → 2 → 3 thành công; SHA-256 Stage 2/3:
`476d976ea7d32d1de2cb0518999145142d313873a9c7272fd08843e2bd8a4c59`.
Stage 3 xuất object Cycle trùng byte với bản tối ưu trước khi chuyển CLI.
Kết quả ở `build-p0-release/native-cli-convergence/results.json`.

Đo riêng **thời gian xuất một object `vpp_cycle`**, năm lượt xen kẽ sau
một lượt warm-up mỗi đường: CLI/bridge Stage 0 cũ có median **5,295 giây**,
CLI V++ Stage 3 trực tiếp **0,02484 giây** (khoảng 213× trong phép đo cục bộ
này). Mọi lượt phải xuất object trùng byte. Đo này gồm khởi động process,
đọc nguồn và ghi object, không gồm bootstrap compiler, và không phải tốc độ
thực thi kernel. Dữ liệu ở `build-benchmark/native-cli-latency-20261026/results.json`.

Benchmark ba kernel qua CLI Stage 3, 15 vòng, correctness PASS; cả ba object
trùng byte với bản tối ưu trước khi chuyển CLI. Kết quả và phụ thuộc compiler/
harness được lưu riêng ở `build-benchmark/native-vpp-cli-20261026/`.

Phạm vi còn lại: `src/cli/main.cpp` vẫn chứa CLI cũ cho quản lý gói và
công cụ; `src/native/aot_driver.cpp`, bộ phát ASM `aot_assembly.cpp` và
runtime giá trị động vẫn dùng C++. Việc xuất object trực tiếp hỗ trợ tập
con i32 AArch64/x86-64 của `mã máy từ cây.vi`, chưa thay backend toàn ngôn
ngữ. Muốn bỏ C++ khỏi binary compiler phải chuyển tiếp backend tổng quát,
runtime và host ABI; muốn bootstrap không dùng mã nguồn C++ cần phân phối
một compiler V++ seed đã kiểm chứng. Đây chưa phải công cụ hoàn toàn thuần V++.

## ARM64 native: so sánh trực tiếp và giữ hằng trong thanh ghi (26/10/2026)

Bộ phát mã AArch64 bằng V++ (`gói/biên dịch/mã máy aarch64.vi`) và bộ
đối chiếu C++ (`src/native/aarch64_codegen.cpp`) hiện gộp chuỗi
`LoadSlot` / `PushConstant` / `Compare` thành so sánh trực tiếp: `CMP #imm12`
cho hằng không âm đến 4095, hoặc tải hằng lớn/âm vào W9. Với `ADD/SUB`
và hằng ngoài immediate, backend dùng trực tiếp thanh ghi slot nguồn,
tránh hai lượt sao chép qua evaluation stack.

Trong hàm lá có đủ thanh ghi, bộ phát mã xác định vùng vòng lặp từ cạnh
nhảy ngược và chọn tối đa một hằng lớn/âm được dùng ít nhất hai lần trong
vùng đó để tải vào W10 **một lần trước phần thân**. W10 được dành riêng,
không cấp cho slot; W9 vẫn phục vụ scratch. Hàm có `CALL` hoặc vượt ngân
sách thanh ghi dùng đường fallback. Đây là tối ưu cục bộ theo back-edge,
chưa có CFG dominance, liveness và register allocation tổng quát.

Kết quả benchmark macOS ARM64, 15 vòng, trung vị ns/lần gọi, correctness PASS:

| Workload | V++ selfhost trước direct compare | V++ sau hoisting | C++ `-O3` cùng lượt cuối |
| --- | ---: | ---: | ---: |
| Arithmetic | 0,911 | 0,845 | 0,848 |
| Cycle | 123,022 | 118,777 | 151,241 |
| Fibonacci | 15,530 | 14,944 | 13,849 |

Object Cycle giảm từ 64 xuống 56 byte sau hoisting (giảm 12,5%); `otool`
xác nhận tải hằng 65521 vào W10 trước vòng lặp. Ở lượt cuối, Cycle nhanh
hơn C++ `-O3` khoảng 21,5%, Fibonacci chậm hơn khoảng 7,9%. Các lượt đo
đều có nhiễu hệ thống; riêng Arithmetic dưới 1 ns/lần gọi cần được diễn
giải thận trọng. Dữ liệu thô và disassembly nằm ở
`build-benchmark/native-hoisted-20261026/`.

Sau cập nhật encoder, ba CTest `vpp-native-object-parity`,
`vpp-native-machine-vpp-parity`, `vpp-native-control-flow-host-link`
đều PASS (3/3; 480,48 giây). Bổ sung regression: fixture kiểm tra
`MOVZ W10, #65521` chỉ xuất hiện một lần; ba hàm `hoist_vong`,
`hoist_hai_vong`, `hoist_long_nhau` kiểm tra parity C++ oracle với
CLI V++ native trên ARM64 macOS/Linux và x86-64 Linux/Windows
(12/12 trường hợp mới ngoài 44/44 trường hợp cũ). Bài host-link còn
chạy thực tế các vòng lặp, nhánh `continue`, hằng âm/lớn, và hàm
`hoist_goi` có `CALL` để xác nhận fallback. Hai CTest bị ảnh hưởng bởi
các fixture mới PASS (2/2; 465,22 giây) trên macOS ARM64; chạy lại
`vpp-native-object-parity` sau đó cũng PASS (1/1; 54,35 giây).

`--emit-obj-selfhost` vẫn được CLI/bridge Stage 0 C++ gọi. Benchmark dùng
C++ harness và thư viện hệ thống; chỉ các kernel phát mã máy là chạy
native không qua VM bytecode. Vì vậy hiệu năng các kernel chưa thể đại diện
cho toàn ngôn ngữ, và công cụ chưa tự chủ hoàn toàn khỏi C++.

Hướng tối ưu tiếp theo có dữ kiện cụ thể là Fibonacci: disassembly của
`vpp_fibonacci.o` còn `mov w4, w5` và `mov w5, w7` ở mỗi vòng sau
`add w7, w4, w5`. Cần phân tích liveness và giá trị mang qua back-edge
để coalesce thanh ghi/copy propagation an toàn tại điểm nối CFG, sau đó
đo lại cùng input và C++ `-O3` trong một lượt đo. Các tối ưu nhánh/phi
phải được kiểm tra với vòng lặp lồng, `break`/`continue` và đường có `CALL`.

## ARM64 native: tối ưu immediate trước direct compare (26/10/2026)

Bộ phát mã máy ARM64 bằng V++ (`gói/biên dịch/mã máy aarch64.vi`) hiện
gộp compare/branch, dùng trực tiếp thanh ghi slot, gộp kết quả với StoreSlot
và phát `add/sub Wd, Wn, #imm12` cho hằng trong `[0, 4095]` khi CFG cho
phép. `src/native/aarch64_codegen.cpp` tiếp tục làm C++ oracle kiểm thử.
Regression kiểm tra các biên immediate `0`, `4095`, `-1`, `4096`. Ba CTest
`vpp-native-object-parity`, `vpp-native-machine-vpp-parity` và
`vpp-native-control-flow-host-link` cùng PASS (3/3, 384,76 giây).

Trên macOS ARM64, 15 vòng benchmark (median ns/lần gọi, correctness PASS):

| Workload | V++ 24/10 | V++ trước immediate | V++ immediate `--emit-obj` | V++ immediate selfhost | C++ `-O3` cùng lượt selfhost |
| --- | ---: | ---: | ---: | ---: | ---: |
| Arithmetic | 0,947 | 0,852 | 0,844 | 0,911 | 0,885 |
| Cycle | 184,143 | 122,588 | 120,255 | 123,022 | 154,702 |
| Fibonacci | 41,781 | 20,858 | 15,182 | 15,530 | 14,227 |

`otool` xác nhận Fibonacci dùng `add w6, w6, #1` và mã máy giảm từ
60 xuống 52 byte. Cả ba object ARM64 từ `--emit-obj` và
`--emit-obj-selfhost` trùng byte, với ba kernel selfhost biên dịch và chạy
PASS. Dữ liệu ở `benchmark/reports/native-2026-10-24/results.json` và
`build-benchmark/native-{direct-stores,immediate,immediate-selfhost}-20261026/results.json`.
Các lượt đo có nhiễu môi trường: cùng object nhưng các median vẫn chênh.

Phụ thuộc còn lại: `--emit-obj` vẫn đi qua C++ `compilePipeline`, CLI C++
và wrapper Stage 0 trong `src/cli/native_vpp_codegen.cpp`;
`--emit-obj-selfhost` dùng compiler nguồn V++ nhưng vẫn được gọi từ
CLI/bridge Stage 0 C++. Mã máy kernel khi chạy không sử dụng bytecode VM.
Executable benchmark liên kết C++ harness cùng `libc++` và `libSystem`, không
phải executable compiler V++ độc lập. Bước tối ưu tiếp theo nên tập trung
loại MOV/hằng tải lại trong vòng `cycle`, kết hợp liveness, copy propagation
và constant hoisting; để bỏ phụ thuộc toolchain cần hoàn tất bootstrap native
nhiều thế hệ và tách CLI/linker khỏi Stage 0.

## Hàm ẩn danh qua compiler tự chủ (26/10/2026)

Parser chương trình V++ đọc được `hàm(tham số) { ... }` trong biểu thức.
Phân giải IR giữ cùng SymbolId cho ô biến được bắt giữ, truyền tiếp biến
qua closure lồng nhau và tách biến cục bộ, tham số, biến catch cùng tên.
Closure trong phương thức bắt được receiver `mình`; thân closure có khung
điều khiển riêng để `thoát`/`bỏ qua` không trỏ tới vòng lặp ngoài.

Bộ phát V++ tạo hàm không có tên runtime (`nameIndex = -1`) và phát
`OP_TAO_DONG_BAO` với danh sách ô biến. Hàm không bắt biến dùng function ID
trực tiếp. Linker một đơn vị, linker mô-đun và artifact giữ các hàm này mà
không tạo tên xuất hay `OP_HAM`. Kiểm tra ABI của Stage 0 được đồng bộ để
nhận giá trị `-1` đã được định dạng artifact cho phép; phần phân tích,
phân giải biến và phát closure nằm trong thư viện V++.

Kiểm chứng macOS ARM64: compiler tổng quát tự biên dịch Stage 1 → 2 → 3,
hai executable Stage 2/3 có cùng SHA256
`ecf6632d6ee2d48223d0a0aa734707c240efb3a33c0f1be09ca75246bbe47059`.
Stage 3 biên dịch và chạy đạt các fixture bắt biến, phạm vi, mô-đun nhập
và OOP; đồng thời từ chối closure dùng vòng lặp ngoài và giữ nguyên đầu ra
cũ. CTest `vpp-native-anonymous-functions`, kiểm thử CLI/artifact, OOP,
FFI và kiểm thử tích hợp đều đạt. Bài V++ `kiem_tra_ham_an_danh_tu_chu.vi`
còn kiểm tra vòng tuần tự hóa/giải mã artifact và các lỗi ngữ nghĩa.

## Tên API và nhập mô-đun (26/10/2026)

API sinh mã dùng các tên `Bộ Hạ Cây`, `sinh mã hàm`, `tạo đối tượng hàm`,
`tạo đối tượng chương trình` và `Bộ Sinh Mã Máy`. Tên symbol ABI
`vpp_native_*` và đường dẫn mô-đun được giữ nguyên. Hai hàm CLI i32 đổi
thành `hiện trợ giúp` và `hiện phiên bản`: từ khóa `in` ở đầu tên cũ làm
lexer i32 tách sai định danh, khiến compiler tự biên dịch trả mã 14.

Trong nhánh i32, `bản địa/bộ biên dịch.vi` tập hợp các khai báo nhập;
trình điều khiển và bộ sinh mã dùng chung bảng hàm toàn chương trình.
Đây chưa phải cơ chế phụ thuộc riêng của từng mô-đun. Compiler tổng quát
dùng các khai báo nhập trong từng tệp; `bộ biên dịch.vi` đã bổ sung phụ
thuộc trực tiếp vào `mã lệnh.vi`. Parser chương trình V++ cũng nhận từ tố
số nguyên trong đường nhập, để nạp các tên như `mã máy x86 64` và
`định dạng elf 64`.

Kiểm chứng trên macOS ARM64: bootstrap i32 Stage 1 → 2 → 3 → 4 đạt,
phần mã máy của Stage 2–4 khớp nhau và các probe thực thi đạt. Năm CTest
đối chiếu object/ABI/Machine IR/ISA, kiểm thử liên kết phép chia và chia
dư, cùng bước tạo compiler tổng quát Stage 1 đều đạt. Các bài kiểm thử
parser, API sinh mã và OOP đạt trên VM; hai probe dùng parser nhập mô-đun
và `Bộ Sinh Mã Máy` được compiler tổng quát tự biên dịch và chạy đạt.

Facade compiler còn API snapshot dựa vào compiler C++ cho đối chiếu.
Các kiểm chứng trên chưa chứng minh việc loại bỏ toàn bộ C++ khỏi toolchain.

## CI native bootstrap (19/10/2026)

Compiler bootstrap bằng bytecode trên C++ VM đã ngừng đăng ký vào CTest:
`vpp-compiler-bytecode-bootstrap`, `vpp-compiler-stage1-stage2`,
`vpp-selfhost-i32-artifact` và tùy chọn Stage 3 qua VM. Các script bootstrap
cũ được giữ để phục vụ tra cứu và debug; phần lịch sử bên dưới ghi lại những
lần kiểm chứng trước khi thay CI gate.

Trên Linux x86-64/AArch64 và macOS ARM64, installed native smoke chạy
`check_native_aot_bootstrap.cmake`: Stage 0 tạo executable compiler V++
Stage 1; Stage 1/2 tự biên dịch Stage 2/3 thành mã máy; SHA256 Stage 2/3
phải trùng nhau. Stage 3 biên dịch, liên kết và chạy probe native, các
fixture FFI, package import và re-export. Bài `vpp-native-compiler-driver`
vẫn có thể chạy thủ công để kiểm chứng nhánh compiler i32 Stage 4 cũ,
nhưng không được chạy lặp lại trong CI/release khi đã có full-language gate.
Windows hiện vẫn kiểm chứng object COFF/ABI và các fixture native của bản
cài đặt, chưa có gate self-hosting compiler ba thế hệ tương đương.

`quality` dùng CTest song song tối đa 2 test (cấu hình qua
`VPP_COVERAGE_CTEST_PARALLEL`), loại riêng các native compiler stress test đã
được kiểm tra ngoài coverage; vẫn giữ ngưỡng line coverage 45%. C/C++ vẫn
được dùng ở Stage 0 để tạo compiler V++ ban đầu; runtime ASM và linker hệ
điều hành vẫn là thành phần của native toolchain ở các stage sau.

## Nạp artifact và kiểm thử i32 bằng compiler V++ (18/10/2026)

`Bộ Mã Hóa Artifact` có API `biên dịch sản phẩm biên dịch giải mã dây` và
`biên dịch chạy sản phẩm biên dịch văn bản`. CLI `run --artifact` dùng đường này để
giữ bốn vùng ABI dưới dạng arrays qua bước kiểm tra và thực thi. Việc kiểm
tra định dạng/version, loại artifact, StringPool, function ID/name index,
initializer và bốn trường i32/opcode thực hiện bằng V++. Biểu diễn map
version 1 vẫn được đọc qua cùng bộ kiểm tra; API giải mã trả artifact map
tiếp tục phục vụ các công cụ cần chỉnh sửa bytecode.

Serializer chỉ chuyển artifact map sang ABI một lần mỗi lần gọi. Bộ nạp
dùng bảng khóa cho kiểm tra trùng function name index/initializer thay vì
quét toàn bộ danh sách đã nạp. Mỗi lần tuần tự hóa đọc lại dữ liệu hiện tại,
kể cả khi caller vừa sửa artifact. Đường VM chung cung cấp GC theo chi phí
duyệt graph và phép cộng/trừ/nhân i32 có kết quả two's-complement xác định.

CTest `vpp-selfhost-i32-artifact` chạy bộ kiểm thử V++ bằng bootstrap, biên
dịch chính bộ kiểm thử và các module compiler bằng `--compiler=vpp`, rồi
chạy artifact đó. Compiler trong artifact tiếp tục biên dịch nguồn mới có
biên i32, phép toán tràn, tham số mặc định và gán kết hợp; bộ kiểm thử còn
đối chiếu API map/wire và từ chối artifact sai ABI. Test CLI kiểm tra thêm
C++/V++ parity và chạy artifact i32 sau khi xóa nguồn đầu vào.

Kiểm chứng trên macOS ARM64 Release ngày 18/10/2026: chín CTest liên quan
đến bootstrap, self-host i32/artifact, runtime GC/JIT, CLI, OOP, source corpus,
import và tích hợp đều đạt. Bootstrap mở rộng chạy trực tiếp bằng
`scripts/quality/checkbootstrapstage3.cmake` cũng đạt Stage 1 → Stage 2 →
Stage 3; compiler Stage 3 sinh artifact cho chương trình kiểm tra chạy ra
`52`. Test ba thế hệ vẫn là lựa chọn bật riêng trong CMake do thời gian
biên dịch đầy đủ dài hơn kiểm thử thường quy.

## Khảo sát bỏ toàn bộ C/C++ và libSystem (17/10/2026)

Đích nghiên cứu được chọn là macOS ARM64 thử nghiệm, bỏ cả `libSystem`.
Probe tự tạo Mach-O và chữ ký SHA-256 đã xác nhận trên macOS 26.6.2:
executable qua dyld không có import dylib vẫn nạp `libSystem`; executable
ARM64 tĩnh và executable dùng loader riêng đều không chạy xong probe.
Vì vậy chưa thể công bố toolchain hoàn toàn độc lập trên host release
hiện tại. Chi tiết bằng chứng, cách chạy lại và thứ tự thay startup,
runtime, linker, signer, cache và bootstrap nằm trong
[khảo sát freestanding macOS](native-freestanding-macos.md).

## Native ARM64: biên dịch đối tượng theo module (17/10/2026)

Nhánh native thử nghiệm giữ quy trình bootstrap Stage 0 (trình biên dịch
V++ trên runtime hiện có) → Stage 1 (executable V++) → Stage 2–4.
`module_loader.vi` nạp các module độc lập theo thứ tự phụ thuộc; bộ biên
dịch hai lượt trong `module_compile.vi` trả bảng chữ ký toàn cục và ranh
giới function ID của từng đơn vị nguồn.

`program_codegen.vi` phát mã ARM64 riêng cho từng đoạn module. CALL cục
bộ được vá trực tiếp, trong khi các CALL liên module giữ bản ghi relocation.
`module_object.vi` tạo MH_OBJECT độc lập, xuất `_main` cho hàm vào và
`_vpp_fn_<tên>` cho các hàm còn lại; các lời gọi ngoài module tạo symbol
undefined cùng `ARM64_RELOC_BRANCH26`. Các lời gọi runtime sử dụng cùng cơ
chế relocation. `driver.vi` đóng gói các object thành archive nhị phân:
`[i32 độ dài][bytes Mach-O]...`.

`vpp_native_link_macho_modules` trong `vpp_aot_runtime.c` tạo tệp object
tạm riêng, gọi clang/ld của macOS để giải symbol và liên kết executable
theo cách nguyên tử; tệp đầu ra cũ được giữ nếu liên kết thất bại. Đây là
**separate compilation về mặt mã máy**, hiện vẫn thực hiện predeclare
toàn bộ module trong một lần gọi compiler. Cache IR/object và build
incremental được mô tả bên dưới; namespace/alias và export private/public
chưa được hỗ trợ. Bộ biên dịch
native vẫn chỉ hỗ trợ tập con i32 trên macOS ARM64, chưa thay đường
production C++ hay loại bỏ runtime C và linker hệ điều hành.

MH_OBJECT phát `LC_BUILD_VERSION` (macOS 11.0) để linker biết nền tảng
của từng module. Bộ liên kết dùng danh sách object và argv cấp phát động,
không còn giới hạn 32 module chứa mã máy; kích thước archive vẫn bị chặn
bởi vùng bytes native tối đa 1 MiB, cùng các giới hạn tệp và hệ điều hành.
Kiểm chứng trực tiếp ngày 17/10/2026: compiler mới liên kết thành công
35 object Mach-O (34 module nhập và một entrypoint), chương trình chạy
trả mã `52`. Trước thay đổi, cùng fixture thất bại ở bước liên kết với
mã `17`. `checknativedriver.cmake` đã có fixture hồi quy 35 object.
CTest `vpp-native-compiler-driver` chạy lại sau thay đổi đạt 1/1
(289,19 giây), gồm bootstrap Stage 1 → 2 → 3 → 4 và các probe.
Phần mã máy `__TEXT,__text` của Stage 2, 3 và 4 đều dài 173.344 byte,
cùng SHA256 `1b1f6fe8b9b22fe9f0ad9edc6c37336df7c62784fe6136de124327999d150ff8`.

## Native incremental, cache IR/object và runtime (17/10/2026)

`gói/biên dịch/bản địa/bộ nhớ đệm.vi` tái sử dụng IR thân hàm và MH_OBJECT theo
từng module. Compiler vẫn đọc đồ thị nguồn, tokenize và đăng ký chữ ký
ở lượt đầu; module có cache IR được bỏ qua lượt parse/hạ thân hàm. Cache
object cho phép bỏ qua phát mã ARM64, vá CALL và đóng gói Mach-O. Liên kết
executable vẫn chạy ở mỗi lần build, runtime C vẫn được biên dịch khi link.
Phạm vi này là compiler i32 native Stage 1 trở đi trên macOS ARM64;
CLI bytecode và đường Stage 0 `--emit-exe-selfhost` chưa dùng cache này.

Khóa mỗi bản ghi gồm loại artifact, target/phiên bản định dạng, nội dung
nguồn module, bảng tên/arity toàn cục theo thứ tự function ID, ranh giới
function ID và điểm vào. IR lưu độ dài và byte của từng hàm; khi nạp lại,
compiler dựng lại offset trong vùng IR toàn chương trình. Vì vậy, thay
đổi kích thước thân hàm ở dependency không làm mất hiệu lực cache caller
có chữ ký không đổi. Đổi tên/arity/thứ tự khai báo sẽ làm mất hiệu lực
cache theo bảng chữ ký mới. Đây là invalidation bảo thủ: chưa có đồ thị
phụ thuộc chữ ký riêng cho từng caller. Cache xét nội dung thay vì mtime.

Runtime cung cấp `cache_đọc(key, data)` và `cache_ghi(key, data)` với hai
handle byte riêng biệt, không rỗng đối với key. Đọc trả `1` khi hit, `0`
khi miss/tắt/không truy cập được cache, `-1` nếu handle không hợp lệ;
miss làm rỗng buffer đích. Ghi trả `0` hoặc `-1`. V++ giữ chính sách và
serialization; C chỉ đảm nhiệm lưu trữ byte, SHA-256 và công bố nguyên tử.
SHA-256 của **nội dung executable compiler đang chạy** tạo namespace cache,
ngăn compiler phiên bản khác tái sử dụng artifact cũ. Di chuyển/copy cùng
binary vẫn tái sử dụng được cache nếu chọn cùng thư mục.

Mỗi record kiểm tra magic/phiên bản, độ dài, key chính xác và SHA-256 của
header/key/payload trước khi cấp dữ liệu. Record hỏng, bị cắt ngắn hoặc
symlink được xem là miss; bản ghi mới dùng tệp tạm riêng rồi `rename`.
Hai build đồng thời có thể công bố cùng record mà không tạo bản ghi dở
dang. Cache không khả dụng không ngăn build thông thường; lỗi nguồn và
linker vẫn giữ nguyên artifact đầu ra đã có.

| Biến môi trường | Hành vi |
| --- | --- |
| `VPP_NATIVE_CACHE_DIR` | Thư mục cache; mặc định `.vpp-native-cache` trong working directory. Tự tạo thư mục cuối, thư mục cha cần có sẵn. |
| `VPP_NATIVE_NO_CACHE=1` | Bỏ qua đọc/ghi cache. |
| `VPP_NATIVE_CACHE_STATS=1` | In `native cache: hits=… misses=… writes=…` ra stderr khi cache hoạt động. Mỗi module chứa hàm có tối đa hai lookup: IR và object. |

```sh
VPP_HOME="$PWD" VPP_NATIVE_CACHE_DIR="$PWD/build/native-cache" \
  VPP_NATIVE_CACHE_STATS=1 \
  build-p0-release/nativedriver/vpp-native 'biên dịch' app.vi --đầu-ra build/app
```

CLI native dùng tiếng Việt: `vpp-native kiểm tra app.vi`,
`vpp-native biên dịch app.vi --đầu-ra build/app`,
`vpp-native đối tượng app.vi --đầu-ra build/app.o` hoặc
`vpp-native xuất đối tượng app.vi --đầu-ra build/app.o`.
Các bí danh `check`, `build`, `dựng`, `object` và tùy chọn `-o` được giữ
để các tập lệnh hiện có tiếp tục hoạt động.

Ngày 25/10/2026, bộ xử lý lệnh trong chính thư viện V++ native đã có thêm
`vpp-native --trợ-giúp`, `vpp-native --phiên-bản`, `trợ giúp`, `phiên bản`
(kèm `--help`, `--version`, `help`, `version`). Khi không truyền tham số,
CLI in hướng dẫn. Có thể bỏ `--đầu-ra`: `biên dịch chương-trình.vi` tạo
`chương-trình`, `đối tượng chương-trình.vi` tạo `chương-trình.o`;
đối với nguồn không kết thúc bằng `.vi`, tên đầu ra mặc định lần lượt
thêm `.out` hoặc `.o`. Hai từ `biên dịch`, `kiểm tra`, `xuất đối tượng`
đều có thể truyền thành một tham số có khoảng trắng hoặc hai tham số.
Việc chọn lệnh, xác định tên đầu ra và gọi compiler native chạy trong mã
V++ đã biên dịch, không cần chạy `src/cli/main.cpp` tại thời điểm đó.
Các lệnh hiện dùng chung một bộ phân tích đối số V++ thay vì lặp lại
việc phân nhánh theo số tham số. Có thể viết `biên dịch --đầu-ra app
"nguồn có dấu.vi"` hoặc `object -o app.o "nguồn có dấu.vi"`, bên cạnh
cú pháp truyền nguồn trước `-o`. Tham số thừa sau lệnh trợ giúp/phiên
bản, tham số đầu ra cho `kiểm tra` và đường đầu ra bị bỏ trống đều bị
từ chối với mã thoát 2. Khi không cấp phát được bộ đệm, đường biên dịch
đi qua phần giải phóng tài nguyên đã tạo trước đó.
Để tự tạo executable CLI native trên macOS ARM64 hoặc Linux x86-64:

```sh
cmake --build build-p0-release --target vpp-native-cli
build-p0-release/native-cli/vpp-native --trợ-giúp
```

`cmake --install build-p0-release --prefix <thư-mục-đích>` sẽ kèm
`bin/vpp-native` nếu đã chạy mục tiêu build trên. Mục tiêu này sử dụng
compiler C++ Stage 0 **lúc bootstrap**; executable Stage 1 chạy các
lệnh native trực tiếp. `vpp`/`vpp-cli` vẫn giữ các lệnh chưa chuyển đổi.

Kiểm chứng trên macOS ARM64: compiler đã có tự biên dịch phiên bản CLI
mới; `checknativedriver.cmake` kiểm tra các lệnh, đầu ra mặc định, đường dẫn
UTF-8 có khoảng trắng, thứ tự tùy chọn đầu ra và đối số sai, đối tượng
native và chuỗi Stage 1 → 2 → 3 → 4; parity mã máy và các chương trình
kiểm tra đều PASS. CLI C++ vẫn phục vụ các lệnh bao quát hơn như chạy VM,
quản lý gói, định dạng, kiểm thử và giai đoạn bootstrap. Runtime native
C và linker của hệ điều hành cũng chưa được loại bỏ; các chức năng này
là phạm vi chuyển đổi tiếp theo.

Có thể xóa thư mục cache để làm sạch; hiện chưa có cơ chế tự thu hồi entry
cũ hoặc hạn mức tổng dung lượng đĩa. Key và payload riêng vẫn chịu giới
hạn buffer 1 MiB; artifact không vừa cache được biên dịch bình thường.

Stage 1 mở rộng bảng intrinsic từ 18 lên 25 mục, bổ sung cache cùng
`byte_cắt`, `byte_tìm`, `byte_in`, `tệp_mở_ghi`, `tệp_ghi_khối`. Các API
byte/file dùng cùng ABI C đã có ở Stage 0: lát sở hữu bản sao, tìm trên
byte nhị phân, I/O giữ byte NUL và `0xff`; tên intrinsic được dành riêng
và lời gọi được kiểm tra arity. Handle vẫn là i32 với giới hạn tài nguyên
hiện hành, chưa phải string/GC hoặc FFI tổng quát.

Trước khi gỡ công cụ Python, `check_native_incremental.py` được CTest gọi để kiểm
chứng cold/warm cache, sửa dependency nhưng giữ mtime, đổi chữ ký/function
ID, đổi compiler, record sai digest/cắt ngắn/symlink, cache không khả dụng,
hai writer đồng thời, chế độ tắt cache, parity mã máy và ABI byte/file.
Kiểm thử chuyên sâu này chưa được chuyển đầy đủ sang CMake/V++.
Chuỗi Stage 1 → 2 → 3 → 4 tiếp tục được kiểm tra độc lập.

Kiểm chứng macOS ARM64 Release ngày 17/10/2026: chín CTest native liên
quan đều đạt sau khi cập nhật fixture codegen theo opcode MOVK từ
assembler Clang. `__TEXT,__text` của Stage 2/3/4 cùng dài 187.840 byte,
SHA-256 `893ff4d9add9f7b5c7ae99ce849a5a8a44ee67ca14a4254378d3641c8cd9429a`.
Phép đo cục bộ biên dịch chính 12 module compiler: cold cache 1,024 giây
(`0 hit / 24 miss / 24 write`), hai lần warm 0,210 và 0,204 giây
(`24 hit / 0 miss / 0 write`), tắt cache 0,774 giây. Vùng mã máy của cả
bốn artifact giống nhau. Đây là mẫu đo trên cùng máy, chưa phải gate
hiệu năng đa nền tảng hoặc benchmark thống kê.

Compiler vẫn cần Stage 0 C++ để bootstrap, runtime C/libc (SHA-256 dùng
CommonCrypto của macOS) và clang/ld của hệ điều hành. Các thế hệ native
tự biên dịch compiler i32 subset, **chưa độc lập khỏi hệ sinh thái C/C++**.


## Luồng OOP hiện hành (15/10/2026)

Compiler V++ có parser, semantic, IR, emitter và linker cho lớp, constructor,
receiver `mình`/`gốc`, kế thừa, giao diện, phương thức và phân quyền. Kiểm tra
hợp đồng phát hiện thiếu phương thức, sai số tham số, triển khai không công
khai và ghi đè thu hẹp quyền. `kiểu mô đun.vi` quản lý kiểu trực tiếp, kiểu
nhìn thấy và kiểu tái xuất theo định danh khai báo; có thể khởi tạo lớp nhập
qua bí danh, triển khai giao diện nhập và kế thừa lớp ở mô-đun khác.

Lời gọi phương thức/đọc/gán thuộc tính nhận cả receiver tạm như kết quả
hàm, phần tử danh sách hoặc chuỗi lời gọi; ABI opcode hiện hành giữ nguyên.
Đường C++ cũng mang chữ ký kiểu nhập để kiểm tra constructor và giao diện.
Runtime giữ receiver cùng quyền truy cập lexical cho closure trong phương
thức; compiler V++ tự chủ chưa parse/hạ lambda và FFI, nên các chương trình
dùng chúng vẫn đi qua compiler C++ bootstrap. Các mục lịch sử phía dưới
mô tả từng giai đoạn và không thay thế trạng thái hiện hành này.

Trước đây `check_oop_pipeline.py` kiểm tra CLI C++/V++, alias nhập lặp,
tái xuất, kế thừa/giao diện xuyên mô-đun, từ chối hợp đồng sai, artifact chạy
khi đã xóa nguồn và module `Ngăn Xếp` được biên dịch bằng V++.
Hiện `check_vpp_cli.cmake` duy trì kiểm tra smoke, chưa bao phủ toàn bộ ca cũ.

## Bề mặt OOP của thư viện compiler

`gói/biên dịch/giao diện cú pháp.vi` khai báo `Truy Cập Cú Pháp` và
`Khách Duyệt Cú Pháp` độc lập với phần triển khai.
`gói/biên dịch/nút cú pháp.vi` cung cấp lớp `Nút Cú Pháp`, `Nút Lớp`,
`Nút Hàm` cho dữ liệu AST map (`loại`, `tên`,
`thành viên`, `phương thức`, `câu lệnh`, `thân`).
`gói/biên dịch/chính.vi` tái xuất cả giao diện và lớp qua `công khai nhập`. Các AST node
được truy cập bằng `loại()`, `tên()`, `các con()` và `dữ liệu()`, hoặc qua
visitor `Khách Duyệt Cú Pháp` với `duyệt(khách)`; parser và IR
vẫn thao tác trên map để giữ ổn định đường self-hosting hiện tại. Đây là
adapter thư viện, không phải chuyển toàn bộ frontend sang mô hình object.
Visitor duyệt cả biểu thức và các nhánh lồng nhau (khối hàm, `nếu`,
`chọn`, `thử`, danh sách/từ điển, lời gọi, toán hạng) thay vì chỉ duyệt
danh sách khai báo. `kiem_tra_ast_oop.vi` kiểm thử AST parser thực và
snapshot tùy biến.

Các module HTTP, Database Connection, File Stream và Logger cũng có API lớp
ở thư viện chuẩn; chúng chỉ bọc thao tác V++ hiện có, không thay thế VM hoặc
thay đổi trạng thái self-hosting/native compiler được mô tả bên dưới.

## Đường production và bootstrap

CLI có đường bytecode opt-in qua compiler V++:

```sh
build/bin/vpp-cli build --compiler=vpp program.vi -o program.vbc
build/bin/vpp-cli run --compiler=vpp program.vi
build/bin/vpp-cli run --artifact program.vbc
```

Các alias tiếng Việt `dựng` / `chạy` hỗ trợ cùng tùy chọn. `--compiler=cpp`
chọn bootstrap C++ tường minh; lệnh không có tùy chọn vẫn giữ backend C++.
`gói/biên dịch/trình điều khiển.vi` điều phối nạp nguồn, lexer, parser,
semantic, IR, phát bytecode và linker V++. Mã nguồn đích được đọc thành chuỗi
trong V++; không được đưa vào `compilePipeline()` C++. C++ chỉ biên dịch
wrapper và thư viện compiler để khởi động VM. Đây là compiler V++ chạy trên
runtime bootstrap, **chưa phải compiler native tự biên dịch toàn bộ chính nó**.

`nguồn tệp.vi` dựng đồ thị import từ các tệp thực, ưu tiên nguồn cục bộ ở
thư mục importer và các tổ tiên, rồi tới `VPP_HOME`/thư viện workspace.
Linker dùng chính identity đã phân giải, bảo toàn bí danh, tái xuất, thứ tự
initializer và import trùng. Resolver hiện nhận đường import nguồn `.vi`
không đuôi; chưa bao phủ package manifest, alias/redirect tương thích và
canonical identity của symlink như resolver C++.

`artifact.vi` tuần tự/giải mã JSON bằng V++, có định dạng `VPP bytecode`,
version 1, ABI `Instruction 4*i32`. Payload giữ StringPool, function ID,
bytecode gốc, thân hàm và initializer bằng bốn vùng wire arrays; từng lệnh
có đúng bốn i32, tránh lặp tên trường trên mỗi opcode. Bộ nạp kiểm tra hình dạng, kiểu i32,
opcode đã định nghĩa, bảng hàm và initializer; VM tiếp tục xác thực bytecode
trước khi chạy. Artifact chạy được sau khi xóa nguồn và các dependency.
`build` chỉ biên dịch, không chạy chương trình; lỗi compiler không tạo hoặc
ghi đè artifact có sẵn. CLI từ chối đầu ra trùng với nguồn điểm vào.

Parser đã bổ sung tên nhiều từ trong chương trình và tham số mặc định scalar
(int/float/string/bool/null, số nguyên âm kể cả `-2147483648`). Emitter dùng
`OP_PARAM_MAC_DINH` với cùng payload StringPool của C++. Phép gán `a[i] = x`
và map/nested index dùng `OP_GAN_CHI_SO`, theo thứ tự base/index/value của C++.
Gán kết hợp và cập nhật trực tiếp vào chỉ số chưa được hạ. `nội tại.vi` và
`opcode.vi` duy trì ABI theo header C++; `vpp-opcode-abi` đối chiếu giá trị
opcode, chưa kiểm tra arity, alias hay quy tắc reserved/shadowing. Lexer duyệt UTF-8 một lượt
để dựng bảng ký tự, tránh lấy chỉ số từ đầu chuỗi cho mỗi ký tự.

Kiểm chứng ngày 14/10/2026: CTest `vpp-compiler-vpp-cli` đạt; kiểm tra source
C++/V++, defaults, module import trùng, artifact chạy không cần nguồn,
version/truncation lỗi, output rỗng và từ chối class không có fallback. Fixture
CLI còn biên dịch module `gói/biên dịch/mã thao tác.vi` bằng V++ và thực thi các
hàm tra opcode từ bytecode mới sinh. Đây là một module compiler; chưa phải
toàn bộ compiler tự biên dịch. Fixture
`kiemtracompilervpp.vi` có 33 phép kiểm tra; đối chiếu
bytecode defaults, index store và intrinsic với oracle C++ độc lập. Lexer UTF-8
và các fixture hồi quy parser/IR/native được chạy riêng. Chưa có bằng chứng
toàn bộ integration suite hoặc bootstrap Stage 1/Stage 2 đạt.

Đường import chấp nhận thành phần có từ khóa (ví dụ `chuyển kiểu`), và tên
hàm nhiều từ có thể bắt đầu/chứa từ khóa theo parser C++. Module thống kê
đã khai báo dependency từ điển/list trực tiếp, thay vì dựa vào namespace
bootstrap được importer tình cờ nạp. Runtime heap bootstrap dùng registry
weak theo địa chỉ để đăng ký allocation trung bình O(1), và ngân sách GC
tỷ lệ với graph sống. Reader JSON cũng dựng bảng code point một lượt,
để nạp artifact lớn không quét lại prefix UTF-8 ở mỗi lần đọc ký tự.
Các module compiler đã khai báo dependency trực tiếp thay vì dựa vào
symbol xuất hiện gián tiếp qua import khác. Kiểm thử runtime P0 bổ sung xác minh deduplication,
address reuse, giữ graph reachable và thu gom các cycle đã tách khỏi root.

Lexer đã được biên dịch bằng V++ thành artifact 79.211 byte, chứa 87 hàm,
7 initializer và 5.950 lệnh thân hàm; artifact này đã được nạp/chạy bằng VM.
Đây là một phần compiler được tự biên dịch, chưa phải kiểm chứng toàn bộ
compiler qua các thế hệ Stage 1/Stage 2. Các fixture JSON, roundtrip tệp,
GC và thống kê liên quan đến những thay đổi này đã khớp golden output.

Lõi bytecode (lexer/parser/semantic/IR/emitter/linker và bộ nạp, cùng các
dependency V++) đã được biên dịch bằng V++ thành artifact 301.406 byte,
chứa 190 hàm, 20 initializer và 22.590 lệnh thân hàm. Chạy artifact đó đã
gọi chính compiler vừa sinh để biên dịch chương trình mới có hàm, tham số
mặc định và vòng lặp, rồi thực thi đúng kết quả `52`. Fixture
`kiemtrabootstrapvpp.vi` và CTest `vpp-compiler-bytecode-bootstrap` giữ đường
kiểm tra này; CTest đã đạt ngày 14/10/2026. Gate CLI và runtime P0 cũng đã
đạt; fixture compiler 33 kiểm tra và JSON liên quan đạt interpreter/JIT.
Đây là bootstrap tự chủ của **lõi compiler bytecode** trên VM,
chưa chứng minh Stage 1/Stage 2 cho toàn compiler, chưa biên dịch driver
FFI/CLI bằng V++ và chưa có executable compiler native độc lập.

## Bootstrap compiler bytecode qua Stage 1 → Stage 2

`scripts/quality/checkbootstrapstages.cmake` kiểm tra một lượt bootstrap
theo ba vai trò:

- **Stage 0:** `vpp-cli run --compiler=cpp` biên dịch và chạy chương trình
  `bootstrap-stage1.vi` bằng bootstrap compiler/runtime C++ hiện hành.
- **Stage 1:** chương trình V++ dựng đồ thị nguồn compiler từ các chuỗi
  được đóng gói trong fixture, sau đó gọi parser, semantic, IR, emitter,
  linker và serializer viết bằng V++ để tạo `stage2.vbc`. Python chỉ chuẩn
  bị đồ thị nguồn và điều chỉnh đường import cho resolver trong bộ nhớ,
  không thực hiện compiler pass.
- **Stage 2:** `vpp-cli run --artifact stage2.vbc` chạy bytecode compiler V++
  vừa được Stage 1 tạo ra. Compiler này biên dịch một chương trình nguồn mới
  có tham số mặc định và vòng lặp thành `stage2-probe.vbc`; VM chạy artifact
  kiểm thử phải in `52`.

Lần chạy 16/10/2026 tạo thành công `stage2.vbc` (509 KiB, 464 hàm, 24
initializer mô-đun, ABI `Instruction 4*i32`, SHA256
`50ed78f0dde310dd831a8e47d3e53b5d3e539b629c112b8b88f6e791cf813c20`).
Lần chạy nạp/chạy Stage 2 bằng CLI chưa tối ưu vượt timeout 1.200 giây.
Chẩn đoán xác định nạp JSON artifact lớn là một bước tốn thời gian; lỗi
timeout đó không chứng minh bytecode Stage 2 không chạy được.

**Kiểm chứng 17/10/2026: bootstrap Stage 1 → Stage 2 → probe đã PASS với
CLI Release.** CLI dựng bằng `-DCMAKE_BUILD_TYPE=Release` nạp, giải mã,
kiểm tra và thực thi `stage2.vbc` thành công trong 113,23 giây ở lần đo
chẩn đoán. `checkbootstrapstages.cmake` chạy lại với
`VPP_BOOTSTRAP_REUSE_STAGE2=ON` đạt: Stage 2 tạo `stage2-probe.vbc`, VM
chạy artifact thử nghiệm ra đúng `52`, và SHA256 Stage 2 giữ nguyên như
trên. Tùy chọn `REUSE` chỉ bỏ qua bước sinh lại Stage 2; mọi bước thực thi
và kiểm tra probe vẫn được chạy. Lệnh tái hiện:

```sh
cmake -S . -B build/bootstrap-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build/bootstrap-release --target vpp-cli -j4
cmake -DVPP_BOOTSTRAP_REUSE_STAGE2=ON \
  -DVPP_EXEC="$PWD/build/bootstrap-release/bin/vpp-cli" \
  -DVPP_ROOT="$PWD" \
  -DVPP_OUTPUT_DIR="$PWD/build/compiler-bootstrap-stages" \
  -P scripts/quality/checkbootstrapstages.cmake
```

Kết quả xác nhận **compiler bytecode V++ chạy qua hai thế hệ**; phép
kiểm tra ba thế hệ được bổ sung ngay dưới đây. Compiler executable native
tự lưu trữ và runtime độc lập khỏi C++ VM vẫn chưa hoàn thành. Đường nạp
JSON artifact lớn trên CLI không tối ưu vẫn cần cải thiện hiệu năng.

### Kiểm chứng compiler Stage 1 → Stage 2 → Stage 3 (17/10/2026)

`scripts/quality/checkbootstrapstage3.cmake` kiểm tra thêm một thế hệ
compiler. CMake dùng `prepare_bootstrap_stages.cmake` hai lần để đóng gói đồ
thị 25 mô-đun V++ thành nguồn Stage 2, rồi đóng gói chính nguồn Stage 2
thành nguồn Stage 1. Các bước parser, semantic, IR, emitter, linker và
serializer của cả hai thế hệ đều chạy trong V++:

- Stage 1 chạy trên VM bootstrap C++, biên dịch chương trình compiler Stage 2.
- Stage 2 **thực thi từ artifact bytecode** và biên dịch lại toàn bộ đồ thị
  compiler V++ thành artifact Stage 3. Không chuyển các pass này sang C++.
- Stage 3 chạy từ bytecode, biên dịch chương trình kiểm thử có tham số mặc
  định và vòng lặp. Chạy bytecode thử nghiệm trả đúng `52`.

Kiểm chứng thực tế bằng `build/bootstrap-release/bin/vpp-cli`:

| Artifact | Kích thước | Hàm | Module initializer | SHA256 |
| --- | ---: | ---: | ---: | --- |
| Stage 2 (chuỗi ba thế hệ) | 981.365 B | 489 | 24 | `2e912ea16052a5c8cb2552be0dfd2f18143b9d8fd18170214a2cde6a08ef7513` |
| Stage 3 | 520.934 B | 464 | 24 | `50ed78f0dde310dd831a8e47d3e53b5d3e539b629c112b8b88f6e791cf813c20` |
| Probe từ Stage 3 | 628 B | 2 | 0 | `2ae01a1925c54eef1611a6892e6156c999b81c61cb5ef0e2280e46ddb2d585d8` |

**Stage 3 trùng từng byte với compiler `stage2.vbc` của bài kiểm tra hai
thế hệ trước** (cùng SHA256); probe từ Stage 3 cũng trùng từng byte với
probe cũ. Toàn chuỗi Stage 1 → Stage 2 → Stage 3 → probe **PASS**. Bước
Stage 1 của chuỗi mới hoàn tất trong 117,07 giây ở lần đo Release.

Chạy lại bằng CLI Release đã build:

```sh
cmake -DVPP_EXEC="$PWD/build/bootstrap-release/bin/vpp-cli" \
  -DVPP_ROOT="$PWD" \
  -DVPP_OUTPUT_DIR="$PWD/build/compiler-bootstrap-stages/three-stage" \
  -P scripts/quality/checkbootstrapstage3.cmake
```

Thêm `-DVPP_BOOTSTRAP_REUSE_STAGE2=ON` để kiểm lại Stage 2 → Stage 3 mà
không sinh lại Stage 2. Nếu có artifact chuẩn trước đó, truyền thêm
`-DVPP_BOOTSTRAP_STAGE3_ORACLE=<đường-dẫn-stage2.vbc>` để kiểm tra SHA256.
CTest toàn chuỗi là tùy chọn khi configure với
`-DVPP_ENABLE_STAGE3_BOOTSTRAP_TEST=ON`, vì phép kiểm tra nặng hơn gate
Stage 1 → Stage 2 hiện hành.

Đây là bằng chứng **self-hosting compiler bytecode qua ba thế hệ**. VM vẫn
được thực thi bởi C++, và Python chỉ đóng gói fixture nguồn. Native AOT
hiện mới bao phủ subset i32, chưa đủ để tạo compiler executable độc lập.
`io_doc_file_vm` bị runtime từ chối trong chế độ system FFI strict, vì vậy
test ba thế hệ dùng đồ thị nguồn đóng gói sẵn; để self-host compiler qua
nguồn tệp thực cần mở rộng compiler/runtime cho FFI và I/O chuẩn.

`src/cli/main.cpp` vẫn gọi `vietvm::compiler::compilePipeline(...)` trong C++.
Đã có thêm đường native AOT tự chủ **opt-in**:
`vpp build --target <triple> --emit-obj-selfhost <function> <source.vi> <output.o>`.
Lệnh này sử dụng `gói/biên dịch/mã máy từ cây.vi` để phân tích trực tiếp
mã nguồn `.vi` bằng lexer/parser V++, dựng AST/IR, hạ Machine IR, phát mã
máy AArch64 hoặc x86-64 và tạo object ELF/Mach-O/COFF từ thư viện V++.
Bridge C++ `compileSourceMachineFunctionVpp` chỉ tạo chương trình V++
gọi các API đó, khởi động VM hiện hành, giải mã bản ghi và ghi object.
**Đây vẫn là bootstrap bằng VM/C++**, chưa phải compiler tự chạy bằng
executable native do chính V++ tạo ra. Đường `--emit-obj` cũ tiếp tục dùng
frontend/semantic C++ để tạo IR đầu vào.

Ngày 16/10/2026 đã bổ sung đường **native executable self-host thử nghiệm**:

```sh
build/bin/vpp-cli build --target aarch64-apple-darwin --emit-exe-selfhost \
  src/tests/nativeaotprogram.vi /tmp/nativeaotprogram
/tmp/nativeaotprogram
echo $? # 42
```

Trên Linux x86-64 sử dụng `--target x86_64-linux-gnu`; phải chọn đúng
`hostTarget()` (chưa hỗ trợ cross-link). Mã nguồn khai báo `hàm chính()`
hoặc `hàm main()` không tham số, trả về `i32`; entrypoint tiếng Việt được
ánh xạ thành symbol ABI `main`, kể cả khi có hàm khác gọi `chính()`.
Các hàm còn lại sử dụng tên symbol ASCII; khai báo đồng thời cả `chính`
và `main` bị từ chối do trùng symbol khi biên dịch chương trình.
`gói/biên dịch/mã máy từ cây.vi` parse và hạ IR **một lần cho toàn bộ chương
trình**, phát object Mach-O/ELF riêng cho từng hàm. Bridge C++ thu các
object rồi gọi `cc` trên host để liên kết relocation và tạo executable.
Có thể chỉ định đường dẫn compiler/linker qua `VPP_NATIVE_CC` (một tên
executable hoặc đường dẫn, không phải câu lệnh shell). Đầu ra chỉ được
thay thế khi linker thành công; đường dẫn đầu ra trỏ về mã nguồn qua
symlink/hard link cũng bị từ chối. CTest `vpp-native-selfhost-executable`
đã chạy trên macOS ARM64, xác minh exit code `42`, relocation của lời gọi
`chính()`, đường dẫn có dấu cách, từ chối biểu thức ngoài subset, không có
entrypoint hoặc trùng `main()`/`chính()`, và bảo toàn artifact khi thất bại.

**Tham số hàm có số lượng động (17/10/2026).** Đường native Stage 0
(`--emit-obj-selfhost` và `--emit-exe-selfhost`) và lõi compiler native V++
Stage 1 đã bỏ ngưỡng cố định bốn tham số. Machine IR biểu diễn riêng
`CALL_ARITY` (số đối số) và `CALL` (định danh hàm), cho phép gọi hàm có
nhiều đối số mà không phải nhét số lượng vào một bitfield nhỏ. Encoder
AArch64 và x86-64 hỗ trợ tham số truyền qua stack theo ABI đích;
trên macOS ARM64, đối số i32 trên stack xếp theo bước 4 byte, còn Linux
AAPCS64 xếp theo bước 8 byte. Đường native C++/V++ đối chiếu IR và byte
phát ra trên các target macOS ARM64, Linux ARM64/x86-64 và Windows x86-64.
CTest `vpp-native-selfhost-executable` còn tạo, liên kết và chạy một
chương trình 128 tham số, kiểm tra đối số cuối và mã thoát `52`.
Thử thủ công 4.200 tham số trên macOS ARM64 cũng đã build/chạy, trả `52`.

Không thể bảo đảm **vô hạn theo nghĩa toán học**: số lượng vẫn bị ràng
buộc bởi bộ nhớ, kích thước nguồn/token/IR, i32 dùng cho chỉ số/số đếm,
kích thước stack và giới hạn ABI/hệ điều hành. Runtime native vẫn có
ngưỡng 1 MiB cho mỗi buffer byte và giới hạn các handle. Việc bỏ một
ngưỡng cố định không đồng nghĩa mọi chương trình với số đối số bất kỳ
đều biên dịch hoặc thực thi được.

Đây là executable native của **chương trình trong subset i32**, chưa phải
executable của **compiler V++**. Driver, VM/bootstrap, linker gọi qua C++ và
toolchain hệ thống vẫn còn; import, GC, lambda, FFI tổng quát và các kiểu
động của runtime VM chưa đi được qua đường AOT này. Chuỗi literal UTF-8
được hỗ trợ qua handle i32 như mô tả dưới đây, chưa có kiểu chuỗi native
đầy đủ hay kiểm tra kiểu tĩnh cho handle.

Runtime AOT i32 hỗ trợ nhập/xuất console tối thiểu. `in biểu_thức` gọi
`vpp_native_print_i32` và in số nguyên có dấu. Hai intrinsic chỉ dành cho
native AOT là `đọc_số()` và `trạng_thái_đọc()`, được hạ trực tiếp thành
`vpp_native_read_i32` và `vpp_native_read_status`, liên kết từ
`src/runtime/native/vpp_aot_runtime.c`. Trạng thái trả về 0 nếu đọc thành
công, 1 nếu EOF, 2 nếu đầu vào không hợp lệ, 3 nếu vượt miền i32; giá trị
trả về từ `đọc_số()` chỉ có nghĩa khi trạng thái bằng 0. Tên intrinsic và
symbol C runtime được dành riêng, và gọi intrinsic có đối số bị từ chối.
`vpp-native-i32-input` kiểm tra hai biên i32, EOF, chữ không hợp lệ và tràn
số; ngày 16/10/2026, ba CTest `vpp-native-i32-input`,
`vpp-native-i32-runtime`, `vpp-native-selfhost-executable` đều đạt (3/3).

**Bổ sung native UTF-8 và byte/file I/O (17/10/2026).**
`src/runtime/native/vpp_aot_runtime.c` cung cấp ABI C cho bộ đệm byte có
handle i32 và stream tệp nhị phân. Các intrinsic chỉ dành cho native AOT:

- `byte_mới()` → handle dương hoặc `-1`; `byte_thêm(handle, byte)` → `0/-1`;
  `byte_dài(handle)` và `byte_lấy(handle, index)` → giá trị hoặc `-1`;
  `byte_hủy(handle)` → `0/-1`.
- `byte_nối(đích, nguồn)` → `0/-1` (cho phép nối chính nó);
  `byte_bằng(a, b)` → `1` nếu bằng, `0` nếu khác, `-1` nếu handle lỗi;
  `byte_in(handle)` ghi nguyên vẹn byte ra stdout (không thêm newline),
  trả về số byte ghi hoặc `-1`.
- `byte_cắt(handle, vị_trí, số_byte)` → một handle **sở hữu bản sao** vùng
  byte hoặc `-1`; chấp nhận lát rỗng ở cuối, từ chối offset/length âm và
  lát vượt giới hạn. `byte_tìm(nguồn, mẫu)` trả về offset byte đầu tiên,
  `-1` nếu không thấy, `-2` nếu handle không hợp lệ; mẫu rỗng khớp tại `0`.
  `byte_làm_rỗng(handle)` đặt chiều dài về `0` để tái sử dụng, trả `0/-1`.
  Offset áp dụng trên byte UTF-8, không phải chỉ số ký tự Unicode.
- `tệp_mở_đọc(path)` và `tệp_mở_ghi(path)` → handle dương hoặc `-1`;
  `tệp_đọc_byte(file)` → `0..255`, `-1` (EOF), `-2` (lỗi);
  `tệp_ghi_byte(file, byte)` và `tệp_đóng(file)` → `0/-1`.
- `tệp_đọc_khối(file, buffer)` nối tối đa 4096 byte vào buffer, trả số byte,
  `0` khi EOF hoặc `-1` khi lỗi/hết dung lượng; nếu buffer đủ 1 MiB, hàm
  từ chối trước khi đọc khỏi stream. `tệp_ghi_khối(file, buffer)` ghi toàn
  bộ byte nhị phân (có NUL), trả số byte thực ghi hoặc `-1` với handle lỗi.

`path` là handle của bộ đệm byte, không phải con trỏ C. Literal như
`biến path = "vpp-io.in";` được compiler V++ chuyển trực tiếp thành byte
UTF-8 và các lệnh gọi cấp phát/ghi byte của Machine IR; ký tự escape theo
quy tắc literal trong frontend. Chương trình phải gọi `byte_hủy(path)`
khi không dùng nữa; literal được tạo mới mỗi lần biểu thức được thực thi,
chưa có GC/reference counting native. `in "chuỗi"` vẫn bị từ chối: `in`
hiện chỉ in i32 và chưa có overload cho chuỗi. Hạn mức runtime: 64 handle
byte và 64 handle tệp đang mở, tối đa 1 MiB/bộ đệm, tối đa 4096 byte
cho đường dẫn; literal được giới hạn 4096 byte để tránh phát object quá lớn.
Handle được cấp tăng dần, kiểm tra loại/hiệu lực, không tái sử dụng handle
cũ trong vòng đời tiến trình. Đường dẫn chứa byte NUL bị từ chối.
Lệnh ghi tệp dùng `wb` (tạo mới/cắt ngắn) theo ngữ nghĩa C; chương trình
chịu trách nhiệm chọn đường dẫn và đóng stream để nhận lỗi flush.

`gói/biên dịch/mã máy từ cây.vi` gắn tên intrinsic vào symbol C và kiểm tra
arity; `gói/biên dịch/mã máy hạ biểu diễn trung gian.vi` xác minh whitelist ABI độc lập rồi
phát `Call` tối đa hai đối số và relocation native. CLI nhận biết các
`vpp_native_*` relocation để liên kết runtime C; backend encoder/object
vẫn do V++ thực hiện. Đây là một ABI C **cố định**, chưa phải cơ chế
`extern`/FFI động cho thư viện C bất kỳ.

CTest `vpp-native-byte-file-io` xác minh chương trình V++ tạo executable,
ghi/đọc/sao chép chính xác `00 ff 0a 41`, UTF-8 `Aé`, escape, byte NUL trong
đường dẫn, EOF, handle đã đóng, byte ngoài phạm vi, arity và reserved symbol.
Ngày 17/10/2026: `vpp-native-byte-file-io`, `vpp-native-i32-input`,
`vpp-native-i32-runtime`, `vpp-native-selfhost-executable` và
`vpp-native-selfhost-host-link` **đạt 5/5** trên macOS ARM64 (Release).

Đợt kiểm chứng tiếp theo ngày 17/10/2026: CTest
`vpp-native-block-file-io` xác minh 10.012 byte nhị phân, SHA-256 độc lập,
ba lần đọc 4096 + 4096 + 1820 byte, EOF, UTF-8 và tên ABI dành riêng.
`vpp-native-byte-ranges` xác minh `byte_cắt` (runtime CALL ba đối số),
`byte_tìm` kể cả NUL/UTF-8/mẫu rỗng, lát sao chép giữ nguyên sau khi xóa
nguồn, giới hạn 1 MiB khi nối/bổ sung/đọc file, xử lý handle lỗi và
kiểm tra số đối số/tên dành riêng. Toàn bộ **7/7 CTest native tập trung
đã đạt** trên macOS ARM64. Bộ đệm và file I/O hiện có ABI i32 cố định;
chúng chưa thay thế kiểu string hoặc API file động trên VM C++.

**Lõi lexer chạy native bằng V++ (17/10/2026).**
`gói/biên dịch/bản địa/phân tích từ vựng.vi` khai báo các hàm độc lập, ABI i32:
`từ vựng khoảng trắng`, `từ vựng dấu phân cách`, `từ vựng unicode tám bit bước`, `từ vựng hai`, `từ vựng số mục bỏ qua`,
`từ vựng nháy điểm kết thúc`, `từ vựng điểm kết thúc`, `từ vựng loại từ tố`. Chúng quét byte UTF-8 trên handle
runtime native và trả offset byte, không dùng VM, bộ sưu tập động hoặc
intrinsic C++ để tách từ tố. `từ vựng unicode tám bit bước` kiểm tra Unicode scalar,
loại NUL, UTF-8 thiếu byte, overlong, surrogate và code point ngoài miền;
`từ vựng số mục bỏ qua` bỏ khoảng trắng/chú thích `//` và `/* */` (từ chối chú thích
khối chưa đóng). `từ vựng điểm kết thúc` hiểu dấu nháy/escape, toán tử đơn/đôi và
định danh UTF-8. `từ vựng loại từ tố` trả mã `1` định danh, `2` số nguyên, `3` số
thực, `4` chuỗi, `5` toán tử, `6` dấu câu; `-1` khi lỗi. Lõi quét còn chưa
ghép từ khóa nhiều từ, chưa tạo AST hay bảng token, và chưa hỗ trợ import
native, do đó **chưa phải frontend native hoàn chỉnh**.

`scripts/quality/checknativelexer.cmake` nối **mã nguồn V++** của lõi và
fixture V++ thành chương trình `.vi` độc lập (CMake chỉ ghép file), sau đó
biên dịch bằng `--emit-exe-selfhost` và chạy executable thực. Fixture kiểm
tra byte span của định danh tiếng Việt, CRLF, UTF-8 2/3/4 byte, toán tử,
số, chuỗi escape, các lỗi UTF-8/chú thích/chuỗi, và đọc/token hóa tệp
nguồn UTF-8 bằng `tệp_đọc_khối`. CTest `vpp-native-lexer-kernel` đã **PASS**
trên macOS ARM64 (Release); toàn bộ 7/7 bài native tập trung gồm bài mới
đã PASS ngày 17/10/2026. Có thể chạy độc lập bằng:

```sh
cmake -S . -B build-p0-release
ctest --test-dir build-p0-release -R '^vpp-native-lexer-kernel$' --output-on-failure
```

**Token buffer, parser/semantic và IR i32 chạy native (17/10/2026).**
`gói/biên dịch/bản địa/từ tố.vi` lưu token thành dãy byte có khuôn dạng
`[i32 số_token][i32 start, i32 end, i32 kind]*số_token`, little-endian.
`start/end` là byte offset UTF-8 trong buffer nguồn. `từ vựng tách từ tố` gọi
lexer V++ để sinh token và dọn buffer khi gặp lỗi. `từ tố kiểm tra hợp lệ` kiểm tra
header, kích thước, offset, token kind, thứ tự và khoảng trắng/chú thích
giữa các token **trước khi** parser tin dữ liệu; từ chối token bị sửa hoặc
không khớp nguồn. ABI C trong `src/runtime/native/vpp_aot_runtime.c` hỗ trợ
`byte_đẩy_i32`, `byte_đọc_i32`, `byte_ghi_i32`, đồng thời dành riêng các
tên intrinsic, kiểm tra arity trong compiler AOT. `byte_đọc_i32` trả `-1`
cho offset sai; không thể tự phân biệt với giá trị i32 hợp lệ bằng `-1`,
nên caller phải xác thực offset/độ dài.

`gói/biên dịch/bản địa/phân tích cú pháp.vi` parse **tập con có chủ ý**: một
`hàm chính()` không tham số; `biến tên = biểu_thức;`, gán lại biến
`tên = biểu_thức;` và `trả về biểu_thức;` cuối hàm. Biểu thức hỗ trợ
i32 dương, định danh đã khai báo, ngoặc, dấu âm đơn ngôi, `+`, `-`, `*`
với thứ tự ưu tiên. Semantic kiểm tra tên chưa khai báo, tên trùng, tên
không hợp lệ, số ngoài miền và câu lệnh dư sau `trả về`. Kết quả là dãy
IR stack-based gồm các cặp `[i32 opcode, i32 operand]`: `PUSH=1`,
`LOAD=2`, `STORE=3`, `ADD=4`, `SUB=5`, `MUL=6`, `RET=7`, `NEG=8`.
`gói/biên dịch/bản địa/biểu diễn trung gian.vi` là bộ thực thi **tham chiếu** viết bằng V++
để đối chiếu kết quả, chưa phát mã máy từ IR này. IR có thể lưu thành
tệp nhị phân và nạp lại bằng ABI file native.

Fixture `src/tests/nativeparserharness.vi` và
`scripts/quality/checknativeparser.cmake` ghép bốn mô-đun V++ native,
build thành executable ARM64, kiểm tra offset Unicode, ưu tiên biểu thức,
gán lại biến, semantic lỗi, token buffer bị sửa, i32 âm/biên 1 MiB,
đọc nguồn CRLF/UTF-8 và ghi/đọc lại artifact `.ir`. CTest
`vpp-native-parser-ir` **PASS** trên macOS ARM64 ngày 17/10/2026;
artifact của fixture dài 64 byte và khớp oracle i32 little-endian độc lập.
Sau thay đổi này, **8/8 CTest native tập trung PASS** (`selfhost-executable`,
`i32-runtime/input`, `byte-file/block-file/byte-ranges`, `lexer-kernel`,
`parser-ir`). Kiểm tra C11 `-Wall -Wextra -Wpedantic -Werror` và
`git diff --check` cũng đạt.

Giới hạn còn lại: parser này chưa có import, nhiều hàm, class/OOP, luồng
điều khiển, chuỗi dạng giá trị, số thực, I/O trong cú pháp nguồn hay
`-2147483648` dạng literal; evaluator IR chưa có kênh báo lỗi riêng để
phân biệt sentinel với kết quả hợp lệ, và chưa có codegen từ IR sang
Mach-O/ELF/COFF. Các module hiện được ghép ở **thời điểm kiểm thử** vì
đường AOT còn thiếu import native; executable compiler chưa tự đọc đối
số CLI hay tự biên dịch lại toàn bộ nguồn compiler. Cần mở rộng tập ngôn
ngữ/IR, codegen và driver native, sau đó chứng minh compiler executable
Stage 1 → Stage 2 byte-for-byte/parity mà không cần VM C++ lúc chạy.

**Cập nhật trình biên dịch native Stage 1 (17/10/2026):** Các giới hạn
được ghi trong hai đoạn trên phản ánh phiên bản parser/IR ban đầu. Hiện
`scripts/quality/checknativedriver.cmake` nối tám module V++ native và
dùng compiler bootstrap Stage 0 tạo executable compiler Stage 1. Stage 1
tự đọc mã nguồn, token hóa, parse các hàm i32, sinh IR, chuyển IR sang mã
máy ARM64, đóng gói object Mach-O và liên kết executable. Pipeline này
chạy bằng mã native V++ cùng các intrinsic runtime và linker nền tảng,
không gọi compiler C++ để biên dịch chương trình đầu vào.

Stage 1 hỗ trợ nhiều hàm, đệ quy, gọi hàm với tham số truyền trên stack,
nhánh và vòng lặp của tập con i32. Bộ phân tích cú pháp chấp nhận trả về
sớm trong nhánh, xác định trường hợp cả hai nhánh đều trả về, hỗ trợ biến
cục bộ trong khối, `thoát;` và `bỏ qua;` ở vòng lặp lồng nhau. Hai lệnh
điều khiển vòng lặp được hạ thành `JMP` sau khi xác định vị trí update/exit;
`bỏ qua` vẫn chạy bước update của vòng lặp. Lệnh sau nhánh mà mọi đường đi
đều `trả về` bị từ chối như mã không thể tới. CTest
`vpp-native-compiler-driver` đã PASS (169,45 giây, 17/10/2026), gồm
fixture nhánh trả về, vòng lặp lồng nhau, `thoát` và `bỏ qua`.
CTest `vpp-native-parser-ir` cũng PASS (275,95 giây) sau khi nâng
timeout dựng parser từ 180 lên 300 giây và cập nhật fixture cho phạm vi biến
trong nhánh cùng quy tắc từ chối mã không thể tới.

Đã bỏ hai giới hạn 32 hàm ở bảng khai
báo và bước kiểm tra CALL. Số hàm vẫn chịu hạn mức bộ đệm byte 1 MiB và
giới hạn kích thước mã máy; đây chưa phải số lượng vô hạn. Hàm điểm vào
`chính()` có thể nằm ở đầu, giữa hoặc cuối tệp. Codegen xác định offset
thực của nó và Mach-O writer ghi offset tương ứng cho symbol `_main`, còn
ID của các lời gọi vẫn dựa trên thứ tự khai báo. Bộ kiểm thử driver có
fixture 64/65 hàm, lời gọi tới/lùi, main ở cuối và trùng định danh sau
vị trí 32. Các trường hợp sai phải giữ nguyên tệp output đã có.

**Native executable compiler Stage 1 → Stage 2 → Stage 3 đã đạt
(17/10/2026):** Stage 0 biên dịch mười module native thành executable Stage 1.
Các thế hệ sau dùng `gói/biên dịch/bản địa/bộ biên dịch.vi` làm điểm vào;
module này chứa các lệnh `nhập` trỏ tới các module compiler, gồm
`module_loader.vi` và `module_compile.vi`. Stage 1 tự đọc từng tệp,
giải import đệ quy bằng V++, tạo archive trong bộ nhớ với các bản ghi
`[độ dài i32, mã nguồn tệp]`, giữ riêng từng đơn vị thay vì ghép
chúng thành một chuỗi nguồn. `module_compile.vi` thực hiện hai lượt:
đăng ký tên/arity và function ID trên toàn đồ thị, sau đó tokenize/parse
và hạ IR riêng từng module. Các IR function được nối theo ID vào
codegen/linker ARM64 đang dùng để tạo executable Stage 2.
Stage 2 tiếp tục biên dịch đúng cùng điểm vào đa tệp
thành executable Stage 3, Stage 3 sinh Stage 4 và biên dịch được chương
trình mới; các executable đều chạy trực tiếp trên macOS mà không khởi động
VM C++ trong những vòng sau Stage 0.

Để khép chuỗi, parser native hỗ trợ gọi 17 intrinsic runtime, chuỗi
literal UTF-8, biểu thức gọi hàm như câu lệnh, `lặp (...; ...; i++)`/
`i--`, nhánh `hoặc nếu` và phạm vi từ vựng của biến tạo trong vòng lặp.
Codegen phát lệnh gọi runtime theo ABI C ARM64, lưu relocation ngoài;
Mach-O writer ghi symbol chưa xác định và `ARM64_RELOC_BRANCH26`. Runtime
C nhỏ đảm nhiệm byte buffer, đối số lệnh, I/O và gọi `clang` để liên kết,
đồng thời được ghép vào executable đích khi có relocation ngoài.

Lỗi khó phát hiện cuối cùng nằm ở hằng lệnh `MOVK` ARM64 trong
`sinh mã hằng số`: immediate cao đã bị dịch sai làm Stage 2 ghi CPU type
Mach-O `0x0000000C` thay vì `0x0100000C`. Đã sửa base thành
`0x72a00009` và kiểm tra lại các giá trị âm/dương 32 bit. Các ca probe
`wide`, `negative`, `utf8`, `loop` và `else-if` đều chạy ra `52` khi
biên dịch bằng Stage 2, Stage 3 và Stage 4. SHA-256 của vùng mã máy
`__TEXT,__text` của ba compiler này đều là
`da28894a17c7db1d8312b54a1b26ca7dbbc3dce3d11bead963d8cb1b7d36cc73`;
hash toàn bộ Mach-O có khác biệt trong vùng UUID/chữ ký liên kết.
`scripts/quality/checknativedriver.cmake` được mở rộng để chạy bootstrap
native Stage 1 → Stage 2 → Stage 3 → Stage 4, thử biên dịch/chạy chương
trình với cả ba compiler Stage 2/3/4. Script
`scripts/quality/check_native_bootstrap_text.cmake` đọc trực tiếp section
`__TEXT,__text` từ Mach-O ARM64 của ba compiler và kiểm tra mã máy
giống từng byte; không so toàn bộ file vì metadata do linker sinh.
Biên dịch trực tiếp bằng Stage 1 đã tạo được Stage 2 → Stage 3 → Stage 4;
ba vùng mã máy đều 147.908 byte, cùng SHA256 như trên. Script kiểm tra
phát hiện được sai lệch khi một byte trong `__text` bị thay đổi.

Module loader `gói/biên dịch/bản địa/nạp mô đun.vi` cho phép `nhập tên;`
và `nhập thư_mục/tên;` ở đầu tệp, tìm `.vi` tương đối với module đang
nhập, loại bỏ import trùng, phát hiện vòng lặp import, module thiếu hoặc
cú pháp import sai. Kiểm tra chạy trực tiếp bằng compiler Stage 1:
import lồng nhau/nhập trùng và tệp điểm vào chỉ có import đều trả về 52;
cycle, module thiếu và import lỗi đều bị từ chối với mã 12, không ghi đè
binary có sẵn. Kiểm thử riêng xác nhận tên hàm trùng giữa các module,
sai arity, lời gọi không xác định và thiếu `chính` đều bị từ chối
với mã 14. Các ca này đã được thêm vào
`scripts/quality/checknativedriver.cmake` để kiểm tra hồi quy.

Fixture `src/tests/nativeparserharness.vi` đã cập nhật theo cú pháp `i++`
và `i--` mới, kiểm tra biến vòng lặp ngoài phạm vi và nhánh `hoặc nếu`.
Evaluator IR tham chiếu `gói/biên dịch/bản địa/biểu diễn trung gian.vi` đã nhận `trả về`
sớm trong nhánh, thống nhất với codegen ARM64. CTest
`vpp-native-parser-ir` đã **PASS** (306,47 giây, 17/10/2026) với các
ca mới. Hai CTest trọng tâm native compiler driver và parser/IR đều đạt.

Phạm vi này là **self-hosting native của compiler i32/AOT subset**.
Stage 0 vẫn cần compiler C++ bootstrap; Stage 1 và các thế hệ sau vẫn
liên kết với runtime C, libc và toolchain hệ điều hành. Compiler native
chỉ hỗ trợ import ở đầu tệp với định danh đường dẫn tương đối, tối đa
12 cấp và không có bí danh/namespace. Frontend hiện đã parse từng đơn vị
độc lập và dùng bảng ký hiệu toàn cục. Backend hiện phát object riêng
theo module, liên kết theo symbol và có cache IR/object như mô tả ở đầu
tài liệu. Compiler native
chưa nhận toàn bộ ngôn ngữ V++ (OOP, lambda, FFI tổng quát), nên chưa
thay thế compiler production/CLI C++.

Hiện subset AOT tự chủ hỗ trợ hàm i32, biến cục bộ và số lượng tham số động,
biểu thức số học/so sánh, if/else, for, break/continue, return và gọi hàm
trực tiếp có relocation. Những toán tử hoặc câu lệnh ngoài tập hỗ trợ sẽ
báo lỗi; không có fallback ngầm sang C++ compiler. Ví dụ:

```sh
build/bin/vpp-cli build --target aarch64-apple-darwin --emit-obj-selfhost \
  sum src/tests/nativeaotsmoke.vi /tmp/sum.o
```

Khi chạy CLI từ thư mục khác repository, cấu hình `VPP_HOME` trỏ tới thư
mục chứa `gói/` đã cài; bộ phân giải import dùng vị trí này để tìm các
thư viện compiler V++. Kiểm thử `vpp-native-selfhost-host-link` thực hiện
chính trường hợp đó, biên dịch độc lập các object, liên kết với harness C,
chạy các giá trị biên của vòng lặp, tham số che tên hàm, và xác minh
phép chia chưa hỗ trợ được từ chối mà không tạo object. Fixture
`kiemtranativetuchu.vi` kiểm tra parser/IR/encoder V++ trong interpreter
và JIT qua `run_tests.sh`.
Ngày 14/10/2026, smoke host-link mới đạt trên macOS ARM64, kể cả khi
source được sao chép ra thư mục build và import bắt buộc qua `VPP_HOME`.
Năm CTest đối chiếu object, machine byte, host-link nhánh/CALL và
relocation cũng đã đạt (5/5). Regression `kiemtramacdinh.vi` xác minh
tham số mặc định âm `-1` và `-2147483648` trên interpreter và JIT.

Riêng đường native tổng quát `--emit-raw`/`--emit-asm`/`--emit-obj` trên
x86-64 và AArch64, bước hạ IR đã được chuyển sang
`gói/biên dịch/mã máy hạ biểu diễn trung gian.vi`. Đường production gọi
`compileMachineFunctionVpp` để hạ IR và phát byte trong **một lần chạy
VM V++**;
C++ `lowerMachineFunction` được giữ làm oracle đối chiếu từng lệnh.
Đường hàm lá i32 của CLI dùng `compileRawLeafVpp` cũng trong một lượt VM
cho 16 kiến trúc CPU; các API hạ/phát riêng còn phục vụ parity, C++ giữ
oracle và bộ in assembly. Parser triple của
`vpp build --target` đã chuyển sang `biên dịch phân tích đích` trong
`gói/biên dịch/đích.vi` qua `parseTargetVpp`; C++ `parseTarget` vẫn là
oracle và còn phục vụ các đường khác như liệt kê target.
Đường `--emit-obj` đã chuyển **bước đóng gói object** sang
`gói/biên dịch/đối tượng.vi`. Production tích hợp lowering, phát mã máy
và đóng gói object **trong cùng một lượt chạy VM V++** qua
`compileRawLeafVpp` hoặc `compileMachineFunctionVpp`; hàm
`emitRawObjectBytesVpp` vẫn được dùng để đối chiếu độc lập. Bộ ghi V++
nhận byte máy, symbol và relocation, rồi tạo ELF32/ELF64 (16 biến thể
kiến trúc Linux), Mach-O64 (AArch64/x86_64 macOS) hoặc COFF
(i386/x86_64 Windows). C++ `emitRawObjectBytes` được giữ làm oracle;
không có đường tự động quay về bộ ghi C++ nếu V++ báo lỗi.

Kiểm thử `vpp-native-object-parity` so byte-for-byte với oracle C++
trên 38 ca: 32 ca bộ ghi độc lập gồm 16 kiến trúc ELF, ARMv7 hard-float GNU/musl, Mach-O,
COFF với symbol dài, năm ca external CALL có hai relocation và năm
ca đầu vào không hợp lệ/kiến trúc chưa hỗ trợ relocation; thêm 6 ca
đường production tích hợp hạ IR + phát byte + ghi object một lần chạy VM,
đối chiếu độc lập với encoder và object writer C++ (hàm lá RISC-V64,
i386/COFF, AArch64/Mach-O; hàm có CALL trên AArch64/Mach-O,
x86-64/ELF và x86-64/COFF). Lần chạy 13/10/2026: **38/38 đạt**.
Đây là parity của các ca được kiểm thử;
chưa đủ để chứng minh mọi object hợp chuẩn trên mọi toolchain.
Trên macOS ARM64, `vpp-raw-object-host-link` và
`vpp-native-control-flow-host-link` và `vpp-native-calls-host-link`
đã liên kết/chạy thành công với
object V++ (13/10/2026). Việc thực thi chéo trên các CPU khác
cần toolchain và runner của hệ đích.

Frontend compiler self-host toàn phần vẫn chưa hoàn thành.
Các API `biên dịch phân tích`, `biên dịch mã trung gian` và `biên dịch tệp`
trong `gói/biên dịch/chính.vi` vẫn phụ thuộc native
`bien_dich_phan_tich_vm` (`src/runtime/native/vm_native_compiler_helpers.cpp`).

Parser độc lập của V++ được triển khai tại:

- `gói/biên dịch/từ vựng.vi`: lexer UTF-8.
- `gói/biên dịch/biểu thức.vi`: AST biểu thức (có postfix `++` / `--`).
- `gói/biên dịch/chương trình.vi`: AST câu lệnh và chương trình.
- `gói/biên dịch/ngữ nghĩa.vi`: các kiểm tra ngữ nghĩa khởi đầu.

API mới `biên dịch phân tích chương trình tự chủ(mã_nguồn)` được xuất từ
`gói/biên dịch/chính.vi`; nó **không** gọi primitive C++ tạo AST/bytecode.
AST được biểu diễn bằng map/list; các bước IR/bytecode bootstrap tự chủ
đã có phiên bản giới hạn, mô tả tại phần cuối tài liệu.

### Những cấu trúc được hỗ trợ

Parser câu lệnh hiện đọc `nhập`, hàm có tên nhiều từ và tham số đơn giản,
khối `{}`, `trả về`, `in`, khai báo `biến`, biểu thức, `nếu` / `hoặc nếu` / `hoặc`,
`lặp(init; condition; update)`, `thoát`, `bỏ qua`, `chọn` / `ca` /
`mặc định`, `thử` / `bắt lỗi` và `ném`.
Semantic pass phát hiện trùng tên hàm/tham số, `trả về` ngoài hàm,
`thoát` ngoài vòng lặp/`chọn`, `bỏ qua` ngoài vòng lặp, nhãn `ca`
không hợp lệ, nhiều nhánh `mặc định` và đích gán/cập nhật không hợp lệ.
Kiểm tra bí danh `nhập ... như ...` được thực hiện bằng V++ theo
`SymbolSpace::Module` của C++: hai bí danh giống nhau trong **cùng một
khối từ vựng** bị báo lỗi; các khối, hàm hoặc nhánh khác nhau có bảng tên
riêng. Bí danh module được kiểm tra độc lập với tên biến và hàm;
`nhập` không có `như` không tạo bí danh tường minh.
Semantic V++ còn phát hiện gán `=`, gán kết hợp và `++`/`--` vào tên
hàm toàn cục theo luật `SemanticBuilder::declareAssignmentTarget` của C++.
Tham số hoặc biến cục bộ khai báo trong cùng hàm có quyền che tên hàm;
việc kiểm tra được thực hiện sau khi thu thập khai báo biến trong thân.

**Chưa hỗ trợ:** các dạng vòng lặp khác;
`lớp`, `giao diện`, FFI và các dạng biểu thức chưa có trong
parser V++. Trường hợp chưa được triển khai phải báo lỗi rõ ràng thay vì
ngầm chuyển sang compiler C++.

### Kiểm thử

`src/tests/kiem_tra_bien_dich_chuong_trinh_vi.vi` kiểm tra AST, nhánh điều
kiện, vòng lặp, lỗi cú pháp và lỗi ngữ nghĩa. Đã đăng ký trong
`run_tests.sh` cho cả interpreter và JIT.
Fixture còn đối chiếu với semantic C++ khi hai `nhập ... như ...` trùng
bí danh ở cấp chương trình hoặc có bí danh khác nhau; kiểm tra trùng
trong một khối, dùng lại ở các nhánh/hàm độc lập, và namespace module
riêng với tên hàm/biến. Phần đối chiếu hiện chỉ xác nhận những ca được
kiểm thử, chưa thay thế phân giải module và phạm vi tên đầy đủ của C++.
Tám ca đối chiếu thêm kiểm tra ghi vào tên hàm, gán kết hợp, postfix,
biến cục bộ khai báo sau phép gán và tham số trùng tên hàm. Ngày
14/10/2026, fixture parser/semantic đạt **78/78** trên interpreter;
đường JIT được kiểm tra cùng golden output.

Phát hiện một bất nhất ở oracle: semantic C++ cho phép tham số che tên
hàm toàn cục, nhưng `supportsFunction` của direct IR trước đây loại bỏ
trường hợp này và báo `unsupported direct IR region`. Direct emitter C++
đã được sửa để gắn slot VM theo SymbolId cho tham số, biến cục bộ và biến
`bắt lỗi`; đọc/ghi, gọi gián tiếp và closure dùng đúng ràng buộc kể cả khi
trùng tên hàm toàn cục. Bộ phát còn truyền SymbolId của receiver ngầm
`mình`/`gốc` vào slot tham số của phương thức; trước bản sửa, phép gọi
`gốc.value()` trong fixture kế thừa thất bại do đọc sai slot. Đã bỏ điều
kiện từ chối direct IR lỗi thời với `bắt lỗi` trùng tên hàm. Fixture
`src/tests/kiemtrathamsotrung.vi` hiện có **16 phép kiểm tra**, gồm hai
khối `bắt lỗi` trùng tên, `bắt lỗi` che tham số/biến cục bộ, catch lồng
nhau và kiểm tra hàm toàn cục vẫn gọi được; chạy đạt trên interpreter và
JIT. Fixture kế thừa `kiem_tra_ke_thua.vi` cũng chạy
đúng ở cả hai chế độ.

Bộ hạ IR thuần V++ tại `gói/biên dịch/biểu diễn trung gian.vi` hiện có hai lượt phân giải
ràng buộc: thu thập khai báo/tham số và đích gán implicit theo phạm vi
hàm/toàn cục, sau đó gắn **SymbolId** vào lệnh đọc, ghi, gọi gián tiếp,
nhãn `ca` và các biến `bắt lỗi` thuộc scope riêng. Bộ phát mã
`gói/biên dịch/phát mã.vi` dùng SymbolId làm khóa slot; slot của một
catch không còn va chạm với tham số, biến ngoài, hàm toàn cục hay catch
anh em/lồng nhau cùng tên. Khóa dự phòng bằng tên văn bản vẫn dành cho
các tên chưa được phân giải; đường cũ theo ID hàm + tên cục bộ đã dọn bỏ.
Đối chiếu bytecode C++/V++ trong `kiem_tra_ir_bytecode_tu_chu.vi` hiện
có **124 phép kiểm tra**, gồm cả trường hợp che tên trong `ca`, chạy đạt
trên interpreter và JIT (14/10/2026). Đây là phạm vi syntax bootstrap
đã hỗ trợ, chưa chứng minh tương đương toàn bộ semantic C++.
Việc này sửa đường oracle/production C++ hiện hành; **frontend CLI vẫn
chưa chuyển hoàn toàn sang V++**.

### Các bước để chuyển CLI sang V++

1. Mở rộng AST biểu thức/chương trình tới toàn bộ cú pháp V++ hiện hành;
   so sánh snapshot AST C++ và V++ trên fixture thực.
2. Hoàn chỉnh phân giải symbol/scope, kiểu, imports và chẩn đoán ngữ nghĩa.
3. Hoàn thiện IR/bytecode V++ và đối chiếu toàn bộ với compiler C++.
4. Bootstrap binary compiler V++ từ C++, rồi dùng compiler V++ biên dịch
   lại mã nguồn compiler V++ và chạy cùng một bộ kiểm thử.
5. Khi đã chứng minh tương đương mới thay đường `compilePipeline` mặc định
   trong CLI. Đường opt-in `--compiler=vpp` đã được tích hợp ở giai đoạn hiện tại.

**Đã sửa giới hạn direct IR của phép đọc chỉ số trên kết quả lời gọi hàm.**
Trước đây `supportsValue` chỉ chấp nhận `LoadName` hoặc `Index` làm đối
tượng được đọc, dù `emitValue(Index)` đã hỗ trợ lấy phần tử từ kết quả nằm
trên VM stack. Bây giờ nhánh đọc chấp nhận thêm `Call`, `CallDynamic`,
`Intrinsic` và `ForeignCall`; từng lời gọi vẫn phải vượt qua kiểm tra
`supportsValue` của chính nó. Nhánh `StoreIndex` giữ nguyên điều kiện cũ.

Ca tái hiện đã được sửa:

```text
nhập gói/bộ sưu tập/từ điển;
hàm chính() {
    danh_sách = [1, 2];
    x = lấy từ điển({"a": danh_sách}, "a", [])[0];
    in x;
};
```

Kiểm thử hồi quy `src/tests/kiem_tra_doc_chi_so_ket_qua_goi.vi` bao phủ
`lấy từ điển(...)[0]`, chuỗi lấy chỉ số trên kết quả gọi hàm, hàm V++ tự định
nghĩa trả về list, và phép lấy chỉ số trên biến. Fixture parser
`kiem_tra_bien_dich_chuong_trinh_vi.vi` cũng trực tiếp dùng cú pháp
`lấy từ điển(...)[0]`. Cả hai fixture được đăng ký cho interpreter và JIT.

Kiểm chứng tại thời điểm sửa: build `vpp-cli` thành công; bốn fixture
biên dịch và lấy chỉ số cho kết quả đúng ở cả hai chế độ (8/8 lượt chạy).
Ba bài CTest hardening qua; CTest integration toàn bộ vượt quá giới hạn
120 giây trong bước chạy fixture batch, nên chưa kết luận toàn bộ bộ test
đã qua.

Việc sửa backend C++ này độc lập với lộ trình chuyển compiler sang V++;
đường production vẫn sử dụng `compilePipeline` trong C++.

## IR và bytecode thuần V++ (bản bootstrap)

Hai mô-đun mới:

- gói/biên dịch/biểu diễn trung gian.vi: hạ AST từ parser/semantic V++ thành một chuỗi
  lệnh IR stack-based, có chẩn đoán tường minh nếu chưa hỗ trợ cú pháp.
- gói/biên dịch/phát mã.vi: phát opcode VM, quản lý slot biến, StringPool,
  và bytecode riêng cho từng hàm. Hoàn toàn viết bằng V++.

API nhập trực tiếp từ hai mô-đun tương ứng:
biên dịch hạ IR thuần(mã_nguồn) và
biên dịch phát mã trung gian thuần(mã_nguồn).

Subset đã triển khai: số nguyên/thực, bool/null, chuỗi có escape
(cả dấu nháy đơn/kép và `\\0`, theo `stripQuotes` của C++),
biến, phép toán, so sánh, logic, đọc chỉ số, gán tên, gán kết hợp,
list/map literal tĩnh và động (kể cả lồng nhau, key map dạng tên/chuỗi),
khai báo biến (có/không có initializer), `nếu`/`hoặc nếu`/`hoặc` (kể cả lồng
nhau), `chọn` / `ca` / `mặc định` (nhãn số nguyên, chuỗi, tên),
`thử` / `bắt lỗi` / `ném` (kể cả lồng nhau), `thoát` trong `chọn`,
in, trả về, khối, hàm có tham số cơ bản, lời gọi hàm trực tiếp theo tên
(gồm gọi đệ quy/gọi trước khai báo), và `lặp(init; condition; update)`
với `thoát` / `bỏ qua` kể cả lồng nhau. Lời gọi tên chưa phân giải dùng
`OP_GOI` với chỉ số StringPool âm; lời gọi qua slot dùng `OP_GOI_GIAN_TIEP`.
Gán kết hợp theo đúng
direct IR C++ là đọc biến, phát opcode toán học tương ứng, sau đó
`OP_TEN_BIEN_ID` và `OP_GAN`; KHÔNG phát trực tiếp opcode 72–76.
Các dạng gọi phương thức/biểu thức động phức tạp, các vòng lặp khác
và lớp/FFI vẫn chưa được emitter bootstrap hỗ trợ.
Function ID của hàm tên được cấp trước khi phát mã. Linker một đơn vị
và linker đồ thị đã được triển khai bằng V++.

**Đã có đường thực thi artifact tự chủ:** `gói/biên dịch/bộ nạp.vi`
chuyển bytecode map thành ABI bốn trường `[op, toán hạng, chỉ số, giá trị]`,
đưa bảng hàm theo function ID và runtime name index, StringPool dùng chung,
cùng initializer theo thứ tự dependency sang primitive `bytecode_chay_vm`.
Primitive này (`src/runtime/native/vm_native_compiler_helpers.cpp`) chỉ nạp
ABI và chạy VM; không biên dịch lại mã nguồn bằng C++.

API có thể nhập trực tiếp:

- `biên dịch chạy sản phẩm biên dịch(artifact)` cho artifact linker V++ đã tạo.
- `biên dịch chạy chương trình tự chủ(mã_nguồn)` cho một đơn vị.
- `biên dịch chạy chương trình mô đun tự chủ(điểm_vào, nguồn)` cho đồ thị.

Loader kiểm tra dạng artifact, bốn trường lệnh, function ID liên tiếp,
StringPool/runtime name index, initializer trùng/thiếu tên. Verifier VM
tiếp tục kiểm tra opcode/operand trước khi thực thi initializer.
`src/tests/kiem_tra_chay_bytecode_tu_chu.vi` đối chiếu đầu ra với compiler
C++ cho chương trình đơn/đa mô-đun, đồng thời bao phủ import lặp,
initializer, trùng tên hàm, StringPool xen kẽ, `chọn/ca`, opcode lỗi và
metadata không hợp lệ. Ca này được đăng ký interpreter/JIT trong `run_tests.sh`.

**Giới hạn còn lại:** chỉ subset parser/emitter/linker V++ được hỗ trợ;
chưa có định dạng serialize bytecode trên đĩa, metadata debug/source hoàn
chỉnh, FFI/class parity hay bootstrap compiler toàn phần. CLI production
vẫn dùng C++ `compilePipeline` cho đến khi đủ bao phủ và parity.

`gói/biên dịch/mã thao tác.vi` lưu bảng mã theo enum `Opcode` trong
`src/include/vm/instruction.h`. Chạy `ctest --test-dir build -R '^vpp-opcode-abi$'` để kiểm tra
đồng bộ số opcode. Bộ sinh Python trước đây đã bị gỡ bỏ; kiểm tra này
không kiểm chứng toàn bộ descriptor nội tại/alias.
Các module phát mã V++ dùng `biên dịch mã thao tác("OP_...")` thay vì
tự đặt số opcode; ABI phụ thuộc đúng giá trị C++ đã định nghĩa.

Fixture `src/tests/kiem_tra_ir_bytecode_tu_chu.vi` có **113 assertion**,
bao gồm so sánh từng bộ bốn trường bytecode
`op`, `toán hạng`, `chỉ số`, `giá trị` với oracle `biên dịch mã trung gian`
do compiler C++ sinh. Các trường hợp đối chiếu có phép gán thường,
gán kết hợp, điều kiện, `chọn` và `thử` (bao gồm offset nhảy),
và khai báo biến/initializer; semantic V++ cũng
từ chối khai báo trùng tên ở cùng phạm vi hàm/global như C++.
Oracle hiện phơi bytecode top-level **và** từng thân hàm, nên fixture
so cả opcode lẫn ba toán hạng trong hàm với đường C++.

Kiểm chứng 12/10/2026: **113/113 assertion** trả `1` trong cả interpreter
và JIT khi chạy `build/bin/vpp-cli`; bytecode khớp đầy đủ bốn trường,
kể cả ca `chọn {}` rỗng, lời gọi hàm, vòng lặp, `thoát`, `bỏ qua`,
khối/try/switch lồng nhau. Script đồng bộ opcode C++ ↔ V++ đã qua.
C++ `supportsInstruction(Switch)` trước đây từ chối switch rỗng mặc dù
emitter có thể sinh đúng `OP_CHON` và `OP_KET_THUC_CHUYEN`;
điều kiện thừa đã được xóa và `vpp-cli` được biên dịch lại thành công.
Fixture parser chương trình đạt 38/38 ở interpreter/JIT, fixture
opcode `chọn/ca` khớp expected, ba bài CTest hardening đều đạt.
Các kết quả này chưa bao gồm toàn bộ CTest integration.

**Collection literal đã hạ bằng V++** tại `biểu thức.vi` → `ngữ nghĩa.vi`
→ `ir.vi` → `phát mã.vi`. Danh sách/từ điển chỉ chứa hằng được mã hóa theo
`src/include/vpp/bytecode/literal_wire.h` (separator 0x1e/0x1f,
tag i/d/s/n/l/m, escape field), lưu StringPool và phát `OP_LIST_LITERAL`
(88) / `OP_MAP_LITERAL` (85) với `chỉ số >= 0`. Biểu thức động đặt phần tử
(hoặc cặp key/value) lên stack, phát cùng opcode với `chỉ số = -1` và
`toán hạng = số phần tử/cặp`. Map key là chuỗi/tên; tên được mã hóa như
chuỗi, không đọc biến. Đầu vào sai và trường hợp chưa hỗ trợ sẽ ném lỗi.

Fixture `src/tests/kiem_tra_tap_hop_bien_dich_tu_chu.vi` so cả bốn trường
opcode top-level/thân hàm với oracle C++, đối chiếu kết quả chạy bằng
VM do V++ biên dịch và đường C++, kiểm tra wire format StringPool,
opcode tĩnh/động, collection lồng nhau và lỗi cú pháp. Đã đăng ký
interpreter và JIT trong `run_tests.sh`.

**Đã chuyển decoder source string sang V++:** `biên dịch phát mã tách chuỗi`
theo cùng quy tắc `stripQuotes` của C++ (cả escape `\\n`, `\\r`, `\\t`,
`\\\\`, `\\'`, `\\"`, `\\0`, escape không nhận diện và hai kiểu dấu nháy).
Emitter giải mã trước khi đưa vào StringPool và trước khi đóng gói list/map
literal tĩnh; không giải mã hai lần. Fixture
`src/tests/kiem_tra_escape_bien_dich_tu_chu.vi` kiểm tra decoder, bytecode
ABI bốn trường, kết quả VM so với oracle C++, collection tĩnh/động
và byte NUL; chạy cả interpreter/JIT trong `run_tests.sh`.

Các bước tiếp: triển khai lời gọi/phương thức động và syntax còn thiếu,
kiểm thử đầy đủ module/lớp/FFI, sau đó chứng minh parity
trước khi chuyển CLI production.

### Linker/function registry cho một chương trình thuần V++

`gói/biên dịch/liên kết.vi` bổ sung API
`biên dịch liên kết mã trung gian thuần(artifact)` và
`biên dịch chương trình tự chủ(mã_nguồn)`; API thứ hai gọi tuần tự
parser → semantic → IR → emitter → linker đều bằng V++.

Linker xây bảng hàm `{tên, id, tên index, bytecode}` từ StringPool và
slot/function ID đã predeclare, kiểm tra mọi `OP_HAM` khớp metadata,
không thiếu/trùng khai báo, và thân hàm có khối mở/đóng. Theo
`Emitter::emitProgram(true)` bên C++, linker tìm entrypoint `chính`,
sau đó đến alias `main`, sinh `OP_GOI(0, slot, 0)` nếu tồn tại và luôn
thêm `OP_DUNG_CHUONG_TRINH(0, 0, 0)`. Nó giữ nguyên bytecode top-level
và thân hàm và **ném lỗi** khi metadata không nhất quán.

Fixture `src/tests/kiem_tra_lien_ket_tu_chu.vi` so bốn trường opcode
top-level với snapshot C++ trước liên kết, kiểm tra vị trí nối
entrypoint, ưu tiên `chính`/`main`, và các lỗi metadata. Có đăng ký
fixture cho interpreter và JIT trong `run_tests.sh`.
Kiểm chứng 12/10/2026: fixture mới đạt 20/20 phép kiểm tra khi chạy
interpreter và JIT; đầu ra interpreter khớp file expected, script đồng
bộ opcode qua, và 3/3 CTest hardening đạt. Toàn bộ integration suite
chưa chạy trong lượt này.

Giới hạn: hiện mới liên kết **một đơn vị biên dịch**, không có import,
module initializer, relocation liên mô-đun hay debug/source metadata.
Artifact là dữ liệu V++ trong VM đang chạy; chưa có bộ nạp để biến
artifact thành một phiên VM độc lập. CLI production vẫn chạy compiler
C++ và các API snapshot gốc vẫn sử dụng native, nên **chưa self-host
hoàn toàn**.

### Đồ thị import local-source thuần V++ (bước chuẩn bị đa mô-đun)

`gói/biên dịch/đồ thị mô đun.vi` triển khai
`biên dịch dựng đồ thị nguồn thuần(điểm_vào, nguồn)`. `nguồn` là map
định danh POSIX tương đối đã chuẩn hóa (không gồm đuôi `.vi`) đến mã
nguồn của từng mô-đun, được cung cấp tường minh bởi caller. Graph quét
AST import **top-level**, giải đường dẫn nhập tương đối từ thư mục của
module đang nhập, duyệt DFS theo thứ tự câu lệnh, phát hiện chu trình
kèm đường đi, ghi `load` / `duplicate_no_op`, và trả thứ tự khởi tạo
**postorder** (mô-đun phụ thuộc trước importer). Parser thuần V++ nhận
`nhập đích như bí_danh` và `công khai nhập đích như bí_danh` và giữ
metadata alias/re-export trên AST. Nhập có đuôi `.vi` bị từ chối.

Hợp đồng này lấy nguyên tắc dedup/DFS từ `src/compiler/module_graph.cpp`
và thứ tự `moduleInitializers` từ `src/compiler/compileRegistry.cpp`,
nhưng chưa thay các bộ giải quyết đường dẫn, package lookup, semantic
cross-module hay registry của C++: **không đọc file trực tiếp**, không
tự giả định module vắng mặt là package, chỉ chấp nhận key nguồn POSIX
tương đối có trong map (không hỗ trợ Windows path ở phiên bản này).
Artifact graph hiện được dùng để dựng sổ hàm toàn phiên và đầu vào
`biên dịch liên kết đồ thị thuần`. Initializer của từng dependency
được giữ riêng (không nối bytecode), cùng bảng slot theo emitter;
chưa có bộ nạp thực thi các initializer trong một VM độc lập.

Fixture `src/tests/kiem_tra_do_thi_module_tu_chu.vi` bao phủ import
đa cấp, import lặp, alias, re-export, thứ tự khởi tạo, chu trình,
module vắng mặt và chuẩn hóa path. Đã đăng ký cả interpreter và JIT
trong `run_tests.sh`.
Kiểm chứng lượt này: 32/32 assertion graph đạt ở interpreter;
fixture parser và linker cũ khớp expected, fixture IR đạt 113/113;
kiểm tra đồng bộ opcode (trước đây dùng công cụ Python), `bash -n run_tests.sh`,
`git diff --check` và 3/3 CTest hardening đều đạt. Kiểm chứng bổ sung
12/10/2026: fixture graph cũng đạt 32/32 trong JIT
(`VPP_ENABLE_JIT=1`), đầu ra khớp file expected.

### Sổ đăng ký function ID và export nhiều mô-đun bằng V++

`gói/biên dịch/sổ đăng ký.vi` thêm hai API độc lập với compiler native:

- `biên dịch đăng ký hàm IR thuần(ir)` cấp ID hàm từ 0, StringPool
  deduplicate và bảng slot/tên hàm cho **một** emitter. Hàm
  `biên dịch phát mã trung gian thuần` đã chuyển bước predeclare sang API này;
  các pass còn lại vẫn chạy bằng V++ và vẫn phát bộ bốn trường opcode.
- `biên dịch đăng ký hàm đồ thị thuần(đồ_thị)` nhận đồ thị import V++
  và cấp function ID **toàn phiên** theo thứ tự predeclare: hàm trực tiếp
  của importer trước, sau đó duyệt DFS qua các module import. Khởi tạo
  module vẫn theo postorder. StringPool dùng chung, còn từng module
  giữ bảng `trực tiếp`, `nhìn thấy`, `xuất` gắn function ID. Imports
  có `như` tạo tên hiển thị `bí_danh.tên`; `công khai nhập` đưa các tên
  đó vào export surface, kể cả re-export nhiều cấp. Nhập lặp cùng
  function ID không đăng ký lại hàm. Hai hàm có cùng tên trong các
  module khác nhau nhận **hai ID**, nhưng tên gốc được deduplicate
  trong StringPool. Xung đột cùng tên hiển thị trỏ tới ID khác nhau
  và trùng tên hàm trong một module đều ném lỗi rõ ràng.

Giới hạn: sổ đã được emitter và linker đồ thị sử dụng để cấp function
ID toàn phiên, giải lời gọi tên liên mô-đun và giữ slot riêng theo
emitter. Bảng tên hiển thị vẫn chưa được nạp vào VM qua
`CompilationRegistryState::setFunctionNameIndex`, chưa có loader,
metadata debug/source hoặc đường production thay C++. Parser bootstrap
chưa nhận `công khai hàm` / hàm private nên mọi hàm trực tiếp đều được
xem là export mặc định. Những giới hạn đó phải được xử lý trước khi
coi bộ biên dịch là self-host hoàn toàn.

Fixture `src/tests/kiem_tra_so_ham_module_tu_chu.vi` kiểm tra 43 điều
kiện: ID, StringPool, thứ tự module, alias, re-export nhiều cấp,
trường hợp nhập lặp, thứ tự giả mạo, cùng tên ở hai module, xung đột namespace, và
đối chiếu sổ IR với emitter một đơn vị. Đăng ký cả interpreter/JIT
trong `run_tests.sh`.

### Emitter và linker đồ thị dùng chung function ID

`biên dịch hạ IR từ cây thuần(cây, bỏ_qua_nhập)` nhận AST đã parse để
không token hóa lại từng mô-đun. Chỉ được bỏ qua câu `nhập` khi graph
đã giải và đăng ký tất cả phụ thuộc. `biên dịch phát mã trung gian IR có sổ`
nhận registry toàn phiên, dùng ID chính xác thay vì chỉ số hàm cục bộ,
chia slot theo mô-đun và dùng StringPool chung. Những tên imported được
giải bằng danh sách `nhìn thấy`, kể cả alias và tái xuất. Tên gọi
không phân giải trong mô-đun gây lỗi tường minh.

`gói/biên dịch/liên kết.vi` bổ sung
`biên dịch chương trình mô đun tự chủ(điểm_vào, nguồn)` để nối
graph → registry → AST/IR → emitter → linker thuần V++.
`biên dịch liên kết đồ thị thuần(đồ_thị)` trả bytecode root có
entrypoint/HALT, bảng hàm toàn phiên, StringPool dùng chung, và
`mô đun khởi tạo` chứa từng initializer cùng slot của mô-đun theo thứ
tự postorder. Linker xác nhận `OP_HAM` đúng một lần cho từng ID, đúng
metadata tên và mô-đun, cùng thân hàm có khối mở/đóng. Không nối
initializer vào bytecode root bởi chỉ số jump và slot là cục bộ.

`src/tests/kiem_tra_lien_ket_module_tu_chu.vi` chứa **71 phép kiểm**
độc lập, kiểm tra đủ bốn trường của các opcode quan trọng, thứ tự
initializer, alias/tái xuất đa cấp, tên trùng trong hai mô-đun,
import lặp, root entrypoint, và lỗi tên chưa phân giải. Đã đăng ký
interpreter/JIT trong `run_tests.sh`. Kiểm chứng 12/10/2026:
71/71 đúng trên interpreter và JIT, output khớp expected;
script opcode sync, `bash -n`, `git diff --check` và ba CTest
hardening đều đạt. Không suy ra toàn bộ integration suite đã qua.

Đây mới là **artifact dữ liệu** được dựng trong V++ VM đang chạy;
chưa có VM loader, chuyển metadata debug, hay khởi động chương trình
đã biên dịch bằng artifact. Linker V++ đã mô hình hóa tên khai báo
và tên runtime sau alias, nhưng chưa lắp vào loader/VM độc lập.
Do đó CLI sản phẩm vẫn dùng trình biên dịch C++ và chưa thể coi là
self-host hoàn chỉnh.

### Kiểm tra oracle C++ đa mô-đun và sửa lỗi bí danh import trùng

`src/tests/kiem_tra_oracle_module_tu_chu.vi` đối chiếu snapshot bytecode
C++ với artifact V++ trên 3 đồ thị: import thường với re-export,
import một dependency thêm lần nữa dưới alias khác, và import lại
module cha dưới alias khác. So sánh thứ tự/function ID, tên runtime,
bytecode thân hàm và đủ bốn trường `op`, `toán hạng`, `chỉ số`,
`giá trị`; root so phần trước CALL entrypoint/HALT do hai opcode đó
chỉ được linker V++ bổ sung. 9/9 assertion đạt trên interpreter và JIT
(12/10/2026). Oracle C++ **không** xuất initializer dependency nên
chưa khẳng định bytecode initializer parity với snapshot.

Đã xác nhận lỗi gốc C++: `compileImportSpec` từng bỏ qua toàn bộ import
trùng canonical path, làm bí danh mới không vào lookup; `OP_GOI` của
`toán.cộng()` thành chỉ số âm `-7` dù ID thực là `2`. Đã sửa registry
C++ bằng ánh xạ tên import → function ID cho từng alias, độc lập với
`functionNameIndices` (metadata runtime chỉ giữ một tên trên ID).
Import trùng vẫn chỉ khởi tạo dependency một lần. Fixture runtime
`src/tests/kiem_tra_module_alias_trung.vi` kiểm chứng output
`99, 42, 42, 42` ở cả interpreter/JIT. Đây là sửa lỗi oracle/đường
production, không phải chuyển thêm chức năng sang native C++.

Đã sửa sai lệch StringPool giữa C++ và V++ khi literal chuỗi xen kẽ
import. Trước đó `biên dịch đăng ký hàm đồ thị thuần` cấp trước toàn bộ
tên hàm, còn emitter khởi tạo dependency theo postorder, làm lệch chỉ
số chuỗi ở bytecode root (`1, 0, 1` ở ca tái hiện). Trong khi đó,
C++ predeclare hàm của module đang xét và biên dịch dependency đúng
vị trí gặp import. `gói/biên dịch/sổ đăng ký.vi` bổ sung chế độ đăng ký
function ID **hoãn cấp StringPool** cho đồ thị; `gói/biên dịch/biểu diễn trung gian.vi`
giữ lại vị trí import, còn `gói/biên dịch/phát mã.vi` phát đệ quy tại
vị trí đó, theo dõi initializer đã phát, các alias và slot của từng
module. `gói/biên dịch/liên kết.vi` thu initializer theo thứ tự
postorder sau khi root đã được phát, bảo toàn function ID đã cấp.
Đường đăng ký/emitter một đơn vị vẫn giữ hành vi cũ.

`src/tests/kiem_tra_oracle_chuoi_module.vi` dùng oracle C++ và thư
viện fixture `src/tests/fixtures/compiler_oracle/string_order/`, đối
chiếu đủ bốn trường bytecode root và thân hàm, function ID và tên
runtime trong **bốn tình huống**: literal trước/sau import; biến chứa
chuỗi bao quanh import; import lặp dưới hai alias; và khai báo hàm/literal
xen giữa dependency. Sau sửa, **12/12 kiểm tra đạt trên interpreter và
12/12 trên JIT** (12/10/2026). Fixture đã được đăng ký trong
`run_tests.sh` cùng file expected. Oracle C++ vẫn chưa xuất bytecode
initializer của dependency, nên kiểm thử này chưa xác minh đối chiếu
phần initializer. Chưa hoàn tất xác minh toàn bộ CTest integration và
chưa chuyển compiler production từ C++ sang V++.
## Cập nhật socket deadline và runtime AOT (28/10/2026)

VM độc lập dùng V++ để quản lý token, trạng thái timeout và ownership;
ASM ARM64/x86-64 bổ sung `fcntl` + `poll` + `SO_ERROR` cho `connect`,
`poll` cho `accept`, và đặt send/recv timeout trên FD mới được accept.
Deadline sử dụng `CLOCK_MONOTONIC`; khi `poll` bị EINTR, ASM tính
thời gian còn lại rồi thử tiếp, kể cả trạng thái connect đang chờ.
Harness macOS hai kiến trúc kiểm tra SIGALRM trong accept, timeout
không bị kéo dài và descriptor listener vẫn còn dùng được.
Kiểm thử ASM socket có thêm ca deadline `accept` và kiểm tra cờ
blocking sau `connect`, chạy thành công trên macOS ARM64 và x86-64/Rosetta
khi được cấp quyền loopback. Các primitive process spawn/pipe/wait và
UID/GID u32 của đợt trước giữ nguyên.

CTest `vpp-native-vpp-runtime` và `vpp-aot-platform-primitives-asm`
đã PASS, cùng dependency Stage 1 bootstrap (3/3, 28/10/2026).
Runtime AOT `VPP_NATIVE_VPP_RUNTIME` loại hẳn nội dung
`src/runtime/native/vpp_aot_runtime.c` lúc biên dịch; linker/argv,
spawn/wait, bộ đệm và cleanup thuộc V++/ASM trong nhánh này.
Bản C fallback vẫn phục vụ các cấu hình tương thích và toolchain còn
phụ thuộc linker hệ thống. TLS verified cho VM 206–208,
DNS 203–205 và compiler self-hosting Stage 2→3 chưa hoàn tất.
