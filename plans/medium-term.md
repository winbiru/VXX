# Kế hoạch Trung hạn (1–3 tháng)

Mục tiêu: Tăng độ tin cậy và maintainability của compiler & VM, mở rộng test coverage, và thêm tooling (linters, coverage, benchmarks).

Ưu tiên: High → Medium

Estimated effort: 5–20 man-days

## Tasks & chi tiết

1) Tách `VM::run()` thành các opcode handler
   - Mục tiêu: chia nhỏ `src/vm/vm.cpp` thành các hàm nhỏ (vd. `handleOP_GOI`, `handleOP_JUMP`, `handleOP_PUSH`), dễ unit-test và profiling.
   - Files: `src/vm/vm.cpp`, `include/vm/vm.h` (cập nhật chữ ký nếu cần)
   - Effort: medium (3–7 days)

2) Viết unit tests cho opcode handlers
   - Mục tiêu: đảm bảo từng opcode hoạt động đúng, viết tests giả lập `VM` state.
   - Files: `tests/unit/test_vm_handlers.cpp`
   - Effort: medium (3–7 days)

3) Tăng cường CI: coverage + sanitizer + linter
   - Mục tiêu: thêm Codecov, AddressSanitizer/UndefinedBehaviorSanitizer trong CI, chạy clang-tidy hoặc cpplint.
   - Files: `.github/workflows/ci.yml`, thêm `ci/` scripts nếu cần.
   - Effort: medium (2–4 days)

4) Improve compiler modularity
   - Mục tiêu: giảm coupling giữa `compileRegistry`, `compileFunction`, và `compileStatement`; rõ ràng interfaces cho compile handlers.
   - Files: `src/compiler/*`, `include/compiler/*`
   - Effort: medium (3–7 days)

5) Add integration tests + harness
   - Mục tiêu: harness để chạy nhiều `.vi` tests headless and assert outputs; add regression tests for previously failing cases.
   - Files: `tests/integration/run_tests.py` or C++ runner + `tests/expected/*`
   - Effort: medium (2–5 days)

6) Add basic benchmarks & profiling harness
   - Mục tiêu: microbench opcode dispatch and hot functions, collect baseline for optimizations.
   - Files: `benchmarks/` (Google Benchmark or simple harness)
   - Effort: medium (2–5 days)

## Checklist PR
- Unit tests for modified modules
- CI updated to run tests and report coverage
- Documentation for any changed bytecode behavior

## Rủi ro & dependency
- Large refactors of VM/internal state may introduce regressions; keep integration tests to detect.
- Sanitizers may reveal undefined behavior that requires careful fixes.

