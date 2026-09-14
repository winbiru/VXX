# Tiến độ phát triển V++

> Cập nhật: 14/09/2026
>
> Tỷ lệ dưới đây được tính theo số checkbox trong roadmap, chỉ dùng để theo dõi
> tiến độ đầu việc; không đại diện cho phần trăm khối lượng kỹ thuật thực tế.

## Tổng quan

| Giai đoạn | Hoàn tất | Còn lại | Tỷ lệ |
| --- | ---: | ---: | ---: |
| Ngắn hạn | 14/15 | 1 | 93% |
| Trung hạn | 18/18 | 0 | 100% |
| Dài hạn | 15/40 | 25 | 38% |
| **Tổng** | **47/73** | **26** | **64%** |

> Số liệu dài hạn được đếm trực tiếp từ 40 checkbox trong phần `Việc lớn còn lại`
> của `plans/long-term.md`; 5 checkbox ở `Nền tảng đã có` là baseline lịch sử và không
> cộng thêm vào mẫu số roadmap. Bảng tổng quan vì vậy phản ánh đúng các đầu việc còn được
> theo dõi thay vì cộng baseline đã hoàn thành vào tiến độ roadmap.

## Đích V++ 1.0

Roadmap phát hành mới nằm tại `plans/roadmap-1.0.md`. Từ thời điểm này, các plan
ngắn/trung/dài hạn vẫn theo dõi công việc kỹ thuật theo thời gian, còn roadmap 1.0 dùng
để quyết định **thứ tự ưu tiên và release gate**.

| Mốc | Trạng thái | Trọng tâm còn lại |
| --- | --- | --- |
| 0.7 | Hoàn tất | Production compiler dùng registry/context tường minh; compatibility facade chỉ còn cho caller cũ |
| 0.8 | Gần hoàn tất | Sanitizer/leak gate Linux |
| 0.9 | Chưa hoàn tất | Package solver + lockfile, stdlib audit, test/tooling/LSP |
| 1.0 RC | Chưa bắt đầu | Freeze contract, fuzz/stress/sanitizer, release smoke đa nền tảng |

Ước lượng theo khối lượng kỹ thuật hiện tại: còn khoảng **25–30%** để đạt 1.0. Con số
này không thay thế tỷ lệ checkbox ở bảng trên vì hardening/stress/release có chi phí lớn
hơn nhiều so với một checkbox tính năng thông thường.

## Đã xác nhận hoàn thành

- [x] CMake/CTest đã tách các target core, frontend, bytecode, compiler, runtime,
  tooling, CLI và các C++ unit test.
- [x] Regression V++ được nối vào CTest trên Unix/Windows; CI Ubuntu có sanitizer.
- [x] `examples/api_project` và `templates/backend` đã chuyển sang kiến trúc OOP theo
  instance: `CấuHìnhApi`, controller triển khai `TrìnhXửLýHttp`, `BộĐịnhTuyến`,
  `MáyChủApi` và `ApiApplication`; dependency được ghép tại composition root rồi inject
  qua constructor. Lệnh `khởi tạo backend` copy đầy đủ các module OOP và smoke test
  xác nhận project sinh mới vẫn phục vụ `/health` thành công.
- [x] Regression `fixtures/api_project` cũng đã chuyển khỏi kiểu gọi class như namespace:
  contract nằm riêng trong `giao_dien/`, còn `UserRepository` + `UserMapper` được inject
  vào `UserService`, rồi `UserService` + `ResponseEntity` được inject vào controller.
  `AdminRestController` dùng kế thừa thật từ `UserRestController`; test tạo object graph tại
  composition root và gọi CRUD/search qua instance để khóa OOP + DI end-to-end.
- [x] `docs/bytecode.md` đã phân biệt rõ proposal `.vbc` với runtime contract hiện tại
  (`Instruction`/`Opcode`) và chỉ ra implementation đang thực thi.
- [x] Pipeline đã có Expression AST, scope tree, name resolution, recursive IR và
  direct IR backend cohort đầu.
- [x] Có regression cho lỗi parser/compiler/semantic, không chỉ expected-output fixture.
- [x] Test discovery đã phân biệt `test/` cho C++ unit và `src/tests/` cho regression
  V++; HTTP fixture được khởi động riêng trong integration runner.
- [x] Parity gate hiện đối chiếu 94 chương trình `.vi`; direct IR backend bao phủ
  94/94 chương trình.
- [x] Parity gate chạy lại cùng corpus theo thứ tự đảo trong cùng process; regression
  compile A → B → A đã khóa lifecycle reset giữa nhiều lần biên dịch.
- [x] VM opcode smoke đã có branch (`OP_JUMP`, `OP_JUMP_IF_FALSE`) và call/return
  (`OP_GOI`, `OP_PARAM`, `OP_TRA_VE`) ở mức bytecode trực tiếp.
- [x] VM opcode smoke đã khóa native collection/text adapter qua direct/indirect call,
  boolean/null + unary stack và assignment/increment/decrement; runtime đã có handler
  trực tiếp cho `OP_DUNG_GIA_TRI` và `OP_SAI_GIA_TRI`.
- [x] `VM::run()` đã được thu gọn thành lifecycle/GC + opcode routing; call, value,
  index, variable/call-frame, switch/block, loop-control, exception và branch có
  handler riêng. Baseline regression hiện tại đạt 78/78.
- [x] Stack trace runtime giữ frame có cấu trúc mà không đổi bytecode ABI: source file,
  line/column, function/method và module identity đi qua function/module child VM; CLI
  hiển thị trace từ điểm lỗi về caller và gộp các frame đệ quy liên tiếp. Regression khóa
  cả method chain, function caller, imported-module frame và recursion overflow. Runtime
  diagnostic dùng catalog mã ổn định `VPP-Rxxxx` thay vì dò chuỗi: lỗi mang code + context,
  CLI sinh nhóm lỗi, giải thích và gợi ý sửa cho các nhóm lỗi runtime phổ biến.
- [x] VM opcode matrix hiện khóa arithmetic, logic/comparison boundary, stack,
  branch, call/return, native call, default parameter, switch/default, throw/catch,
  uncaught error và các lỗi boundary chính.
- [x] `VMRuntimeFixture` + `vpp-vm-handler-unit` đã tạo test boundary nội bộ cho
  stack/PC/variables/call frame/control stacks và output sink, cho phép test handler
  trực tiếp mà không phải chạy toàn bộ dispatch/stdout.
- [x] `CompilationContext` hiện sở hữu `StringPool`, function maps, import set và
  class/access state. `vpp-compiler-support-unit` có regression compile song song bằng
  hai context và xác nhận registry không rò chéo giữa hai thread.
- [x] Import resolution dùng `CompilationContext.importResolutionBase`; regression
  song song biên dịch hai `module.vi` cùng tên từ hai thư mục độc lập mà không đổi
  process cwd. CLI chỉ giữ cwd compatibility trong lúc VM thực thi file/database tương đối.
- [x] Direct emitter đã có ranh giới `emitBlock`, `emitStatement` và
  `emitFunctionBody`, làm rõ dữ liệu vào-ra của block/statement/function trước khi
  tiếp tục các refactor compiler lớn hơn.
- [x] **Module semantics Phase 1:** local `.vi` module có identity/export index,
  namespace alias đưa export vào `SemanticEnvironment`, imported function có call kind
  riêng nhưng vẫn giữ runtime-name linking, và lifecycle tracker khóa
  `uninitialized → initializing → initialized/failed`.
- [x] Quality baseline đã có coverage gate 45%, `.clang-tidy` versioned và benchmark
  lặp lại được cho VM dispatch, lexer/compiler và native HTTP helpers.

## Đang thực hiện

- [x] **Compiler state:** `CompilationContext` đã trở thành owner của mutable registry;
  direct codegen, callable lookup và recursive import giờ nhận registry tường minh thay vì
  active context ẩn. CLI, tooling lint và `compileSource()` dùng `CompilationContext`;
  `StringPool`/`hamMap` thread-local chỉ còn là compatibility facade cho test/caller cũ.
  Concurrent compilation + import root độc lập và regression legacy-registry isolation đều có.
- [x] **Bỏ token bridge:** production compiler chỉ còn Direct IR → bytecode. Import local
  dùng metadata AST/IR có cấu trúc và compile đệ quy qua pipeline hiện tại; token-dispatch
  compiler và các `compile*.cpp` cũ đã bị xóa khỏi source/CMake. Region chưa được direct
  emitter hỗ trợ giờ làm compile thất bại với diagnostic tường minh. `materializeIrTokens()`
  chỉ còn phục vụ lossless IR test/debug, không nằm trên production compile path.
- [x] **CI quality:** coverage threshold và clang-tidy chạy trên Ubuntu; macOS đã có
  job build + full CTest thường trực bên cạnh Ubuntu và Windows.
- [x] **Module runtime:** runtime có `ModuleTable` độc lập compiler; CLI chuyển module
  initializer metadata vào VM, initializer chạy dependency-first đúng một lần và giữ
  state `failed` khi khởi tạo ném lỗi. Module semantics 1.0 đã có `công khai nhập`,
  re-export/alias, chặn symbol ẩn và diagnostic import cycle có chuỗi module; package
  resolver/versioning vẫn còn theo roadmap.
- [x] Regression `.vi` cho module runtime đã khóa ba contract end-to-end qua CLI:
  dependency-first initialization, duplicate import chỉ khởi tạo một lần và alias
  namespace vẫn gọi được imported function sau khi initializer chạy.
- [x] **Object model:** runtime substrate đã có `RuntimeClass`/`RuntimeInstance`, method
  table + superclass lookup, instance fields và identity equality trong `StackValue`.
  Semantic/IR/bytecode/VM hiện đã chạy end-to-end `Class(...)`, field read/write
  và bound method call; `.vi` regression `kiem_tra_object_model.vi` khóa contract này.
  Implicit receiver đã chốt dùng `mình` cho current instance và `gốc` cho superclass receiver:
  semantic khai báo cả hai trong method, direct emitter chỉ bind receiver được tham chiếu,
  VM chuyển instance + defining class qua call frame; `gốc.method(...)` bắt đầu lookup từ
  superclass nên bỏ qua override của lớp hiện tại. `self`/`this` không còn là implicit
  receiver. Regression `.vi` cho `mình` hiện có thêm ba stress case: receiver đi qua
  `lặp` + `nếu`, hai instance độc lập/copy qua method parameter, và đệ quy method để khóa
  call-frame receiver. Cú pháp kế thừa chuẩn `lớp Con kế thừa Cha { ... }` đã chạy end-to-end
  (dấu `:` vẫn là alias tương thích mã cũ):
  parser giữ superclass metadata, semantic phân giải type + chặn superclass không tồn tại,
  tự kế thừa và chu trình; direct emitter phát lớp cha trước cả khi khai báo source nằm sau,
  VM tạo `RuntimeClass` với superclass thực. Regression `kiem_tra_ke_thua.vi` khóa cả
  override qua `gốc` và method kế thừa không override. Constructor có tham số hiện dùng
  `hàm khởi tạo(...)`: class call đẩy đối số vào `OP_TAO_DOI_TUONG`, VM bind `mình`, hỗ trợ
  tham số mặc định và giữ instance làm kết quả biểu thức sau khi constructor chạy. Constructor
  lớp cha không tự chạy; lớp con gọi tường minh `gốc.khởi tạo(...)` khi cần. Regression
  `kiem_tra_constructor_tham_so.vi` khóa constructor nhiều tham số, mặc định, hai instance độc
  lập và gọi constructor lớp cha. Visibility qua instance đã hoàn tất cho method: semantic
  suy luận lớp từ constructor/alias và hierarchy để chặn private/protected khi có thể, đồng
  thời VM giữ visibility trong runtime method table để chặn cả receiver động truyền qua
  parameter. Private chỉ gọi trong lớp sở hữu; protected cho lớp sở hữu và subclass.
  Regression `kiem_tra_visibility_instance.vi` khóa đường gọi hợp lệ. Field hiện là thuộc tính
  động, không có declaration/modifier riêng nên theo contract hiện tại là public/dynamic.
- [x] **Interface / triển khai:** `giao diện` khai báo chữ ký method không có thân;
  interface có thể kế thừa nhiều interface qua `kế thừa`, còn class giữ đơn kế thừa class và dùng
  `triển khai A, B` cho nhiều interface. Semantic phân giải interface trong type space, chặn
  target sai/không tồn tại, duplicate và cycle, đồng thời yêu cầu class cung cấp method cùng
  tên + số tham số với visibility công khai. Method kế thừa từ superclass được tính là một
  implementation hợp lệ. Interface là contract compile-time ở milestone này nên không thêm
  runtime vtable/dispatch mới. Regression `kiem_tra_giao_dien_trien_khai.vi` khóa interface
  inheritance, class inheritance và nhiều hợp đồng trong một chương trình chạy thật.
- [x] **Override contract 1.0:** semantic coi method cùng tên ở subclass là override của
  method không-private gần nhất trong hierarchy; override phải giữ cùng arity và không được
  thu hẹp visibility. Constructor `khởi tạo` có lifecycle riêng nên không tham gia override,
  còn method private của lớp cha không tạo ràng buộc override. Pipeline unit test khóa cả
  case hợp lệ và diagnostic sai arity/public→protected/protected→private.
- [x] **Value/scope semantics 1.0:** `docs/semantics.md` đã khóa contract cho scope gán
  biến theo function/lambda, shadowing của parameter, `rỗng`, truthiness, equality theo
  value/identity, ordering và implicit coercion. Runtime dùng chung `stackValueTruthy()` cho
  branch, `!`, `&&`, `||`; `==/!=` dùng chung `sameStackValue()` nên null/reference/mixed-type
  không còn lệch giữa collection helper và toán tử ngôn ngữ. Regression
  `kiem_tra_semantics_gia_tri.vi` khóa các trường hợp biên end-to-end, còn
  `vpp-runtime-value-unit` khóa helper và mixed ordering error.
- [x] **Runtime error contract 1.0:** `docs/runtime-errors.md` phân biệt exception do
  chương trình `ném` với lỗi VM fatal. `LanguageException` giữ nguyên `StackValue` qua child
  VM/function boundary; handler unwind data/block/switch/control state về snapshot của `thử`,
  còn failed child call luôn pop call frame trước khi truyền lỗi. `RuntimeError` mang kind
  `VmFault`/`CallBoundary`/`ModuleInitialization`; lỗi module giữ trạng thái `failed` và không
  chạy initializer lần hai. Regression `kiem_tra_ngoai_le_xuyen_ham.vi` khóa catch + rethrow
  xuyên nhiều hàm, còn `vpp-vm-handler-unit` khóa control-stack/call-frame/module-state cleanup.
- [x] **Type policy 1.0:** ADR `docs/adr/0001-type-policy.md` chốt V++ dùng dynamic typing.
  Binding/parameter/field/return không có kiểu tĩnh bắt buộc; semantic chỉ kiểm tra arity khi
  callable đích biết chắc. Function/method/constructor có biên đối số tối thiểu/tối đa theo
  default parameter; dynamic/indirect/imported call được kiểm tra lại ở VM và không còn tự bù
  `0` cho đối số bắt buộc thiếu. Regression `kiem_tra_kieu_dong_call_boundary.vi` khóa đổi kiểu
  binding + default parameter; pipeline/VM unit khóa direct và runtime arity error.
- [x] **Closure/finalizer semantics 1.0:** closure capture dùng shared cell theo tham chiếu;
  mutation nhìn thấy hai chiều, captured value sống sau khi outer function trả về và nested
  closure dùng chung cell. V++ 1.0 không có destructor/finalizer do người dùng định nghĩa;
  hành vi chương trình không được phụ thuộc thời điểm GC. Regression
  `kiem_tra_closure_capture.vi` và runtime-value/GC unit khóa contract này.
- [x] **Tracing GC:** `RuntimeHeap` theo từng VM đăng ký map/list/tuple/class/instance bằng
  weak registry, mark từ stack, biến global/local, call frame, receiver, class table và
  switch value rồi sweep bằng cách cắt cạnh của graph không reachable. Child VM dùng chung
  heap và kế thừa snapshot root của caller nên GC trong function/module lồng nhau không làm
  mất giá trị còn nằm trên stack bên ngoài. GC chạy mặc định với interval 2048 opcode và
  `VPP_GC_INTERVAL` có thể hạ interval cho stress test. Marker và `trackValue()` đã chuyển
  sang worklist lặp để graph sâu không phụ thuộc native C++ recursion. Unit stress khóa cycle
  4.096 node + burst 1.024 self-cycle; `kiem_tra_gc_mvp.vi` khóa self-cycle list/map,
  instance cycle, graph sâu, allocation burst và nested-call caller root ở interval 1.

## Khoảng trống nền tảng đã chốt

| Năng lực | Trạng thái hiện tại | Milestone dài hạn |
| --- | --- | --- |
| Thread / concurrency / async | Chưa có model tổng quát | Memory model, scheduler/API, synchronization, cancellation |
| FFI | Chưa có | ABI, marshal, ownership, error boundary, native allowlist |
| Reflection / metadata | Chưa chốt policy | Quyết định no-reflection hoặc introspection giới hạn |
| Crypto | Chưa có package chuẩn | Secure random + hash/HMAC + primitive từ implementation kiểm chứng |
| Sandbox / permission | Chưa có | Capability cho file/network/process/env/FFI, enforce tại runtime boundary |
| Profiler | Benchmark có, profiler còn thiếu | CPU/timing/allocation sampling hoặc VM event hooks |
| Package dependency solver | Resolver theo tên/path, chưa có solver | Version/range, conflict resolution, lockfile, cache/offline |
| Typed IR | Hoãn sau 1.0 | Dynamic typing đã chốt; Typed IR/static/gradual checker là tính năng hậu 1.0 |
| GC production | Tracing GC đã chạy mặc định, có cycle sweep + root graph | Tiếp tục đo allocation/latency cùng profiler trước tối ưu sâu |
| Object model | Hoàn tất contract hiện tại: object/field động, bound-call, `mình`/`gốc`, đơn kế thừa class, nhiều interface compile-time, constructor có tham số, method visibility semantic + runtime | Trait/generic, runtime interface introspection và field declaration nếu bổ sung sẽ cần contract mới |

## Rủi ro cần xử lý sớm

- [x] Xác nhận tính độc lập của `vpp-pipeline-legacy-parity`: corpus 94 chương trình
  chạy thuận và đảo thứ tự trong cùng process đều khớp snapshot; chạy riêng parity
  và bộ CTest không gồm integration đều qua.
- [x] Handler VM vẫn thao tác trên state của instance, nhưng `VMRuntimeFixture` đã tạo
  ranh giới test nội bộ để dựng/quan sát state và gọi handler độc lập. Việc tối ưu
  dispatch sâu hơn giờ có baseline handler-level để bảo vệ semantics.
- [x] Import resolution đã tách khỏi process cwd cho context-driven compilation; hai
  compilation có import cùng tên từ hai working directory khác nhau chạy song song và
  giữ đúng module riêng của từng context.

## Kiểm tra tại thời điểm cập nhật

```text
VM opcode smoke (build trực tiếp bằng C++17): passed
Integration regression: 78/78 passed
CTest baseline gần nhất: 16/16 passed
ASan+UBSan local CTest: 16/16 passed; LSan chờ Ubuntu CI `detect_leaks=1`
Pipeline parity baseline gần nhất: 94 chương trình, direct IR 94 chương trình
Coverage cross-check: 75.77% line coverage (8,884/11,725), gate 45%
Benchmark baseline: VM dispatch + lexer + compiler pipeline + native HTTP helpers
Short-term: 14/15
Medium-term: 18/18
Long-term: 15/40
```

## Ưu tiên tiếp theo

1. [x] Làm test parity chạy độc lập ổn định và thêm regression cho lifecycle compile nhiều lần.
2. [x] Mở rộng VM opcode matrix cho branch + call/return trước khi tách `VM::run()`.
3. [x] Hoàn tất bước đầu `CompilationContext`: top-level reset/snapshot/cleanup và
   migrate `runSnippet()`; tiếp tục dời registry nội bộ ở các bước sau.
4. [x] Đưa toàn bộ regression corpus hiện tại sang direct IR, khóa bằng parity gate và
   xóa token bridge khỏi production compiler/source set.
5. [x] Sau khi test architecture ổn định, thêm coverage + clang-tidy + benchmark baseline.
6. [x] Tách nốt variable/index/switch/block/exception khỏi `VM::run()` và mở rộng
   opcode matrix; full regression hiện tại đạt 78/78.
7. [x] Tạo internal VM state fixture/API và output sink để test handler trực tiếp
   không phụ thuộc stdout; khóa bằng `vpp-vm-handler-unit`.
8. [x] Dời `StringPool`, function registry, import set và class/access state vào
   `CompilationContext`; thêm concurrent-compilation regression và migrate toàn bộ CLI
   sang context snapshot.
9. [x] Tách import resolution khỏi process cwd bằng `CompilationContext.importResolutionBase`
   và khóa bằng regression song song cho hai module cùng tên ở hai thư mục độc lập.
10. [x] Tách ranh giới direct emitter thành `emitBlock`, `emitStatement` và
    `emitFunctionBody` với output bytecode tường minh; parity hiện tại 94/94 và full CTest vẫn xanh.
11. [x] Hoàn tất Module Semantics Phase 1: index local `.vi` exports, alias namespace,
    semantic `ImportedFunction`, lifecycle state contract và production top-level
    indexing; package/compat imports vẫn do resolver hiện hữu xử lý.
12. [x] Nối module lifecycle vào runtime `ModuleTable`, chạy initializer dependency-first
    đúng một lần và giữ failed state khi lỗi.
13. [x] Hoàn tất object model theo contract hiện tại: construction có tham số qua
    `hàm khởi tạo(...)`, property động read/write, bound-method dispatch, receiver
    `mình`/`gốc`, inheritance, interface/`triển khai` compile-time và method visibility đã
    chạy qua semantic/direct IR/VM.
14. [x] **Semantics freeze:** scope/shadowing, `rỗng`, equality/coercion, runtime error,
    override, dynamic type policy/call boundary, closure shared-cell capture, module
    export/re-export + import cycle và no-finalizer policy đều đã chốt bằng docs/regression.
15. [ ] **Runtime hardening:** call-stack stress đã khóa recursion sâu, nested exception và
    stack overflow bằng giới hạn 256 với `CallBoundary` có kiểm soát; GC stress đã khóa graph
    sâu/allocation burst/caller roots ở interval 1. VM-state invariant đã chứng minh cùng VM
    dùng tiếp được sau `VmFault` mà không mất caller stack/heap root/call-frame balance.
    Local ASan+UBSan full CTest hiện 16/16 sau khi sửa hai lỗi sanitizer; Ubuntu CI đã bật
    leak detection tường minh. Stack trace source span/function/method/module đã hoàn tất;
    còn CI Linux leak gate để đóng runtime hardening 0.8.
16. [ ] **Package 0.9:** chốt manifest/project layout; tách resolver; xây semver/range,
    dependency conflict diagnostic, deterministic lockfile, cache/offline + local/Git/registry source.
17. [ ] **Stdlib 1.0:** audit UTF-8, filesystem/path, time/date và các package hiện có;
    bổ sung process, crypto cơ bản và nâng test framework nếu release gate yêu cầu.
18. [x] **Compiler re-entrant:** production compiler không còn dựa vào active registry ẩn;
    state đi qua `CompilationContext`/`CompilationRegistryState` tường minh, kể cả recursive import.
19. [ ] **Compiler hardening còn lại:** fuzz lexer/parser, malformed AST/IR, bytecode verification,
    deterministic/reproducible compile và stress project lớn.
20. [ ] **Toolchain 1.0:** đóng public CLI, formatter/linter/LSP/VS Code integration,
    project templates, docs và installer/update path Windows/macOS/Linux.
21. [ ] **1.0 RC:** freeze syntax/semantics/bytecode/package/CLI; chạy fuzz + stress +
    sanitizer/leak + release-install smoke và sample project thực tế trên cả ba nền tảng.
