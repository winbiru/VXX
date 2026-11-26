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
heap và gộp khối trống liên tiếp. Allocator sử dụng **18 bucket free-list**
theo tổng kích thước block (gồm header): <=32, <=48, <=64, <=96,
<=128, <=192, <=256, <=512, <=1024, <=2048, <=4096, <=8192,
<=16384, <=32768, <=65536, <=262144, <=1048576 byte và lớn hơn 1048576 byte.
Các đầu danh sách nhỏ nằm ở VM header slot 19..28; tám bucket lớn bổ sung
nằm ở vùng 4198728..4198759, sau cờ worker. Bộ đếm
`free-list-probes` đo số block đã xét, không đếm số bucket trống.

VM và AOT thu hồi ngay bộ nhớ nội bộ có chủ sở hữu duy nhất khi
kết thúc vòng đời: vùng dữ liệu cũ của vector khi tăng dung lượng, vùng
stack tràn, bảng phạm vi riêng và bản ghi switch/handler của khung đã
kết thúc. Kho giữ tối đa 64 khung rỗng trên mỗi state; khung vượt giới hạn
trở về free-list. Đối số lời gọi trực tiếp từ 1 đến 4 phần tử dùng vùng
nội tuyến trong khung gọi. Tối đa 16 ô biến cục bộ dùng vùng khung;
closure bắt giữ ô sẽ chuyển ô đó lên heap. Cell và giá trị được chia sẻ,
bao gồm đồ thị có chu trình, vẫn do mark/sweep GC quản lý.

Allocator đánh dấu block cấp phát sau lần GC gần nhất bằng epoch -1.
Khi thu hồi sớm một block nội bộ, xóa dấu allocation-start, đưa block
vào free-list và chỉ giảm ngân sách cấp phát nếu block thuộc epoch -1.
Không chạy GC trong helper khi giá trị tạm chưa có root. Thu hồi sớm
chỉ áp dụng khi runtime chứng minh quyền sở hữu vùng dữ liệu; cơ chế này
không thay đổi ngữ nghĩa tham chiếu của mã khách.

Đường worker cũng thu hồi các vùng tạm có chủ duy nhất: bộ đệm serializer
và bảng memo đã thay thế, queue/table dùng để đóng gói hoặc giải mã đồ thị,
snapshot đầu vào sau khi ASM sao chép xong, packet kết quả và bộ đệm đồng hồ.
Chờ vô hạn, poll và chờ worker đã kết thúc không cần cấp phát đồng hồ.
Đồ thị guest được giải mã giữ ngữ nghĩa alias và chu trình như trước.

Đo trên macOS ARM64 ngày 25/11/2026 (`VPP_VM_PROFILE=1`), cùng ảnh
bytecode và đầu ra:

| Workload | Cấp phát trước → sau | GC trước → sau | Free-list probes trước → sau |
| --- | ---: | ---: | ---: |
| Vòng lặp/call, baseline trước thu hồi sớm | 475084 → 385022 | 4 → 2 | 1487329 → 208953 |
| Worker stress, baseline đã có frame pool | 47344 → 46900 | 8 → 1 | 52806 → 67848 |

Tách bucket lớn giảm probes của worker stress từ 269253 (10 bucket,
đã thu hồi buffer worker) xuống 67848. Các số này đo số lần cấp phát,
GC và xét free-list trong heap chính; không phải benchmark thời gian
chạy toàn bộ hệ thống, cũng không bao gồm cấp phát trong heap con.

### Loại bỏ cấp phát qua hạ IR

Linker bật tối ưu mặc định (`VPP_OPTIMIZE_IR=0` để đối chiếu). Bộ hạ IR
chỉ gấp phép `+`, `-`, `*` trong cây hằng nguyên có giới hạn an toàn;
phép đọc `[1, 2, 3][1]` được thay bằng hằng `2` khi mọi phần tử là hằng
nguyên hợp lệ và chỉ số nằm trong biên. Danh sách tạm không cần cấp phát.
Chỉ số động/sai biên, hằng ngoài miền hỗ trợ và phép chia giữ đường xử lý
thông thường. IR thô, metadata editor và lượt xác thực JIT đầy đủ giữ nguyên.

Đo cùng VM macOS ARM64, cùng nguồn `scripts/test/fixtures/ir_optimizer_runtime.vi`
(100.000 lần đọc danh sách tạm), với cùng compiler và bật/tắt tối ưu:

| Chỉ số | Tắt | Bật |
| --- | ---: | ---: |
| Cấp phát heap | 1.398.280 | 298.252 |
| GC | 8 | 2 |
| Free-list probes | 1.160.741 | 69.553 |
| Bytecode VMB (byte) | 1.308 | 1.165 |

Đầu ra VM, AOT và JIT khi bật/tắt giống nhau; đường worker 2 cũng đạt.
Đây là số liệu của fixture chuyên biệt, chưa phải benchmark tổng quát.

### Escape analysis cục bộ và arena đối số (25/11/2026)

`[1, biểu_thức, 3][1]` được hạ trực tiếp thành biểu thức được chọn nếu
chỉ số là hằng hợp lệ và mọi phần tử bị bỏ qua đều là hằng nguyên nhỏ.
Biểu thức được chọn vẫn chạy một lần, giữ alias, tác dụng phụ và lỗi.
Nếu danh sách được mã hóa tĩnh thì chỉ thay thế hằng nguyên nhỏ như trước.
Danh sách có phần tử không được chọn nhưng có lời gọi hoặc phép tính
vẫn đi qua đường dựng danh sách đầy đủ.

VM và AOT dựng danh sách/từ điển động trực tiếp từ vùng toán hạng đang
nằm trong khung. Các tham chiếu gốc được giữ tới khi hoàn tất sao chép,
sau đó xóa các ô đã dùng. Danh sách dài hơn hai phần tử cấp phát vùng
tham chiếu đúng kích thước một lần; map dùng quy tắc chèn và trùng khóa
hiện có. Không tạo vector tham số trung gian hoặc chạy GC giữa helper.

Khung tăng từ 336 lên 352 byte; ô 84 giữ arena đối số cấp phát lười.
Lời gọi guest trực tiếp trong VM, và lời gọi guest trực tiếp, gián tiếp,
phương thức trong AOT dùng bốn ô nội tuyến như trước hoặc arena riêng
64 ô cho 5–64 đối số. Arena nằm trong một block GC được khung cha giữ,
chỉ được khung con mượn khi chạy. Khi khung con kết thúc hoặc bị tháo
vì ngoại lệ, xóa các ô tham chiếu; khi khung sở hữu kết thúc, thu hồi
block ngay. Lời gọi trên 64 đối số vẫn dùng vector heap thông thường.
Host ABI giữ đường cấp phát cũ. Giá trị guest và closure thoát khỏi
khung tiếp tục được GC quản lý.

So sánh phase 5 và phase 6 trên cùng bytecode (tắt tối ưu IR để cô lập
thay đổi runtime), VM macOS ARM64, `VPP_VM_PROFILE=1`:

| Fixture (100.000 vòng) | Cấp phát trước → sau | GC trước → sau | Free-list probes trước → sau |
| --- | ---: | ---: | ---: |
| `scoped_collection_runtime` | 2.084.048 → 1.484.036 | 11 → 8 | 1.829.561 → 1.203.451 |
| `scoped_args_runtime` | 2.000.824 → 1.800.808 | 8 → 6 | 1.770.277 → 1.568.117 |

Bật thêm tối ưu IR phase 6 cho `scoped_collection_runtime` trên cùng VM:
1.484.036 → 1.283.976 lần cấp phát, GC 8 → 7, free-list probes
1.203.451 → 1.004.092; kích thước VMB 4.332 → 4.012 byte.
So với phase 5 tắt IR, tổng mức giảm cấp phát là 38,4% và GC 36,4%.
Compiler native và JIT `--vm-image` mới cho cùng số liệu và đầu ra chuẩn.
VM core, hai fixture phase 6 và `ir_optimizer_runtime` qua VM, AOT,
JIT động đều PASS trên macOS ARM64; `ownership_valid` qua VM/AOT PASS.
Chưa xác minh toàn bộ regression suite và Linux/Windows.

Đây là tối ưu dựa trên chứng minh cục bộ và vùng tạm nội bộ có vòng đời
rõ ràng. Chưa có phân tích escape liên thủ tục hay stack/arena allocation
tổng quát cho đối tượng guest. Không thay opcode hoặc định dạng VMB.

### Arena đối số cho closure, phương thức và constructor (phase 7)

VM phân giải đích lời gọi gián tiếp trước khi lấy đối số. Closure guest,
phương thức và constructor guest dùng chung cơ chế đối số có vòng đời
theo khung: 1–4 đối số nội tuyến, 5–64 đối số mượn arena của khung cha,
trên 64 đối số dùng vector heap. AOT áp dụng cơ chế này cho constructor.
Host ABI, thứ tự đánh giá đối số, tham chiếu capture và định dạng VMB
giữ nguyên. Khi trả về hoặc tháo khung do ngoại lệ, các ô đã mượn được xóa.

Đo phase 6 và phase 7 trên macOS ARM64 với cùng compiler và fixture
`scripts/test/fixtures/scoped_dispatch_runtime.vi` (100.000 vòng),
VM chạy cùng VMB và AOT liên kết với runtime tương ứng:

| Backend | Cấp phát trước → sau | GC trước → sau | Free-list probes trước → sau |
| --- | ---: | ---: | ---: |
| VM | 5.201.512 → 4.601.500 | 20 → 17 | 4.898.239 → 4.290.963 |
| AOT | 1.500.089 → 1.300.085 | 7 → 5 | 1.239.588 → 1.053.407 |

Fixture kiểm tra constructor, closure lồng nhau, capture thay đổi,
phương thức, tham số mặc định, ngoại lệ và tái sử dụng arena sau ngoại lệ.
Đầu ra VM/AOT trước và sau giống nhau. VM core và các fixture arena/IR
phase 6 đã đạt trên macOS ARM64. Compiler native và JIT được biên dịch lại
với runtime phase 7; JIT động, JIT tạo VMB, compiler tạo VMB và AOT đều
cho đầu ra chuẩn. Bản CLI cục bộ đã được cập nhật và kiểm tra cả ba đường
thực thi. Số liệu chỉ đại diện cho fixture này.
Chưa có phân tích escape liên thủ tục hoặc cấp phát đối tượng guest
tổng quát trên stack. Chưa xác minh toàn bộ regression suite, Linux/Windows.

### Phân tích escape liên thủ tục cho danh sách tạm (phase 8)

Compiler nhận diện lời gọi trực tiếp đến hàm cục bộ có đúng một tham số
và thân IR chỉ đọc phần tử thứ nhất của chính tham số đó rồi trả về.
Khi đối số là danh sách tạm động có đúng một phần tử, compiler giữ kết
quả đánh giá phần tử trên stack và loại bỏ cả `OP_LIST_LITERAL` lẫn
`OP_GOI`. Danh sách trung gian không cần cấp phát trên heap.
Thứ tự đánh giá, ngoại lệ từ biểu thức đối số và định danh tham chiếu
của phần tử được bảo toàn.

Chỉ áp dụng khi chứng minh được đích gọi và dạng thân hàm nói trên.
Danh sách tĩnh, biến danh sách, nhiều phần tử, lời gọi gián tiếp,
phương thức, hàm bị che khuất bởi biến cục bộ và thân hàm có thao tác
bổ sung vẫn theo đường thực thi thông thường. `VPP_OPTIMIZE_IR=0`
tắt tối ưu này. Không thay đổi runtime, opcode hay định dạng VMB.

Đo trên macOS ARM64 với fixture
`scripts/test/fixtures/interprocedural_escape_runtime.vi` (100.000 vòng),
so sánh compiler phase 7 và phase 8 với cùng runtime:

| Backend | Cấp phát trước → sau | GC trước → sau | Free-list probes trước → sau |
| --- | ---: | ---: | ---: |
| VM | 599.557 → 299.464 | 3 → 2 | 377.832 → 71.198 |
| AOT | 299.251 → 199.248 | 2 → 1 | 274.137 → 25.333 |

Đầu ra VM, AOT và JIT động khớp fixture chuẩn. Compiler native và JIT
tạo VMB tối ưu giống nhau từng byte. Khi tắt tối ưu, VMB từ compiler
mới giống từng byte với baseline phase 7. Kiểm thử cấu trúc IR và
disassemble xác nhận hai opcode được loại bỏ tại lời gọi đủ điều kiện.
VM core gate đạt; CLI cục bộ đã cập nhật compiler và JIT, kiểm tra thành
công `vm-build`, `artifact`, `jit --dynamic` và `build`.
Đây là phân tích escape cho một dạng lời gọi cụ thể; chưa hỗ trợ
phân tích escape tổng quát, cấp phát đối tượng guest trên stack hay
giải phóng sớm mọi đối tượng không escape. Số liệu chỉ đại diện cho
fixture trên; chưa xác minh toàn bộ regression suite, Linux/Windows.

### Chiếu danh sách tạm nhiều phần tử (phase 9)

Mở rộng chứng minh phase 8 cho chỉ số nguyên hằng không âm (tối đa
32767). Đối số là danh sách động có đúng một biểu thức cần giữ;
các phần tử còn lại phải là literal nguyên thập phân nhỏ đã xác minh.
Chỉ thay thế khi hàm đích đọc đúng chỉ số của biểu thức cần giữ.

Bước hạ IR lưu vị trí biểu thức cần giữ và phạm vi IR của đối số.
Emitter xác minh lại đích gọi, binding tham số, số phần tử và các
lệnh literal trước khi phát mã. Các literal không cần thiết, lệnh
tạo danh sách và lời gọi được bỏ qua ngay trong IR, nên không phải
chỉnh sửa địa chỉ nhảy hoặc vị trí nguồn đã phát. Lời gọi lồng nhau
cũng được phân tích trên cùng dãy IR gốc.

Biểu thức được chọn vẫn thực thi đúng một lần và giữ nguyên tham chiếu.
Danh sách có nhiều biểu thức động, chỉ số ngoài miền, literal tĩnh,
hàm sửa hoặc trả về danh sách, lời gọi gián tiếp và binding bị che
khuất giữ nguyên đường thực thi. `VPP_OPTIMIZE_IR=0` tắt tối ưu.

Fixture `scripts/test/fixtures/interprocedural_projection_runtime.vi`
chạy 100.000 vòng, mỗi vòng chiếu phần tử đầu, giữa và cuối. Trên
macOS ARM64, cả VM và AOT cho cùng đầu ra chuẩn trước và sau tối ưu:

| Chỉ số | VM phase 8 | VM phase 9 | AOT phase 8 | AOT phase 9 |
| --- | ---: | ---: | ---: | ---: |
| Lượt cấp phát | 1.700.025 | 499.613 | 999.261 | 399.251 |
| Lượt GC | 7 | 2 | 5 | 2 |
| Free-list probes | 1.427.901 | 237.872 | 726.285 | 162.593 |

Kiểm thử cấu trúc IR xác nhận các trường hợp dương và âm, kể cả
chiếu lồng nhau. Khi tắt tối ưu, VMB của compiler phase 8 và phase 9
giống nhau từng byte. Runtime và định dạng VMB không thay đổi.

Compiler native và JIT phase 9 đã được build lại; VMB của hai compiler
khớp với ảnh do harness IR tạo ra. Gate VM/ASM core PASS (giá trị,
điều khiển, closure, OOP, ngoại lệ, GC và kiểm tra symbol C++).
Fixture phase 8 tiếp tục PASS trên AOT và JIT. CLI cục bộ đã được
cập nhật và kiểm tra lại VM, JIT động, AOT với fixture phase 9.
Kết quả này chỉ đo workload trên, chưa đo toàn bộ chi phí biên dịch
hoặc xác nhận toàn bộ regression trên Linux/Windows. Bản CLI trước
được giữ tại `build-pure-vpp/rust-phase9/previous-active`.

### Chiếu danh sách tạm có nhiều biểu thức động (phase 10)

Mở rộng phase 9 để loại bỏ danh sách tạm ngay cả khi nhiều phần tử
là biểu thức động. Compiler vẫn yêu cầu chứng minh được lời gọi trực
tiếp đến hàm cục bộ chỉ đọc một chỉ số nguyên hằng của tham số duy nhất.
IR ghi độ dài của từng biểu thức phần tử; emitter kiểm tra phạm vi,
chỉ số, binding của đích gọi và loại trừ IR phép gán không có kết quả
stack được bảo đảm. Danh sách tĩnh và lời gọi chưa chứng minh được
tiếp tục dùng đường thực thi thông thường.

Các biểu thức động được đánh giá theo thứ tự nguồn. Kết quả không được
chọn được bỏ khỏi stack bằng `OP_BAT_LOI` với slot -1; kết quả được
chọn vẫn là root trên stack. Literal nguyên nhỏ đã xác minh có thể
được bỏ hoàn toàn. Không thêm opcode, thay runtime hoặc định dạng VMB.
Tắt tối ưu bằng `VPP_OPTIMIZE_IR=0` cho VMB giống từng byte với phase 9.

Fixture `scripts/test/fixtures/interprocedural_effect_projection_runtime.vi`
chạy 100.000 vòng, mỗi vòng chiếu ba danh sách có nhiều biểu thức động.
Kết quả trên macOS ARM64, cùng runtime:

| Chỉ số | VM phase 9 | VM phase 10 | AOT phase 9 | AOT phase 10 |
| --- | ---: | ---: | ---: | ---: |
| Lượt cấp phát | 1.700.194 | 499.934 | 999.284 | 399.266 |
| Lượt GC | 7 | 2 | 5 | 2 |
| Free-list probes | 1.428.623 | 237.807 | 721.135 | 162.495 |

Fixture kiểm tra thứ tự hiệu ứng, ngoại lệ trước và sau phần tử được
chọn, định danh tham chiếu và đột biến qua alias, chiếu lồng nhau,
giá trị rỗng, số lớn và số thực. Kiểm thử cấu trúc IR, VM core gate,
fixture phase 9 trên AOT/JIT và đầu ra VM/JIT/AOT phase 10 đều PASS.
Compiler native và JIT đã build lại, tạo VMB khớp harness IR và được
cài vào CLI cục bộ; `vm-build`, `artifact`, `jit --dynamic` và `build`
đều được kiểm tra. Bản CLI phase 9 được giữ tại
`build-pure-vpp/rust-phase10/previous-active`.
Số liệu chỉ đại diện cho fixture này. Chưa đo chi phí biên dịch tổng
thể, chưa chạy toàn bộ regression suite hoặc xác minh Linux/Windows.

### Đối chiếu cùng workload giữa phase 8, 9 và 10 (25/11/2026)

Hai benchmark ở trên có cùng số vòng lặp nhưng khác biểu thức phần tử.
Không ghép số liệu của chúng thành một chuỗi giảm dần. Đã chạy lại
cả ba compiler trên cả hai fixture, cùng VM, runtime AOT, macOS ARM64,
`VPP_OPTIMIZE_IR=1` và `VPP_VM_PROFILE=1`.

**Fixture một biểu thức động, hai literal nguyên nhỏ** (`interprocedural_projection_runtime.vi`):

| Chỉ số | Phase 8 | Phase 9 | Phase 10 |
| --- | ---: | ---: | ---: |
| VM cấp phát | 1.700.025 | 499.613 | 499.605 |
| VM GC | 7 | 2 | 2 |
| VM free-list probes | 1.427.901 | 237.872 | 237.871 |
| AOT cấp phát | 999.261 | 399.251 | 399.247 |
| AOT GC | 5 | 2 | 2 |
| AOT free-list probes | 726.285 | 162.593 | 162.600 |

**Fixture ba biểu thức động** (`interprocedural_effect_projection_runtime.vi`):

| Chỉ số | Phase 8 | Phase 9 | Phase 10 |
| --- | ---: | ---: | ---: |
| VM cấp phát | 1.700.194 | 1.700.194 | 499.934 |
| VM GC | 7 | 7 | 2 |
| VM free-list probes | 1.428.623 | 1.428.623 | 237.807 |
| AOT cấp phát | 999.284 | 999.284 | 399.266 |
| AOT GC | 5 | 5 | 2 |
| AOT free-list probes | 721.135 | 721.135 | 162.495 |

Đầu ra của cả 12 phép chạy khớp fixture chuẩn. Phase 9 tối ưu workload
thứ nhất; phase 10 giữ gần như nguyên số lượt cấp phát của workload đó
và mở rộng tối ưu sang workload thứ hai. AOT free-list probes tăng
7 lượt ở workload thứ nhất. Không cộng dồn tỷ lệ giảm của hai workload.
Các số liệu này đo số lần cấp phát và GC, chưa đo peak RAM hoặc tốc độ.
Log thô nằm tại `build-pure-vpp/phase8-10-comparison/`.
Các phase tiếp theo cần chạy lại cả hai fixture trên cùng runtime khi
đánh giá riêng thay đổi compiler, và ghi rõ runtime nếu có thay đổi.

### Chiếu trực tiếp danh sách tạm có biểu thức động (phase 11)

Mở rộng scalar replacement sang `[a(), b(), c()][1]`. IR ghi lại
ranh giới từng biểu thức và chỉ số nguyên nhỏ đã xác minh nằm trong
danh sách. Emitter dùng chung phép kiểm tra ranh giới với chiếu liên
thủ tục, giữ thứ tự đánh giá và ngoại lệ, giữ kết quả được chọn trên
stack làm GC root và bỏ từng kết quả không được chọn. Không tạo danh
sách tạm hoặc thực thi lệnh đọc chỉ số trong trường hợp đủ điều kiện.
Chỉ số động, ngoài phạm vi, literal tĩnh chưa được tối ưu và IR gán
tiếp tục đi qua đường phát mã thông thường. VM, AOT runtime và opcode
không đổi.

**Đối chiếu phase 10/11 trên macOS ARM64, cùng VM và runtime AOT,
`VPP_OPTIMIZE_IR=1`, `VPP_VM_PROFILE=1` (25/11/2026).**

| Workload | Backend | Cấp phát P10 → P11 | GC P10 → P11 | Free-list probes P10 → P11 |
| --- | --- | ---: | ---: | ---: |
| interprocedural_projection_runtime | VM | 499.605 → 499.605 | 2 → 2 | 237.871 → 237.871 |
| interprocedural_projection_runtime | AOT | 399.247 → 399.247 | 2 → 2 | 162.600 → 162.600 |
| interprocedural_effect_projection_runtime | VM | 499.934 → 499.934 | 2 → 2 | 237.807 → 237.807 |
| interprocedural_effect_projection_runtime | AOT | 399.266 → 399.266 | 2 → 2 | 162.495 → 162.495 |
| direct_effect_projection_runtime | VM | 1.100.038 → 499.990 | 5 → 2 | 824.782 → 237.814 |
| direct_effect_projection_runtime | AOT | 999.324 → 399.304 | 5 → 2 | 726.237 → 162.505 |
| scoped_dispatch_runtime | VM | 4.601.500 → 4.601.500 | 17 → 17 | 4.290.963 → 4.290.963 |
| scoped_dispatch_runtime | AOT | 1.300.085 → 1.300.085 | 5 → 5 | 1.053.407 → 1.053.407 |

Fixture mới đo 300.000 phép chiếu trực tiếp, cùng số vòng lặp và biểu
thức ở hai phase. VM giảm cấp phát 54,55%, GC 60%, dò free-list 71,17%.
AOT giảm tương ứng 60,04%, 60% và 77,62%. Ba workload đối chứng giữ
nguyên tất cả bộ đếm. Đây là bộ đếm cấp phát, chưa phải peak RAM
hoặc thời gian thực thi.

Runner `scripts/quality/benchmark_vm_allocations.py` cố định bốn
fixture, xác minh stdout theo expected, ghi SHA-256 của compiler, VM,
runtime object, fixture và artifact; lưu profile thô và JSON.
Có thể tái hiện từ thư mục dự án trên macOS ARM64:

```sh
p="$PWD/build-pure-vpp/rust-phase11"
b="$PWD/build-pure-vpp/bin"
export VPP_AOT_VPP_OBJECT="$PWD/build-pure-vpp/native-vpp-runtime/runtime.o"
export VPP_AOT_PLATFORM_OBJECT="$PWD/build-pure-vpp/native-vpp-runtime/platform.o"
export VPP_AOT_DYNAMIC_BRIDGE_OBJECT="$PWD/build-pure-vpp/native-vpp-runtime/dynamic-bridge.o"
export VPP_AOT_NATIVE_MACHO_LINK=1 VPP_AOT_NATIVE_ARM64_OBJECT=1
export VPP_AOT_ASSEMBLER="$b/assembler-must-not-run"
python3 scripts/quality/benchmark_vm_allocations.py --vm "$b/vpp-vm" \
    --output-dir "$p/recheck" phase10="$p/previous-active/vpp-native" \
    phase11="$p/vpp-native"
```

Kết quả đã đo ở `build-pure-vpp/rust-phase11/comparison-summary.json`.
Cả 16 phép chạy khớp expected; fixture, VM và runtime có SHA-256
trùng nhau giữa hai lần đo. Kiểm thử cấu trúc IR, bytecode tắt tối ưu
trùng từng byte, JIT dynamic/VM image, VM core gate và launcher VM/AOT/JIT
đều PASS. Probe bổ sung giữ danh sách tạm sống qua 25 GC trong VM và 20
GC trong AOT, cho đúng kết quả 42, 43, 44. Compiler và JIT phase 11
đã được đặt vào `build-pure-vpp/bin`, bản phase 10 lưu tại
`rust-phase11/previous-active`. Chưa chạy toàn bộ regression suite
hoặc xác minh Linux/Windows.

### Số nguyên nội tuyến và hợp nhất lệnh VM (phase 12)

VM giữ lại ô nội tuyến khi khai báo hoặc bind tham số lặp lại và ô chưa
thoát ra closure. Closure vẫn chuyển ô sang heap trước khi bắt giữ.
Dispatcher nhận chuỗi `load slot; i32 immediate; compare` và chuỗi
`load slot; i32 immediate; add/sub/mul; slot id; store` cùng slot.
Phép toán trên i32 giữ nguyên quy tắc tràn 32 bit. Ô nội tuyến lưu
kết quả thô (tag 12); ô heap lưu kết quả đóng hộp. Kiểu động, slot
không tồn tại và các chuỗi lệnh khác tiếp tục qua dispatcher cũ.
Không đổi opcode hoặc định dạng VMB, compiler, JIT hay runtime AOT đã cài.

**So sánh phase 11/12 trên macOS ARM64 (25/11/2026).** Hai VM chạy
cùng từng artifact VMB đã băm SHA-256, đối chiếu stdout với expected.
Bộ đếm lấy với `VPP_VM_PROFILE=1`; thời gian là trung vị 5 lượt chạy
xen kẽ không bật profile sau warmup, tính cả khởi động tiến trình.

| Workload | Cấp phát P11 → P12 | GC P11 → P12 | Free-list probes P11 → P12 | Thời gian (s) P11 → P12 |
| --- | ---: | ---: | ---: | ---: |
| interprocedural_projection_runtime | 499.605 → 399.578 | 2 → 2 | 237.871 → 163.083 | 0.842 → 0.702 |
| interprocedural_effect_projection_runtime | 499.934 → 399.852 | 2 → 2 | 237.807 → 163.182 | 1.307 → 1.124 |
| direct_effect_projection_runtime | 499.990 → 399.890 | 2 → 2 | 237.814 → 163.215 | 1.403 → 1.141 |
| scoped_dispatch_runtime | 4.601.500 → 3.201.417 | 17 → 12 | 4.290.963 → 2.899.978 | 5.736 → 4.889 |
| i32_slot_runtime | 600.389 → 100.262 | 3 → 1 | 335.008 → 11.323 | 0.555 → 0.226 |

Fixture `i32_slot_runtime` kiểm tra 200.000 phép cập nhật số nguyên,
tràn i32, so sánh, closure và chuyển kiểu sang số thực/chuỗi. Giảm
83,30% lượt cấp phát, 66,67% GC, 96,62% dò free-list và 59,32% thời gian.
Bốn fixture cũ giảm cấp phát 20,02–30,43%. Các bộ đếm không đo peak RAM;
profiling theo dispatch có thể ghi ít bước hơn vì hợp nhất lệnh.

Runner `scripts/quality/benchmark_vm_runtime.py` giữ nguyên bytecode,
expected và executable trong suốt phép đo; ghi profile và SHA-256 vào
`build-pure-vpp/rust-phase12/comparison/results.json`. Tái hiện:

```sh
p=build-pure-vpp/rust-phase12
old=build-pure-vpp/rust-phase11/comparison-candidate
python3 scripts/quality/benchmark_vm_runtime.py --baseline "$p/previous-active" \
  --candidate "$p/vpp-vm-final" --output-dir "$p/recheck" --repeats 5 \
  interprocedural_projection_runtime="$old/phase11-interprocedural_projection_runtime.vmb" \
  interprocedural_effect_projection_runtime="$old/phase11-interprocedural_effect_projection_runtime.vmb" \
  direct_effect_projection_runtime="$old/phase11-direct_effect_projection_runtime.vmb" \
  scoped_dispatch_runtime="$old/phase11-scoped_dispatch_runtime.vmb" \
  i32_slot_runtime="$p/i32_slot_runtime.vmb"
```

VM core gate, optimizer tự biên dịch, launcher VM artifact, AOT và JIT
`--dynamic` cho fixture mới đều PASS. JIT mặc định báo chưa hỗ trợ literal
1.5; kiểm thử dùng chế độ dynamic. Đã cài VM mới vào `build-pure-vpp/bin/vpp-vm`
và lưu VM cũ tại `rust-phase12/previous-active`. Chưa chạy toàn bộ regression,
self-host stage 2/3 hoặc xác minh Linux/Windows.

Giá trị số nguyên bất biến -16..4095 được intern trong một bảng tại
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

Chạy test runtime đầy đủ và profiler nội bộ Stage 2:

```sh
cmake -DVPP_STAGE1="$PWD/build/nativedriver/vpp-native" \
  -DVPP_NATIVE_COMPILER="$PWD/build/native-cli/vpp-native" \
  -DVPP_ROOT="$PWD" -DVPP_OUTPUT_DIR="$PWD/build/vm-asm-optimized" \
  -P scripts/quality/check_vm_asm.cmake
VPP_VM_PROFILE=1 VPP_HOME="$PWD" build/vm-asm-optimized/vpp-vm \
  build/vm-asm-selfhost/compiler-stage1.vmb build \
  "gói/biên dịch/dòng lệnh VM.vi" -o build/compiler-stage2-profile.vmb \
  2> build/stage2-profile.log
```

`VPP_VM_PROFILE=1` ghi stderr sau mỗi 250.000 guest instruction,
gồm vị trí function/PC, số allocation, GC và số lượt dò free-list.
Helper Python giới hạn thời gian và tạo JSON đã được gỡ khỏi repo.
Lệnh trên ghi log thô và chạy đến khi xong hoặc người dùng dừng.
Các phép đo giới hạn thời gian trước đây chỉ có nghĩa
**Stage 2 chưa hoàn thành trong thời gian quan sát**.
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
`scripts/ASM/common/posix/vpp_vm_directory_primitives.inc` giữ ranh giới OS và con trỏ
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
biên offset. Shell POSIX đối chiếu mode theo umask, PID/PPID với PID
tiến trình con/cha và UID/GID với OS. VM chạy với PATH rỗng.

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
`scripts/ASM/common/posix/vpp_vm_process_primitives.inc`. Control block 1024 byte thuộc
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

`scripts/ASM/common/posix/vpp_vm_socket_primitives.inc` và
`common/posix/vpp_vm_socket_x86_64.inc` gọi socket/connect/bind/listen/accept,
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

`scripts/ASM/common/posix/vpp_vm_system_primitives.inc` đổi offset arena thành con trỏ
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
