// The implementation of the ReclaimCommand class.
//
// The command is deliberately a composition rather than a new engine: it runs the
// compact command and then the vacuum command, both with their copies, their
// checks, and their restores intact, and it reports the one number a person wants,
// which is how many bytes the drive actually got back. The single confirmation
// belongs to this command, so the operations underneath run pre-answered.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `cin` and `getline`, for the typed confirmation.
#include <iostream>
// Provides `tolower`, used by the confirmation comparison.
#include <cctype>
// Provides the ReclaimCommand class declaration.
#include "AgentDbDoctor/Command/ReclaimCommand.hpp"
// Provides the two commands this one composes.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
// Provides the doctor, for the size before and after.
#include "AgentDbDoctor/Doctor/AgentDatabaseDoctor.hpp"
// Provides PlatformMetadata, for the file metadata.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
ReclaimCommand::ReclaimCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string ReclaimCommand::describe() const {
    return string(
        "Give a target amount of space back: compaction first, because it makes the vacuum "
        "smaller, then the vacuum. Each step keeps its own copy and its own checks."
    );
}
string ReclaimCommand::commandName() const {
    return string("reclaim");
}
long long ReclaimCommand::parseTargetBytes(const string& targetText) {
    if (targetText.empty()) {
        return 0;
    }
    // The suffixes are the ones people type. The comparison is case insensitive, so
    // `20gb` and `20GB` are the same target.
    string upperCaseText;
    upperCaseText.reserve(targetText.size());
    for (const char currentCharacter : targetText) {
        upperCaseText.push_back(static_cast<char>(toupper(static_cast<unsigned char>(currentCharacter))));
    }
    long long multiplier=1;
    string numberText=upperCaseText;
    const vector<pair<string, long long>> suffixes={
        pair<string, long long>("TB", 1024LL * 1024LL * 1024LL * 1024LL),
        pair<string, long long>("GB", 1024LL * 1024LL * 1024LL),
        pair<string, long long>("MB", 1024LL * 1024LL),
        pair<string, long long>("KB", 1024LL),
        pair<string, long long>("B", 1)
    };
    for (const pair<string, long long>& suffix : suffixes) {
        if (upperCaseText.size()>suffix.first.size()
            && upperCaseText.compare(
                upperCaseText.size() - suffix.first.size(),
                suffix.first.size(),
                suffix.first
            )==0) {
            multiplier=suffix.second;
            numberText=upperCaseText.substr(0, upperCaseText.size() - suffix.first.size());
            break;
        }
    }
    // The digits are parsed by hand: `stoll` would accept leading spaces and stop at
    // the first non digit, and a target is either a size or a mistake.
    long long value=0;
    if (numberText.empty()) {
        return 0;
    }
    for (const char currentCharacter : numberText) {
        if (currentCharacter<'0' || currentCharacter>'9') {
            return 0;
        }
        value=value * 10 + (currentCharacter - '0');
        if (value<0) {
            return 0;
        }
    }
    if (value==0) {
        return 0;
    }
    return value * multiplier;
}
bool ReclaimCommand::confirmWithUser(const CommandOptions& options, const string& questionText) const {
    if (options.answerYesToEverything) {
        return true;
    }
    reporter.writeQuestion(questionText);
    reporter.writeQuestion("Type yes to go ahead, or anything else to stop: ");
    string answer;
    if (!getline(cin, answer)) {
        reporter.writeLine("");
        reporter.writeLine(
            "There is nobody to answer, so nothing was changed. Pass --yes to run without asking."
        );
        return false;
    }
    string trimmedAnswer=answer;
    while (!trimmedAnswer.empty() && (trimmedAnswer.back()==' ' || trimmedAnswer.back()=='\r')) {
        trimmedAnswer.pop_back();
    }
    if (trimmedAnswer.size()!=3) {
        return false;
    }
    string lowerCaseAnswer=trimmedAnswer;
    for (char& currentCharacter : lowerCaseAnswer) {
        currentCharacter=static_cast<char>(tolower(static_cast<unsigned char>(currentCharacter)));
    }
    return lowerCaseAnswer=="yes";
}
int ReclaimCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (options.databaseFilePath.empty()) {
        reporter.writeFailure(
            string("This command needs to know which database to reclaim space from. Tell it with "
                "--database."),
            string()
        );
        return 1;
    }
    const long long targetBytes=parseTargetBytes(options.reclaimTarget);
    if (targetBytes<=0) {
        reporter.writeFailure(
            string("This command needs a target: how much space to give back. For example:\n"
                "  agent-db-doctor reclaim --database <path> --target 20GB\n\n"
                "The target accepts KB, MB, GB, and TB, or a plain number of bytes."),
            string()
        );
        return 2;
    }
    try {
        AgentDatabaseDoctor doctor;
        const DiagnosisReport reportBefore=doctor.diagnoseDatabase(
            options.databaseFilePath,
            options.agentName,
            false
        );
        if (!reportBefore.getFileExists()) {
            reporter.writeFailure(
                string("There is no database file at ") + options.databaseFilePath + ".", string()
            );
            return 1;
        }
        const long long sizeBeforeBytes=reportBefore.getLogicalSizeBytes();
        const long long freePageBytes=reportBefore.getReclaimableBytes();
        const bool compactionWillRemoveAnything=reportBefore.getCompactionSupported()
            && reportBefore.getRedundantEventRowCount()>0;
        // The plan. The vacuum's share is exact, because it is the free list; the
        // compaction's share is what makes the vacuum's share, so the two are not
        // added together. That is said plainly rather than dressed up.
        if (!reporter.isMachineReadable()) {
            reporter.writeHeading("The plan");
            reporter.writeLine("Size now: " + HumanReadableFormatting::formatBytes(sizeBeforeBytes) + ".");
            if (compactionWillRemoveAnything) {
                reporter.writeLine(
                    string("Compaction would remove ") 
                    + HumanReadableFormatting::formatCount(reportBefore.getRedundantEventRowCount())
                    + " redundant snapshots first, which is what makes the vacuum smaller and "
                      "faster."
                );
            } else {
                reporter.writeLine(
                    "Compaction has nothing to remove: the newest snapshot of every message is "
                    "already the only copy of it."
                );
            }
            reporter.writeLine(
                string("The vacuum then hands the file's unused pages back: about ")
                + HumanReadableFormatting::formatBytes(freePageBytes) + " of the file today, more "
                  "after any compaction."
            );
            reporter.writeLine(
                string("Target: ") + HumanReadableFormatting::formatBytes(targetBytes) + "."
            );
        }
        if (!confirmWithUser(
            options,
            string("This runs compaction, if there is anything to compact, and then a vacuum on ")
            + options.databaseFilePath + ". Each step makes its own safety copy first and checks "
            "its own result, and the copies are removed again when their checks pass."
        )) {
            reporter.writeLine("Nothing was changed.");
            return 0;
        }
        // The two operations run with the question already answered, because the
        // question was asked here, once.
        CommandOptions stepOptions=options;
        stepOptions.answerYesToEverything=true;
        stepOptions.dryRunOnly=false;
        long long compactedRows=0;
        if (compactionWillRemoveAnything) {
            CompactCommand compactCommand(false);
            const int compactExitCode=compactCommand.execute(stepOptions);
            compactedRows=compactCommand.lastOutcome().removedRowCount;
            if (compactExitCode!=0 || compactCommand.lastOutcome().wasRefused) {
                reporter.writeLine(
                    "The compaction did not finish, so the vacuum was not started. Nothing else "
                    "was changed; run the command again when the reason above is resolved."
                );
                return 1;
            }
        }
        VacuumCommand vacuumCommand(false);
        const int vacuumExitCode=vacuumCommand.execute(stepOptions);
        if (vacuumExitCode!=0 || vacuumCommand.lastOutcome().wasRefused) {
            reporter.writeLine(
                "The vacuum did not finish. The compaction's result, if any, is in place; run the "
                "command again when the reason above is resolved."
            );
            return 1;
        }
        const long long sizeAfterBytes=vacuumCommand.lastOutcome().sizeAfterBytes>0
            ? vacuumCommand.lastOutcome().sizeAfterBytes
            : PlatformMetadata::readFileMetadata(options.databaseFilePath).logicalSizeBytes;
        const long long freedBytes=sizeBeforeBytes-sizeAfterBytes;
        const bool targetWasMet=freedBytes>=targetBytes;
        if (!reporter.isMachineReadable()) {
            reporter.writeHeading("Done");
            reporter.writeLine(
                string("Given back to the drive: ") + HumanReadableFormatting::formatBytes(freedBytes)
                + (freedBytes>0 ? " (" + HumanReadableFormatting::formatBytes(sizeBeforeBytes)
                    + " to " + HumanReadableFormatting::formatBytes(sizeAfterBytes) + ")" : string())
            );
            if (targetWasMet) {
                reporter.writeLine(
                    string("The target of ") + HumanReadableFormatting::formatBytes(targetBytes)
                    + " was met."
                );
            } else {
                reporter.writeLine(
                    string("The target of ") + HumanReadableFormatting::formatBytes(targetBytes)
                    + " was not met: everything that could be reclaimed safely has been. The rest "
                      "is the conversation itself."
                );
                reporter.writeSuggestedNextSteps(vector<string>{
                    string("explain-space --sessions 10 ranks the conversations by the payload they store."),
                    string("sessions delete --session <id> removes one, with the same copy and check."),
                    string("An older database of the same agent may hold the rest: agent-db-doctor look")
                });
            }
        }
        if (reporter.isMachineReadable()) {
            reporter.writeMachineResult("reclaim", vector<MachineField>{
                MachineField::number("targetBytes", targetBytes),
                MachineField::number("sizeBeforeBytes", sizeBeforeBytes),
                MachineField::number("sizeAfterBytes", sizeAfterBytes),
                MachineField::number("freedBytes", freedBytes),
                MachineField::boolean("targetMet", targetWasMet),
                MachineField::number("compactedRows", compactedRows),
                MachineField::number("vacuumBytesReclaimed", vacuumCommand.lastOutcome().bytesReclaimed)
            });
        }
        return 0;
    } catch (const DatabaseOpenException& openFailure) {
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        return 1;
    } catch (const DatabaseOperationException& operationFailure) {
        reporter.writeFailure(operationFailure.message(), string());
        return 1;
    }
}
}
