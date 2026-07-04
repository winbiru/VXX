#pragma once

#include <optional>
#include <string>

#include "vm/vm.h"

namespace vietvm::helpers {

bool hasEnvVar(const char *name);
std::optional<std::string> getEnvVar(const char *name);
bool startsWith(const std::string &value, const std::string &prefix);

std::string trimCopy(const std::string &s);
std::string argToRawString(const StackValue &v);
std::string decodeSimpleEscapes(const std::string &s);
std::string readPropertyByKey(const std::string &filePath,
                              const std::string &key,
                              const std::string &fallback);

bool parseIntArgFromStack(const StackValue &arg,
                          const std::string &fn,
                          const std::string &label,
                          int &out,
                          std::string &err);

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
