# Roadmap V++ 1.0

> Cập nhật: 15/09/2026
>
> Mục tiêu 1.0 không còn là mở rộng cú pháp theo chiều rộng. Trọng tâm là đóng băng
> semantics, làm cứng compiler/runtime, hoàn thiện package + stdlib + toolchain và chứng
> minh V++ chạy ổn định trên dự án thực tế.
>
> Checklist trong file này cố ý liệt kê chi tiết phần **còn phải chứng minh/đóng contract**,
> vì vậy tỷ lệ checkbox không được dùng như phần trăm khối lượng hoàn thành. Ước lượng tiến
> độ tới 1.0 nằm ở cuối tài liệu và được đánh giá theo khối lượng kỹ thuật.

## Định nghĩa V++ 1.0

V++ 1.0 được coi là đạt khi ngôn ngữ có semantics ổn định, compiler/runtime đáng tin cậy,
tracing GC hoạt động tốt dưới tải, module/package đủ dùng, stdlib đủ xây ứng dụng thực tế và
toolchain có thể cài đặt/chạy trên Windows, macOS và Linux.

Các hạng mục **không bắt buộc** để chặn 1.0: JIT production-grade, LLVM backend, generic,
reflection đầy đủ, GUI, FFI đầy đủ và concurrency/async phức tạp. Các phần này chỉ được kéo
vào 1.0 nếu chúng cần thiết để đóng một contract nền tảng đã có.

## 1. Đóng băng semantics ngôn ngữ

- [x] Viết ADR chốt type policy: V++ 1.0 dùng dynamic typing; value/call boundary,
  default parameter và arity compile-time/runtime đã có contract + regression.
- [x] Đóng contract cho scope, shadowing, `rỗng`, truthiness, equality, so sánh và chuyển
  đổi kiểu ngầm; thêm regression cho các trường hợp biên.
- [x] Đóng contract lỗi runtime: loại lỗi, thông điệp, unwind và trạng thái VM sau lỗi.
- [x] Đóng semantics object hiện tại: `mình`, `gốc`, constructor có tham số, đơn kế thừa
  class, nhiều interface compile-time và visibility method private/protected/public.
- [x] Chốt override method đầy đủ: arity, visibility narrowing, constructor interaction và
  diagnostic cho override không hợp lệ.
- [x] Chốt module semantics còn lại: explicit export/re-export, truy cập symbol không export,
  import cycle và diagnostic có đường dẫn module rõ ràng.
- [x] Chốt lambda/closure capture: capture theo value/reference, lifetime và mutation.
- [x] Quyết định destructor/finalizer: không hỗ trợ trong 1.0 hoặc định nghĩa lifecycle rõ ràng;
  không để behavior ngầm phụ thuộc GC.

## 2. Runtime và VM ổn định dưới tải

- [x] Runtime object model, superclass dispatch và tracing GC có cycle sweep đã chạy end-to-end.
- [x] Xây GC stress suite: marker/`trackValue()` dùng worklist lặp; unit test khóa cycle sâu
  4.096 node + burst 1.024 self-cycle, còn regression `.vi` khóa graph sâu/allocation liên tục,
  caller root qua child VM và collection với `VPP_GC_INTERVAL=1`.
- [x] Stress call stack: regression `.vi` chạy recursion sâu + exception xuyên nhiều child VM;
  VM giới hạn độ sâu lời gọi ở 256 và trả `CallBoundary` có diagnostic ổn định thay vì tràn
  native C++ stack. Handler unit khóa việc unwind call frame sau lỗi.
- [x] Bổ sung invariant/regression để runtime error không làm corrupt stack, call frame,
  module lifecycle hoặc heap roots cho lần chạy tiếp theo. Handler unit khóa cùng VM tiếp tục
  gọi function hợp lệ sau `VmFault`, caller stack/root vẫn nguyên và module failed-state ổn định.
  Catch variable trong function bind vào local frame/captured cell, không ghi nhầm global;
  regression `.vi` khóa cả closure capture biến catch.
- [ ] Chạy ASan/UBSan/LSan hoặc công cụ tương đương cho stress suite; local AppleClang
  ASan+UBSan hiện xanh 2/2 CTest trên test tree hiện hành, gồm focused internal hardening +
  integration regression (147.09s local ngày 18/09/2026). CI Linux ép `detect_leaks=1`; cần một
  lượt CI Linux xanh để đóng phần LSan/leak của gate.
- [x] Xây stack trace có source file, line/column, function/method và module identity;
  debug metadata đi song song bytecode nên không thay đổi layout `Instruction`/ABI snapshot,
  `RuntimeError` giữ frame có cấu trúc và CLI nén frame đệ quy lặp liên tiếp. Diagnostic runtime
  dùng mã ổn định `VPP-Rxxxx` + catalog tập trung để sinh nhóm lỗi, giải thích và gợi ý mà
  không parse chuỗi `what()` hoặc lộ opcode/program counter nội bộ.
- [ ] Bổ sung benchmark dài hạn và profiler/event hook cho CPU/timing/allocation/GC latency;
  chỉ tối ưu JIT/dispatch dựa trên số đo.

## 3. Module và package system dùng được cho dự án thật

- [x] Chốt project manifest `vpp.json` schema 1 và layout project chuẩn; model/parser/writer
  dùng chung nằm ở core, đọc tương thích manifest legacy `gói: [...]` và ghi canonical
  deterministic. Contract được mô tả trong `docs/package-system.md`.
- [x] Tách package/bare-module resolution khỏi `compileRegistry`: `PackageResolver` sở hữu
  lookup file/project package, compatibility redirect, alias chuẩn và `VPP_HOME`; unit test
  khóa thứ tự ưu tiên và UTF-8 package name.
- [x] Hỗ trợ Semantic Versioning 2.0 + range `*`, exact, `^`, `~`, comparator AND; `vpp khóa`
  kiểm tra exact version đã cài với range manifest và trả conflict rõ ràng.
- [x] Tạo deterministic lockfile và reproducible install/build.
  `vpp.lock` schema 1 + deterministic writer/content fingerprint đã có; install/update/sync
  tự ghi lock, restore/install từ lock ưu tiên exact cache bytes và có cache-only offline.
  CLI run/compile path tìm lock gần nhất và từ chối package thiếu hoặc fingerprint lệch trước
  khi compiler chạy, nên project đã khóa luôn dùng đúng dependency bytes đã chấp nhận.
- [x] Hỗ trợ package cache/offline, local path package và Git package.
  Git transport được tách khỏi solver, resolve branch/tag/commit thành exact revision trong
  `vpp.lock`, loại `.git/` khỏi artifact và restore exact commit khi cache thiếu. Cache-only
  `restore/install --offline` vẫn phục hồi được exact bytes khi source path/repository không còn.
- [x] Thiết kế registry package và metadata tối thiểu cho publish/install.
  Filesystem registry v1 dùng `<root>/<name>/<semver>/` + `vpp.json`, chọn version cao nhất
  thỏa range, version publish bất biến và hỗ trợ root tường minh hoặc `VPP_REGISTRY`.
- [x] Hoàn thiện CLI dependency flow tương đương `vpp cài`, `vpp cập nhật`, `vpp xóa`,
  `vpp khóa`: path/Git/registry cùng đi qua graph + lock/cache/restore, registry update chọn
  version mới trong range còn restore pin exact version/fingerprint đã khóa.

## 4. Standard library 1.0

- [x] Audit UTF-8 end-to-end cho string/index/slice/length/case/normalization vì tiếng Việt là
  first-class syntax. Đã hoàn tất code-point semantics cho length/reverse/index/slice,
  validator malformed UTF-8, case conversion cho toàn bộ chữ cái tiếng Việt dựng sẵn và NFC
  composition cho tổ hợp nguyên âm + dấu tiếng Việt. Title/palindrome/anagram cũng dùng NFC +
  case tiếng Việt. Đếm/chứa/thay chuỗi cũng normalize NFC, và corpus NFD -> NFC đã phủ toàn bộ
  67 chữ thường tiếng Việt. Scope 1.0 không mở rộng Unicode tổng quát; phần text/string tiếng
  Việt của mục này đã đủ contract 1.0.
- [ ] Chuẩn hóa filesystem/path API đa nền tảng và edge case separator/encoding. Native
  boundary đã reject malformed UTF-8 cho cả path API, đọc/ghi/đếm tệp và đọc cấu hình, trả
  path bằng generic `/`, và regression khóa Unicode directory/file + join/parent/missing-path
  cùng file/config tiếng Việt + Unicode ngoài BMP. Regression hiện khóa thêm filename và ba
  predicate trên path không tồn tại; test suite Windows đã đồng bộ cùng test crypto. Còn cần
  release-matrix Windows/Linux/macOS xanh trước khi đóng mục.
- [x] Hoàn thiện time/date API và timezone contract ở mức đủ dùng cho ứng dụng thực tế.
  `lấy thời gian hiện tại()` trả local ISO-8601 có UTC offset, `lấy thời gian utc()` trả UTC
  ISO-8601 hậu tố `Z`, còn `độ lệch múi giờ()` trả offset theo phút. Runtime dùng API chuyển
  `tm` thread-safe theo nền tảng và regression khóa format/offset/arity qua native + `.vi`.
- [x] Chốt process API 1.0: chưa expose spawn/exec/shell công khai vì runtime chưa có
  permission/capability model để giới hạn thực thi tiến trình. Các adapter nội bộ hiện có không
  trở thành stdlib contract; process API được hoãn sang sau 1.0 cùng permission model.
- [ ] Bổ sung crypto cơ bản bằng implementation đã được kiểm chứng: secure random, hash/HMAC;
  không tự viết primitive mật mã trong VM. Đã bổ sung API `ngẫu nhiên bảo mật`, `băm sha256`
  và `hmac sha256`; runtime dùng Security/CommonCrypto trên macOS, BCrypt trên Windows và
  OpenSSL trên Linux, trả hex chữ thường và có regression bằng SHA-256/HMAC-SHA256 test vector.
  Cần release-matrix Windows/Linux/macOS xanh trước khi đóng mục.
- [x] Nâng package testing: assertion cơ bản + rỗng/không rỗng, expected error qua callback,
  setup/teardown bảo đảm cleanup khi thân test lỗi và test discovery CLI đệ quy theo thứ tự
  xác định; contract được khóa bằng regression và mô tả trong `docs/testing.md`.
- [ ] Audit các package đã có: collection, file, JSON, HTTP client/server, math, random,
  environment, logging và config; đóng behavior + lỗi + cross-platform test cho 1.0. Slice
  file/config/random đã khóa UTF-8 path, line/word count, config key/fallback và strict integer
  bounds; collection/JSON đã khóa thứ tự key deterministic cho `khóa map` và `json tạo` để
  output không phụ thuộc `unordered_map`; HTTP client đã validate URL HTTP(S), UTF-8, host và
  ký tự điều khiển trước khi gọi curl. Math đã khóa chia/chia dư cho 0 theo runtime error,
  giữ `chia an toàn` làm fallback riêng và từ chối số mũ/giai thừa không phải số nguyên không âm.
  Environment đã validate tên biến nhất quán trước host API (rỗng/`=`/NUL/UTF-8 lỗi bị từ chối),
  giữ fallback cho biến hợp lệ bị thiếu. Logging đã khóa wrapper, Unicode tiếng Việt, giá trị động
  và giá trị trả `0` bằng regression. JSON parser/serializer đã chặn UTF-8 lỗi ở input, string value
  và object key, đồng thời giữ tiếng Việt/Unicode ngoài BMP hợp lệ. Collection đã chặn overflow ở `tổng list`
  và khóa diagnostic cho sort hỗn hợp/min rỗng; HTTP validate authority, port và IPv6 trước curl;
  math từ chối modulo số thực có phần lẻ và cận `giới hạn` đảo ngược; text + helper integer
  dùng chung chấp nhận số thực tích phân nhưng từ chối phần lẻ/suffix thay vì cắt ngầm. Phần còn lại là xác nhận
  các contract này trên release-matrix trước RC.

## 5. Compiler hardening

- [x] `CompilationContext` đã sở hữu registry chính và có regression compile song song bằng
  context độc lập.
- [x] Loại bỏ mutable global state/facade còn lại khỏi production compiler: direct codegen,
  callable lookup và recursive import nhận registry tường minh; CLI/tooling/`compileSource`
  dùng `CompilationContext`. Facade thread-local chỉ còn cho compatibility test/caller cũ,
  và regression "poison legacy registry" khóa việc context-driven compile không phụ thuộc nó.
- [x] Thêm fuzzing cho lexer/parser và malformed-source corpus có seed cố định.
  `scripts/quality/rc-hardening.py` chạy 16 malformed corpus case + 512 input sinh xác định từ
  seed `0x56505031` qua public CLI, giới hạn timeout từng case và từ chối crash/signal. Script
  đã nối vào CI Ubuntu/macOS/Windows.
- [x] Thêm malformed AST/invalid IR tests và bytecode verifier trước khi VM thực thi input
  không tin cậy. Focused target `vpp-rc-internal-hardening` kiểm tra ExprId ngoài arena, chu trình
  AST, IR operand/control-flow sai cùng verifier cho opcode/jump/StringPool/function/closure
  metadata; VM vẫn chạy verifier cho root/function bytecode trước dispatch.
- [x] Khóa deterministic compilation/reproducible bytecode cho cùng source + dependency lock.
  RC hardening script biên dịch cùng source + import graph ba lần và so khớp byte-for-byte output
  AST, optimized IR và disassembly; package lock khóa exact dependency version/revision + fingerprint.
- [x] Stress source/project lớn và theo dõi peak memory/compiler time để phát hiện regression.
  RC hardening script khóa source 800 hàm, project 24 module × 24 hàm và 5 lượt compile,
  timeout 30 giây mỗi compile + peak RSS budget 1 GiB trên Unix/macOS. Baseline local 18/09/2026:
  single source 4.01s, module stress 3.18s tổng năm lượt, peak RSS 17.0 MiB.

## 6. Toolchain và trải nghiệm phát triển

- [x] Đóng public CLI contract cho `new`, `run`, `test`, `build` và package commands.
  Giao diện canonical 1.0 dùng tiếng Việt: `khởi tạo`, `chạy`, `kiểm thử`, `dựng` và
  namespace `gói`; `new/run/test/build` cùng các tên package tiếng Anh chỉ là alias tương thích.
  `kiểm thử` quét `.vi` theo thứ tự xác định và trả summary; `dựng` biên dịch đầy đủ nhưng chưa
  ghi artifact `.vbc` theo compatibility policy 1.0. CLI parser hiện giữ canonical command + alias;
  dedicated CMake contract test đã bị xóa ở `473a8e4`.
- [x] Hoàn thiện formatter và linter đủ ổn định để dùng trong CI/editor. Formatter giữ comment
  + literal, bảo toàn compiler token stream, idempotent và có `--kiểm-tra`/`--ghi-tệp` cho
  file hoặc thư mục. Linter trả diagnostic có cấu trúc gồm severity + source span, CLI
  `--soát-lỗi` xuất `tệp:dòng:cột` và exit code phù hợp CI, còn LSP dùng cùng diagnostic range.
  Dedicated CMake tooling/CLI tests đã bị xóa ở `473a8e4`; public behavior hiện được giữ qua
  implementation + integration workflow và cần được bổ sung lại bằng CLI-level checks nếu mở rộng.
- [x] Nâng LSP: diagnostic, completion, go-to-definition, hover và rename dựa trên semantic model.
  Completion/definition/hover/rename dùng symbol/binding từ semantic model; rename chỉ sửa
  declaration + reference cùng `SymbolId`, range LSP dùng UTF-16 và declaration được thu hẹp
  đúng token tên thay vì toàn statement. Dedicated LSP CMake test đã bị xóa ở `473a8e4`.
- [x] Đồng bộ VS Code extension với LSP/toolchain 1.0 và syntax mới.
  Extension tự khởi động `vpp --lsp` khi mở tài liệu V++, đồng bộ full-document change và
  nối diagnostic/completion/definition/hover/rename/format vào VS Code mà không cần dependency
  npm ngoài. Có cấu hình bật/tắt + executable tùy chọn, command restart LSP, tự ưu tiên VM do
  extension quản lý rồi PATH. TextMate grammar đã bổ sung `giao diện`, `kế thừa`, `triển khai`,
  `mình` và `gốc`; manifest/JS/grammar đều qua kiểm tra cú pháp.
- [x] Bổ sung project templates và ít nhất một sample project thực tế ngoài regression corpus.
  `vpp khởi tạo ứng dụng <tên>` tạo project schema 1 với `src/`, `tests/`, `gói/`, README
  và `.gitignore`, đồng thời giữ nguyên contract cũ của `khởi tạo <tên>`. Sample
  `examples/hoa-don-cua-hang` dùng module, class/constructor và import tương đối. Ngày 18/09/2026,
  `examples/quan-ly-kho-api` đã dựng, chạy feature gate, rollback và demo end-to-end thành công bằng
  build tree; HTTP smoke bị sandbox local chặn bind socket và vẫn cần chạy trên CI/release host.
- [x] Hoàn thiện installer/update path cho Windows, macOS và Linux; đánh giá Homebrew/winget
  sau khi install contract ổn định. Installer Unix/Windows dùng staged update và thay toàn bộ
  `gói/`, `templates/`, `examples` để không giữ file stale; có chế độ CI không sửa profile/PATH.
  Release workflow hiện build + regression + RC hardening, sau đó giải nén chính artifact,
  cài hai lần vào prefix tạm, xác nhận managed stale file bị xóa và chạy warehouse sample/HTTP
  bằng bản đã cài trên Ubuntu/macOS/Windows trước khi upload artifact. Smoke macOS cục bộ đã
  xác nhận tarball tạm cài/update + build/feature/rollback/demo; release gate đa nền tảng chỉ
  đóng sau khi matrix CI thực tế xanh.
- [x] Hoàn thiện documentation 1.0: `docs/language-reference.md` khóa bề mặt ngôn ngữ;
  `docs/cli.md` khóa workflow CLI/tooling; `docs/debugging.md` mô tả diagnostic/AST/IR/stack trace;
  `docs/migration-1.0.md` + `CHANGELOG.md` ghi migration/release notes. Package/testing/install
  guide đã có tại `docs/package-system.md`, `docs/testing.md`, `docs/installation.md`; README liên
  kết bộ tài liệu và các sample trong `examples/` tiếp tục là ví dụ chạy thật.

## Mốc phát hành

### V++ 0.7 — Runtime / compiler cleanup

- [x] `CompilationContext` sở hữu state chính và concurrent compilation có regression.
- [x] Module lifecycle Phase 1 + runtime initialization dependency-first.
- [x] `VM::run()` đã tách thành opcode handlers có fixture test nội bộ.
- [x] Loại facade/global mutable state khỏi production path và khóa compiler re-entrant contract.

### V++ 0.8 — Object/runtime stability

- [x] Object model + `mình`/`gốc` + constructor.
- [x] Kế thừa class + interface/`triển khai`.
- [x] Tracing GC + cycle sweep.
- [x] GC/runtime stress + VM-state invariant + stack overflow/nested exception hardening.
- [x] Stack trace có source span/function/module.

### V++ 0.9 — Developer ecosystem

- [x] Package resolver + semver + lockfile + cache.
- [ ] Standard library audit/completeness cho 1.0.
- [x] Test framework nâng cấp.
- [x] Formatter/linter/LSP/VS Code integration hoàn thiện.
- [x] Project templates + sample project thực tế.

### V++ 1.0 RC

- [x] Freeze syntax và semantics: language reference + semantics/type/runtime-error contract đã
  được chốt; thay đổi breaking sau freeze phải có migration/version decision tường minh.
- [x] Freeze bytecode compatibility/versioning policy: V++ 1.0 không công bố serialized `.vbc`
  ABI; `Instruction`/`Opcode` trong bộ nhớ là implementation detail. Policy nằm tại ADR 0002.
- [x] Freeze package manifest/lockfile format: `vpp.json` schema 1 và `vpp.lock` schema 1 là
  public format 1.0; breaking format change phải dùng schema mới.
- [x] Freeze public CLI contract: tên canonical tiếng Việt + alias tương thích được khóa trong
  `docs/cli.md` và ADR 0002.
- [ ] Fuzz + stress + sanitizer/leak suite xanh.
- [ ] Release artifact và install smoke test xanh trên Windows/macOS/Linux. Workflow đã enforce
  install/update + stale-file check trên artifact trước upload; còn chờ matrix CI thực tế xanh.
- [ ] Chạy thành công sample project thực tế bằng artifact release, không dùng build tree.
  Local macOS tarball tạm đã dựng/chạy feature gate, rollback và demo từ prefix cài đặt; HTTP
  localhost bị sandbox local chặn `EPERM`, còn workflow release sẽ chạy full HTTP verify trên runner.
- [x] Documentation + migration notes + changelog hoàn chỉnh.

## Release gate 1.0

Không phát hành 1.0 nếu còn lỗi làm VM corrupt state, crash trên input hợp lệ, package install
không reproducible, semantics chưa có contract hoặc artifact release không qua smoke test trên
ba hệ điều hành mục tiêu.

Ước lượng hiện tại: còn khoảng **25–30% khối lượng tới 1.0**, nhưng phần còn lại tập trung vào
hardening, kiểm thử và đóng contract nên có độ khó cao hơn việc thêm cú pháp/tính năng mới.
