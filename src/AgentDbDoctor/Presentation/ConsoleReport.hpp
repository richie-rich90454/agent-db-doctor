// The ConsoleReport class, which turns results into the words a person reads.
//
// Every line the tool prints on the success path goes through this class. That is
// deliberate: the specification requires a single place where user facing text is
// produced, so that the tone stays consistent and so that the plain text and the
// terminal output can never disagree about what happened.
#ifndef AGENT_DB_DOCTOR_PRESENTATION_CONSOLEPORT_HPP
#define AGENT_DB_DOCTOR_PRESENTATION_CONSOLEPORT_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DiagnosisReport value type.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
// Provides the SafetyCheckResult value type.
#include "AgentDbDoctor/Doctor/SafetyCheckResult.hpp"
// Provides the RecoveryRecommendation value type.
#include "AgentDbDoctor/Recovery/RecoveryRecommendation.hpp"
// Provides the SpaceRequirement and DiskSpace value types.
#include "AgentDbDoctor/FileSystem/DiskSpaceChecker.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::Recovery;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Presentation {
// The ServiceStatusSnapshot value type says what the system service is doing, so
// that the report can print it without knowing anything about services.
struct ServiceStatusSnapshot final {
    // True when the service is registered with the operating system.
    bool isInstalled=false;
    // True when the service is registered and currently running.
    bool isRunning=false;
    // A sentence saying where the configuration and log files are.
    string installationDescription;
    // The most recent line the service wrote to its log, or the empty string when
    // there is none. Showing it means a user does not have to go hunting for it.
    string mostRecentLogLine;
    // A sentence describing the current state, for example "running, watching 2
    // databases".
    string stateDescription;
};
// The MachineField value type is one named value inside a machine readable result.
//
// A field knows whether it is a number, a boolean, or text, so a script receives
// `"rowsRemoved":1200` rather than `"rowsRemoved":"1200"` and never has to guess.
struct MachineField final {
    // Which kind of value this field holds.
    enum class Kind {
        Text,
        Number,
        Boolean
    };
    // The field name, for example `rowsRemoved`.
    string name;
    // The value when the kind is text.
    string textValue;
    // The value when the kind is number.
    long long numberValue=0;
    // The value when the kind is boolean.
    bool booleanValue=false;
    // Which of the three value slots above is in use.
    Kind kind=Kind::Text;
    // Builds a text field.
    static MachineField text(const string& fieldName, const string& value);
    // Builds a number field.
    static MachineField number(const string& fieldName, long long value);
    // Builds a boolean field.
    static MachineField boolean(const string& fieldName, bool value);
};
// The ConsoleReport class prints every message the tool produces on a normal run.
//
// The class is `final` because the specification forbids inheritance. It holds the
// terminal flag, because that decision is made once at start up rather than at each
// print, and it must be the same for every line.
class ConsoleReport final {
public:
    // Builds a reporter. `isTerminalAttached` should be the result of asking the
    // platform whether standard output is a terminal. When it is false, no escape
    // sequence is ever written, so the output is safe to pipe into a file.
    explicit ConsoleReport(bool isTerminalAttached);
    // Turns on machine readable output: every report becomes one JSON object per
    // line instead of prose. Used by the `--machine-readable` flag, and the same
    // objects are what the graphical interface reads.
    void enableMachineReadableOutput();
    // Turns machine readable output on for every reporter this process creates from
    // now on. The command line is one process with one output mode, and every
    // command builds its own reporter, so the mode is process wide rather than per
    // instance.
    static void setMachineReadableForProcess(bool enabled);
    // True when machine readable output is on.
    bool isMachineReadable() const;
    // True when standard output is a terminal. Used by the commands that draw a live
    // view, which must not clear the screen when the output is redirected.
    bool isTerminalAttached() const;
    // Writes one result object for a command that has finished, carrying the typed
    // numbers a script cares about.
    void writeMachineResult(const string& commandName, const vector<MachineField>& fields) const;
    // Writes one progress object. The command line installs a sink that calls this
    // when machine readable output is on, so a script can follow a long run.
    void writeProgressEvent(
        const string& taskDescription,
        long long completedUnits,
        long long totalUnits,
        bool isFinished
    ) const;
    // Writes one complete JSON object the caller built itself. The object is written
    // as it stands, without being wrapped in a line event, which is what a command
    // that builds its own objects needs.
    void writeMachineObject(const string& jsonObject) const;
    // Writes the object every machine readable run ends with, so a script knows the
    // run is over even when it read nothing else.
    void writeExitEvent(int exitCode) const;
    // Writes how many bytes and read calls the run made, for `diagnose --trace-reads`.
    void writeReadTraceEvent(long long readBytes, long long readCalls) const;
    // Escapes the characters JSON requires to be escaped. Public because the few
    // places that build a JSON object of their own, rather than printing one, must
    // use exactly the same rule as this class does.
    static string escapeForJson(const string& text);
    // Writes the two line banner that starts every command's output.
    void writeBanner(const string& commandName, const string& oneLinePurpose) const;
    // Writes a heading, underlined when the output is a terminal.
    void writeHeading(const string& headingText) const;
    // Writes one ordinary line.
    void writeLine(const string& lineText) const;
    // Writes one line indented under the previous one, used for explanations.
    void writeDetailLine(const string& lineText) const;
    // Writes a complete diagnosis for one database.
    void writeDiagnosisReport(const DiagnosisReport& report) const;
    // Writes the list of pre-flight checks with a pass or fail beside each.
    void writeSafetyCheckResults(const vector<SafetyCheckResult>& results) const;
    // Writes a recovery plan, with a number beside each step and the command to run
    // shown in a form the user can copy.
    void writeRecoveryPlan(
        const string& agentName,
        const string& databaseFilePath,
        const vector<RecoveryRecommendation>& plan
    ) const;
    // Writes the answer to a question the tool asked, for example a confirmation.
    void writeQuestion(const string& questionText) const;
    // Writes a warning in a form that cannot be missed, with a blank line above and
    // below it.
    void writeWarning(const string& warningText) const;
    // Writes the list of things the user could do next. The specification requires
    // every command to finish with this.
    void writeSuggestedNextSteps(const vector<string>& suggestedSteps) const;
    // Writes the service status.
    void writeServiceStatus(const ServiceStatusSnapshot& status) const;
    // Writes a failure in the form the specification requires: the translated
    // sentence first, and the raw code beside it only as a search term.
    void writeFailure(const string& translatedExplanation, const string& rawCodeForSearching) const;
    // Writes what a safety copy was made and where it was put. The specification
    // requires this before any destructive operation, so the user always knows
    // where their data went.
    void writeBackupLocation(const string& backupFilePath, long long backupSizeBytes) const;
    // A line of dashes the width of the widest heading, used to separate sections.
    static string separatorLine();
private:
    bool terminalIsAttached=false;
    // True when every report is written as one JSON object per line.
    bool machineReadable=false;
    // Writes one complete JSON object followed by a newline, and flushes so a
    // script reading the pipe sees each object as it happens.
    void writeJsonLine(const string& jsonText) const;
};
}
#endif
