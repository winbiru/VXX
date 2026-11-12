// Comparable std::ifstream workload for bulk I/O. No third-party JSON parser.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>

static std::string write_payload;
static std::string write_output;

#if defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif

// Keep the native reference measuring actual calls, even under -O3.
BENCH_NOINLINE int feature_call(int x) {
    volatile int result = x * 3 + 7;
    return result;
}

std::string required(const char* key) {
    const char* value = std::getenv(key);
    if (!value) throw std::runtime_error(std::string("missing ") + key);
    return value;
}

std::int64_t run(const std::string& name, const std::string& path, int n, std::int64_t file_bytes) {
    if (name == "filter_sum") {
        std::vector<int> values;
        for (int i = 0; i < n; ++i) values.push_back(i);
        std::int64_t total = 0;
        for (int value : values) if (value % 3 == 0) total += value;
        return total;
    }
    if (name == "seek_sample") {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("seek open failed");
        std::int64_t total = 0;
        for (int i = 0; i < 32; ++i) {
            file.seekg((i * 1009 + 17) % file_bytes, std::ios::beg);
            const int value = file.get();
            if (value == std::char_traits<char>::eof()) throw std::runtime_error("seek read failed");
            total += value;
        }
        return total;
    }
    if (name == "write_bytes") {
        std::ofstream file(write_output, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("write open failed");
        file.write(write_payload.data(), static_cast<std::streamsize>(write_payload.size()));
        file.close();
        if (!file) throw std::runtime_error("write close failed");
        return static_cast<std::int64_t>(write_payload.size());
    }
    if (name == "sort_numbers") {
        std::vector<int> values;
        for (int i = n - 1; i >= 0; --i) values.push_back(i);
        std::sort(values.begin(), values.end());
        std::int64_t total = 0;
        for (int i = 0; i < n; ++i) total += (i + 1) * values[i];
        return total;
    }
    if (name == "linear_search") {
        std::vector<int> values;
        for (int i = 0; i < n; ++i) values.push_back(i);
        return std::distance(values.begin(), std::find(values.begin(), values.end(), n - 1));
    }
    if (name == "set_dedup") {
        std::unordered_set<int> distinct;
        for (int i = 0; i < n; ++i) distinct.insert(i % 16);
        std::int64_t total = distinct.size();
        for (int value : distinct) total += value;
        return total;
    }
    if (name == "list_append") {
        std::vector<int> a;
        for (int i = 0; i < n; ++i) a.push_back(i);
        std::int64_t s = 0; for (int v : a) s += v;
        return s;
    }
    if (name == "map_lookup") {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < n; ++i) m["k" + std::to_string(i)] = 2 * i;
        std::int64_t s = 0;
        for (int i = 0; i < n; ++i) s += m.at("k" + std::to_string(i));
        return s;
    }
    if (name == "function_calls") {
        std::int64_t s = 0; for (int i = 0; i < n; ++i) s += feature_call(i);
        return s;
    }
    if (name == "string_concat") {
        std::string s;
        for (int i = 0; i < n; ++i) s += "V++";
        return static_cast<std::int64_t>(s.size());
    }
    if (name == "alloc_objects") {
        struct Object { int id, value; };
        std::vector<Object> objects;
        for (int i = 0; i < n; ++i) objects.push_back({i, i * 2});
        std::int64_t s = 0; for (const auto& obj : objects) s += obj.value;
        return s;
    }
    if (name == "exceptions") {
        int caught = 0;
        for (int i = 0; i < n; ++i) {
            try { if (i % 16 == 0) throw std::runtime_error("benchmark exception"); }
            catch (const std::runtime_error&) { ++caught; }
        }
        return caught;
    }
    if (name == "recursion") {
        volatile int input = 12;
        const auto fib = [](const auto& self, int x) -> int {
            return x < 2 ? x : self(self, x - 1) + self(self, x - 2);
        };
        std::int64_t s = 0; for (int i = 0; i < n; ++i) s += fib(fib, input);
        return s;
    }
    if (name == "stream_64k") {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("open failed");
        std::vector<char> buffer(65536);
        std::int64_t count = 0;
        while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount()) {
            count += file.gcount();
        }
        if (file.bad()) throw std::runtime_error("read failed");
        return count;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("open failed");
    const std::string data(std::istreambuf_iterator<char>{file}, {});
    if (file.bad()) throw std::runtime_error("read failed");
    if (name == "read_bytes") return static_cast<std::int64_t>(data.size());
    if (name == "bytes_sum") {
        std::uint64_t sum = 0;
        for (unsigned char byte : data) sum += byte;
        return static_cast<std::int64_t>(sum % 65521);
    }
    if (name == "read_text") {
        // Decode/validate UTF-8 scalar values, matching Python's strict UTF-8 read.
        std::int64_t count = 0;
        for (std::size_t offset = 0; offset < data.size();) {
            const auto c = static_cast<unsigned char>(data[offset]);
            std::size_t length = 1;
            std::uint32_t codepoint = c;
            if (c >= 0xc2 && c <= 0xdf) { length = 2; codepoint = c & 0x1f; }
            else if (c >= 0xe0 && c <= 0xef) { length = 3; codepoint = c & 0x0f; }
            else if (c >= 0xf0 && c <= 0xf4) { length = 4; codepoint = c & 0x07; }
            else if (c >= 0x80) throw std::runtime_error("invalid UTF-8 lead byte");
            if (offset + length > data.size()) throw std::runtime_error("truncated UTF-8");
            for (std::size_t next = 1; next < length; ++next) {
                const auto b = static_cast<unsigned char>(data[offset + next]);
                if ((b & 0xc0) != 0x80) throw std::runtime_error("invalid UTF-8 continuation");
                codepoint = (codepoint << 6) | (b & 0x3f);
            }
            if ((length == 2 && codepoint < 0x80) ||
                (length == 3 && codepoint < 0x800) ||
                (length == 4 && codepoint < 0x10000) ||
                (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
                throw std::runtime_error("invalid UTF-8 scalar");
            offset += length;
            ++count;
        }
        return count;
    }
    if (name == "csv_split") {
        std::int64_t total = 0;
        std::size_t start = 0;
        while (start < data.size()) {
            const auto end = data.find('\n', start);
            const auto stop = end == std::string::npos ? data.size() : end;
            if (stop > start) {
                const auto first = data.find(',', start);
                const auto second = first == std::string::npos ? first : data.find(',', first + 1);
                if (first >= stop || second >= stop) throw std::runtime_error("csv columns");
                total += std::stoll(data.substr(start, first - start));
                total += std::stoll(data.substr(second + 1, stop - second - 1));
            }
            start = stop + 1;
        }
        return total;
    }
    throw std::runtime_error("unsupported case");
}

int main() {
    try {
        const auto name = required("BENCH_CASE"), path = required("BENCH_FILE");
        const int iterations = std::stoi(required("BENCH_ITERATIONS"));
        const int n = std::stoi(required("BENCH_FEATURE_N"));
        const auto file_bytes = std::stoll(required("BENCH_FILE_BYTES"));
        const auto expected = std::stoll(required("BENCH_EXPECT_UNIT"));
        if (name == "write_bytes") {
            write_output = required("BENCH_OUTPUT");
            std::ifstream source(path, std::ios::binary);
            if (!source) throw std::runtime_error("source open failed");
            write_payload.assign(std::istreambuf_iterator<char>{source}, {});
            if (source.bad()) throw std::runtime_error("source read failed");
        }
        if (iterations < 1 || iterations > 1000000 || expected < 0 || n < 1 || n > 1024 || file_bytes < 1)
            throw std::runtime_error("invalid args");
        if (run(name, path, n, file_bytes) != expected) throw std::runtime_error("preflight mismatch");
        auto start = std::chrono::steady_clock::now();
        std::int64_t checksum = 0;
        for (int i = 0; i < iterations; ++i) {
            const auto value = run(name, path, n, file_bytes);
            if (value != expected) throw std::runtime_error("unit mismatch");
            checksum += value;
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "{\"case\":\"" << name << "\",\"iterations\":" << iterations
                  << ",\"unit\":" << expected << ",\"checksum\":" << checksum
                  << ",\"elapsed_ms\":" << ms << "}\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
