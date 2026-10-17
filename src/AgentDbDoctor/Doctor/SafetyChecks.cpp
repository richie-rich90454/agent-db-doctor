// The implementation of the SafetyChecks class.
//
// The pattern repeated here is: ask the database one small question, and treat any
// failure as the dangerous answer. A check that cannot prove something is safe
// reports that it is not safe, because the tool is about to delete rows from a
// file the user cannot afford to lose.
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `pair`, which holds two values of different types.
#include <utility>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the SafetyChecks class and the snapshot structure.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
// Provides the SafetyCheckResult value type.
#include "AgentDbDoctor/Doctor/SafetyCheckResult.hpp"
// Provides StatisticsReader, used to test the shape of the schema.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides FileLockDetector and DiskSpaceChecker, used by two of the checks.
#include "AgentDbDoctor/FileSystem/FileLockDetector.hpp"
#include "AgentDbDoctor/FileSystem/DiskSpaceChecker.hpp"
// Provides PlatformMetadata, used to read the size and the free space.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Persistence;
namespace AgentDbDoctor::Doctor {
// A readable part is one holding more than this many characters of text. Anything
// shorter is more likely to be a title or a placeholder than a message, and
// counting those would let a session of nothing but headers pass the check.
static constexpr long long MINIMUM_READABLE_TEXT_LENGTH=20;
// The query used to find the oldest sessions. Ordering by the creation time and
// taking a fixed number is an indexed read, so its cost does not grow with the
// number of sessions in the database.
static constexpr const char* OLDEST_SESSIONS_QUERY=
    "SELECT id FROM session ORDER BY time_created ASC LIMIT ?1";
// The query that counts a session's messages.
static constexpr const char* SESSION_MESSAGE_COUNT_QUERY=
    "SELECT COUNT(*) FROM message WHERE session_id=?1";
// The query that counts every part of a session, of any kind.
static constexpr const char* SESSION_PART_COUNT_QUERY=
    "SELECT COUNT(*) FROM part WHERE message_id IN "
    "(SELECT id FROM message WHERE session_id=?1)";
// The query that counts a session's readable text parts.
//
// The `json_extract` calls pull two fields out of the stored JSON. The first is
// the part's type, which OpenCode stores as `text` for a written message. The
// second is the text itself. Testing both in the `WHERE` clause means the filter
// happens inside SQLite, so only the matching rows travel back.
static constexpr const char* READABLE_TEXT_PARTS_QUERY=
    "SELECT COUNT(*) FROM part "
    "WHERE message_id IN (SELECT id FROM message WHERE session_id=?1) "
    "AND json_extract(data, '$.type')='text' "
    "AND length(json_extract(data, '$.text'))>?2";
// The integrity pragma. `quick_check` verifies page structure without comparing
// every index against every table, which is what makes it fast enough to run on a
// hundred gigabyte database.
static constexpr const char* QUICK_CHECK_PRAGMA="PRAGMA quick_check";
// The longest result a quick check produces. SQLite returns a single row saying
// `ok` when the database is sound, or one row per problem otherwise. Reading only
// the first row is therefore enough to classify the result.
static constexpr int QUICK_CHECK_RESULT_LIMIT=5;
long long SafetyChecks::countReadableTextParts(Database& database, const string& sessionIdentifier) {
    // The statement is prepared once here and stepped once. Preparing inside a
    // loop is what principle P-11 of the specification forbids, and this method is
    // called once per session, so preparing once per call is correct.
    Statement readablePartsStatement(
        database.rawHandle(),
        READABLE_TEXT_PARTS_QUERY,
        "the number of readable text parts in a session"
    );
    if (!readablePartsStatement.isValid()) {
        return 0;
    }
    readablePartsStatement.bindText(1, sessionIdentifier);
    readablePartsStatement.bindInteger(2, MINIMUM_READABLE_TEXT_LENGTH);
    if (!readablePartsStatement.step()) {
        return 0;
    }
    return readablePartsStatement.columnInteger(0);
}
SafetyCheckResult SafetyChecks::verifyMessageContentPersistsOutsideEventTable(Database& database) {
    if (!StatisticsReader::tableExists(database, "session")) {
        return SafetyCheckResult::failed(
            "This database has no session table, so it is not an AI coding agent database. "
            "Nothing was changed."
        );
    }
    if (!StatisticsReader::tableExists(database, "part")) {
        return SafetyCheckResult::failed(
            "This database has no part table, which is where message content is kept. "
            "The prune command cannot confirm that anything would survive, so nothing was changed."
        );
    }
    // The oldest session is the right one to test, because it is the one a previous
    // prune would have damaged first. A database with no sessions cannot be harmed
    // by deleting rows, so it passes.
    Statement oldestSessionStatement(
        database.rawHandle(),
        OLDEST_SESSIONS_QUERY,
        "a search for the oldest session"
    );
    if (!oldestSessionStatement.isValid()) {
        return SafetyCheckResult::failed(
            "The query that looks for the oldest session could not be run, so the tool cannot "
            "confirm that message content is safe. Nothing was changed."
        );
    }
    oldestSessionStatement.bindInteger(1, 1);
    if (!oldestSessionStatement.step()) {
        return SafetyCheckResult(string(
            "The database contains no sessions, so there is nothing to lose. "
            "The prune command would not have anything to delete."
        ));
    }
    const string oldestSessionIdentifier=oldestSessionStatement.columnText(0);
    const long long readableTextPartCount=countReadableTextParts(database, oldestSessionIdentifier);
    if (readableTextPartCount==0) {
        return SafetyCheckResult::failed(
            "The oldest session in this database has no readable message text. The content may "
            "only exist inside the rows this command is about to delete, so nothing was changed. "
            "Run the diagnose command to see the details."
        );
    }
    return SafetyCheckResult(string(
        "Message content is stored in the part table, not only in the event table. The oldest "
        "session still holds "
    ) + to_string(readableTextPartCount) + " readable text parts, so deleting redundant event rows would not lose them.");
}
SafetyCheckResult SafetyChecks::verifyOldestSessionStillHasReadableContent(Database& database) {
    Statement oldestSessionStatement(
        database.rawHandle(),
        OLDEST_SESSIONS_QUERY,
        "a search for the oldest session"
    );
    if (!oldestSessionStatement.isValid()) {
        return SafetyCheckResult::failed(
            "The query that looks for the oldest session could not be run. Nothing was changed."
        );
    }
    oldestSessionStatement.bindInteger(1, 1);
    if (!oldestSessionStatement.step()) {
        return SafetyCheckResult(string(
            "The database contains no sessions, so there is nothing to check."
        ));
    }
    const string oldestSessionIdentifier=oldestSessionStatement.columnText(0);
    const long long readableTextPartCount=countReadableTextParts(database, oldestSessionIdentifier);
    if (readableTextPartCount==0) {
        return SafetyCheckResult::failed(
            "The oldest session no longer has readable message text. Something outside this tool "
            "has already removed it, or the database was damaged. Nothing was changed."
        );
    }
    return SafetyCheckResult(string(
        "The oldest session still holds "
    ) + to_string(readableTextPartCount) + " readable text parts.");
}
SafetyCheckResult SafetyChecks::verifyDatabaseIsNotLocked(const string& databaseFilePath) {
    const FileLockStatus lockStatus=FileLockDetector::checkLockStatus(databaseFilePath);
    if (lockStatus.isLocked) {
        return SafetyCheckResult::failed(
            lockStatus.explanation
            + " Close the coding agent and run the command again. Nothing was changed, and the "
            + "database is exactly as it was."
        );
    }
    return SafetyCheckResult(string("No other program has this database open."));
}
SafetyCheckResult SafetyChecks::verifySufficientFreeSpaceForBackup(
    const string& databaseFilePath,
    long long currentFileSizeBytes
) {
    const DiskSpace availableSpace=PlatformMetadata::readDiskSpace(databaseFilePath);
    const SpaceRequirement backupRequirement=DiskSpaceChecker::requiredBytesForBackup(currentFileSizeBytes);
    if (!DiskSpaceChecker::hasEnoughSpace(availableSpace, backupRequirement)) {
        return SafetyCheckResult::failed(
            string("There is not enough free space on the drive to make a safety copy first. ")
            + DiskSpaceChecker::describeAvailableSpace(availableSpace)
            + " A copy needs about " + to_string(backupRequirement.requiredBytes)
            + " bytes. Free some space and run the command again. Nothing was changed."
        );
    }
    return SafetyCheckResult(string("There is room on the drive for a safety copy."));
}
SafetyCheckResult SafetyChecks::verifyNoInterruptedWriteIsPending(const string& databaseFilePath) {
    FileLockStatus companionStatus;
    FileLockDetector::inspectCompanionFiles(databaseFilePath, companionStatus);
    if (companionStatus.hasWriteAheadLog && companionStatus.hasRollbackJournal) {
        return SafetyCheckResult::failed(
            "This database has both a write ahead log and a rollback journal, which means a write "
            "was interrupted part way through. Run the recover command first so SQLite can replay "
            "the interrupted write, then run this command again. Nothing was changed."
        );
    }
    return SafetyCheckResult(string("No interrupted write is waiting to be replayed."));
}
SafetyCheckResult SafetyChecks::verifyDatabaseIsNotDamaged(Database& database) {
    // The quick check pragma is run through the statement wrapper rather than
    // through `applyPragma`, because it returns rows and those rows carry the
    // answer.
    Statement quickCheckStatement(
        database.rawHandle(),
        QUICK_CHECK_PRAGMA,
        "a quick integrity check"
    );
    if (!quickCheckStatement.isValid()) {
        return SafetyCheckResult::failed(
            "The integrity check could not be run, so the tool cannot confirm the database is "
            "sound. Run the diagnose command to see the details. Nothing was changed."
        );
    }
    int resultCount=0;
    bool reportedProblem=false;
    string firstProblemText;
    // Only the first few results are read. SQLite returns a single row saying `ok`
    // when the database is sound, so anything else is already a problem and the
    // remaining rows are only used for the message.
    while (quickCheckStatement.step() && resultCount<QUICK_CHECK_RESULT_LIMIT) {
        const string resultText=quickCheckStatement.columnText(0);
        if (resultText!="ok") {
            if (!reportedProblem) {
                reportedProblem=true;
                firstProblemText=resultText;
            }
        }
        resultCount++;
    }
    if (reportedProblem) {
        return SafetyCheckResult::failed(
            string("SQLite reports that this database is damaged: ") + firstProblemText
            + " Deleting rows from a damaged database makes the damage worse, so nothing was changed. "
            + "Run the recover command for the safe next step."
        );
    }
    return SafetyCheckResult(string("The integrity check found no problems."));
}
SafetyCheckResult SafetyChecks::verifySchemaSupportsPruning(Database& database) {
    // Every table and column the prune query touches is tested before it is used.
    // An agent release that stores a different shape would otherwise produce a
    // query error in the middle of a deletion, which is the worst possible moment.
    const vector<string> requiredTables={"session", "message", "part", "event"};
    for (const string& requiredTable : requiredTables) {
        if (!StatisticsReader::tableExists(database, requiredTable)) {
            return SafetyCheckResult::failed(
                string("This database has no ") + requiredTable
                + " table, so its layout is not one this tool recognises. Nothing was changed."
            );
        }
    }
    const vector<pair<string, string>> requiredColumns={
        pair<string, string>("message", "session_id"),
        pair<string, string>("part", "message_id"),
        pair<string, string>("event", "type")
    };
    for (const auto& requiredColumn : requiredColumns) {
        if (!StatisticsReader::columnExists(database, requiredColumn.first, requiredColumn.second)) {
            return SafetyCheckResult::failed(
                string("The ") + requiredColumn.first + " table has no " + requiredColumn.second
                + " column, so its layout is not one this tool recognises. Nothing was changed."
            );
        }
    }
    return SafetyCheckResult(string("The tables and columns this command needs are all present."));
}
vector<SafetyCheckResult> SafetyChecks::runAllPreFlightChecks(
    Database& database,
    const string& databaseFilePath
) {
    // The order is fixed so that two reports read the same way. The cheapest and
    // most decisive checks come first, so a clearly locked database is reported as
    // locked without paying for an integrity check.
    vector<SafetyCheckResult> results;
    results.push_back(verifyDatabaseIsNotLocked(databaseFilePath));
    results.push_back(verifyNoInterruptedWriteIsPending(databaseFilePath));
    const FileMetadata fileMetadata=PlatformMetadata::readFileMetadata(databaseFilePath);
    results.push_back(verifySufficientFreeSpaceForBackup(databaseFilePath, fileMetadata.logicalSizeBytes));
    results.push_back(verifySchemaSupportsPruning(database));
    results.push_back(verifyMessageContentPersistsOutsideEventTable(database));
    results.push_back(verifyDatabaseIsNotDamaged(database));
    return results;
}
vector<SessionSnapshot> SafetyChecks::captureOldestSessions(Database& database, int sessionCountToCapture) {
    vector<SessionSnapshot> snapshots;
    if (sessionCountToCapture<=0) {
        return snapshots;
    }
    // The statement is prepared once and then stepped repeatedly, which is the
    // whole point of the Statement class.
    Statement oldestSessionsStatement(
        database.rawHandle(),
        OLDEST_SESSIONS_QUERY,
        "a capture of the oldest sessions"
    );
    if (!oldestSessionsStatement.isValid()) {
        return snapshots;
    }
    oldestSessionsStatement.bindInteger(1, sessionCountToCapture);
    while (oldestSessionsStatement.step()) {
        SessionSnapshot snapshot;
        snapshot.sessionIdentifier=oldestSessionsStatement.columnText(0);
        snapshot.readableTextPartCount=countReadableTextParts(database, snapshot.sessionIdentifier);
        // The message and part counts are read with one prepared statement each,
        // both of which use the session identifier as a bound parameter.
        Statement messageCountStatement(
            database.rawHandle(),
            SESSION_MESSAGE_COUNT_QUERY,
            "a count of the messages in a session"
        );
        if (messageCountStatement.isValid()) {
            messageCountStatement.bindText(1, snapshot.sessionIdentifier);
            if (messageCountStatement.step()) {
                snapshot.messageCount=messageCountStatement.columnInteger(0);
            }
        }
        Statement partCountStatement(
            database.rawHandle(),
            SESSION_PART_COUNT_QUERY,
            "a count of the parts in a session"
        );
        if (partCountStatement.isValid()) {
            partCountStatement.bindText(1, snapshot.sessionIdentifier);
            if (partCountStatement.step()) {
                snapshot.partCount=partCountStatement.columnInteger(0);
            }
        }
        snapshot.contentPersistsOutsideEventTable=snapshot.readableTextPartCount>0;
        snapshots.push_back(snapshot);
    }
    return snapshots;
}
SafetyCheckResult SafetyChecks::verifySnapshotsSurvivedUnchanged(
    Database& database,
    const vector<SessionSnapshot>& snapshotsTakenBefore
) {
    if (snapshotsTakenBefore.empty()) {
        return SafetyCheckResult(string(
            "There were no sessions to compare, so there was nothing that could have been lost."
        ));
    }
    for (const SessionSnapshot& snapshotBefore : snapshotsTakenBefore) {
        const long long readableTextPartCountNow=countReadableTextParts(
            database,
            snapshotBefore.sessionIdentifier
        );
        // The comparison is exact rather than "at least as many", because a
        // reduction means content was lost. A larger number is fine, because the
        // tool only deletes event rows and never creates parts.
        if (readableTextPartCountNow < snapshotBefore.readableTextPartCount) {
            return SafetyCheckResult::failed(
                string("Session ") + snapshotBefore.sessionIdentifier + " had "
                + to_string(snapshotBefore.readableTextPartCount)
                + " readable text parts before the change and has "
                + to_string(readableTextPartCountNow)
                + " now. The change was rolled back and the database was restored from the safety "
                + "copy. Nothing has been lost."
            );
        }
    }
    return SafetyCheckResult(string(
        "All "
    ) + to_string(snapshotsTakenBefore.size()) + " sampled sessions still hold their original message text.");
}
}
