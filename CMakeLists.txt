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
    # The archive is pinned by hash as well as by URL. A download that does not match
    # is treated as a failure rather than compiled, because a replaced archive is
    # exactly the supply chain problem this line exists to stop.
    URL_HASH "SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032"
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
    # Pinned to the commit behind v2.5.0 rather than the tag. A tag can be moved,
    # and what gets compiled must not be able to change underneath a build.
    GIT_TAG 4160d259d961cd393fd8d67590a8c7d210207348
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
    # Pinned to the commit behind 11.1.4; see the note above.
    GIT_TAG 123913715afeb8a437e6388b4473fcc4753e1c9a
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
    # Pinned to the commit behind v1.15.3; see the note above.
    GIT_TAG 6fa36017cfd5731d617e1a934f0e5ea9c4445b13
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
        # Pinned to the commit behind v3.8.1; see the note above.
        GIT_TAG 2b60af89e23d28eefc081bc930831ee9d45ea58b
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
    src/AgentDbDoctor/Persistence/ReadCountingVfs.cpp
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
    src/AgentDbDoctor/Agents/ClineAgent.cpp
    src/AgentDbDoctor/Agents/RooCodeAgent.cpp
    src/AgentDbDoctor/Agents/ReasonixAgent.cpp
    src/AgentDbDoctor/Agents/VscodeAgent.cpp
    src/AgentDbDoctor/Agents/GeminiAgent.cpp
    src/AgentDbDoctor/Agents/WindsurfAgent.cpp
    src/AgentDbDoctor/Agents/ZedAgent.cpp
    src/AgentDbDoctor/Agents/CopilotAgent.cpp
    src/AgentDbDoctor/Agents/WarpAgent.cpp
    src/AgentDbDoctor/Agents/KiloCodeAgent.cpp
    src/AgentDbDoctor/Agents/AugmentAgent.cpp
    src/AgentDbDoctor/Agents/TraeAgent.cpp
    src/AgentDbDoctor/Agents/PearAIAgent.cpp
    src/AgentDbDoctor/Agents/AmpAgent.cpp
    src/AgentDbDoctor/Agents/CommandCodeAgent.cpp
    src/AgentDbDoctor/Agents/PiAgent.cpp
    src/AgentDbDoctor/Agents/VoidAgent.cpp
    src/AgentDbDoctor/Agents/TabnineAgent.cpp
    src/AgentDbDoctor/Agents/CodyAgent.cpp
    src/AgentDbDoctor/Agents/CodeiumAgent.cpp
    src/AgentDbDoctor/Agents/SupermavenAgent.cpp
    src/AgentDbDoctor/Agents/BitoAgent.cpp
    src/AgentDbDoctor/Agents/CodeGPTAgent.cpp
    src/AgentDbDoctor/Agents/BlackboxAgent.cpp
    src/AgentDbDoctor/Agents/AskCodiAgent.cpp
    src/AgentDbDoctor/Agents/PhindAgent.cpp
    src/AgentDbDoctor/Agents/IntelliCodeAgent.cpp
    src/AgentDbDoctor/Agents/TabbyAgent.cpp
    src/AgentDbDoctor/Agents/PiecesAgent.cpp
    src/AgentDbDoctor/Agents/AmazonQAgent.cpp
    src/AgentDbDoctor/Agents/JunieAgent.cpp
    src/AgentDbDoctor/Agents/QoderAgent.cpp
    src/AgentDbDoctor/Agents/LingmaAgent.cpp
    src/AgentDbDoctor/Agents/ComateAgent.cpp
    src/AgentDbDoctor/Agents/DroidAgent.cpp
    src/AgentDbDoctor/Agents/GitLabDuoAgent.cpp
    src/AgentDbDoctor/Agents/WorkbuddyAgent.cpp
    src/AgentDbDoctor/Agents/KimiAgent.cpp
    src/AgentDbDoctor/Agents/KiroAgent.cpp
    src/AgentDbDoctor/Agents/QwenCodeAgent.cpp
    src/AgentDbDoctor/Agents/CodeBuddyAgent.cpp
    src/AgentDbDoctor/Agents/MarsCodeAgent.cpp
    src/AgentDbDoctor/Agents/QodoAgent.cpp
    src/AgentDbDoctor/Agents/SourceryAgent.cpp
    src/AgentDbDoctor/Agents/SnykAgent.cpp
    src/AgentDbDoctor/Agents/SonarQubeAgent.cpp
    src/AgentDbDoctor/Agents/AnythingLLMAgent.cpp
    src/AgentDbDoctor/Agents/GPT4AllAgent.cpp
    src/AgentDbDoctor/Agents/JanAgent.cpp
    src/AgentDbDoctor/Agents/LMStudioAgent.cpp
    src/AgentDbDoctor/Agents/OpenWebUIAgent.cpp
    src/AgentDbDoctor/Agents/AIChatAgent.cpp
    src/AgentDbDoctor/Agents/GptmeAgent.cpp
    src/AgentDbDoctor/Agents/LlmAgent.cpp
    src/AgentDbDoctor/Agents/ShellGPTAgent.cpp
    src/AgentDbDoctor/Agents/RaycastAgent.cpp
    src/AgentDbDoctor/Doctor/AgentDatabaseDoctor.cpp
    src/AgentDbDoctor/Doctor/DiagnosisReport.cpp
    src/AgentDbDoctor/Doctor/SchemaVersionDetector.cpp
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
    src/AgentDbDoctor/Command/CompactCommand.cpp
    src/AgentDbDoctor/Command/EventLogCompactor.cpp
    src/AgentDbDoctor/Command/VacuumCommand.cpp
    src/AgentDbDoctor/Command/RecoverCommand.cpp
    src/AgentDbDoctor/Command/ServiceCommand.cpp
    src/AgentDbDoctor/Command/BackupsCommand.cpp
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

# The macOS implementation is compiled everywhere, not only on macOS.
#
# Its code is portable POSIX, apart from one branch naming the modification time
# field, so it can be compiled on a machine that is not a Mac. That check is here
# because the alternative is what happened for a long time: the macOS files were
# never compiled by anything, and so a missing include and an unreachable
# qualification sat in them unnoticed until the project was finally built on a second
# platform. Compiling them costs two translation units and turns that class of
# mistake from "found on release day" into "found by the build".
#
# The objects are never linked. They define the same functions as whichever platform
# implementation is in use, so linking them would be a duplicate symbol error.
#
# The check runs on Linux, which has the same Berkeley derived headers macOS does.
# It is skipped on Windows, where `sys/mount.h` does not exist and the file could not
# be compiled for a reason that says nothing about the code.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(agent_db_doctor_macos_compile_check OBJECT
        src/AgentDbDoctor/FileSystem/PlatformMetadataMacOS.cpp
        src/AgentDbDoctor/Service/ServiceManagerMacOS.cpp
    )
    target_include_directories(agent_db_doctor_macos_compile_check
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(agent_db_doctor_macos_compile_check PRIVATE fmt::fmt spdlog::spdlog)
    target_compile_definitions(agent_db_doctor_macos_compile_check PRIVATE SPDLOG_COMPILED_LIB)
    target_compile_options(agent_db_doctor_macos_compile_check PRIVATE ${AGENT_DB_DOCTOR_WARNINGS})
    if(NOT MSVC)
        target_compile_options(agent_db_doctor_macos_compile_check PRIVATE -fno-rtti)
    endif()
    message(STATUS "agent-db-doctor: compiling the macOS sources as a check")
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
# The Windows icon and version stamp. The resource compiler resolves relative
# names against its working directory, so the icon path is filled in as an
# absolute path at configure time. Other platforms skip this entirely.
if(WIN32)
    string(REPLACE "." "," AGENT_DB_DOCTOR_VERSION_COMMA "${PROJECT_VERSION}")
    set(APP_ICO_ABSOLUTE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico")
    set(APP_VERSION_COMMA "${AGENT_DB_DOCTOR_VERSION_COMMA},0")
    set(APP_VERSION_DOTS "${PROJECT_VERSION}")
    set(APP_EXE_FILENAME "agent-db-doctor.exe")
    set(APP_FILE_DESCRIPTION "A doctor for AI coding agent databases.")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/agent_db_doctor_app.rc"
        @ONLY
    )
    target_sources(agent-db-doctor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/agent_db_doctor_app.rc")
endif()
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
# ---------------------------------------------------------------------------
# The graphical interface. SDL3 and Dear ImGui live here and nowhere else: the
# command line target above keeps its exact five dependencies, and a build with
# `-DAGENT_DB_DOCTOR_BUILD_GUI=OFF` never downloads them.
# ---------------------------------------------------------------------------
option(AGENT_DB_DOCTOR_BUILD_GUI "Build the SDL3 graphical interface." ON)
if(AGENT_DB_DOCTOR_BUILD_GUI)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL "https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz"
        # Pinned by hash as well as by URL, for the reason given at the SQLite
        # declaration above.
        URL_HASH "SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"
    )
    FetchContent_MakeAvailable(SDL3)
    # Dear ImGui ships no CMakeLists, so its sources are compiled directly. The
    # backend pair is the official SDL3 platform layer with the SDL_Renderer
    # layer: no OpenGL loader, nothing platform specific, the same binary
    # everywhere.
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        # Pinned to the commit behind v1.92.2; see the note at the CLI11 declaration.
        GIT_TAG 2b24f5fa71d124de108da54d9da49ea3049dc91e
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)
    add_library(imgui_lib STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
    )
    target_include_directories(imgui_lib SYSTEM PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
    )
    target_link_libraries(imgui_lib PUBLIC SDL3::SDL3)
    if(MSVC)
        target_compile_options(imgui_lib PRIVATE /W0)
    else()
        target_compile_options(imgui_lib PRIVATE -w)
    endif()
    # `stb_image` decodes the window icon PNG. It is one public domain header
    # compiled once, GUI only, and never near the core library. The header is
    # fetched at a pinned commit rather than from the moving branch head, and the
    # result is checked against a hash, so what is compiled cannot change silently.
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h"
        "${CMAKE_CURRENT_BINARY_DIR}/stb_image.h"
        EXPECTED_HASH "SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3"
        TLS_VERIFY ON
    )
    add_library(stb_image STATIC gui/StbImage.c)
    target_include_directories(stb_image SYSTEM PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
    if(MSVC)
        target_compile_options(stb_image PRIVATE /W0)
    else()
        target_compile_options(stb_image PRIVATE -w)
    endif()
    set(AGENT_DB_DOCTOR_GUI_SOURCES
        gui/GuiMain.cpp
        gui/BackgroundScan.cpp
        gui/DashboardScreen.cpp
        gui/ActionRunner.cpp
        gui/ActionScreens.cpp
    )
    add_executable(agent-db-doctor-gui ${AGENT_DB_DOCTOR_GUI_SOURCES})
    target_link_libraries(agent-db-doctor-gui PRIVATE agent_db_doctor_core imgui_lib stb_image SDL3::SDL3)
    # SDL3's headers contain inline helpers whose style the tool's own strict
    # warning set rejects on some compilers (an old style cast in one of the bit
    # helpers, for example). The headers are third party code, so their folder is
    # marked as a system folder and the strict warnings stay aimed at this
    # repository, where they belong. SDL3 exposes the public header folder
    # through its `SDL3::Headers` interface target rather than on the library
    # target itself.
    get_target_property(AGENT_DB_DOCTOR_SDL3_INCLUDE_FOLDERS SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(agent-db-doctor-gui SYSTEM PRIVATE ${AGENT_DB_DOCTOR_SDL3_INCLUDE_FOLDERS})
    target_compile_options(agent-db-doctor-gui PRIVATE ${AGENT_DB_DOCTOR_WARNINGS})
    if(NOT MSVC)
        target_compile_options(agent-db-doctor-gui PRIVATE -fno-rtti)
    endif()
    # The compiled-in asset folder is only a fallback: at run time the window
    # looks beside its own binary first, which is what makes a copied package
    # work on a machine that never saw this build directory. The rest of the
    # defines stamp the version and the platform the binary was built for.
    target_compile_definitions(agent-db-doctor-gui PRIVATE
        AGENT_DB_DOCTOR_VERSION="${PROJECT_VERSION}"
        AGENT_DB_DOCTOR_PLATFORM="${AGENT_DB_DOCTOR_PLATFORM_TAG}"
        AGENT_DB_DOCTOR_ASSET_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    # The icon the window shows is copied beside the binary, so the program
    # finds it with no search and no installation step.
    file(COPY
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/favicon-64.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/logo-512.png"
        DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
    # The shared library sits beside the executable on every platform: a missing
    # DLL error on Windows and a loader search miss on Unix are the same
    # failure, and neither should reach a user.
    add_custom_command(TARGET agent-db-doctor-gui POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3-shared>"
            "$<TARGET_FILE_DIR:agent-db-doctor-gui>"
    )
    if(NOT WIN32)
        if(APPLE)
            # A packaged copy keeps the dylib beside the binary, so the loader is
            # told to look in the executable's own folder first. Without this the
            # binary only ran inside the build tree on the machine that built it.
            set_target_properties(agent-db-doctor-gui PROPERTIES BUILD_RPATH "@executable_path")
        else()
            # The same idea on Linux, where the marker is `$ORIGIN`.
            set_target_properties(agent-db-doctor-gui PROPERTIES BUILD_RPATH "$ORIGIN")
        endif()
        # The copy above uses the file's real name, so a link under the name the
        # binary actually asks for (the library's own soname, not a guess) is
        # added beside it. Both the real file and the link travel in the package.
        add_custom_command(TARGET agent-db-doctor-gui POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "$<TARGET_FILE_NAME:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:agent-db-doctor-gui>/$<TARGET_SONAME_FILE_NAME:SDL3::SDL3-shared>"
        )
    endif()
    if(WIN32)
        string(REPLACE "." "," AGENT_DB_DOCTOR_VERSION_COMMA "${PROJECT_VERSION}")
        set(APP_ICO_ABSOLUTE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico")
        set(APP_VERSION_COMMA "${AGENT_DB_DOCTOR_VERSION_COMMA},0")
        set(APP_VERSION_DOTS "${PROJECT_VERSION}")
        set(APP_EXE_FILENAME "agent-db-doctor-gui.exe")
        set(APP_FILE_DESCRIPTION "A doctor for AI coding agent databases (graphical interface).")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/agent_db_doctor_gui_app.rc"
            @ONLY
        )
        target_sources(agent-db-doctor-gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/agent_db_doctor_gui_app.rc")
    endif()
endif()
message(STATUS "agent-db-doctor ${PROJECT_VERSION} configured for ${AGENT_DB_DOCTOR_PLATFORM_TAG} "
    "with build type '${CMAKE_BUILD_TYPE}'.")