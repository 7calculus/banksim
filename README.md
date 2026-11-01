# BankSim - Multithreaded Banking & ATM Simulator

C++17 + Qt 6 Widgets desktop app (Windows 10/11; also builds on Linux/macOS). One central Bank Engine;
ATM 1-3, Online and Mobile Banking are only access channels. Every transaction runs on its own
`std::thread`; the GUI never blocks (workers -> queued Qt signals).

## Build & run (Windows)
Requirements: Visual Studio 2022 (Desktop C++), CMake >= 3.16, Qt 6 (MSVC 64-bit kit).

    scripts\build_windows.bat "C:\Qt\6.7.3\msvc2022_64"

This builds Release, runs the automated tests, and packages `dist\BankSim\BankSim.exe` with all Qt DLLs
(`windeployqt`). Double-click the exe. Without a local Qt, push to GitHub: the `windows-build` workflow
builds, tests, packages and uploads a `BankSim-windows` artifact (unzip and run).

Manual: `cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt> && cmake --build build --config Release && cmake --build build --target deploy`.
Tests only (no Qt needed): `-DBANKSIM_BUILD_GUI=OFF`, then run `banksim_tests` (`--repeat N` to loop).

## Screens
Dashboard - Customers & Accounts (search, joint accounts, create customers/accounts) - Banking Simulator
(Execute Transaction / Execute Concurrently / Stress Test / Reset) - Concurrency Lab - Live Thread Monitor (filters, CSV export).

## Concurrency Lab
1. **Race condition** (unsafe): atomics + barriers, no undefined behaviour; deterministic lost update.
   A and B withdraw 7,000 + 6,000 from a joint 10,000: both "succeed", balance 4,000 instead of 3,000.
2. **Synchronization** (safe): per-account `std::shared_mutex` (RAII `unique_lock` writers, `shared_lock`
   enquiries). Same scenario: A succeeds, B is rejected, balance 3,000.
3. **Deadlock**: opposite transfers with inconsistent lock order, `std::timed_mutex` time-outs, wait-for-graph
   cycle detection - the app never freezes.
4. **Prevention**: locks taken in ascending account-id order - no deadlock.
5. **Automated tests**: the 10 PRD scenarios + 5 extra (validation, unique IDs, stress, ATM cash, reset), PASS/FAIL.

Transfers are atomic (both locks held for debit+credit). No global mutex: independent accounts run in parallel.

## Layout
`src/core` Qt-free engine (bank, transaction_engine, thread_manager, concurrency_lab, deadlock_simulator,
stress_test, test_suite) - `src/gui` Qt screens - `tests/` CLI runner.

## Verification status
Core tests 15/15 pass (repeated runs, ThreadSanitizer clean; removing locks in a mutant makes tests fail).
GUI verified via an offscreen tour on Linux. The Windows .exe is produced by the script/CI above.
