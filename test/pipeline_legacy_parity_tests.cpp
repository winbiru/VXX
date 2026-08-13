#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/storeString.h"
#include "frontend/keywords.h"
#include "vpp/compiler/compiler.h"
#include "vpp/compiler/pipeline.h"

namespace {

namespace fs = std::filesystem;

struct CompilerSnapshot {
    vietvm::compiler::BytecodeBackend backend =
        vietvm::compiler::BytecodeBackend::LegacyTokenBridge;
    std::vector<Instruction> rootBytecode;
    std::vector<std::string> stringPool;
    std::unordered_map<int, std::vector<Instruction>> functionBytecode;
    std::unordered_map<int, int> functionNameIndices;
};

class CurrentPathGuard {
public:
    CurrentPathGuard() : saved_(fs::current_path()) {}
    ~CurrentPathGuard() {
        try {
            fs::current_path(saved_);
        } catch (...) {
        }
    }

    CurrentPathGuard(const CurrentPathGuard &) = delete;
    CurrentPathGuard &operator=(const CurrentPathGuard &) = delete;

private:
    fs::path saved_;
};

// FNV-1a is used only as a deterministic snapshot fingerprint. The input is a
// canonical, length-delimited serialization, so map iteration order and host
// byte order cannot change the result.
class StableHasher {
public:
    void addUnsigned(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            addByte(static_cast<unsigned char>((value >> shift) & 0xffU));
        }
    }

    void addSigned(int value) {
        addUnsigned(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
    }

    void addString(const std::string &value) {
        addUnsigned(value.size());
        for (unsigned char byte : value) addByte(byte);
    }

    std::uint64_t value() const noexcept { return value_; }

private:
    void addByte(unsigned char byte) {
        value_ ^= byte;
        value_ *= 1099511628211ULL;
    }

    std::uint64_t value_ = 14695981039346656037ULL;
};

std::string readSource(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("cannot open source file: " + path.u8string());
    }
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

void hashInstruction(StableHasher &hasher, const Instruction &instruction) {
    hasher.addSigned(static_cast<int>(instruction.op));
    hasher.addSigned(instruction.operand);
    hasher.addSigned(instruction.operandIndex);
    hasher.addSigned(instruction.operandValue);
}

void hashBytecode(StableHasher &hasher,
                  const std::vector<Instruction> &bytecode) {
    hasher.addUnsigned(bytecode.size());
    for (const Instruction &instruction : bytecode) {
        hashInstruction(hasher, instruction);
    }
}

std::uint64_t hashSnapshot(const CompilerSnapshot &snapshot) {
    StableHasher hasher;
    hasher.addString("vpp-legacy-compiler-snapshot-v1");
    hashBytecode(hasher, snapshot.rootBytecode);

    hasher.addUnsigned(snapshot.stringPool.size());
    for (const std::string &entry : snapshot.stringPool) {
        hasher.addString(entry);
    }

    std::vector<int> functionIds;
    functionIds.reserve(snapshot.functionBytecode.size());
    for (const auto &entry : snapshot.functionBytecode) {
        functionIds.push_back(entry.first);
    }
    std::sort(functionIds.begin(), functionIds.end());
    hasher.addUnsigned(functionIds.size());
    for (int functionId : functionIds) {
        hasher.addSigned(functionId);
        hashBytecode(hasher, snapshot.functionBytecode.at(functionId));
    }

    std::vector<int> namedFunctionIds;
    namedFunctionIds.reserve(snapshot.functionNameIndices.size());
    for (const auto &entry : snapshot.functionNameIndices) {
        namedFunctionIds.push_back(entry.first);
    }
    std::sort(namedFunctionIds.begin(), namedFunctionIds.end());
    hasher.addUnsigned(namedFunctionIds.size());
    for (int functionId : namedFunctionIds) {
        hasher.addSigned(functionId);
        hasher.addSigned(snapshot.functionNameIndices.at(functionId));
    }
    return hasher.value();
}

std::string formatHash(std::uint64_t hash) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::unordered_map<std::string, std::uint64_t> readExpectedSnapshots(
    const fs::path &manifestPath) {
    std::ifstream input(manifestPath);
    if (!input.is_open()) {
        throw std::runtime_error(
            "cannot open legacy snapshot manifest: " + manifestPath.u8string());
    }

    std::unordered_map<std::string, std::uint64_t> expected;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;

        const std::size_t separator = line.find('\t');
        if (separator == std::string::npos || separator == 0 ||
            separator + 1 >= line.size()) {
            throw std::runtime_error(
                "invalid legacy snapshot manifest line " +
                std::to_string(lineNumber));
        }

        const std::string path = line.substr(0, separator);
        const std::string encodedHash = line.substr(separator + 1);
        std::size_t parsed = 0;
        const std::uint64_t hash = std::stoull(encodedHash, &parsed, 16);
        if (encodedHash.size() != 16 || parsed != encodedHash.size()) {
            throw std::runtime_error(
                "invalid legacy snapshot hash on line " +
                std::to_string(lineNumber));
        }
        if (!expected.emplace(path, hash).second) {
            throw std::runtime_error(
                "duplicate legacy snapshot path: " + path);
        }
    }
    return expected;
}

CompilerSnapshot capturePipeline(const std::string &source) {
    vietvm::compiler::resetCompilationState();
    CompilerSnapshot snapshot;
    vietvm::compiler::CompilationArtifacts artifacts =
        vietvm::compiler::compilePipeline(source, keywordMap, true);
    snapshot.backend = artifacts.backend;
    snapshot.rootBytecode = std::move(artifacts.bytecode);
    snapshot.stringPool = vietvm::compiler::StringPool::getPool();
    snapshot.functionBytecode = vietvm::compiler::hamMap::hamBytecodeMap;
    snapshot.functionNameIndices = vietvm::compiler::hamMap::hamNameIndexMap;
    return snapshot;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: vpp-pipeline-legacy-parity <repository-root> "
                     "<snapshot-manifest>\n";
        return 1;
    }

    const fs::path repositoryRoot = fs::path(argv[1]);
    const fs::path sourceRoot = repositoryRoot / "src" / "tests";
    const fs::path manifestPath = fs::path(argv[2]);

    std::unordered_map<std::string, std::uint64_t> expected;
    try {
        expected = readExpectedSnapshots(manifestPath);
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }

    std::vector<fs::path> testFiles;
    for (const fs::directory_entry &entry :
         fs::recursive_directory_iterator(sourceRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vi") {
            testFiles.push_back(entry.path());
        }
    }
    std::sort(testFiles.begin(), testFiles.end());

    if (testFiles.empty()) {
        std::cerr << "FAIL: no .vi regression programs found under "
                  << sourceRoot.u8string() << '\n';
        return 1;
    }

    int failures = 0;
    std::size_t directPrograms = 0;
    std::unordered_set<std::string> visited;
    const std::unordered_set<std::string> requiredDirectPrograms = {
        "src/tests/kiem_tra_bo_qua.vi",
        "src/tests/kiem_tra_boolean.vi",
        "src/tests/kiem_tra_de_quy.vi",
        "src/tests/kiem_tra_dieu_kien_long_nhieu_cap.vi",
        "src/tests/kiem_tra_dieu_kien_phu_dinh.vi",
        "src/tests/kiem_tra_chon_ca.vi",
        "src/tests/kiem_tra_cu_phap_modifier_cu.vi",
        "src/tests/kiem_tra_ham.vi",
        "src/tests/kiem_tra_ham_4_tham_so.vi",
        "src/tests/kiem_tra_ham_da_tu_khong_nhay.vi",
        "src/tests/kiem_tra_ham_tham_so.vi",
        "src/tests/kiem_tra_jit_mvp.vi",
        "src/tests/kiem_tra_lambda_hof_mac_dinh.vi",
        "src/tests/kiem_tra_lop_truy_cap.vi",
        "src/tests/kiem_tra_mang_3_chieu.vi",
        "src/tests/kiem_tra_noi_chuoi.vi",
        "src/tests/kiem_tra_ngoai_le.vi",
        "src/tests/kiem_tra_rong_va_map.vi",
        "src/tests/kiem_tra_so_chan_1-20.vi",
        "src/tests/kiem_tra_so_chia_het_cho_3_va_4.vi",
        "src/tests/kiem_tra_so_le_chia_het_cho_5.vi",
        "src/tests/kiem_tra_so_nguyen.vi",
        "src/tests/kiem_tra_so_thuc.vi",
        "src/tests/kiem_tra_toan_tu_moi.vi",
        "src/tests/kiem_tra_tra_ve.vi",
        "src/tests/modules/math.vi",
        "src/tests/fixtures/api_project/dto.vi",
        "src/tests/fixtures/api_project/entity.vi",
        "src/tests/program.vi",
    };
    for (const fs::path &testFile : testFiles) {
        const std::string relative =
            fs::relative(testFile, repositoryRoot).generic_u8string();
        visited.insert(relative);

        const auto expectedEntry = expected.find(relative);
        if (expectedEntry == expected.end()) {
            ++failures;
            std::cerr << "FAIL: no frozen legacy snapshot for " << relative << '\n';
            continue;
        }

        try {
            const std::string source = readSource(testFile);
            CurrentPathGuard cwdGuard;
            fs::current_path(testFile.parent_path());
            const CompilerSnapshot pipeline = capturePipeline(source);
            if (pipeline.backend == vietvm::compiler::BytecodeBackend::DirectIr) {
                ++directPrograms;
            }
            if (requiredDirectPrograms.find(relative) != requiredDirectPrograms.end() &&
                pipeline.backend != vietvm::compiler::BytecodeBackend::DirectIr) {
                ++failures;
                std::cerr << "FAIL: " << relative
                          << " returned to the legacy token bridge\n";
            }
            const std::uint64_t actual = hashSnapshot(pipeline);
            if (actual != expectedEntry->second) {
                ++failures;
                std::cerr << "FAIL: " << relative
                          << ": legacy snapshot="
                          << formatHash(expectedEntry->second)
                          << ", pipeline=" << formatHash(actual) << '\n';
            }
        } catch (const std::exception &error) {
            ++failures;
            std::cerr << "FAIL: " << relative
                      << ": pipeline compilation threw: " << error.what() << '\n';
        }
    }

    for (const auto &entry : expected) {
        if (visited.find(entry.first) == visited.end()) {
            ++failures;
            std::cerr << "FAIL: frozen legacy snapshot has no .vi source: "
                      << entry.first << '\n';
        }
    }
    vietvm::compiler::resetCompilationState();

    if (failures != 0) {
        std::cerr << failures << " legacy compiler snapshot check(s) failed\n";
        return 1;
    }

    std::cout << "pipeline matches frozen legacy compiler state for "
              << testFiles.size() << " .vi files; direct IR backend covers "
              << directPrograms << " program(s)\n";
    return 0;
}
