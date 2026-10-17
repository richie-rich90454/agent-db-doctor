// The SyntheticDatabaseBuilder class, which creates a real agent database for the
// tests to work on.
//
// The tests need a file with the same shape an AI coding agent produces: a session
// table, a message table, a part table holding the conversation text, and an event
// table holding the older copies of changed messages. Building it from a real
// SQLite file rather than a mock is what makes the tests worth running, because the
// behaviour that matters is the behaviour of a real database.
#ifndef AGENT_DB_DOCTOR_TESTS_SYNTHETICDATABASEBUILDER_HPP
#define AGENT_DB_DOCTOR_TESTS_SYNTHETICDATABASEBUILDER_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
using namespace std;
namespace AgentDbDoctor::TestSupport {
// The SyntheticDatabaseBuilder class creates a database for a test to act on.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so every method is `static`.
class SyntheticDatabaseBuilder final {
public:
    SyntheticDatabaseBuilder()=delete;
    // The options say how large a database to build and how much of it is
    // duplicated. The defaults make a small database with a realistic amount of
    // duplication, which is enough for every test in the suite.
    struct Options final {
        // How many sessions to create.
        int sessionCount=20;
        // How many messages each session holds.
        int messageCountPerSession=10;
        // How many parts each message holds. One is a text part, the rest are
        // tool results, which is roughly what a real session looks like.
        int partCountPerMessage=3;
        // How many older copies of each changed message to write into the event
        // table. This is the duplication the compact command exists to remove.
        int redundantCopiesPerMessage=4;
        // The text placed in each part. It is deliberately longer than the
        // twenty character threshold the safety check uses.
        string messageText=string("A message body that is comfortably longer than twenty ")
            + "characters so that the safety check treats it as readable content.";
        // The size in bytes of each individual event row, which is what makes a
        // large database possible without a large test.
        int bytesPerEventRow=4096;
        // True when the statistics table should be populated, so the constant time
        // row count path is exercised.
        bool populateStatisticsTable=true;
        // True when the file should be grown to a much larger size afterwards, to
        // prove the diagnostic pass does not read the file.
        bool padToLargeSize=false;
        // The size to pad to when `padToLargeSize` is true. A sparse file is used,
        // so this costs no disk space and the test still runs instantly.
        long long paddedSizeBytes=107374182400LL;
    };
    // Creates a database at `databaseFilePath` and returns the number of redundant
    // event rows it wrote, which is what a test asserts against.
    static long long build(const string& databaseFilePath, const Options& options);
    // Creates a database and then deliberately damages it, so the recovery tests
    // have something to recover from.
    //
    // The damage is written to a copy rather than to the original, so the caller
    // keeps a sound file to compare against.
    static void buildDamagedCopy(const string& soundFilePath, const string& damagedFilePath);
    // A path inside the temporary folder, unique to the test that asked for it, so
    // two tests never see each other's files.
    static string temporaryDatabasePath(const string& testName);
    // The shared scratch folder every temporary path lives in. A test asking about a
    // folder rather than a file points at this, because a path that was never
    // created is neither a file nor a folder and would answer about nothing.
    static string temporaryFolderPath();
    // Removes a file if it exists. The tests use it so a failed run does not leave
    // a database behind for the next one to trip over.
    static void removeIfPresent(const string& filePath);
};
}
#endif
