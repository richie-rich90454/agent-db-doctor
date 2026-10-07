// The implementation of the SchemaVersionDetector class.
//
// Each probe asks whether one row exists and stops there. `step()` returning
// true means a row was found; anything else, including a failed statement,
// means it was not. Failing closed is what makes an empty or half migrated
// database answer "first generation, nothing in it" rather than "unknown",
// which is the truthful answer for a file whose schema the tool supports.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the SchemaVersionDetector class declaration.
#include "AgentDbDoctor/Doctor/SchemaVersionDetector.hpp"
// Provides StatisticsReader, which answers whether a table exists.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the Statement class, which runs one probe query each.
#include "AgentDbDoctor/Persistence/Statement.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Persistence;
namespace AgentDbDoctor::Doctor {
// A row exists when the statement steps once. The statement is deliberately
// stepped exactly once: the question is existence, not count, and a second
// step on a large table would be the start of a scan.
static bool rowExists(Database& database, const char* probeQuery, const char* probeDescription) {
    Statement probe(database.rawHandle(), probeQuery, probeDescription);
    if (!probe.isValid()) {
        return false;
    }
    return probe.step();
}
SchemaGeneration SchemaVersionDetector::detect(Database& database) {
    // The first generation marks its rows with a `.1` suffix, for example
    // `message.updated.1`. The second generation family has no such suffix,
    // so the two patterns cannot match each other's rows.
    static constexpr const char* FIRST_GENERATION_PROBE=
        "SELECT 1 FROM event WHERE type LIKE '%.1' LIMIT 1";
    static constexpr const char* SECOND_GENERATION_EVENT_PROBE=
        "SELECT 1 FROM event WHERE type LIKE 'session.next.%' LIMIT 1";
    static constexpr const char* SECOND_GENERATION_MESSAGE_PROBE=
        "SELECT 1 FROM session_message LIMIT 1";
    static constexpr const char* SECOND_GENERATION_INPUT_PROBE=
        "SELECT 1 FROM session_input LIMIT 1";
    static constexpr const char* ANY_EVENT_ROW_PROBE=
        "SELECT 1 FROM event LIMIT 1";
    const bool hasFirstGenerationRows=StatisticsReader::tableExists(database, "event")
        && rowExists(database, FIRST_GENERATION_PROBE, "a first generation event row");
    const bool hasSecondGenerationRows=
        (StatisticsReader::tableExists(database, "event")
         && rowExists(
             database, SECOND_GENERATION_EVENT_PROBE, "a second generation event row"))
        || (StatisticsReader::tableExists(database, "session_message")
            && rowExists(
                database, SECOND_GENERATION_MESSAGE_PROBE, "a second generation message row"))
        || (StatisticsReader::tableExists(database, "session_input")
            && rowExists(
                database, SECOND_GENERATION_INPUT_PROBE, "a second generation input row"));
    if (hasFirstGenerationRows && hasSecondGenerationRows) {
        return SchemaGeneration::Version1And2;
    }
    if (hasFirstGenerationRows) {
        return SchemaGeneration::Version1;
    }
    if (hasSecondGenerationRows) {
        return SchemaGeneration::Version2Only;
    }
    // No rows of either generation. Rows of some other kind are content no
    // version of this tool knows, and refusing them is the whole point of the
    // `Unknown` answer. An empty event table on an otherwise well formed schema
    // is simply a fresh or fully pruned database.
    const bool eventTableHasAnyRows=StatisticsReader::tableExists(database, "event")
        && rowExists(database, ANY_EVENT_ROW_PROBE, "any event row at all");
    if (eventTableHasAnyRows) {
        return SchemaGeneration::Unknown;
    }
    const bool supportsFirstGeneration=StatisticsReader::tableExists(database, "session")
        && StatisticsReader::tableExists(database, "message")
        && StatisticsReader::tableExists(database, "part")
        && StatisticsReader::tableExists(database, "event");
    if (supportsFirstGeneration) {
        return SchemaGeneration::Version1;
    }
    return SchemaGeneration::Unknown;
}
string SchemaVersionDetector::describe(SchemaGeneration generation) {
    switch (generation) {
        case SchemaGeneration::Version1:
            return string("first generation event rows");
        case SchemaGeneration::Version1And2:
            return string("first generation event rows, with second generation tables also in use");
        case SchemaGeneration::Version2Only:
            return string("second generation rows only; the older duplicate snapshots are absent");
        case SchemaGeneration::Unknown:
            break;
    }
    return string("a layout this version of the tool does not recognise");
}
}
