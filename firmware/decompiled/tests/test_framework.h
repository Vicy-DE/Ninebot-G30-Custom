/**
 * @file test_framework.h
 * @brief Minimal test framework for Ninebot G30 firmware tests.
 *
 * Provides a lightweight xUnit-style test runner:
 *   - TEST(suite, name) macro to define test cases
 *   - ASSERT_* macros for assertions with file/line info
 *   - Automatic test registration and execution
 *   - Summary with pass/fail counts
 *
 * Usage:
 *   TEST(Protocol, ChecksumCalculation) {
 *       uint16_t cs = calculateChecksum(data, len);
 *       ASSERT_EQ(cs, 0x1234);
 *   }
 */

#ifndef NINEBOT_TEST_FRAMEWORK_H
#define NINEBOT_TEST_FRAMEWORK_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <sstream>

namespace test {

/* =========================================================================
 * Test Registration
 * ========================================================================= */

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> func;
};

/** Global test registry (populated via TEST() macro). */
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

/** Auto-register helper. */
struct TestRegistrar {
    TestRegistrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back({suite, name, std::move(fn)});
    }
};


/* =========================================================================
 * Assertion State
 * ========================================================================= */

struct AssertionState {
    int assertions      = 0;
    int failures        = 0;
    bool currentFailed  = false;
    std::string lastError;
};

inline AssertionState& state() {
    static AssertionState s;
    return s;
}

inline void resetCurrentTest() {
    state().currentFailed = false;
    state().lastError.clear();
}


/* =========================================================================
 * Assertion Macros
 * ========================================================================= */

#define TEST_FAIL(msg)                                                       \
    do {                                                                     \
        test::state().assertions++;                                          \
        test::state().failures++;                                            \
        test::state().currentFailed = true;                                  \
        std::ostringstream oss;                                              \
        oss << "  FAIL: " << __FILE__ << ":" << __LINE__ << ": " << msg;    \
        test::state().lastError = oss.str();                                 \
        std::cerr << test::state().lastError << "\n";                        \
    } while (0)

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        test::state().assertions++;                                          \
        if (!(expr)) {                                                       \
            TEST_FAIL(#expr " is false");                                    \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(expr)                                                   \
    do {                                                                     \
        test::state().assertions++;                                          \
        if ((expr)) {                                                        \
            TEST_FAIL(#expr " is true (expected false)");                    \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(a, b)                                                      \
    do {                                                                     \
        test::state().assertions++;                                          \
        auto va = (a); auto vb = (b);                                        \
        if (va != vb) {                                                      \
            std::ostringstream oss;                                          \
            oss << #a " == " #b " (" << va << " != " << vb << ")";          \
            TEST_FAIL(oss.str());                                            \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_NE(a, b)                                                      \
    do {                                                                     \
        test::state().assertions++;                                          \
        auto va = (a); auto vb = (b);                                        \
        if (va == vb) {                                                      \
            std::ostringstream oss;                                          \
            oss << #a " != " #b " (both are " << va << ")";                 \
            TEST_FAIL(oss.str());                                            \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_LT(a, b)                                                      \
    do {                                                                     \
        test::state().assertions++;                                          \
        auto va = (a); auto vb = (b);                                        \
        if (!(va < vb)) {                                                    \
            std::ostringstream oss;                                          \
            oss << #a " < " #b " (" << va << " >= " << vb << ")";           \
            TEST_FAIL(oss.str());                                            \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_LE(a, b)                                                      \
    do {                                                                     \
        test::state().assertions++;                                          \
        auto va = (a); auto vb = (b);                                        \
        if (!(va <= vb)) {                                                   \
            std::ostringstream oss;                                          \
            oss << #a " <= " #b " (" << va << " > " << vb << ")";           \
            TEST_FAIL(oss.str());                                            \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_GT(a, b)                                                      \
    do {                                                                     \
        test::state().assertions++;                                          \
        auto va = (a); auto vb = (b);                                        \
        if (!(va > vb)) {                                                    \
            std::ostringstream oss;                                          \
            oss << #a " > " #b " (" << va << " <= " << vb << ")";           \
            TEST_FAIL(oss.str());                                            \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_GE(a, b)                                                      \
    do {                                                                     \
        test::state().assertions++;                                          \
        auto va = (a); auto vb = (b);                                        \
        if (!(va >= vb)) {                                                   \
            std::ostringstream oss;                                          \
            oss << #a " >= " #b " (" << va << " < " << vb << ")";           \
            TEST_FAIL(oss.str());                                            \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_NEAR(a, b, tol)                                               \
    do {                                                                     \
        test::state().assertions++;                                          \
        auto va = (a); auto vb = (b); auto vt = (tol);                      \
        if (std::abs(va - vb) > vt) {                                        \
            std::ostringstream oss;                                          \
            oss << "|" #a " - " #b "| <= " #tol " ("                        \
                << va << " vs " << vb << ", delta=" << std::abs(va-vb) << ")";\
            TEST_FAIL(oss.str());                                            \
            return;                                                          \
        }                                                                    \
    } while (0)


/* =========================================================================
 * TEST Macro
 * ========================================================================= */

#define TEST(suite, name)                                                    \
    void test_##suite##_##name();                                            \
    static test::TestRegistrar registrar_##suite##_##name(                   \
        #suite, #name, test_##suite##_##name);                               \
    void test_##suite##_##name()


/* =========================================================================
 * Test Runner
 * ========================================================================= */

inline int runAllTests() {
    auto& tests = registry();
    int passed = 0, failed = 0;

    std::cout << "========================================\n";
    std::cout << " Ninebot G30 Firmware Test Suite\n";
    std::cout << " " << tests.size() << " tests registered\n";
    std::cout << "========================================\n\n";

    for (auto& tc : tests) {
        resetCurrentTest();
        std::cout << "[RUN ] " << tc.suite << "." << tc.name << std::endl;

        try {
            tc.func();
        } catch (const std::exception& ex) {
            TEST_FAIL(std::string("Exception: ") + ex.what());
        } catch (...) {
            TEST_FAIL("Unknown exception");
        }

        if (state().currentFailed) {
            std::cout << "[FAIL] " << tc.suite << "." << tc.name << "\n";
            failed++;
        } else {
            std::cout << "[ OK ] " << tc.suite << "." << tc.name << "\n";
            passed++;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << " Results: " << passed << " passed, "
              << failed << " failed, "
              << (passed + failed) << " total\n";
    std::cout << " Assertions: " << state().assertions << " checked, "
              << state().failures << " failed\n";
    std::cout << "========================================\n";

    return failed > 0 ? 1 : 0;
}


} // namespace test

#endif // NINEBOT_TEST_FRAMEWORK_H
