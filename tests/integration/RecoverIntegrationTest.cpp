// The integration test for the recover command.
//
// The recover command exists because of a real loss, so its test is about the two
// things that would have saved it: telling the user to stop touching the disk, and
// saying out loud when the data is gone. A tool that stayed quiet about both would
// have let the same thing happen again.
#include <string>
// Provides 
#include <filesystem>
// Provides ofstream, used to write a file that is not a database.
#include <fstream>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the command under test.
#include "AgentDbDoctor/Command/RecoverCommand.hpp"
// Provides the options the command reads.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the pattern classifier and the plan builder the command uses.
#include "AgentDbDoctor/Recovery/KnownFailurePatterns.hpp"
#include "AgentDbDoctor/Recovery/RecoveryPlaybook.hpp"
// Provides the database wrapper, so the test can read the file itself.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Recovery;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::TestSupport;
TEST_CASE("recover tells a healthy database that nothing needs doing", "[integration][recover]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("RecoverHealthy");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    RecoverCommand command(false);
    // A healthy database has nothing to recover, and the tool must not pretend
    // otherwise or suggest a repair that would be pointless.
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE_FALSE(command.lastOutcome().dataIsUnrecoverable);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("recover explains a file that is not a database", "[integration][recover]") {
    const string textFilePath=SyntheticDatabaseBuilder::temporaryDatabasePath("RecoverNotDatabase");
    SyntheticDatabaseBuilder::removeIfPresent(textFilePath);
    {
        std::ofstream textFile(textFilePath);
        textFile << "this is not a database at all";
    }
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=textFilePath;
    commandOptions.quiet=true;
    RecoverCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    // A file whose beginning is gone cannot be read by any tool, and the tool has to
    // say so rather than promising a repair.
    REQUIRE(command.lastOutcome().dataIsUnrecoverable);
    std::filesystem::remove(textFilePath);
}
TEST_CASE("recover refuses a file that is not there", "[integration][recover]") {
    const string missingPath=SyntheticDatabaseBuilder::temporaryDatabasePath("RecoverMissing");
    SyntheticDatabaseBuilder::removeIfPresent(missingPath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=missingPath;
    commandOptions.quiet=true;
    RecoverCommand command(false);
    // The path is the one thing the user has to supply, so a wrong one has to be
    // reported rather than guessed at.
    REQUIRE(command.execute(commandOptions)==1);
}
TEST_CASE("recover leaves the file exactly as it found it", "[integration][recover]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("RecoverNoChange");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=5;
    options.messageCountPerSession=3;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database beforeDatabase(databasePath, Database::OPEN_READ_ONLY);
    const long long partCountBefore=beforeDatabase.querySingleInteger("SELECT COUNT(*) FROM part")
        .value_or(0);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    RecoverCommand command(false);
    command.execute(commandOptions);
    // Running the recovery command without asking for a rebuild must write nothing,
    // because a user who only wanted to understand what happened should not have
    // their database touched.
    Database afterDatabase(databasePath, Database::OPEN_READ_ONLY);
    const long long partCountAfter=afterDatabase.querySingleInteger("SELECT COUNT(*) FROM part")
        .value_or(0);
    REQUIRE(partCountAfter==partCountBefore);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("recover builds a working copy only when it is asked", "[integration][recover]") {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath("RecoverRebuild");
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=10;
    options.messageCountPerSession=5;
    options.redundantCopiesPerMessage=0;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    Database beforeDatabase(databasePath, Database::OPEN_READ_ONLY);
    const long long sessionCountBefore=beforeDatabase.querySingleInteger("SELECT COUNT(*) FROM session")
        .value_or(0);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    commandOptions.rebuildWorkingCopy=true;
    RecoverCommand command(false);
    command.execute(commandOptions);
    // The rebuilt file is a new one beside the original. The original is never
    // modified, which is the promise the tool makes to someone whose database is
    // already damaged.
    const string rebuiltPath=command.lastOutcome().rebuiltDatabasePath;
    REQUIRE_FALSE(rebuiltPath.empty());
    REQUIRE(std::filesystem::exists(rebuiltPath));
    // Every session the original held must be in the copy, and the original must be
    // exactly as it was. Both files are read inside their own blocks so that the
    // handles are closed before the copy is cleaned up, which Windows requires.
    {
        Database rebuiltDatabase(rebuiltPath, Database::OPEN_READ_ONLY);
        const long long rebuiltSessionCount=rebuiltDatabase
            .querySingleInteger("SELECT COUNT(*) FROM session")
            .value_or(0);
        REQUIRE(rebuiltSessionCount==sessionCountBefore);
    }
    {
        Database originalDatabase(databasePath, Database::OPEN_READ_ONLY);
        const long long originalSessionCount=originalDatabase
            .querySingleInteger("SELECT COUNT(*) FROM session")
            .value_or(0);
        REQUIRE(originalSessionCount==sessionCountBefore);
    }
    std::filesystem::remove(rebuiltPath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the plan names the failure and says what cannot be fixed", "[integration][recover]") {
    // The plan is the part a person actually reads, so its wording is tested as
    // carefully as its contents.
    const vector<RecoveryRecommendation> plan=RecoveryPlaybook::buildPlanForUnreadableDatabase(
        "/home/ann/.local/share/opencode/opencode.db",
        266,
        "The operating system cannot read this file."
    );
    REQUIRE(plan.size()>=4);
    REQUIRE(plan.front().title().find("Stop")!=string::npos);
    REQUIRE(plan[1].explanation().find("Copy")!=string::npos);
    REQUIRE(plan.back().isFinalWord());
    // The last step must say plainly that some of the data cannot come back, so the
    // user stops hoping for a command that would work.
    REQUIRE(plan.back().explanation().find("No program can restore")!=string::npos);
    // The numeric code appears only as a search term, never on its own.
    REQUIRE(plan[2].explanation().find("266")!=string::npos);
}
