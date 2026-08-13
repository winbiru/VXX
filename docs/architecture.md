# Kiến trúc V++

V++ tách compiler, runtime, tooling và thư viện ngôn ngữ thành các lớp có dependency một chiều. Mục tiêu là để CLI chỉ ghép các thành phần; parser không biết HTTP/DB, và runtime không đọc compiler global state.

## C++ modules

```text
vpp-core ──> vpp-bytecode ──┬──> vpp-frontend ──> vpp-compiler ──> vpp-tooling
                             └──────────────────────────────────> vpp-runtime
                                                                   │
vpp-cli <─────────────────────────────────────────────────────────┘
```

- `vpp-core`: text, UTF-8 path và layout project/package dùng chung, không phụ thuộc ngôn ngữ.
- `vpp-bytecode`: opcode và mô tả bytecode dùng chung cho compiler/runtime.
- `vpp-frontend`: lexer và keyword map.
- `vpp-compiler`: compile expression/statement/import cùng symbol state.
- `vpp-runtime`: VM và native adapters HTTP, file, config, database.
- `vpp-tooling`: formatter, linter, disassembler và renderer AST/IR cho debug.
- `vpp-cli`: REPL, LSP command loop, package/scaffold commands và entrypoint.

Source hiện nằm tại:

```text
src/core/
src/bytecode/
src/frontend/
src/compiler/{support,...}
src/runtime/{native,...}
src/tooling/
src/cli/
```

CMake định nghĩa các target `vpp-core`, `vpp-bytecode`, `vpp-frontend`, `vpp-compiler`, `vpp-runtime`, `vpp-tooling` và `vpp-cli`. Không dùng `GLOB_RECURSE`; mỗi source có owner rõ ràng.

## Runtime boundary

CLI compile source rồi copy function bytecode/name table vào `VM`. VM không còn đọc `compiler::hamMap` hay `StringPool` global ở runtime. Điều này làm runtime có thể nhận bytecode từ nguồn khác ngoài CLI.

Compiler vẫn dùng mutable state nội bộ trong phiên compile. Bước tiếp theo của API embedding là thay state này bằng `CompilationContext` và `BytecodeProgram` bất biến; không xem các header `compile*.h` là public API ổn định.

## Compiler pipeline

Pipeline compiler được tách thành các bước rõ ràng, nhưng được đưa vào theo
hướng incremental để giữ nguyên hành vi đang có:

```text
Source
  ↓
Lexer (token mang span)
  ↓
Parser
  ↓
AST
  ↓
Semantic Analysis
  ↓
IR không kiểu
  ↓
Optimizer
  ↓
Bytecode
  ↓
V++ VM
```

Lexer gắn span nguồn vào token để parser, AST và các diagnostic sau đó có cùng
toạ độ nguồn. Parser tạo statement tree và expression arena; semantic analysis tạo
scope tree rồi bind expression/call theo ExprId. Lambda body có scope, binding và
capture metadata riêng; module graph/import exports và các tolerant token region
vẫn chưa được resolve hoàn toàn. IR cho optimizer là
biểu diễn trung gian **không kiểu**.

V++ vẫn là runtime giá trị động (`int`/`double`/`string`/`rỗng`/map scalar).
Vì vậy pipeline hiện chưa áp dụng typed IR hay một type policy tĩnh: không suy
ra rằng semantic analysis đồng nghĩa với static type checker. Một quyết định
riêng về dynamic, static hay gradual typing là điều kiện trước khi bổ sung IR
có kiểu.

Backend selector phát bytecode trực tiếp từ IR khi toàn program thuộc cohort được
hỗ trợ. Nếu còn instruction/value chưa hỗ trợ, compiler materialize token payload và
dùng legacy backend cho toàn program. Cách chuyển tiếp này bảo toàn bytecode/VM
contract; backend cũ không trở thành API compiler public lâu dài.

CLI đưa ranh giới này ra dùng thực tế qua `--dump-ast <file.vi>` và
`--dump-ir <file.vi>`. AST dump hiển thị statement tree, expression arena và
span; IR dump hiển thị value arena, statement children và fallback count sau
optimizer, tức dữ liệu dùng để chọn direct emitter hoặc bridge. Các dump không
chạy VM và được in ra stdout để có thể redirect hoặc dùng trong test.

### Thứ tự thay legacy bridge

Không được xem sơ đồ pipeline là bằng chứng rằng mọi feature đã hoàn thiện.
Expression arena, scope tree, ExprId-based resolution và recursive untyped IR đã
có. Direct emitter hiện nhận literal/name/operator/assignment/postfix/print/primitive map,
top-level function, primitive default parameter, return, resolved function call, structured
if/else/for-loop/switch/try-catch, continue, break, throw và class namespace/method.
Lambda capture-free và dynamic/native/indirect call đã phát trực tiếp; lambda có
capture và Import vẫn đi qua bridge. Migration
tiếp tục theo thứ tự:

```text
Expression AST
  ↓
Scope tree
  ↓
Real name resolution
  ↓
Recursive IR lowering
  ↓
Direct IR → bytecode emission theo từng opcode/feature
  ↓
Giảm dần legacy token fallback về 0 rồi mới xóa bridge
```

Trong giai đoạn chuyển tiếp, IR đánh dấu explicit legacy regions cho phần còn
phụ thuộc token; direct-support analyzer còn đếm top-level instruction đã có IR
cấu trúc nhưng emitter chưa hỗ trợ. Chỉ khi toàn program supported mới chọn
direct backend. Emitter “trực tiếp” nghĩa là đọc IR operands/control-flow, không
parse token lần nữa; một IR opcode vẫn có thể
phát nhiều VM instruction. Mỗi nhóm feature chỉ được chuyển sang emitter mới
khi parity test xác nhận fingerprint của top-level bytecode, StringPool,
function bytecode và function-name map vẫn khớp baseline legacy đã đóng băng
trên toàn bộ corpus `.vi`.

CTest `vpp-pipeline-legacy-parity` tự động quét `src/tests/**/*.vi` và thực hiện
so sánh pipeline hiện tại với manifest `test/data/legacy_compiler_snapshots.tsv`.
Production compiler không chứa API, mode hay nhánh code dành riêng cho test.
Test là compile-only để không mở cổng HTTP hay gọi native/external service;
regression runtime/output hiện hành vẫn do các runner `.vi` đảm nhiệm. Không
được tạo lại hàng loạt manifest để làm test xanh: mỗi thay đổi fingerprint phải
được review như một thay đổi bytecode/compiler-state có chủ ý.

Gate hiện xác nhận 57/57 compiler snapshots khớp và khóa chính xác
28 direct program trong `requiredDirectPrograms` của
`test/pipeline_legacy_parity_tests.cpp`. Tập này bao phủ expression/map,
function/return/call, condition, recursion, loop/continue, switch/break,
try/throw và class method. Con số phải tăng theo
từng cohort; không được quay lại bridge mà test vẫn xanh.

Các bước trên dùng IR không kiểu và giữ semantics động hiện hành. Quyết định
dynamic/static/gradual chỉ là điều kiện cho type checking/Typed IR, không phải
điều kiện để xây Expression AST, scope hay name resolution.

GC và JIT hiện chỉ là MVP runtime; chúng không phải tracing collector hay
compiler sinh mã máy production-grade.

## Headers

Header public mới bắt đầu dưới `include/vpp/`, ví dụ `vpp/core/text.h`, `vpp/bytecode/{instruction,opcode}.h`, `vpp/compiler/compiler.h`, `vpp/runtime/{value,vm}.h` và `vpp/tooling/tooling.h`. Đường dẫn header được quy hoạch cho pipeline là `include/vpp/frontend/{token,ast,parser}.h` và `include/vpp/compiler/{semantic,ir,optimizer,pipeline}.h`; chúng mô tả ranh giới frontend/compiler mới và chưa nên được xem là embedding API ổn định cho đến khi policy kiểu được chốt. `vpp/runtime/value.h` là ranh giới chung cho VM và native adapters, nên native header không phải kéo theo `VM`. Header legacy dưới `include/common`, `include/compiler` và `include/vm` còn được giữ để tránh phá vỡ mã hiện có. Header compiler detail, VM call frame và native implementation là internal implementation, không phải embedding API.

## Thư viện V++ và framework modules

Thư viện chuẩn là package `.vi` duy nhất dưới `gói/`; các module tiếng Việt
nằm bên trong nó:

```text
gói/
└── thư viện/
    ├── main.vi             # entrypoint đầy đủ
    ├── cốt lõi/            # toán, chuỗi, luận lý, xác thực
    ├── vào ra/             # tệp, cấu hình, đồng hồ, nhật ký
    ├── mạng/               # HTTP client GET/POST/PUT/DELETE và HTTP server native mức thấp
    ├── mạng web/           # REST/JSON helpers; kiểm thử/api không được import mặc định
    ├── dữ liệu/            # phân trang và database adapter
    ├── ứng dụng/           # lifecycle/bootstrap chung
    ├── khởi động/          # facade web, dữ liệu và ứng dụng full stack
    └── kiểm thử/           # assertion helpers, không import mặc định
```

Program mới nên import package hẹp nhất. Tên package có khoảng trắng có thể
để trần hoặc đặt trong dấu nháy; đường dẫn trực tiếp có khoảng trắng phải dùng
dấu nháy:

```vi
nhập cốt lõi;
nhập mạng;
nhập "gói/thư viện/mạng web/kiểm thử/api.vi";
```

Bare import ưu tiên package cùng tên của project, rồi mới tìm module bundle
dưới `gói/thư viện/`. Các đường dẫn phẳng cũ như `gói/cốt lõi/...` được
redirect khi không còn file local tương ứng.
`gói/thư viện/ứng dụng/main.vi` không import API-project adapter tương thích;
routes/schema/token của một project mẫu không phải standard library.

Các module này là bundled optional modules, chưa phải package độc lập có dependency/version resolver. Cài riêng module mạng web mà không có module mạng chưa được package manager tự giải quyết.

## Examples, templates và tests

```text
examples/api_project/              # ví dụ HTTP chạy độc lập
templates/backend/                 # nguồn cho `vpp khởi tạo backend <tên>`
test/*.cpp                         # source C++ cho CTest unit/tooling
src/tests/fixtures/api_project/    # fixture DTO/repository/database deterministic
src/tests/*.vi                     # regression entrypoints
src/tests/expected/*.expected      # output của entrypoint
```

`src/tests/` chỉ chứa chương trình regression V++ (`.vi`), expected output và
fixture runtime; source C++ của CTest nằm ở `test/`. Không đặt application
sample trong `src/tests/`. Regression có vài fixture legacy được track
(`src/tests/api_project.db`, `.tmp_*`); dữ liệu runtime mới phải dùng
`src/tests/.tmp/` đã ignore, không thêm artifact database mới vào Git.

## Build và phát hành

- CMake cài binary, `gói/`, `templates/` và `examples/`.
- Release archive chứa cùng các resource này.
- Installer đặt chúng cạnh CLI và đặt `VPP_HOME`; import resolver tìm local project trước, sau đó tìm `$VPP_HOME/gói`.
- HTTP server native dùng POSIX sockets trên Unix và Winsock2 trên Windows.

## Quy tắc thay đổi

1. Source mới phải có một CMake target owner; không thêm lại thư mục `helpers` chung.
2. Không để frontend phụ thuộc runtime/native.
3. Không đưa framework web/dữ liệu/ứng dụng vào `gói/thư viện/cốt lõi` hoặc import full-stack mặc định.
4. Thay đổi public behavior cần test `.vi` và expected output; example/scaffold cần smoke test.
