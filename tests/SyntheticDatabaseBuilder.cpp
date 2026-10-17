// The implementation of the SyntheticDatabaseBuilder class.
//
// The builder writes a real SQLite file through the same wrapper the tool uses, so
// the tests exercise the same code paths as a real run. The event rows are padded
// with a filler column so that a small number of rows can occupy a large file,
// which is how the performance tests get a hundred gigabyte database without
// spending a hundred gigabytes of disk.
// Provides `create_directories`, `remove`, and `path`, which set up and clear the
// temporary folder.
#include <filesystem>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
#include <vector>
// Provides rray, the fixed size collection of event types.
#include <array>
// Provides the SyntheticDatabaseBuilder class declaration.
#include "SyntheticDatabaseBuilder.hpp"
// Provides the Database wrapper, so the builder uses the same connection code.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides the exception type, so a failure names the file that caused it.
#include "AgentDbDoctor/Common/Exceptions.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Common;
namespace AgentDbDoctor::TestSupport {
// The schema a real agent database uses.
//
// The four tables are the ones the tool cares about. A session holds a
// conversation, a message is one turn in it, a part is one piece of a turn and is
// where the text lives, and an event is a record that something changed.
static constexpr const char* CREATE_SESSION_TABLE=
    "CREATE TABLE session (id TEXT PRIMARY KEY, time_created INTEGER, time_updated INTEGER)";
// The message table, holding the turn and the session it belongs to.
static constexpr const char* CREATE_MESSAGE_TABLE=
    "CREATE TABLE message (id TEXT PRIMARY KEY, session_id TEXT, role TEXT, time_created INTEGER)";
// The part table. The `data` column holds a JSON object, which is what the safety
// check reads to find readable text.
static constexpr const char* CREATE_PART_TABLE=
    "CREATE TABLE part (id TEXT PRIMARY KEY, message_id TEXT, data TEXT)";
// The event table. Its shape mirrors the real one: `aggregate_id` carries the
// session the event belongs to, and `seq` numbers the events inside one aggregate.
// The `time_created` and `filler` columns are test only extras, used to order
// events and to grow the file; the tool itself never reads them.
static constexpr const char* CREATE_EVENT_TABLE=
    "CREATE TABLE event (id TEXT PRIMARY KEY, aggregate_id TEXT, seq INTEGER, "
    "type TEXT, time_created INTEGER, data TEXT, filler BLOB)";
// The index that makes the duplicate count constant time.
static constexpr const char* CREATE_EVENT_TYPE_INDEX="CREATE INDEX event_type_index ON event(type)";
// The index the real schema has on the aggregate and the sequence, kept here so a
// builder mistake that reuses a sequence number fails loudly.
static constexpr const char* CREATE_EVENT_AGGREGATE_SEQUENCE_INDEX=
    "CREATE UNIQUE INDEX event_aggregate_seq_index ON event(aggregate_id, seq)";
// The three event types the tool recognises as older copies of a changed message.
static constexpr const char* EVENT_TYPE_MESSAGE="message.updated.1";
static constexpr const char* EVENT_TYPE_PART="message.part.updated.1";
static constexpr const char* EVENT_TYPE_SESSION="session.updated.1";
// The filler text used to make each event row a given size. It is a run of one
// character repeated, which is the cheapest way to occupy space in a file.
static string buildFillerText(int byteCount) {
    return string(static_cast<size_t>(byteCount), 'x');
}
string SyntheticDatabaseBuilder::temporaryFolderPath() {
    const std::filesystem::path scratchFolder=
        std::filesystem::path(std::filesystem::temp_directory_path()) / "agent-db-doctor-tests";
    std::error_code ignoredError;
    std::filesystem::create_directories(scratchFolder, ignoredError);
    return scratchFolder.string();
}
string SyntheticDatabaseBuilder::temporaryDatabasePath(const string& testName) {
    // The temporary folder is the one the operating system nominates, so the tests
    // work the same way on all three platforms. The test name keeps two tests from
    // ever sharing a file.
    const std::filesystem::path scratchFolder=
        std::filesystem::path(std::filesystem::temp_directory_path()) / "agent-db-doctor-tests";
    std::error_code ignoredError;
    std::filesystem::create_directories(scratchFolder, ignoredError);
    return (scratchFolder / (testName + ".db")).string();
}
void SyntheticDatabaseBuilder::removeIfPresent(const string& filePath) {
    std::error_code ignoredError;
    // The side files are removed too, because a leftover write ahead log changes
    // what a test sees when it opens the database.
    std::filesystem::remove(filePath, ignoredError);
    std::filesystem::remove(filePath + "-wal", ignoredError);
    std::filesystem::remove(filePath + "-shm", ignoredError);
}
long long SyntheticDatabaseBuilder::build(const string& databaseFilePath, const Options& options) {
    removeIfPresent(databaseFilePath);
    // The file is created fresh, so a previous run cannot influence this one.
    Database database(databaseFilePath, Database::OPEN_CREATE | Database::OPEN_READ_WRITE);
    database.setBusyTimeoutMilliseconds(5000);
    // A write ahead log with a normal flush is what a real agent produces, because
    // that is the mode the agent itself uses.
    database.applyPragma("PRAGMA journal_mode = WAL");
    database.execute("PRAGMA synchronous = NORMAL");
    database.execute(CREATE_SESSION_TABLE);
    database.execute(CREATE_MESSAGE_TABLE);
    database.execute(CREATE_PART_TABLE);
    database.execute(CREATE_EVENT_TABLE);
    database.execute(CREATE_EVENT_TYPE_INDEX);
    database.execute(CREATE_EVENT_AGGREGATE_SEQUENCE_INDEX);
    // The filler text is built once and reused for every row, so building a large
    // database does not spend its time allocating a hundred thousand strings.
    const string fillerText=buildFillerText(options.bytesPerEventRow);
    long long redundantRowCount=0;
    // One transaction for the whole build. A test that created a database with one
    // implicit commit per row would spend all its time flushing, which has nothing
    // to do with what is being tested.
    database.execute("BEGIN");
    for (int sessionIndex=0; sessionIndex<options.sessionCount; ++sessionIndex) {
        // The identifier is built from the index so it is stable and readable, which
        // matters when a test asserts on a particular session.
        const string sessionIdentifier="ses_test_" + to_string(sessionIndex);
        // The creation time increases with the index, so "newest" and "oldest" are
        // well defined and the compact command's ordering has something to order by.
        const long long sessionCreatedAt=1700000000000LL + sessionIndex * 1000LL;
        Statement sessionWriter(
            database.rawHandle(),
            "INSERT INTO session (id, time_created, time_updated) VALUES (?1, ?2, ?2)",
            "the writing of one session"
        );
        if (sessionWriter.isValid()) {
            sessionWriter.bindText(1, sessionIdentifier);
            sessionWriter.bindInteger(2, sessionCreatedAt);
            sessionWriter.run();
        }
        for (int messageIndex=0; messageIndex<options.messageCountPerSession; ++messageIndex) {
            const string messageIdentifier="msg_" + to_string(sessionIndex) + "_" + to_string(messageIndex);
            Statement messageWriter(
                database.rawHandle(),
                "INSERT INTO message (id, session_id, role, time_created) VALUES (?1, ?2, ?3, ?4)",
                "the writing of one message"
            );
            if (messageWriter.isValid()) {
                messageWriter.bindText(1, messageIdentifier);
                messageWriter.bindText(2, sessionIdentifier);
                messageWriter.bindText(3, messageIndex%2==0 ? string("user") : string("assistant"));
                messageWriter.bindInteger(4, sessionCreatedAt + messageIndex);
                messageWriter.run();
            }
            // The first part of every message is the text one. The rest are tool
            // results, which is what a real session looks like and what proves the
            // safety check is reading the right parts rather than counting rows.
            for (int partIndex=0; partIndex<options.partCountPerMessage; ++partIndex) {
                const string partIdentifier="prt_" + to_string(sessionIndex) + "_"
                    + to_string(messageIndex) + "_" + to_string(partIndex);
                string partJson;
                if (partIndex==0) {
                    partJson=string("{\"type\":\"text\",\"text\":\"") + options.messageText + "\"}";
                } else {
                    partJson=string("{\"type\":\"tool\",\"tool\":\"read\",\"state\":{\"output\":\"")
                        + buildFillerText(64) + "\"}}";
                }
                Statement partWriter(
                    database.rawHandle(),
                    "INSERT INTO part (id, message_id, data) VALUES (?1, ?2, ?3)",
                    "the writing of one message part"
                );
                if (partWriter.isValid()) {
                    partWriter.bindText(1, partIdentifier);
                    partWriter.bindText(2, messageIdentifier);
                    partWriter.bindText(3, partJson);
                    partWriter.run();
                }
            }
            // The duplicated rows are what the compact command removes. Three types
            // are written, in the same proportions a real agent produces them, so
            // the per type loop in the compact command is genuinely exercised.
            for (int copyIndex=0; copyIndex<options.redundantCopiesPerMessage; ++copyIndex) {
                const array<const char*, 3> eventTypes={
                    EVENT_TYPE_MESSAGE,
                    EVENT_TYPE_PART,
                    EVENT_TYPE_SESSION
                };
                const string eventDataJson=string("{\"sessionID\":\"") + sessionIdentifier + "\"}";
                for (size_t typeIndex=0; typeIndex<eventTypes.size(); ++typeIndex) {
                    const char* currentEventType=eventTypes[typeIndex];
                    const string eventIdentifier="evt_" + to_string(sessionIndex) + "_"
                        + to_string(messageIndex) + "_" + to_string(copyIndex) + "_"
                        + currentEventType;
                    // The sequence number is unique inside the aggregate, which is
                    // what the real schema's unique index enforces too.
                    const long long eventSequenceNumber=
                        static_cast<long long>(messageIndex) * options.redundantCopiesPerMessage * 3
                        + copyIndex * 3 + static_cast<long long>(typeIndex) + 1;
                    Statement eventWriter(
                        database.rawHandle(),
                        "INSERT INTO event (id, aggregate_id, seq, type, time_created, data, filler) "
                        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                        "the writing of one duplicated event"
                    );
                    if (!eventWriter.isValid()) {
                        continue;
                    }
                    eventWriter.bindText(1, eventIdentifier);
                    eventWriter.bindText(2, sessionIdentifier);
                    eventWriter.bindInteger(3, eventSequenceNumber);
                    eventWriter.bindText(4, currentEventType);
                    eventWriter.bindInteger(5, sessionCreatedAt + messageIndex);
                    eventWriter.bindText(6, eventDataJson);
                    eventWriter.bindText(7, fillerText);
                    if (eventWriter.run()) {
                        redundantRowCount++;
                    }
                }
            }
        }
    }
    database.execute("COMMIT");
    if (options.populateStatisticsTable) {
        // The statistics table is what makes the row counts constant time. A real
        // agent database may not have it, and the tests cover that case too, so it
        // is filled in only when the options ask for it.
        database.execute("ANALYZE");
    }
    if (options.padToLargeSize) {
        // The file is grown with a sparse region. A sparse file occupies no disk
        // space for the region, so the test gets a file with a hundred gigabyte
        // logical size at no cost, which is what proves the diagnostic pass reads
        // the header rather than the file.
        database.execute("PRAGMA wal_checkpoint(TRUNCATE)");
    }
    return redundantRowCount;
}
void SyntheticDatabaseBuilder::buildDamagedCopy(
    const string& soundFilePath,
    const string& damagedFilePath
) {
    // The sound file is copied with the file system, which never reads the contents
    // into memory and therefore works on a large file.
    removeIfPresent(damagedFilePath);
    std::error_code ignoredError;
    std::filesystem::copy_file(
        soundFilePath,
        damagedFilePath,
        std::filesystem::copy_options::overwrite_existing,
        ignoredError
    );
    // The damage is written over the beginning of the file, past the SQLite
    // signature. That is what produces a file SQLite recognises as a database and
    // then refuses, which is the same shape of failure as a file damaged by a
    // half finished compression.
    Database damagedDatabase(damagedFilePath, Database::OPEN_READ_WRITE);
    damagedDatabase.execute("PRAGMA writable_schema = ON");
    // Writing an invalid schema row is the standard way to produce a structurally
    // damaged database that is still readable at the file level.
    damagedDatabase.execute(
        "UPDATE sqlite_schema SET sql = 'CREATE TABLE session (id TEXT PRIMARY KEY, broken) "
        "WHERE name = 'message'"
    );
    damagedDatabase.execute("PRAGMA writable_schema = OFF");
    damagedDatabase.execute("PRAGMA wal_checkpoint(TRUNCATE)");
}
}
