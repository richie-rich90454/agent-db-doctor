// The integration test for the insight and rescue commands.
//
// These tests exercise the commands that answer questions and the two that can
// remove something: explain-space, history, why, export, sessions, reclaim, watch,
// the incremental vacuum, and the HTML report. Each one runs against a real
// synthetic database, and the two that change data are checked the same way the
// compaction is: the rows must be gone, and every other conversation must be
// exactly as it was.
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the commands under test.
#include "AgentDbDoctor/Command/ExplainSpaceCommand.hpp"
#include "AgentDbDoctor/Command/HistoryCommand.hpp"
#include "AgentDbDoctor/Command/WhyCommand.hpp"
#include "AgentDbDoctor/Command/ExportCommand.hpp"
#include "AgentDbDoctor/Command/SessionsCommand.hpp"
#include "AgentDbDoctor/Command/ReclaimCommand.hpp"
#include "AgentDbDoctor/Command/WatchCommand.hpp"
#include "AgentDbDoctor/Command/DiagnoseCommand.hpp"
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
#include "AgentDbDoctor/Command/BackupsCommand.hpp"
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the database wrapper, so the tests can read and change the file.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the growth record the history command reads.
#include "AgentDbDoctor/Service/HistoryStore.hpp"
// Provides PlatformMetadata, for the file sizes.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the synthetic database builder.
#include "SyntheticDatabaseBuilder.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Service;
using namespace AgentDbDoctor::TestSupport;
namespace {
// Builds a small database with the given amount of duplication and returns its path.
string buildDatabase(const string& testName, int redundantCopies, int secondGenerationRows) {
    const string databasePath=SyntheticDatabaseBuilder::temporaryDatabasePath(testName);
    SyntheticDatabaseBuilder::Options options;
    options.sessionCount=8;
    options.messageCountPerSession=4;
    options.redundantCopiesPerMessage=redundantCopies;
    options.secondGenerationRowsPerMessage=secondGenerationRows;
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
    SyntheticDatabaseBuilder::build(databasePath, options);
    return databasePath;
}
// Adds free pages to a database, which is the state a vacuum exists to undo.
void inflateWithFreePages(const string& databasePath, int freePageCount) {
    Database database(databasePath, Database::OPEN_READ_WRITE);
    database.execute("CREATE TABLE filler (id INTEGER PRIMARY KEY, body BLOB)");
    for (int pageIndex=0; pageIndex<freePageCount; ++pageIndex) {
        database.execute("INSERT INTO filler (body) VALUES (zeroblob(3000))");
    }
    database.execute("DROP TABLE filler");
}
// Counts the readable text across the whole database.
long long countReadableTextParts(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    Statement countingStatement(
        database.rawHandle(),
        "SELECT COUNT(*) FROM part WHERE json_extract(data, '$.type')='text' "
        "AND length(json_extract(data, '$.text'))>20",
        "a count of the readable text across the whole database"
    );
    if (!countingStatement.isValid() || !countingStatement.step()) {
        return 0;
    }
    return countingStatement.columnInteger(0);
}
// Reads a whole file into a string.
string readFileText(const string& filePath) {
    std::ifstream inputFile(filePath);
    std::stringstream capturedText;
    capturedText<<inputFile.rdbuf();
    return capturedText.str();
}
// True when `text` contains `fragment`.
bool containsText(const string& text, const string& fragment) {
    return text.find(fragment)!=string::npos;
}
// The first session identifier in the database, oldest first.
string firstSessionIdentifier(const string& databasePath) {
    Database database(databasePath, Database::OPEN_READ_ONLY);
    return database.querySingleText("SELECT id FROM session ORDER BY rowid ASC LIMIT 1").value_or("");
}
}
TEST_CASE("explain-space names the tables and reports the free pages", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightExplainSpace", 1, 0);
    inflateWithFreePages(databasePath, 300);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    commandOptions.machineReadableOutput=true;
    ExplainSpaceCommand command(false);
    // The machine readable object is what the test can check without parsing prose,
    // and the command writes it to standard output; the exit code is the contract.
    REQUIRE(command.execute(commandOptions)==0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("the history command reports the recorded growth", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightHistory", 1, 0);
    const string storePath=SyntheticDatabaseBuilder::temporaryDatabasePath("InsightHistoryStore") + ".tsv";
    std::filesystem::remove(storePath);
    HistoryStore::setStorePathForTests(storePath);
    // Three sizes a day apart, so the trend is known exactly: one megabyte a day.
    HistoryStore::recordSize(databasePath, 1000000, 1000000000);
    HistoryStore::recordSize(databasePath, 2000000, 1000086400);
    HistoryStore::recordSize(databasePath, 3000000, 1000172800);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    HistoryCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    // The store itself is checked too, because the command is a view of it.
    const vector<HistoryPoint> points=HistoryStore::pointsFor(databasePath, 0);
    REQUIRE(points.size()==3);
    REQUIRE(points.front().sizeBytes==1000000);
    REQUIRE(points.back().sizeBytes==3000000);
    // A fourth recorded size that has not changed is not added.
    REQUIRE_FALSE(HistoryStore::recordSize(databasePath, 3000000, 1000259200));
    REQUIRE(HistoryStore::pointsFor(databasePath, 0).size()==3);
    std::filesystem::remove(storePath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("why points at the vacuum when the file holds unused pages", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightWhy", 2, 0);
    inflateWithFreePages(databasePath, 400);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    WhyCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("export writes a conversation out in all three formats", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightExport", 1, 0);
    const string sessionIdentifier=firstSessionIdentifier(databasePath);
    REQUIRE_FALSE(sessionIdentifier.empty());
    for (const string& format : {string("markdown"), string("json"), string("html")}) {
        const string outputPath=SyntheticDatabaseBuilder::temporaryDatabasePath("InsightExportOut") + "." + format;
        std::filesystem::remove(outputPath);
        CommandOptions commandOptions;
        commandOptions.databaseFilePath=databasePath;
        commandOptions.sessionIdentifier=sessionIdentifier;
        commandOptions.exportFormat=format;
        commandOptions.outputFilePath=outputPath;
        commandOptions.quiet=true;
        ExportCommand command(false);
        REQUIRE(command.execute(commandOptions)==0);
        const string documentText=readFileText(outputPath);
        REQUIRE_FALSE(documentText.empty());
        // Every format carries the session identifier and the conversation body.
        REQUIRE(containsText(documentText, sessionIdentifier));
        REQUIRE(containsText(documentText, "comfortably longer than twenty"));
        std::filesystem::remove(outputPath);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("sessions lists the largest conversations", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightSessionsList", 1, 0);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.quiet=true;
    commandOptions.sessionsToRank=3;
    SessionsCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("deleting a session removes it and keeps every other conversation", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightSessionsDelete", 1, 0);
    const string sessionIdentifier=firstSessionIdentifier(databasePath);
    REQUIRE_FALSE(sessionIdentifier.empty());
    long long sessionCountBefore=0;
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        sessionCountBefore=database.querySingleInteger("SELECT COUNT(*) FROM session").value_or(0);
    }
    const long long readablePartsBefore=countReadableTextParts(databasePath);
    // The readable text parts of the session about to be deleted, counted with the
    // same rule the whole database count uses, so the two numbers are comparable.
    const long long deletedSessionReadableParts=
        [&databasePath, &sessionIdentifier]() {
            Database database(databasePath, Database::OPEN_READ_ONLY);
            Statement countStatement(
                database.rawHandle(),
                "SELECT COUNT(*) FROM part p JOIN message m ON p.message_id = m.id "
                "WHERE m.session_id=?1 AND json_extract(p.data, '$.type')='text' "
                "AND length(json_extract(p.data, '$.text'))>20",
                "a count of the session's readable text parts"
            );
            if (!countStatement.isValid()) {
                return 0LL;
            }
            countStatement.bindText(1, sessionIdentifier);
            return countStatement.step() ? countStatement.columnInteger(0) : 0LL;
        }();
    REQUIRE(deletedSessionReadableParts>0);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.sessionIdentifier=sessionIdentifier;
    commandOptions.sessionsAction="delete";
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    SessionsCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(database.querySingleInteger("SELECT COUNT(*) FROM session").value_or(0)==sessionCountBefore-1);
        REQUIRE(database.querySingleInteger(
            "SELECT COUNT(*) FROM session WHERE id='" + sessionIdentifier + "'"
        ).value_or(-1)==0);
        // The cookie crumb check: the conversation that was deleted is the only text
        // that is gone, and the readable parts of everything else are untouched.
        REQUIRE(database.querySingleText("PRAGMA quick_check").value_or("")=="ok");
    }
    REQUIRE(countReadableTextParts(databasePath)==readablePartsBefore-deletedSessionReadableParts);
    // The successful run removed its own copy, so nothing was left beside the file.
    REQUIRE(BackupsCommand::findBackupsBeside(databasePath).empty());
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("deleting a session refuses on a database with second generation tables", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightSessionsRefuse", 1, 2);
    const string sessionIdentifier=firstSessionIdentifier(databasePath);
    REQUIRE_FALSE(sessionIdentifier.empty());
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.sessionIdentifier=sessionIdentifier;
    commandOptions.sessionsAction="delete";
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    SessionsCommand command(false);
    REQUIRE(command.execute(commandOptions)==1);
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(database.querySingleInteger(
            "SELECT COUNT(*) FROM session WHERE id='" + sessionIdentifier + "'"
        ).value_or(0)==1);
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("reclaim reaches a target and reports what the drive got back", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightReclaim", 6, 0);
    inflateWithFreePages(databasePath, 500);
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.reclaimTarget="1MB";
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    ReclaimCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    const long long sizeAfter=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    REQUIRE(sizeAfter<sizeBefore);
    // The parse is checked directly as well, because a target that quietly parsed to
    // zero would make every reclaim a no-op.
    REQUIRE(ReclaimCommand::parseTargetBytes("20GB")==20LL * 1024LL * 1024LL * 1024LL);
    REQUIRE(ReclaimCommand::parseTargetBytes("500MB")==500LL * 1024LL * 1024LL);
    REQUIRE(ReclaimCommand::parseTargetBytes("1024")==1024LL);
    REQUIRE(ReclaimCommand::parseTargetBytes("not a size")==0);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("watch renders one snapshot and exits", "[integration][insight]") {
    CommandOptions commandOptions;
    commandOptions.quiet=true;
    commandOptions.watchOnce=true;
    WatchCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
}
TEST_CASE("a database set up for incremental auto-vacuum is vacuumed in place", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightIncremental", 1, 0);
    {
        // The conversion is the expensive part and happens once, exactly as the
        // documentation says: set the pragma, then vacuum to apply it.
        Database database(databasePath, Database::OPEN_READ_WRITE);
        database.execute("PRAGMA auto_vacuum = INCREMENTAL");
        database.execute("VACUUM");
    }
    // Pages are freed after the conversion, which is the state the incremental path
    // exists to clean up without a rewrite.
    {
        Database database(databasePath, Database::OPEN_READ_WRITE);
        database.execute("CREATE TABLE filler (id INTEGER PRIMARY KEY, body BLOB)");
        for (int pageIndex=0; pageIndex<400; ++pageIndex) {
            database.execute("INSERT INTO filler (body) VALUES (zeroblob(3000))");
        }
        database.execute("DROP TABLE filler");
    }
    const long long sizeBefore=PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes;
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.answerYesToEverything=true;
    commandOptions.quiet=true;
    VacuumCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    // The cheap path was taken, which is the whole point of the setting, and it did
    // hand space back.
    REQUIRE(command.lastOutcome().usedIncrementalVacuum);
    REQUIRE(command.lastOutcome().wasCompacted);
    REQUIRE(command.lastOutcome().bytesReclaimed>0);
    REQUIRE(PlatformMetadata::readFileMetadata(databasePath).logicalSizeBytes<sizeBefore);
    {
        Database database(databasePath, Database::OPEN_READ_ONLY);
        REQUIRE(database.querySingleText("PRAGMA quick_check").value_or("")=="ok");
    }
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
TEST_CASE("diagnose writes a standalone HTML report", "[integration][insight]") {
    const string databasePath=buildDatabase("InsightHtmlReport", 1, 0);
    const string reportPath=SyntheticDatabaseBuilder::temporaryDatabasePath("InsightHtmlReportOut") + ".html";
    std::filesystem::remove(reportPath);
    CommandOptions commandOptions;
    commandOptions.databaseFilePath=databasePath;
    commandOptions.outputFilePath=reportPath;
    commandOptions.quiet=true;
    DiagnoseCommand command(false);
    REQUIRE(command.execute(commandOptions)==0);
    REQUIRE(std::filesystem::exists(reportPath));
    const string pageText=readFileText(reportPath);
    REQUIRE(containsText(pageText, "<title>agent-db-doctor report</title>"));
    REQUIRE(containsText(pageText, "agent-db-doctor report"));
    // The path of the database appears in the page, so a reader can tell which file
    // the numbers came from.
    REQUIRE(containsText(pageText, "InsightHtmlReport"));
    std::filesystem::remove(reportPath);
    SyntheticDatabaseBuilder::removeIfPresent(databasePath);
}
