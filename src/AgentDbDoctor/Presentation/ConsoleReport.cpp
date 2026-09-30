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
ConsoleReport::ConsoleReport(bool isTerminalAttached)
    : terminalIsAttached(isTerminalAttached) {
}
void ConsoleReport::writeBanner(const string& commandName, const string& oneLinePurpose) const {
    writeHeading("agent-db-doctor " + commandName);
    writeLine(oneLinePurpose);
    writeLine(separatorLine());
}
void ConsoleReport::writeHeading(const string& headingText) const {
    if (terminalIsAttached) {
        fputs((string(BOLD_ON) + headingText + BOLD_OFF + "\n").c_str(), stdout);
    } else {
        // A plain line, with no escape bytes anywhere in the output.
        fputs((headingText + "\n").c_str(), stdout);
    }
    fflush(stdout);
}
void ConsoleReport::writeLine(const string& lineText) const {
    fputs((lineText + "\n").c_str(), stdout);
    fflush(stdout);
}
void ConsoleReport::writeDetailLine(const string& lineText) const {
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
        : "no, so the compact command is what shrinks the file"));
    if (report.getPruningSupported()) {
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
            writeLine("Older copies of changed messages: "
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
        case IntegrityStatus::INTACT:
            writeLine("Integrity:             every page checked out, the database is sound");
            break;
        case IntegrityStatus::NOT_A_DATABASE:
            writeLine("Integrity:             this file is not a SQLite database");
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
    writeLine(questionText);
}
void ConsoleReport::writeWarning(const string& warningText) const {
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
    writeHeading("What you can do next");
    for (const string& suggestedStep : suggestedSteps) {
        writeLine("  " + suggestedStep);
    }
}
void ConsoleReport::writeServiceStatus(const ServiceStatusSnapshot& status) const {
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
    writeWarning(translatedExplanation);
    if (!rawCodeForSearching.empty()) {
        // The code comes second and is labelled as a search term, because the
        // specification requires the sentence to come first and the code never to
        // appear on its own.
        writeDetailLine("Technical code, in case you want to search for it: " + rawCodeForSearching);
    }
}
void ConsoleReport::writeBackupLocation(const string& backupFilePath, long long backupSizeBytes) const {
    writeHeading("A copy of your database was made first");
    writeLine("Nothing has been changed yet. The copy is at:");
    writeDetailLine(backupFilePath);
    writeDetailLine("It is " + HumanReadableFormatting::formatBytes(backupSizeBytes)
        + ". It will not be deleted by this tool. Remove it yourself once you are happy.");
}
}
