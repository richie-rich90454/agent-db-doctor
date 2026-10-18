// The integration test for interruptions.
//
// A compaction is the only operation that deletes rows, and the promise the tool
// makes about it is that an interruption leaves a database that is consistent and
// holds every word of every conversation. On a real machine an interruption is a
// power loss; in a test it is the process being killed while it works, which is the
// same thing from the file's point of view.
//
// The kill is done with the Unix process functions, and the test is therefore only
// compiled there. Windows has no portable way to kill another process mid-write
// from a test, and the same guarantee is covered on Linux in continuous
// integration, which is where the file system behaviour that matters is exercised.
#include <string>
// Provides `remove` and `exists`, which clear and check the test's own files.
#include <filesystem>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the commands under test.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
#include "AgentDbDoctor/Command/CommandOptions.hpp"
#include "AgentDbDoctor/Command/BackupsCommand.hpp"
// Provides the database wrapper, so the test can read the file itself.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the platform metadata, for the environment and the clock.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
#if !defined(_WIN32)
// Provides `fork`, `execl`, `kill`, `waitpid`, and the sleep.
#include <unistd.h>
// Provides the signal numbers.
#include <signal.h>
// Provides `WEXITSTATUS` and `WIFEXITED`.
#include <sys/wait.h>
// Provides `freopen`, used to send the child's output nowhere.
#include <cstdio>
#endif
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Removes everything a run may have left beside the database.
void removeEverythingBeside(const string& databasePath) {
    for (const BackupFile& backup : BackupsCommand::findBackupsBeside(databasePath)) {
        std::filesystem::remove(backup.filePath);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
}
#if !defined(_WIN32)
namespace {
// Counts the readable text across the whole database, the direct measure of whether
// a conversation survived.
long long countReadableTextParts(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    Statement countingStatement(
        database.rawHandle(),
        "SELECT COUNT(*) FROM part "
        "WHERE json_extract(data, '$.type')='text' "
        "AND length(json_extract(data, '$.text'))>20",
        "a count of the readable text across the whole database"
    );
    if (!countingStatement.isValid() || !countingStatement.step()) {
        return 0;
    }
    return countingStatement.columnInteger(0);
}
}
TEST_CASE("a compaction killed partway leaves a consistent database", "[integration][interrupt]") {
    string cliPath;
    PlatformMetadata::readEnvironmentVariable("AGENT_DB_DOCTOR_CLI", cliPath);
    REQUIRE_FALSE(cliPath.empty());
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("InterruptKill");
    SyntheticDatabaseBuilder::Options options;
    // Enough redundant rows that the removal loop runs for long enough to be killed
    // inside it: forty sessions of a hundred and twenty messages with twenty five
    // older copies each is three hundred and sixty thousand rows to remove.
    options.sessionCount=40;
    options.messageCountPerSession=120;
    options.partCountPerMessage=2;
    options.redundantCopiesPerMessage=25;
    options.bytesPerEventRow=64;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    const long long redundantRowsBefore=SyntheticDatabaseBuilder::build(databasePath, options);
    REQUIRE(redundantRowsBefore>100000);
    const long long readablePartsBefore=countReadableTextParts(databasePath);
    // The compaction runs in a child process so it can be killed mid-write.
    const pid_t childProcess=fork();
    if (childProcess==0) {
        // The child prints nothing; its exit code is not what this test is about.
        // `freopen` reports failure by returning null, which is checked only to
        // satisfy the compiler: there is nothing sensible to do about it here.
        if (freopen("/dev/null", "w", stdout)==nullptr || freopen("/dev/null", "w", stderr)==nullptr) {
            _exit(127);
        }
        execl(
            cliPath.c_str(),
            cliPath.c_str(),
            "compact",
            "-d",
            databasePath.c_str(),
            "--yes",
            "--quiet",
            static_cast<char*>(nullptr)
        );
        _exit(127);
    }
    REQUIRE(childProcess>0);
    // It is killed while it is working, which is what a power loss looks like to a
    // file. The delay is long enough to reach the removal loop on a real machine and
    // short enough that the test stays quick whatever phase it lands in.
    PlatformMetadata::sleepMilliseconds(700);
    kill(childProcess, SIGKILL);
    int waitStatus=0;
    waitpid(childProcess, &waitStatus, 0);
    // The first promise: the database is structurally sound after an interruption.
    {
        Database interruptedDatabase(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(interruptedDatabase.querySingleText("PRAGMA quick_check").value_or("")=="ok");
    }
    // The second promise: not one word of the conversation was lost.
    REQUIRE(countReadableTextParts(databasePath)==readablePartsBefore);
    // And a second run finishes the job and leaves the same content behind.
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    CompactCommand secondCommand(false);
    REQUIRE(secondCommand.execute(commandOptions)==0);
    REQUIRE(countReadableTextParts(databasePath)==readablePartsBefore);
    {
        Database finishedDatabase(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(finishedDatabase.querySingleText("PRAGMA quick_check").value_or("")=="ok");
    }
    removeEverythingBeside(databasePath);
}
#endif
TEST_CASE("a vacuum refuses while another process holds a change open", "[integration][interrupt]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("InterruptHeldOpen");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=6;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=1;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
#if defined(_WIN32)
    {
        // On Windows the file sharing rules let the probe see any open handle, this
        // process included, so the holder can live in the same process.
        Database holdingConnection(databasePath, Database::OPEN_READ_WRITE);
        holdingConnection.setBusyTimeoutMilliseconds(2000);
        holdingConnection.execute("CREATE TABLE IF NOT EXISTS holder (id INTEGER)");
        holdingConnection.execute("INSERT INTO holder (id) VALUES (1)");
        CommandOptions refusedOptions;
        refusedOptions.databaseFilePath=databasePath;
        refusedOptions.answerYesToEverything=true;
        refusedOptions.quiet=true;
        VacuumCommand refusedVacuum(false);
        REQUIRE(refusedVacuum.execute(refusedOptions)==1);
        REQUIRE_FALSE(refusedVacuum.lastOutcome().wasCompacted);
        // The database was not touched: the marker row is still there.
        REQUIRE(holdingConnection.querySingleInteger("SELECT COUNT(*) FROM holder").value_or(0)==1);
    }
#else
    {
        // On the Unix family the file locks alone cannot see an idle connection, so
        // the holder is a real second process with a transaction open, which is what
        // an actively writing agent looks like. A pipe reports when the transaction
        // is in place, so the test never races the child.
        int readinessPipe[2]={-1, -1};
        REQUIRE(pipe(readinessPipe)==0);
        const pid_t childProcess=fork();
        if (childProcess==0) {
            close(readinessPipe[0]);
            {
                Database holdingConnection(databasePath, Database::OPEN_READ_WRITE);
                holdingConnection.setBusyTimeoutMilliseconds(2000);
                holdingConnection.execute("BEGIN IMMEDIATE");
                holdingConnection.execute("CREATE TABLE IF NOT EXISTS holder (id INTEGER)");
                holdingConnection.execute("INSERT INTO holder (id) VALUES (1)");
                const char readySignal='r';
                if (write(readinessPipe[1], &readySignal, 1)!=1) {
                    _exit(127);
                }
                // The transaction is held open until the parent kills this process.
                pause();
            }
            _exit(0);
        }
        REQUIRE(childProcess>0);
        close(readinessPipe[1]);
        char readySignal=0;
        REQUIRE(read(readinessPipe[0], &readySignal, 1)==1);
        close(readinessPipe[0]);
        CommandOptions refusedOptions;
        refusedOptions.databaseFilePath=databasePath;
        refusedOptions.answerYesToEverything=true;
        refusedOptions.quiet=true;
        VacuumCommand refusedVacuum(false);
        REQUIRE(refusedVacuum.execute(refusedOptions)==1);
        REQUIRE_FALSE(refusedVacuum.lastOutcome().wasCompacted);
        kill(childProcess, SIGKILL);
        int waitStatus=0;
        waitpid(childProcess, &waitStatus, 0);
    }
#endif
    // Once the holder is gone, the same command succeeds. This is what proves the
    // refusal above was about the holder rather than about the database. The child
    // was killed mid-transaction on the Unix family, so a write ahead log from it is
    // part of what the successful run has to deal with, which is the state an
    // unclean agent shutdown leaves.
    CommandOptions vacuumOptions;
    vacuumOptions.databaseFilePath=databasePath;
    vacuumOptions.answerYesToEverything=true;
    vacuumOptions.quiet=true;
    VacuumCommand successfulVacuum(false);
    REQUIRE(successfulVacuum.execute(vacuumOptions)==0);
    REQUIRE(successfulVacuum.lastOutcome().wasCompacted);
    removeEverythingBeside(databasePath);
}
