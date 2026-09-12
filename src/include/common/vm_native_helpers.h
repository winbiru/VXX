#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "vpp/runtime/value.h"

namespace vietvm::helpers {

bool hasEnvVar(const char *name);
std::optional<std::string> getEnvVar(const char *name);
bool startsWith(const std::string &value, const std::string &prefix);

std::string trimCopy(const std::string &s);
std::string argToRawString(const StackValue &v);
std::string decodeSimpleEscapes(const std::string &s);
std::optional<std::pair<std::string, std::string>> parsePropertyAssignment(
    const std::string &line);
std::string readPropertyByKey(const std::string &filePath,
                              const std::string &key,
                              const std::string &fallback);

bool parseIntArgFromStack(const StackValue &arg,
                          const std::string &fn,
                          const std::string &label,
                          int &out,
                          std::string &err);

// Shared native-stdlib validation and value helpers.  These deliberately
// operate on StackValue rather than VM internals, so every native hook uses
// the same collection, comparison, and ASCII-text contract.
std::string nativeArgumentCountError(const std::string &fn, int expectedCount);
// Returns true only when the native call has exactly expectedCount arguments.
// On mismatch it leaves the standard, stable arity diagnostic in err.
bool requireNativeArgumentCount(const std::vector<StackValue> &args,
                                const std::string &fn,
                                int expectedCount,
                                std::string &err);

bool getFirstListArgument(const std::vector<StackValue> &args,
                          const std::string &fn,
                          ListHandle &out,
                          std::string &err);
bool getListArgument(const std::vector<StackValue> &args,
                     std::size_t index,
                     const std::string &fn,
                     ListHandle &out,
                     std::string &err);
bool getFirstMapArgument(const std::vector<StackValue> &args,
                         const std::string &fn,
                         MapHandle &out,
                         std::string &err);
bool getNonNegativeListIndex(const StackValue &value, int &index, std::string &err);

bool runDbConnect(const std::string &driverClass,
                  const std::string &jdbcUrl,
                  const std::string &user,
                  const std::string &password,
                  StackValue &result,
                  std::string &err);

bool runDbQuery(const std::string &driverClass,
                const std::string &jdbcUrl,
                const std::string &user,
                const std::string &password,
                const std::string &sql,
                StackValue &result,
                std::string &err);

bool runCurlHttpRequest(const std::string &method,
                        const std::string &fnName,
                        const std::string &url,
                        const std::optional<std::string> &payload,
                        StackValue &result,
                        std::string &err);

} // namespace vietvm::helpers
