// The ServiceConfiguration class, which holds the settings the system service uses.
//
// The specification permits no JSON library, and the service configuration is a
// small object, so this file contains a small hand written reader and writer for
// the subset of JSON the configuration actually uses: an object of numbers,
// strings, booleans, arrays of strings, and the array of policy objects that
// carries the per database settings. A dependency that can parse any JSON would
// be several thousand lines to read a handful of fields.
#ifndef AGENT_DB_DOCTOR_SERVICE_SERVICECONFIGURATION_HPP
#define AGENT_DB_DOCTOR_SERVICE_SERVICECONFIGURATION_HPP
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
using namespace std;
namespace AgentDbDoctor::Service {
// The DatabasePolicy value type holds the settings that apply to one database.
//
// The configuration wide settings are the right answer for a machine nobody has
// thought about, and they are the wrong answer for a machine where the work lives
// in three databases with very different value: one is a scratch file that may be
// rewritten whenever the service likes, one holds a year of history that must not
// be touched without being asked. A policy is how the file says which is which.
//
// Every field has a default that is the safe answer, so a policy written with one
// field in it behaves the way the field name suggests and says nothing about the
// rest.
struct DatabasePolicy final {
    // The exact full path this policy belongs to. It is compared with the paths
    // the service measures, so it must be spelled the way the service received
    // them: the paths in `watchedDatabases`, or the paths the agent adapters
    // report when that list is empty. An empty path is refused, because a policy
    // that applies to no file is a mistake rather than a policy.
    string databaseFilePath;
    // The size at which this database is treated as urgent, overriding the
    // configuration's `criticalThresholdBytes`. Zero means the policy does not
    // change the threshold.
    long long criticalThresholdBytes=0;
    // True when the service may compact and vacuum this database on its own. It
    // overrides the configuration's `allowAutomaticCompaction` in both
    // directions: true permits what the file refuses, false refuses what the file
    // permits, so a policy can pull one database out of a machine wide decision.
    bool allowAutomaticCompaction=false;
    // True when a threshold crossing on this database should also raise a desktop
    // notification, not only a log line. This is the explicit request the
    // notification rule requires: a service that starts showing notifications
    // nobody asked for is the surprise this tool avoids elsewhere.
    bool notifyOnThreshold=false;
};
// The ServiceConfiguration value type holds every setting the service reads.
//
// It is a plain value type with public fields, written to and read from a JSON file
// so that a user can change a threshold without rebuilding the tool.
struct ServiceConfiguration final {
    // The full paths the service watches. The empty list means every supported
    // coding agent, which is what a fresh install uses.
    vector<string> watchedDatabasePaths;
    // The size in bytes a database must reach before the service writes a line to
    // its log. Fifty gigabytes is the default, because that is where a disk starts
    // to become a real problem on a typical machine.
    long long warningThresholdBytes=53687091200LL;
    // The size in bytes at which the service says the situation is urgent.
    long long criticalThresholdBytes=107374182400LL;
    // How many seconds the service waits between checks. The default is one hour,
    // which matches the specification's note that the service logs at most one line
    // an hour.
    int checkIntervalSeconds=3600;
    // True when the service should also delete redundant rows on its own. The
    // default is false, because a tool that changes a database while nobody is
    // watching is a tool nobody trusts.
    bool allowAutomaticCompaction=false;
    // The per database settings, in the order they were written. An empty list,
    // which is the default, means every database is governed by the
    // configuration wide settings above.
    vector<DatabasePolicy> databasePolicies;
    // The full path of the log file.
    string logFilePath;
    // The full path of the file this configuration was read from.
    string configurationFilePath;
    // A sentence saying where the configuration and the log live, shown by the
    // status command.
    string describeLocations() const;
    // The policy for one database, or nothing when no policy names it. The match
    // is on the exact path, because the paths in the file are the paths the
    // service measures, and a policy that matched a different spelling would be
    // a promise the service silently ignored.
    const DatabasePolicy* policyFor(const string& databaseFilePath) const;
    // The configuration a fresh install uses, with the paths filled in for the
    // platform the tool is running on.
    static ServiceConfiguration createDefault();
    // True when the settings are self consistent. A configuration with a critical
    // threshold below the warning threshold would never fire the warning, so that
    // is treated as a mistake rather than silently accepted.
    bool isValid(string& explanationOut) const;
private:
};
// The ConfigurationFile class reads and writes the configuration file.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so every method is `static`.
class ConfigurationFile final {
public:
    ConfigurationFile()=delete;
    // Reads a configuration from the file at `filePath`.
    //
    // Returns `nullopt` when the file does not exist, and throws
    // `ConfigurationException` when it exists but cannot be understood. The two
    // cases are different: a missing file is a normal first run, and a malformed
    // one is a mistake the user needs told about.
    static optional<ServiceConfiguration> read(const string& filePath);
    // Reads a configuration from text that is already in memory, with the given
    // path recorded as where it came from. This is the same parser as `read`, so
    // a configuration that is accepted here would be accepted from a file and one
    // refused here is refused with the same sentence. It exists so that the
    // parser can be driven directly, without a file, by a fuzzing harness.
    static optional<ServiceConfiguration> fromText(
        const string& text,
        const string& configurationFilePath
    );
    // Writes a configuration to the file at `filePath`, creating the parent
    // directory when it does not exist. The file is written with a comment at the
    // top naming the tool, because a user who opens it should know what it is.
    static void write(const ServiceConfiguration& configuration, const string& filePath);
    // The path the configuration lives at on this platform: under the program data
    // directory on Windows, `/etc` on Linux, and the application support directory
    // on macOS. The returned path is a directory, and the file inside it has a
    // fixed name.
    static string defaultDirectoryPath();
    // The full path of the configuration file, which is the fixed name inside the
    // directory above.
    static string defaultFilePath();
    // The full path of the log file the service writes to, which sits beside the
    // configuration in a `logs` folder.
    static string defaultLogFilePath();
    // Renders a configuration as the text that is written to the file. Exposed so
    // the tests can check the format without touching the disk.
    static string renderAsJson(const ServiceConfiguration& configuration);
private:
};
}
#endif
