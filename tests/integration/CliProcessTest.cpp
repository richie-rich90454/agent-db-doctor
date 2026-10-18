// The process level test for the command line program.
//
// Every other test calls the command classes directly. This one runs the real
// binary, which is the only way to check the things that live in the process rather
// than in a class: the exit codes a script reads, the dispatch from a subcommand
// name to a command, and the machine readable output as a script receives it.
//
// The path to the binary arrives in the `AGENT_DB_DOCTOR_CLI` environment variable,
// which the build sets from the built target. The output of each run is captured
// through a temporary file, so no platform specific pipe handling is needed.
#include <cstdlib>
#include <string>
// Provides `ifstream`, used to read what the child process printed.
#include <fstream>
// Provides `stringstream`, used to read a file into a string in one step.
#include <sstream>
// Provides `remove` and `exists`, which clear and check the test's own files.
#include <filesystem>
// Provides `vector`, used for the wide character buffers on Windows.
#include <vector>
// Provides `memset`, used to zero the Windows structures before they are filled.
#include <cstring>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
#if defined(_WIN32)
// `WIN32_LEAN_AND_MEAN` keeps the RPC headers out, whose C `byte` typedef clashes
// with `std::byte`; `NOMINMAX` keeps the `min` and `max` macros away from the
// standard library. Both are needed before the header is included, and both are
// guarded because the toolchain may define them already.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Declares the process and file functions used to run the program directly.
#include <windows.h>
#else
// Provides `popen` and `pclose`, which run a program and read what it printed.
#include <cstdio>
#endif
// Provides the environment variable lookup and the file metadata.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the database wrapper, for counting rows around a preview.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
#if !defined(_WIN32)
// Provides `WEXITSTATUS`, which extracts the exit code from what `system` returned.
#include <sys/wait.h>
#endif
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
namespace {
// The path of the built command line program, from the environment.
string cliPath() {
    string path;
    PlatformMetadata::readEnvironmentVariable("AGENT_DB_DOCTOR_CLI", path);
    return path;
}
// Runs the program with `arguments` and returns its exit code, with everything it
// printed, standard error included, captured into `outputOut`.
//
// On Windows the process is created directly, bypassing the shell entirely. That is
// deliberate: `system` and `popen` both go through whatever program `COMSPEC`
// names, and a developer whose shell is not the standard one, which is common on
// Windows, would get a mysterious failure instead of a test result.
int runCli(const string& arguments, string& outputOut) {
    const string outputPath=SyntheticDatabaseBuilder::temporaryDatabasePath("CliOutput") + ".txt";
    std::filesystem::remove(outputPath);
#if defined(_WIN32)
    // Both the command line and the output path are converted to the wide
    // characters Windows uses for paths and command lines.
    const auto toWide=[](const string& text) {
        const int wideLength=MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        vector<wchar_t> wideText(static_cast<size_t>(wideLength) + 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wideText.data(), wideLength);
        return wideText;
    };
    SECURITY_ATTRIBUTES securityAttributes;
    memset(&securityAttributes, 0, sizeof(securityAttributes));
    securityAttributes.nLength=sizeof(securityAttributes);
    securityAttributes.bInheritHandle=TRUE;
    vector<wchar_t> wideOutputPath=toWide(outputPath);
    const HANDLE outputHandle=CreateFileW(
        wideOutputPath.data(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &securityAttributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (outputHandle==INVALID_HANDLE_VALUE) {
        return -1;
    }
    // The command line begins with the quoted program path, and the arguments follow
    // exactly as the test wrote them.
    const string commandLine=string("\"") + cliPath() + "\" " + arguments;
    vector<wchar_t> wideCommandLine=toWide(commandLine);
    STARTUPINFOW startupInformation;
    memset(&startupInformation, 0, sizeof(startupInformation));
    startupInformation.cb=sizeof(startupInformation);
    startupInformation.dwFlags=STARTF_USESTDHANDLES;
    startupInformation.hStdOutput=outputHandle;
    startupInformation.hStdError=outputHandle;
    startupInformation.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION processInformation;
    memset(&processInformation, 0, sizeof(processInformation));
    const BOOL processWasCreated=CreateProcessW(
        nullptr,
        wideCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInformation,
        &processInformation
    );
    CloseHandle(outputHandle);
    if (!processWasCreated) {
        return -1;
    }
    WaitForSingleObject(processInformation.hProcess, INFINITE);
    DWORD exitCode=1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hProcess);
    CloseHandle(processInformation.hThread);
    {
        std::ifstream outputFile(outputPath);
        std::stringstream capturedText;
        capturedText<<outputFile.rdbuf();
        outputOut=capturedText.str();
    }
    std::filesystem::remove(outputPath);
    return static_cast<int>(exitCode);
#else
    // On the Unix family a shell pipe is the simplest portable answer, and `/bin/sh`
    // is guaranteed to be there.
    const string commandLine=string("\"") + cliPath() + "\" " + arguments + " 2>&1";
    FILE* outputPipe=popen(commandLine.c_str(), "r");
    if (outputPipe==nullptr) {
        return -1;
    }
    char readBuffer[4096];
    size_t bytesRead=0;
    while ((bytesRead=fread(readBuffer, 1, sizeof(readBuffer), outputPipe))>0) {
        outputOut.append(readBuffer, bytesRead);
    }
    const int status=pclose(outputPipe);
    return WEXITSTATUS(status);
#endif
}
// True when `text` contains `fragment`.
bool contains(const string& text, const string& fragment) {
    return text.find(fragment)!=string::npos;
}
}
TEST_CASE("the program reports its version and exits zero", "[integration][cli]") {
    REQUIRE_FALSE(cliPath().empty());
    string output;
    REQUIRE(runCli("--version", output)==0);
    REQUIRE(contains(output, "1.0.0"));
}
TEST_CASE("the help text lists the commands and exits zero", "[integration][cli]") {
    string output;
    REQUIRE(runCli("--help", output)==0);
    REQUIRE(contains(output, "diagnose"));
    REQUIRE(contains(output, "vacuum"));
    REQUIRE(contains(output, "backups"));
}
TEST_CASE("an unknown subcommand is a usage error with exit code two", "[integration][cli]") {
    string output;
    REQUIRE(runCli("compacct", output)==2);
    REQUIRE(contains(output, "problem with the command line"));
}
TEST_CASE("diagnose prints machine readable objects and ends with an exit object", "[integration][cli]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CliDiagnose");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=4;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    string output;
    const int exitCode=runCli(
        "diagnose -d \"" + databasePath + "\" --machine-readable --quiet",
        output
    );
    REQUIRE(exitCode==0);
    // The database object is the whole point of the machine readable mode: every
    // number a script or the graphical interface needs, in one line.
    REQUIRE(contains(output, "\"event\":\"database\""));
    REQUIRE(contains(output, "\"pageCount\":"));
    REQUIRE(contains(output, "\"integrity\":"));
    // The last object says the run is over, so a script knows it read everything.
    REQUIRE(contains(output, "\"event\":\"exit\",\"exitCode\":0"));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a missing database is reported with exit code one", "[integration][cli]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("CliMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    string output;
    REQUIRE(runCli("diagnose -d \"" + missingPath + "\" --quiet", output)==1);
    REQUIRE_FALSE(std::filesystem::exists(missingPath));
}
TEST_CASE("a preview through the binary changes nothing at all", "[integration][cli]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CliPreview");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    long long eventRowsBefore=0;
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        eventRowsBefore=database.querySingleInteger("SELECT COUNT(*) FROM event").value_or(0);
    }
    string output;
    const int exitCode=runCli(
        "compact -d \"" + databasePath + "\" --dry-run --machine-readable --quiet --yes",
        output
    );
    REQUIRE(exitCode==0);
    REQUIRE(contains(output, "\"previewOnly\":true"));
    // The promise of a preview, checked from outside the process: the file is the
    // same size and holds the same number of rows.
    REQUIRE(PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes==sizeBefore);
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(database.querySingleInteger("SELECT COUNT(*) FROM event").value_or(0)==eventRowsBefore);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("a real run through the binary reports its numbers and exits zero", "[integration][cli]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CliCompact");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=3;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    string output;
    const int exitCode=runCli(
        "compact -d \"" + databasePath + "\" --yes --machine-readable --quiet",
        output
    );
    REQUIRE(exitCode==0);
    REQUIRE(contains(output, "\"event\":\"result\",\"command\":\"compact\""));
    REQUIRE(contains(output, "\"rolledBack\":false"));
    REQUIRE(contains(output, "\"event\":\"exit\",\"exitCode\":0"));
    // The safety copy is removed after a successful run, so the machine readable
    // result says so and nothing is left beside the database.
    REQUIRE(contains(output, "\"safetyCopyRemoved\":true"));
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("deleting backups without naming any is a usage error", "[integration][cli]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("CliBackupsDelete");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=4;
    options.messageCountPerSession=2;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    string output;
    REQUIRE(runCli("backups delete -d \"" + databasePath + "\" --yes --quiet", output)==2);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the agents command lists agents in machine readable form", "[integration][cli]") {
    string output;
    REQUIRE(runCli("agents --machine-readable --quiet", output)==0);
    REQUIRE(contains(output, "\"event\":\"agent\""));
    REQUIRE(contains(output, "\"event\":\"result\",\"command\":\"agents\""));
    REQUIRE(contains(output, "\"event\":\"exit\",\"exitCode\":0"));
}
