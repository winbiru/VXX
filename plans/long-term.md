# Kế hoạch dài hạn (3–12+ tháng)

> Cập nhật: 14/09/2026
> Các mục dưới đây là công việc nền tảng chưa hoàn tất. V++ hiện có compiler, bytecode
> VM, package/gói chuẩn, tooling MVP và pipeline release; không nên diễn giải
> điều đó là mức hoàn thiện tương đương Java, C# hay Python.

## Đích phát hành 1.0

Roadmap phát hành chi tiết nằm tại `plans/roadmap-1.0.md`. Từ trạng thái hiện tại,
ưu tiên 1.0 chuyển từ "thêm cú pháp" sang sáu nhóm: đóng semantics, runtime/VM hardening,
module + package system, stdlib, compiler hardening và toolchain đa nền tảng.

JIT production-grade, LLVM backend, generic, reflection đầy đủ, GUI, FFI đầy đủ và
concurrency/async phức tạp không phải release blocker mặc định của 1.0. Chúng chỉ được
đưa vào nếu cần để hoàn tất một contract nền tảng đã chốt.

## Nền tảng đã có

- [x] Workflow release tạo artifact cho Linux, macOS và Windows, bao gồm binary,
  gói/chuẩn, template và ví dụ.
- [x] CONTRIBUTING.md đã có hướng dẫn đóng góp cơ bản.
- [x] Runtime đã có tracing GC quản lý object graph + cycle sweep; JIT vẫn ở mức MVP.
  CI có regression/sanitizer nền tảng, còn profiler và stress/leak gate production vẫn
  nằm trong roadmap 1.0.
- [x] Pipeline đã có expression AST mang span, scope tree, ExprId-based resolution,
  recursive untyped IR, optimizer và direct bytecode backend; production token bridge đã bị xóa.
- [x] Parity gate compile toàn bộ `src/tests/**/*.vi` qua pipeline rồi đối chiếu
  fingerprint bytecode, StringPool và function registries với baseline legacy
  đóng băng dưới `test/data/`.

## Việc lớn còn lại

1. Frontend và semantic pipeline

   Thứ tự migration bắt buộc, mỗi bước phải giữ regression bytecode/VM của toàn bộ
   corpus `.vi` trước khi chuyển bước tiếp theo:

   1. [x] **Expression AST** — parse literal, name, unary/binary, assignment,
      call, lambda và map theo precedence hiện hành; vẫn giữ source span/token
      range làm fallback tạm thời.
   2. [x] **Scope tree** — tạo global/class/function/block/lambda/catch scope,
      parent/child link và khai báo parameter/local/import alias.
   3. [x] **Real name resolution** — bind expression node tới symbol lexical,
      phân biệt direct call, indirect value call và dynamic/native fallback.
   4. [x] **Recursive IR lowering** — hạ expression arena, parameter defaults và
      statement children; conditional/loop/switch/try/class đã có structured payload;
      import local/package đã có structured `AstImportSpec`/IR payload và module graph.
   5. [x] **IR → bytecode trực tiếp, cohort đầu** — emitter đã phát trực tiếp
      literal/name/operator/assignment/postfix/print/primitive map, top-level function,
      primitive default, return, direct/dynamic/indirect calls, structured lambda,
      if/else/for-loop/switch/try, continue, break, throw và class namespace/method.
      Emitter tự sở hữu function ID, VM slot và jump
      fixup; shared mixed-backend context còn thiếu.
   6. [x] **Bỏ token bridge** — production compiler chỉ còn Direct IR → bytecode;
      vùng chưa được direct emitter hỗ trợ bị từ chối tường minh. Corpus `.vi`
      đạt 94/94 direct IR và vẫn giữ snapshot compiler state đã chốt.

   Hiện direct backend bao phủ 94/94 regression program; parity gate yêu cầu toàn bộ
   corpus phải giữ direct IR và vẫn đối chiếu đầy đủ bytecode/StringPool/function
   registries. Call argument, function/lambda parameter và loop header đã dùng chung
   splitter top-level quote-aware; string chứa delimiter không còn tự tạo fallback.
   Helper loop cũ và implementation `compileFunction` không còn caller đã được xóa.
   `materializeIrTokens()` chỉ còn phục vụ lossless IR test/debug; nó không còn nằm
   trên production compile path. Các cú pháp ngoài direct cohort được diagnostic.

   Type policy đã được chốt bằng ADR 0001: V++ 1.0 dùng dynamic typing. Expression AST,
   scope tree, name resolution và structured **untyped IR** tiếp tục là production contract;
   Typed IR/static checker được hoãn sau 1.0.

   - [x] **Type policy 1.0** — dynamic typing; value/call boundary và arity đã có contract,
     semantic chỉ kiểm tra phần biết chắc còn runtime kiểm tra loại giá trị động. Typed IR /
     static hoặc gradual checker được hoãn hậu 1.0 và không đổi contract dynamic hiện tại.

2. Module semantics

   - [x] **Phase 1: identity + export/import namespace + lifecycle contract** — local
     `.vi` module có identity ổn định, export index cho top-level function/class
     public hoặc không ghi visibility, alias import được đưa vào `SemanticEnvironment`,
     call tới imported function có semantic kind riêng nhưng vẫn dùng runtime-name
     linking để giữ bytecode tương thích. `ModuleInitializationTracker` khóa trạng thái
     `uninitialized → initializing → initialized/failed` và chặn begin lặp/cycle.
   - [x] Nối lifecycle vào runtime `ModuleTable`: compiler giữ initializer metadata theo
     module identity, CLI chuyển metadata sang VM, và runtime chạy initializer theo thứ
     tự dependency-first đúng một lần với state `uninitialized → initializing →
     initialized/failed`.
   - [x] Chốt module semantics 1.0: `công khai nhập` tạo re-export có alias, private/protected
     không lọt vào export surface, truy cập symbol ẩn có semantic diagnostic; local import cycle
     bị từ chối với chuỗi path/identity rõ ràng và recursive relative import resolve từ module cha.

3. Runtime, object model và debugger

   - [x] Hoàn tất value/object/instance semantics ở mức ngôn ngữ. Runtime substrate đã
     có `RuntimeClass`/`RuntimeInstance`, field storage, method table, inheritance lookup
     và identity equality. Lát cắt đầu tiên đã chạy end-to-end qua semantic → untyped IR
     → bytecode → VM: `obj = Class()`, `obj.field = value`, `obj.field` và
     `obj.method(args)`. Implicit receiver đã chốt dùng `mình` cho current instance và `gốc`
     cho superclass dispatch; bound call mang cả receiver và defining-class qua call frame nên
     tham số nguồn không đổi ABI. `gốc.method(...)` đã có runtime test khóa việc bỏ qua override
     của lớp hiện tại. Inheritance syntax `lớp Con kế thừa Cha { ... }` đã đi qua parser → semantic
     → IR → bytecode → VM, gồm forward superclass emission và kiểm tra superclass/cycle.
     Interface dùng cú pháp `giao diện Tên { hàm tácVụ(...); }`; giao diện có thể kế thừa
     nhiều giao diện qua `giao diện B kế thừa A, C`, còn lớp giữ đúng một superclass nhưng có thể
     `triển khai` nhiều giao diện, ví dụ `lớp Con kế thừa Cha triển khai A, B`. Semantic kiểm tra
     target, duplicate/cycle, tên + số tham số và visibility công khai; method kế thừa từ
     superclass cũng có thể thỏa hợp đồng. Interface hiện là contract compile-time nên không
     tạo runtime class/vtable riêng và không thay đổi dynamic method dispatch.
     Constructor có tham số đã chạy end-to-end bằng `hàm khởi tạo(...)`: lời gọi
     `Class(args...)` chuyển đối số qua opcode tạo instance, bind receiver `mình`, hỗ trợ
     default parameter và giữ instance làm kết quả biểu thức. Constructor lớp cha không tự
     chạy; lớp con có thể gọi tường minh `gốc.khởi tạo(...)`. Visibility của method qua
     instance hiện được kiểm tra hai tầng: semantic suy luận lớp từ `Class(...)`/alias và
     hierarchy để chặn private/protected sớm, còn runtime mang visibility trong method table
     để chặn cả receiver động đi qua parameter. Private chỉ gọi được trong chính lớp sở hữu;
     protected gọi được trong lớp sở hữu và subclass. Field hiện là thuộc tính động tạo qua
     phép gán (`mình.x = ...`), không có khai báo field nên chưa có modifier visibility riêng;
     theo contract hiện tại field động là public/dynamic. Trait/generic và runtime interface
     introspection tiếp tục phụ thuộc quyết định ngôn ngữ/metadata.
   - [ ] **Reflection/metadata** — chốt rõ V++ có chủ đích không hỗ trợ reflection
     tổng quát hay cung cấp introspection giới hạn. Nếu hỗ trợ, metadata class/method/
     field/module phải có contract ổn định và không phá visibility/sandbox.
   - [x] Nâng MVP GC thành tracing GC quản lý object graph an toàn. Runtime có
     `RuntimeHeap` riêng theo VM, registry weak cho map/list/tuple/class/instance/closure và
     mark từ stack, global/local variable, call frame receiver/args/locals, class table
     cùng switch value. Function/module child VM chia sẻ heap và mang snapshot root của
     caller để collection lồng nhau không quét nhầm object còn sống. Sweep cắt cạnh của
     object không reachable nên thu được cả shared_ptr cycle; closure capture dùng shared cell
     theo tham chiếu, giữ lifetime qua outer return và GC cắt được closure↔cell cycle. Regression
     khóa instance↔list, self-cycle list/map, argument caller và nested closure mutation.
   - [x] **Runtime/GC stress trước 1.0** — khóa object graph lớn, allocation liên tục,
     cycle sâu, caller roots qua child VM, recursion sâu, nested exception và stack overflow;
     runtime error không được làm corrupt stack/call frame/module/heap state cho lần chạy sau.
     Phần call stack đã có regression recursion sâu + exception xuyên child VM và giới hạn
     256 tầng trả `CallBoundary` có kiểm soát. GC stress đã có graph 4.096 node, burst 1.024
     cycle và `.vi` interval 1. Handler invariant khóa stack/call frame/heap root sau `VmFault`,
     cùng VM gọi tiếp được function hợp lệ; module failed-state vẫn giữ contract riêng.
   - [ ] **Leak/sanitizer gate** — chạy stress suite qua ASan/UBSan và leak checker phù hợp;
     mọi leak/use-after-free/crash tái hiện được phải được xử lý trước 1.0 RC. Local
     AppleClang ASan+UBSan đã chạy full CTest 16/16; quá trình này phát hiện và đã sửa
     invalid-enum UB cùng destructor-chain stack overflow ở GC. Ubuntu CI hiện ép
     `ASAN_OPTIONS=detect_leaks=1`; leak gate chờ CI Linux xác nhận.
   - [ ] Stack trace có source span/function/method/module identity đã hoàn tất bằng debug
     metadata song song bytecode, structured `RuntimeError` và CLI formatter có nén frame
     đệ quy. Phần còn lại của đầu việc là debugger hook cho breakpoint, step và
     frame/variable inspection trước khi làm debugger UI đầy đủ.
   - [ ] **Profiler production** — bổ sung CPU/timing/allocation sampling hoặc event
     hooks có thể gắn vào VM/compiler; benchmark hiện tại chỉ là baseline đo lặp lại,
     chưa phải profiler cho chương trình V++.
   - [ ] Chỉ tối ưu JIT/dispatch sau benchmark; cần fallback interpreter và regression
     cross-platform.
   - [ ] **Thread/concurrency/async** — chốt memory model, scheduler/thread API,
     synchronization, cancellation và cách exception/module/GC tương tác khi chạy
     đồng thời trước khi thêm `async/await`, channel hay thread public API.
   - [ ] **FFI** — thiết kế ABI/calling convention, marshal value/string/object,
     ownership/lifetime, error propagation và allowlist native symbol trước khi cho
     gọi C/C++ hoặc thư viện hệ thống từ V++.

4. Package resolver

   - [x] Tách package/bare-module lookup khỏi `compileRegistry`: project package,
     bundled standard packages, compatibility redirect và `VPP_HOME` đi qua `PackageResolver`
     độc lập; unit test khóa precedence, alias, UTF-8 và installation-home fallback.
   - [ ] **Dependency solver** — manifest schema 1, source kind `path/git/registry`, SemVer/range,
     version conflict check khi khóa và deterministic `vpp.lock` đã có. Còn graph transitive,
     restore/install từ lockfile, registry/Git fetch và offline/cache behavior.

5. An toàn và bảo mật runtime

   - [ ] **Crypto** — cung cấp package chuẩn tối thiểu cho secure random, hash/HMAC
     và primitive mã hóa qua implementation đã được kiểm chứng; không tự viết thuật
     toán mật mã trong VM.
   - [ ] **Sandbox/permission** — thiết kế capability/permission cho file, network,
     process/environment và FFI; policy phải áp dụng ở native runtime boundary chứ
     không chỉ kiểm tra cú pháp ở compiler.

6. Chuẩn hoá bytecode

   - [ ] Định nghĩa format, versioning, validation và compatibility policy sau khi
     opcode ổn định.
   - [ ] Viết assembler/disassembler và round-trip test; tránh coi tài liệu proposal
     là format runtime đã phát hành.

7. API embedding

   - [ ] Thiết kế C API/C++ API cho lifecycle VM, load/chạy program và callback I/O.
   - [x] Loại dependency production compiler vào global/active registry trước khi công bố
     API ổn định; context-driven compile hiện truyền state tường minh xuyên codegen/import.
   - [ ] Thêm ABI/versioning, sample host và test API độc lập.

8. Phát hành và cộng đồng

   - [ ] Duy trì workflow release hiện có, thêm smoke test artifact cài từ package nếu
     cần.
   - [ ] Đánh giá Homebrew, Debian/MSI hoặc package manager khác sau khi install
     contract ổn định; chúng chưa có trong repo.
   - [ ] Bổ sung CODE_OF_CONDUCT, issue/PR template, changelog/release-note process
     và maintainer guide. CONTRIBUTING.md đã hoàn thành phần đầu tiên.

9. Standard library 1.0

   - [ ] Audit UTF-8 cho text/string API và malformed input; tiếng Việt phải giữ semantics
     ổn định qua lexer, runtime text và package chuẩn.
   - [ ] Hoàn thiện filesystem/path và time/date contract đa nền tảng.
   - [ ] Bổ sung process API tối thiểu nếu permission model cho phép.
   - [ ] Nâng testing package và audit collection/file/JSON/HTTP/math/random/env/log/config
     bằng regression đa nền tảng trước release candidate.

10. Compiler hardening và release candidate

   - [x] Loại mutable global/facade khỏi production compiler: direct codegen/import/callable
     lookup nhận registry tường minh; CLI/tooling/`compileSource` dùng `CompilationContext`.
     Compatibility facade thread-local chỉ còn cho test/caller cũ và regression xác nhận
     context-driven compile không đọc/ghi một active registry khác.
   - [ ] Fuzz lexer/parser, malformed AST/IR test, bytecode verification và deterministic
     compilation/reproducible bytecode.
   - [ ] Stress source/project lớn và theo dõi compiler memory/time regression.
   - [ ] Freeze syntax/semantics/bytecode/package/public CLI rồi chạy release-install smoke
     bằng artifact thật trên Windows, macOS và Linux cùng ít nhất một sample project thực tế.

## Điều kiện thực hiện

- Không bắt đầu Typed IR trước 1.0; public embedding API phải tuân theo dynamic type policy và
  lifecycle dữ liệu; điều kiện này không chặn sáu bước migration untyped ở trên.
- Không mở FFI/crypto/network quyền cao trước khi có sandbox/permission model tối thiểu;
  native boundary phải là nơi enforce policy cuối cùng.
- Mọi tối ưu runtime phải có benchmark, test lỗi và regression cross-platform.
- Roadmap cần được cập nhật cùng code để trạng thái checklist không bị nhầm với mức
  hoàn thiện của các platform trưởng thành.
