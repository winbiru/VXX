#pragma once
#include <string>
#include <vector>

namespace vietvm::compiler {

    class StringPool {
    public:
        static int storeString(const std::string& s);
        static const std::string& getString(int idx);
        static size_t size() noexcept;
        static const std::vector<std::string>& getPool();
        static void clear();
    private:
        static std::vector<std::string> pool_;
    };

} // namespace vietvm::compiler