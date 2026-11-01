#include "test_util.h"

TEST(skeleton_is_alive) {
    CHECK_EQ(1 + 1, 2);
}

int main() {
    for (const auto& test : test_registry()) {
        const int before = test_failures();
        test.fn();
        const int failures = test_failures() - before;
        std::cout << (failures == 0 ? "PASS " : "FAIL ") << test.name << '\n';
    }
    if (test_failures() != 0) {
        std::cerr << test_failures() << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed (" << test_registry().size() << ")\n";
    return 0;
}
