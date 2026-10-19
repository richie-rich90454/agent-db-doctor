// The implementation of the ConsoleReport class.
//
// The rule this file exists to enforce is that no escape sequence is ever written
// when standard output is not a terminal. Every write goes through one of the
// methods below, and each of them checks the flag before emitting anything, so a
// piped run produces plain lines that a person can read in a log.
#include <string>
// Provides `fputs` and `fflush`, which write to standard output.
#include <cstdio>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the ConsoleReport class declaration.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides the number to text conversions used throughout the output.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
#include "AgentDbDoctor/Recovery/RecoveryPlaybook.hpp"
using namespace std;
using namespace AgentDbDoctor;
namespace AgentDbDoctor::Presentation {
// The escape sequences, written as literal text. None of them is ever written when
// the output is not a terminal.
//
// Bold makes a heading stand out without needing a box around it.
static constexpr const char* BOLD_ON="\x1B[1m";
// Bold off returns the terminal to its normal weight.
static constexpr const char* BOLD_OFF="\x1B[0m";
// The width of the separator line, chosen to fit an eighty column terminal without
// wrapping, which is the narrowest width almost every terminal has.
static constexpr int SEPARATOR_WIDTH=72;
// True when every reporter in this process writes JSON objects. Set once by the
// command line after parsing, and read by every reporter constructed afterwards.
static bool processMachineReadable=false;
ConsoleReport::ConsoleReport(bool isTerminalAttached)
    : terminalIsAttached(isTerminalAttached),
      machineReadable(processMachineReadable) {
}
void ConsoleReport::setMachineReadableForProcess(bool enabled) {
    processMachineReadable=enabled;
}
MachineField MachineField::text(const string& fieldName, const string& value) {
    MachineField field;
    field.name=fieldName;
    field.textValue=value;
    field.kind=Kind::Text;
    return field;
}
MachineField MachineField::number(const string& fieldName, long long value) {
    MachineField field;
    field.name=fieldName;
    field.numberValue=value;
    field.kind=Kind::Number;
    return field;
}
MachineField MachineField::boolean(const string& fieldName, bool value) {
    MachineField field;
    field.name=fieldName;
    field.booleanValue=value;
    field.kind=Kind::Boolean;
    return field;
}
void ConsoleReport::enableMachineReadableOutput() {
    machineReadable=true;
}
bool ConsoleReport::isMachineReadable() const {
    return machineReadable;
}
bool ConsoleReport::isTerminalAttached() const {
    return terminalIsAttached;
}
void ConsoleReport::writeJsonLine(const string& jsonText) const {
    fputs((jsonText + "\n").c_str(), stdout);
    // Every object is flushed as it is written, because the reader is usually a
    // script or another program watching a pipe, and a buffered line that arrives
    // after the process ends is useless for following a long run.
    fflush(stdout);
}
string ConsoleReport::escapeForJson(const string& text) {
    string escapedText;
    escapedText.reserve(text.size() + 8);
    for (const char currentCharacter : text) {
        switch (currentCharacter) {
            case '"': escapedText+="\\\""; break;
            case '\\': escapedText+="\\\\"; break;
            case '\n': escapedText+="\\n"; break;
            case '\r': escapedText+="\\r"; break;
            case '\t': escapedText+="\\t"; break;
            default: {
                const auto characterCode=static_cast<unsigned char>(currentCharacter);
                if (characterCode<0x20) {
                    char escapeBuffer[8]={0};
                    snprintf(escapeBuffer, sizeof(escapeBuffer), "\\u%04x", characterCode);
                    escapedText+=escapeBuffer;
                } else {
                    escapedText.push_back(currentCharacter);
                }
                break;
            }
        }
    }
    return escapedText;
}
namespace {
// The JSON name of an integrity status, so a script never has to parse prose.
const char* integrityStatusName(IntegrityStatus status) {
    switch (status) {
        case IntegrityStatus::NOT_CHECKED: return "not_checked";
        case IntegrityStatus::IS_A_FOLDER: return "folder";
        case IntegrityStatus::INTACT: return "intact";
        case IntegrityStatus::NOT_A_DATABASE: return "not_a_database";
        case IntegrityStatus::STRUCTURALLY_DAMAGED: return "damaged";
        case IntegrityStatus::UNREADABLE: return "unreadable";
        case IntegrityStatus::PROBLEM_REPORTED: return "problem_reported";
    }
    return "unknown";
}
}
void ConsoleReport::writeMachineResult(
    const string& commandName,
    const vector<MachineField>& fields
) const {
    string jsonText=string("{\"event\":\"result\",\"command\":\"") + escapeForJson(commandName)
        + "\",\"fields\":{";
    bool isFirstField=true;
    for (const MachineField& field : fields) {
        if (!isFirstField) {
            jsonText+=",";
        }
        isFirstField=false;
        jsonText+="\"" + escapeForJson(field.name) + "\":";
        if (field.kind==MachineField::Kind::Number) {
            jsonText+=to_string(field.numberValue);
        } else if (field.kind==MachineField::Kind::Boolean) {
            jsonText+=field.booleanValue ? "true" : "false";
        } else {
            jsonText+="\"" + escapeForJson(field.textValue) + "\"";
        }
    }
    jsonText+="}}";
    writeJsonLine(jsonText);
}
void ConsoleReport::writeProgressEvent(
    const string& taskDescription,
    long long completedUnits,
    long long totalUnits,
    bool isFinished
) const {
    writeJsonLine(
        string("{\"event\":\"progress\",\"task\":\"") + escapeForJson(taskDescription)
        + "\",\"done\":" + to_string(completedUnits)
        + ",\"total\":" + to_string(totalUnits)
        + ",\"finished\":" + (isFinished ? "true" : "false") + "}"
    );
}
void ConsoleReport::writeMachineObject(const string& jsonObject) const {
    writeJsonLine(jsonObject);
}
void ConsoleReport::writeRawOutput(const string& text) const {
    // The text is written as it stands. The only liberty taken is a trailing
    // newline, so that the next thing printed starts on its own line.
    const bool needsTrailingNewline=text.empty() || text.back()!='\n';
    fputs((needsTrailingNewline ? text + "\n" : text).c_str(), stdout);
    fflush(stdout);
}
void ConsoleReport::writeExitEvent(int exitCode) const {
    writeJsonLine(string("{\"event\":\"exit\",\"exitCode\":") + to_string(exitCode) + "}");
}
void ConsoleReport::writeReadTraceEvent(long long readBytes, long long readCalls) const {
    writeJsonLine(
        string("{\"event\":\"reads\",\"bytes\":") + to_string(readBytes)
        + ",\"calls\":" + to_string(readCalls) + "}"
    );
}
void ConsoleReport::writeBanner(const string& commandName, const string& oneLinePurpose) const {
    if (machineReadable) {
        writeJsonLine(
            string("{\"event\":\"command\",\"name\":\"") + escapeForJson(commandName)
            + "\",\"purpose\":\"" + escapeForJson(oneLinePurpose) + "\"}"
        );
        return;
    }
    writeHeading("agent-db-doctor " + commandName);
    writeLine(oneLinePurpose);
    writeLine(separatorLine());
}
void ConsoleReport::writeHeading(const string& headingText) const {
    if (machineReadable) {
        writeJsonLine(string("{\"event\":\"heading\",\"text\":\"") + escapeForJson(headingText) + "\"}");
        return;
    }
    if (terminalIsAttached) {
        fputs((string(BOLD_ON) + headingText + BOLD_OFF + "\n").c_str(), stdout);
    } else {
        // A plain line, with no escape bytes anywhere in the output.
        fputs((headingText + "\n").c_str(), stdout);
    }
    fflush(stdout);
}
void ConsoleReport::writeLine(const string& lineText) const {
    if (machineReadable) {
        writeJsonLine(string("{\"event\":\"line\",\"text\":\"") + escapeForJson(lineText) + "\"}");
        return;
    }
    fputs((lineText + "\n").c_str(), stdout);
    fflush(stdout);
}
void ConsoleReport::writeDetailLine(const string& lineText) const {
    if (machineReadable) {
        writeJsonLine(string("{\"event\":\"detail\",\"text\":\"") + escapeForJson(lineText) + "\"}");
        return;
    }
    // Two spaces of indent is enough to show the relationship without pushing the
    // text off a narrow terminal.
    fputs(("  " + lineText + "\n").c_str(), stdout);
    fflush(stdout);
}
string ConsoleReport::separatorLine() {
    // The separator is built once per call into a string of a fixed width, which is
    // cheap and keeps the character count exact rather than approximate.
    return string(static_cast<size_t>(SEPARATOR_WIDTH), '-');
}
void ConsoleReport::writeDiagnosisReport(const DiagnosisReport& report) const {
    if (machineReadable) {
        // One object with every number the diagnostic pass produced. A script and
        // the graphical interface both read this, so a field here is a promise.
        string jsonText="{\"event\":\"database\"";
        jsonText+=",\"path\":\"" + escapeForJson(report.getDatabaseFilePath()) + "\"";
        jsonText+=",\"agent\":\"" + escapeForJson(report.getAgentName()) + "\"";
        jsonText+=",\"exists\":" + string(report.getFileExists() ? "true" : "false");
        jsonText+=",\"isDirectory\":" + string(report.getIsDirectory() ? "true" : "false");
        jsonText+=",\"healthy\":" + string(report.isHealthy() ? "true" : "false");
        jsonText+=",\"healthScore\":" + to_string(report.getHealthScore());
        jsonText+=",\"sizeBytes\":" + to_string(report.getLogicalSizeBytes());
        jsonText+=",\"allocatedBytes\":" + to_string(report.getAllocatedSizeBytes());
        jsonText+=",\"hasWriteAheadLog\":" + string(report.getHasWriteAheadLog() ? "true" : "false");
        jsonText+=",\"writeAheadLogBytes\":" + to_string(report.getWriteAheadLogSizeBytes());
        jsonText+=",\"modifiedMilliseconds\":" + to_string(report.getModificationEpochMilliseconds());
        jsonText+=",\"pageSizeBytes\":" + to_string(report.getPageSizeBytes());
        jsonText+=",\"pageCount\":" + to_string(report.getPageCount());
        jsonText+=",\"freePageCount\":" + to_string(report.getFreePageCount());
        jsonText+=",\"reclaimableBytes\":" + to_string(report.getReclaimableBytes());
        jsonText+=",\"journal\":\"" + escapeForJson(report.getJournalMode()) + "\"";
        jsonText+=",\"autoVacuum\":" + string(report.getAutoVacuumEnabled() ? "true" : "false");
        jsonText+=",\"compactionSupported\":" + string(report.getCompactionSupported() ? "true" : "false");
        jsonText+=",\"schema\":\"" + escapeForJson(report.getSchemaGenerationDescription()) + "\"";
        jsonText+=",\"redundantEventRows\":" + to_string(report.getRedundantEventRowCount());
        jsonText+=",\"integrity\":\"" + string(integrityStatusName(report.getIntegrityStatus())) + "\"";
        jsonText+=",\"integrityMessage\":\"" + escapeForJson(report.getIntegrityMessage()) + "\"";
        jsonText+=",\"databaseOpenSucceeded\":" + string(report.getDatabaseOpenSucceeded() ? "true" : "false");
        jsonText+=",\"openFailureCode\":" + to_string(report.getOpenFailureCode());
        jsonText+=",\"durationNanoseconds\":" + to_string(report.getDurationNanoseconds());
        jsonText+=",\"metadataCallCount\":" + to_string(report.getMetadataCallCount());
        jsonText+=",\"redundantEventTypes\":[";
        const vector<string> redundantTypes=report.getRedundantEventTypes();
        for (size_t typeIndex=0; typeIndex<redundantTypes.size(); ++typeIndex) {
            if (typeIndex>0) {
                jsonText+=",";
            }
            jsonText+="\"" + escapeForJson(redundantTypes[typeIndex]) + "\"";
        }
        jsonText+="]";
        jsonText+=",\"rows\":{";
        const vector<string> tableNames=report.countedTableNames();
        for (size_t tableIndex=0; tableIndex<tableNames.size(); ++tableIndex) {
            if (tableIndex>0) {
                jsonText+=",";
            }
            jsonText+="\"" + escapeForJson(tableNames[tableIndex]) + "\":{\"count\":"
                + to_string(report.rowCount(tableNames[tableIndex]))
                + ",\"approximate\":" + (report.isRowCountApproximate(tableNames[tableIndex]) ? "true" : "false")
                + "}";
        }
        jsonText+="}";
        jsonText+=",\"warnings\":[";
        const vector<string> warnings=report.getWarnings();
        for (size_t warningIndex=0; warningIndex<warnings.size(); ++warningIndex) {
            if (warningIndex>0) {
                jsonText+=",";
            }
            jsonText+="\"" + escapeForJson(warnings[warningIndex]) + "\"";
        }
        jsonText+="]";
        jsonText+=",\"nextSteps\":[";
        const vector<string> nextSteps=report.getSuggestedNextSteps();
        for (size_t stepIndex=0; stepIndex<nextSteps.size(); ++stepIndex) {
            if (stepIndex>0) {
                jsonText+=",";
            }
            jsonText+="\"" + escapeForJson(nextSteps[stepIndex]) + "\"";
        }
        jsonText+="]";
        jsonText+="}";
        writeJsonLine(jsonText);
        return;
    }
    const string agentLabel=report.getAgentName().empty()
        ? string("Database")
        : report.getAgentName() + " database";
    writeHeading(agentLabel + ": " + report.getDatabaseFilePath());
    if (!report.getFileExists()) {
        writeLine("No database was found at that path.");
        for (const string& warning : report.getWarnings()) {
            writeWarning(warning);
        }
        return;
    }
    // A folder is not a database, so the lines below, which describe a database's
    // pages and journal, have nothing true to say about it. They are skipped rather
    // than printed with placeholder values, and the folder is described instead.
    if (report.getIntegrityStatus()==IntegrityStatus::IS_A_FOLDER) {
        writeLine("This is a folder:       yes, so there is no database file here to measure");
        writeDetailLine("A folder's own size is the size of a directory entry, not the size of "
            "what is inside it. Adding up the files inside it would mean walking the whole "
            "folder, which this tool does not do, so no size is given here.");
        return;
    }
    // The sizes come first, because they are what a worried user wants to know.
    writeLine("Size on disk:          " + HumanReadableFormatting::formatBytes(report.getLogicalSizeBytes()));
    string occupiedShareText="";
    if (report.getLogicalSizeBytes()>0) {
        occupiedShareText=", which is " + HumanReadableFormatting::formatPercentage(
            static_cast<double>(report.getAllocatedSizeBytes())
                / static_cast<double>(report.getLogicalSizeBytes()),
            1
        ) + " of the size the file reports";
    }
    writeLine("Space the file uses:   " + HumanReadableFormatting::formatBytes(report.getAllocatedSizeBytes())
        + occupiedShareText);
    // The score is printed after the two sizes, because it is the other number a
    // worried user looks at first, and the trailing phrase at the extremes saves
    // them from having to know what a score of 100 or of 20 means.
    const int healthScore=report.getHealthScore();
    if (healthScore>=0) {
        string healthTrailer;
        if (healthScore==100) {
            healthTrailer=", nothing needs doing";
        } else if (healthScore<40) {
            healthTrailer=", needs attention";
        }
        writeLine("Health:                " + to_string(healthScore) + " of 100" + healthTrailer);
    }
    writeLine("Last written:          " + HumanReadableFormatting::formatTimestamp(report.getModificationEpochMilliseconds()));
    if (report.getHasWriteAheadLog()) {
        writeLine("Write ahead log:       " + HumanReadableFormatting::formatBytes(report.getWriteAheadLogSizeBytes())
            + " (a copy of the newest changes that has not been merged into the main file yet)");
    }
    if (report.getPageSizeBytes()>0) {
        writeLine("Page size:             " + HumanReadableFormatting::formatCount(report.getPageSizeBytes()) + " bytes");
        writeLine("Pages:                 " + HumanReadableFormatting::formatCount(report.getPageCount()));
        writeLine("Unused pages inside:   " + HumanReadableFormatting::formatCount(report.getFreePageCount())
            + " (" + HumanReadableFormatting::formatBytes(report.getReclaimableBytes()) + ")");
    }
    if (!report.getJournalMode().empty()) {
        // The journal mode is a technical name, so it is explained rather than
        // merely printed.
        const string modeExplanation=report.getJournalMode()=="wal"
            ? string("wal - changes are written to a side file first, which is fast")
            : string(report.getJournalMode() + " - changes are written to the main file first, which is slower but simpler");
        writeLine("How it stores changes: " + modeExplanation);
    }
    writeLine("Returns unused space itself: " + string(report.getAutoVacuumEnabled()
        ? "yes"
        : "no, so the vacuum command is what hands the space back to the drive"));
    if (!report.getSchemaGenerationDescription().empty()) {
        writeLine("Schema layout:           " + report.getSchemaGenerationDescription());
    }
    if (report.getCompactionSupported()) {
        for (const string& tableName : report.countedTableNames()) {
            const string approximateLabel=report.isRowCountApproximate(tableName)
                ? string(" (approximate)")
                : string("");
            writeLine(
                string("Rows in ") + tableName + ": " + HumanReadableFormatting::formatCount(report.rowCount(tableName))
                + approximateLabel
            );
        }
        if (!report.getRedundantEventTypes().empty()) {
            string joinedTypes;
            for (size_t typeIndex=0; typeIndex<report.getRedundantEventTypes().size(); ++typeIndex) {
                if (typeIndex>0) {
                    joinedTypes+=typeIndex+1==report.getRedundantEventTypes().size() ? " and " : ", ";
                }
                joinedTypes+=report.getRedundantEventTypes()[typeIndex];
            }
            writeLine("Redundant snapshots in the event log: "
                + HumanReadableFormatting::formatCount(report.getRedundantEventRowCount())
                + " (of type " + joinedTypes + ")");
        }
    }
    // The integrity line is phrased so that the good case is unmistakably good and
    // the bad case is unmistakably bad.
    switch (report.getIntegrityStatus()) {
        case IntegrityStatus::NOT_CHECKED:
            writeLine("Integrity:             not checked");
            break;
        case IntegrityStatus::IS_A_FOLDER:
            writeLine("Integrity:             not applicable, this is a folder rather than a database");
            break;
        case IntegrityStatus::INTACT:
            writeLine("Integrity:             every page checked out, the database is sound");
            break;
        case IntegrityStatus::NOT_A_DATABASE:
            // An empty file gets its own words, because saying its first bytes are
            // wrong when it has no bytes at all is not accurate.
            if (report.getLogicalSizeBytes()==0) {
                writeLine("Integrity:             the file is empty, so there is nothing in it to check");
            } else {
                writeLine("Integrity:             this file is not a SQLite database");
            }
            break;
        case IntegrityStatus::STRUCTURALLY_DAMAGED:
            writeLine("Integrity:             DAMAGED - the structure inside the file is broken");
            break;
        case IntegrityStatus::UNREADABLE:
            writeLine("Integrity:             UNREADABLE - the operating system cannot read the file");
            break;
        case IntegrityStatus::PROBLEM_REPORTED:
            writeLine("Integrity:             a problem was reported");
            break;
    }
    if (!report.getIntegrityMessage().empty()) {
        writeDetailLine(report.getIntegrityMessage());
    }
    if (report.getOpenFailureCode()!=0) {
        writeDetailLine("The raw code is " + to_string(report.getOpenFailureCode())
            + ", which is included so you can search for it.");
    }
    for (const string& warning : report.getWarnings()) {
        writeWarning(warning);
    }
    // The cost of the pass is reported because the specification makes it a
    // headline feature, and because a user who suspects the tool is slow can check.
    writeDetailLine("This check took "
        + HumanReadableFormatting::formatDuration(report.getDurationNanoseconds())
        + " and made " + to_string(report.getMetadataCallCount()) + " file system queries.");
}
void ConsoleReport::writeSafetyCheckResults(const vector<SafetyCheckResult>& results) const {
    if (machineReadable) {
        string jsonText="{\"event\":\"checks\",\"results\":[";
        for (size_t resultIndex=0; resultIndex<results.size(); ++resultIndex) {
            if (resultIndex>0) {
                jsonText+=",";
            }
            jsonText+="{\"passed\":" + string(results[resultIndex].passed() ? "true" : "false")
                + ",\"explanation\":\"" + escapeForJson(results[resultIndex].explanation()) + "\"}";
        }
        jsonText+="]}";
        writeJsonLine(jsonText);
        return;
    }
    writeHeading("Safety checks");
    for (const SafetyCheckResult& currentResult : results) {
        // The word is chosen so it cannot be read as a status code.
        writeLine(string(currentResult.passed() ? "  ok    " : "  BLOCKED")
            + "  " + currentResult.explanation());
    }
}
void ConsoleReport::writeRecoveryPlan(
    const string& agentName,
    const string& databaseFilePath,
    const vector<RecoveryRecommendation>& plan
) const {
    if (machineReadable) {
        string jsonText="{\"event\":\"plan\"";
        jsonText+=",\"agent\":\"" + escapeForJson(agentName) + "\"";
        jsonText+=",\"path\":\"" + escapeForJson(databaseFilePath) + "\"";
        jsonText+=",\"steps\":[";
        for (size_t stepIndex=0; stepIndex<plan.size(); ++stepIndex) {
            if (stepIndex>0) {
                jsonText+=",";
            }
            jsonText+="{\"title\":\"" + escapeForJson(plan[stepIndex].title()) + "\""
                + ",\"explanation\":\"" + escapeForJson(plan[stepIndex].explanation()) + "\""
                + ",\"command\":\"" + escapeForJson(plan[stepIndex].command()) + "\""
                + ",\"changesData\":" + (plan[stepIndex].isDestructive() ? "true" : "false")
                + ",\"severity\":\"" + escapeForJson(plan[stepIndex].describeSeverity()) + "\"}";
        }
        jsonText+="]}";
        writeJsonLine(jsonText);
        return;
    }
    writeHeading(RecoveryPlaybook::openingSentence(agentName, databaseFilePath));
    int stepNumber=0;
    for (const RecoveryRecommendation& recommendation : plan) {
        stepNumber++;
        writeLine("Step " + to_string(stepNumber) + ". " + recommendation.title()
            + "  [" + recommendation.describeSeverity() + "]");
        // The explanation is written with blank lines in it, so it is split and
        // printed as its own paragraphs rather than run together.
        string remainingText=recommendation.explanation();
        size_t paragraphStart=0;
        while (paragraphStart<remainingText.size()) {
            const size_t paragraphEnd=remainingText.find("\n\n", paragraphStart);
            const size_t endOfParagraph=paragraphEnd==string::npos
                ? remainingText.size()
                : paragraphEnd;
            writeDetailLine(remainingText.substr(paragraphStart, endOfParagraph - paragraphStart));
            if (paragraphEnd==string::npos) {
                break;
            }
            paragraphStart=paragraphEnd + 2;
        }
        if (!recommendation.command().empty()) {
            if (recommendation.isDestructive()) {
                writeDetailLine("This step writes to the file, so the tool will make a copy first and ask you to confirm.");
            }
            writeDetailLine("Run this:");
            writeDetailLine("    " + recommendation.command());
        }
    }
    if (stepNumber>0) {
        writeLine(separatorLine());
    }
}
void ConsoleReport::writeQuestion(const string& questionText) const {
    if (machineReadable) {
        writeJsonLine(string("{\"event\":\"question\",\"text\":\"") + escapeForJson(questionText) + "\"}");
        return;
    }
    writeLine(questionText);
}
void ConsoleReport::writeWarning(const string& warningText) const {
    if (machineReadable) {
        writeJsonLine(string("{\"event\":\"warning\",\"text\":\"") + escapeForJson(warningText) + "\"}");
        return;
    }
    // A blank line above and below makes a warning findable when a user is
    // scrolling, which is the only time they read this.
    writeLine("");
    if (terminalIsAttached) {
        fputs((string(BOLD_ON) + "! " + warningText + BOLD_OFF + "\n").c_str(), stdout);
    } else {
        fputs(("! " + warningText + "\n").c_str(), stdout);
    }
    writeLine("");
    fflush(stdout);
}
void ConsoleReport::writeSuggestedNextSteps(const vector<string>& suggestedSteps) const {
    if (suggestedSteps.empty()) {
        return;
    }
    if (machineReadable) {
        string jsonText="{\"event\":\"next\",\"steps\":[";
        for (size_t stepIndex=0; stepIndex<suggestedSteps.size(); ++stepIndex) {
            if (stepIndex>0) {
                jsonText+=",";
            }
            jsonText+="\"" + escapeForJson(suggestedSteps[stepIndex]) + "\"";
        }
        jsonText+="]}";
        writeJsonLine(jsonText);
        return;
    }
    writeHeading("What you can do next");
    for (const string& suggestedStep : suggestedSteps) {
        writeLine("  " + suggestedStep);
    }
}
void ConsoleReport::writeServiceStatus(const ServiceStatusSnapshot& status) const {
    if (machineReadable) {
        writeJsonLine(
            string("{\"event\":\"service\",\"installed\":")
            + (status.isInstalled ? "true" : "false")
            + ",\"running\":" + (status.isRunning ? "true" : "false")
            + ",\"state\":\"" + escapeForJson(status.stateDescription) + "\""
            + ",\"locations\":\"" + escapeForJson(status.installationDescription) + "\""
            + ",\"lastLogLine\":\"" + escapeForJson(status.mostRecentLogLine) + "\"}"
        );
        return;
    }
    writeHeading("The agent-db-doctor service");
    if (!status.isInstalled) {
        writeLine("Not installed. Nothing is being watched.");
        writeDetailLine(status.installationDescription);
        writeDetailLine("To install it, run: agent-db-doctor service install");
        return;
    }
    writeLine(status.stateDescription);
    writeDetailLine(status.installationDescription);
    if (!status.mostRecentLogLine.empty()) {
        writeLine("Most recent log line:");
        writeDetailLine(status.mostRecentLogLine);
    }
}
void ConsoleReport::writeFailure(const string& translatedExplanation, const string& rawCodeForSearching) const {
    if (machineReadable) {
        writeJsonLine(
            string("{\"event\":\"error\",\"message\":\"") + escapeForJson(translatedExplanation)
            + "\",\"code\":\"" + escapeForJson(rawCodeForSearching) + "\"}"
        );
        return;
    }
    writeWarning(translatedExplanation);
    if (!rawCodeForSearching.empty()) {
        // The code comes second and is labelled as a search term, because the
        // specification requires the sentence to come first and the code never to
        // appear on its own.
        writeDetailLine("Technical code, in case you want to search for it: " + rawCodeForSearching);
    }
}
void ConsoleReport::writeBackupLocation(const string& backupFilePath, long long backupSizeBytes) const {
    if (machineReadable) {
        writeJsonLine(
            string("{\"event\":\"backup\",\"path\":\"") + escapeForJson(backupFilePath)
            + "\",\"sizeBytes\":" + to_string(backupSizeBytes) + "}"
        );
        return;
    }
    writeHeading("A copy of your database was made first");
    writeLine("Nothing has been changed yet. The copy is at:");
    writeDetailLine(backupFilePath);
    writeDetailLine("It is " + HumanReadableFormatting::formatBytes(backupSizeBytes)
        + ". It is removed again once the run finishes and its checks pass, so it does not "
          "cost space in the end. Pass --keep-backup to keep it.");
}
}
