// The command line program.
//
// Every line of user facing text the program can print comes from one of the
// command classes, so this file only has to work out which command was asked for,
// turn the command line into a CommandOptions value, and turn the return value into
// an exit code. That is the whole of its job, and keeping it that small is what
// makes the tool easy to extend without making the entry point unreadable.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the CLI11 header only command line parser.
#include <CLI/CLI.hpp>
// Provides the CommandOptions value type, which every command reads.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the five command classes.
#include "AgentDbDoctor/Command/DiagnoseCommand.hpp"
#include "AgentDbDoctor/Command/CompactCommand.hpp"
#include "AgentDbDoctor/Command/VacuumCommand.hpp"
#include "AgentDbDoctor/Command/RecoverCommand.hpp"
#include "AgentDbDoctor/Command/ServiceCommand.hpp"
// Provides the command that manages the safety copies.
#include "AgentDbDoctor/Command/BackupsCommand.hpp"
// Provides ConsoleReport, which prints every line the program writes.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides PlatformMetadata, for the terminal test.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::FileSystem;
// The exit code used when the program stops because of a problem rather than
// because of a bad answer. It is one, which is what a shell script checks for.
static constexpr int EXIT_CODE_PROBLEM=1;
// The exit code used when the command line itself was wrong. It is two, which is
// the convention every other program on these platforms follows.
static constexpr int EXIT_CODE_USAGE=2;
// Adds the options that say which database the command works on.
void addTargetingOptions(CLI::App& application, CommandOptions& options) {
    // The database flag is the only one most people ever need, so it is described
    // in terms of what it is for rather than in terms of the tool's internals.
    application.add_option(
        "-d,--database",
        options.databaseFilePath,
        "The database file to work on. Leave this out and the tool finds it for you where it can."
    );
    application.add_option(
        "-a,--agent",
        options.agentName,
        "Only look at this coding agent, for example OpenCode."
    );
}
// Adds the options that govern how much is printed.
void addOutputOptions(CLI::App& application, CommandOptions& options) {
    application.add_flag(
        "-q,--quiet",
        options.quiet,
        "Print only the lines that matter. Useful in a script."
    );
    application.add_flag(
        "-v,--verbose",
        options.verbose,
        "Write extra detail to the log, including every path the tool considered."
    );
    application.add_flag(
        "--machine-readable",
        options.machineReadableOutput,
        "Print one JSON object per line instead of sentences, for a script or another program."
    );
}
// Adds the options that govern a run which changes something: the confirmation and
// the preview.
void addConfirmationOptions(CLI::App& application, CommandOptions& options) {
    application.add_flag(
        "-y,--yes",
        options.answerYesToEverything,
        "Do not ask before doing something that changes the database. Required when running with no terminal."
    );
    application.add_flag(
        "-n,--dry-run",
        options.dryRunOnly,
        "Report what would happen and change nothing."
    );
}
// Adds the options that describe where the safety copy goes and whether it stays.
void addBackupOptions(CLI::App& application, CommandOptions& options) {
    application.add_option(
        "--backup-to",
        options.backupFilePath,
        "Where to write the safety copy. Leave this out and the tool puts it beside the database. Naming a path keeps the copy."
    );
    application.add_flag(
        "--keep-backup",
        options.keepBackup,
        "Keep the safety copy after a successful run. By default it is removed once the checks pass, so the space it holds comes back too."
    );
}
// Adds the options only compaction uses.
void addCompactionOptions(CLI::App& application, CommandOptions& options) {
    application.add_option(
        "-k,--keep",
        options.sessionsToKeep,
        "How many of the most recent sessions to keep when compacting. Zero, the default, keeps the newest half."
    );
    application.add_option(
        "--max-fraction",
        options.maximumFractionToRemove,
        "Refuse to compact more than this share of the event table. The default is 0.5, or half."
    );
    application.add_option(
        "--batch-size",
        options.deletionBatchSize,
        "How many rows to remove in one transaction when compacting. The default is 10000."
    );
}
int main(int argumentCount, char** argumentValues) {
    // The terminal flag is read once, here, because every line the program prints
    // needs it and asking the operating system per line would be wasteful.
    const bool terminalIsAttached=PlatformMetadata::isStandardOutputATerminal();
    ConsoleReport reporter(terminalIsAttached);
    CommandOptions options;
    CLI::App application(
        "agent-db-doctor - a doctor for AI coding agent databases.\n"
        "Diagnose, compact, vacuum, recover, and monitor the databases that AI coding agents use to "
        "remember your work."
    );
    application.require_subcommand(1);
    // The program name and version are compiled in, so `--version` never has to
    // read a file from disk.
    application.set_version_flag("--version", string(AGENT_DB_DOCTOR_VERSION));
    // The subcommands are declared with a one line description each, which is what
    // the command list shows.
    CLI::App* diagnoseSubcommand=application.add_subcommand(
        "diagnose",
        "Look at every supported database and report what is wrong. Changes nothing."
    );
    addTargetingOptions(*diagnoseSubcommand, options);
    addOutputOptions(*diagnoseSubcommand, options);
    diagnoseSubcommand->add_flag(
        "--check",
        options.runIntegrityCheck,
        "Run the full page by page integrity check. Slower, and it reads the whole file."
    );
    diagnoseSubcommand->add_flag(
        "--trace-reads",
        options.traceReadCounts,
        "Also report how many bytes were read, counted through a wrapper around SQLite's own file layer."
    );
    CLI::App* compactSubcommand=application.add_subcommand(
        "compact",
        "Compact the event log: keep the newest snapshot of every message and remove the older "
        "ones, after copying your database and checking nothing was lost."
    );
    addTargetingOptions(*compactSubcommand, options);
    addOutputOptions(*compactSubcommand, options);
    addConfirmationOptions(*compactSubcommand, options);
    addBackupOptions(*compactSubcommand, options);
    addCompactionOptions(*compactSubcommand, options);
    CLI::App* vacuumSubcommand=application.add_subcommand(
        "vacuum",
        "Give unused space inside the database back to the drive by rewriting the file."
    );
    addTargetingOptions(*vacuumSubcommand, options);
    addOutputOptions(*vacuumSubcommand, options);
    addConfirmationOptions(*vacuumSubcommand, options);
    addBackupOptions(*vacuumSubcommand, options);
    CLI::App* recoverSubcommand=application.add_subcommand(
        "recover",
        "Work out what is wrong with a database that will not open, and say what can be fixed."
    );
    addTargetingOptions(*recoverSubcommand, options);
    addOutputOptions(*recoverSubcommand, options);
    addConfirmationOptions(*recoverSubcommand, options);
    addBackupOptions(*recoverSubcommand, options);
    recoverSubcommand->add_flag(
        "--rebuild",
        options.rebuildWorkingCopy,
        "Build a working copy holding every row that can still be read. The original is never modified."
    );
    CLI::App* serviceSubcommand=application.add_subcommand(
        "service",
        "Install and control the background service that watches database sizes."
    );
    addOutputOptions(*serviceSubcommand, options);
    addConfirmationOptions(*serviceSubcommand, options);
    serviceSubcommand->add_option(
        "--warning-threshold",
        options.warningThresholdBytes,
        "The size in bytes a database must reach before the service mentions it. The default is 53687091200, which is 50 GB."
    );
    serviceSubcommand->add_option(
        "--interval",
        options.watchdogIntervalSeconds,
        "How many seconds the service waits between checks. The default is 3600, which is an hour."
    );
    serviceSubcommand->add_option(
        "action",
        options.serviceAction,
        "One of: install, uninstall, start, stop, status, run."
    );
    CLI::App* backupsSubcommand=application.add_subcommand(
        "backups",
        "List, verify, restore, or delete the safety copies made before a destructive run."
    );
    addOutputOptions(*backupsSubcommand, options);
    addConfirmationOptions(*backupsSubcommand, options);
    backupsSubcommand->add_option(
        "action",
        options.backupsAction,
        "One of: list, verify, restore, delete. The default is list."
    );
    backupsSubcommand->add_option(
        "--file",
        options.backupsFilePath,
        "Act on this copy instead of the newest one beside the database."
    );
    backupsSubcommand->add_flag(
        "--all",
        options.backupsDeleteAll,
        "Verify every copy rather than the newest, or remove every copy rather than the old ones."
    );
    backupsSubcommand->add_option(
        "--older-than",
        options.backupsOlderThanDays,
        "When deleting, remove copies older than this many days. Required unless --all is given."
    );
    // The parse is the only part of start up that allocates, and it is finished
    // before any measurement of start up cost is taken.
    try {
        // `allow_extras` is false, so an unknown option is an error rather than
        // something silently ignored. A tool that ignores what it does not
        // understand is a tool that surprises people.
        application.parse(argumentCount, argumentValues);
    } catch (const CLI::ParseError& parseFailure) {
        // CLI11 reports a help request or a version request as an exception too, so
        // both are handled here rather than treated as a mistake. A help or version
        // request is a success, so it exits zero.
        if (parseFailure.get_exit_code()==0) {
            return application.exit(parseFailure);
        }
        // A genuine mistake gets the same treatment as every other error this tool
        // raises: a sentence, an explanation, and the command to run next. CLI11's own
        // one line and its exit code of one hundred and six are neither of them, and
        // one hundred and six is not a code a shell script expects.
        fputs(
            (string("There is a problem with the command line.\n\n")
             + parseFailure.what()
             + "\n\nRun agent-db-doctor --help to see what each command does, or "
               "agent-db-doctor diagnose to look at your databases without changing anything.\n").c_str(),
            stderr
        );
        fflush(stderr);
        return EXIT_CODE_USAGE;
    }
    // The service is the one command that must run with no terminal, so a missing
    // `--yes` is a usage error there rather than a question nobody can answer.
    const string chosenSubcommand=application.get_subcommands().empty()
        ? string()
        : application.get_subcommands().front()->get_name();
    // Machine readable output turns every report into one JSON object per line and
    // installs a progress sink that does the same for the numbers a long operation
    // reports. Both the scripts people write and the graphical interface read this,
    // so the shape is documented rather than incidental.
    if (options.machineReadableOutput) {
        ConsoleReport::setMachineReadableForProcess(true);
        reporter.enableMachineReadableOutput();
        long long lastReportedPercent=-1;
        options.progressSink=[&reporter, lastReportedPercent](const ProgressUpdate& update) mutable {
            const long long percent=update.totalUnits>0
                ? (update.completedUnits * 100) / update.totalUnits
                : -1;
            // One object per percent, plus the final one. A sink called per row
            // would flood a pipe; a sink called per percent follows the run at the
            // pace a person reads.
            if (percent==lastReportedPercent && !update.isFinished) {
                return;
            }
            lastReportedPercent=percent;
            reporter.writeProgressEvent(
                update.taskDescription,
                update.completedUnits,
                update.totalUnits,
                update.isFinished
            );
        };
    }
    int exitCode=EXIT_CODE_USAGE;
    try {
        if (chosenSubcommand=="diagnose") {
            DiagnoseCommand command(terminalIsAttached);
            exitCode=command.execute(options);
        } else if (chosenSubcommand=="compact") {
            CompactCommand command(terminalIsAttached);
            exitCode=command.execute(options);
        } else if (chosenSubcommand=="vacuum") {
            VacuumCommand command(terminalIsAttached);
            exitCode=command.execute(options);
        } else if (chosenSubcommand=="recover") {
            RecoverCommand command(terminalIsAttached);
            exitCode=command.execute(options);
        } else if (chosenSubcommand=="service") {
            ServiceCommand command(terminalIsAttached);
            exitCode=command.execute(options);
        } else if (chosenSubcommand=="backups") {
            BackupsCommand command(terminalIsAttached);
            exitCode=command.execute(options);
        } else {
            // A subcommand with no name means the user asked for help, which CLI11
            // has already printed by the time control gets here.
            return EXIT_CODE_USAGE;
        }
    } catch (const std::exception& unexpectedFailure) {
        // A failure from the standard library itself, such as running out of
        // memory. The technical text goes on the end so it can be searched for,
        // but the sentence comes first.
        reporter.writeFailure(
            string("Something went wrong that this version of the tool does not have an "
                "explanation for. Nothing was changed.\n\nIf you can, please report this. The "
                "technical text follows."),
            unexpectedFailure.what()
        );
        exitCode=EXIT_CODE_PROBLEM;
    } catch (...) {
        // The tool's own exception types deliberately share no base class, so this
        // final catch cannot read their sentences. Every command catches the
        // failure types it raises, so reaching here means a gap somewhere; even
        // then the user gets words rather than a crash.
        reporter.writeFailure(
            string("Something went wrong that this version of the tool does not have an "
                "explanation for. Nothing was changed.\n\nIf you can, please report this."),
            string()
        );
        exitCode=EXIT_CODE_PROBLEM;
    }
    if (options.machineReadableOutput) {
        // The last object of a machine readable run says the run is over, so a
        // script that reads a pipe knows it has everything.
        reporter.writeExitEvent(exitCode);
    }
    return exitCode;
}
