// The child process runner: how the window runs the command line program.
//
// The window owns no diagnostics of its own. Every action it offers is a command
// line invocation of `agent-db-doctor`, started as a child process with its output
// piped back. This class is the whole of that: it finds the program, starts it,
// reads its merged output on a reader thread, splits that output into lines,
// parses the lines that are JSON objects, and hands them to the interface through
// a queue.
//
// Nothing here draws anything and nothing here opens a database, which is what
// keeps the graphical interface and the command line from ever disagreeing about
// a number: there is one implementation of every number, and it lives in the
// command line program.
#ifndef AGENT_DB_DOCTOR_GUI_CLIRUNNER_HPP
#define AGENT_DB_DOCTOR_GUI_CLIRUNNER_HPP
// Provides `atomic`, for the flags the interface thread reads without locking.
#include <atomic>
// Provides `mutex`, guarding the line queue and the process handle.
#include <mutex>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `thread`, the reader that drains the child's output.
#include <thread>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the JsonValue the machine readable lines are parsed into.
#include "Json.hpp"
// Says that the SDL process type exists without including any of SDL, so that
// this header can be included by files that never touch the window. The process
// handle itself appears below only as a pointer, which needs no definition.
struct SDL_Process;
using namespace std;
namespace AgentDbDoctor::Gui {
// One line the child produced.
struct CliLine final {
    // The line as it arrived, without its line ending.
    string text;
    // True when the line began with a brace and parsed as one JSON object.
    bool isJson=false;
    // The parsed object. Meaningful only when `isJson` is true.
    JsonValue json;
};
// Runs one command line program at a time, with its output piped back.
class CliRunner final {
public:
    CliRunner()=default;
    // Waits for the reader thread and destroys any process still held.
    ~CliRunner();
    CliRunner(const CliRunner&)=delete;
    CliRunner& operator=(const CliRunner&)=delete;
    // The program to run: the sibling binary beside this window first, then the
    // path named by the environment variable AGENT_DB_DOCTOR_CLI, then the empty
    // string, which means nothing was found and the operating system is left to
    // find the program by name.
    static string locateCliExecutable();
    // Where the program was looked for and what will be run, in one sentence. The
    // settings tab shows it, and so does a run that cannot be started.
    static string locateCliExecutableDetails();
    // Starts the program with these arguments, after waiting for any previous run.
    // Returns false when it could not be started, in which case the reason is
    // already queued as a line for the log pane.
    bool start(const vector<string>& arguments);
    // True from the moment the program starts until the end of its output.
    bool isRunning() const;
    // Stops the program. The reader thread still runs to its end, so the lines the
    // child produced before the stop are not lost.
    void cancel();
    // Waits for the reader thread and then for the process, and destroys the
    // process handle. The exit code is available afterwards.
    void join();
    // Everything the child wrote since the last call, oldest first.
    vector<CliLine> takeLines();
    // The exit code of the last finished run, as the operating system reports it.
    // Meaningful after `join`. It is zero for every run started in the background,
    // which is how every run is started, so the interface reads the code from the
    // `exit` object the command line writes instead and uses this only as the
    // fallback for a run that wrote nothing at all.
    int exitCode() const;
private:
    // The reader thread's body: reads chunks, splits them into lines, queues them,
    // and marks the run over when the output ends.
    void readOutputLoop();
    // Queues one line, parsing it when it begins with a brace.
    void queueLine(const string& lineText);
    // The child process. Created by the interface thread and then touched only
    // through the lock below, so a stop from the interface cannot race a read.
    SDL_Process* process=nullptr;
    // Guards the process handle.
    mutable mutex processMutex;
    // The reader thread, joinable only while a run is in flight or unjoined.
    thread readerThread;
    // Guards the queue below.
    mutable mutex lineMutex;
    // The lines waiting to be taken, oldest first.
    vector<CliLine> queuedLines;
    // True while the child is running. Read by the interface without locking.
    atomic<bool> runningFlag{false};
    // The exit code of the last finished run. Read after `join`.
    atomic<int> exitCodeValue{0};
};
}
#endif
