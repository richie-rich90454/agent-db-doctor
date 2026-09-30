// The macOS implementation of the platform specific parts of the ServiceManager
// class.
//
// On macOS a service is a property list loaded by launchd. Installing means
// writing that plist into the launch daemon folder and telling launchd to load it;
// starting, stopping, and querying all go through the `launchctl` program, which is
// the supported interface.
#include <mach-o/dyld.h>
// Provides `_NSGetExecutablePath`, which reports where the running program is.
#include <mach-o/dyld.h>
#include <cstdio>
// Provides `popen` and `pclose`, which run a command and read its output.
#include <cstdio>
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
#include <fstream>
// Provides `ofstream`, which writes the property list file.
#include <fstream>
#include <vector>
#include <filesystem>
// Provides the ServiceManager class declaration.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Service;
namespace AgentDbDoctor::Service {
// Where launchd looks for system wide launch daemons.
static constexpr const char* LAUNCH_DAEMON_DIRECTORY="/Library/LaunchDaemons";
// The name of the property list file, which is also the label launchd knows the
// service by. The reverse domain form is the convention Apple asks for.
static constexpr const char* LAUNCH_DAEMON_FILE_NAME="com.agentdbdoctor.plist";
// The label passed to launchctl, which is the plist name without its suffix.
static constexpr const char* LAUNCH_LABEL="com.agentdbdoctor";
string ServiceManager::currentExecutablePath() {
    // The function asks the dynamic loader where the running image is. The first
    // call reports the size needed, the second fills the buffer.
    uint32_t bufferSize=0;
    _NSGetExecutablePath(nullptr, &bufferSize);
    if (bufferSize==0) {
        return string("/usr/local/bin/agent-db-doctor");
    }
    vector<char> pathBuffer(bufferSize, '\0');
    if (_NSGetExecutablePath(pathBuffer.data(), &bufferSize)!=0) {
        return string("/usr/local/bin/agent-db-doctor");
    }
    // The reported path can be relative, so it is turned into an absolute one.
    // Without this, a daemon launched from a different working directory would
    // fail to find its own executable.
    std::error_code ignoredError;
    const std::filesystem::path absolutePath=std::filesystem::weakly_canonical(
        std::filesystem::path(pathBuffer.data()),
        ignoredError
    );
    if (absolutePath.empty()) {
        return string(pathBuffer.data());
    }
    return absolutePath.string();
}
namespace {
// Runs a command and returns everything it wrote to standard output.
//
// `popen` starts a shell and returns a handle to read from it, which is the
// supported way for a program to ask `launchctl` something.
string runCommandAndCaptureOutput(const string& command) {
    string collectedOutput;
    FILE* commandPipe=popen(command.c_str(), "r");
    if (commandPipe==nullptr) {
        return collectedOutput;
    }
    char lineBuffer[512];
    while (fgets(lineBuffer, sizeof(lineBuffer), commandPipe)!=nullptr) {
        collectedOutput+=lineBuffer;
    }
    pclose(commandPipe);
    return collectedOutput;
}
}
void ServiceManager::writePlatformServiceDefinition() const {
    const string plistFilePath=string(LAUNCH_DAEMON_DIRECTORY) + "/" + LAUNCH_DAEMON_FILE_NAME;
    std::ofstream plistFile(plistFilePath, std::ios::trunc);
    if (!plistFile.is_open()) {
        throw ServiceOperationException(
            string("The service file could not be written"),
            string("The tool tried to write ") + plistFilePath
            + ". That folder is owned by root, so the command needs to be run with "
            + "administrator rights, for example with sudo."
        );
    }
    // The property list is written by hand so its contents stay visible to whoever
    // ends up debugging the service. The three settings under `ProcessType` and
    // `Nice` are what keep a background daemon from competing with real work.
    plistFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    plistFile << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
              << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    plistFile << "<plist version=\"1.0\">\n";
    plistFile << "<dict>\n";
    plistFile << "    <key>Label</key>\n";
    plistFile << "    <string>" << LAUNCH_LABEL << "</string>\n";
    plistFile << "    <key>ProgramArguments</key>\n";
    plistFile << "    <array>\n";
    plistFile << "        <string>" << currentExecutablePath() << "</string>\n";
    plistFile << "        <string>service</string>\n";
    plistFile << "        <string>run</string>\n";
    plistFile << "    </array>\n";
    // The daemon keeps running and is restarted if it ever exits, which is the
    // behaviour a monitoring service wants.
    plistFile << "    <key>KeepAlive</key>\n";
    plistFile << "    <true/>\n";
    plistFile << "    <key>RunAtLoad</key>\n";
    plistFile << "    <true/>\n";
    // `Low` priority and a small footprint keep the tick off the critical path when
    // the machine is busy.
    plistFile << "    <key>ProcessType</key>\n";
    plistFile << "    <string>Background</string>\n";
    plistFile << "    <key>LowPriorityIO</key>\n";
    plistFile << "    <true/>\n";
    plistFile << "    <key>Nice</key>\n";
    plistFile << "    <integer>5</integer>\n";
    plistFile << "    <key>StandardOutPath</key>\n";
    plistFile << "    <string>" << serviceConfiguration.logFilePath << "</string>\n";
    plistFile << "    <key>StandardErrorPath</key>\n";
    plistFile << "    <string>" << serviceConfiguration.logFilePath << "</string>\n";
    plistFile << "    <key>EnvironmentVariables</key>\n";
    plistFile << "    <dict>\n";
    // A launch daemon starts with a minimal environment and no home directory, so
    // the paths the tool looks in would otherwise all be wrong. The home directory
    // of the user who installed it is recorded here, which is the only way a system
    // wide daemon can find a per user database.
    plistFile << "        <key>HOME</key>\n";
    plistFile << "        <string>" << PlatformMetadata::homeDirectory() << "</string>\n";
    plistFile << "    </dict>\n";
    plistFile << "</dict>\n";
    plistFile << "</plist>\n";
    plistFile.close();
    // The daemon must own the file, or launchd refuses to load it. `chown` is run
    // through the shell because there is no standard library call for it.
    runCommandAndCaptureOutput(
        string("chown root:wheel \"") + plistFilePath + "\" && chmod 644 \"" + plistFilePath + "\""
    );
    runCommandAndCaptureOutput(
        string("launchctl unload \"") + plistFilePath + "\" 2>/dev/null"
    );
    runCommandAndCaptureOutput(string("launchctl load -w \"") + plistFilePath + "\"");
}
void ServiceManager::removePlatformServiceDefinition() const {
    const string plistFilePath=string(LAUNCH_DAEMON_DIRECTORY) + "/" + LAUNCH_DAEMON_FILE_NAME;
    runCommandAndCaptureOutput(string("launchctl unload \"") + plistFilePath + "\" 2>/dev/null");
    std::error_code ignoredError;
    std::filesystem::remove(plistFilePath, ignoredError);
}
void ServiceManager::startPlatformService() const {
    const string plistFilePath=string(LAUNCH_DAEMON_DIRECTORY) + "/" + LAUNCH_DAEMON_FILE_NAME;
    const string result=runCommandAndCaptureOutput(
        string("launchctl load -w \"") + plistFilePath + "\" 2>&1"
    );
    if (result.find("Load failed")!=string::npos) {
        throw ServiceOperationException(
            string("The service did not start"),
            string("launchctl said: ") + result
            + " Check the log at " + serviceConfiguration.logFilePath + "."
        );
    }
}
void ServiceManager::stopPlatformService() const {
    const string plistFilePath=string(LAUNCH_DAEMON_DIRECTORY) + "/" + LAUNCH_DAEMON_FILE_NAME;
    runCommandAndCaptureOutput(string("launchctl unload \"") + plistFilePath + "\" 2>/dev/null");
}
ServiceStatusSnapshot ServiceManager::readPlatformStatus() const {
    ServiceStatusSnapshot status;
    const string plistFilePath=string(LAUNCH_DAEMON_DIRECTORY) + "/" + LAUNCH_DAEMON_FILE_NAME;
    status.isInstalled=std::filesystem::exists(plistFilePath);
    if (!status.isInstalled) {
        return status;
    }
    // `launchctl list` prints one line per loaded job whose label matches. The
    // presence of a line with the first dash column filled means it is running.
    const string output=runCommandAndCaptureOutput(
        string("launchctl list ") + LAUNCH_LABEL + " 2>&1"
    );
    status.isRunning=output.find(LAUNCH_LABEL)!=string::npos
        && output.find("Could not find service")==string::npos;
    return status;
}
}
