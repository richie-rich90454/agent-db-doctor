// The command line program.
//
// Every line of user facing text the program can print comes from one of the
// command classes, so this file only has to work out which command was asked for,
// turn the command line into a CommandOptions value, and turn the return value into
// an exit code. That is the whole of its job, and keeping it that small is what
// makes the tool easy to extend without making the entry point unreadable.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the CLI11 header only command line parser.
#include <CLI/CLI.hpp>
// Provides the CommandOptions value type, which every command reads.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the five command classes.
#include "AgentDbDoctor/Command/DiagnoseCommand.hpp"
#include "AgentDbDoctor/Command/PruneCommand.hpp"
#include "AgentDbDoctor/Command/CompactCommand.hpp"
#include "AgentDbDoctor/Command/RecoverCommand.hpp"
#include "AgentDbDoctor/Command/ServiceCommand.hpp"
// Provides ConsoleReport, which prints every line the program writes.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// Provides PlatformMetadata, for the terminal test.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the exception types, so a failure is reported rather than thrown out of
// the program as an unhandled error.
#include "AgentDbDoctor/Common/Exceptions.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Command;
using namespace AgentDbDoctor::Presentation;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Common;
// The exit code used when the program stops because of a problem rather than
// because of a bad answer. It is one, which is what a shell script checks for.
static constexpr int EXIT_CODE_PROBLEM=1;
// The exit code used when the command line itself was wrong. It is two, which is
// the convention every other program on these platforms follows.
static constexpr int EXIT_CODE_USAGE=2;
// Adds the options every command shares. CLI11 groups them so that `--help` shows
// them once rather than five times.
void addSharedOptions(CLI::App& application, CommandOptions& options) {
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
    application.add_flag(
        "-q,--quiet",
        options.quiet,
        "Print only the lines that matter. Useful in a script."
    );
    application.add_flag(
        "-v,--verbose",
        options.verbose,
        "Write extra detail about what the tool is doing."
    );
    application.add_option(
        "-k,--keep",
        options.sessionsToKeep,
        "How many of the most recent sessions to keep when pruning. Zero, the default, keeps the newest half."
    );
    application.add_option(
        "--max-fraction",
        options.maximumFractionToRemove,
        "Refuse to prune more than this share of the event table. The default is 0.5, or half."
    );
    application.add_option(
        "--batch-size",
        options.deletionBatchSize,
        "How many rows to remove in one transaction when pruning. The default is 10000."
    );
    application.add_option(
        "--backup-to",
        options.backupFilePath,
        "Where to write the safety copy. Leave this out and the tool puts it beside the database."
    );
    application.add_option(
        "--warning-threshold",
        options.warningThresholdBytes,
        "The size in bytes a database must reach before the service mentions it. The default is 53687091200, which is 50 GB."
    );
    application.add_option(
        "--interval",
        options.watchdogIntervalSeconds,
        "How many seconds the service waits between checks. The default is 3600, which is an hour."
    );
    application.add_flag(
        "--rebuild",
        options.rebuildWorkingCopy,
        "When recovering, build a working copy holding every row that can still be read. The original is never modified."
    );
    application.add_flag(
        "--check",
        options.runIntegrityCheck,
        "Run the full page by page integrity check. Slower, and it reads the whole file."
    );
    application.add_flag(
        "--machine-readable",
        options.machineReadableOutput,
        "Print sizes and counts as plain numbers rather than as sentences."
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
        "Diagnose, prune, compact, recover, and monitor the databases that AI coding agents use to "
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
    addSharedOptions(*diagnoseSubcommand, options);
    CLI::App* pruneSubcommand=application.add_subcommand(
        "prune",
        "Remove older copies of changed messages, after copying your database and checking nothing was lost."
    );
    addSharedOptions(*pruneSubcommand, options);
    CLI::App* compactSubcommand=application.add_subcommand(
        "compact",
        "Rewrite the database so it gives unused space back to the drive."
    );
    addSharedOptions(*compactSubcommand, options);
    CLI::App* recoverSubcommand=application.add_subcommand(
        "recover",
        "Work out what is wrong with a database that will not open, and say what can be fixed."
    );
    addSharedOptions(*recoverSubcommand, options);
    CLI::App* serviceSubcommand=application.add_subcommand(
        "service",
        "Install and control the background service that watches database sizes."
    );
    addSharedOptions(*serviceSubcommand, options);
    serviceSubcommand->add_option(
        "action",
        options.serviceAction,
        "One of: install, uninstall, start, stop, status, run."
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
        // both are handled here rather than treated as a mistake.
        return application.exit(parseFailure);
    }
    // The service is the one command that must run with no terminal, so a missing
    // `--yes` is a usage error there rather than a question nobody can answer.
    const string chosenSubcommand=application.get_subcommands().empty()
        ? string()
        : application.get_subcommands().front()->get_name();
    try {
        if (chosenSubcommand=="diagnose") {
            DiagnoseCommand command(terminalIsAttached);
            return command.execute(options);
        }
        if (chosenSubcommand=="prune") {
            PruneCommand command(terminalIsAttached);
            return command.execute(options);
        }
        if (chosenSubcommand=="compact") {
            CompactCommand command(terminalIsAttached);
            return command.execute(options);
        }
        if (chosenSubcommand=="recover") {
            RecoverCommand command(terminalIsAttached);
            return command.execute(options);
        }
        if (chosenSubcommand=="service") {
            ServiceCommand command(terminalIsAttached);
            return command.execute(options);
        }
        // A subcommand with no name means the user asked for help, which CLI11 has
        // already printed by the time control gets here.
        return EXIT_CODE_USAGE;
    } catch (const AgentDbDoctorException& toolFailure) {
        // Every exception this tool raises carries a complete sentence, which is
        // what the specification requires. It is printed as a failure rather than
        // allowed to escape, so the user sees a sentence and not a stack trace.
        reporter.writeFailure(toolFailure.message(), string());
        return EXIT_CODE_PROBLEM;
    } catch (const std::exception& unexpectedFailure) {
        // A failure this tool did not anticipate is still reported in plain
        // language. The technical text goes on the end so it can be searched for,
        // but the sentence comes first.
        reporter.writeFailure(
            string("Something went wrong that this version of the tool does not have an "
                "explanation for. Nothing was changed.\n\nIf you can, please report this. The "
                "technical text follows."),
            unexpectedFailure.what()
        );
        return EXIT_CODE_PROBLEM;
    }
}
