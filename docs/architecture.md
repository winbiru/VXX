# Kiến trúc V++

V++ tách compiler, runtime, tooling và các gói ngôn ngữ thành các lớp có dependency một chiều. Mục tiêu là để CLI chỉ ghép các thành phần; parser không biết HTTP/DB, và runtime không đọc compiler global state.

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
- `vpp-runtime`: VM và native adapters HTTP/JSON, file/config, filesystem/system,
  database, collections và text.
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

Top-level compile hiện đã có `CompilationContext` sở hữu `StringPool`, function maps,
import set, class/access state và `importResolutionBase` của riêng compilation. Production
pipeline truyền registry tường minh qua direct codegen, callable lookup và recursive import;
không còn bind `CompilationContext` vào active registry thread-local. `StringPool`, `hamMap`,
context-less `compilePipeline(...)` và `resetCompilationState()` chỉ còn là compatibility API
cho test/caller cũ. CLI, tooling lint và `compileSource()` đều tạo/dùng `CompilationContext`
riêng. Hai top-level compilation độc lập trên hai thread đã có regression kiểm tra isolation
cả registry lẫn import path, kể cả hai module cùng tên nằm ở hai thư mục khác nhau; regression
khác cố ý đặt một legacy active registry chứa dữ liệu "poison" rồi xác nhận context-driven
compile không đọc hay ghi registry đó. Public embedding API/`BytecodeProgram` vẫn chưa chốt.

Lifecycle production hiện tại được khóa như sau: top-level caller tạo
`CompilationContext`, `compilePipeline(CompilationContext&, ...)` xóa state transient cũ
rồi truyền context xuyên suốt frontend → codegen. Recursive import gọi
`compilePipelineInRegistry(..., topLevel=false)` trên chính registry đó nên module con giữ
chung StringPool/function/module metadata mà không cần global binding. Relative import được
resolve từ `CompilationContext.importResolutionBase`; CLI truyền thư mục của entry source
vào context nên compiler không cần đổi process cwd. Để giữ hành vi tương thích cho native
file/database dùng path tương đối, CLI chỉ đổi cwd trong scope `VM::run()`.

`VM::run()` hiện giữ lifecycle/GC và routing; logic opcode đã được tách thành các
handler theo nhóm. Unit test handler dùng `VMRuntimeFixture` ở
`src/include/vpp/runtime/vm_fixture.h` để dựng stack, PC, variables, call frame và
control stacks rồi gọi handler trực tiếp. Fixture cũng cấu hình `OutputSink`, nên test
`OP_IN` không phụ thuộc stdout. Fixture này là internal test boundary, không phải API
embedding ổn định.

Tracing GC dùng `RuntimeHeap` riêng theo VM. Factory của map/list/tuple/class/instance
đăng ký weak handle vào heap đang active; collector mark từ stack, variables, call frame,
receiver, class table và switch value rồi cắt cạnh của object không reachable để phá
shared_ptr cycle. Function/module child VM chia sẻ cùng heap và nhận snapshot root của
caller, vì vậy collection trong lời gọi lồng nhau không làm mất object còn nằm trên stack
bên ngoài. GC chạy mặc định với interval 2048 opcode; `VPP_GC_INTERVAL` dùng để điều chỉnh
interval và regression ép xuống 1 để stress root boundary.

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
capture metadata riêng. Import local/package đã có `AstImportSpec`, module graph và
structured IR payload. Với local `.vi`, Phase 1 module semantics đã bổ sung stable
module identity, export index cho top-level function/class public hoặc không ghi
visibility, namespace alias đưa vào `SemanticEnvironment`, và semantic call kind
`ImportedFunction`. Production semantic pass chỉ index direct imports để tránh quét
lặp trong recursive compile; graph API vẫn hỗ trợ traversal đệ quy cho tooling.
Explicit export/re-export và package/bare-module resolver vẫn chưa hoàn tất. IR cho optimizer là
biểu diễn trung gian **không kiểu**.

Module lifecycle có hai lớp tách biệt. Compiler dùng `ModuleInitializationTracker` cho
semantic/indexing contract và giữ top-level bytecode của từng local module trong
`CompilationContext.moduleInitializers`. Recursive import ghi metadata theo thứ tự
dependency-first. CLI chuyển danh sách này sang `VM::addModuleInitializer()` trước khi
`VM::run()`.

Runtime sở hữu `vietvm::runtime::ModuleTable` và không phụ thuộc compiler headers.
Mỗi module đi qua `uninitialized → initializing → initialized/failed`; initializer chỉ
chạy một lần, module `Initialized` không chạy lại ở lần `VM::run()` sau, còn lỗi khởi tạo
để lại state `Failed`. Child VM dùng cho function call không khởi tạo lại module. Module
initializer ghi state runtime vào cùng variable/class state được VM đưa vào tracing roots;
module table tự thân chỉ giữ bytecode initializer và lifecycle state, không sở hữu
`StackValue` cần mark riêng.

V++ vẫn là runtime giá trị động. `StackValue` hiện mang scalar
(`int`/`double`/`string`/`rỗng`), collection handle và object handle. Runtime object
substrate gồm `RuntimeClass` với optional superclass + method table theo runtime function
ID, và `RuntimeInstance` với field map. Method lookup đi từ class hiện tại lên superclass,
field lưu `StackValue`, class/instance so sánh theo identity. Runtime layer này không phụ
thuộc compiler.

Object model hiện có lát cắt ngôn ngữ end-to-end đầu tiên. Dotted name vẫn là một token
để giữ tương thích với module alias và static class method; semantic analysis chỉ biến
`receiver.member` thành instance member khi `receiver` bind tới biến runtime. Call tới
tên class được đánh dấu `ClassConstructor`; call tới instance member được đánh dấu
`InstanceMethod`. Untyped IR có `LoadProperty`/`StoreProperty`, còn bytecode bổ sung
`OP_TAO_LOP`, `OP_THEM_PHUONG_THUC`, `OP_TAO_DOI_TUONG`, `OP_DOC_THUOC_TINH`,
`OP_GAN_THUOC_TINH` và `OP_GOI_PHUONG_THUC`. Vì vậy `obj = Class()`, field read/write và
bound-method dispatch chạy qua production Direct IR. Instance method giờ có hai receiver ẩn
thuần Việt: `mình` là instance hiện tại, còn `gốc` giữ cùng instance nhưng method call bắt đầu
lookup từ superclass của lớp đã cung cấp method hiện tại. Semantic scope khai báo hai tên,
direct emitter chỉ phát hidden receiver binding cho tên thật sự được dùng, và
`OP_GOI_PHUONG_THUC` chuyển instance cùng owner-class qua call frame mà không làm lệch chỉ số
tham số nguồn. Vì vậy `mình.field`/`mình.method(...)` dùng current instance, còn
`gốc.method(...)` bỏ qua override hiện tại để gọi superclass; field storage hiện vẫn nằm trên
instance nên `gốc.field` truy cập cùng field map. `self` và `this` không còn là implicit
receiver. Source inheritance dùng `lớp Con kế thừa Cha { ... }`; parser giữ superclass metadata,
semantic phân giải trong type space và chặn superclass không tồn tại/tự kế thừa/chu trình,
direct emitter đảm bảo lớp cha được đăng ký trước lớp con, còn VM nối superclass thật vào
`RuntimeClass`. Interface dùng `giao diện I { hàm f(...); }`; một interface có thể kế thừa
nhiều interface qua `giao diện J kế thừa I, K`, còn class dùng
`lớp C kế thừa Base triển khai I, J` để giữ một superclass nhưng nhận nhiều hợp đồng. Semantic
kiểm tra target interface, duplicate/cycle, tên + số tham số method và visibility công khai;
method kế thừa từ superclass được phép thỏa hợp đồng. Interface hiện chỉ tồn tại ở frontend/
semantic và hạ thành no-op compile-time, không thêm runtime interface table hay thay đổi
dynamic method dispatch. Constructor dùng `hàm khởi tạo(...)`, nhận tham số/default parameter và có
thể gọi constructor cha tường minh qua `gốc.khởi tạo(...)`. Method visibility được kiểm tra
cả ở semantic khi suy luận được lớp receiver và ở runtime method table cho receiver động;
private chỉ dùng trong lớp sở hữu, protected dùng trong lớp sở hữu/subclass. Field vẫn là
thuộc tính động public vì ngôn ngữ chưa có declaration/modifier field riêng.

Vì vậy pipeline hiện chưa áp dụng typed IR hay một type policy tĩnh: không suy
ra rằng semantic analysis đồng nghĩa với static type checker. Một quyết định
riêng về dynamic, static hay gradual typing là điều kiện trước khi bổ sung IR
có kiểu.

Compiler hiện chỉ có một production backend: **Direct IR → bytecode**. Nếu program chứa
region mà direct emitter chưa hỗ trợ, pipeline báo lỗi compiler tường minh thay vì
materialize token rồi chuyển sang backend cũ. Regression gate khóa toàn bộ **79 chương
trình `.vi`** trong corpus ở direct IR.

CLI đưa ranh giới này ra dùng thực tế qua `--dump-ast <file.vi>` và
`--dump-ir <file.vi>`. AST dump hiển thị statement tree, expression arena và
span; IR dump hiển thị value arena, statement children và số region chưa được direct
emitter hỗ trợ sau optimizer. Các dump không
chạy VM và được in ra stdout để có thể redirect hoặc dùng trong test.

### Direct IR migration

Không được xem sơ đồ pipeline là bằng chứng rằng mọi feature đã hoàn thiện.
Expression arena, scope tree, ExprId-based resolution và recursive untyped IR đã
có. Direct emitter hiện nhận literal/name/operator/assignment/postfix/print, list/map
literal đệ quy như giá trị hạng nhất (kể cả trong call argument), top-level function,
primitive default parameter, return, resolved function call, structured
if/else/for-loop/switch/try-catch, continue, break, throw và class namespace/method.
Lambda capture-free, dynamic/native/indirect call và structured import đã có direct
emission trong regression corpus. Grammar edge/malformed case chưa được hỗ trợ sẽ bị
từ chối bằng diagnostic thay vì rơi sang token compiler. Mốc migration toàn corpus và
việc xóa production bridge đều đã đạt:

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
Unsupported Direct IR = 0 trên regression corpus (đã đạt 70/70)
```

IR vẫn có metadata `UnsupportedDirectRegion` để analyzer/diagnostic nhận diện phần chưa
được direct emitter hỗ trợ. Emitter “trực tiếp” nghĩa là đọc IR operands/control-flow,
không parse token lần nữa; một IR opcode vẫn có thể phát nhiều VM instruction.
Parity test khóa fingerprint của top-level bytecode, StringPool, function bytecode và
function-name map theo baseline đã đóng băng trên toàn bộ corpus `.vi`.

CTest `vpp-pipeline-legacy-parity` tự động quét `src/tests/**/*.vi` và thực hiện
so sánh pipeline hiện tại với manifest `test/data/legacy_compiler_snapshots.tsv`.
Production compiler không chứa API, mode hay nhánh code dành riêng cho test.
Test là compile-only để không mở cổng HTTP hay gọi native/external service;
regression runtime/output hiện hành vẫn do các runner `.vi` đảm nhiệm. Không
được tạo lại hàng loạt manifest để làm test xanh: mỗi thay đổi fingerprint phải
được review như một thay đổi bytecode/compiler-state có chủ ý.

Gate hiện tự động quét **70/70** chương trình `.vi`, xác nhận compiler snapshots khớp
và yêu cầu mọi program có `unsupportedDirectIrRegions == 0`. Source test dùng
`CompilationContext` cho từng top-level compile, sau đó chạy lại corpus theo thứ tự
ngược trong cùng process để khóa reset/import-base isolation. Toàn corpus chính là direct-IR
contract; không còn backend selector hay token compiler để quay lại.

Các bước trên dùng IR không kiểu và giữ semantics động hiện hành. Quyết định
dynamic/static/gradual chỉ là điều kiện cho type checking/Typed IR, không phải
điều kiện để xây Expression AST, scope hay name resolution.

GC hiện là tracing collector cho object graph runtime và có cycle sweep; profiler/allocation
telemetry vẫn còn thiếu. JIT vẫn là MVP và chưa phải compiler sinh mã máy production-grade.

## Headers

Header public mới bắt đầu dưới `src/include/vpp/`, ví dụ `vpp/core/text.h`, `vpp/bytecode/{instruction,opcode}.h`, `vpp/compiler/compiler.h`, `vpp/runtime/{value,vm}.h` và `vpp/tooling/tooling.h`. Đường dẫn header được quy hoạch cho pipeline là `src/include/vpp/frontend/{token,ast,parser}.h` và `src/include/vpp/compiler/{semantic,ir,optimizer,pipeline}.h`; chúng mô tả ranh giới frontend/compiler mới và chưa nên được xem là embedding API ổn định cho đến khi policy kiểu được chốt. `vpp/runtime/value.h` là ranh giới chung cho VM và native adapters, nên native header không phải kéo theo `VM`. `vpp/runtime/vm_fixture.h` nằm trong namespace path mới nhưng chỉ phục vụ test nội bộ. Header compatibility dưới `src/include/common`, `src/include/compiler` và `src/include/vm` còn được giữ để tránh phá vỡ mã hiện có. Header compiler detail, VM call frame, fixture và native implementation là internal implementation, không phải embedding API.

## Thư viện V++ và framework modules

Thư viện chuẩn là tập package `.vi` nằm trực tiếp dưới `gói/`. Package `chuẩn`
chỉ là entrypoint tổng hợp:

```text
gói/
├── chuẩn/
│   └── main.vi             # entrypoint tổng hợp
├── lõi/                    # toán, UTF-8 cơ bản, collections, chuyển kiểu, random
├── nhập xuất/              # tệp, path/thư mục, cấu hình, đồng hồ, nhật ký
├── hệ thống/               # env, nền tảng, sleep
├── mạng/                   # HTTP client/server + REST + JSON parse/serialize
├── dữ liệu/                # phân trang và database adapter
├── ứng dụng/               # lifecycle chung + cầu nối tùy chọn
├── dựng/                   # facade web, dữ liệu và ứng dụng full stack
└── kiểm thử/               # assertion helpers, không import mặc định
```

Program mới nên import package hẹp nhất. Tên package có khoảng trắng có thể
để trần hoặc đặt trong dấu nháy; đường dẫn trực tiếp có khoảng trắng phải dùng
dấu nháy:

```vi
nhập lõi;
nhập "nhập xuất";
nhập "hệ thống";
nhập mạng;
nhập dựng;
nhập "gói/mạng/kiểm thử/api.vi";
```

Bare import ưu tiên package cùng tên của project, rồi mới tìm package bundle
dưới `$VPP_HOME/gói/`. Các alias package cũ được xử lý ở resolver khi cần,
không xuất hiện trong layout canonical.
`gói/ứng dụng/main.vi` không import `gói/ứng dụng/cầu nối/api.vi`;
routes/schema/token của một project mẫu không phải standard library.

Các package này được bundle cùng runtime nhưng chưa có dependency/version resolver. HTTP, REST và JSON hiện cùng nằm trong package `mạng` để dùng một entrypoint thống nhất. JSON object/array được ánh xạ trực tiếp sang map/list runtime. `lõi` nối native cho chuyển kiểu/type và random; `nhập xuất` nối path/filesystem; `hệ thống` nối env/platform/sleep. Các native helper này không phụ thuộc CLI/stdout.

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
3. Không đưa framework web/dữ liệu/ứng dụng vào `gói/lõi` hoặc import full-stack mặc định.
4. Thay đổi public behavior cần test `.vi` và expected output; example/scaffold cần smoke test.
