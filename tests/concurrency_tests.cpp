// Command-line runner for the BankSim automated tests (no GUI needed).
// Exit code 0 = every test passed.
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../src/core/test_suite.h"

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // so the rupee sign prints correctly
#endif
    int rounds = 1;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc) rounds = std::atoi(argv[++i]);

    int failures = 0;
    for (int round = 1; round <= rounds; ++round) {
        if (rounds > 1) std::printf("=== round %d/%d ===\n", round, rounds);
        const auto results = banksim::runAllTests([](const banksim::TestResult& r) {
            std::printf("[%s] Test %2d: %s  (%lld ms)\n        %s\n", r.passed ? "PASS" : "FAIL", r.number,
                        r.name.c_str(), r.ms, r.detail.c_str());
            std::fflush(stdout);
        });
        int passed = 0;
        for (const auto& r : results) passed += r.passed ? 1 : 0;
        std::printf("\n%d / %zu tests passed\n\n", passed, results.size());
        failures += static_cast<int>(results.size()) - passed;
    }
    return failures == 0 ? 0 : 1;
}
