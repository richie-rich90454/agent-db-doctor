// The implementation of the SpaceAttribution class.
//
// The page accounting query groups `dbstat` by name. The virtual table reports one
// row per page, so grouping is done by SQLite in one pass and only the totals cross
// into this process. The session ranking joins the parts to their messages and sums
// the lengths of the stored payload, which is an approximation of the bytes a
// session holds: it counts the payload rather than the pages, because pages are
// shared between rows and a per-session page count does not exist.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the SpaceAttribution class declaration.
#include "AgentDbDoctor/Doctor/SpaceAttribution.hpp"
// Provides StatisticsReader, which answers whether a table exists.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
using namespace std;
using namespace AgentDbDoctor::Persistence;
namespace AgentDbDoctor::Doctor {
bool SpaceAttribution::pageAccountingIsAvailable(Database& database) {
    // The probe prepares the real query and asks only whether it compiled. A build
    // whose SQLite lacks the accounting table fails to prepare, and the caller gets
    // a sentence rather than an error.
    Statement probeStatement(
        database.rawHandle(),
        "SELECT COUNT(*) FROM dbstat",
        "a probe for the page accounting table"
    );
    return probeStatement.isValid();
}
vector<SpaceUsageRow> SpaceAttribution::tableAndIndexSizes(Database& database) {
    vector<SpaceUsageRow> usageRows;
    Statement accountingStatement(
        database.rawHandle(),
        "SELECT name, SUM(pgsize) AS bytes FROM dbstat GROUP BY name ORDER BY bytes DESC",
        "the bytes each table and index occupies"
    );
    if (!accountingStatement.isValid()) {
        return usageRows;
    }
    while (accountingStatement.step()) {
        SpaceUsageRow usageRow;
        usageRow.name=accountingStatement.columnText(0);
        usageRow.bytes=accountingStatement.columnInteger(1);
        // The accounting table names an index the same way the schema does, so the
        // indexes are separated by asking the schema rather than by guessing at the
        // name.
        usageRow.isIndex=StatisticsReader::indexExists(database, usageRow.name);
        usageRows.push_back(std::move(usageRow));
    }
    return usageRows;
}
vector<SessionSizeRow> SpaceAttribution::largestSessions(Database& database, int sessionLimit) {
    vector<SessionSizeRow> sessionRows;
    if (!StatisticsReader::tableExists(database, "part")
        || !StatisticsReader::tableExists(database, "message")) {
        return sessionRows;
    }
    Statement sessionSizeStatement(
        database.rawHandle(),
        "SELECT m.session_id, SUM(LENGTH(p.data)) AS payload_bytes, COUNT(*) AS part_count "
        "FROM part p JOIN message m ON p.message_id = m.id "
        "GROUP BY m.session_id ORDER BY payload_bytes DESC LIMIT ?1",
        "the largest sessions by stored payload"
    );
    if (!sessionSizeStatement.isValid()) {
        return sessionRows;
    }
    sessionSizeStatement.bindInteger(1, sessionLimit);
    while (sessionSizeStatement.step()) {
        SessionSizeRow sessionRow;
        sessionRow.sessionIdentifier=sessionSizeStatement.columnText(0);
        sessionRow.payloadBytes=sessionSizeStatement.columnInteger(1);
        sessionRow.partCount=sessionSizeStatement.columnInteger(2);
        sessionRows.push_back(std::move(sessionRow));
    }
    return sessionRows;
}
}
