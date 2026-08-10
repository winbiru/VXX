#pragma once

#include "vpp/core/message_constants.h"

namespace vietvm::messages {

// CLI/REPL and package-manager messages: VPP-CLI, VPP-REPL, VPP-PKG, VPP-TOOL.
// Use positional placeholders ({0}, {1}, ...) for dynamic values.

// General CLI output and diagnostics.
inline constexpr MessageDefinition kCliUsage = {
    "VPP-CLI-0001",
    "V++ CLI\n"
    "Cách dùng:\n"
    "  vpp giúp đỡ\n"
    "  vpp phiên bản\n"
    "  vpp bác sĩ\n"
    "  vpp nơi\n"
    "  vpp thống kê\n"
    "  vpp <file.vi>\n"
    "  vpp chạy <file.vi>\n"
    "  vpp --giải-mã <file.vi>\n"
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
    "  vpp gói kiểm tra <tên>\n"};

inline constexpr MessageDefinition kCliVersion = {
    "VPP-CLI-0002", "Phiên bản V++ CLI: {0}\n"};
inline constexpr MessageDefinition kCliTestRunning = {
    "VPP-CLI-0003", "\n🔹 Đang chạy test: {0}"};
inline constexpr MessageDefinition kCliDoctorHeading = {
    "VPP-CLI-0004", "Chan doan V++ CLI\n"};
inline constexpr MessageDefinition kCliDoctorVersion = {
    "VPP-CLI-0005", "  phien_ban: {0}\n"};
inline constexpr MessageDefinition kCliDoctorExecutable = {
    "VPP-CLI-0006", "  tep_thuc_thi: {0}\n"};
inline constexpr MessageDefinition kCliDoctorCurrentDirectory = {
    "VPP-CLI-0007", "  thu_muc_hien_tai: {0}\n"};
inline constexpr MessageDefinition kCliDoctorManifest = {
    "VPP-CLI-0008", "  manifest: {0}\n"};
inline constexpr MessageDefinition kCliDoctorPackageCount = {
    "VPP-CLI-0009", "  so_goi: {0}\n"};

inline constexpr MessageDefinition kCliFileOpenFailed = {
    "VPP-CLI-1001", "Không thể mở file: {0}"};
inline constexpr MessageDefinition kCliRunMissingFile = {
    "VPP-CLI-1002", "chay: thieu duong dan tep"};
inline constexpr MessageDefinition kCliDisassembleMissingFile = {
    "VPP-CLI-1003", "--giai-ma can duong dan tep"};
inline constexpr MessageDefinition kCliLintMissingFile = {
    "VPP-CLI-1004", "--lint can duong dan tep"};
inline constexpr MessageDefinition kCliFormatMissingFile = {
    "VPP-CLI-1005", "--dinh-dang can duong dan tep"};
inline constexpr MessageDefinition kCliTestsDirectoryMissing = {
    "VPP-CLI-1006", "Thư mục tests/ không tồn tại."};
inline constexpr MessageDefinition kCliUnhandledException = {
    "VPP-CLI-1099", "Lỗi: {0}"};

// REPL output and diagnostics.
inline constexpr MessageDefinition kReplWelcome = {
    "VPP-REPL-0001", "V++ REPL. Nhập :quit để thoát.\n"};
inline constexpr MessageDefinition kReplPrompt = {
    "VPP-REPL-0002", "vpp> "};
inline constexpr MessageDefinition kReplHelp = {
    "VPP-REPL-0003", ":quit, :exit, :help\n"};
inline constexpr MessageDefinition kReplExecutionFailed = {
    "VPP-REPL-1001", "Lỗi: {0}"};

// Tooling output and diagnostics.
inline constexpr MessageDefinition kToolLintPassed = {
    "VPP-TOOL-0001", "{0}: OK\n"};
inline constexpr MessageDefinition kToolLintFailed = {
    "VPP-TOOL-1001", "{0}: {1}"};

// Package-manager information output.
inline constexpr MessageDefinition kPkgManifestCreated = {
    "VPP-PKG-0001", "Da tao manifest tai {0}\n"};
inline constexpr MessageDefinition kPkgBackendCreated = {
    "VPP-PKG-0002", "Da tao backend project tai {0}\n"};
inline constexpr MessageDefinition kPkgInstalled = {
    "VPP-PKG-0003", "Da cai goi: {0}\n"};
inline constexpr MessageDefinition kPkgListEmpty = {
    "VPP-PKG-0004", "Chua co goi nao duoc cai.\n"};
inline constexpr MessageDefinition kPkgRemoved = {
    "VPP-PKG-0005", "Da xoa goi: {0}\n"};
inline constexpr MessageDefinition kPkgInfoName = {
    "VPP-PKG-0006", "ten: {0}\n"};
inline constexpr MessageDefinition kPkgInfoPath = {
    "VPP-PKG-0007", "duong_dan: {0}\n"};
inline constexpr MessageDefinition kPkgInfoMainFile = {
    "VPP-PKG-0008", "tep_chinh: {0}\n"};
inline constexpr MessageDefinition kPkgValuePresent = {
    "VPP-PKG-0009", "co"};
inline constexpr MessageDefinition kPkgValueAbsent = {
    "VPP-PKG-0010", "khong"};
inline constexpr MessageDefinition kPkgStatsProject = {
    "VPP-PKG-0011", "du_an: {0}\n"};
inline constexpr MessageDefinition kPkgStatsManifest = {
    "VPP-PKG-0012", "manifest: {0}\n"};
inline constexpr MessageDefinition kPkgStatsCount = {
    "VPP-PKG-0013", "so_goi: {0}\n"};

// Package-manager diagnostics.
inline constexpr MessageDefinition kPkgBackendNameMissing = {
    "VPP-PKG-1001", "backend init: thieu ten du an"};
inline constexpr MessageDefinition kPkgBackendDirectoryExists = {
    "VPP-PKG-1002", "backend init: thu muc da ton tai: {0}"};
inline constexpr MessageDefinition kPkgBackendTemplatesMissing = {
    "VPP-PKG-1003", "backend init: khong tim thay templates/backend"};
inline constexpr MessageDefinition kPkgBackendTemplateCopyFailed = {
    "VPP-PKG-1004", "backend init: khong the copy template {0}: {1}"};
inline constexpr MessageDefinition kPkgSourceNotFound = {
    "VPP-PKG-1005", "Khong tim thay nguon: {0}"};
inline constexpr MessageDefinition kPkgRemoveNameMissing = {
    "VPP-PKG-1006", "pkg xoa: thieu ten goi"};
inline constexpr MessageDefinition kPkgNotFound = {
    "VPP-PKG-1007", "Khong tim thay goi: {0}"};
inline constexpr MessageDefinition kPkgRemoveFailed = {
    "VPP-PKG-1008", "Khong the xoa goi: {0} ({1})"};
inline constexpr MessageDefinition kPkgInfoNameMissing = {
    "VPP-PKG-1009", "pkg thong tin: thieu ten goi"};
inline constexpr MessageDefinition kPkgSubcommandMissing = {
    "VPP-PKG-1010", "pkg: thieu lenh con"};
inline constexpr MessageDefinition kPkgAddSourceMissing = {
    "VPP-PKG-1011", "pkg them: thieu duong dan nguon"};
inline constexpr MessageDefinition kPkgHasNameMissing = {
    "VPP-PKG-1012", "pkg kiem tra: thieu ten goi"};
inline constexpr MessageDefinition kPkgInvalidSubcommand = {
    "VPP-PKG-1013", "pkg: lenh con khong hop le: {0}"};
inline constexpr MessageDefinition kPkgInstallSourceMissing = {
    "VPP-PKG-1014", "caidat: thieu duong dan nguon"};
inline constexpr MessageDefinition kPkgTopLevelRemoveNameMissing = {
    "VPP-PKG-1015", "xoa: thieu ten goi"};
inline constexpr MessageDefinition kPkgTopLevelInfoNameMissing = {
    "VPP-PKG-1016", "thong tin: thieu ten goi"};
inline constexpr MessageDefinition kPkgTopLevelHasNameMissing = {
    "VPP-PKG-1017", "kiem tra: thieu ten goi"};

} // namespace vietvm::messages
