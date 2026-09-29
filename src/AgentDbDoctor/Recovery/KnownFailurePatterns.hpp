// The KnownFailurePatterns class, which recognises the specific ways an AI coding
// agent database goes wrong.
//
// The specification tells a real story: a developer compressed a live database
// with an algorithm that could not compress its pages, the drive ran out of room,
// the machine was rebooted mid write, and the file was left in a state that no
// tool could read. The point of this class is that the tool recognises that exact
// situation by name instead of reporting a raw error number and leaving the user
// to work out what it means.
#ifndef AGENT_DB_DOCTOR_RECOVERY_KNOWNFAILUREPATTERNS_HPP
#define AGENT_DB_DOCTOR_RECOVERY_KNOWNFAILUREPATTERNS_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>

// Provides `vector`, a growable array of values.
#include <vector>
// Provides the FileMetadata and FileLockStatus value types.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
#include "AgentDbDoctor/FileSystem/FileLockDetector.hpp"
// Provides the SeverityLevel enumeration.
#include "AgentDbDoctor/Recovery/RecoveryRecommendation.hpp"
// The value types this header's interface uses live in another subsystem, so
// that namespace is brought into scope here. The directive sits after the
// includes because that is where the namespace has been declared.
using namespace AgentDbDoctor::FileSystem;

// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Recovery {
// The FailureMatch value type says which known pattern a database matches.
//
// A match may be empty, which means nothing recognisable was found and the tool
// should describe what it sees rather than guess at a cause.
struct FailureMatch final {
    // The internal name of the pattern, such as `compressed-while-open`. It is
    // never shown to the user; the plain explanation is.
    string patternName;
    // True when the pattern was recognised.
    bool matched=false;
    // How urgent the situation is.
    SeverityLevel severity=SeverityLevel::INFORMATION;
    // A paragraph in plain English saying what happened and what it means.
    string plainExplanation;
    // True when this tool can make the situation better on its own.
    bool recoverableByThisTool=false;
    // True when the file's own bytes are gone, so nothing can bring them back. The
    // specification is explicit that the tool must say this out loud rather than
    // let a user keep trying.
    bool dataIsUnrecoverable=false;
};
// The KnownFailurePatterns class recognises the situations the specification names,
// and a few more that SQLite reports often enough to be worth naming.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so every method is `static`.
class KnownFailurePatterns final {
public:
    KnownFailurePatterns()=delete;
    // Classifies a database from the facts a diagnostic pass already gathered.
    //
    // `sqliteResultCode` is the code from a failed open, or zero when the open
    // succeeded. `fileMetadata` is what the file system reported. `lockStatus` is
    // what the lock probe found, and `availableSpace` is the free space on the
    // volume. All four are already in hand after a diagnostic pass, so this costs
    // no system calls at all.
    static FailureMatch classify(
        int sqliteResultCode,
        const FileMetadata& fileMetadata,
        const FileLockStatus& lockStatus,
        const DiskSpace& availableSpace
    );
    // The names of every pattern this class can recognise, for the diagnose
    // command's list and for the tests.
    static vector<string> allPatternNames();
    // True when the file's allocated size is so far below its logical size that
    // the file system is storing it more efficiently than SQLite's pages allow.
    // This is the signature of a compressed or sparse database, and it is the
    // situation the specification warns about at length.
    static bool looksCompressedOrSparse(const FileMetadata& fileMetadata);
    // True when the file's allocated size is larger than the size SQLite believes
    // the database should be, which is the signature of the interrupted
    // compression described in the specification: the file system kept clusters
    // that the logical file no longer describes.
    static bool hasAllocatedSizeMismatch(const FileMetadata& fileMetadata, long long declaredFileSizeBytes);
private:
    // Builds an empty match, meaning nothing recognisable was found.
    static FailureMatch noMatch();
    // Builds a match for one of the named patterns.
    static FailureMatch buildMatch(
        string patternName,
        SeverityLevel severity,
        string plainExplanation,
        bool isRecoverable,
        bool isUnrecoverable
    );
};
}
#endif
