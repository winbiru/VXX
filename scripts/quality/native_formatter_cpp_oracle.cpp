// Stage 0 formatter oracle for migration parity tests only.
#include "vpp/tooling/tooling.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("cannot read formatter fixture");
        const std::string source((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());
        std::cout << vietvm::tooling::formatSource(source);
        return std::cout ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
