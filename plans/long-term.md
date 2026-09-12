# Kế hoạch dài hạn (3–12+ tháng)

> Cập nhật: 12/09/2026
> Các mục dưới đây là công việc nền tảng chưa hoàn tất. V++ hiện có compiler, bytecode
> VM, package/thư viện chuẩn, tooling MVP và pipeline release; không nên diễn giải
> điều đó là mức hoàn thiện tương đương Java, C# hay Python.

## Nền tảng đã có

- [x] Workflow release tạo artifact cho Linux, macOS và Windows, bao gồm binary,
  gói/thư viện, template và ví dụ.
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
      import local/package đã có structured `AstImportSpec`/IR payload và module graph;
      export semantics vẫn còn là phần tiếp theo.
   5. [x] **IR → bytecode trực tiếp, cohort đầu** — emitter đã phát trực tiếp
      literal/name/operator/assignment/postfix/print/primitive map, top-level function,
      primitive default, return, direct/dynamic/indirect calls, structured lambda,
      if/else/for-loop/switch/try, continue, break, throw và class namespace/method.
      Emitter tự sở hữu function ID, VM slot và jump
      fixup; shared mixed-backend context còn thiếu.
   6. [x] **Bỏ token bridge** — production compiler chỉ còn Direct IR → bytecode;
      vùng chưa được direct emitter hỗ trợ bị từ chối tường minh. Corpus `.vi`
      đạt 61/61 direct IR và vẫn giữ cùng bytecode/compiler state.

   Hiện direct backend bao phủ 61/61 regression program; parity gate yêu cầu toàn bộ
   corpus phải giữ direct IR và vẫn đối chiếu đầy đủ bytecode/StringPool/function
   registries. Call argument, function/lambda parameter và loop header đã dùng chung
   splitter top-level quote-aware; string chứa delimiter không còn tự tạo fallback.
   Helper loop cũ và implementation `compileFunction` không còn caller đã được xóa.
   `materializeIrTokens()` chỉ còn phục vụ lossless IR test/debug; nó không còn nằm
   trên production compile path. Các cú pháp ngoài direct cohort được diagnostic.

   Chốt dynamic/static/gradual typing và Typed IR là quyết định riêng. Expression
   AST, scope tree, name resolution và structured **untyped IR** không phải chờ
   quyết định kiểu này.

2. Chuẩn hoá bytecode

   - [ ] Định nghĩa format, versioning, validation và compatibility policy sau khi
     opcode ổn định.
   - [ ] Viết assembler/disassembler và round-trip test; tránh coi tài liệu proposal
     là format runtime đã phát hành.

3. Runtime và object model

   - [ ] Thiết kế value/object/instance, inheritance hoặc trait/generic theo quyết
     định ngôn ngữ; class namespace/visibility hiện có không thay thế object model.
   - [ ] Nâng MVP GC thành quản lý object graph an toàn nếu object/closure cần heap
     lâu dài.
   - [ ] Chỉ tối ưu JIT/dispatch sau benchmark; cần fallback interpreter và regression
     cross-platform.
   - [ ] Thiết kế concurrency/async và native/FFI với ownership/cancellation rõ ràng.

4. API embedding

   - [ ] Thiết kế C API/C++ API cho lifecycle VM, load/chạy program và callback I/O.
   - [ ] Loại dependency vào global compiler state trước khi công bố API ổn định.
   - [ ] Thêm ABI/versioning, sample host và test API độc lập.

5. Phát hành và cộng đồng

   - [ ] Duy trì workflow release hiện có, thêm smoke test artifact cài từ package nếu
     cần.
   - [ ] Đánh giá Homebrew, Debian/MSI hoặc package manager khác sau khi install
     contract ổn định; chúng chưa có trong repo.
   - [ ] Bổ sung CODE_OF_CONDUCT, issue/PR template, changelog/release-note process
     và maintainer guide. CONTRIBUTING.md đã hoàn thành phần đầu tiên.

## Điều kiện thực hiện

- Không bắt đầu Typed IR hay public embedding API trước khi quyết định type policy và
  lifecycle dữ liệu; điều kiện này không chặn sáu bước migration untyped ở trên.
- Mọi tối ưu runtime phải có benchmark, test lỗi và regression cross-platform.
- Roadmap cần được cập nhật cùng code để trạng thái checklist không bị nhầm với mức
  hoàn thiện của các platform trưởng thành.
