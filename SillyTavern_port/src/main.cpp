#include "ui/application.h"
#include "ui/smoke.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string version = "SillyTavern Port 0.2.0";
    if (argc > 1 && std::string(argv[1]) == "--version") {
        std::cout << version << '\n';
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--smoke-test") {
        return st::ui::run_smoke_test();
    }

    auto application = st::ui::Application::create();
    return application->run(argc, argv);
}
