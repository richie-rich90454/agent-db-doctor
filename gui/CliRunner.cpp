// The implementation of the child process runner.
//
// Three things decide the shape of this file.
//
// The child is started with no window of its own, because a console flashing up
// behind the graphical interface for every command would be worse than useless.
// SDL does that through its background flag, which passes CREATE_NO_WINDOW to the
// system on Windows.
//
// Standard error is merged into standard output, so the reader sees the child's
// lines in the order the child wrote them. Two streams would need two readers and
// would put a warning from the tool's own failure path in a different place from
// the sentence that explains it.
//
// The reading happens on its own thread. A long diagnosis takes seconds, and a
// window that stopped drawing for seconds would look like a window that crashed.
// The thread ends by itself when the child closes its output, which is how a run
// finishes; everything the interface reads is either an atomic or behind a lock.
// Provides `lock_guard`, which holds a lock for exactly one block.
#include <mutex>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides `SDL_GetBasePath`, which names the folder this window runs from.
#include <SDL3/SDL_filesystem.h>
// Provides `SDL_ReadIO`, `SDL_GetIOStatus`, and the status values.
#include <SDL3/SDL_iostream.h>
// Provides SDL_CreateProcessWithProperties and the create properties.
#include <SDL3/SDL_process.h>
// Provides SDL_CreateProperties and the property accessors.
#include <SDL3/SDL_properties.h>
// Provides `SDL_Delay`, which paces the reader while the child is quiet.
#include <SDL3/SDL_timer.h>
// Provides the runner class declaration.
#include "CliRunner.hpp"
using namespace std;
namespace AgentDbDoctor::Gui {
namespace {
// The name the build gives the command line program on each platform.
#if defined(_WIN32)
static constexpr const char* CLI_PROGRAM_FILE_NAME="agent-db-doctor.exe";
#else
static constexpr const char* CLI_PROGRAM_FILE_NAME="agent-db-doctor";
#endif
// The environment variable a user sets to point the window at a program that is
// not beside it, for example one that is still in a build tree.
static constexpr const char* CLI_LOCATION_ENVIRONMENT_VARIABLE="AGENT_DB_DOCTOR_CLI";
// How much of the child's output is read per call. Large enough that a chatty
// command needs few calls, small enough that the queue is filled promptly.
static constexpr size_t READ_CHUNK_BYTES=4096;
// How long the reader waits before looking for output again. The child writes
// nothing while it is thinking, and a window has no use for a line sooner than
// the next frame, which is about sixteen milliseconds away.
static constexpr long long READER_IDLE_MILLISECONDS=8;
// True when a plain file (not a folder) exists at the path.
bool fileExistsAt(const string& path) {
    if (path.empty()) {
        return false;
    }
    SDL_PathInfo pathInformation;
    if (!SDL_GetPathInfo(path.c_str(), &pathInformation)) {
        return false;
    }
    return pathInformation.type==SDL_PATHTYPE_FILE;
}
// What was found, and the sentence that says where it was looked for.
struct LocatedProgram final {
    // The program to run, or the empty string when nothing was found.
    string executablePath;
    // The sentence for the settings tab and for a failed start.
    string details;
};
// Looks for the command line program in the order the window promises: beside
// this window, then named by the environment variable. An empty result means the
// operating system will be asked to find the program by name when a command runs.
LocatedProgram locateProgram() {
    LocatedProgram locatedProgram;
    const char* reportedBasePath=SDL_GetBasePath();
    if (reportedBasePath!=nullptr && reportedBasePath[0]!='\0') {
        const string besideWindowPath=string(reportedBasePath) + CLI_PROGRAM_FILE_NAME;
        if (fileExistsAt(besideWindowPath)) {
            locatedProgram.executablePath=besideWindowPath;
            locatedProgram.details=string("The copy beside this window: ") + besideWindowPath;
            return locatedProgram;
        }
        locatedProgram.details=string("Nothing is at ") + besideWindowPath + ".";
    } else {
        locatedProgram.details=
            "This platform did not report the folder holding this window, so the copy of "
            "the program beside it could not be looked for.";
    }
    const char* configuredPath=SDL_getenv(CLI_LOCATION_ENVIRONMENT_VARIABLE);
    if (configuredPath!=nullptr && configuredPath[0]!='\0') {
        locatedProgram.executablePath=configuredPath;
        locatedProgram.details+=
            " The environment variable " + string(CLI_LOCATION_ENVIRONMENT_VARIABLE)
            + " names " + string(configuredPath) + ".";
        return locatedProgram;
    }
    locatedProgram.details+=
        string(" The environment variable ") + CLI_LOCATION_ENVIRONMENT_VARIABLE
        + " is not set, so " + CLI_PROGRAM_FILE_NAME
        + " is left to the operating system to find on its own search path.";
    return locatedProgram;
}
}
CliRunner::~CliRunner() {
    join();
}
string CliRunner::locateCliExecutable() {
    return locateProgram().executablePath;
}
string CliRunner::locateCliExecutableDetails() {
    return locateProgram().details;
}
bool CliRunner::start(const vector<string>& arguments) {
    join();
    const LocatedProgram locatedProgram=locateProgram();
    const string programToRun=locatedProgram.executablePath.empty()
        ? string(CLI_PROGRAM_FILE_NAME)
        : locatedProgram.executablePath;
    // The argument array SDL wants is the program followed by the arguments, with a
    // null at the end. The strings themselves are kept alive by `argumentTexts`
    // until the process has been created.
    vector<string> argumentTexts;
    argumentTexts.reserve(arguments.size() + 1);
    argumentTexts.push_back(programToRun);
    for (const string& argument : arguments) {
        argumentTexts.push_back(argument);
    }
    vector<const char*> argumentPointers;
    argumentPointers.reserve(argumentTexts.size() + 1);
    for (const string& argumentText : argumentTexts) {
        argumentPointers.push_back(argumentText.c_str());
    }
    argumentPointers.push_back(nullptr);
    SDL_PropertiesID creationProperties=SDL_CreateProperties();
    if (creationProperties==0) {
        {
            lock_guard<mutex> lineLock(lineMutex);
            CliLine failureLine;
            failureLine.text=
                string("The command line program could not be started: this system refused to "
                    "create the properties a new process needs. ") + locatedProgram.details;
            queuedLines.push_back(failureLine);
        }
        return false;
    }
    // SDL reads the argument array through a writable pointer because its C
    // signature has no way to say `const`. The array is never written to and it
    // outlives the call, so the cast changes nothing at run time.
    SDL_SetPointerProperty(
        creationProperties,
        SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
        const_cast<char**>(argumentPointers.data()));
    // Standard input is nothing, because the window answers every question the
    // command line would ask itself, before it starts the program.
    SDL_SetNumberProperty(
        creationProperties,
        SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
        SDL_PROCESS_STDIO_NULL);
    // Standard output and standard error are piped into this program, as one
    // stream, which is the stream the reader below drains.
    SDL_SetNumberProperty(
        creationProperties,
        SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
        SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(
        creationProperties,
        SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
        true);
    // The background flag is what keeps a console window from appearing behind the
    // graphical interface: on Windows SDL answers it with CREATE_NO_WINDOW. The
    // output pipe set above is still created, because only the inherited streams
    // are replaced by that flag.
    SDL_SetBooleanProperty(
        creationProperties,
        SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN,
        true);
    {
        lock_guard<mutex> processLock(processMutex);
        process=SDL_CreateProcessWithProperties(creationProperties);
    }
    SDL_DestroyProperties(creationProperties);
    if (process==nullptr) {
        lock_guard<mutex> lineLock(lineMutex);
        CliLine failureLine;
        failureLine.text=
            string("The command line program could not be started. ") + locatedProgram.details;
        queuedLines.push_back(failureLine);
        runningFlag=false;
        return false;
    }
    runningFlag=true;
    exitCodeValue=0;
    readerThread=thread(&CliRunner::readOutputLoop, this);
    return true;
}
bool CliRunner::isRunning() const {
    return runningFlag;
}
void CliRunner::cancel() {
    // The kill is serialised with the reader's calls into the same process handle,
    // because SDL's process functions are documented as safe to call from any
    // thread but not as safe to call at the same time as each other. The lock is
    // held for one non-blocking read at most.
    lock_guard<mutex> processLock(processMutex);
    if (process!=nullptr && runningFlag) {
        SDL_KillProcess(process, true);
    }
}
void CliRunner::join() {
    if (readerThread.joinable()) {
        readerThread.join();
    }
    // The reader has ended, which means the child closed its output: it is either
    // finished or was stopped. Waiting for it here rather than on the reader thread
    // keeps every call into the process handle on the interface thread once the run
    // is over, and it is what makes the exit code exact rather than a guess.
    lock_guard<mutex> processLock(processMutex);
    if (process!=nullptr) {
        int finishedExitCode=0;
        SDL_WaitProcess(process, true, &finishedExitCode);
        exitCodeValue=finishedExitCode;
        SDL_DestroyProcess(process);
        process=nullptr;
    }
}
vector<CliLine> CliRunner::takeLines() {
    lock_guard<mutex> lineLock(lineMutex);
    vector<CliLine> takenLines;
    takenLines.swap(queuedLines);
    return takenLines;
}
int CliRunner::exitCode() const {
    return exitCodeValue;
}
void CliRunner::queueLine(const string& lineText) {
    CliLine line;
    line.text=lineText;
    // Only a line that begins with a brace can be an event. Anything else is prose
    // the command printed for a person, and it is kept as text for the log pane
    // rather than reported as a parse failure.
    if (!lineText.empty() && lineText[0]=='{' && looksLikeJsonObject(lineText)) {
        JsonValue parsedLine;
        if (JsonValue::parse(lineText, parsedLine)) {
            line.isJson=true;
            line.json=parsedLine;
        }
    }
    lock_guard<mutex> lineLock(lineMutex);
    queuedLines.push_back(line);
}
void CliRunner::readOutputLoop() {
    const string locatedDetails=locateCliExecutableDetails();
    SDL_IOStream* outputStream=nullptr;
    {
        lock_guard<mutex> processLock(processMutex);
        if (process!=nullptr) {
            outputStream=SDL_GetProcessOutput(process);
        }
    }
    if (outputStream==nullptr) {
        {
            lock_guard<mutex> lineLock(lineMutex);
            CliLine failureLine;
            failureLine.text=
                string("The command line program started but its output could not be read. ")
                + locatedDetails;
            queuedLines.push_back(failureLine);
        }
        runningFlag=false;
        return;
    }
    // The text of the line being assembled. A chunk of output can end anywhere,
    // including in the middle of a line, so the remainder is kept until the rest
    // of it arrives.
    string pendingText;
    char readBuffer[READ_CHUNK_BYTES];
    bool reachedEndOfOutput=false;
    while (!reachedEndOfOutput) {
        size_t readByteCount=0;
        {
            lock_guard<mutex> processLock(processMutex);
            if (process==nullptr) {
                reachedEndOfOutput=true;
                break;
            }
            readByteCount=SDL_ReadIO(outputStream, readBuffer, sizeof(readBuffer));
        }
        if (readByteCount>0) {
            pendingText.append(readBuffer, readByteCount);
            size_t lineStart=0;
            size_t lineEnd=pendingText.find('\n', lineStart);
            while (lineEnd!=string::npos) {
                string lineText=pendingText.substr(lineStart, lineEnd - lineStart);
                // A carriage return before the line feed is how Windows ends a
                // line; it is not part of the line.
                if (!lineText.empty() && lineText.back()=='\r') {
                    lineText.pop_back();
                }
                queueLine(lineText);
                lineStart=lineEnd + 1;
                lineEnd=pendingText.find('\n', lineStart);
            }
            pendingText.erase(0, lineStart);
            continue;
        }
        // A read of nothing means either that the child is quiet or that it has
        // closed its output, and the stream's own status is what tells them apart.
        SDL_IOStatus streamStatus=SDL_IO_STATUS_READY;
        {
            lock_guard<mutex> processLock(processMutex);
            if (process==nullptr) {
                reachedEndOfOutput=true;
                break;
            }
            streamStatus=SDL_GetIOStatus(outputStream);
        }
        if (streamStatus==SDL_IO_STATUS_NOT_READY) {
            SDL_Delay(static_cast<Uint32>(READER_IDLE_MILLISECONDS));
            continue;
        }
        reachedEndOfOutput=true;
    }
    // A last line without its line ending is still a line. The tool always writes
    // the line ending, but a child that was stopped part way through a write must
    // not lose what it had already handed over.
    if (!pendingText.empty()) {
        if (pendingText.back()=='\r') {
            pendingText.pop_back();
        }
        queueLine(pendingText);
    }
    runningFlag=false;
}
}
