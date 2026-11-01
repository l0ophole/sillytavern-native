#pragma once

#include <iostream>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline int& test_failures() {
    static int failures = 0;
    return failures;
}

inline bool register_test(const char* name, void (*fn)()) {
    test_registry().push_back(TestCase{name, fn});
    return true;
}

#define TEST(name)                                      \
    void name();                                        \
    static bool name##_registered = register_test(#name, name); \
    void name()

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << __FILE__ << ":" << __LINE__                         \
                      << ": CHECK failed: " #condition "\n";                 \
            ++test_failures();                                               \
        }                                                                    \
    } while (false)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        const auto& actual_value = (actual);                                 \
        const auto& expected_value = (expected);                             \
        if (!(actual_value == expected_value)) {                             \
            std::cerr << __FILE__ << ":" << __LINE__                         \
                      << ": CHECK_EQ failed: " #actual " == " #expected      \
                      << " (actual: " << actual_value                        \
                      << ", expected: " << expected_value << ")\n";          \
            ++test_failures();                                               \
        }                                                                    \
    } while (false)
