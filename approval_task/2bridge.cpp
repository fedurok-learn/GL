#include <iostream>
#include <cmath>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Common, specify N and D!\n";
        return 1;
    }

    auto N = std::stol(argv[1]);
    auto D = std::stol(argv[2]);

    std::cout << (N * (N + 1) / 2 * D) << '\n';

    return 0;
}
