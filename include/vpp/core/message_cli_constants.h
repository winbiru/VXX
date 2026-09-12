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
    "  vpp bác sĩ\n"
    "  vpp nơi\n"
    "  vpp thống kê\n"
    "  vpp <file.vi>\n"
    "  vpp chạy <file.vi>\n"
    "  vpp --giải-mã <file.vi>\n"
    "  vpp --dump-ast <file.vi>\n"
    "  vpp --dump-ir <file.vi>\n"
    "  vpp --lint <file.vi>\n"
    "  vpp --định-dạng <file.vi> [--in-place]\n"
    "  vpp --repl\n"
    "  vpp khởi tạo [tên-dự-án]\n"
    "  vpp khởi tạo backend <tên-dự-án>\n"
    "  vpp cài đặt <nguồn> [tên]\n"
    "  vpp danh sách\n"
    "  vpp thông tin <tên>\n"
    "  vpp kiểm tra <tên>\n"
    "  vpp gói khởi tạo [tên]\n"
    "  vpp gói thêm <nguồn> [tên]\n"
    "  vpp gói xóa <tên>\n"
    "  vpp gói danh sách\n"
    "  vpp gói thông tin <tên>\n"
    "  vpp gói kiểm tra <tên>\n";

inline constexpr std::string_view kCliVersion = "Phiên bản V++ CLI: {0}\n";
inline constexpr std::string_view kCliTestRunning = "\n🔹 Đang chạy kiểm thử: {0}";
inline constexpr std::string_view kCliDoctorHeading = "Chẩn đoán V++ CLI\n";
inline constexpr std::string_view kCliDoctorVersion = "  phiên_bản: {0}\n";
inline constexpr std::string_view kCliDoctorExecutable = "  tệp_thực_thi: {0}\n";
inline constexpr std::string_view kCliDoctorCurrentDirectory = "  thư_mục_hiện_tại: {0}\n";
inline constexpr std::string_view kCliDoctorManifest = "  tệp_khai_báo: {0}\n";
inline constexpr std::string_view kCliDoctorPackageCount = "  số_gói: {0}\n";

inline constexpr std::string_view kCliFileOpenFailed = "Không thể mở tệp: {0}";
inline constexpr std::string_view kCliRunMissingFile = "chạy: thiếu đường dẫn tệp";
inline constexpr std::string_view kCliDisassembleMissingFile = "--giải-mã cần đường dẫn tệp";
inline constexpr std::string_view kCliLintMissingFile = "--lint cần đường dẫn tệp";
inline constexpr std::string_view kCliFormatMissingFile = "--định-dạng cần đường dẫn tệp";
inline constexpr std::string_view kCliTestsDirectoryMissing = "Thư mục kiểm thử tests/ không tồn tại.";
inline constexpr std::string_view kCliDumpAstMissingFile = "--dump-ast cần đường dẫn tệp";
inline constexpr std::string_view kCliDumpIrMissingFile = "--dump-ir cần đường dẫn tệp";
inline constexpr std::string_view kCliUnhandledException = "Lỗi: {0}";

// REPL output and diagnostics.
inline constexpr std::string_view kReplWelcome = "V++ REPL. Nhập :quit hoặc :exit để thoát.\n";
inline constexpr std::string_view kReplPrompt = "vpp> ";
inline constexpr std::string_view kReplHelp = ":quit, :exit, :help\n";
inline constexpr std::string_view kReplExecutionFailed = "Lỗi: {0}";

// Tooling output and diagnostics.
inline constexpr std::string_view kToolLintPassed = "{0}: OK\n";
inline constexpr std::string_view kToolLintFailed = "{0}: {1}";

// Package-manager information output.
inline constexpr std::string_view kPkgManifestCreated = "Đã tạo tệp khai báo tại {0}\n";
inline constexpr std::string_view kPkgBackendCreated = "Đã tạo dự án backend tại {0}\n";
inline constexpr std::string_view kPkgInstalled = "Đã cài gói: {0}\n";
inline constexpr std::string_view kPkgListEmpty = "Chưa có gói nào được cài.\n";
inline constexpr std::string_view kPkgRemoved = "Đã xóa gói: {0}\n";
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
inline constexpr std::string_view kPkgTopLevelRemoveNameMissing = "xóa: thiếu tên gói";
inline constexpr std::string_view kPkgTopLevelInfoNameMissing = "thông tin: thiếu tên gói";
inline constexpr std::string_view kPkgTopLevelHasNameMissing = "kiểm tra: thiếu tên gói";

} // namespace vietvm::messages
