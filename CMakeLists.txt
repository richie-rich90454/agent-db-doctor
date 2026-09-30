# agent-db-doctor - a doctor for AI coding agent databases.
#
# This file is the top level build description. It downloads the five permitted
# third party libraries, compiles every source file of the core library, builds
# the command line executable, and then hands control to the tests folder.
cmake_minimum_required(VERSION 3.24)
# The project version is also used to stamp the executable and the service files.
project(agent-db-doctor
    VERSION 1.0.0
    DESCRIPTION "A doctor for AI coding agent databases."
    # C is enabled only because the vendored SQLite amalgamation is a C file. No
    # first party source in this project is C.
    LANGUAGES CXX C
)
# `CXX_STANDARD 23` requires the compiler to implement C++23. We additionally
# require the extensions to be off so that the code stays portable.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Position independent code is required so the static library can be linked into
# a shared object later, and it is harmless on every supported platform.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Only build the artefacts we actually ship, in every configuration.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# `BUILD_TESTING` is the conventional switch that CTest understands.
include(CTest)
# This is an option, not a variable, so callers can pass `-DAGENT_DB_DOCTOR_BUILD_TESTS=OFF`.
option(AGENT_DB_DOCTOR_BUILD_TESTS "Build the unit, integration, and benchmark tests." ON)
option(AGENT_DB_DOCTOR_ENABLE_WERROR "Turn compiler warnings into errors." OFF)
# If we are the top level project, default to a release build because the
# performance contract in the specification is only meaningful when optimised.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "The build type." FORCE)
    endif()
endif()
# `FetchContent` downloads and configures the permitted third party libraries.
include(FetchContent)
# `SYSTEM` makes the compiler treat the third party headers as system headers so
# that our own strict warning flags do not fire inside vendor code.
set(FETCHCONTENT_QUIET OFF)
# ---------------------------------------------------------------------------
# sqlite3 - vendored from the official SQLite amalgamation.
# ---------------------------------------------------------------------------
# The SQLite project publishes a single zip archive containing sqlite3.c,
# sqlite3.h, and sqlite3ext.h. We download the zip and unpack it with the
# archive support built into CMake, which keeps the build free of external
# tools such as unzip or PowerShell.
set(AGENT_DB_DOCTOR_SQLITE_VERSION "3500400")
set(AGENT_DB_DOCTOR_SQLITE_URL
    "https://www.sqlite.org/2025/sqlite-amalgamation-${AGENT_DB_DOCTOR_SQLITE_VERSION}.zip")
# `SOURCE_SUBDIR` names a directory that does not exist. That is deliberate: it
# tells FetchContent to download and unpack the archive but not to look for a
# CMakeLists inside it, because the SQLite amalgamation has none. It is the
# supported way to do this in current CMake, replacing the deprecated
# `FetchContent_Populate` call.
FetchContent_Declare(sqlite3_amalgamation
    URL "${AGENT_DB_DOCTOR_SQLITE_URL}"
    SOURCE_SUBDIR "agent-db-doctor-does-not-build-sqlite-with-cmake"
)
FetchContent_MakeAvailable(sqlite3_amalgamation)
# The archive unpacks into the directory FetchContent chose, containing
# sqlite3.c, sqlite3.h, sqlite3ext.h, and shell.c.
set(AGENT_DB_DOCTOR_SQLITE_DIR "${sqlite3_amalgamation_SOURCE_DIR}")
if(NOT EXISTS "${AGENT_DB_DOCTOR_SQLITE_DIR}/sqlite3.c")
    # Some archive layouts add one level of nesting, so the tree is searched one
    # level down. `file(GLOB)` returns a list, and the first entry is taken.
    file(GLOB AGENT_DB_DOCTOR_UNPACKED "${sqlite3_amalgamation_SOURCE_DIR}/*/sqlite3.c")
    list(GET AGENT_DB_DOCTOR_UNPACKED 0 AGENT_DB_DOCTOR_UNPACKED_C)
    get_filename_component(AGENT_DB_DOCTOR_SQLITE_DIR "${AGENT_DB_DOCTOR_UNPACKED_C}" DIRECTORY)
endif()
if(NOT EXISTS "${AGENT_DB_DOCTOR_SQLITE_DIR}/sqlite3.c")
    message(FATAL_ERROR
        "The SQLite amalgamation was downloaded but sqlite3.c was not found in "
        "${AGENT_DB_DOCTOR_SQLITE_DIR}.")
endif()
# `SQLITE_OMIT_DEPRECATED` removes long deprecated APIs we never use.
# `SQLITE_OMIT_SHARED_CACHE` removes the global shared cache, which is unsafe
# in a process that may load several databases at once.
# `SQLITE_DEFAULT_MEMSTATUS=0` turns off the memory statistics counters. We
# manage our own memory and never query SQLite for allocation totals.
# `SQLITE_DQS=0` rejects double quoted string literals in SQL, which is the
# usual source of injection bugs.
# `SQLITE_THREADSAFE=0` removes mutexes entirely. The tool is single threaded by
# design, and the specification forbids synchronisation primitives.
add_library(sqlite3 STATIC "${AGENT_DB_DOCTOR_SQLITE_DIR}/sqlite3.c")
target_include_directories(sqlite3 SYSTEM PUBLIC "${AGENT_DB_DOCTOR_SQLITE_DIR}")
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0
    SQLITE_THREADSAFE=0
    SQLITE_ENABLE_COLUMN_METADATA=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_USE_ALLOCA=1
    SQLITE_DEFAULT_FOREIGN_KEYS=0
)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()
set_target_properties(sqlite3 PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
# ---------------------------------------------------------------------------
# CLI11 - header only command line parsing.
# ---------------------------------------------------------------------------
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CLI11)
# ---------------------------------------------------------------------------
# fmt - formatting, used only on cold paths such as report rendering.
# ---------------------------------------------------------------------------
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.1.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)
# ---------------------------------------------------------------------------
# spdlog - logging, configured later by our own Logger class.
# ---------------------------------------------------------------------------
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
# `SPDLOG_FMT_EXTERNAL` makes spdlog use the fmt library we already built
# instead of its own bundled copy, which would duplicate every format symbol.
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)
# ---------------------------------------------------------------------------
# Catch2 - the test framework, only required when tests are enabled.
# ---------------------------------------------------------------------------
if(AGENT_DB_DOCTOR_BUILD_TESTS)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
endif()
# ---------------------------------------------------------------------------
# Our own warning and language configuration.
# ---------------------------------------------------------------------------
# `AGENT_DB_DOCTOR_WARNINGS` is applied to our targets only. Vendor targets keep
# their own settings so that a new upstream warning never blocks our build.
set(AGENT_DB_DOCTOR_WARNINGS)
if(MSVC)
    list(APPEND AGENT_DB_DOCTOR_WARNINGS
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /wd4996
    )
    if(AGENT_DB_DOCTOR_ENABLE_WERROR)
        list(APPEND AGENT_DB_DOCTOR_WARNINGS /WX)
    endif()
else()
    list(APPEND AGENT_DB_DOCTOR_WARNINGS
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wdouble-promotion
        -Wformat=2
    )
    # Clang can report the one shadowing case that is an actual bug, a field
    # initialised from itself, without also reporting every constructor parameter
    # that shares its member's name. That is the idiomatic form of an initialiser
    # list and the specification's own example uses it, so plain `-Wshadow` would be
    # noise. GCC has no separate switch for it, so it gets nothing rather than a flag
    # it does not recognise.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND AGENT_DB_DOCTOR_WARNINGS -Wshadow-field-in-constructor)
    endif()
    if(AGENT_DB_DOCTOR_ENABLE_WERROR)
        list(APPEND AGENT_DB_DOCTOR_WARNINGS -Werror)
    endif()
endif()
# Every source file is listed explicitly. A glob would silently accept a file
# that was never reviewed.
set(AGENT_DB_DOCTOR_CORE_SOURCES
    src/AgentDbDoctor/Common/Exceptions.cpp
    src/AgentDbDoctor/Common/Logger.cpp
    src/AgentDbDoctor/Persistence/Database.cpp
    src/AgentDbDoctor/Persistence/Statement.cpp
    src/AgentDbDoctor/FileSystem/DiskSpaceChecker.cpp
    src/AgentDbDoctor/FileSystem/FileLockDetector.cpp
    src/AgentDbDoctor/Agents/AgentRegistry.cpp
    src/AgentDbDoctor/Agents/OpenCodeAgent.cpp
    src/AgentDbDoctor/Agents/CodexAgent.cpp
    src/AgentDbDoctor/Agents/ClaudeCodeAgent.cpp
    src/AgentDbDoctor/Agents/CursorAgent.cpp
    src/AgentDbDoctor/Agents/ContinueAgent.cpp
    src/AgentDbDoctor/Agents/AiderAgent.cpp
    src/AgentDbDoctor/Doctor/AgentDatabaseDoctor.cpp
    src/AgentDbDoctor/Doctor/DiagnosisReport.cpp
    src/AgentDbDoctor/Doctor/SafetyChecks.cpp
    src/AgentDbDoctor/Doctor/StatisticsReader.cpp
    src/AgentDbDoctor/Recovery/KnownFailurePatterns.cpp
    src/AgentDbDoctor/Recovery/RecoveryRecommendation.cpp
    src/AgentDbDoctor/Recovery/RecoveryPlaybook.cpp
    src/AgentDbDoctor/Presentation/ConsoleReport.cpp
    src/AgentDbDoctor/Presentation/HumanReadableFormatting.cpp
    src/AgentDbDoctor/Presentation/ProgressBar.cpp
    src/AgentDbDoctor/Command/DiagnoseCommand.cpp
    src/AgentDbDoctor/Command/DestructiveOperationGuard.cpp
    src/AgentDbDoctor/Command/PruneCommand.cpp
    src/AgentDbDoctor/Command/CompactCommand.cpp
    src/AgentDbDoctor/Command/RecoverCommand.cpp
    src/AgentDbDoctor/Command/ServiceCommand.cpp
    src/AgentDbDoctor/Service/ServiceConfiguration.cpp
    src/AgentDbDoctor/Service/ServiceManager.cpp
)
# The platform specific translation units are chosen at configure time so that
# exactly one implementation is compiled into the binary.
if(WIN32)
    list(APPEND AGENT_DB_DOCTOR_CORE_SOURCES
        src/AgentDbDoctor/FileSystem/PlatformMetadataWindows.cpp
        src/AgentDbDoctor/Service/ServiceManagerWindows.cpp
    )
    set(AGENT_DB_DOCTOR_PLATFORM_TAG "Windows")
elseif(APPLE)
    list(APPEND AGENT_DB_DOCTOR_CORE_SOURCES
        src/AgentDbDoctor/FileSystem/PlatformMetadataMacOS.cpp
        src/AgentDbDoctor/Service/ServiceManagerMacOS.cpp
    )
    set(AGENT_DB_DOCTOR_PLATFORM_TAG "macOS")
else()
    list(APPEND AGENT_DB_DOCTOR_CORE_SOURCES
        src/AgentDbDoctor/FileSystem/PlatformMetadataLinux.cpp
        src/AgentDbDoctor/Service/ServiceManagerLinux.cpp
    )
    set(AGENT_DB_DOCTOR_PLATFORM_TAG "Linux")
endif()
add_library(agent_db_doctor_core STATIC ${AGENT_DB_DOCTOR_CORE_SOURCES})
target_include_directories(agent_db_doctor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# The formatting and logging libraries are third party code. Their headers are
# marked as system includes, so a strict warning set is never reported on code
# this project does not control.
foreach(third_party_target fmt::fmt spdlog::spdlog CLI11::CLI11)
    # A namespaced target such as `fmt::fmt` is an alias, and an alias cannot hold a
    # property, so the real target behind it is what gets set.
    get_target_property(third_party_alias_target ${third_party_target} ALIASED_TARGET)
    if(NOT third_party_alias_target)
        set(third_party_alias_target ${third_party_target})
    endif()
    get_target_property(third_party_includes ${third_party_alias_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(third_party_includes)
        set_target_properties(${third_party_alias_target} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${third_party_includes}"
        )
    endif()
endforeach()
target_link_libraries(agent_db_doctor_core
    PUBLIC sqlite3 CLI11::CLI11 fmt::fmt spdlog::spdlog
)
target_compile_options(agent_db_doctor_core PRIVATE ${AGENT_DB_DOCTOR_WARNINGS})
# `SQLITE_OMIT_LOAD_EXTENSION` and friends are private to the sqlite3 target,
# so our code does not need to repeat them.
if(NOT MSVC)
    # `-fno-exceptions` is deliberately NOT used: the specification requires
    # exceptions for unrecoverable programming errors.
    target_compile_options(agent_db_doctor_core PRIVATE -fno-rtti)
endif()
add_executable(agent-db-doctor src/main.cpp)
target_link_libraries(agent-db-doctor PRIVATE agent_db_doctor_core)
target_compile_options(agent-db-doctor PRIVATE ${AGENT_DB_DOCTOR_WARNINGS})
if(NOT MSVC)
    target_compile_options(agent-db-doctor PRIVATE -fno-rtti)
endif()
# A version banner is compiled in so `agent-db-doctor --version` never needs to
# read a file from disk.
target_compile_definitions(agent-db-doctor PRIVATE
    AGENT_DB_DOCTOR_VERSION="${PROJECT_VERSION}"
    AGENT_DB_DOCTOR_PLATFORM="${AGENT_DB_DOCTOR_PLATFORM_TAG}"
)
if(AGENT_DB_DOCTOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
message(STATUS "agent-db-doctor ${PROJECT_VERSION} configured for ${AGENT_DB_DOCTOR_PLATFORM_TAG} "
    "with build type '${CMAKE_BUILD_TYPE}'.")