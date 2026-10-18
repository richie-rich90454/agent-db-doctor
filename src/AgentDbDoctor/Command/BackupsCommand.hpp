// The BackupsCommand class, which lists, verifies, restores, and deletes the
// safety copies the tool makes before a destructive run.
//
// A copy nobody can inspect, test, or clear away is not a safety net. This command
// is the other half of the promise: it proves a copy is restorable without
// touching the database, can put one back, and can hand back the space a set of
// old copies occupies.
#ifndef AGENT_DB_DOCTOR_COMMAND_BACKUPSCOMMAND_HPP
#define AGENT_DB_DOCTOR_COMMAND_BACKUPSCOMMAND_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
// The BackupFile value type describes one safety copy that was found.
struct BackupFile final {
    // The full path of the copy.
    string filePath;
    // The bytes the copy occupies, main file and side files together. This is what
    // deleting it hands back.
    long long sizeBytes=0;
    // When the copy was written, as milliseconds since the Unix epoch.
    long long modificationEpochMilliseconds=0;
};
// The BackupsCommand class manages the safety copies of a database.
//
// The class is `final` because the specification forbids inheritance. It holds a
// ConsoleReport as a field and calls methods on it, which is the composition the
// specification requires in place of deriving from anything.
class BackupsCommand final {
public:
    // Builds the command with its own reporter.
    explicit BackupsCommand(bool isTerminalAttached);
    // Runs the action named in the options and returns the process exit code.
    int execute(const CommandOptions& options);
    // The one line description shown in the command list.
    string describe() const;
    // The name shown in the command list.
    string commandName() const;
    // Every safety copy of a database, newest first. The naming rule comes from
    // the guard, so this method knows nothing about how a copy is named.
    static vector<BackupFile> findBackupsBeside(const string& databaseFilePath);
private:
    int listBackups(const CommandOptions& options);
    int verifyBackups(const CommandOptions& options);
    int restoreChosenBackup(const CommandOptions& options);
    int deleteBackups(const CommandOptions& options);
    // The confirmation used before a restore or a deletion. The same exact word the
    // rest of the tool demands, for the same reason: anything else is a stop.
    bool confirmWithUser(const CommandOptions& options, const string& questionText) const;
    // The databases the action works on: the one named with --database, or every
    // discovered database when listing. Empty when the action needs a database and
    // none was named.
    vector<string> targetDatabasePaths(const CommandOptions& options, bool discoveryIsAllowed) const;
    // The copy to act on: the one named with --file, or the newest beside the
    // database. Returns false when there is none.
    bool chooseBackup(const CommandOptions& options, const string& databaseFilePath, BackupFile& chosenOut) const;
    // Every line the command prints goes through the reporter, which is the only
    // class in the tool allowed to write to the user's screen.
    ConsoleReport reporter;
};
}
#endif
