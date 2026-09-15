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
- [ ] Chạy ASan/UBSan/LSan hoặc công cụ tương đương cho stress suite; local AppleClang
  ASan+UBSan hiện xanh 16/16 sau khi sửa UB opcode và GC destructor-chain stack overflow.
  Ubuntu CI đã ép `detect_leaks=1`; cần một lượt CI Linux xanh để đóng leak gate.
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

- [ ] Audit UTF-8 end-to-end cho string/index/slice/length/case/normalization vì tiếng Việt là
  first-class syntax. Đã hoàn tất code-point semantics cho length/reverse/index/slice,
  validator malformed UTF-8, case conversion cho toàn bộ chữ cái tiếng Việt dựng sẵn và NFC
  composition cho tổ hợp nguyên âm + dấu tiếng Việt. Title/palindrome/anagram cũng dùng NFC +
  case tiếng Việt. Đếm/chứa/thay chuỗi cũng normalize NFC, và corpus NFD -> NFC đã phủ toàn bộ
  67 chữ thường tiếng Việt. Scope 1.0 không mở rộng Unicode tổng quát; phần text/string tiếng
  Việt của mục này đã đủ contract 1.0.
- [ ] Chuẩn hóa filesystem/path API đa nền tảng và edge case separator/encoding. Native
  boundary đã reject malformed UTF-8 cho cả path API, đọc/ghi/đếm tệp và đọc cấu hình, trả
  path bằng generic `/`, và regression khóa Unicode directory/file + join/parent/missing-path
  cùng file/config tiếng Việt + Unicode ngoài BMP; còn xác minh Windows/Linux và các edge case hệ điều hành
  trước khi đóng mục.
- [x] Hoàn thiện time/date API và timezone contract ở mức đủ dùng cho ứng dụng thực tế.
  `lấy thời gian hiện tại()` trả local ISO-8601 có UTC offset, `lấy thời gian utc()` trả UTC
  ISO-8601 hậu tố `Z`, còn `độ lệch múi giờ()` trả offset theo phút. Runtime dùng API chuyển
  `tm` thread-safe theo nền tảng và regression khóa format/offset/arity qua native + `.vi`.
- [ ] Bổ sung process API tối thiểu nếu permission model cho phép.
- [ ] Bổ sung crypto cơ bản bằng implementation đã được kiểm chứng: secure random, hash/HMAC;
  không tự viết primitive mật mã trong VM.
- [ ] Nâng package testing: assertion, expected error, setup/teardown và test discovery rõ ràng.
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
  `vpp-frontend-fuzz-unit` chạy corpus malformed cố định cùng 512 input sinh xác định từ
  seed `0x56505031`, yêu cầu mọi input hoặc parse thành công với ExprId/LambdaId arena liên
  tục hoặc bị từ chối bằng lỗi có kiểm soát; chạy lặp cùng seed phải sinh đúng cùng corpus.
- [x] Thêm malformed AST/invalid IR tests và bytecode verifier trước khi VM thực thi input
  không tin cậy. AST có ExprId ngoài arena/chu trình được hạ thành unsupported IR thay vì
  truy cập ngoài biên; direct codegen từ chối IR có operand/control-flow shape sai. Tầng
  `vpp-bytecode` kiểm tra opcode, jump/try target, StringPool reference, argument/capture count
  và function metadata cơ bản; `VM::run()` chạy verifier cho root + function bytecode trước
  module initialization, JIT và interpreter dispatch.
- [x] Khóa deterministic compilation/reproducible bytecode cho cùng source + dependency lock.
  `vpp-compiler-support-unit` biên dịch lặp lại cùng source + import graph và so khớp chính xác
  root/function bytecode, StringPool, function ID/name index, debug metadata, module initializer
  và module export. Package lock đã khóa exact dependency version/revision + fingerprint/bytes,
  nên cùng source + cùng lock dẫn tới cùng compiler input graph và bytecode tái lập.
- [x] Stress source/project lớn và theo dõi peak memory/compiler time để phát hiện regression.
  `vpp-compiler-stress-unit` khóa source 800 hàm, project 24 module × 24 hàm và 5 lần
  compile lặp trên cùng `CompilationContext`; test in `compile_ms` + peak RSS (khi hệ điều hành
  hỗ trợ), chặn runaway >30 giây/scenario và peak RSS >1 GiB, đồng thời xác nhận registry không
  tích lũy function/StringPool state giữa các lượt compile.

## 6. Toolchain và trải nghiệm phát triển

- [x] Đóng public CLI contract cho `new`, `run`, `test`, `build` và package commands.
  Giao diện canonical 1.0 dùng tiếng Việt: `khởi tạo`, `chạy`, `kiểm thử`, `dựng` và
  namespace `gói`; `new/run/test/build` cùng các tên package tiếng Anh chỉ là alias tương thích.
  `kiểm thử` quét `.vi` theo thứ tự xác định và trả summary; `dựng` biên dịch đầy đủ nhưng chưa
  ghi artifact `.vbc` trước khi format bytecode được freeze. `vpp-cli-contract-unit` khóa help,
  alias compatibility, build-only, test discovery, package namespace và `chẩn đoán`.
- [x] Hoàn thiện formatter và linter đủ ổn định để dùng trong CI/editor. Formatter giữ comment
  + literal, bảo toàn compiler token stream, idempotent và có `--kiểm-tra`/`--ghi-tệp` cho
  file hoặc thư mục. Linter trả diagnostic có cấu trúc gồm severity + source span, CLI
  `--soát-lỗi` xuất `tệp:dòng:cột` và exit code phù hợp CI, còn LSP dùng cùng diagnostic range.
  `vpp-tooling-unit` và `vpp-cli-contract-unit` khóa comment/UTF-8/spacing/idempotence,
  lexer/parser location, import tương đối và directory workflow.
- [x] Nâng LSP: diagnostic, completion, go-to-definition, hover và rename dựa trên semantic model.
  Completion/definition/hover/rename dùng symbol/binding từ semantic model; rename chỉ sửa
  declaration + reference cùng `SymbolId`, range LSP dùng UTF-16 và declaration được thu hẹp
  đúng token tên thay vì toàn statement. `vpp-lsp-semantic-unit` khóa capability, diagnostic,
  completion, definition, hover, rename, tên rename không hợp lệ và range identifier tiếng Việt.
- [x] Đồng bộ VS Code extension với LSP/toolchain 1.0 và syntax mới.
  Extension tự khởi động `vpp --lsp` khi mở tài liệu V++, đồng bộ full-document change và
  nối diagnostic/completion/definition/hover/rename/format vào VS Code mà không cần dependency
  npm ngoài. Có cấu hình bật/tắt + executable tùy chọn, command restart LSP, tự ưu tiên VM do
  extension quản lý rồi PATH. TextMate grammar đã bổ sung `giao diện`, `kế thừa`, `triển khai`,
  `mình` và `gốc`; manifest/JS/grammar đều qua kiểm tra cú pháp.
- [x] Bổ sung project templates và ít nhất một sample project thực tế ngoài regression corpus.
  `vpp khởi tạo ứng dụng <tên>` tạo project schema 1 với `src/`, `tests/`, `gói/`, README
  và `.gitignore`, đồng thời giữ nguyên contract cũ của `khởi tạo <tên>`. Sample
  `examples/hoa-don-cua-hang` dùng module, class/constructor, import tương đối và smoke test;
  `vpp-sample-project-unit` khóa cả dựng, chạy và kiểm thử sample.
- [x] Hoàn thiện installer/update path cho Windows, macOS và Linux; đánh giá Homebrew/winget
  sau khi install contract ổn định. Installer Unix/Windows dùng staged update và thay toàn bộ
  `gói/`, `templates/`, `examples` để không giữ file stale; có chế độ CI không sửa profile/PATH.
  Release workflow chạy install smoke trên Ubuntu/macOS/Windows trước khi upload artifact,
  gồm cài lại cùng prefix, tạo/dựng/chạy/test template và dựng/test sample thực tế. Smoke macOS
  cục bộ đã xanh; release gate đa nền tảng vẫn chỉ đóng sau khi matrix CI thực tế xanh.
- [ ] Hoàn thiện documentation 1.0: language reference, package guide, CLI guide, debugging,
  migration/changelog và examples.

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
- [ ] Test framework nâng cấp.
- [x] Formatter/linter/LSP/VS Code integration hoàn thiện.
- [x] Project templates + sample project thực tế.

### V++ 1.0 RC

- [ ] Freeze syntax và semantics.
- [ ] Freeze bytecode compatibility/versioning policy.
- [ ] Freeze package manifest/lockfile format.
- [ ] Freeze public CLI contract.
- [ ] Fuzz + stress + sanitizer/leak suite xanh.
- [ ] Release artifact và install smoke test xanh trên Windows/macOS/Linux.
- [ ] Chạy thành công sample project thực tế bằng artifact release, không dùng build tree.
- [ ] Documentation + migration notes + changelog hoàn chỉnh.

## Release gate 1.0

Không phát hành 1.0 nếu còn lỗi làm VM corrupt state, crash trên input hợp lệ, package install
không reproducible, semantics chưa có contract hoặc artifact release không qua smoke test trên
ba hệ điều hành mục tiêu.

Ước lượng hiện tại: còn khoảng **25–30% khối lượng tới 1.0**, nhưng phần còn lại tập trung vào
hardening, kiểm thử và đóng contract nên có độ khó cao hơn việc thêm cú pháp/tính năng mới.
