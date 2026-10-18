// The ReadCountingVfs class, a wrapper around SQLite's own file layer that counts
// how many bytes the tool actually reads from the databases it looks at.
//
// The performance contract of this project is stated in bytes: a diagnostic pass on
// a hundred gigabyte database reads fewer than sixty four kilobytes. That number
// used to be a code review promise; with this wrapper installed it is a
// measurement, asserted by the benchmark suite and available to a user as
// `diagnose --trace-reads`.
//
// The wrapper is installed once per process as the default file system. It copies
// SQLite's own virtual file system, keeps every one of its functions, and gives out
// a file object that counts before delegating. SQLite is compiled with no threading
// support, so the counters are plain variables.
#ifndef AGENT_DB_DOCTOR_PERSISTENCE_READCOUNTINGVFS_HPP
#define AGENT_DB_DOCTOR_PERSISTENCE_READCOUNTINGVFS_HPP
using namespace std;
namespace AgentDbDoctor::Persistence {
// The ReadCountingVfs class counts the bytes every connection reads.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class ReadCountingVfs final {
public:
    ReadCountingVfs()=delete;
    // Installs the wrapper as the default file system. Calling it more than once
    // does nothing the second time, so every connection can simply ask for it.
    static void install();
    // Zeroes the counters. Counting itself needs no switch: the wrapper counts
    // always, and a caller reads the numbers between two resets.
    static void resetCounters();
    // The bytes read since the counters were reset.
    static long long countedBytes();
    // The number of read calls since the counters were reset. The call count is
    // asserted as well as the byte count, because one huge read and a thousand
    // small ones are different kinds of failure.
    static long long countedReadCalls();
    // True when the wrapper has been installed as the default file system.
    static bool isInstalled();
};
}
#endif
