# Mã thông báo chẩn đoán

Các lỗi do V++ phát ra theo định dạng sau:

```text
[VPP-NHOM-SO] Nội dung tiếng Việt có thể đọc được
```

Mã là định danh ổn định cho automation, IDE và LSP; nội dung có thể được cải
thiện mà không làm mất khả năng nhận diện lỗi. Catalog nguồn duy nhất nằm tại
[`src/include/vpp/core/message_constants.h`](../src/include/vpp/core/message_constants.h)
và các catalog con đi kèm.

| Nhóm mã | Phạm vi |
| --- | --- |
| `VPP-CLI-*` | Lệnh dòng lệnh |
| `VPP-REPL-*` | REPL |
| `VPP-PKG-*` | Package/scaffold |
| `VPP-TOOL-*` | Lint/tooling |
| `VPP-LEX-*` | Lexer |
| `VPP-CMP-*`, `VPP-SYN-*`, `VPP-SEM-*`, `VPP-IMP-*`, `VPP-INT-*` | Compiler, cú pháp, ngữ nghĩa, import và invariant nội bộ |
| `VPP-VM-*` | VM/bytecode |
| `VPP-NATIVE-*` | Hàm native: file, HTTP, thời gian và DB |

Thông báo thông tin trên `stdout` (ví dụ `[IN]`, kết quả package và JSON-RPC
LSP framing) vẫn giữ nguyên nội dung cũ để không phá script hiện có. Chúng vẫn
được định nghĩa tập trung trong catalog và có mã nội bộ tương ứng. Với lỗi LSP,
trường JSON `code` dùng mã `VPP-*` khi lỗi đã có mã.

Khi thêm thông báo mới, hãy thêm `MessageDefinition { code, text }` vào đúng
catalog con, dùng `formatMessage(...)` cho lỗi hiển thị cho người dùng và
`messageText(...)` khi phải giữ chính xác giao thức/đầu ra cũ.
