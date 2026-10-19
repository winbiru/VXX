#pragma once

namespace vietvm::constants {

inline constexpr int kHttpStatusOk = 200;

inline constexpr const char *kArgLabelPort = "cổng";
inline constexpr const char *kArgLabelServerId = "mã máy chủ";
inline constexpr const char *kArgLabelStatus = "trạng thái";

inline constexpr const char *kReqFieldMethod = "method";
inline constexpr const char *kReqFieldPath = "path";
inline constexpr const char *kReqFieldQuery = "query";
inline constexpr const char *kReqFieldBody = "body";
inline constexpr const char *kReqFieldHeader = "header";
inline constexpr const char *kEnvVppEnableGc = "VPP_ENABLE_GC";
inline constexpr const char *kEnvVietvmEnableGc = "VIETVM_ENABLE_GC";
inline constexpr const char *kEnvVppGcInterval = "VPP_GC_INTERVAL";
inline constexpr const char *kEnvVietvmGcInterval = "VIETVM_GC_INTERVAL";
inline constexpr const char *kEnvVppEnableJit = "VPP_ENABLE_JIT";
inline constexpr const char *kEnvVietvmEnableJit = "VIETVM_ENABLE_JIT";

inline constexpr int kDefaultGcInterval = 2048;

} // namespace vietvm::constants
