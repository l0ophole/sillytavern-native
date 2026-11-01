#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string version = "SillyTavern Port 0.1.0 (skeleton)";
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::cout << version << '\n';
        return 0;
    }
    std::cout << version << '\n';
    return 0;
}
