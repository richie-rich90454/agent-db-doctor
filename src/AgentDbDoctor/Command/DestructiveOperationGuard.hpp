// The DestructiveOperationGuard class, which stands between the user and anything
// that cannot be undone.
//
// The specification's humanist rule is that the first destructive operation on any
// database produces a backup, that the tool prints where it went, and that nothing
// is deleted without a confirmation. Those three rules are implemented once here
// and used by the compact, vacuum, and recover commands, rather than being written
// three times and drifting apart.
#ifndef AGENT_DB_DOCTOR_COMMAND_DESTRUCTIVEOPERATIONGUARD_HPP
#define AGENT_DB_DOCTOR_COMMAND_DESTRUCTIVEOPERATIONGUARD_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the CommandOptions value type.
#include "AgentDbDoctor/Command/CommandOptions.hpp"
// Provides the ConsoleReport class, which prints every user facing line.
#include "AgentDbDoctor/Presentation/ConsoleReport.hpp"
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}
// The console report this guard prints through lives in the presentation
// subsystem, so that namespace is brought into scope here. The directive sits
// after the declaration above, because that is where the namespace exists.
using namespace AgentDbDoctor::Presentation;
using namespace std;
namespace AgentDbDoctor::Command {
// The BackupRecord value type says what copy was made and where it went.
struct BackupRecord final {
    // True when a copy was made during this run.
    bool wasCreated=false;
    // The full path of the copy.
    string backupFilePath;
    // The size of the copy in bytes.
    long long backupSizeBytes=0;
    // The total bytes the copy occupies, main file and side files together. This
    // is what deleting the copy hands back, and therefore what the net space
    // arithmetic needs.
    long long copiedBytes=0;
    // A sentence saying what the copy is for.
    string explanation;
};
// The DestructiveOperationGuard class makes a safety copy and asks before acting.
//
// The class is `final` because the specification forbids inheritance. It holds the
// options and the reporter as fields and calls methods on them, which is the
// composition the specification requires.
class DestructiveOperationGuard final {
public:
    // Builds a guard for one run.
    //
    // `options` says whether the user already answered yes, and `reporter` is where
    // every message goes. Both are held by reference so the guard never copies them.
    DestructiveOperationGuard(const CommandOptions& options, const ConsoleReport& reporter);
    // Makes a copy of the database beside the original.
    //
    // The copy is written next to the original with a timestamp in its name, so two
    // runs on the same day do not overwrite each other's safety net. The method
    // returns the record describing what was made.
    //
    // A copy of the side files is made too when they exist, because a copy of the
    // main file without its write ahead log can be missing committed changes.
    BackupRecord createBackup(const string& databaseFilePath);
    // Asks the user whether to go ahead, unless the options already say yes.
    //
    // Returns true when the operation may continue. The question is phrased in plain
    // language and states plainly that the original file is untouched so far and
    // that a copy exists.
    bool confirmWithUser(const string& operationDescription, const BackupRecord& backup);
    // True when the options say the user already answered yes.
    bool isAlreadyConfirmed() const;
    // Removes the safety copy after a run that finished and passed its checks.
    //
    // Returns the number of bytes handed back, which is zero when the copy was
    // kept and also zero when it could not be removed. A copy is kept when the
    // user asked with `--keep-backup` or named a destination with `--backup-to`,
    // and this method says so in a line rather than removing it. A removal that
    // fails leaves the copy in place and names the path, because a file the tool
    // cannot delete is a file the user can.
    long long removeBackupAfterSuccess(const BackupRecord& backup);
    // Puts the safety copy back over the database, side files included.
    //
    // This is the promise behind every destructive run: when the check that
    // follows the change fails, the database is returned to the state it was
    // found in. The current side files are removed first, because a write ahead
    // log from the changed database paired with the restored main file is exactly
    // the stale pairing that corrupts a database. Returns true when the copy is
    // in place, and false when it could not be, in which case the copy is still
    // untouched at its own path.
    bool restoreBackup(const BackupRecord& backup, const string& databaseFilePath);
    // True when the safety copy must be kept: the user asked with `--keep-backup`
    // or named a destination with `--backup-to`.
    bool shouldKeepBackup() const;
    // The sentence used to begin every confirmation question, exposed so the
    // command classes can build the part that names the specific change.
    string confirmationPreamble(const string& operationDescription) const;
private:
    // Builds the path a copy should be written to. The timestamp keeps repeated
    // runs from overwriting one another.
    string buildBackupFilePath(const string& databaseFilePath) const;
    const CommandOptions& options;
    const ConsoleReport& reporter;
};
}
#endif
