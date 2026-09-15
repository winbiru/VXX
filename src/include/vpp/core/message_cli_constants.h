#pragma once

#include <string_view>

// Thông báo cho CLI, REPL, tooling và trình quản lý gói.
namespace vietvm::messages {

// Use positional placeholders ({0}, {1}, ...) for dynamic values.

// General CLI output and diagnostics.
inline constexpr std::string_view kCliUsage = "V++ CLI\n"
    "Cách dùng:\n"
    "  vpp giúp đỡ\n"
    "  vpp phiên bản\n"
    "  vpp chẩn đoán\n"
    "  vpp nơi\n"
    "  vpp thống kê\n"
    "  vpp <file.vi>\n"
    "  vpp chạy <file.vi>\n"
    "  vpp kiểm thử [<tệp-hoặc-thư-mục>]\n"
    "  vpp dựng <file.vi>\n"
    "  vpp --giải-mã <file.vi>\n"
    "  vpp --dump-ast <file.vi>\n"
    "  vpp --dump-ir <file.vi>\n"
    "  vpp --soát-lỗi <tệp-hoặc-thư-mục>\n"
    "  vpp --định-dạng <tệp-hoặc-thư-mục> [--ghi-tệp | --kiểm-tra]\n"
    "  vpp --repl\n"
    "  vpp khởi tạo [tên-dự-án]\n"
    "  vpp khởi tạo ứng dụng <tên-dự-án>\n"
    "  vpp khởi tạo backend <tên-dự-án>\n"
    "  vpp gói khởi tạo [tên]\n"
    "  vpp gói thêm <nguồn> [tên]\n"
    "  vpp gói cài đặt [<nguồn> [tên] | --ngoại-tuyến]\n"
    "  vpp gói phát hành <thư-mục-kho-đăng-ký>\n"
    "  vpp gói đồng bộ\n"
    "  vpp gói cập nhật\n"
    "  vpp gói khóa\n"
    "  vpp gói phục hồi [--ngoại-tuyến]\n"
    "  vpp gói xóa <tên>\n"
    "  vpp gói danh sách\n"
    "  vpp gói thông tin <tên>\n"
    "  vpp gói kiểm tra <tên>\n";

inline constexpr std::string_view kCliVersion = "Phiên bản V++ CLI: {0}\n";
inline constexpr std::string_view kCliTestRunning = "\nĐang chạy kiểm thử: {0}";
inline constexpr std::string_view kCliDoctorHeading = "Chẩn đoán V++ CLI\n";
inline constexpr std::string_view kCliDoctorVersion = "  phiên_bản: {0}\n";
inline constexpr std::string_view kCliDoctorExecutable = "  tệp_thực_thi: {0}\n";
inline constexpr std::string_view kCliDoctorCurrentDirectory = "  thư_mục_hiện_tại: {0}\n";
inline constexpr std::string_view kCliDoctorManifest = "  tệp_khai_báo: {0}\n";
inline constexpr std::string_view kCliDoctorPackageCount = "  số_gói: {0}\n";

inline constexpr std::string_view kCliFileOpenFailed = "Không thể mở tệp: {0}";
inline constexpr std::string_view kCliRunMissingFile = "chạy: thiếu đường dẫn tệp";
inline constexpr std::string_view kCliBuildMissingFile = "dựng: thiếu đường dẫn tệp";
inline constexpr std::string_view kCliBuildSucceeded =
    "Đã dựng thành công {0}: {1} lệnh bytecode, {2} hàm.\n";
inline constexpr std::string_view kCliTestPathMissing =
    "kiểm thử: không tìm thấy tệp hoặc thư mục: {0}";
inline constexpr std::string_view kCliTestNoFiles =
    "kiểm thử: không tìm thấy tệp .vi trong {0}";
inline constexpr std::string_view kCliTestSummary =
    "Kiểm thử hoàn tất: {0}/{1} tệp đạt.\n";
inline constexpr std::string_view kCliDisassembleMissingFile = "--giải-mã cần đường dẫn tệp";
inline constexpr std::string_view kCliLintMissingFile = "--soát-lỗi cần đường dẫn tệp hoặc thư mục";
inline constexpr std::string_view kCliFormatMissingFile = "--định-dạng cần đường dẫn tệp hoặc thư mục";
inline constexpr std::string_view kCliToolPathMissing = "Không tìm thấy tệp hoặc thư mục: {0}";
inline constexpr std::string_view kCliToolNoSourceFiles = "Không tìm thấy tệp .vi trong {0}";
inline constexpr std::string_view kCliLintDiagnostic = "{0}:{1}:{2}: {3}: {4}\n";
inline constexpr std::string_view kCliLintSummary = "Soát lỗi hoàn tất: {0} tệp, {1} lỗi, {2} cảnh báo.\n";
inline constexpr std::string_view kCliFormatChanged = "Chưa đúng định dạng: {0}\n";
inline constexpr std::string_view kCliFormatSummary = "Định dạng hoàn tất: {0} tệp, {1} tệp thay đổi.\n";
inline constexpr std::string_view kCliFormatDirectoryNeedsMode =
    "--định-dạng với thư mục cần --ghi-tệp hoặc --kiểm-tra";
inline constexpr std::string_view kCliFormatConflictingModes =
    "--định-dạng: không thể dùng đồng thời --ghi-tệp và --kiểm-tra";
inline constexpr std::string_view kCliFormatWriteFailed = "Không thể ghi tệp đã định dạng: {0}";
inline constexpr std::string_view kCliTestsDirectoryMissing = "Thư mục kiểm thử tests/ không tồn tại.";
inline constexpr std::string_view kCliDumpAstMissingFile = "--dump-ast cần đường dẫn tệp";
inline constexpr std::string_view kCliDumpIrMissingFile = "--dump-ir cần đường dẫn tệp";
inline constexpr std::string_view kCliUnhandledException = "Lỗi: {0}";

// REPL output and diagnostics.
inline constexpr std::string_view kReplWelcome = "V++ REPL. Nhập :thoát để thoát, :giúp để xem trợ giúp.\n";
inline constexpr std::string_view kReplPrompt = "vpp> ";
inline constexpr std::string_view kReplHelp = ":thoát, :giúp (tương thích: :quit, :exit, :help)\n";
inline constexpr std::string_view kReplExecutionFailed = "Lỗi: {0}";

// Tooling output and diagnostics.
inline constexpr std::string_view kToolLintPassed = "{0}: đạt\n";
inline constexpr std::string_view kToolLintFailed = "{0}: {1}";
inline constexpr std::string_view kToolSeverityError = "lỗi";
inline constexpr std::string_view kToolSeverityWarning = "cảnh báo";

// Package-manager information output.
inline constexpr std::string_view kPkgManifestCreated = "Đã tạo tệp khai báo tại {0}\n";
inline constexpr std::string_view kPkgApplicationCreated = "Đã tạo dự án ứng dụng tại {0}\n";
inline constexpr std::string_view kPkgBackendCreated = "Đã tạo dự án backend tại {0}\n";
inline constexpr std::string_view kPkgInstalled = "Đã cài gói: {0}\n";
inline constexpr std::string_view kPkgPublished = "Đã phát hành gói {0}@{1} vào kho đăng ký: {2}\n";
inline constexpr std::string_view kPkgSynced = "Đã đồng bộ {0} gói từ tệp khai báo.\n";
inline constexpr std::string_view kPkgListEmpty = "Chưa có gói nào được cài.\n";
inline constexpr std::string_view kPkgRemoved = "Đã xóa gói: {0}\n";
inline constexpr std::string_view kPkgLocked = "Đã tạo tệp khóa: {0}\n";
inline constexpr std::string_view kPkgRestored = "Đã phục hồi {0} gói từ tệp khóa.\n";
inline constexpr std::string_view kPkgInfoName = "tên: {0}\n";
inline constexpr std::string_view kPkgInfoPath = "đường_dẫn: {0}\n";
inline constexpr std::string_view kPkgInfoMainFile = "tệp_chính: {0}\n";
inline constexpr std::string_view kPkgValuePresent = "có";
inline constexpr std::string_view kPkgValueAbsent = "không";
inline constexpr std::string_view kPkgStatsProject = "dự_án: {0}\n";
inline constexpr std::string_view kPkgStatsManifest = "tệp_khai_báo: {0}\n";
inline constexpr std::string_view kPkgStatsCount = "số_gói: {0}\n";

// Package-manager diagnostics.
inline constexpr std::string_view kPkgBackendNameMissing = "khởi tạo backend: thiếu tên dự án";
inline constexpr std::string_view kPkgBackendDirectoryExists = "khởi tạo backend: thư mục đã tồn tại: {0}";
inline constexpr std::string_view kPkgBackendTemplatesMissing = "khởi tạo backend: không tìm thấy mẫu templates/backend";
inline constexpr std::string_view kPkgBackendTemplateCopyFailed = "khởi tạo backend: không thể sao chép mẫu {0}: {1}";
inline constexpr std::string_view kPkgApplicationNameMissing = "khởi tạo ứng dụng: thiếu tên dự án";
inline constexpr std::string_view kPkgApplicationDirectoryExists = "khởi tạo ứng dụng: thư mục đã tồn tại: {0}";
inline constexpr std::string_view kPkgApplicationTemplatesMissing = "khởi tạo ứng dụng: không tìm thấy mẫu templates/application";
inline constexpr std::string_view kPkgApplicationTemplateCopyFailed = "khởi tạo ứng dụng: không thể sao chép mẫu {0}: {1}";
inline constexpr std::string_view kPkgSourceNotFound = "Không tìm thấy nguồn: {0}";
inline constexpr std::string_view kPkgRemoveNameMissing = "gói xóa: thiếu tên gói";
inline constexpr std::string_view kPkgNotFound = "Không tìm thấy gói: {0}";
inline constexpr std::string_view kPkgRemoveFailed = "Không thể xóa gói: {0} ({1})";
inline constexpr std::string_view kPkgInfoNameMissing = "gói thông tin: thiếu tên gói";
inline constexpr std::string_view kPkgSubcommandMissing = "gói: thiếu lệnh con";
inline constexpr std::string_view kPkgAddSourceMissing = "gói thêm: thiếu đường dẫn nguồn";
inline constexpr std::string_view kPkgHasNameMissing = "gói kiểm tra: thiếu tên gói";
inline constexpr std::string_view kPkgInvalidSubcommand = "gói: lệnh con không hợp lệ: {0}";
inline constexpr std::string_view kPkgInstallSourceMissing = "cài đặt: thiếu đường dẫn nguồn";
inline constexpr std::string_view kPkgPublishRegistryMissing = "phát hành: thiếu thư mục kho đăng ký";
inline constexpr std::string_view kPkgLockManifestMissing = "khóa: không tìm thấy vpp.json";
inline constexpr std::string_view kPkgSyncManifestMissing = "đồng bộ: không tìm thấy vpp.json";
inline constexpr std::string_view kPkgRestoreLockMissing = "phục hồi: không tìm thấy vpp.lock";
inline constexpr std::string_view kPkgLockDependencyMissing = "khóa: gói phụ thuộc chưa được cài: {0}";
inline constexpr std::string_view kPkgLockVersionConflict = "khóa: phiên bản {1} của gói phụ thuộc '{0}' không thỏa khoảng phiên bản {2}";
inline constexpr std::string_view kPkgLockInstalledVersionMismatch = "khóa: gói '{0}' đang cài phiên bản {1}, bộ phân giải yêu cầu {2}; chạy 'vpp gói cập nhật' trước khi khóa";
inline constexpr std::string_view kPkgRestoreVersionMismatch = "phục hồi: gói phụ thuộc '{0}' có phiên bản nguồn {1}, tệp khóa yêu cầu {2}";
inline constexpr std::string_view kPkgOfflineCacheMissing = "phục hồi --ngoại-tuyến: bộ nhớ đệm không có gói '{0}' với dấu vân tay {1}";
inline constexpr std::string_view kPkgLockedPackageMissing = "vpp.lock yêu cầu gói '{0}' nhưng chưa được cài tại {1}; chạy 'vpp gói phục hồi'";
inline constexpr std::string_view kPkgLockedPackageChanged = "gói '{0}' không khớp vpp.lock (tệp khóa {1}, hiện tại {2}); chạy 'vpp gói phục hồi' để khôi phục hoặc 'vpp gói khóa' để chấp nhận thay đổi";
inline constexpr std::string_view kPkgTopLevelRemoveNameMissing = "xóa: thiếu tên gói";
inline constexpr std::string_view kPkgTopLevelInfoNameMissing = "thông tin: thiếu tên gói";
inline constexpr std::string_view kPkgTopLevelHasNameMissing = "kiểm tra: thiếu tên gói";

} // namespace vietvm::messages
