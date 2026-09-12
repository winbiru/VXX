# Tiến độ phát triển V++

> Cập nhật: 12/09/2026
>
> Tỷ lệ dưới đây được tính theo số checkbox trong roadmap, chỉ dùng để theo dõi
> tiến độ đầu việc; không đại diện cho phần trăm khối lượng kỹ thuật thực tế.

## Tổng quan

| Giai đoạn | Hoàn tất | Còn lại | Tỷ lệ |
| --- | ---: | ---: | ---: |
| Ngắn hạn | 14/15 | 1 | 93% |
| Trung hạn | 10/18 | 8 | 56% |
| Dài hạn | 10/23 | 13 | 43% |
| **Tổng** | **34/56** | **22** | **61%** |

## Đã xác nhận hoàn thành

- [x] CMake/CTest đã tách các target core, frontend, bytecode, compiler, runtime,
  tooling, CLI và các C++ unit test.
- [x] Regression V++ được nối vào CTest trên Unix/Windows; CI Ubuntu có sanitizer.
- [x] `docs/bytecode.md` đã phân biệt rõ proposal `.vbc` với runtime contract hiện tại
  (`Instruction`/`Opcode`) và chỉ ra implementation đang thực thi.
- [x] Pipeline đã có Expression AST, scope tree, name resolution, recursive IR và
  direct IR backend cohort đầu.
- [x] Có regression cho lỗi parser/compiler/semantic, không chỉ expected-output fixture.
- [x] Test discovery đã phân biệt `test/` cho C++ unit và `src/tests/` cho regression
  V++; HTTP fixture được khởi động riêng trong integration runner.
- [x] Parity gate hiện đối chiếu 61 chương trình `.vi`; direct IR backend bao phủ
  61/61 chương trình.
- [x] Parity gate chạy lại cùng corpus theo thứ tự đảo trong cùng process; regression
  compile A → B → A đã khóa lifecycle reset giữa nhiều lần biên dịch.
- [x] VM opcode smoke đã có branch (`OP_JUMP`, `OP_JUMP_IF_FALSE`) và call/return
  (`OP_GOI`, `OP_PARAM`, `OP_TRA_VE`) ở mức bytecode trực tiếp.
- [x] VM opcode smoke đã khóa native collection/text adapter qua direct/indirect call,
  boolean/null + unary stack và assignment/increment/decrement; runtime đã có handler
  trực tiếp cho `OP_DUNG_GIA_TRI` và `OP_SAI_GIA_TRI`.
- [x] Quality baseline đã có coverage gate 45%, `.clang-tidy` versioned và benchmark
  lặp lại được cho VM dispatch, lexer/compiler và native HTTP helpers.

## Đang thực hiện

- [ ] **Tách `VM::run()`:** dispatch chính vẫn là switch lớn trong `src/runtime/vm.cpp`;
  chưa có handler API độc lập để unit test theo opcode.
- [ ] **Compiler state:** đã có `CompilationContext` cho top-level compile theo cơ chế
  reset + snapshot + cleanup, và CLI `runSnippet()` đã dùng context này. Nội bộ
  `StringPool`/`hamMap` vẫn là mutable global state nên compiler chưa re-entrant.
- [ ] **Bỏ token bridge:** regression corpus hiện đạt 61/61 direct IR. Import local
  đã dùng metadata AST/IR có cấu trúc và common import/allocation contract giữa
  legacy với direct backend. Call/parameter/loop header hiện dùng chung splitter
  top-level quote-aware; `loopUltil.h` và đường `compileFunction.cpp` không còn caller
  đã được xóa. `materializeIrTokens()` và legacy compiler
  vẫn tồn tại làm compatibility fallback cho các vùng cú pháp chưa được direct
  backend chấp nhận trong unit/diagnostic cases; cần audit các vùng này trước khi xóa.
- [ ] **CI quality:** coverage threshold và clang-tidy đã có trong job Ubuntu; macOS
  regression CI thường trực vẫn chưa có.

## Rủi ro cần xử lý sớm

- [x] Xác nhận tính độc lập của `vpp-pipeline-legacy-parity`: corpus 61 chương trình
  chạy thuận và đảo thứ tự trong cùng process đều khớp snapshot; chạy riêng parity
  và bộ CTest không gồm integration đều qua.
- [ ] `VM::run()` còn tập trung nhiều side effect/call-frame logic trong một dispatch;
  cần khóa hành vi bằng test trước khi tách handler.
- [ ] Global compiler registries vẫn chặn mục tiêu re-entrant/concurrent compilation.

## Kiểm tra tại thời điểm cập nhật

```text
CTest: 14/14 passed
Integration regression: 54/54 passed
Pipeline parity: 61 chương trình, direct IR 61 chương trình
Coverage cross-check: 75.77% line coverage (8,884/11,725), gate 45%
Benchmark baseline: VM dispatch + lexer + compiler pipeline + native HTTP helpers
Short-term: 14/15
Medium-term: 10/18
Long-term: 10/23
```

## Ưu tiên tiếp theo

1. [x] Làm test parity chạy độc lập ổn định và thêm regression cho lifecycle compile nhiều lần.
2. [x] Mở rộng VM opcode matrix cho branch + call/return trước khi tách `VM::run()`.
3. [x] Hoàn tất bước đầu `CompilationContext`: top-level reset/snapshot/cleanup và
   migrate `runSnippet()`; tiếp tục dời registry nội bộ ở các bước sau.
4. [x] Đưa toàn bộ regression corpus 61/61 sang direct IR và khóa bằng parity gate;
   bước xóa hẳn compatibility bridge được theo dõi riêng ở mục "Bỏ token bridge".
5. [x] Sau khi test architecture ổn định, thêm coverage + clang-tidy + benchmark baseline.
