// The Linux implementation of the platform specific parts of the ServiceManager
// class.
//
// On Linux a service is a unit file that systemd reads. Installing means writing
// that file and asking systemd to load it; starting, stopping, and querying all go
// through the `systemctl` program, which is the supported interface rather than
// talking to the daemon over its socket.
#include <unistd.h>
// Provides `readlink`, which reads the symbolic link the kernel maintains pointing
// at the running program.
#include <unistd.h>
#include <cstdio>
// Provides `popen` and `pclose`, which run a command and read its output.
#include <cstdio>
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
#include <filesystem>
// Provides the ServiceManager class declaration.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Service;
namespace AgentDbDoctor::Service {
// Where systemd keeps unit files that belong to the system rather than to a user.
static constexpr const char* UNIT_FILE_DIRECTORY="/etc/systemd/system";
// The name of the unit file, without its suffix.
static constexpr const char* UNIT_FILE_NAME="agent-db-doctor.service";
// The name the user types, which is also what systemctl is given.
static constexpr const char* UNIT_NAME="agent-db-doctor.service";
string ServiceManager::currentExecutablePath() {
    // The kernel keeps a symbolic link at this path pointing at the running
    // program. Reading the link gives the answer in one call and no allocation
    // beyond the string itself.
    char pathBuffer[4096];
    const ssize_t copiedLength=readlink("/proc/self/exe", pathBuffer, sizeof(pathBuffer) - 1);
    if (copiedLength<=0) {
        return string("/usr/local/bin/agent-db-doctor");
    }
    return string(pathBuffer, static_cast<size_t>(copiedLength));
}
namespace {
// Runs a command and returns everything it wrote to standard output.
//
// `popen` starts a shell and returns a handle to read from it, which is the
// supported way for a program to ask `systemctl` something. The second argument
// says to read rather than write, which is the only direction a caller needs.
string runCommandAndCaptureOutput(const string& command) {
    string collectedOutput;
    FILE* commandPipe=popen(command.c_str(), "r");
    if (commandPipe==nullptr) {
        return collectedOutput;
    }
    // The buffer is a fixed size array, so reading the output allocates nothing
    // until the final string is built.
    char lineBuffer[512];
    while (fgets(lineBuffer, sizeof(lineBuffer), commandPipe)!=nullptr) {
        collectedOutput+=lineBuffer;
    }
    // The handle must be closed, and `pclose` also waits for the command to finish,
    // which is what makes the output complete.
    pclose(commandPipe);
    return collectedOutput;
}
}
void ServiceManager::writePlatformServiceDefinition() const {
    const string unitFilePath=string(UNIT_FILE_DIRECTORY) + "/" + UNIT_FILE_NAME;
    std::ofstream unitFile(unitFilePath, std::ios::trunc);
    if (!unitFile.is_open()) {
        throw ServiceOperationException(
            string("The service file could not be written"),
            string("The tool tried to write ") + unitFilePath
            + ". That folder is owned by root, so the command needs to be run with "
            + "administrator rights, for example with sudo."
        );
    }
    // The unit file is written by hand so that its contents are visible and
    // editable by whoever ends up debugging the service. Every setting is chosen
    // for the performance contract: one thread, a hard memory limit, and a restart
    // policy that does not thrash.
    unitFile << "[Unit]\n";
    unitFile << "Description=Watches AI coding agent databases and reports when they grow too large\n";
    unitFile << "After=default.target\n";
    unitFile << "\n";
    unitFile << "[Service]\n";
    unitFile << "Type=simple\n";
    // The loop is the service body, so it runs in the foreground of the unit. That
    // is what lets systemd know the service is alive without a separate heartbeat.
    unitFile << "ExecStart=" << currentExecutablePath() << " service run\n";
    // The unit restarts if the loop ever exits unexpectedly, but not more often
    // than once every five minutes, so a persistent fault does not spin the process.
    unitFile << "Restart=on-failure\n";
    unitFile << "RestartSec=300\n";
    // The memory limit is the specification's idle figure plus headroom. A process
    // that exceeds it is a bug, and systemd restarting it is the correct response.
    unitFile << "MemoryMax=60M\n";
    // One thread, because that is what the design assumes and what keeps the idle
    // cost predictable.
    unitFile << "TasksMax=1\n";
    // The service never needs to be interactive, so it gets no terminal and no
    // standard input.
    unitFile << "StandardInput=null\n";
    // The service writes its own log file, so systemd's journal stays clean.
    unitFile << "StandardOutput=null\n";
    unitFile << "StandardError=null\n";
    unitFile << "\n";
    unitFile << "[Install]\n";
    unitFile << "WantedBy=multi-user.target\n";
    unitFile.close();
    // The daemon has to be told to read the new file, and then told to start
    // following it on boot. Both are the documented two step for enabling a unit.
    runCommandAndCaptureOutput("systemctl daemon-reload");
    runCommandAndCaptureOutput(string("systemctl enable ") + UNIT_NAME);
}
void ServiceManager::removePlatformServiceDefinition() const {
    runCommandAndCaptureOutput(string("systemctl disable ") + UNIT_NAME);
    runCommandAndCaptureOutput(string("systemctl stop ") + UNIT_NAME);
    std::error_code ignoredError;
    std::filesystem::remove(string(UNIT_FILE_DIRECTORY) + "/" + UNIT_FILE_NAME, ignoredError);
    runCommandAndCaptureOutput("systemctl daemon-reload");
}
void ServiceManager::startPlatformService() const {
    const string result=runCommandAndCaptureOutput(string("systemctl start ") + UNIT_NAME + " 2>&1");
    if (result.find("Failed")!=string::npos || result.find("error")!=string::npos) {
        throw ServiceOperationException(
            string("The service did not start"),
            string("systemctl said: ") + result
            + " Check the log at " + serviceConfiguration.logFilePath + "."
        );
    }
}
void ServiceManager::stopPlatformService() const {
    runCommandAndCaptureOutput(string("systemctl stop ") + UNIT_NAME);
}
ServiceStatusSnapshot ServiceManager::readPlatformStatus() const {
    ServiceStatusSnapshot status;
    const string unitFilePath=string(UNIT_FILE_DIRECTORY) + "/" + UNIT_FILE_NAME;
    // The file system answers whether the service is installed, which costs one
    // call and no process at all.
    status.isInstalled=std::filesystem::exists(unitFilePath);
    if (!status.isInstalled) {
        return status;
    }
    const string output=runCommandAndCaptureOutput(
        string("systemctl is-active ") + UNIT_NAME + " 2>&1"
    );
    // `systemctl is-active` prints `active` and nothing else when the unit is up.
    status.isRunning=output.find("active")!=string::npos;
    return status;
}
}
