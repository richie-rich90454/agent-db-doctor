// The ServiceConfiguration class, which holds the settings the system service uses.
//
// The specification permits no JSON library, and the service configuration is a
// small object, so this file contains a small hand written reader and writer for
// the subset of JSON the configuration actually uses: an object of numbers,
// strings, booleans, and arrays of strings. A dependency that can parse any JSON
// would be several thousand lines to read six fields.
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
    // The full path of the log file.
    string logFilePath;
    // The full path of the file this configuration was read from.
    string configurationFilePath;
    // A sentence saying where the configuration and the log live, shown by the
    // status command.
    string describeLocations() const;
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
