# Đặc tả triển khai Native Code Generator cho V++ (dành cho coding agent)

**Cập nhật 17/10/2026:** encoder AArch64/x86-64 và đường native AST/IR
V++ đã bỏ giới hạn cố định bốn tham số của mốc 13/10. Có thể truyền
đối số i32 qua thanh ghi và stack theo ABI đích; `CALL_ARITY` lưu số
đối số riêng với đích `CALL`. Kiểm thử liên kết/thực thi 128 tham số
được bổ sung vào `vpp-native-selfhost-executable`, và thử thủ công 4.200
tham số đã trả đúng `52` trên macOS ARM64. Giới hạn thực tế đến từ
bộ nhớ, chiều dài nguồn, i32/IR, stack và ABI hệ điều hành.

**Cập nhật tối ưu AArch64 (16/10/2026):** bộ phát V++ và oracle C++ hiện
giữ toàn bộ local/evaluation slot trong thanh ghi khi hàm không chứa `Call`
và `slotCount + maxStackDepth <= 16`. Slot 0–8 dùng W0–W8, slot 9–15
dùng W11–W17. Ánh xạ cố định cho cả hàm nên các nhánh, điểm hợp và vòng
lặp dùng cùng vị trí giá trị; không cần đồng bộ cache theo từng đường đi.
Không dùng W18 dành cho nền tảng hoặc thanh ghi callee-saved W19+.
Hàm đủ điều kiện không tạo stack frame hay lưu LR; phép toán dùng trực tiếp
thanh ghi của evaluation slot, bỏ MOV cùng nguồn/đích và bỏ MOVK nếu nửa
cao của hằng bằng 0. Hàm có lời gọi, kể cả lời gọi trong mã không tới được,
hoặc vượt ngân sách thanh ghi giữ nguyên đường phát dùng stack. Đây là
phân bổ cố định cho hàm nhỏ, chưa phải bộ phân bổ theo liveness hoặc SSA.
Phạm vi thay đổi là AArch64; các backend khác giữ đường phát hiện có.
Benchmark và hướng dẫn chạy lại ở [benchmark/NATIVE.md](../benchmark/NATIVE.md).

**Trạng thái (2026-10-13):** đã có JIT mã máy thử nghiệm (opt-in) trên
macOS ARM64, Linux ARM64/x86-64 và Windows x86-64. Raw machine-code emitter
cho tập hàm lá i32 tối giản có 16 biến thể kiến trúc, xuất được byte mã máy,
assembly và **object tái định vị (`--emit-obj`)**: ELF 32/64 trên Linux,
Mach-O 64 trên macOS (x86-64/ARM64), COFF trên Windows (i386/x86-64).
Đã liên kết và chạy được hàm V++ đã native hóa trong chương trình C++ trên
macOS ARM64. **Chưa có AOT toàn chương trình, linker/runtime native cho V++
hoặc compiler self-host hoàn chỉnh.**

**Mốc Machine IR (13/10/2026):** riêng **x86_64 và AArch64** đã có đường
`lowerMachineFunction` hạ hàm `i32` gồm tối đa 4 đối số, khai báo/gán biến
cục bộ có khởi tạo, `nếu`/`hoặc`, `lặp(init;điều kiện;cập nhật)`, `bỏ qua`,
`thoát`, return, số nguyên, so sánh signed, `+`, `-`, `*`, phủ định và một số
toán tử đơn ngôi. **Lời gọi hàm V++ trực tiếp** đã được hạ thành opcode
`MachineOpcode::Call` khi semantic binding xác định đúng hàm đích, với
0–4 đối số `i32` (không có tham số mặc định). Hỗ trợ lời gọi lồng nhau,
đệ quy và gọi hàm trong vòng lặp; bộ phát đối chiếu số lượng tham số và
từ chối tên/symbol không hợp lệ. Nhánh được hạ thành target instruction index, sau đó patch
thành **x86-64 rel32** hoặc **AArch64 B/CBZ PC-relative**. Mỗi backend cấp
một frame căn chỉnh 16 byte và lưu tham số vào các slot; biểu thức sử dụng
stack slot trong frame cùng hai thanh ghi scratch (`eax/ecx` hoặc `w9/w10`).
Mỗi lệnh lưu `depthBefore`. Bộ kiểm tra CFG xét độ sâu stack ở các nhánh,
điểm hợp, vòng lặp và `return` sớm; từ chối underflow, nhảy ngoài hàm,
độ sâu không khớp và fallthrough không hợp lệ. Backend dùng stack depth
tại từng lệnh để tính địa chỉ biến tạm cho đúng ở mọi nhánh.
Các lệnh mới đi qua **`--emit-raw`, `--emit-asm`, `--emit-obj` thực tế**.
CLI hạ `compiler::IrProgram` đã được frontend/semantic C++ tạo ra thành
`MachineFunction` qua thư viện **V++** `gói/biên dịch/mã máy hạ biểu diễn trung gian.vi`, rồi
phát byte mã máy và relocation từ thư viện V++ qua bridge
`src/cli/native_vpp_codegen.cpp`. Từ 13/10, mỗi đường production
`compileRawLeafVpp` hoặc `compileMachineFunctionVpp` thực hiện cả hạ IR và
phát byte **trong một lần chạy VM V++**; bridge vẫn dùng C++ để serialize
IR và đọc bản ghi kết quả. C++ vẫn cung cấp frontend/semantic,
sinh văn bản assembly và ghi định dạng object. Bộ hạ IR C++
`lowerMachineFunction` được giữ làm oracle độc lập. Hàm lá i32 của 16
kiến trúc được phân loại và phát byte chung trong `compileRawLeafVpp`;
encoder C++ giữ làm oracle và in assembly. API hạ/phát riêng vẫn có để
đối chiếu parity, không còn được CLI gọi nối tiếp cho từng hàm.

Smoke `vpp-native-control-flow-host-link` đã liên kết/chạy các hàm có nhánh,
vòng lặp và biến cục bộ bằng Mach-O AArch64, bao gồm `return` trong các
nhánh lồng nhau và vòng lặp, phép tính 4 tham số; manual smoke qua Rosetta
x86_64 cũng trả đúng. `vpp-native-real-assembler-parity` sử dụng Clang
assembler thực cho i386/x86_64 (ELF/COFF) và AArch64 (ELF); so byte chính
xác cho các hàm lá và hàm tổng quát AArch64. Assembly của hàm tổng quát
x86-64 được kiểm tra lắp ráp thành công; assembler được quyền rút gọn disp32
và rel32 nên không bắt buộc đồng nhất byte với encoder trực tiếp.

Smoke `vpp-native-calls-host-link` **đã liên kết và thực thi thật trên
macOS AArch64**: tám object `.o` V++ độc lập chứa hàm gọi chéo, tên symbol
dài, đệ quy tính giai thừa, lời gọi bên trong vòng lặp và hàm bốn tham số.
`vpp-native-call-relocation-parity` xuất cả object và assembly rồi so với
Clang assembler qua `objdump`: **20 trường hợp** (4 hàm × 5 triple), gồm
ELF x86-64 `R_X86_64_PLT32` (addend `-4`), ELF AArch64
`R_AARCH64_CALL26`, Mach-O x86-64 `X86_64_RELOC_BRANCH`, Mach-O AArch64
`ARM64_RELOC_BRANCH26` và COFF x86-64 `IMAGE_REL_AMD64_REL32`.
AArch64 còn đối chiếu byte `.text` cùng offset relocation; x86-64 so loại
relocation và tên callee do assembler có thể rút gọn encoding. Với COFF,
fixture chứa tên hàm dài để kiểm tra symbol string table. Xuất `--emit-raw`
cho hàm còn lời gọi chưa liên kết phải báo lỗi tường minh chứa `CALL`.
Trên máy macOS kiểm tra ngày 13/10/2026, **12/12 CTest native liên quan
đã đạt**. Cùng ngày, tám ELF AArch64 object từ `src/tests/kiem_tra_native_calls.vi`
được sinh bởi `vpp-cli` trên macOS, liên kết bằng GCC trong Docker Ubuntu 24.04
ARM64 và **thực thi thành công dưới Linux ARM64**, in `native_calls_ok`.
Đây là kiểm thử Linux ARM64 thật trong container; không phải QEMU hoặc
kiểm thử trên một máy Linux x86-64. COFF Windows x86-64 đã đối chiếu
assembler/relocation nhưng **chưa chạy executable trên Windows thật**.

**Bước CI tiếp theo (chưa có kết quả chạy của commit này):** Linux ARM64
chạy thêm `vpp-native-calls-host-link` với linker/compiler của runner;
Windows x86-64 MSVC sinh tám object COFF `.obj` bằng `vpp-cli`, liên kết chúng
với harness C ABI được biên dịch bằng CMake/MSBuild và chạy `.exe` qua CTest
`vpp-native-calls-host-link`. Bài thử kiểm tra gọi hàm chéo object, đệ quy,
vòng lặp, tên symbol dài và 4 đối số. Cả hai bài đều fail khi không có test,
link thất bại hoặc kết quả chạy sai. **Bài chạy Docker Linux ARM64 đã đạt;
trạng thái CI Linux ARM64 và Windows x86-64 vẫn chờ log runner thực tế.**

**Giới hạn của Machine IR:** chỉ là tập con hàm `i32` và các lời gọi trực tiếp
`i32` với số đối số động; chưa có lời gọi động, giá trị/kiểu động, biến global/heap,
FFI, xử lý tràn động theo quy tắc của VM,
short-circuit `&&/||`, GC safepoint, unwind hoặc register allocator hoàn
chỉnh. Hiện dùng slot cố định trong stack frame, **chưa phân bổ thanh ghi
tối ưu**. C++ vẫn làm frontend/semantic và điều phối CLI; V++ đã đảm nhiệm
hạ IR sang Machine IR và phát byte tổng quát x86-64/AArch64 nhưng **chưa
thay thế toàn bộ** compiler và bộ in assembly C++.

**Tiến độ self-host AArch64 và x86_64 Machine IR (13/10/2026):**
`gói/biên dịch/mã máy aarch64.vi` và
`gói/biên dịch/mã máy x86 64.vi` hiện có bộ phát Machine IR i32
**viết hoàn toàn bằng V++**. Hai hàm tương ứng là
`biên dịch mã máy phát aarch64 hàm(hàm_ir)` và
`biên dịch mã máy phát x86 sáu mươi tư hàm(hàm_ir, hệ_điều_hành)`.
Chúng nhận MachineOpcode đúng tên trong `src/include/vpp/native/raw_codegen.h`:
`PushConstant`, `LoadSlot`, `StoreSlot`, `Drop`, `Add`, `Sub`, `Mul`,
`Equal`, `NotEqual`, `Less`, `LessEqual`, `Greater`, `GreaterEqual`,
`BranchZero`, `Jump`, `Call`, `Return`, `Trap`.
Bộ phát xử lý frame căn chỉnh 16 byte, W0–W3 theo AAPCS64 trên ARM,
SysV và Win64 ABI trên x86-64 (bao gồm shadow space, R8D/R9D),
temporary slot, signed comparison, fixup nhánh tương đối kể cả nhảy lùi,
CALL và offset relocation. Opcode không được khai báo sử dụng
`chọn/ca/mặc định` để **ném lỗi ngay**.
API `biên dịch mã máy hàm("aarch64", hàm_ir, "linux")`
hoặc `biên dịch mã máy hàm("x86_64", hàm_ir, "windows")`
trong `gói/biên dịch/mã máy.vi` trả `{ "bytes", "relocations" }`.
Target tổng quát khác phải ném lỗi do chưa có backend V++ tương ứng.
Trước khi phát byte, `biên dịch mã máy kiểm tra CFG` trong
`mã máy nền.vi` duyệt mọi lệnh (kể cả unreachable) và duyệt các cạnh CFG,
kiểm tra chiều cao stack trước/sau, nhánh/hợp nhánh/vòng lặp, return,
fallthrough, phạm vi slot và tên symbol CALL theo quy tắc ASCII của C++.
Các helper trong thư viện phát IR nhận trường opcode/immediate/operand/
depthBefore/symbol giống hệt C++ `MachineInstruction`.

`vpp-native-machine-vpp-parity` chạy ba đường phát riêng và đối chiếu
byte trên **24 trường hợp** (6 hàm × 4 target):
`tru_dao`, `tong_bon`, `dieu_kien`, `max_i32`, `tong` (nhánh lùi vòng lặp)
và `goi_bon` (relocation CALL). Các target là macOS AArch64,
Linux AArch64, Linux x86-64 và Windows x86-64. Nguồn kiểm chứng
`scripts/quality/native_machine_cpp_oracle.cpp` biên dịch các fixture bằng
pipeline C++, hạ thành Machine IR và gọi trực tiếp **encoder C++ gốc**
`emitMachineFunction`, không đi qua V++ bridge. Kết quả được so với
fixture `src/tests/kiem_tra_native_machine_vi.vi` (Machine IR được xây
trong V++) và byte thực của `vpp-cli --emit-raw` hoặc `--emit-obj`.
Với `goi_bon`, oracle còn yêu cầu relocation tới symbol `bon`; CLI
được kiểm tra trên object có chứa byte mã máy và bộ kiểm tra relocation
riêng xác nhận loại/offset relocation khi liên kết.
Fixture V++ còn kiểm tra opcode chưa hỗ trợ,
nhánh ngoài hàm, stack underflow, CALL thiếu/sai symbol, độ sâu sai
tại CFG join, fallthrough và backend không có encoder. Phép đối chiếu
không dựa trên các hex được chép tay: CTest sinh lại byte qua C++ và V++.
**Kết quả chạy cục bộ ngày 13/10/2026:** bài đối chiếu độc lập
`vpp-native-machine-vpp-parity` đã đạt 24/24 trường hợp; toàn bộ
**10/10 CTest `-R '(native|raw)'`** cũng đạt, gồm assembler thực,
CALL relocation và smoke liên kết/thực thi native.

**Kiểm thử lowering độc lập mới:**
`vpp-native-machine-lowering-parity` so sánh trực tiếp C++
`lowerMachineFunction` với V++ `lowerMachineFunctionVpp`, trên **24/24
trường hợp đã đạt**: thông số frame (số tham số, slot, độ sâu stack tối đa)
và từng opcode/immediate/operand/depthBefore/symbol của MachineFunction;
bao gồm nhánh, vòng lặp, `break`/`continue`, return sớm, lời gọi có
semantic binding, đệ quy và ranh giới signed i32. Ca overflow/tràn trung
gian/negation vượt phạm vi được dựng trực tiếp thành compiler IR để kiểm tra
chính bộ lowerer, do frontend loại một số literal bất hợp lệ trước bước này.
Các ca lỗi bắt buộc cả C++ lẫn V++ cùng từ chối, không chấp nhận wrap hoặc
giả lập kết quả. File kiểm thử: `scripts/quality/native_machine_cpp_oracle.cpp`,
nguồn fixture: `src/tests/kiem_tra_native_i32_boundaries.vi`.

**Đường production đã tích hợp một phần:** CLI gọi `compileRawLeafVpp` để
nhận diện và phát hàm lá i32 trên 16 kiến trúc **trong một lần chạy VM**.
Khi hạ lá bị từ chối, trên x86-64/AArch64 `vpp-cli` gọi
`compileMachineFunctionVpp`: thư viện `gói/biên dịch/mã máy hạ biểu diễn trung gian.vi`
hạ IR tổng quát và `gói/biên dịch/mã máy.vi` phát mã máy, cũng **trong
một lần chạy VM**. Cầu nối nhận `MachineFunction`, byte mã máy và CALL
relocation để ghi `--emit-raw`/`--emit-obj`. Khi đã nhận được bản ghi
`L:` từ hạ IR, lỗi encoder được báo trực tiếp, không chuyển ngầm sang
backend tổng quát. Với `--emit-asm`,
C++ encoder gốc được dùng làm bộ sinh assembly và kiểm tra lại byte,
relocation trước khi xuất văn bản. Riêng `--emit-asm` của hàm lá
đối chiếu byte với encoder C++ và
dùng C++ làm bộ in assembly.
Object writer ELF/Mach-O/COFF hiện nằm trong `gói/biên dịch/đối tượng.vi`;
bản C++ được giữ làm oracle độc lập.
Smoke riêng cho thay đổi cầu nối (13/10/2026): RISC-V64 `tra42`
`--emit-raw` khớp 12 byte mong đợi; x86-64 `tong` xuất assembly 265 byte
cho hàm có vòng lặp; AArch64/macOS `tinh` xuất object Mach-O 64-bit 216
byte mã máy. Cả ba lệnh và build `vpp-cli` đều thành công. Chưa chạy lại
toàn bộ CTest trong lượt tối ưu này.
Smoke bổ sung `goi_bon` trên x86-64/Linux cũng xuất thành công object ELF
64-bit chứa hàm có CALL relocation (128 byte mã máy).
**Các bước tiếp theo:** chuyển frontend/semantic C++ sang V++;
chuyển assembly printer khi đủ oracle và smoke link thực;
giảm chi phí biên dịch và chạy source V++ sinh động ở bridge.
Giữ encoder C++ hiện có làm oracle đối chiếu và thành phần assembly đang
sử dụng; mã C++ không còn được dùng sẽ để lại dưới dạng chú thích theo yêu
cầu của dự án, không xóa.

Trong đường biên dịch chung, direct IR hiện hỗ trợ dấu âm trước biểu thức
lồng nhau (`-(2 + 5)`), dấu âm kép, dấu cộng đơn ngôi và phủ định logic.
Bytecode cho dấu âm trên biểu thức động được phát theo thứ tự `0`, toán hạng,
`OP_TRU`; phép âm trên số literal vẫn dùng trực tiếp hằng âm. Regression
`kiem_tra_native_unary.vi` kiểm tra kết quả chạy VM, gồm `-2147483648`,
`-(2147483648)` và phép âm biểu thức số thực. Direct emitter đọc hằng số
nguyên qua i64 trước khi áp dấu để nhận đúng `INT32_MIN`; literal thực sự
vượt giới hạn i32 báo lỗi tường minh. Smoke object ARM64 kiểm tra biểu thức
âm hằng và hai cách viết `INT32_MIN` đã được hạ thành mã máy.

### Xuất lệnh assembly theo từng kiến trúc

`mã máy` là các byte lệnh CPU, **không phải** văn bản assembly. Ví dụ cùng một
biểu thức `a + b` trong V++ nhưng ABI Linux x86-64 nhận tham số tại `edi/esi`,
còn AArch64 dùng `w0/w1`. Có thể yêu cầu CLI xuất file `.s` để kiểm tra:

```sh
build/bin/vpp-cli build --target x86_64-linux-gnu --emit-asm cong \
  src/tests/kiem_tra_native_leaf.vi build/cong-x86_64.s
build/bin/vpp-cli build --target aarch64-apple-darwin --emit-asm cong \
  src/tests/kiem_tra_native_leaf.vi build/cong-arm64.s
build/bin/vpp-cli build --target aarch64-apple-darwin --emit-raw cong \
  src/tests/kiem_tra_native_leaf.vi build/cong-arm64.bin
build/bin/vpp-cli build --target aarch64-apple-darwin --emit-obj cong \
  src/tests/kiem_tra_native_leaf.vi build/cong-arm64.o
build/bin/vpp-cli build --target x86_64-linux-gnu --emit-obj cong \
  src/tests/kiem_tra_native_leaf.vi build/cong-linux-x64.o
build/bin/vpp-cli build --target aarch64-apple-darwin --emit-obj tru \
  src/tests/kiem_tra_native_leaf.vi build/tru-arm64.o
```

File x86-64 sử dụng `mov eax, edi; add eax, esi; ret`, còn ARM64 phát
`add w0, w0, w1; ret`. CLI xuất **mã máy thực** cho `--emit-raw`, hoặc
**lệnh assembly có nhãn hàm** cho `--emit-asm`, hoặc **object có symbol
exported** cho `--emit-obj`. Lệnh này chưa tự liên kết executable. Bộ kiểm thử
`vpp-raw-object-host-link` biên dịch đối tượng từ các hàm hằng số, cộng và trừ,
liên kết chúng bằng compiler C++ của máy chủ, rồi chạy executable để đối chiếu
giá trị trả về thực. Lệnh assembly trên ARM64 đã được lắp ráp độc lập bằng Clang
và đối chiếu với byte được phát trực tiếp. Bộ phát hiện hỗ trợ:

- `hàm f() { trả về <hằng i32>; }`
- `hàm f() { trả về (9 + 3) * (8 - 4); }`: tính trước biểu thức chỉ gồm
  hằng nguyên, dấu âm, `+`, `-`, `*`; kiểm tra **mọi bước trung gian** nằm
  trong `int32` trước khi phát lệnh trả về hằng (ví dụ cho kết quả `48`).
- `hàm f(a, b) { trả về a + b; }` với giả định cả hai đối số đều là i32.
- `hàm f(a, b) { trả về b + a; }` vì phép cộng hai tham số giao hoán.
- `hàm f(a, b) { trả về a - b; }` với đúng thứ tự đối số ABI `a`, rồi `b`.

Phép cộng/trừ raw chạy theo toán học **modulo 2³²** như lệnh CPU; ở nhánh
leaf thử nghiệm hiện tại chưa có đường kiểm tra tràn và ném lỗi V++.
Đường **hàm lá 16 kiến trúc** từ chối phép `b - a` để không phát nhầm `a - b`.
Trên x86-64/AArch64, Machine IR tổng quát hỗ trợ `b - a` đúng thứ tự.
Tính hằng không nhận phép chia, gọi hàm hoặc biểu thức có tác dụng phụ;
trường hợp tràn ở giữa như `(2147483647 + 1) - 1` cũng bị từ chối.

Ở đường **hàm lá 16 kiến trúc**, những hàm có side effect, điều khiển luồng,
kiểu động, lời gọi lồng nhau hoặc ABI runtime V++ sẽ bị từ chối ở bước
lowering. **Riêng x86-64/AArch64** đã phát được lời gọi direct từ Machine IR,
có relocation tại object để trình liên kết giải quyết symbol callee.
Hai backend vẫn dùng frame cố định, chưa có register allocator, linker V++
hay định dạng executable hoàn chỉnh.
Object writer nhận cả hàm lá và byte từ Machine IR thông qua
`emitRawObjectBytes`. Relocation của lời gọi direct hiện chỉ hỗ trợ ELF
x86-64/AArch64, Mach-O x86-64/AArch64 và COFF x86-64; những tổ hợp
kiến trúc/object khác chưa được phép phát CALL chưa liên kết.
PowerPC64 big-endian ELFv1 có
descriptor `.opd` và relocation `R_PPC64_ADDR64` trỏ về `.text`; các target
còn lại ở đường **hàm lá** chưa cần relocation. RISC-V 64 và LoongArch64 xuất cờ ELF LP64D mặc
định; chưa có lựa chọn ABI soft-float riêng. Kiểm tra header/section/symbol
chưa chứng minh mọi object sẽ liên kết hoặc chạy được ở từng ABI, đặc biệt
cần kiểm chứng thêm ABI MIPS. Các tệp `.s` ngoài x86-64/AArch64 cần được kiểm tra bằng assembler
và emulator đúng target trước khi dùng trong sản phẩm.

### Smoke test liên kết và chạy mã máy

`scripts/quality/run_cross_native_smoke.sh` lấy 11 hàm `tra42`, `tra_am`,
`tra_i32`, `tra_i32_am`, `tra_i32_am_ngoac`, `tinh_hang`, `tinh_am`,
`tinh_sau`, `cong`, `cong_dao`, `tru`
trong fixture `.vi`, gọi `vpp-cli --emit-obj` từng hàm,
biên dịch harness C bằng compiler **của kiến trúc đích**, liên kết executable
tĩnh rồi chạy trực tiếp qua QEMU user-mode. Chương trình C đối chiếu giá trị
âm, dương, biên `int32` và hai tham số theo ABI. Sai khác kết quả, lỗi linker
hoặc lỗi chạy QEMU đều trả mã lỗi khác 0 và khiến smoke test thất bại.

Ví dụ chạy trên máy Linux đã cài cross compiler và QEMU user:

```sh
bash scripts/quality/run_cross_native_smoke.sh "$PWD/build/bin/vpp-cli" \
  powerpc64-linux-gnu powerpc64-linux-gnu-gcc qemu-ppc64 build/smoke-ppc64
bash scripts/quality/run_cross_native_smoke.sh "$PWD/build/bin/vpp-cli" \
  riscv64-linux-gnu riscv64-linux-gnu-gcc qemu-riscv64 build/smoke-riscv64
bash scripts/quality/run_cross_native_smoke.sh "$PWD/build/bin/vpp-cli" \
  loongarch64-linux-gnu loongarch64-linux-gnu-gcc qemu-loongarch64 build/smoke-loongarch64
```

GitHub Actions `native-cross-smoke` sử dụng runner Ubuntu 26.04 và ba bộ
cross GCC/QEMU tương ứng để thực thi các bài này. Nếu chưa có log CI xác
nhận, đây vẫn là **kiểm thử đã cấu hình, chưa phải bằng chứng ba CPU chạy
thành công**. Smoke script đã liên kết/chạy thành công ở chế độ `native`
trên macOS ARM64; CPU khác cần kết quả QEMU hoặc máy thật.

**Cấu trúc phẳng, theo hai JIT tham chiếu** `src/native/aarch64_jit.cpp` và
`src/native/x64_jit.cpp`: mỗi CPU/endianness có tệp
`src/native/<arch>_codegen.cpp` độc lập (16 tệp), chứa cả opcode encoder
và cú pháp assembly. Không tạo thư mục backend phụ, không gộp các biến thể
POWER/MIPS/SPARC. Tên `_codegen` phân biệt bộ phát thân hàm AOT/raw với
hai JIT chạy trên host hiện có. `src/native/raw_backend.h` chỉ giữ hợp đồng
chung và thao tác ghi word theo endian; `src/native/raw_codegen.cpp` giữ
lowering IR, kiểm tra nhãn và phân phối tới từng encoder.

Phía V++ giữ các tệp phẳng `gói/biên dịch/mã máy <arch>.vi` cùng cấp với
`mã máy.vi`; `mã máy nền.vi` ghép byte. Kiểm thử parity đối chiếu các byte
đầu ra C++ và V++ trên 16 biến thể, gồm biên `int32` RISC-V và s390x.
Backend s390x dùng `LHI` cho hằng `int16`, `LGFI` cho toàn miền hằng `int32`.
Bootstrap V++ hiện còn chia sẻ encoder trong cùng họ ISA. Đường CLI
`--emit-raw`/`--emit-asm`/`--emit-obj` đã gọi V++ để hạ IR/phát byte hàm lá;
x86-64/AArch64 còn có Machine IR tổng quát bằng V++. C++ vẫn làm frontend
và in assembly. **Bước xuất object của `--emit-obj` đã được chuyển sang
`gói/biên dịch/đối tượng.vi`**; C++ object writer được giữ làm oracle.

**Object writer parity (13/10/2026):** CTest `vpp-native-object-parity`
gọi cùng byte máy/symbol/relocation vào bộ ghi V++ và C++, so sánh
toàn bộ file object theo byte. Đã đạt **38/38 ca**: 32 ca độc lập gồm 16 ELF Linux,
ARMv7 GNU/musl hard-float, Mach-O AArch64/x86-64, COFF i386/x86-64
(gồm tên symbol dài), nhiều CALL relocation trên ELF/Mach-O/COFF và
các ca lỗi không được phép fallback. Sáu ca bổ sung so toàn bộ output
đường tích hợp **lowering → encoder → object** trong một VM V++ với C++
oracle, gồm ba target hàm lá và ba target CALL. `vpp-raw-object-host-link`,
`vpp-native-control-flow-host-link` và `vpp-native-calls-host-link`
trên macOS ARM64 cũng đạt.
Điều này xác minh byte parity và khả năng liên kết/thực thi ở host;
không chứng minh việc liên kết trên mọi kiến trúc chéo, hoặc AOT toàn
chương trình. Test nguồn ở `scripts/quality/nativeobjectparity.cpp`.

**Parser triple (13/10/2026):** `vpp build --target` gọi
`parseTargetVpp` trong `src/cli/native_vpp_codegen.cpp` để thực thi
`biên dịch phân tích đích` từ `gói/biên dịch/đích.vi`. Parser V++ xử lý
16 kiến trúc, alias, vendor, hệ điều hành và ABI như `src/native/target.cpp`;
`biên dịch bộ ba chuẩn` tạo tên triple canonical. CTest
`vpp-target-vpp-cpp-parity` đối chiếu với parser C++ độc lập trên 16 tên CPU,
13 triple bổ sung và 10 trường hợp bị từ chối (39/39); toàn bộ 4/4
CTest `vpp-target-*` đã đạt. C++ vẫn nhận diện host và cung cấp oracle.

Để kiểm tra lời gọi liên object và relocation (sau khi cấu hình/build CMake):

```sh
ctest --test-dir build --output-on-failure -R 'vpp-(raw-machine|raw-object|target-|native-)'
build/bin/vpp-cli build --target aarch64-linux-gnu --emit-obj ket_hop \
  src/tests/kiem_tra_native_calls.vi build/ket_hop-aarch64-linux.o
build/bin/vpp-cli build --target x86_64-windows-msvc --emit-asm ket_hop \
  src/tests/kiem_tra_native_calls.vi build/ket_hop-win64.s
```

Chỉ file `.o` với đầy đủ các symbol callee được linker của hệ đích giải
quyết mới có thể gọi hàm thành công. Các file trên chưa tự tạo executable
Linux/Windows; thử nghiệm thực thi chéo vẫn phải chạy bằng linker và
runner/emulator tương ứng.

### Registry đa kiến trúc (bước nền tảng)

`src/include/vpp/native/target.h` và `src/native/target.cpp` đã cung cấp
registry CPU/OS/ABI/object-format và parser triple có chẩn đoán lỗi.
`gói/biên dịch/đích.vi` cung cấp metadata kiến trúc bằng V++ (`chọn/ca`) để
bootstrap compiler self-host; có regression fixture riêng kiểm tra hàm V++.
Đường `build --target` đã gọi parser triple V++ qua bridge, còn
`--list-targets` và các API C++ độc lập vẫn dùng registry C++.

Các nhóm CPU được nhận diện: **x86-64, AArch64, RISC-V 64, ARM32, x86 32-bit, POWER,
MIPS, LoongArch, s390x và SPARC**. Registry phân biệt endianness và biến thể
32/64-bit, tổng cộng **16 target CPU**: `x86_64`, `aarch64`, `riscv64`,
`armv7`, `i386`, `powerpc`, `powerpc64`, `powerpc64le`, `mips`, `mipsel`,
`mips64`, `mips64el`, `loongarch64`, `s390x`, `sparc`, `sparc64`.

```sh
build/bin/vpp-cli --list-targets
build/bin/vpp-cli build --target riscv64-unknown-linux-gnu src/tests/kiem_tra_machine_jit.vi
```

Lệnh đầu tiên liệt kê các kiến trúc cùng định dạng object (ELF,
PE/COFF hoặc Mach-O); lệnh thứ hai **từ chối rõ ràng** vì native AOT toàn
chương trình chưa được triển khai. Dùng `--emit-raw`, `--emit-asm`, hoặc
`--emit-obj` như ví dụ ở trên để phát **một hàm lá i32**. `vpp build <file.vi>`
vẫn chỉ xác minh pipeline tạo bytecode trong bộ nhớ. Registry và object writer
không đồng nghĩa executable toàn chương trình đã được triển khai.

| Backend JIT thử nghiệm có thật | Kiến trúc còn lại |
| --- | --- |
| macOS AArch64, Linux AArch64/x86-64, Windows x86-64 (chỉ host tương ứng) | RISC-V 64, ARM32, x86 32-bit, POWER, MIPS, LoongArch, s390x, SPARC đã có raw i32 leaf emitter nhưng **chưa có JIT runtime hoặc executable AOT** |

**Việc tiếp theo để thật sự chạy được trên các CPU còn lại:** lower IR chung
sang machine IR có type/CFG; emitter + register allocation + stack-frame và
relocation riêng cho từng ISA; mở rộng relocation của object writer và linker theo ELF/Mach-O/PE;
ABI/runtime bridge, unwind/GC safepoints; cross-compile và kiểm thử chạy thật
trên máy hoặc emulator của từng target. Trước khi hoàn thành các bước này,
CLI trả lỗi thay vì âm thầm quay về bytecode khi người dùng yêu cầu native.

Tài liệu này nói về **mã máy thực sự** (AOT/JIT), khác với việc chuyển các
primitive hệ điều hành sang thư viện `.vi` qua FFI. Đối với việc giảm primitive,
xem [kiểm toán biên native](native-boundary-audit.md) và
[kế hoạch System FFI](system-ffi-migration-plan.md).

## 1. Điểm xuất phát đã xác minh

- `src/include/vpp/compiler/pipeline.h`: `CompilationArtifacts` giữ AST,
  semantic, IR, bytecode và thông tin debug.
- `src/compiler/ir.cpp` + `src/include/vpp/compiler/ir.h`: IR hiện có
  `Call`, `CallDynamic`, `Intrinsic`, `ForeignCall`, điều khiển luồng.
- `src/compiler/codegen.cpp`: `emitDirectBytecode()` chỉ tạo `Instruction`
  cho VM, chưa phát mã máy hoặc object file.
- `src/runtime/vm.cpp`: `VM::runJitCompiled()` xây `std::function<void()>`
  cho từng opcode; các lệnh phức tạp đi qua dispatcher C++.
- `src/native/aarch64_jit.cpp`: bộ phát lệnh máy AArch64 macOS/Linux;
  `src/native/x64_jit.cpp`: bộ phát x86-64 SysV Linux và Win64 Windows.
  Tất cả đều opt-in qua `VPP_ENABLE_MACHINE_JIT=1`.
- `src/include/vm/vm.h`: `StackValue`, call frame, try frame, GC roots,
  module state và foreign resources được VM sở hữu. Không được bỏ qua
  những trạng thái này khi chạy mã máy.

**Mục tiêu:** `vpp build --target <triple> <source.vi>` tạo executable native
hoặc object hợp lệ, có cùng hành vi quan sát được với interpreter trên tập
tính năng được hỗ trợ. Không quảng cáo `VPP_ENABLE_JIT=1` là machine-code JIT.

### 1.1. JIT mã máy thực tế đã có (MVP)

Trên **macOS ARM64, Linux ARM64/x86-64 hoặc Windows x86-64**, chạy
một file `.vi` bằng CLI được build cho đúng hệ điều hành:

```sh
VPP_ENABLE_MACHINE_JIT=1 build/bin/vpp-cli src/tests/kiem_tra_machine_jit.vi
VPP_ENABLE_MACHINE_JIT=1 VPP_MACHINE_JIT_STATS=1 \
  build/bin/vpp-cli src/tests/kiem_tra_machine_jit.vi
```

Windows PowerShell (x64, bản dựng CMake đa cấu hình):

```powershell
cmake -S . -B build
cmake --build build --config Release
$env:VPP_HOME = (Get-Location).Path
$env:VPP_ENABLE_MACHINE_JIT = "1"
$env:VPP_MACHINE_JIT_STATS = "1"
& .\build\bin\Release\vpp-cli.exe src/tests/kiem_tra_machine_jit.vi
```

Linux x86-64/aarch64 (cần các phụ thuộc build của dự án, gồm libffi):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
VPP_HOME="$PWD" VPP_ENABLE_MACHINE_JIT=1 VPP_MACHINE_JIT_STATS=1 \
  build/bin/vpp-cli src/tests/kiem_tra_machine_jit.vi
```

Đường chạy này phát và thực thi **lệnh máy thật**. macOS ARM64 sử dụng
`mmap(MAP_JIT)` với cơ chế bảo vệ ghi; Linux ARM64/x86-64 dùng `mmap`
RW → `mprotect` RX; Windows x86-64 dùng `VirtualAlloc` RW →
`VirtualProtect(PAGE_EXECUTE_READ)` và `FlushInstructionCache`:

- Biểu thức hằng `int32`: `OP_BIEN_SO`, `OP_DUNG_GIA_TRI`,
  `OP_SAI_GIA_TRI`, `OP_CONG`, `OP_TRU`, `OP_NHAN` và 6 opcode so sánh
  (`==`, `!=`, `>`, `<`, `>=`, `<=`). Tối đa 96 bytecode
  và 7 thanh ghi làm việc cho một region. Chỉ chọn đoạn có một kết quả
  hợp lệ; không băng qua đích nhảy/điểm vào của handler.
- `+`, `-`, `*`, 6 phép so sánh và `OP_Logic_VA`/`OP_Logic_HOAC`
  trên hai `StackValue` runtime đều là `int`:
  gọi hàm mã máy nhận hai tham số AArch64 ABI; trường hợp không chắc chắn
  về kiểu hoặc tràn `int32` trong phép số học được xử lý bởi VM gốc.
- `OP_JUMP_IF_FALSE` với điều kiện `int` dùng mã máy `CBZ` để chọn PC
  nhánh đúng/sai theo ABI 64-bit; VM vẫn kiểm tra kiểu, quản lý stack và
  thực thi các opcode ở địa chỉ kế tiếp. Verifier phải từ chối địa chỉ nhảy
  nằm ngoài bytecode; nhánh native còn kiểm tra lại địa chỉ được chọn để
  giữ nguyên lỗi runtime nếu bytecode chưa được xác thực. Đây chưa phải
  native control-flow graph chạy độc lập.
- Hàm V++ **thuần, tuyến tính**, có đúng **0–2 tham số `int32` bắt buộc**
  được phát thành một entrypoint AArch64 độc lập nhận đối số qua `w0/w1`
  và trả số nguyên qua `x0`. Bộ phát mã chấp nhận literal, đọc tham số
  `OP_PARAM`/`OP_TEN_BIEN_GIA_TRI`, `+`, `-`, `*`, sáu phép so sánh,
  `OP_Logic_VA`/`OP_Logic_HOAC`, `OP_KHONG`/`OP_PHU_DINH`
  (logic trên số nguyên)
  và `OP_TRA_VE`; chấp nhận `OP_KHOI_TAO` khi nó đứng ngay trước
  `OP_PARAM` cùng slot và các dấu khối ngoài do compiler phát.
  VM vẫn kiểm tra **arity, call depth và kiểu đối số** trước lời gọi native.
  Native tính toán phép cộng/trừ có kiểm tra cờ overflow và nhân bằng
  kết quả signed 64-bit rồi kiểm tra có biểu diễn được bằng int32.
  Nếu phát hiện tràn, VM chạy lại **toàn bộ hàm gốc** để bảo toàn chẩn đoán
  và hành vi interpreter. Không native hóa hàm có biến global, closure,
  default argument, nhánh/loop, gọi hàm khác, exception, FFI hoặc GC.
  Đối với những hàm đó, execution context/call frame vẫn chạy qua VM.
- **Native-to-native `BL`/`CALL rel32` (MVP)**: khi một hàm tuyến tính thuần gọi trực tiếp
  hàm V++ thuần khác qua `OP_GOI` với **function ID cố định** và đúng 0–2
  đối số, JIT ghép các hàm thành **một vùng mã** và vá địa chỉ `BL`/`CALL`
  PC-relative sau khi xác định layout. Hỗ trợ chuỗi lời gọi lồng nhau,
  nhiều lời gọi trong một biểu thức, và kết hợp hàm 0/1/2 đối số.
  AArch64 bảo toàn `LR`, `x19/x20`, các thanh ghi tạm `x9..x15`, và căn chỉnh
  `SP` 16 byte. x86-64 dùng các ô nhớ stack cho biểu thức; giữ alignment
  16 byte trước `CALL`, dành 32 byte shadow space trên Windows,
  lấy tham số từ `edi/esi` (Linux) hoặc `ecx/edx` (Windows), trả kết quả
  `int64` qua `rax`. Sentinel tràn số truyền xuyên qua các cuộc gọi
  và kích hoạt chạy lại bytecode gốc.
  Trước khi sử dụng, JIT loại đồ thị có vòng gọi, callee thiếu/không thuần,
  sai số tham số, hoặc độ sâu tĩnh vượt 32; VM cũng kiểm tra độ sâu còn lại
  theo `maxCallDepth` tại lúc gọi. Hàm cần closure, receiver, branch, module,
  FFI, exception, GC, hay call by name tiếp tục do VM thực thi.
- Chạy bên trong cùng VM; VM giữ quyền sở hữu stack, control flow, GC,
  callback, exception, FFI và module. Chỉ hợp nhất vùng hằng khi không có
  switch frame đang mở để giữ nguyên quy tắc `chọn/ca`.
- `VPP_MACHINE_JIT_STATS=1` ghi số region/lệnh đã chạy bằng mã máy ra
  `stderr`. Chỉ số đo phần công việc được native hóa, **không** phải số
  lệnh máy hoặc tỷ lệ tăng tốc.

`VPP_ENABLE_MACHINE_JIT` là **tùy chọn thử nghiệm** trên các nền tảng
được nêu ở trên. Kiến trúc khác báo lỗi nền tảng chưa hỗ trợ.
`VPP_ENABLE_JIT` vẫn dùng JIT dạng closure C++ trước đây. Kết quả và
đường lỗi cần so sánh bằng `src/tests/kiem_tra_machine_jit.vi` với
`src/tests/expected/kiem_tra_machine_jit.expected`; `run_tests.sh` chạy
trên Darwin ARM64 hoặc Linux ARM64/x86-64, `scripts/windows/run-tests.ps1`
chạy Win64. Hardening C++ so sánh bytecode/closure/native, kiểm tra
integer boundary, nhánh, lỗi tràn, DAG, vòng gọi và giới hạn call depth.

**Tình trạng xác minh tại thời điểm triển khai**: build và CTest hardening
trên macOS ARM64 đã chạy thành công. Emitter x86-64 SysV đã thực thi smoke
test bằng Rosetta x86-64 trên macOS, gồm `CALL rel32`, tràn số và nhánh.
Đường phát lệnh Win64 (`ecx/edx/r8`, shadow space 32 byte và nested CALL) cũng
đã qua smoke test Rosetta với ABI `ms_abi` và Win32 memory API mô phỏng;
đó **không phải** kiểm thử Windows thật. GitHub Actions có các bước kiểm tra
`vpp-runtime-p0-hardening` và so sánh golden output, kèm xác nhận số vùng mã
máy được chạy, trên `ubuntu-latest` (Linux x86-64), `ubuntu-24.04-arm`
(Linux ARM64) và `windows-latest` (Win64). Thư viện V++ chuyển kết quả
`close()`/`closesocket()` trả 0 khi thành công thành giá trị `đúng` của API
`đầu nối đóng()`, đồng thời báo lỗi nếu đóng thất bại. Linux và Windows
vẫn cần kết quả CI thực tế để xác nhận khả năng phát hành.

**Giới hạn còn lại:** đây là JIT cục bộ cho số nguyên, phép chọn PC,
**native leaf-function invocation** và **`BL` trong đồ thị hàm thuần, không vòng**.
Chưa hỗ trợ full native call frames mang `StackValue`/GC roots,
chuỗi basic block nhảy
trực tiếp hay string/list/GC trong mã máy,
chưa phát object/executable Mach-O
độc lập, chưa có runtime ABI native công khai. Việc phát trang mã cho từng
region còn tốn chi phí khởi tạo/bộ nhớ và chưa được chứng minh nhanh hơn
interpreter trên workload ứng dụng. Các mốc NCG bên dưới vẫn cần thực hiện.

## 2. Quyết định kiến trúc

1. Dùng compiler pipeline hiện có làm frontend chung; thêm native backend **sau
   semantic/IR**, không biên dịch lại source `.vi` bằng parser riêng.
2. JIT thử nghiệm chỉ hỗ trợ các vùng biểu thức nguyên thuần túy đã mô tả
   tại §1.1. Đối với native backend tổng quát, ưu tiên **AOT** để giới hạn
   rủi ro executable memory, relocation động, bảo vệ W^X, unwind và cache
   khi ABI chưa ổn định.
3. Bắt đầu một target được kiểm thử thật: **macOS arm64 / AArch64**;
   mở rộng Linux x86_64 rồi Windows x86_64 sau. Không được tự nhận target
   hỗ trợ chỉ vì có thể tạo tệp bytes.
4. Mã máy gọi runtime V++ bằng một **C ABI versioned** cho giá trị động,
   cấp phát, GC roots, exception, intrinsic và FFI. Runtime substrate vẫn
   là mã native cần thiết cho tới khi V++ có runtime tự lưu trữ an toàn.
5. Native backend không gọi `VM::run()` để thực hiện *toàn bộ* chương trình.
   Fallback chỉ được phép cho opcode cụ thể có khai báo và được đo trong test.
   Nếu một construct chưa hỗ trợ, phát diagnostic ở compile time hoặc
   fallback có kiểm soát theo flag, tuyệt đối không âm thầm sinh kết quả sai.
6. Tất cả target object/linker, ABI và symbol mang tên riêng; không sửa enum
   opcode hoặc format bytecode chỉ để phục vụ native backend.

Luồng dữ liệu đề xuất:

```text
source .vi -> Parser -> Semantic -> IR -> optimizer
                                |             |
                                |             +-> bytecode -> VM hiện hành
                                +----------------> Native lowering
                                                    -> machine IR
                                                    -> AArch64 emitter
                                                    -> Mach-O object
                                                    -> linker + V++ runtime
                                                    -> executable
```

## 3. Hợp đồng runtime và ABI (hoàn tất trước emitter)

Định nghĩa header mới `src/include/vpp/native/runtime_abi.h` theo C ABI;
triển khai bridge nội bộ trong `src/runtime/native/` (đường dẫn đề xuất).
Lưu tài liệu version, kiểu số và ownership cạnh header:

- `VppValue` là biểu diễn opaque/được cố định kích thước, **không** ép
  `std::variant`/`StackValue` thành ABI nhị phân công khai. Bridge cung cấp
  tạo/đọc `int32`, `double`, `bool`, `null`, string, boxed 64-bit, object handle.
- `VppNativeContext *` bao gồm globals, modules, capability set, debug spans,
  source locations, GC roots, exception state và foreign handle owner.
- Với mỗi hàm compiled, xác định parameter/result convention, thanh ghi
  caller/callee saved, stack alignment, stack growth, symbol mangling,
  lifetime của value và điểm safepoint. Ghi rõ khi call có thể GC hoặc ném.
- Root mọi live handle trước safepoint hoặc dùng shadow stack; hàm compiled
  phải tháo root khi return hay throw. Không giữ con trỏ thô qua lần GC.
- Exception V++ đi qua status/result bridge có kiểm soát; không unwind C++
  trực tiếp xuyên qua frame mã máy khi chưa có unwind metadata hợp lệ.
- `ForeignCall` phải dùng descriptor + capability + buffer extent đang được
  verifier/FFI kiểm tra; không phát lệnh gọi pointer không kiểm chứng.

## 4. Các lát cắt triển khai độc lập

### NCG-0 — Khóa baseline, parser CLI và báo lỗi rõ ràng

- Thêm CLI opt-in `vpp build --target ... --output ...` (hoặc lệnh tương đương
  theo convention CLI đã có); **không thay đổi** `vpp <file.vi>` mặc định.
- Xác thực triple, kiến trúc host, linker có sẵn; target chưa hỗ trợ phải báo
  lỗi rõ. Tạo test cho trường hợp lệnh build chưa được hỗ trợ.
- Ghi snapshot parity interpreter/JIT trên: toán học, hàm, closures,
  collections, throw/catch, import, unicode, FFI.

### NCG-1 — Biên dịch leaf functions sang mã máy

- Thêm `src/include/vpp/native/{target,lowering,emitter}.h` và
  `src/native/{lowering,aarch64,object}.cpp` (đường dẫn đề xuất, chỉnh CMake).
- Chuyển một tập IR có kiểu rõ (literal i32, +, -, *, so sánh, nhánh,
  gọi hàm tĩnh/return) sang machine IR với basic blocks và labels.
- Emitter AArch64 phải kiểm soát immediate range, signed overflow, divide by
  zero, calling convention, stack 16-byte aligned và relocation.
- Phát **Mach-O object thực** và link executable với entrypoint/runtime shim;
  đối chiếu stdout, stderr và exit code với interpreter. Không pass bằng cách
  chỉ sinh assembly dạng văn bản hoặc wrapper gọi interpreter.

### NCG-2 — Hỗ trợ giá trị động và cấu trúc của V++

- Thêm load/store globals, locals, gọi hàm có tham số/default, strings,
  list/map/tuple, `CallDynamic`, `Intrinsic`; gọi runtime ABI cho phép
  thao tác VM representation khi cần.
- Preserving short-circuit `&&`/`||`, thứ tự evaluation, truthiness, phép
  toán boxed i64/u64, NUL/UTF-8, lỗi chỉ số và tác dụng phụ.
- Module initializer chạy đúng một lần theo dependency; import cycle giữ
  cùng hành vi với VM. Unsupported opcode nhận diagnostic có source span.

### NCG-3 — GC, exception, debug, FFI

- Root tracking/safepoint, worker-cancel, exception `thử/bắt/ném`, stack trace
  phải parity với interpreter; ghi test cho throw xuyên nhiều frame.
- Foreign descriptors và capabilities giữ nguyên semantic; thêm negative
  tests cho ABI sai, NUL, buffer extent, handle khác VM và quyền bị từ chối.
- Runtime không để foreign resource rò khi throw hoặc process exit.

### NCG-4 — Release và tối ưu

- Chỉ bật native làm default sau khi cùng tập golden tests pass cho target.
  Test binary chạy từ thư mục khác để phát hiện phụ thuộc sai vào repo cwd.
- Chạy benchmark p50/p95 trên cùng workload với interpreter/closure JIT;
  tách thời gian compile, startup, peak RSS và throughput; giữ số thô.
- Thêm target Linux/Windows qua test runner thật và ABI/linker riêng, không
  suy luận khả năng tương thích từ macOS.

## 5. Quy tắc làm việc cho coding agent

Mỗi PR chỉ nên chứa **một** lát cắt dùng được và kiểm thử được. Với mỗi lát:

1. Đọc API và các golden fixtures liên quan, xác nhận trạng thái trước sửa.
2. Viết contract input/output và error mode ngắn gọn; đặt diagnostic fail-fast
   cho trường hợp chưa hỗ trợ.
3. Implement compiler + runtime bridge cần thiết cho lát cắt; không sao chép
   toàn bộ VM hoặc thư viện chuẩn vào emitter.
4. Bổ sung test chạy **executable sinh ra** và đối chiếu cùng nguồn `.vi`
   chạy interpreter và closure JIT, cả thành công và lỗi.
5. Chạy build, verifier/runtime, integration liên quan; `git diff --check`;
   báo rõ OS/target nào chưa được kiểm chứng.

**Điều kiện nghiệm thu Native Code Generator 1.0:** tạo executable và object
native thật, vượt qua toàn bộ bài parity đã cam kết trên mỗi target công bố;
có ABI version, root/exception correctness, link/deployment docs, kiểm thử
cross-module và FFI; không còn opcode được hỗ trợ phải quay về interpreter
ẩn. Self-host compiler hoặc viết lại kernel bằng V++ là dự án độc lập sau mốc
runtime/native ABI ổn định.

### Tích hợp native AOT từ frontend V++ (14/10/2026)

CLI có tùy chọn `--emit-obj-selfhost` để gọi trực tiếp parser V++, bộ dựng
IR từ AST (`gói/biên dịch/mã máy từ cây.vi`), lowerer và encoder V++ rồi
đóng gói object. Trình biên dịch VM C++ vẫn bootstrap module compiler này;
`--emit-obj` truyền thống vẫn nhận IR từ frontend C++. Cả hai luồng có
giới hạn i32 và ABI như mô tả phía trên.

```sh
build/bin/vpp-cli build --target aarch64-apple-darwin --emit-obj-selfhost \
  sum src/tests/nativeaotsmoke.vi /tmp/sum.o
```

Kiểm thử `vpp-native-selfhost-host-link` xuất object cho `twice`, `sum`
và `other` bằng lệnh trên, từ thư mục build với `VPP_HOME` trỏ tới bộ
thư viện V++ của dự án. Harness C chạy 41 đầu vào và đối chiếu cả CALL
relocation, vòng lặp `bỏ qua`/`thoát`, biến cục bộ và tham số trùng tên
hàm. Test đồng thời yêu cầu phép chia chưa hỗ trợ phải trả lỗi và không
sinh artifact. Test này chỉ được đăng ký khi CMake nhận ra compiler và
target host native tương thích; Windows COFF chưa được harness này xác
nhận trên Windows thực tế. `src/tests/kiemtranativetuchu.vi` kiểm tra
logic self-host trực tiếp trong V++ (interpreter và JIT).
Trên macOS ARM64 ngày 14/10/2026, CTest smoke `vpp-native-selfhost-host-link`
đạt với cả nguồn `.vi` nằm ngoài repository; năm bài kiểm thử parity và
host-link khác cũng đã đạt. Cùng ngày, kiểm tra riêng giá trị mặc định
số nguyên âm trong C++ direct emitter đạt trên interpreter/JIT.

## 6. Native nào vẫn cần giữ?


Ngay cả khi NCG-4 hoàn thành, mã V++ vẫn phải tương tác với kernel/libc,
dynamic loader, TLS/cryptographic provider và ABI máy. Thư viện chuẩn có thể
điều phối toàn bộ **logic ứng dụng** bằng `.vi`, nhưng không thể thay các
syscall, root tracking và chuyển kiểu ABI bằng lời gọi V++ thuần không có
substrate bên dưới. Loại bỏ hoàn toàn C++ chỉ hợp lý sau khi bổ sung runtime,
allocator/GC, OS ABI và bootstrap viết bằng V++ rồi chứng minh tương đương
trên từng nền tảng.
