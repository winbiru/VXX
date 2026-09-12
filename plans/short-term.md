# Kế hoạch ngắn hạn (1–2 tuần)

> Cập nhật: 12/09/2026
> Mục tiêu là củng cố baseline build/test và tài liệu. Các mục đánh dấu hoàn tất chỉ
> xác nhận source hoặc workflow đã có trong repo, không thay cho kết quả CI của một
> commit cụ thể.

## Nền tảng đã có

- [x] Build artefact phổ biến đã được ignore qua .gitignore; CMake build ở thư mục
  ngoài source.
- [x] README.md, README-updates.md và CONTRIBUTING.md đã tồn tại.
- [x] CI GitHub Actions đã có cho nhánh developer: Ubuntu chạy full regression và
  sanitizer, Windows chạy full regression Release.
- [x] Release workflow đã có riêng; matrix đóng gói Ubuntu, macOS và Windows nằm ở
  .github/workflows/release-binaries.yml.
- [x] CTest đã có ba baseline C++ unit target trong test/: compiler support
  (StringPool, symbolTable, hamMap), canonical opcode/native constants và VM opcode smoke.
- [x] Hồi quy end-to-end đã có run_tests.sh trên Unix, runner PowerShell trên Windows,
  cùng các file expected trong src/tests/expected/.

## Việc còn lại

1. Đồng bộ tài liệu bytecode

   - [x] Đối chiếu docs/bytecode.md với Instruction/Opcode và src/bytecode/opcode.cpp
     đang thực thi.
   - [x] Ghi rõ phần nào là proposal format tuần tự hoá, phần nào là contract runtime
     hiện tại.

2. Củng cố test baseline

   - [x] Mở rộng compiler support test cho đường lỗi và lifecycle khi compile nhiều
     chương trình. Đã có regression A → B → A và context-driven compile tự reset,
     snapshot, cleanup giữa các top-level compilation; chưa tuyên bố concurrent re-entrant.
   - [x] Thêm case cho opcode còn lại theo từng nhóm (stack, jump, gọi hàm, native
     adapter). Smoke test hiện khóa số học, so sánh, chuỗi, list/index và lỗi biên,
     branch, call/return, native collection/text adapter, literal boolean/null, unary
     boolean và stack assignment/increment/decrement.
   - [x] Giữ mọi regression V++ có expected output và chạy được từ CTest trên nền tảng
     phù hợp. Unix/Windows runner dùng danh sách regression tường minh; fixture HTTP
     và bài manual được tách khỏi expected-output suite.

3. Làm rõ bug và hygiene

   - [x] Rà soát contract OP_GOI/call frame bằng test bytecode trực tiếp cho
     `OP_GOI` + `OP_PARAM` + `OP_TRA_VE`, khóa đường truyền tham số/giá trị trả về
     trước khi tách `VM::run()`.
   - [x] Tài liệu hoá điểm reset/lifecycle của StringPool và các map compiler còn có
     state chung trong `docs/architecture.md` và contract `CompilationContext`.
   - [x] Không thêm source mới vào một target helpers tổng quát; CMake khai báo owner
     theo `VPP_*_SOURCES` và target module cụ thể.

4. Kiểm tra CI thực tế

   - [ ] Khi thay đổi runner hoặc native adapter, xác nhận cả job Ubuntu và Windows
     trên GitHub Actions; macOS hiện được build trong workflow release, chưa phải job
     regression thường trực. Local baseline ngày 12/09/2026 đã qua 14/14 CTest
     (integration 54/54, parity 61/61); cần commit/push bộ thay đổi hiện tại để có
     bằng chứng Actions tương ứng.

## Tiêu chí cho mỗi PR

- Build và CTest phù hợp với nền tảng thay đổi.
- Có test hoặc expected output cho hành vi mới/sửa lỗi.
- Cập nhật docs khi grammar, bytecode, package hoặc CLI thay đổi.
- Không mở rộng global state mà không có reset contract và test tương ứng.

## Rủi ro

- Các fixture HTTP dùng port cục bộ nên runner phải dọn process và thư mục tạm đáng
  tin cậy trên cả Unix lẫn Windows.
- Unit test không được che giấu lỗi tích hợp: bộ src/tests/*.vi vẫn là regression
  contract của CLI và package/thư viện chuẩn.
