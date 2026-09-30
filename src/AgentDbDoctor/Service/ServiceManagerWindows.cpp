// The Windows implementation of the platform specific parts of the ServiceManager
// class.
//
// A Windows program becomes a service in two pieces. The executable exports an
// entry point named `ServiceMain`, which the Service Control Manager looks up by
// name. The install command creates a registry key that names the program and the
// arguments it is started with. Together those two make a process that the
// operating system starts, watches, and stops cleanly.
#define WIN32_LEAN_AND_MEAN
// Declares the Service Control Manager and registry functions used below.
#include <windows.h>
// Provides `string` and `wstring`, the narrow and wide character strings.
#include <string>
// Provides `wcslen`, which measures a wide string.
#include <cwchar>
// Provides `optional`, which the configuration read returns.
#include <optional>
#include <vector>
// Provides the ServiceManager class declaration.
#include "AgentDbDoctor/Service/ServiceManager.hpp"
// Provides the configuration reader and the default values.
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
// Provides the ServiceOperationException, raised when the platform refuses.
#include "AgentDbDoctor/Common/Exceptions.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Service;
namespace AgentDbDoctor::Service {
// The name the Control Manager knows this service by. The registry key is named
// after it, so the two must agree and the constant keeps them that way.
static constexpr const wchar_t* WINDOWS_SERVICE_NAME=L"agent-db-doctor";
// The display name shown in the Services list. It says what the service is for
// rather than repeating the program name, because that is what a person is looking
// at when they open the list.
static constexpr wchar_t* WINDOWS_DISPLAY_NAME=
    const_cast<wchar_t*>(L"AI coding agent database doctor");
// The description shown in the service's properties page.
static constexpr wchar_t* WINDOWS_DESCRIPTION=
    const_cast<wchar_t*>(L"Watches coding agent databases and reports when they grow too large");
// The handle of the running ServiceManager, so the control handler can reach it.
//
// The Control Manager calls the control handler on a thread of its own, so the
// manager has to be reachable from somewhere outside the stack that started the
// service. It is a plain pointer rather than a `unique_ptr` because nothing owns
// it: the operating system owns the process, and the manager is a local object in
// the service entry point whose lifetime is the service's lifetime.
static ServiceManager* runningServiceManager=nullptr;
// The status the Control Manager expects a service to report. It is a file scope
// value because the Control Manager is told about it repeatedly, and passing it by
// address each time is what the interface requires.
static SERVICE_STATUS serviceStatus;
// The registry path of the service key, built once. The doubled backslashes are
// the wide string escape for a single one.
static const wchar_t* WINDOWS_SERVICE_KEY_PATH=
    L"SYSTEM\\CurrentControlSet\\Services\\agent-db-doctor";
string ServiceManager::currentExecutablePath() {
    // The path buffer is a fixed size array, so asking where the program lives costs
    // no allocation. The size is in characters rather than bytes on Windows, which
    // is why the buffer is four times the maximum path length in characters.
    wchar_t pathBuffer[MAX_PATH * 4];
    const DWORD copiedLength=GetModuleFileNameW(nullptr, pathBuffer, MAX_PATH * 4);
    if (copiedLength==0) {
        return string("agent-db-doctor.exe");
    }
    const int byteCount=WideCharToMultiByte(
        CP_UTF8,
        0,
        pathBuffer,
        static_cast<int>(copiedLength),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (byteCount<=0) {
        return string("agent-db-doctor.exe");
    }
    string executablePath(static_cast<size_t>(byteCount), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        pathBuffer,
        static_cast<int>(copiedLength),
        executablePath.data(),
        byteCount,
        nullptr,
        nullptr
    );
    return executablePath;
}
// Writes one wide string into a registry value. The size argument is the byte count
// including the terminating null, which is what the registry interface wants.
static void writeRegistryString(HKEY serviceKey, const wchar_t* valueName, const wchar_t* valueText) {
    RegSetValueExW(
        serviceKey,
        valueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(valueText),
        static_cast<DWORD>((wcslen(valueText) + 1) * sizeof(wchar_t))
    );
}
// The control handler, which the Control Manager calls when the service is asked to
// stop or when it wants to know the current status.
//
// The signature is fixed by Windows and takes only the control code. Anything else
// this service does not handle is answered by doing nothing, which is the
// documented way to say so.
static VOID WINAPI serviceControlHandler(DWORD controlCode) {
    switch (controlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN: {
            // The status is reported as pending first, so the Control Manager knows
            // a stop is under way and does not send another one.
            serviceStatus.dwCurrentState=SERVICE_STOP_PENDING;
            SetServiceStatus(nullptr, &serviceStatus);
            // The loop notices the request on its next short sleep and returns, at
            // which point this function returns and the process ends.
            if (runningServiceManager!=nullptr) {
                runningServiceManager->requestStop();
            }
            return;
        }
        case SERVICE_CONTROL_INTERROGATE: {
            // The Control Manager asks for the status at any time. Answering keeps
            // it from deciding the service has hung and restarting it.
            SetServiceStatus(nullptr, &serviceStatus);
            return;
        }
        default: {
            return;
        }
    }
}
// The service entry point, which the Control Manager calls once when the service
// starts. The name and the export are both fixed by Windows.
extern "C" __declspec(dllexport) VOID WINAPI ServiceMain(
    DWORD argumentCount,
    LPWSTR* argumentValues
) {
    // The two parameters are named because the signature is fixed, and unused
    // because the Control Manager passes nothing the service needs.
    (void)argumentCount;
    (void)argumentValues;
    // The configuration is read here, on the service's own thread, so a bad file
    // is reported into the log rather than stopping the process before the Control
    // Manager has been told the service started.
    ServiceConfiguration configuration=ServiceConfiguration::createDefault();
    const optional<ServiceConfiguration> readConfiguration=
        ConfigurationFile::read(configuration.configurationFilePath);
    if (readConfiguration.has_value()) {
        configuration=readConfiguration.value();
    }
    ServiceManager serviceManager(configuration);
    runningServiceManager=&serviceManager;
    // The status is filled in before any call that could block, because the Control
    // Manager expects to be told the service has started.
    serviceStatus.dwServiceType=SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState=SERVICE_START_PENDING;
    serviceStatus.dwWin32ExitCode=NO_ERROR;
    serviceStatus.dwServiceSpecificExitCode=0;
    // The control handler is registered by address. Windows stores the pointer and
    // calls it, so it must outlive this function. The handle it returns is kept
    // only long enough to be given to the status setter.
    const SERVICE_STATUS_HANDLE statusHandle=
        RegisterServiceCtrlHandlerW(WINDOWS_SERVICE_NAME, serviceControlHandler);
    if (statusHandle==nullptr) {
        // Without a status handle the Control Manager cannot be told anything, and
        // it will treat the service as having failed to start. Returning here ends
        // the process, which is the correct outcome.
        return;
    }
    SetServiceStatus(statusHandle, &serviceStatus);
    serviceStatus.dwCurrentState=SERVICE_RUNNING;
    SetServiceStatus(statusHandle, &serviceStatus);
    // The watchdog loop is the body of the service. It does not return until a stop
    // is requested.
    serviceManager.runWatchdogLoop();
    serviceStatus.dwCurrentState=SERVICE_STOPPED;
    SetServiceStatus(statusHandle, &serviceStatus);
    runningServiceManager=nullptr;
}
// The dispatcher entry point, which the Control Manager calls first. It reads the
// table of entry points and connects them, then calls `ServiceMain` later.
extern "C" __declspec(dllexport) VOID WINAPI ServiceMainEntry(
    DWORD argumentCount,
    LPWSTR* argumentValues
) {
    (void)argumentCount;
    (void)argumentValues;
    // The empty name in the first entry tells the dispatcher to use the name this
    // program was started with. The null entry ends the table, which is required.
    SERVICE_TABLE_ENTRYW serviceTable[]={
        {const_cast<LPWSTR>(L""), ServiceMain},
        {nullptr, nullptr}
    };
    StartServiceCtrlDispatcherW(serviceTable);
}
}
void ServiceManager::writePlatformServiceDefinition() const {
    // A Windows service is defined entirely in the registry, so there is no file to
    // write. The install command creates the key and fills in the values the Control
    // Manager reads when it starts the program.
    HKEY serviceKey=nullptr;
    DWORD disposition=0;
    const LONG createResult=RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        WINDOWS_SERVICE_KEY_PATH,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &serviceKey,
        &disposition
    );
    if (createResult!=ERROR_SUCCESS) {
        throw ServiceOperationException(
            string("Registering the service"),
            string("The registry key could not be created. That needs administrator rights, so run ")
                + "the command from a prompt that was opened as an administrator."
        );
    }
    writeRegistryString(serviceKey, L"DisplayName", WINDOWS_DISPLAY_NAME);
    writeRegistryString(serviceKey, L"Description", WINDOWS_DESCRIPTION);
    // `auto` means the Control Manager starts the service when the machine does.
    writeRegistryString(serviceKey, L"Start", const_cast<wchar_t*>(L"auto"));
    // The image path is the program followed by its arguments, all inside one quoted
    // string, because that is the form the Control Manager splits.
    const string imagePath="\"" + currentExecutablePath() + "\" service run";
    const std::wstring wideImagePath(imagePath.begin(), imagePath.end());
    RegSetValueExW(
        serviceKey,
        L"ImagePath",
        0,
        REG_EXPAND_SZ,
        reinterpret_cast<const BYTE*>(wideImagePath.c_str()),
        static_cast<DWORD>((wideImagePath.size() + 1) * sizeof(wchar_t))
    );
    // Running as the local system account is what lets the service read a database
    // in any user's home directory, which is where every supported agent keeps one.
    writeRegistryString(serviceKey, L"ObjectName", const_cast<wchar_t*>(L"LocalSystem"));
    // The key is closed on the way out, because leaving a registry key open blocks
    // anything else that wants to change the service.
    RegCloseKey(serviceKey);
}
void ServiceManager::removePlatformServiceDefinition() const {
    // Stopping first is what makes removal safe: a running service holds its own key
    // open, and deleting it underneath a running process leaves the Control Manager
    // unable to account for it.
    stopPlatformService();
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, WINDOWS_SERVICE_KEY_PATH);
}
void ServiceManager::startPlatformService() const {
    const SC_HANDLE managerHandle=OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (managerHandle==nullptr) {
        throw ServiceOperationException(
            string("Starting the service"),
            string("The Service Control Manager could not be opened. That needs administrator ")
                + "rights, so run the command from a prompt that was opened as an administrator."
        );
    }
    const SC_HANDLE targetService=OpenServiceW(
        managerHandle,
        WINDOWS_SERVICE_NAME,
        SERVICE_START | SERVICE_QUERY_STATUS
    );
    if (targetService==nullptr) {
        CloseServiceHandle(managerHandle);
        throw ServiceOperationException(
            string("Starting the service"),
            string("The service does not appear to be installed. Run the install command first.")
        );
    }
    const BOOL startResult=StartServiceW(targetService, 0, nullptr);
    // A service that is already running reports an error rather than succeeding, so
    // that one error is treated as success rather than as a failure to report.
    const DWORD startError=GetLastError();
    CloseServiceHandle(targetService);
    CloseServiceHandle(managerHandle);
    if (!startResult && startError!=ERROR_SERVICE_ALREADY_RUNNING) {
        throw ServiceOperationException(
            string("Starting the service"),
            string("The operating system reported error ") + to_string(startError)
            + ". Check that the log folder exists and that the program can write to it."
        );
    }
}
void ServiceManager::stopPlatformService() const {
    const SC_HANDLE managerHandle=OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (managerHandle==nullptr) {
        // A machine where the Control Manager cannot be opened has no service
        // running, which is what the caller wanted anyway.
        return;
    }
    const SC_HANDLE targetService=OpenServiceW(
        managerHandle,
        WINDOWS_SERVICE_NAME,
        SERVICE_STOP | SERVICE_QUERY_STATUS
    );
    if (targetService==nullptr) {
        CloseServiceHandle(managerHandle);
        return;
    }
    SERVICE_STATUS currentStatus;
    ControlService(targetService, SERVICE_CONTROL_STOP, &currentStatus);
    // The polling loop is what turns a stop request into a stopped service. Ten
    // seconds is long enough for the watchdog to notice between its short sleeps.
    for (int waitAttempt=0; waitAttempt<100; ++waitAttempt) {
        if (!QueryServiceStatus(targetService, &currentStatus)) {
            break;
        }
        if (currentStatus.dwCurrentState==SERVICE_STOPPED) {
            break;
        }
        Sleep(100);
    }
    CloseServiceHandle(targetService);
    CloseServiceHandle(managerHandle);
}
ServiceStatusSnapshot ServiceManager::readPlatformStatus() const {
    ServiceStatusSnapshot status;
    const SC_HANDLE managerHandle=OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (managerHandle==nullptr) {
        return status;
    }
    const SC_HANDLE targetService=OpenServiceW(
        managerHandle,
        WINDOWS_SERVICE_NAME,
        SERVICE_QUERY_STATUS
    );
    if (targetService==nullptr) {
        // The Control Manager opened but the service is not there, which is the
        // normal answer on a machine where it was never installed.
        CloseServiceHandle(managerHandle);
        return status;
    }
    status.isInstalled=true;
    SERVICE_STATUS currentStatus;
    if (QueryServiceStatus(targetService, &currentStatus)) {
        status.isRunning=currentStatus.dwCurrentState==SERVICE_RUNNING;
    }
    CloseServiceHandle(targetService);
    CloseServiceHandle(managerHandle);
    return status;
}
