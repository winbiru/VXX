// utility.h
#pragma once

#include <string>
#include <algorithm>
#include <cctype>

class Utility {
public:

    // Loại bỏ khoảng trắng ở đầu
    static inline void ltrim(std::string &s) {
        s.erase(
            s.begin(),
            std::find_if(s.begin(), s.end(),
                         [](unsigned char ch){ return !std::isspace(ch); })
        );
    }

    // Loại bỏ khoảng trắng ở cuối
    static inline void rtrim(std::string &s) {
        s.erase(
            std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char ch){ return !std::isspace(ch); })
                .base(),
            s.end()
        );
    }

    // trim cả đầu và cuối
    static inline std::string trim(const std::string &s) {
        std::string copy = s;
        ltrim(copy);
        rtrim(copy);
        return copy;
    }
};
