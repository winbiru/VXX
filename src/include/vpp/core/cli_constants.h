#pragma once

#include <string_view>

namespace vietvm::cli {

// Top-level CLI commands and aliases.
inline constexpr std::string_view kCmdHelp = "help";
inline constexpr std::string_view kCmdHelpShort = "-h";
inline constexpr std::string_view kCmdHelpLong = "--help";
inline constexpr std::string_view kCmdHelpVi = "giúp đỡ";
inline constexpr std::string_view kCmdCommands = "commands";
inline constexpr std::string_view kCmdVersion = "version";
inline constexpr std::string_view kCmdVersionShort = "-v";
inline constexpr std::string_view kCmdVersionLong = "--version";
inline constexpr std::string_view kCmdVersionVi = "phiên bản";
inline constexpr std::string_view kCmdDoctor = "doctor";
inline constexpr std::string_view kCmdDoctorVi = "bác sĩ";
inline constexpr std::string_view kCmdDiagnoseVi = "chẩn đoán";
inline constexpr std::string_view kCmdWhere = "where";
inline constexpr std::string_view kCmdWhereVi = "nơi";
inline constexpr std::string_view kCmdLsp = "--lsp";
inline constexpr std::string_view kCmdRepl = "--repl";
inline constexpr std::string_view kCmdRun = "run";
inline constexpr std::string_view kCmdRunVi = "chạy";
inline constexpr std::string_view kCmdTest = "test";
inline constexpr std::string_view kCmdTestVi = "kiểm thử";
inline constexpr std::string_view kCmdTestViHyphen = "kiểm-thử";
inline constexpr std::string_view kCmdBuild = "build";
inline constexpr std::string_view kCmdBuildVi = "dựng";
inline constexpr std::string_view kCmdDisassemble = "--disassemble";
inline constexpr std::string_view kCmdDisassembleVi = "--giải mã";
inline constexpr std::string_view kCmdDisassembleViHyphen = "--giải-mã";
inline constexpr std::string_view kCmdDumpAst = "--dump-ast";
inline constexpr std::string_view kCmdDumpIr = "--dump-ir";
inline constexpr std::string_view kCmdLint = "--lint";
inline constexpr std::string_view kCmdLintVi = "--soát lỗi";
inline constexpr std::string_view kCmdLintViHyphen = "--soát-lỗi";
inline constexpr std::string_view kCmdFormat = "--format";
inline constexpr std::string_view kCmdFormatVi = "--định dạng";
inline constexpr std::string_view kCmdFormatViHyphen = "--định-dạng";
inline constexpr std::string_view kCmdFormatInPlace = "--in-place";
inline constexpr std::string_view kCmdFormatInPlaceVi = "--ghi-tệp";
inline constexpr std::string_view kCmdFormatCheck = "--check";
inline constexpr std::string_view kCmdFormatCheckVi = "--kiểm-tra";
inline constexpr std::string_view kCmdPackage = "pkg";
inline constexpr std::string_view kCmdPackageVi = "gói";
inline constexpr std::string_view kCmdInit = "init";
inline constexpr std::string_view kCmdNew = "new";
inline constexpr std::string_view kCmdInitVi = "khởi tạo";

// Package subcommands and Vietnamese aliases.
inline constexpr std::string_view kPkgInit = "init";
inline constexpr std::string_view kPkgInitVi = "khởi tạo";
inline constexpr std::string_view kPkgAdd = "add";
inline constexpr std::string_view kPkgAddVi = "thêm";
inline constexpr std::string_view kPkgInstall = "install";
inline constexpr std::string_view kPkgInstallVi = "cài đặt";
inline constexpr std::string_view kPkgInstallAscii = "cai";
inline constexpr std::string_view kPkgInstallAsciiCompact = "caidat";
inline constexpr std::string_view kPkgPublish = "publish";
inline constexpr std::string_view kPkgPublishVi = "phát hành";
inline constexpr std::string_view kPkgPublishViHyphen = "phát-hành";
inline constexpr std::string_view kPkgList = "list";
inline constexpr std::string_view kPkgListVi = "danh sách";
inline constexpr std::string_view kPkgSync = "sync";
inline constexpr std::string_view kPkgSyncVi = "đồng bộ";
inline constexpr std::string_view kPkgUpdate = "update";
inline constexpr std::string_view kPkgUpdateVi = "cập nhật";
inline constexpr std::string_view kPkgLock = "lock";
inline constexpr std::string_view kPkgLockVi = "khóa";
inline constexpr std::string_view kPkgRestore = "restore";
inline constexpr std::string_view kPkgRestoreVi = "phục hồi";
inline constexpr std::string_view kPkgRemove = "remove";
inline constexpr std::string_view kPkgRemoveVi = "xóa";
inline constexpr std::string_view kPkgRemoveAscii = "xoa";
inline constexpr std::string_view kPkgInfo = "info";
inline constexpr std::string_view kPkgInfoVi = "thông tin";
inline constexpr std::string_view kPkgHas = "has";
inline constexpr std::string_view kPkgHasVi = "kiểm tra";
inline constexpr std::string_view kPkgStats = "stats";
inline constexpr std::string_view kPkgStatsVi = "thống kê";

// Uninstall command and managed installer assets.
inline constexpr std::string_view kUninstallWord1 = "gỡ";
inline constexpr std::string_view kUninstallWord2 = "cài";
inline constexpr std::string_view kUninstallWord3 = "đặt";
inline constexpr std::string_view kUninstallCommandVi = "gỡ cài đặt";
inline constexpr std::string_view kOfflineFlag = "--offline";
inline constexpr std::string_view kOfflineFlagVi = "--ngoại-tuyến";
inline constexpr std::string_view kApplication = "application";
inline constexpr std::string_view kApplicationShort = "app";
inline constexpr std::string_view kApplicationVi = "ứng dụng";
inline constexpr std::string_view kApplicationViHyphen = "ứng-dụng";
inline constexpr std::string_view kBackend = "backend";
inline constexpr std::string_view kUnixUninstallScript = "uninstall-vpp.sh";
inline constexpr std::wstring_view kWindowsUninstallScript = L"uninstall-vpp.ps1";
inline constexpr std::wstring_view kPowerShellRelativePath = L"WindowsPowerShell/v1.0/powershell.exe";

} // namespace vietvm::cli
