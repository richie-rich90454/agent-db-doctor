// The implementation of the AutomaticMaintenance class.
//
// The order of the steps below is the whole safety argument and it matches the
// order of the gates in the header. Every gate that fails produces a sentence for
// the log rather than a silent return, because the difference between "the tool
// decided to wait" and "the tool forgot this database" is exactly the difference
// this tool exists to remove.
// Provides `chrono`, used for the wall clock reading that decides whether a
// database has gone quiet.
#include <chrono>
// Provides `exception`, the base type the final catch turns into a log line.
#include <exception>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the AutomaticMaintenance class declaration.
#include "AgentDbDoctor/Service/AutomaticMaintenance.hpp"
// Provides the compaction command the first step runs.
#include "AgentDbDoctor/Command/CompactCommand.hpp"
// Provides the vacuum command the second step runs.
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
// Provides the audit log every step is recorded in.
#include "AgentDbDoctor/Service/AuditLog.hpp"
// Provides PlatformMetadata, for the sizes and the modification time.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides SafetyChecks, for the check that no other program holds the file.
#include "AgentDbDoctor/Doctor/SafetyChecks.hpp"
// Provides the number to text conversions used in the log line.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
// Provides every exception type the steps can raise, which are all turned into
// log lines here.
#include "AgentDbDoctor/Common/Exceptions.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Service {
// The name every log line begins with, so a reader can tell the service's lines
// from the agent's own.
static constexpr const char* SERVICE_NAME="agent-db-doctor";
// The number of minutes the tests asked for, or a negative value when the tests
// have not asked and the default applies.
static int idleMinutesOverride=-1;
// The number of minutes of quiet a database needs before it is touched.
static int effectiveIdleMinutes() {
    return idleMinutesOverride>=0 ? idleMinutesOverride : AutomaticMaintenance::AUTO_MAINTENANCE_IDLE_MINUTES;
}
// The wall clock reading the idle rule compares against, as milliseconds since
// the Unix epoch, given in the same unit as the modification time it is compared
// with.
static long long currentEpochMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}
void AutomaticMaintenance::setIdleMinutesForTests(int minutes) {
    idleMinutesOverride=minutes;
}
void AutomaticMaintenance::appendAuditEntry(
    const string& action,
    const string& databaseFilePath,
    long long sizeBeforeBytes,
    long long sizeAfterBytes,
    const string& backupFilePath,
    const string& outcome,
    int exitCode
) {
    AuditEntry entry;
    entry.epochSeconds=std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    entry.action=action;
    entry.databaseFilePath=databaseFilePath;
    entry.sizeBeforeBytes=sizeBeforeBytes;
    entry.sizeAfterBytes=sizeAfterBytes;
    entry.backupFilePath=backupFilePath;
    entry.outcome=outcome;
    entry.exitCode=exitCode;
    // The result is deliberately ignored: a log that cannot be written is worth
    // a line in the service's own log, but it is not a reason to fail the
    // maintenance that already happened or to skip the one about to happen.
    static_cast<void>(AuditLog::append(entry));
}
AutomaticMaintenance::AutomaticMaintenance(
    const ServiceConfiguration& configuration,
    const ConsoleReport& reporter
) : serviceConfiguration(configuration),
    reporter(reporter) {
}
AutomaticMaintenance::MaintenanceRunSummary AutomaticMaintenance::runForIfRequired(
    const string& databaseFilePath,
    long long currentSizeBytes
) {
    MaintenanceRunSummary summary;
    summary.databaseFilePath=databaseFilePath;
    const DatabasePolicy* policy=serviceConfiguration.policyFor(databaseFilePath);
    const long long criticalThresholdBytes=policy!=nullptr && policy->criticalThresholdBytes>0
        ? policy->criticalThresholdBytes
        : serviceConfiguration.criticalThresholdBytes;
    if (currentSizeBytes<criticalThresholdBytes) {
        summary.logLine=string(SERVICE_NAME) + ": automatic maintenance was not needed for "
            + databaseFilePath + ", which is " + HumanReadableFormatting::formatBytes(currentSizeBytes)
            + " and below its urgent threshold of "
            + HumanReadableFormatting::formatBytes(criticalThresholdBytes) + ".";
        return summary;
    }
    const bool maintenanceIsAllowed=policy!=nullptr
        ? policy->allowAutomaticCompaction
        : serviceConfiguration.allowAutomaticCompaction;
    if (!maintenanceIsAllowed) {
        summary.logLine=string(SERVICE_NAME) + ": automatic maintenance on " + databaseFilePath
            + " was not attempted, because automatic changes are not allowed for it. "
            + (policy!=nullptr
                ? string("Its policy sets allowAutomaticCompaction to false.")
                : string("The configuration's allowAutomaticCompaction is false, and no policy "
                    "names this database to allow it."));
        return summary;
    }
    const FileMetadata fileStatus=PlatformMetadata::readFileMetadata(databaseFilePath);
    if (!fileStatus.exists || fileStatus.isDirectory) {
        summary.logLine=string(SERVICE_NAME) + ": automatic maintenance on " + databaseFilePath
            + " was not attempted, because there is no database file at that path.";
        return summary;
    }
    const int idleMinutes=effectiveIdleMinutes();
    const long long idleMilliseconds=currentEpochMilliseconds()-fileStatus.modificationEpochMilliseconds;
    if (idleMilliseconds<static_cast<long long>(idleMinutes)*60LL*1000LL) {
        // An agent that wrote a second ago is in use. Waiting is not a failure
        // and the next tick will try again, which is why the line says so.
        summary.logLine=string(SERVICE_NAME) + ": automatic maintenance on " + databaseFilePath
            + " was not attempted, because it was written to less than "
            + to_string(idleMinutes) + (idleMinutes==1 ? " minute" : " minutes")
            + " ago, so it is in use. The next check will try again.";
        return summary;
    }
    const SafetyCheckResult lockCheck=SafetyChecks::verifyDatabaseIsNotLocked(databaseFilePath);
    if (!lockCheck.passed()) {
        summary.logLine=string(SERVICE_NAME) + ": automatic maintenance on " + databaseFilePath
            + " was not attempted, because another program holds it open. "
            + lockCheck.explanation();
        return summary;
    }
    // Every gate has passed, so the steps run. The options below are the whole
    // difference between this run and a manual one: nothing is printed beyond what
    // the commands print anyway, nobody is asked, and nothing is a preview. The
    // commands still make their own safety copies and still verify what they did,
    // so every promise the tool makes about a manual run holds here too.
    summary.ran=true;
    const long long sizeBeforeBytes=fileStatus.logicalSizeBytes;
    CommandOptions maintenanceOptions;
    maintenanceOptions.databaseFilePath=databaseFilePath;
    maintenanceOptions.quiet=true;
    maintenanceOptions.answerYesToEverything=true;
    maintenanceOptions.dryRunOnly=false;
    // The step that is running, so a failure can name it in the audit log and the
    // log line. It starts at the compaction because that is the first thing tried,
    // and the size at the step's start is kept beside it so a failure entry still
    // carries honest numbers.
    string currentStep="automatic-compaction";
    long long sizeAtStepStartBytes=sizeBeforeBytes;
    const auto noteFailure=[
        &databaseFilePath,
        &summary,
        &currentStep,
        &sizeAtStepStartBytes
    ](const string& explanation) {
        summary.logLine=string(SERVICE_NAME) + ": automatic maintenance on " + databaseFilePath
            + " stopped at the " + (currentStep=="automatic-compaction" ? string("compaction") : string("vacuum"))
            + ". " + explanation + " Nothing else was attempted; the database is as that step left it.";
        // The failed step is recorded with exit code one, which is the same code
        // the command line gives a step that was refused or failed.
        appendAuditEntry(
            currentStep,
            databaseFilePath,
            sizeAtStepStartBytes,
            PlatformMetadata::readFileMetadata(databaseFilePath).logicalSizeBytes,
            string(),
            explanation,
            1
        );
    };
    try {
        // The compaction comes first because it is what the vacuum then shrinks:
        // removing rows leaves free pages, and the vacuum is what hands those
        // pages back. A refusal, or a run that removed nothing, does not stop the
        // vacuum, because a file can hold unused pages without holding anything
        // to compact.
        CompactCommand compactCommand(reporter.isTerminalAttached());
        const int compactExitCode=compactCommand.execute(maintenanceOptions);
        const EventLogCompactionReport& compactionOutcome=compactCommand.lastOutcome();
        summary.compactRan=compactionOutcome.anythingWasRemoved;
        summary.compactedRows=compactionOutcome.removedRowCount;
        const long long sizeAfterCompactionBytes=
            PlatformMetadata::readFileMetadata(databaseFilePath).logicalSizeBytes;
        appendAuditEntry(
            "automatic-compaction",
            databaseFilePath,
            sizeBeforeBytes,
            sizeAfterCompactionBytes,
            compactionOutcome.safetyCopy.backupFilePath,
            compactionOutcome.summarySentence,
            compactExitCode
        );
        currentStep="automatic-vacuum";
        sizeAtStepStartBytes=sizeAfterCompactionBytes;
        VacuumCommand vacuumCommand(reporter.isTerminalAttached());
        const int vacuumExitCode=vacuumCommand.execute(maintenanceOptions);
        const VacuumOutcome& vacuumOutcome=vacuumCommand.lastOutcome();
        summary.vacuumRan=vacuumOutcome.wasCompacted;
        const long long sizeAfterBytes=PlatformMetadata::readFileMetadata(databaseFilePath).logicalSizeBytes;
        appendAuditEntry(
            "automatic-vacuum",
            databaseFilePath,
            sizeAfterCompactionBytes,
            sizeAfterBytes,
            string(),
            vacuumOutcome.summarySentence,
            vacuumExitCode
        );
        summary.bytesFreed=sizeBeforeBytes-sizeAfterBytes;
        // The steps that ran, named the way the log line names them.
        string stepsText="neither step removed anything";
        if (summary.compactRan && summary.vacuumRan) {
            stepsText="compaction and vacuum";
        } else if (summary.compactRan) {
            stepsText="compaction";
        } else if (summary.vacuumRan) {
            stepsText="vacuum";
        }
        if (summary.bytesFreed>0) {
            summary.logLine=string(SERVICE_NAME) + ": automatic maintenance on " + databaseFilePath
                + " gave back " + HumanReadableFormatting::formatBytes(summary.bytesFreed)
                + " (" + stepsText + ").";
        } else {
            summary.logLine=string(SERVICE_NAME) + ": automatic maintenance on " + databaseFilePath
                + " gave back nothing (" + stepsText + ").";
        }
    } catch (const ConfigurationException& failure) {
        noteFailure(failure.message());
    } catch (const ServiceOperationException& failure) {
        noteFailure(failure.message());
    } catch (const FileSystemException& failure) {
        noteFailure(failure.message());
    } catch (const DatabaseOpenException& failure) {
        noteFailure(failure.message());
    } catch (const DatabaseOperationException& failure) {
        noteFailure(failure.message());
    } catch (const PragmaException& failure) {
        noteFailure(failure.message());
    } catch (const InsufficientDiskSpaceException& failure) {
        noteFailure(failure.message());
    } catch (const AgentDatabaseMissingException& failure) {
        noteFailure(failure.message());
    } catch (const UserDeclinedException& failure) {
        noteFailure(failure.message());
    } catch (const std::exception& failure) {
        // A failure from the standard library itself, such as running out of
        // memory. The service cannot act on it, and it must not die from it.
        noteFailure(failure.what());
    } catch (...) {
        noteFailure("The failure was not one this version of the tool has words for.");
    }
    return summary;
}
}
