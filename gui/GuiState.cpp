// The implementation of the window's state and of the event parsing.
//
// The parser is a chain of small, named steps rather than one long function: an
// event carries twenty fields, and a reader of this file should be able to see
// which field feeds which number without counting commas.
//
// Two rules are worth stating because they are easy to get wrong.
//
// A database is identified by its path. When an event describes a database the
// state already holds, the record is replaced rather than added, so a second look
// at one database updates the row the user is reading instead of appending a
// duplicate below it.
//
// Unknown events are ignored. The command line and the window are separate
// programs that ship together, but a window must never refuse to work because a
// command it started printed something it did not recognise.
// Provides `snprintf`, which formats the numbers and the sentences.
#include <cstdio>
// Provides `ptrdiff_t`, the type an iterator difference has.
#include <cstddef>
// Provides `time`, `localtime_s`, and `localtime_r`, used for the timestamps.
#include <ctime>
// Provides `chrono`, for the wall clock.
#include <chrono>
// Provides `sort` and `stable_sort`, which order the tables and the forecasts.
#include <algorithm>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the state and the functions this file implements.
#include "GuiState.hpp"
using namespace std;
namespace AgentDbDoctor::Gui {
namespace {
// One binary kilobyte, and the units above it. A kilobyte in this tool is 1024
// bytes, because that is the unit a file system allocates in and the unit a file
// browser shows.
constexpr long long KILOBYTE_BYTES=1024LL;
constexpr long long MEGABYTE_BYTES=KILOBYTE_BYTES * 1024LL;
constexpr long long GIGABYTE_BYTES=BYTES_PER_GIGABYTE;
constexpr long long TERABYTE_BYTES=GIGABYTE_BYTES * 1024LL;
// The text of an array of strings, which several events carry. Elements that are
// not strings are skipped rather than turned into empty lines.
vector<string> textElements(const JsonValue* arrayValue) {
    vector<string> texts;
    if (arrayValue==nullptr || !arrayValue->is(JsonValue::Kind::Array)) {
        return texts;
    }
    for (const JsonValue& element : arrayValue->elements()) {
        if (element.is(JsonValue::Kind::Text)) {
            texts.push_back(element.textValue());
        }
    }
    return texts;
}
// One table's row count inside a `database` event's `rows` object, whose member
// names are the table names and are therefore not known in advance.
vector<RowCount> rowCountsFromEvent(const JsonValue* rowsValue) {
    vector<RowCount> rowCounts;
    if (rowsValue==nullptr || !rowsValue->is(JsonValue::Kind::Object)) {
        return rowCounts;
    }
    for (const pair<string, JsonValue>& memberEntry : rowsValue->members()) {
        RowCount rowCount;
        rowCount.tableName=memberEntry.first;
        rowCount.rowCount=memberEntry.second.memberInteger("count", 0);
        rowCount.isApproximate=memberEntry.second.memberBoolean("approximate", false);
        rowCounts.push_back(rowCount);
    }
    return rowCounts;
}
// One `database` event as a record.
DatabaseRecord databaseRecordFromEvent(const JsonValue& event) {
    DatabaseRecord record;
    record.filePath=event.memberText("path", string());
    record.agentName=event.memberText("agent", string());
    record.exists=event.memberBoolean("exists", false);
    record.isDirectory=event.memberBoolean("isDirectory", false);
    record.isHealthy=event.memberBoolean("healthy", false);
    record.sizeBytes=event.memberInteger("sizeBytes", 0);
    record.allocatedBytes=event.memberInteger("allocatedBytes", 0);
    record.hasWriteAheadLog=event.memberBoolean("hasWriteAheadLog", false);
    record.writeAheadLogBytes=event.memberInteger("writeAheadLogBytes", 0);
    record.modifiedMilliseconds=event.memberInteger("modifiedMilliseconds", 0);
    record.pageSizeBytes=event.memberInteger("pageSizeBytes", 0);
    record.pageCount=event.memberInteger("pageCount", 0);
    record.freePageCount=event.memberInteger("freePageCount", 0);
    record.reclaimableBytes=event.memberInteger("reclaimableBytes", 0);
    record.journalMode=event.memberText("journal", string());
    record.autoVacuumEnabled=event.memberBoolean("autoVacuum", false);
    record.compactionSupported=event.memberBoolean("compactionSupported", false);
    record.schemaDescription=event.memberText("schema", string());
    record.redundantEventRows=event.memberInteger("redundantEventRows", 0);
    record.integrity=event.memberText("integrity", string());
    record.integrityMessage=event.memberText("integrityMessage", string());
    record.databaseOpenSucceeded=event.memberBoolean("databaseOpenSucceeded", false);
    record.openFailureCode=event.memberInteger("openFailureCode", 0);
    record.durationNanoseconds=event.memberInteger("durationNanoseconds", 0);
    record.metadataCallCount=event.memberInteger("metadataCallCount", 0);
    record.redundantEventTypes=textElements(event.member("redundantEventTypes"));
    record.rowCounts=rowCountsFromEvent(event.member("rows"));
    record.warnings=textElements(event.member("warnings"));
    record.nextSteps=textElements(event.member("nextSteps"));
    return record;
}
// The field list of a `result` event. The values are typed by the event, so the
// field keeps the kind it arrived as.
vector<ResultField> resultFieldsFromEvent(const JsonValue* fieldsValue) {
    vector<ResultField> fields;
    if (fieldsValue==nullptr || !fieldsValue->is(JsonValue::Kind::Object)) {
        return fields;
    }
    for (const pair<string, JsonValue>& memberEntry : fieldsValue->members()) {
        ResultField field;
        field.name=memberEntry.first;
        field.kind=memberEntry.second.kind();
        if (field.kind==JsonValue::Kind::Number) {
            field.numberValue=memberEntry.second.integerValue();
        } else if (field.kind==JsonValue::Kind::Boolean) {
            field.booleanValue=memberEntry.second.booleanValue();
        } else {
            field.textValue=memberEntry.second.textValue();
        }
        fields.push_back(field);
    }
    return fields;
}
// The steps of a `plan` event.
vector<PlanStep> planStepsFromEvent(const JsonValue* stepsValue) {
    vector<PlanStep> steps;
    if (stepsValue==nullptr || !stepsValue->is(JsonValue::Kind::Array)) {
        return steps;
    }
    for (const JsonValue& stepValue : stepsValue->elements()) {
        PlanStep step;
        step.title=stepValue.memberText("title", string());
        step.explanation=stepValue.memberText("explanation", string());
        step.command=stepValue.memberText("command", string());
        step.severity=stepValue.memberText("severity", string());
        step.changesData=stepValue.memberBoolean("changesData", false);
        steps.push_back(step);
    }
    return steps;
}
// The entries of a `sessions` event, or of the `plan`-shaped part of an
// `explain-space --sessions` run.
vector<SessionEntry> sessionEntriesFromEvent(const JsonValue* entriesValue) {
    vector<SessionEntry> entries;
    if (entriesValue==nullptr || !entriesValue->is(JsonValue::Kind::Array)) {
        return entries;
    }
    for (const JsonValue& entryValue : entriesValue->elements()) {
        SessionEntry entry;
        entry.identifier=entryValue.memberText("id", string());
        entry.payloadBytes=entryValue.memberInteger("payloadBytes", 0);
        entry.partCount=entryValue.memberInteger("partCount", 0);
        entries.push_back(entry);
    }
    return entries;
}
// The tables and indexes of a `space` event.
vector<SpaceEntry> spaceEntriesFromEvent(const JsonValue* entriesValue) {
    vector<SpaceEntry> entries;
    if (entriesValue==nullptr || !entriesValue->is(JsonValue::Kind::Array)) {
        return entries;
    }
    for (const JsonValue& entryValue : entriesValue->elements()) {
        SpaceEntry entry;
        entry.name=entryValue.memberText("name", string());
        entry.bytes=entryValue.memberInteger("bytes", 0);
        entry.isIndex=entryValue.memberBoolean("isIndex", false);
        entries.push_back(entry);
    }
    return entries;
}
// One `history` event as a row.
HistoryRow historyRowFromEvent(const JsonValue& event) {
    HistoryRow row;
    row.databasePath=event.memberText("path", string());
    row.recordedPointCount=event.memberInteger("points", 0);
    row.windowDays=event.memberInteger("windowDays", 0);
    row.firstEpochSeconds=event.memberInteger("firstEpochSeconds", 0);
    row.firstBytes=event.memberInteger("firstBytes", 0);
    row.lastEpochSeconds=event.memberInteger("lastEpochSeconds", 0);
    row.lastBytes=event.memberInteger("lastBytes", 0);
    row.changeBytes=event.memberInteger("changeBytes", 0);
    row.bytesPerDay=event.memberInteger("bytesPerDay", 0);
    row.freeBytes=event.memberInteger("freeBytes", 0);
    row.daysUntilDriveFull=event.memberInteger("daysUntilDriveFull", -1);
    return row;
}
// One `agent` event as a record.
AgentRecord agentRecordFromEvent(const JsonValue& event) {
    AgentRecord record;
    record.name=event.memberText("name", string());
    record.storage=event.memberText("storage", string());
    record.supportsCompaction=event.memberBoolean("supportsCompaction", false);
    record.foundPaths=textElements(event.member("foundPaths"));
    record.candidatePaths=textElements(event.member("candidatePaths"));
    return record;
}
// A number with thousands separators, so a byte count reads as a size rather
// than as a string of digits.
string withThousandsSeparators(long long value) {
    const bool isNegative=value<0;
    // The magnitude is taken as an unsigned value, which handles the one negative
    // number whose positive partner does not fit in a signed 64 bit integer.
    unsigned long long magnitude=isNegative
        ? 0ULL - static_cast<unsigned long long>(value)
        : static_cast<unsigned long long>(value);
    string digits;
    do {
        digits.push_back(static_cast<char>('0' + static_cast<int>(magnitude % 10ULL)));
        magnitude/=10ULL;
    } while (magnitude!=0ULL);
    reverse(digits.begin(), digits.end());
    string separated;
    for (size_t digitIndex=0; digitIndex<digits.size(); ++digitIndex) {
        if (digitIndex>0 && (digits.size() - digitIndex) % 3==0) {
            separated.push_back(',');
        }
        separated.push_back(digits[digitIndex]);
    }
    return isNegative ? "-" + separated : separated;
}
// True when a field's name says its value is a byte count, which is what decides
// whether the result dialogue shows "1.2 GB" or "1,234,567,890". The command line
// spells byte counts with "Bytes" in the middle as well as at the end, as in
// `reclaimableBytesNow` and `payloadBytesRemoved`, so the whole name is searched.
bool nameIsAByteCount(const string& fieldName) {
    return fieldName.find("Bytes")!=string::npos;
}
// The text of a settings value, trimmed of the spaces a person might type around
// it when editing the file by hand.
string trimmed(const string& text) {
    size_t textStart=0;
    size_t textEnd=text.size();
    while (textStart<textEnd && (text[textStart]==' ' || text[textStart]=='\t' || text[textStart]=='\r')) {
        textStart++;
    }
    while (textEnd>textStart
        && (text[textEnd - 1]==' ' || text[textEnd - 1]=='\t' || text[textEnd - 1]=='\r')) {
        textEnd--;
    }
    return text.substr(textStart, textEnd - textStart);
}
// One audit entry as a line a person can read. The audit event has a documented
// shape, so the fields are used by name; an entry from a newer command line that does
// not have them returns nothing, and the caller falls back to listing the names and
// values it did find.
string auditEntryText(const JsonValue& entryValue) {
    const string action=entryValue.memberText("action", string());
    const string filePath=entryValue.memberText("path", string());
    if (action.empty() && filePath.empty()) {
        return string();
    }
    string entryText=action.empty() ? filePath : action + "  " + filePath;
    const long long sizeBeforeBytes=entryValue.memberInteger("sizeBeforeBytes", -1);
    const long long sizeAfterBytes=entryValue.memberInteger("sizeAfterBytes", -1);
    if (sizeBeforeBytes>=0 && sizeAfterBytes>=0) {
        entryText+="  " + formatBytesForWindow(sizeBeforeBytes) + " -> "
            + formatBytesForWindow(sizeAfterBytes);
    }
    const string outcome=entryValue.memberText("outcome", string());
    if (!outcome.empty()) {
        entryText+="  " + outcome;
    }
    const string backupPath=entryValue.memberText("backupPath", string());
    if (!backupPath.empty()) {
        entryText+="  (copy: " + backupPath + ")";
    }
    const long long epochSeconds=entryValue.memberInteger("epochSeconds", 0);
    if (epochSeconds>0) {
        entryText=formatTimestampForWindow(epochSeconds * 1000LL) + "  " + entryText;
    }
    return entryText;
}
// A whole number read from settings text. Returns false when the text is not one,
// in which case the caller keeps the value it had.
bool parseWholeNumber(const string& text, long long& valueOut) {
    const string trimmedText=trimmed(text);
    if (trimmedText.empty()) {
        return false;
    }
    size_t textPosition=0;
    bool isNegative=false;
    if (trimmedText[textPosition]=='-') {
        isNegative=true;
        textPosition++;
    }
    long long value=0;
    for (; textPosition<trimmedText.size(); ++textPosition) {
        const char currentCharacter=trimmedText[textPosition];
        if (currentCharacter<'0' || currentCharacter>'9') {
            return false;
        }
        value=value * 10 + (currentCharacter - '0');
        if (value<0) {
            return false;
        }
    }
    valueOut=isNegative ? -value : value;
    return true;
}
// True when the text is one of the three theme names.
int themeModeFromText(const string& text) {
    if (text=="dark") {
        return THEME_DARK;
    }
    if (text=="light") {
        return THEME_LIGHT;
    }
    return THEME_FOLLOW_SYSTEM;
}
// The theme name written into the settings file.
const char* themeModeText(int themeMode) {
    if (themeMode==THEME_DARK) {
        return "dark";
    }
    if (themeMode==THEME_LIGHT) {
        return "light";
    }
    return "follow";
}
// The smallest window the placement in the settings file is allowed to describe.
// A window smaller than this cannot show the tables, and a window dragged half off
// the screen would come back unusable.
constexpr int MINIMUM_WINDOW_WIDTH=800;
constexpr int MINIMUM_WINDOW_HEIGHT=560;
// A target of zero gigabytes would make a reclaim pointless, and an absurd target
// would be a typo rather than a wish.
constexpr long long MAXIMUM_RECLAIM_TARGET_GIGABYTES=1000000LL;
}
long long currentEpochMilliseconds() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()
    ).count();
}
void applyJsonEvent(GuiState& state, const JsonValue& event) {
    if (!event.is(JsonValue::Kind::Object)) {
        return;
    }
    const string eventName=event.memberText("event", string());
    if (eventName=="database") {
        const DatabaseRecord record=databaseRecordFromEvent(event);
        for (DatabaseRecord& existingRecord : state.databases) {
            if (existingRecord.filePath==record.filePath) {
                existingRecord=record;
                return;
            }
        }
        state.databases.push_back(record);
        return;
    }
    if (eventName=="progress") {
        state.progress.task=event.memberText("task", string());
        state.progress.completedUnits=event.memberInteger("done", 0);
        state.progress.totalUnits=event.memberInteger("total", 0);
        state.progress.finished=event.memberBoolean("finished", false);
        return;
    }
    if (eventName=="result") {
        state.lastResult.commandName=event.memberText("command", string());
        state.lastResult.fields=resultFieldsFromEvent(event.member("fields"));
        state.lastResult.isAvailable=true;
        return;
    }
    if (eventName=="error") {
        state.hasError=true;
        state.lastErrorMessage=event.memberText("message", string());
        state.lastErrorCode=event.memberText("code", string());
        return;
    }
    if (eventName=="warning") {
        const string warningText=event.memberText("text", string());
        if (!warningText.empty()) {
            state.runWarnings.push_back(warningText);
        }
        return;
    }
    if (eventName=="next") {
        state.runNextSteps=textElements(event.member("steps"));
        return;
    }
    if (eventName=="heading" || eventName=="line" || eventName=="detail") {
        // The three prose shapes of the machine readable output. They are kept as
        // the text of the run so the dialogues can show them, and the log pane
        // shows every one of them in any case. A detail line is indented, which is
        // how the command line prints it when no machine reads the output.
        const string lineText=event.memberText("text", string());
        state.runTextLines.push_back(eventName=="detail" ? "  " + lineText : lineText);
        return;
    }
    if (eventName=="backup") {
        BackupRecord backup;
        backup.filePath=event.memberText("path", string());
        backup.sizeBytes=event.memberInteger("sizeBytes", 0);
        state.runBackups.push_back(backup);
        return;
    }
    if (eventName=="plan") {
        state.recoveryPlan.agentName=event.memberText("agent", string());
        state.recoveryPlan.filePath=event.memberText("path", string());
        state.recoveryPlan.steps=planStepsFromEvent(event.member("steps"));
        state.recoveryPlan.isAvailable=true;
        return;
    }
    if (eventName=="sessions") {
        // The event carries no path, because the command was told which database to
        // read. The list therefore stays with the database it was asked for, which
        // the interface set before the run started.
        state.sessionList.entries=sessionEntriesFromEvent(event.member("entries"));
        return;
    }
    if (eventName=="space") {
        state.spaceTable.databasePath=event.memberText("path", string());
        state.spaceTable.sizeBytes=event.memberInteger("sizeBytes", 0);
        state.spaceTable.pageSizeBytes=event.memberInteger("pageSizeBytes", 0);
        state.spaceTable.pageCount=event.memberInteger("pageCount", 0);
        state.spaceTable.freeBytes=event.memberInteger("freeBytes", 0);
        state.spaceTable.entries=spaceEntriesFromEvent(event.member("entries"));
        state.spaceTable.isAvailable=true;
        return;
    }
    if (eventName=="history") {
        const HistoryRow row=historyRowFromEvent(event);
        for (HistoryRow& existingRow : state.historyRows) {
            if (existingRow.databasePath==row.databasePath) {
                existingRow=row;
                return;
            }
        }
        state.historyRows.push_back(row);
        return;
    }
    if (eventName=="service") {
        state.service.isInstalled=event.memberBoolean("installed", false);
        state.service.isRunning=event.memberBoolean("running", false);
        state.service.stateSentence=event.memberText("state", string());
        state.service.locations=event.memberText("locations", string());
        state.service.lastLogLine=event.memberText("lastLogLine", string());
        state.service.isAvailable=true;
        return;
    }
    if (eventName=="agent") {
        const AgentRecord record=agentRecordFromEvent(event);
        for (AgentRecord& existingRecord : state.agents) {
            if (existingRecord.name==record.name) {
                existingRecord=record;
                return;
            }
        }
        state.agents.push_back(record);
        return;
    }
    if (eventName=="audit") {
        // The audit listing is a list of records. Each entry becomes one line of the
        // run's text, so it is shown in the log pane and in any dialogue that reads
        // the run's lines. An entry whose fields this version does not know is
        // written as a list of names and values rather than dropped, so a newer
        // command line is still readable.
        const JsonValue* entriesValue=event.member("entries");
        if (entriesValue!=nullptr && entriesValue->is(JsonValue::Kind::Array)) {
            for (const JsonValue& entryValue : entriesValue->elements()) {
                if (entryValue.is(JsonValue::Kind::Text)) {
                    state.runTextLines.push_back(entryValue.textValue());
                    continue;
                }
                const string auditText=auditEntryText(entryValue);
                if (!auditText.empty()) {
                    state.runTextLines.push_back(auditText);
                    continue;
                }
                string entryText;
                for (const pair<string, JsonValue>& memberEntry : entryValue.members()) {
                    if (!entryText.empty()) {
                        entryText+=", ";
                    }
                    entryText+=memberEntry.first + "=";
                    if (memberEntry.second.is(JsonValue::Kind::Number)) {
                        entryText+=to_string(memberEntry.second.integerValue());
                    } else if (memberEntry.second.is(JsonValue::Kind::Boolean)) {
                        entryText+=memberEntry.second.booleanValue() ? "yes" : "no";
                    } else {
                        entryText+=memberEntry.second.textValue();
                    }
                }
                if (!entryText.empty()) {
                    state.runTextLines.push_back(entryText);
                }
            }
        }
        return;
    }
    if (eventName=="reads") {
        state.hasReadTrace=true;
        state.readTraceBytes=event.memberInteger("bytes", 0);
        state.readTraceCalls=event.memberInteger("calls", 0);
        return;
    }
    if (eventName=="exit") {
        // The last object of every machine readable run. The run itself is closed
        // by the interface when the child has ended, because the exit code that
        // matters is the one the operating system reports for the process.
        state.hasExitCode=true;
        state.lastExitCode=static_cast<int>(event.memberInteger("exitCode", 0));
        return;
    }
}
void appendLogLine(GuiState& state, const string& lineText) {
    state.logLines.push_back(lineText);
    if (state.logLines.size()>LOG_LINE_LIMIT) {
        // The oldest lines fall off the top. Dropping them in one erase is what
        // keeps the log a fixed length rather than a growing file; a reader who
        // wanted an old line can run the same command in a terminal.
        const size_t linesToDrop=state.logLines.size() - LOG_LINE_LIMIT;
        state.logLines.erase(
            state.logLines.begin(),
            state.logLines.begin() + static_cast<ptrdiff_t>(linesToDrop));
    }
}
void appendLogLines(GuiState& state, const vector<string>& lineTexts) {
    for (const string& lineText : lineTexts) {
        appendLogLine(state, lineText);
    }
}
void beginRun(GuiState& state, const string& runDescription) {
    state.run.description=runDescription;
    state.run.isActive=true;
    // Everything the previous command published goes. The database records, the
    // conversation list, and the recorded history stay: they describe the machine
    // rather than the run, and clearing them would make the window blink empty
    // between two looks.
    state.lastResult=CommandResult();
    state.hasError=false;
    state.lastErrorMessage.clear();
    state.lastErrorCode.clear();
    state.hasExitCode=false;
    state.lastExitCode=0;
    state.lastRunEnding.clear();
    state.runWarnings.clear();
    state.runNextSteps.clear();
    state.runBackups.clear();
    state.runTextLines.clear();
    state.hasReadTrace=false;
    state.readTraceBytes=0;
    state.readTraceCalls=0;
    state.progress=ProgressState();
    state.progress.task=runDescription;
}
void beginScan(GuiState& state) {
    beginRun(state, "Looking at every database");
    state.databases.clear();
    state.scan.isRunning=true;
}
bool endRun(GuiState& state, int exitCode) {
    if (!state.run.isActive) {
        return false;
    }
    state.run.isActive=false;
    if (state.hasExitCode) {
        state.lastRunEnding="The last command ended with exit code "
            + to_string(state.lastExitCode) + ".";
    } else {
        // No closing object arrived, which means the command line never got as far
        // as running a command. The sentence says so rather than reporting the
        // process's own code, which is zero for every background process.
        state.lastExitCode=exitCode;
        state.lastRunEnding="The last command ended without reporting its own exit code.";
    }
    if (state.scan.isRunning) {
        state.scan.isRunning=false;
        state.scan.hasFinished=true;
        state.scan.databaseCount=static_cast<int>(state.databases.size());
        state.scan.finishedMilliseconds=currentEpochMilliseconds();
    }
    return true;
}
void prepareSessionList(GuiState& state, const string& databasePath) {
    state.sessionList.databasePath=databasePath;
    state.sessionList.entries.clear();
}
void beginHistoryRefresh(GuiState& state) {
    state.historyRows.clear();
}
const DatabaseRecord* findDatabaseRecord(const GuiState& state, const string& filePath) {
    for (const DatabaseRecord& record : state.databases) {
        if (record.filePath==filePath) {
            return &record;
        }
    }
    return nullptr;
}
vector<const DatabaseRecord*> largestDatabasesFirst(const GuiState& state) {
    vector<const DatabaseRecord*> orderedRecords;
    orderedRecords.reserve(state.databases.size());
    for (const DatabaseRecord& record : state.databases) {
        if (!record.exists) {
            continue;
        }
        orderedRecords.push_back(&record);
    }
    stable_sort(
        orderedRecords.begin(),
        orderedRecords.end(),
        [](const DatabaseRecord* left, const DatabaseRecord* right) {
            if (left->sizeBytes!=right->sizeBytes) {
                return left->sizeBytes>right->sizeBytes;
            }
            return left->filePath<right->filePath;
        });
    return orderedRecords;
}
long long totalDatabaseBytes(const GuiState& state) {
    long long totalBytes=0;
    for (const DatabaseRecord& record : state.databases) {
        totalBytes+=record.sizeBytes;
    }
    return totalBytes;
}
long long totalReclaimableBytes(const GuiState& state) {
    long long totalBytes=0;
    for (const DatabaseRecord& record : state.databases) {
        totalBytes+=record.reclaimableBytes;
    }
    return totalBytes;
}
string healthWord(const DatabaseRecord& record) {
    if (record.isDirectory) {
        return string("Folder");
    }
    if (!record.exists) {
        return string("Missing");
    }
    if (record.isHealthy) {
        return string("Sound");
    }
    if (record.integrity=="damaged") {
        return string("Damaged");
    }
    if (record.integrity=="unreadable") {
        return string("Unreadable");
    }
    if (record.integrity=="not_a_database") {
        return record.sizeBytes==0 ? string("Empty") : string("Not a database");
    }
    if (record.integrity=="folder") {
        return string("Folder");
    }
    if (!record.databaseOpenSucceeded) {
        return string("Will not open");
    }
    return string("Attention");
}
string scanSummarySentence(const GuiState& state, long long nowMilliseconds) {
    if (state.scan.isRunning) {
        const size_t foundSoFar=state.databases.size();
        if (foundSoFar==0) {
            return string("Looking at every database. The first ones appear as they are measured.");
        }
        return string("Looking at every database. ") + formatCountForWindow(static_cast<long long>(foundSoFar))
            + " found so far.";
    }
    if (!state.scan.hasFinished) {
        return string("No look has finished yet. The databases appear once the command has run.");
    }
    const int foundCount=state.scan.databaseCount;
    string sentence=to_string(foundCount)
        + (foundCount==1 ? " database was found. " : " databases were found. ");
    sentence+="The last look finished "
        + formatAgeForWindow(state.scan.finishedMilliseconds, nowMilliseconds) + ".";
    return sentence;
}
string runVerdictSentence(const GuiState& state, int exitCode, bool wasCancelled) {
    if (wasCancelled) {
        return string("You stopped this command. Anything it had already done was left to the "
            "tool's own safety checks, which put the original back when a check did not pass.");
    }
    const string commandName=state.lastResult.commandName.empty()
        ? string("The command")
        : state.lastResult.commandName;
    if (state.hasError) {
        return commandName=="The command"
            ? string("The command stopped with a problem rather than finishing.")
            : commandName + " stopped with a problem rather than finishing.";
    }
    if (!state.hasExitCode) {
        // The command line writes a closing object for every run it starts. When none
        // arrived, the command line itself was refused before any command ran, which
        // is a mistake in the arguments rather than a failure of the work.
        return string("The command ended without writing its closing report, which is what "
            "happens when the command line itself was refused. Its own words are in the log "
            "pane.");
    }
    if (exitCode==0) {
        return commandName=="The command"
            ? string("Finished. The command reported success.")
            : commandName + " finished. The command reported success.";
    }
    return commandName + " stopped with an error. Its exit code was " + to_string(exitCode) + ".";
}
string formatResultFieldValue(const ResultField& field) {
    if (field.kind==JsonValue::Kind::Boolean) {
        return field.booleanValue ? string("yes") : string("no");
    }
    if (field.kind==JsonValue::Kind::Number) {
        if (nameIsAByteCount(field.name)) {
            return formatBytesForWindow(field.numberValue);
        }
        return formatCountForWindow(field.numberValue);
    }
    return field.textValue;
}
string formatBytesForWindow(long long byteCount) {
    if (byteCount<0) {
        return string("0 bytes");
    }
    if (byteCount>=TERABYTE_BYTES) {
        return formatCountForWindow(byteCount) + " bytes";
    }
    char numberBuffer[64]={0};
    const double gigabyteValue=static_cast<double>(byteCount) / static_cast<double>(GIGABYTE_BYTES);
    const double megabyteValue=static_cast<double>(byteCount) / static_cast<double>(MEGABYTE_BYTES);
    const double kilobyteValue=static_cast<double>(byteCount) / static_cast<double>(KILOBYTE_BYTES);
    // One decimal place is what a person needs to tell two sizes apart; the exact
    // byte count is in the drawer and in the command line's own output.
    if (byteCount>=GIGABYTE_BYTES) {
        const int written=snprintf(numberBuffer, sizeof(numberBuffer), "%.1f GB", gigabyteValue);
        if (written>0) {
            return string(numberBuffer, static_cast<size_t>(written));
        }
    }
    if (byteCount>=MEGABYTE_BYTES) {
        const int written=snprintf(numberBuffer, sizeof(numberBuffer), "%.1f MB", megabyteValue);
        if (written>0) {
            return string(numberBuffer, static_cast<size_t>(written));
        }
    }
    if (byteCount>=KILOBYTE_BYTES) {
        const int written=snprintf(numberBuffer, sizeof(numberBuffer), "%.1f KB", kilobyteValue);
        if (written>0) {
            return string(numberBuffer, static_cast<size_t>(written));
        }
    }
    return formatCountForWindow(byteCount) + " bytes";
}
string formatCountForWindow(long long count) {
    return withThousandsSeparators(count);
}
string formatTimestampForWindow(long long epochMilliseconds) {
    const long long epochSeconds=epochMilliseconds / 1000LL;
    const time_t calendarTime=static_cast<time_t>(epochSeconds);
    struct tm brokenDownTime;
#if defined(_WIN32)
    if (localtime_s(&brokenDownTime, &calendarTime)!=0) {
        return string("an unknown time");
    }
#else
    if (localtime_r(&calendarTime, &brokenDownTime)==nullptr) {
        return string("an unknown time");
    }
#endif
    char timestampBuffer[64]={0};
    const size_t written=strftime(timestampBuffer, sizeof(timestampBuffer), "%Y-%m-%d %H:%M:%S", &brokenDownTime);
    if (written==0) {
        return string("an unknown time");
    }
    return string(timestampBuffer, written);
}
string formatAgeForWindow(long long epochMilliseconds, long long nowMilliseconds) {
    if (epochMilliseconds<=0) {
        return string("at an unknown time");
    }
    const long long differenceMilliseconds=nowMilliseconds - epochMilliseconds;
    if (differenceMilliseconds<0) {
        return string("in the future");
    }
    constexpr long long MILLISECONDS_PER_MINUTE=60000LL;
    constexpr long long MILLISECONDS_PER_HOUR=3600000LL;
    constexpr long long MILLISECONDS_PER_DAY=86400000LL;
    if (differenceMilliseconds<MILLISECONDS_PER_MINUTE) {
        return string("just now");
    }
    if (differenceMilliseconds<MILLISECONDS_PER_HOUR) {
        const long long minutes=differenceMilliseconds / MILLISECONDS_PER_MINUTE;
        return to_string(minutes) + (minutes==1 ? " minute ago" : " minutes ago");
    }
    if (differenceMilliseconds<MILLISECONDS_PER_DAY) {
        const long long hours=differenceMilliseconds / MILLISECONDS_PER_HOUR;
        return to_string(hours) + (hours==1 ? " hour ago" : " hours ago");
    }
    const long long days=differenceMilliseconds / MILLISECONDS_PER_DAY;
    if (days<30) {
        return to_string(days) + (days==1 ? " day ago" : " days ago");
    }
    // Past a month the exact date reads better than a count nobody can picture.
    return string("on ") + formatTimestampForWindow(epochMilliseconds);
}
string serializeSettings(const InterfaceSettings& settings) {
    string settingsText;
    settingsText+="theme=" + string(themeModeText(settings.themeMode)) + "\n";
    settingsText+="reclaimTargetGigabytes=" + to_string(settings.reclaimTargetGigabytes) + "\n";
    settingsText+="keepSafetyCopy=" + string(settings.keepSafetyCopy ? "1" : "0") + "\n";
    settingsText+="backupDestination=" + settings.backupDestinationPath + "\n";
    settingsText+="windowX=" + to_string(settings.windowX) + "\n";
    settingsText+="windowY=" + to_string(settings.windowY) + "\n";
    settingsText+="windowWidth=" + to_string(settings.windowWidth) + "\n";
    settingsText+="windowHeight=" + to_string(settings.windowHeight) + "\n";
    settingsText+="logPaneIsOpen=" + string(settings.logPaneIsOpen ? "1" : "0") + "\n";
    return settingsText;
}
void parseSettings(const string& settingsText, InterfaceSettings& settingsOut) {
    // The reader starts from the settings it was given, so a file that only holds
    // a theme keeps every other value the program starts with, and an unknown line
    // is stepped over rather than throwing the rest away.
    InterfaceSettings settings=settingsOut;
    bool isWindowPlaced=false;
    int windowWidth=settings.windowWidth;
    int windowHeight=settings.windowHeight;
    int windowX=settings.windowX;
    int windowY=settings.windowY;
    size_t lineStart=0;
    while (lineStart<=settingsText.size()) {
        size_t lineEnd=settingsText.find('\n', lineStart);
        if (lineEnd==string::npos) {
            lineEnd=settingsText.size();
        }
        const string lineText=trimmed(settingsText.substr(lineStart, lineEnd - lineStart));
        if (!lineText.empty()) {
            const size_t separatorPosition=lineText.find('=');
            if (separatorPosition!=string::npos) {
                const string settingName=trimmed(lineText.substr(0, separatorPosition));
                const string settingValue=trimmed(lineText.substr(separatorPosition + 1));
                long long numberValue=0;
                if (settingName=="theme") {
                    settings.themeMode=themeModeFromText(settingValue);
                } else if (settingName=="reclaimTargetGigabytes"
                    && parseWholeNumber(settingValue, numberValue)) {
                    settings.reclaimTargetGigabytes=numberValue>0
                        ? min(numberValue, MAXIMUM_RECLAIM_TARGET_GIGABYTES)
                        : 1;
                } else if (settingName=="keepSafetyCopy") {
                    settings.keepSafetyCopy=settingValue=="1" || settingValue=="true";
                } else if (settingName=="backupDestination") {
                    settings.backupDestinationPath=settingValue;
                } else if (settingName=="windowX" && parseWholeNumber(settingValue, numberValue)) {
                    windowX=static_cast<int>(numberValue);
                    isWindowPlaced=true;
                } else if (settingName=="windowY" && parseWholeNumber(settingValue, numberValue)) {
                    windowY=static_cast<int>(numberValue);
                    isWindowPlaced=true;
                } else if (settingName=="windowWidth" && parseWholeNumber(settingValue, numberValue)) {
                    windowWidth=static_cast<int>(numberValue);
                } else if (settingName=="windowHeight" && parseWholeNumber(settingValue, numberValue)) {
                    windowHeight=static_cast<int>(numberValue);
                } else if (settingName=="logPaneIsOpen") {
                    settings.logPaneIsOpen=settingValue=="1" || settingValue=="true";
                }
            }
        }
        if (lineEnd>=settingsText.size()) {
            break;
        }
        lineStart=lineEnd + 1;
    }
    // A window smaller than the tables need is grown back to the useful minimum,
    // and a window narrowed by a mistake in the file cannot come back unusable.
    settings.windowWidth=windowWidth<MINIMUM_WINDOW_WIDTH ? MINIMUM_WINDOW_WIDTH : windowWidth;
    settings.windowHeight=windowHeight<MINIMUM_WINDOW_HEIGHT ? MINIMUM_WINDOW_HEIGHT : windowHeight;
    settings.windowX=windowX;
    settings.windowY=windowY;
    settings.hasWindowPlacement=isWindowPlaced;
    settingsOut=settings;
}
vector<string> buildCommandArguments(const CliInvocation& invocation) {
    vector<string> arguments;
    arguments.reserve(24);
    arguments.push_back(invocation.command);
    if (!invocation.action.empty()) {
        arguments.push_back(invocation.action);
    }
    if (!invocation.databasePath.empty()) {
        arguments.push_back("-d");
        arguments.push_back(invocation.databasePath);
    }
    if (!invocation.sessionIdentifier.empty()) {
        arguments.push_back("--session");
        arguments.push_back(invocation.sessionIdentifier);
    }
    if (!invocation.exportFormat.empty()) {
        arguments.push_back("--format");
        arguments.push_back(invocation.exportFormat);
    }
    if (!invocation.outputPath.empty()) {
        arguments.push_back("-o");
        arguments.push_back(invocation.outputPath);
    }
    if (!invocation.backupDestinationPath.empty()) {
        arguments.push_back("--backup-to");
        arguments.push_back(invocation.backupDestinationPath);
    }
    if (!invocation.backupsFilePath.empty()) {
        arguments.push_back("--file");
        arguments.push_back(invocation.backupsFilePath);
    }
    if (invocation.reclaimTargetBytes>0) {
        // The command takes a target as text with a unit, so the byte count is
        // written back as gigabytes, which is the unit the settings tab asks for.
        const long long targetGigabytes=invocation.reclaimTargetBytes / GIGABYTE_BYTES;
        arguments.push_back("--target");
        arguments.push_back(
            to_string(targetGigabytes>0 ? targetGigabytes : 1) + "GB");
    }
    if (invocation.warningThresholdBytes>0) {
        arguments.push_back("--warning-threshold");
        arguments.push_back(to_string(invocation.warningThresholdBytes));
    }
    if (invocation.intervalSeconds>0) {
        arguments.push_back("--interval");
        arguments.push_back(to_string(invocation.intervalSeconds));
    }
    if (invocation.sessionsToKeep>=0) {
        arguments.push_back("-k");
        arguments.push_back(to_string(invocation.sessionsToKeep));
    }
    if (invocation.maximumFractionToRemove>=0.0) {
        char fractionBuffer[32]={0};
        const int written=snprintf(
            fractionBuffer,
            sizeof(fractionBuffer),
            "%.2f",
            invocation.maximumFractionToRemove);
        if (written>0) {
            arguments.push_back("--max-fraction");
            arguments.push_back(string(fractionBuffer, static_cast<size_t>(written)));
        }
    }
    if (invocation.sessionLimit>0) {
        arguments.push_back("--top");
        arguments.push_back(to_string(invocation.sessionLimit));
    }
    if (invocation.historyDays>0) {
        arguments.push_back("--days");
        arguments.push_back(to_string(invocation.historyDays));
    }
    if (invocation.olderThanDays>=0) {
        arguments.push_back("--older-than");
        arguments.push_back(to_string(invocation.olderThanDays));
    }
    if (invocation.keepSafetyCopy) {
        arguments.push_back("--keep-backup");
    }
    if (invocation.backupsAll) {
        arguments.push_back("--all");
    }
    if (invocation.integrityCheck) {
        arguments.push_back("--check");
    }
    if (invocation.traceReads) {
        arguments.push_back("--trace-reads");
    }
    if (invocation.enableAutoVacuum) {
        arguments.push_back("--enable-auto-vacuum");
    }
    if (invocation.verifyCandidatePaths) {
        arguments.push_back("--verify");
    }
    if (invocation.rebuildWorkingCopy) {
        arguments.push_back("--rebuild");
    }
    // A dry run never answers the question it exists to ask, and the command line
    // treats the two flags as opposites, so only one of them is ever passed.
    if (invocation.dryRun) {
        arguments.push_back("--dry-run");
    } else if (invocation.answerYes) {
        arguments.push_back("--yes");
    }
    if (invocation.quiet) {
        arguments.push_back("--quiet");
    }
    // Every run is asked for the objects. The window reads them, and the prose the
    // command writes for a person arrives beside them and goes to the log pane.
    arguments.push_back("--machine-readable");
    return arguments;
}
}
