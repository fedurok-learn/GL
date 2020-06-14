#include <iostream>
#include <cmath>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Common, specify K and N!\n";
        return 1;
    }

    auto N = std::stoi(argv[1]);
    auto K = std::stoi(argv[2]) + 1;

    std::cout << std::ceil(N * 1.0 / K) << '\n';

    return 0;
}