// The HistoryStore class, the small record of how large each database has been.
//
// A single size tells a user how bad things are. A series of sizes tells them how
// bad things are getting, and that is the number that actually changes a decision:
// a database that grew four gigabytes last week will fill the disk next month. The
// watchdog writes one line per database per hour, and only when the size changed,
// so the record costs almost nothing and answers the question in one read.
//
// The store is a tab separated text file beside the service configuration. It is
// pruned to the newest entries older than the window, so it cannot grow without
// bound, and it is written by appending a line, which is the cheapest possible
// update for a file that is read far more often than it is written.
#ifndef AGENT_DB_DOCTOR_SERVICE_HISTORYSTORE_HPP
#define AGENT_DB_DOCTOR_SERVICE_HISTORYSTORE_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
using namespace std;
namespace AgentDbDoctor::Service {
// One recorded size.
struct HistoryPoint final {
    // When the size was seen, as seconds since the Unix epoch.
    long long epochSeconds=0;
    // The size of the database file in bytes.
    long long sizeBytes=0;
};
// The HistoryStore class records and reads the growth of the databases.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class HistoryStore final {
public:
    HistoryStore()=delete;
    // The path of the store: a small text file beside the configuration.
    static string storeFilePath();
    // Overrides the store path. Used only by the tests, which cannot write to the
    // shared location the service uses.
    static void setStorePathForTests(const string& storeFilePath);
    // Records one size, unless it is the same as the last recorded one for that
    // database. Returns true when a line was written. The store is pruned as part of
    // the write, so it stays bounded.
    static bool recordSize(const string& databaseFilePath, long long sizeBytes, long long epochSeconds);
    // The recorded sizes for one database, oldest first, limited to the newest
    // `maximumPoints`. An empty vector means nothing has been recorded, which is the
    // normal state before the service has run.
    static vector<HistoryPoint> pointsFor(const string& databaseFilePath, int maximumPoints);
    // Every database the store knows about, in the order first seen.
    static vector<string> knownDatabasePaths();
    // How many days the store keeps. Older entries are pruned on the next write.
    static constexpr int RETENTION_DAYS=90;
    // The most lines the store is allowed to hold. A machine with many databases
    // would otherwise grow it without bound.
    static constexpr size_t MAXIMUM_LINE_COUNT=20000;
};
}
#endif
