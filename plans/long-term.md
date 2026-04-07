# Kế hoạch Dài hạn (3–12+ tháng)

Mục tiêu: Chuẩn hoá bytecode, nâng cao hiệu năng runtime, xác định API công khai để nhúng VietVM vào ứng dụng khác và có quy trình phát hành (releases).

Ưu tiên: Medium → Low (tùy roadmap)

Estimated effort: 20+ man-days

## Tasks & chi tiết

1) Chuẩn hoá & version hoá bytecode
   - Mục tiêu: định nghĩa rõ `bytecode.md` (binary layout, Instruction fields, versioning), viết disassembler/assembler.
   - Files: `docs/bytecode.md`, `tools/assembler.cpp`, `tools/disassembler.cpp`
   - Effort: large (5–10 days)

2) Thiết kế API nhúng (C API / C++ API)
   - Mục tiêu: expose an embeddable interface for creating VM instances, loading bytecode, and running with callbacks for I/O.
   - Files: `include/vietvm.h`, `src/vietvm_api.cpp`
   - Effort: large (5–10 days)

3) Tối ưu hoá VM (JIT/bytecode optimizations)
   - Mục tiêu: profile-hotspots, implement optimizations (immediate operands, threaded code, inline caches), cân nhắc JIT nếu cần.
   - Files: `src/vm/*`, `benchmarks/*`
   - Effort: large (10–40 days)

4) Release process & packaging
   - Mục tiêu: tạo release artifacts (pre-built binaries), package manager support (Homebrew formula / Debian package), và tạo changelog templates.
   - Files: `.github/workflows/release.yml`, `packaging/` scripts
   - Effort: medium (5–10 days)

5) Community & contributors
   - Mục tiêu: thêm CONTRIBUTING.md, CODE_OF_CONDUCT.md, issue/PR templates, maintainers guide.
   - Files: `CONTRIBUTING.md`, `.github/ISSUE_TEMPLATE`, `.github/PULL_REQUEST_TEMPLATE.md`
   - Effort: small (2–4 days)

## Rủi ro & dependency
- JIT / big optimizations đa phần phức tạp và dễ gây regressions; cần benchmark và nhiều tests.
- API nhúng yêu cầu lock-down các dữ liệu global; cân nhắc làm không có global state.

