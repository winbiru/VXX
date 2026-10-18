#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "vpp/native/target.h"

int main() {
    using namespace vietvm::native;
    unsigned failures = 0;
    const auto expect = [&](bool valid, const std::string &label) {
        if (!valid) {
            std::cerr << "FAIL: " << label << '\n';
            ++failures;
        }
    };

    const auto &architectures = registeredArchitectures();
    expect(architectures.size() == 16, "đầy đủ kiến trúc và biến thể endian");
    std::set<std::string> names;
    for (const auto &entry : architectures) {
        expect(names.insert(entry.name).second, std::string("không trùng tên: ") + entry.name);
        expect(entry.bits == 32 || entry.bits == 64, std::string("độ rộng: ") + entry.name);
        const auto result = parseTarget(std::string(entry.name) + "-unknown-linux-gnu");
        expect(result.target.has_value(), std::string("nhận target Linux: ") + entry.name);
        if (result.target) {
            expect(result.target->arch == entry.arch, std::string("kiến trúc: ") + entry.name);
            expect(result.target->objectFormat == ObjectFormat::Elf,
                   std::string("ELF: ") + entry.name);
            expect(canonicalTriple(*result.target) == std::string(entry.name) + "-unknown-linux-gnu",
                   std::string("triple ổn định: ") + entry.name);
        }
    }

    const std::vector<std::pair<std::string, std::string>> aliases = {
        {"AMD64-linux", "x86_64-unknown-linux-gnu"},
        {"arm64-apple-darwin", "aarch64-apple-darwin"},
        {"rv64-linux-musl", "riscv64-unknown-linux-musl"},
        {"i686-linux-gnu", "i386-unknown-linux-gnu"},
        {"armv7l-linux-gnueabihf", "armv7-unknown-linux-gnueabihf"},
        {"ppc64le-linux-gnu", "powerpc64le-unknown-linux-gnu"},
        {"mips64el-linux-gnu", "mips64el-unknown-linux-gnu"},
        {"x64-pc-windows-msvc", "x86_64-pc-windows-msvc"},
        {"x86_64-w64-mingw32", "x86_64-w64-windows-gnu"},
    };
    for (const auto &[input, canonical] : aliases) {
        const auto parsed = parseTarget(input);
        expect(parsed.target.has_value(), "alias hợp lệ: " + input);
        if (parsed.target) {
            expect(canonicalTriple(*parsed.target) == canonical,
                   "canonical triple: " + input);
            const auto roundtrip = parseTarget(canonical);
            expect(roundtrip.target.has_value() &&
                       canonicalTriple(*roundtrip.target) == canonical,
                   "roundtrip triple: " + canonical);
        }
    }

    for (const std::string &bad : {
             "riscv128-linux-gnu", "aarch64", "aarch64-plan9",
             "riscv64-linux-msvc", "x86_64-windows-musl",
             "aarch64-apple-darwin-gnu", "mips64-windows-msvc",
             "x86_64-linux-unknown", "-linux-gnu"}) {
        const auto result = parseTarget(bad);
        expect(!result.target && !result.error.empty(), "từ chối target sai: " + bad);
    }

    for (const std::string &triple : {
             "aarch64-apple-darwin", "aarch64-linux-gnu",
             "x86_64-linux-gnu", "x86_64-pc-windows-msvc"}) {
        const auto result = parseTarget(triple);
        expect(result.target && hasExperimentalMachineJit(*result.target),
               "nhận diện backend JIT có thật: " + triple);
    }
    for (const std::string &triple : {
             "x86_64-apple-darwin", "aarch64-pc-windows-msvc",
             "riscv64-linux-gnu", "loongarch64-linux-gnu", "s390x-linux-gnu"}) {
        const auto result = parseTarget(triple);
        expect(result.target && !hasExperimentalMachineJit(*result.target),
               "không quảng cáo JIT chưa có: " + triple);
    }
    const auto winArm64 = parseTarget("aarch64-pc-windows-msvc");
    expect(winArm64.target &&
               winArm64.target->objectFormat == ObjectFormat::Coff &&
               canonicalTriple(*winArm64.target) == "aarch64-pc-windows-msvc",
           "Windows ARM64 là target hợp lệ dù chưa có JIT");

    const auto host = hostTarget();
    expect(host.arch != Architecture::Unknown && host.os != OperatingSystem::Unknown,
           "nhận diện host hiện tại");
    const auto hostParsed = parseTarget(canonicalTriple(host));
    expect(hostParsed.target.has_value(), "host triple phân tích ngược");

    if (failures) return 1;
    std::cout << "Target registry hardening: PASS\n";
    return 0;
}
