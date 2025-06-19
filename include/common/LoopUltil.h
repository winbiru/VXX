#pragma once
#include "Utility.h"
#include <vector>
#include <string>

static std::vector<std::string> splitLoopParts(const std::string& s) {
    std::vector<std::string> parts;
    std::string current;
    int parenDepth = 0;
    for (char c : s) {
        if (c == '(') ++parenDepth;
        else if (c == ')') --parenDepth;

        if (c == ';' && parenDepth == 0) {
            parts.push_back(Utility::trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(Utility::trim(current));
    }
    return parts;
}
