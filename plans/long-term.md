# Kế hoạch dài hạn (3–12+ tháng)

> Cập nhật: 12/08/2026
> Các mục dưới đây là công việc nền tảng chưa hoàn tất. V++ hiện có compiler, bytecode
> VM, package/thư viện chuẩn, tooling MVP và pipeline release; không nên diễn giải
> điều đó là mức hoàn thiện tương đương Java, C# hay Python.

## Nền tảng đã có

- [x] Workflow release tạo artifact cho Linux, macOS và Windows, bao gồm binary,
  gói/thư viện, template và ví dụ.
- [x] CONTRIBUTING.md đã có hướng dẫn đóng góp cơ bản.
- [x] Runtime có MVP cho GC/JIT và CI có regression/sanitizer nền tảng; các phần này
  chưa phải implementation production-grade có profiling đầy đủ.
- [x] Pipeline incremental đã có token mang span, AST cấu trúc ban đầu, semantic model
  cho khai báo/lời gọi trực tiếp và cầu nối IR không kiểu, lossless tới backend
  bytecode legacy.

## Việc lớn còn lại

1. Frontend và semantic pipeline

   - [ ] Chốt chiến lược dynamic, static hoặc gradual typing.
   - [ ] Mở rộng AST cấu trúc mang span hiện có thành AST biểu thức đầy đủ, cùng test
     parser và lỗi nguồn.
   - [ ] Mở rộng semantic model từ khai báo/lời gọi trực tiếp tới scope, name
     resolution, import, lớp và semantic diagnostics đầy đủ.
   - [ ] Sau khi có contract kiểu, thiết kế Typed IR trên cầu nối IR không kiểu,
     lossless hiện có rồi mới thay dần bytecode/codegen legacy.

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
  lifecycle dữ liệu.
- Mọi tối ưu runtime phải có benchmark, test lỗi và regression cross-platform.
- Roadmap cần được cập nhật cùng code để trạng thái checklist không bị nhầm với mức
  hoàn thiện của các platform trưởng thành.
