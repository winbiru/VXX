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
toạ độ nguồn. Parser tạo AST cấu trúc; semantic analysis hiện xử lý khai báo và
lời gọi trực tiếp, rồi sẽ mở rộng thành scope/name resolution đầy đủ. IR cho
optimizer là biểu diễn trung gian **không kiểu**.

V++ vẫn là runtime giá trị động (`int`/`double`/`string`/`rỗng`/map scalar).
Vì vậy pipeline hiện chưa áp dụng typed IR hay một type policy tĩnh: không suy
ra rằng semantic analysis đồng nghĩa với static type checker. Một quyết định
riêng về dynamic, static hay gradual typing là điều kiện trước khi bổ sung IR
có kiểu.

Giai đoạn Bytecode dùng một *legacy bytecode backend bridge*: IR đã tối ưu đi
qua cầu nối này để dùng backend bytecode hiện có. Cách làm này bảo toàn tương
thích với bytecode, CLI và V++ VM trong khi từng bước mới được hoàn thiện; nó
không biến backend cũ thành API compiler public lâu dài.

CLI đưa ranh giới này ra dùng thực tế qua `--dump-ast <file.vi>` và
`--dump-ir <file.vi>`. AST dump hiển thị cây cấu trúc/span của parser; IR dump
hiển thị IR sau optimizer, tức dữ liệu đi vào legacy bytecode bridge. Các dump
không chạy VM và được in ra stdout để có thể redirect hoặc dùng trong test.

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
