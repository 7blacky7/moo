#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char **argv) {
    if (argc != 3) { std::cerr << "Aufruf: ast_vergleich erwartet erhalten\n"; return 2; }
    std::ifstream expected_file(argv[1], std::ios::binary);
    std::ifstream actual_file(argv[2], std::ios::binary);
    if (!expected_file || !actual_file) { std::cerr << "AST-Datei nicht lesbar\n"; return 2; }
    const std::string expected((std::istreambuf_iterator<char>(expected_file)), {});
    const std::string actual((std::istreambuf_iterator<char>(actual_file)), {});
    if (expected == actual) { std::cout << "AST-Golden stimmt\n"; return 0; }
    std::size_t pos = 0, line = 1;
    while (pos < expected.size() && pos < actual.size() && expected[pos] == actual[pos]) {
        if (expected[pos] == '\n') ++line;
        ++pos;
    }
    std::cerr << "AST-Golden abweichend: Byte " << pos << ", Zeile " << line << '\n';
    return 1;
}
