// test_suite.h - the automated validation scenarios (PRD section 9 + extra rule checks).
// Shared by the command-line runner (tests/concurrency_tests.cpp) and the GUI test tab.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace banksim {

struct TestResult {
    int number = 0;
    std::string name;
    bool passed = false;
    std::string detail;
    long long ms = 0;
};

// Runs every test sequentially on the calling thread (each test spawns its own worker
// threads). `progress` is invoked after each test completes.
std::vector<TestResult> runAllTests(const std::function<void(const TestResult&)>& progress = {});

// Number of tests runAllTests() will execute.
int testCount();

}  // namespace banksim
