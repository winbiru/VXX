# Kế hoạch dài hạn (3–12+ tháng)

> Cập nhật: 14/09/2026
> Các mục dưới đây là công việc nền tảng chưa hoàn tất. V++ hiện có compiler, bytecode
> VM, package/gói chuẩn, tooling MVP và pipeline release; không nên diễn giải
> điều đó là mức hoàn thiện tương đương Java, C# hay Python.

## Nền tảng đã có

- [x] Workflow release tạo artifact cho Linux, macOS và Windows, bao gồm binary,
  gói/chuẩn, template và ví dụ.
- [x] CONTRIBUTING.md đã có hướng dẫn đóng góp cơ bản.
- [x] Runtime có MVP cho GC/JIT và CI có regression/sanitizer nền tảng; các phần này
  chưa phải implementation production-grade có profiling đầy đủ.
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
      đạt 78/78 direct IR và vẫn giữ snapshot compiler state đã chốt.

   Hiện direct backend bao phủ 78/78 regression program; parity gate yêu cầu toàn bộ
   corpus phải giữ direct IR và vẫn đối chiếu đầy đủ bytecode/StringPool/function
   registries. Call argument, function/lambda parameter và loop header đã dùng chung
   splitter top-level quote-aware; string chứa delimiter không còn tự tạo fallback.
   Helper loop cũ và implementation `compileFunction` không còn caller đã được xóa.
   `materializeIrTokens()` chỉ còn phục vụ lossless IR test/debug; nó không còn nằm
   trên production compile path. Các cú pháp ngoài direct cohort được diagnostic.

   Chốt dynamic/static/gradual typing và Typed IR là quyết định riêng. Expression
   AST, scope tree, name resolution và structured **untyped IR** không phải chờ
   quyết định kiểu này.

   - [ ] **Type policy + Typed IR** — chốt ADR cho dynamic/static/gradual typing,
     kiểu của symbol/expression/call boundary và chiến lược diagnostic trước khi
     đưa Typed IR hoặc static checker vào production pipeline.

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
   - [ ] Chốt cycle behavior giàu diagnostic hơn, explicit export/re-export và diagnostic
     khi truy cập symbol không export.

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
     `RuntimeHeap` riêng theo VM, registry weak cho map/list/tuple/class/instance và
     mark từ stack, global/local variable, call frame receiver/args/locals, class table
     cùng switch value. Function/module child VM chia sẻ heap và mang snapshot root của
     caller để collection lồng nhau không quét nhầm object còn sống. Sweep cắt cạnh của
     object không reachable nên thu được cả shared_ptr cycle; regression khóa
     instance↔list, self-cycle list/map và argument đang nằm trên caller stack.
   - [ ] Xây stack trace có source span/function/module identity và debugger hook cho
     breakpoint, step, frame/variable inspection trước khi làm debugger UI đầy đủ.
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

   - [ ] Tách package/bare-module lookup khỏi `compileRegistry`: project package,
     bundled standard packages, compatibility redirect và `VPP_HOME` phải đi qua resolver có
     identity/dependency policy rõ ràng trước khi thêm version/lockfile.
   - [ ] **Dependency solver** — định nghĩa manifest dependency, semantic version/range,
     source registry/path, conflict resolution, deterministic lockfile và offline/cache
     behavior; resolver hiện tại mới dừng ở tìm package/module theo tên/path.

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
   - [ ] Loại dependency vào global compiler state trước khi công bố API ổn định.
   - [ ] Thêm ABI/versioning, sample host và test API độc lập.

8. Phát hành và cộng đồng

   - [ ] Duy trì workflow release hiện có, thêm smoke test artifact cài từ package nếu
     cần.
   - [ ] Đánh giá Homebrew, Debian/MSI hoặc package manager khác sau khi install
     contract ổn định; chúng chưa có trong repo.
   - [ ] Bổ sung CODE_OF_CONDUCT, issue/PR template, changelog/release-note process
     và maintainer guide. CONTRIBUTING.md đã hoàn thành phần đầu tiên.

## Điều kiện thực hiện

- Không bắt đầu Typed IR hay public embedding API trước khi quyết định type policy và
  lifecycle dữ liệu; điều kiện này không chặn sáu bước migration untyped ở trên.
- Không mở FFI/crypto/network quyền cao trước khi có sandbox/permission model tối thiểu;
  native boundary phải là nơi enforce policy cuối cùng.
- Mọi tối ưu runtime phải có benchmark, test lỗi và regression cross-platform.
- Roadmap cần được cập nhật cùng code để trạng thái checklist không bị nhầm với mức
  hoàn thiện của các platform trưởng thành.
