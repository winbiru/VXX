#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "frontend/lexer.h"
#include "vpp/frontend/parser.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<std::string> fixedMalformedCorpus() {
    return {
        "hàm main( {",
        "hàm main() { trả về 1;",
        "in \"chuỗi chưa đóng",
        "/* chú thích chưa đóng",
        "nếu (đúng { in 1; }",
        "lặp (i = 0; i < 10; i++ { in i; }",
        "chọn (x) { ca 1 in 1; }",
        "thử { ném \"lỗi\"; } bắt lỗi (",
        "lớp A kế thừa { }",
        "giao diện I { hàm chạy( ; }",
        "[1, 2, 3",
        "{\"khóa\": 1",
        "a[[[0];",
        "))))))))",
        "}}}}}}}}",
        "hàm f(a = ) { trả về a; }",
        "công khai nhập \"module.vi",
        std::string("in \"") + static_cast<char>(0xc0) + static_cast<char>(0xaf) + "\";",
        std::string("tên") + '\0' + "biến = 1;",
    };
}

std::vector<std::string> generateSeededCorpus(std::uint32_t seed,
                                              std::size_t count) {
    static const std::vector<std::string> fragments = {
        "hàm", "main", "nếu", "nếu không", "lặp", "chọn", "ca", "mặc định",
        "trả về", "ném", "thử", "bắt lỗi", "lớp", "giao diện", "triển khai",
        "nhập", "công khai", "mình", "gốc", "đúng", "sai", "rỗng",
        "tên", "danh_sách", "giá_trị", "𠀀",
        "(", ")", "{", "}", "[", "]", ";", ",", ":", ".",
        "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "&&", "||",
        "\"", "'", "//", "/*", "*/", "\n", "\t", "0", "1", "1.5",
    };

    std::mt19937 random(seed);
    std::uniform_int_distribution<std::size_t> fragmentCount(1, 40);
    std::uniform_int_distribution<std::size_t> fragmentIndex(0, fragments.size() - 1);
    std::uniform_int_distribution<int> separator(0, 4);

    std::vector<std::string> corpus;
    corpus.reserve(count);
    for (std::size_t caseIndex = 0; caseIndex < count; ++caseIndex) {
        std::string source;
        const std::size_t parts = fragmentCount(random);
        for (std::size_t part = 0; part < parts; ++part) {
            if (!source.empty()) {
                const int kind = separator(random);
                if (kind == 0) source.push_back(' ');
                else if (kind == 1) source.push_back('\n');
            }
            source += fragments[fragmentIndex(random)];
        }
        corpus.push_back(std::move(source));
    }
    return corpus;
}

void exerciseSource(const std::string &source,
                    std::size_t &accepted,
                    std::size_t &rejected) {
    try {
        const auto raw = vietvm::compiler::tokenizeWithSpans(source);
        const auto tokens = vietvm::compiler::postProcessTokensWithSpans(raw);
        const auto program = vietvm::frontend::parseTokens(tokens);

        for (std::size_t index = 0; index < program.expressions.size(); ++index) {
            expect(program.expressions[index].id == index,
                   "parser giữ ExprId liên tục trong arena sau fuzz input");
        }
        for (std::size_t index = 0; index < program.lambdas.size(); ++index) {
            expect(program.lambdas[index].id == index,
                   "parser giữ LambdaId liên tục trong arena sau fuzz input");
        }
        ++accepted;
    } catch (const std::runtime_error &) {
        ++rejected;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: frontend ném exception ngoài contract: " << error.what() << '\n';
        ++failures;
    } catch (...) {
        std::cerr << "FAIL: frontend ném exception không xác định\n";
        ++failures;
    }
}

void testFixedSeedLexerParserFuzz() {
    constexpr std::uint32_t kSeed = 0x56505031u;
    constexpr std::size_t kGeneratedCases = 512;

    const auto first = generateSeededCorpus(kSeed, kGeneratedCases);
    const auto second = generateSeededCorpus(kSeed, kGeneratedCases);
    expect(first == second,
           "fuzz corpus sinh lại chính xác với cùng seed cố định");

    std::vector<std::string> corpus = fixedMalformedCorpus();
    corpus.insert(corpus.end(), first.begin(), first.end());

    std::size_t accepted = 0;
    std::size_t rejected = 0;
    for (const std::string &source : corpus) {
        exerciseSource(source, accepted, rejected);
    }

    expect(accepted > 0,
           "fuzz corpus có ít nhất một input được parser chấp nhận");
    expect(rejected > 0,
           "fuzz corpus có ít nhất một input bị frontend từ chối có kiểm soát");
    expect(accepted + rejected == corpus.size(),
           "mọi fuzz input đều kết thúc bằng parse thành công hoặc lỗi có kiểm soát");
}

} // namespace

int main() {
    testFixedSeedLexerParserFuzz();

    if (failures != 0) {
        std::cerr << failures << " kiểm tra fuzz frontend thất bại\n";
        return 1;
    }
    std::cout << "frontend fuzz seed cố định: đạt\n";
    return 0;
}
