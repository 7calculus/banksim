cmake_minimum_required(VERSION 3.16)
project(BankSim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(BANKSIM_BUILD_GUI   "Build the Qt 6 desktop application" ON)
option(BANKSIM_BUILD_TESTS "Build the command-line test runner" ON)
option(BANKSIM_SANITIZE_THREAD "Build with ThreadSanitizer (GCC/Clang, Linux)" OFF)

find_package(Threads REQUIRED)

# ------------------------------------------------------------------------------
# Core engine: Qt-free C++17 (std::thread, std::mutex, std::shared_mutex, ...)
# ------------------------------------------------------------------------------
add_library(banksim_core STATIC
  src/core/types.cpp
  src/core/event_log.cpp
  src/core/bank.cpp
  src/core/transaction_engine.cpp
  src/core/thread_manager.cpp
  src/core/concurrency_lab.cpp
  src/core/deadlock_simulator.cpp
  src/core/stress_test.cpp
  src/core/test_suite.cpp)
target_include_directories(banksim_core PUBLIC src/core)
target_link_libraries(banksim_core PUBLIC Threads::Threads)

if(MSVC)
  target_compile_options(banksim_core PUBLIC /W4 /utf-8 /EHsc)
  target_compile_definitions(banksim_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(banksim_core PUBLIC -Wall -Wextra)
endif()

if(BANKSIM_SANITIZE_THREAD AND NOT MSVC)
  target_compile_options(banksim_core PUBLIC -fsanitize=thread -g -O1)
  target_link_options(banksim_core PUBLIC -fsanitize=thread)
endif()

# ------------------------------------------------------------------------------
# Command-line tests (also usable on machines without Qt)
# ------------------------------------------------------------------------------
if(BANKSIM_BUILD_TESTS)
  enable_testing()
  add_executable(banksim_tests tests/concurrency_tests.cpp)
  target_link_libraries(banksim_tests PRIVATE banksim_core)
  add_test(NAME banksim_concurrency_tests COMMAND banksim_tests)
endif()

# ------------------------------------------------------------------------------
# Qt 6 desktop application
# ------------------------------------------------------------------------------
if(BANKSIM_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)

  set(BANKSIM_GUI_SOURCES
    src/main.cpp
    src/gui/backend.h            src/gui/backend.cpp
    src/gui/theme.h              src/gui/theme.cpp
    src/gui/widgets.h            src/gui/widgets.cpp
    src/gui/event_model.h        src/gui/event_model.cpp
    src/gui/dashboard_page.h     src/gui/dashboard_page.cpp
    src/gui/accounts_page.h      src/gui/accounts_page.cpp
    src/gui/simulator_page.h     src/gui/simulator_page.cpp
    src/gui/lab_result_view.h    src/gui/lab_result_view.cpp
    src/gui/lab_page.h           src/gui/lab_page.cpp
    src/gui/monitor_page.h       src/gui/monitor_page.cpp
    src/gui/mainwindow.h         src/gui/mainwindow.cpp
    resources/resources.qrc)

  if(WIN32)
    list(APPEND BANKSIM_GUI_SOURCES resources/banksim.rc)
  endif()

  add_executable(BankSim WIN32 ${BANKSIM_GUI_SOURCES})
  target_include_directories(BankSim PRIVATE src src/gui)
  target_link_libraries(BankSim PRIVATE banksim_core Qt6::Widgets)
  set_target_properties(BankSim PROPERTIES WIN32_EXECUTABLE ON)

  # `cmake --build . --target deploy` copies the Qt DLLs next to BankSim.exe (Windows).
  if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
      add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --compiler-runtime --no-translations
                --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:BankSim>"
        DEPENDS BankSim
        COMMENT "Running windeployqt")
    endif()
  endif()
endif()
