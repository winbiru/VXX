#pragma once

#include <string>

namespace vietvm::helpers {

std::string extractSimpleJsonStringField(const std::string &body, const std::string &key);
bool splitPathAndQuery(const std::string &target, std::string &path, std::string &query);
std::string queryParam(const std::string &query, const std::string &key);

} // namespace vietvm::helpers
