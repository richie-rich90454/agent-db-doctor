// The FileLockDetector class, which answers one question: is another program
// holding this database open right now?
//
// The answer matters before every destructive operation. SQLite's own locking is
// advisory, which means two programs can both open the same file and only one of
// them wins at write time. Asking the operating system directly gives a clearer
// answer than waiting for a lock timeout, and lets the tool explain what is
// happening in plain language instead of returning an error code.
#ifndef AGENT_DB_DOCTOR_FILESYSTEM_FILELOCKDETECTOR_HPP
#define AGENT_DB_DOCTOR_FILESYSTEM_FILELOCKDETECTOR_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::FileSystem {
// The FileLockStatus value type describes whether a path is in use.
struct FileLockStatus final {
    // True when at least one other program appears to be holding the file.
    bool isLocked=false;
    // The platform error number that proved the file is in use. Zero when the
    // file is free, or when the platform would not say.
    int errorCode=0;
    // A sentence naming the likely cause, for example "the editor is still open".
    string explanation;
    // The side files that exist next to the database. A write ahead log or a
    // shared memory file means a process opened the database recently, even if it
    // has since closed it. The names are the full paths.
    vector<string> companionFilePaths;
    // True when a write ahead log exists. A non empty log means there may be
    // committed data that is not yet in the main database file, so a copy taken
    // without the log is incomplete.
    bool hasWriteAheadLog=false;
    // True when a rollback journal exists. A non empty journal means a write was
    // interrupted, and SQLite must replay it before the database is consistent.
    bool hasRollbackJournal=false;
};
// The FileLockDetector class probes a database path and its neighbours.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class FileLockDetector final {
public:
    FileLockDetector()=delete;
    // Checks whether `databaseFilePath` is currently open by another program.
    //
    // On Windows this is one call that requests exclusive access. On Linux and
    // macOS it is one open call followed by one `fcntl` request for a write lock
    // over the whole file, which conflicts with the byte range locks SQLite
    // itself takes. Either way the answer costs two system calls and reads no
    // bytes from the database.
    static FileLockStatus checkLockStatus(const string& databaseFilePath);
    // Checks the side files that belong with a database, and records what was
    // found in `status`. The write ahead log and the rollback journal are named
    // by appending a suffix to the database path, which is how SQLite names them.
    static void inspectCompanionFiles(const string& databaseFilePath, FileLockStatus& status);
    // A sentence telling the user what to close, naming the file that is in the
    // way. The list of known agent process names is passed in so that this class
    // does not need to know what an AI coding agent is called.
    static string describeWhoMightBeUsingIt(const vector<string>& likelyProcessNames);
private:
};
}
#endif
