# Kế hoạch Ngắn hạn (1–2 tuần)

Mục tiêu: ổn định build, bổ sung test tối thiểu, sửa một số bug rõ ràng và cải thiện tài liệu để contributor mới có thể chạy dự án dễ dàng.

Ưu tiên: High

Estimated effort: 1–5 man-days

## Tasks & chi tiết

1) Làm sạch repository
   - Mục tiêu: loại bỏ/ignore build artefacts (`cmake-build-debug/`) khỏi VCS.
   - Files/đường dẫn: `.gitignore`, xóa `cmake-build-debug/` nếu đã bị commit.
   - Effort: small (0.5 day)

2) Update README chính và thêm `README-updates.md`
   - Mục tiêu: đảm bảo hướng dẫn build/run rõ ràng. (đã thêm `README-updates.md`)
   - Files: `README.md`, `README-updates.md`
   - Effort: small (0.5 day)

3) Thiết lập CI tối thiểu (GitHub Actions)
   - Mục tiêu: build project trên Ubuntu/macOS, chạy một tập test đơn giản.
   - Files: `.github/workflows/ci.yml`
   - Effort: small (1 day)
   - Ghi chú: nếu repo dùng GitLab, tạo `gitlab-ci.yml` tương đương.

4) Tạo skeleton unit tests
   - Mục tiêu: thêm thư mục `tests/unit/` với vài unit test cho `StringPool`, `symbolTable`, và một bài test chạy `vm` trên 1-2 file `.vi`.
   - Files: `tests/unit/test_stringpool.cpp`, `tests/unit/CMakeLists.txt`
   - Effort: small → medium (1–2 days)

5) Fix bugs nhỏ & code hygiene
   - Ví dụ: rà soát `OP_GOI` semantics trong `src/vm/vm.cpp` và `compileFunction.cpp` để đảm bảo function lookup nhất quán.
   - Files: `src/vm/vm.cpp`, `src/compiler/compileFunction.cpp`, `src/compiler/compileRegistry.cpp`
   - Effort: small (1 day)

6) Document bytecode (quick update)
   - Mục tiêu: đồng bộ `docs/bytecode.md` với `include/vm/instruction.h` và thực thi hiện tại.
   - Files: `docs/bytecode.md`, `include/vm/instruction.h`
   - Effort: small (0.5–1 day)

## Checklist PR (mỗi PR)
- Build passes on CI (matrix entries included)
- Unit tests added / updated, with instructions
- Docs updated if behavior changed
- No new global state leak; `StringPool` lifecycle documented

## Tests cần viết
- Unit tests cho `StringPool` (`src/helpers/storeString.cpp`): add/get/clear behavior
- Unit tests cho `symbolTable` (`src/helpers/symbolTable.cpp`)
- Integration test: compile a small `.vi` sample and run with `vietvm-cli`, assert expected output (stdout)

## Rủi ro & dependency
- Nếu project sử dụng globals và singletons (vd. `StringPool`), tests cần reset state between cases.
- CMake setup may need tweaking to include test targets.

---

Sau khi hoàn thành các bước ngắn hạn, repository sẽ có CI cơ bản, tests skeleton và tài liệu tốt hơn cho contributor mới.

