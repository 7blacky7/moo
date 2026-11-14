#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    std::ifstream expected(argv[1]), actual(argv[2]);
    if (!expected || !actual) return 2;
    std::string left, right;
    unsigned line = 0;
    while (true) {
        const bool a = static_cast<bool>(std::getline(expected, left));
        const bool b = static_cast<bool>(std::getline(actual, right));
        if (!a && !b) break;
        ++line;
        if (a != b || left != right) {
            std::cerr << "layout mismatch at line " << line << '\n';
            return 1;
        }
    }
    if (expected.bad() || actual.bad() || line == 0) return 2;
    std::cout << "Layout rows match\n";
    return 0;
}
