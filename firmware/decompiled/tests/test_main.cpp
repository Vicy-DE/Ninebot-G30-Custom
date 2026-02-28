/**
 * @file test_main.cpp
 * @brief Test runner entry point for Ninebot G30 firmware test suite.
 *
 * Registers and runs all test cases defined across test files.
 * Returns 0 on all tests passing, 1 on any failure.
 */

#include "test_framework.h"

int main()
{
    return test::runAllTests();
}
