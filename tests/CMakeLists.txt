# The build description for the test suites.
# 
# Three suites are built: one per source file in the unit folder, one per file in
# the integration folder, and one per file in the benchmark folder. They are
# separate executables so that a crash in a benchmark cannot hide a unit test
# failure, and so that the fast suite can be run on its own during development.
find_package(Threads REQUIRED)
# Catch2's headers are third party code written for every compiler, and one of
# its macros expands to `__COUNTER__`, which the newest Clang versions flag as
# an extension under the project's own strict warning set. The include folder is
# therefore marked as a system folder for every test target, so the strict
# warnings stay aimed at this repository.
get_target_property(AGENT_DB_DOCTOR_CATCH2_INCLUDE_FOLDERS Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)
# The Catch2 main object is compiled once and linked into every test binary. It
# provides the `main` function that Catch2 expects a test program to have.
add_library(agent_db_doctor_catch_main STATIC CatchMain.cpp)
target_link_libraries(agent_db_doctor_catch_main PUBLIC agent_db_doctor_core Catch2::Catch2WithMain)
target_include_directories(agent_db_doctor_catch_main SYSTEM PRIVATE ${AGENT_DB_DOCTOR_CATCH2_INCLUDE_FOLDERS})
target_compile_options(agent_db_doctor_catch_main PRIVATE ${AGENT_DB_DOCTOR_WARNINGS})
# The synthetic database builder creates a real SQLite database with the schema
# an AI coding agent uses. Both the integration suite and the unit suite need it,
# so it is built once as a static library.
add_library(agent_db_doctor_test_support STATIC SyntheticDatabaseBuilder.cpp)
target_include_directories(agent_db_doctor_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(agent_db_doctor_test_support PUBLIC agent_db_doctor_core)
target_compile_options(agent_db_doctor_test_support PRIVATE ${AGENT_DB_DOCTOR_WARNINGS})
# The helper that applies the warning flags to a test target.
function(agent_db_doctor_add_test target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name}
        PRIVATE agent_db_doctor_core agent_db_doctor_test_support
                agent_db_doctor_catch_main Catch2::Catch2)
    target_include_directories(${target_name} SYSTEM PRIVATE ${AGENT_DB_DOCTOR_CATCH2_INCLUDE_FOLDERS})
    target_compile_options(${target_name} PRIVATE ${AGENT_DB_DOCTOR_WARNINGS})
    if(NOT MSVC)
        target_compile_options(${target_name} PRIVATE -fno-rtti)
    endif()
    # The watchdog benchmark reads the process's own memory usage, which on Windows
    # comes from the process information library.
    if(WIN32)
        target_link_libraries(${target_name} PRIVATE psapi)
    endif()
    add_test(NAME ${target_name} COMMAND ${target_name})
    # A hung test would otherwise block a continuous integration run forever.
    set_tests_properties(${target_name} PROPERTIES TIMEOUT 300)
endfunction()
# The unit suite. Each file covers one class that has logic worth testing.
set(AGENT_DB_DOCTOR_UNIT_TESTS
    unit/TestArena.cpp
    unit/TestDatabase.cpp
    unit/TestResult.cpp
    unit/TestHumanReadableFormatting.cpp
    unit/TestPlatformMetadata.cpp
    unit/TestDiskSpaceChecker.cpp
    unit/TestStatisticsReader.cpp
    unit/TestAgents.cpp
    unit/TestSchemaVersionDetector.cpp
    unit/TestKnownFailurePatterns.cpp
    unit/TestProgressBar.cpp
    unit/TestServiceConfiguration.cpp
    unit/TestStatement.cpp
    unit/TestActionRunner.cpp
)
foreach(test_source IN LISTS AGENT_DB_DOCTOR_UNIT_TESTS)
    get_filename_component(test_name "${test_source}" NAME_WE)
    agent_db_doctor_add_test(${test_name} "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}")
endforeach()
# The action worker test drives the graphical worker headlessly. Its sources
# live with the interface but depend only on the core library, so they compile
# here without SDL or Dear ImGui anywhere near the test.
target_include_directories(TestActionRunner PRIVATE "${CMAKE_SOURCE_DIR}/gui")
target_sources(TestActionRunner PRIVATE "${CMAKE_SOURCE_DIR}/gui/ActionRunner.cpp")
# The integration suite. These tests open real SQLite files on the real file
# system and exercise whole commands from end to end.
set(AGENT_DB_DOCTOR_INTEGRATION_TESTS
    integration/DiagnoseIntegrationTest.cpp
    integration/PruneIntegrationTest.cpp
    integration/CompactIntegrationTest.cpp
    integration/RecoverIntegrationTest.cpp
    integration/ServiceIntegrationTest.cpp
    integration/CompressionCorruptionIntegrationTest.cpp
    integration/NotADatabaseIntegrationTest.cpp
    integration/EndToEndIntegrationTest.cpp
)
foreach(test_source IN LISTS AGENT_DB_DOCTOR_INTEGRATION_TESTS)
    get_filename_component(test_name "${test_source}" NAME_WE)
    agent_db_doctor_add_test(${test_name} "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}")
endforeach()
# The benchmark suite. These tests measure wall clock time, resident memory, and
# the number of file metadata calls the tool makes. They are registered with
# CTest under the `benchmark` label so a continuous integration job can run or
# skip them.
set(AGENT_DB_DOCTOR_BENCHMARK_TESTS
    benchmark/DiagnoseBenchmark.cpp
    benchmark/PruneBenchmark.cpp
    benchmark/WatchdogBenchmark.cpp
)
foreach(test_source IN LISTS AGENT_DB_DOCTOR_BENCHMARK_TESTS)
    get_filename_component(test_name "${test_source}" NAME_WE)
    agent_db_doctor_add_test(${test_name} "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}")
    set_tests_properties(${test_name} PROPERTIES LABELS "benchmark" TIMEOUT 900)
endforeach()
