// The SpaceAttribution class, which answers the question every other tool leaves
// open: where are the bytes actually?
//
// The header of a SQLite file says how large it is and how many pages are free, and
// nothing about which table or index holds the rest. SQLite's `dbstat` virtual
// table does: it walks the database's own page accounting rather than the user's
// rows, and reports the bytes each table and index occupies. That is what makes
// "why is this database forty gigabytes" answerable with numbers instead of a
// shrug, and it is read only.
//
// The session ranking is a different kind of measurement and says so: it sums the
// lengths of the stored parts, which means it reads the part table and is therefore
// asked for on purpose rather than computed on every diagnose.
#ifndef AGENT_DB_DOCTOR_DOCTOR_SPACEATTRIBUTION_HPP
#define AGENT_DB_DOCTOR_DOCTOR_SPACEATTRIBUTION_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the Database wrapper the measurements are made through.
#include "AgentDbDoctor/Persistence/Database.hpp"
using namespace std;
using namespace AgentDbDoctor::Persistence;
namespace AgentDbDoctor::Doctor {
// One table or index and the bytes it occupies.
struct SpaceUsageRow final {
    // The name of the table or index.
    string name;
    // The bytes the pages belonging to it occupy.
    long long bytes=0;
    // True when the name belongs to an index rather than a table.
    bool isIndex=false;
};
// One session and how much payload it holds.
struct SessionSizeRow final {
    // The session identifier.
    string sessionIdentifier;
    // The number of bytes of stored part payload the session holds.
    long long payloadBytes=0;
    // How many parts the session has.
    long long partCount=0;
};
// The SpaceAttribution class measures where a database's bytes live.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class SpaceAttribution final {
public:
    SpaceAttribution()=delete;
    // True when this build's SQLite provides the page accounting table. It always
    // does in a normal build; the check exists so a build without it degrades to a
    // clear sentence rather than an error.
    static bool pageAccountingIsAvailable(Database& database);
    // The bytes each table and index occupies, largest first. Entries belonging to
    // SQLite's own bookkeeping are included, because they are part of the file.
    // Returns an empty vector when the accounting is unavailable.
    static vector<SpaceUsageRow> tableAndIndexSizes(Database& database);
    // The largest sessions by stored payload, largest first. This reads the part
    // table, so it is asked for rather than assumed. Returns an empty vector when
    // the tables are not there or hold nothing.
    static vector<SessionSizeRow> largestSessions(Database& database, int sessionLimit);
};
}
#endif
