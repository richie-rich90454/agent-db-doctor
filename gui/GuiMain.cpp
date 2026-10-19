// The graphical interface: the window, the menus, the settings file, and the loop.
//
// This file owns the SDL3 window, the Dear ImGui context, and the frame loop. It
// does not own any of the tool's behaviour: every action in the window is a command
// line invocation of `agent-db-doctor`, started by the runner in CliRunner.cpp,
// whose output is turned into state by GuiState.cpp and drawn by AppScreens.cpp.
// The window therefore never opens a database, and it cannot disagree with the
// command line about a number, because there is only one implementation of the
// numbers and it lives in the command line program.
//
// The layout is fixed: a menu bar, the tab area, and then the activity bar and the
// log pane, which are always in the same place. A reader always knows where the
// progress of a long command is, and where its raw output went.
// Provides `uintptr_t`, the integer type a texture handle travels through.
#include <cstdint>
// Provides `snprintf`, for the settings text and the font path messages.
#include <cstdio>
// Provides `mutex`, guarding the answer a file dialogue leaves behind.
#include <mutex>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
#if defined(_WIN32)
// The window is built as a Windows program rather than as a console program, so its
// entry point is WinMain and SDL must not supply one of its own.
#define SDL_MAIN_HANDLED 1
// Provides the types WinMain is declared with on Windows.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
// Provides `HINSTANCE`, `LPSTR`, and `WINAPI`, which the Windows entry point needs.
#include <windows.h>
#endif
// Provides SDL3: window, renderer, events, process dialogs, and the theme query.
#include <SDL3/SDL.h>
// Provides Dear ImGui: the immediate mode widgets of every screen.
#include <imgui.h>
// Provides the SDL3 platform layer for Dear ImGui.
#include <imgui_impl_sdl3.h>
// Provides the SDL_Renderer drawing layer for Dear ImGui.
#include <imgui_impl_sdlrenderer3.h>
// Provides the PNG decoder for the window icon and the about logo.
#include <stb_image.h>
// Provides the screens: every tab, the activity bar, the log pane, and the dialogues.
#include "AppScreens.hpp"
// Provides the runner that starts the command line program.
#include "CliRunner.hpp"
// Provides the state the events fill and the settings file format.
#include "GuiState.hpp"
// The tool version and the platform the window was built for, stamped by CMake so
// the title bar, the about dialogue, and `--version` all agree.
#ifndef AGENT_DB_DOCTOR_VERSION
#define AGENT_DB_DOCTOR_VERSION "1.0.0"
#endif
#ifndef AGENT_DB_DOCTOR_PLATFORM
#define AGENT_DB_DOCTOR_PLATFORM "Unknown"
#endif
// The directory the artwork was copied beside the binary at build time. It is only
// a fallback: at run time the window looks beside its own executable first, which is
// what makes a copied package work on a machine that never saw the build directory.
#ifndef AGENT_DB_DOCTOR_ASSET_DIR
#define AGENT_DB_DOCTOR_ASSET_DIR "."
#endif
using namespace std;
using namespace AgentDbDoctor::Gui;
namespace {
// How many frames the smoke test draws before it exits. Continuous integration runs
// it under a virtual display to prove that the window, the renderer, and the drawing
// pipeline all come up.
constexpr int SMOKE_TEST_FRAME_COUNT=120;
// How many milliseconds each smoke test frame is held for, which gives the window
// about two seconds of drawing.
constexpr int SMOKE_TEST_FRAME_DELAY_MILLISECONDS=16;
// How many tabs the window has, which is what the smoke test walks through.
constexpr int TAB_COUNT=5;
// The smallest fraction of the window the tab area is allowed to keep. The room the
// activity bar and the log pane keep for themselves is computed from the current
// font by bottomBarHeight, because fixed pixel counts clipped the newest log line
// at a scaled display.
constexpr float MINIMUM_TAB_AREA_HEIGHT=140.0f;
// The name of the settings file inside the folder the platform gives this program
// for its own files. It is plain text rather than an ImGui ini file, because the
// settings are the tool's own and a person should be able to read and edit them.
constexpr const char* SETTINGS_FILE_NAME="interface-settings.txt";
// The folder the artwork is found in: the folder holding the running binary, which
// is where the build copies the PNGs and where a package keeps them beside the
// program. SDL_GetBasePath returns a path that already ends with a separator, and
// the buffer belongs to SDL, so it is copied and not freed.
string artworkDirectoryPath() {
    string basePath;
    const char* reportedBasePath=SDL_GetBasePath();
    if (reportedBasePath!=nullptr) {
        basePath=reportedBasePath;
    }
    if (basePath.empty()) {
        basePath=AGENT_DB_DOCTOR_ASSET_DIR;
        if (!basePath.empty() && basePath.back()!='/' && basePath.back()!='\\') {
            basePath+="/";
        }
    }
    return basePath;
}
// Loads a PNG beside the binary into an SDL texture, or null when it is missing.
// Missing artwork is never an error: the interface simply shows one fewer picture,
// because pictures must never stop the tool from working.
SDL_Texture* loadArtworkTexture(SDL_Renderer* renderer, const char* fileName) {
    const string artworkPath=artworkDirectoryPath() + fileName;
    int artworkWidth=0;
    int artworkHeight=0;
    int ignoredChannels=0;
    unsigned char* artworkPixels=stbi_load(
        artworkPath.c_str(), &artworkWidth, &artworkHeight, &ignoredChannels, STBI_rgb_alpha);
    if (artworkPixels==nullptr || artworkWidth<=0 || artworkHeight<=0) {
        return nullptr;
    }
    SDL_Surface* artworkSurface=SDL_CreateSurfaceFrom(
        artworkWidth,
        artworkHeight,
        SDL_PIXELFORMAT_RGBA32,
        artworkPixels,
        artworkWidth * 4);
    if (artworkSurface==nullptr) {
        stbi_image_free(artworkPixels);
        return nullptr;
    }
    SDL_Texture* artworkTexture=SDL_CreateTextureFromSurface(renderer, artworkSurface);
    SDL_DestroySurface(artworkSurface);
    stbi_image_free(artworkPixels);
    return artworkTexture;
}
// Loads the window icon, or null when it is missing, for the same reason.
SDL_Surface* loadWindowIcon() {
    const string iconPath=artworkDirectoryPath() + "favicon-64.png";
    int iconWidth=0;
    int iconHeight=0;
    int ignoredChannels=0;
    unsigned char* iconPixels=stbi_load(
        iconPath.c_str(), &iconWidth, &iconHeight, &ignoredChannels, STBI_rgb_alpha);
    if (iconPixels==nullptr || iconWidth<=0 || iconHeight<=0) {
        return nullptr;
    }
    SDL_Surface* iconSurface=SDL_CreateSurface(iconWidth, iconHeight, SDL_PIXELFORMAT_RGBA32);
    if (iconSurface==nullptr) {
        stbi_image_free(iconPixels);
        return nullptr;
    }
    SDL_memcpy(
        iconSurface->pixels,
        iconPixels,
        static_cast<size_t>(iconWidth) * static_cast<size_t>(iconHeight) * 4U);
    stbi_image_free(iconPixels);
    return iconSurface;
}
// Applies the theme the theme menu picked. Follow means what the system is using,
// which is the answer on the platforms that report it. Sizes are set here too,
// because a thirteen pixel default font is too small to use: everything is laid out
// for eighteen point text.
void applyTheme(int themeMode) {
    bool useDarkTheme=true;
    if (themeMode==THEME_DARK) {
        useDarkTheme=true;
    } else if (themeMode==THEME_LIGHT) {
        useDarkTheme=false;
    } else {
        useDarkTheme=SDL_GetSystemTheme()!=SDL_SYSTEM_THEME_LIGHT;
    }
    if (useDarkTheme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }
    ImGuiStyle& interfaceStyle=ImGui::GetStyle();
    interfaceStyle.WindowPadding=ImVec2(14, 12);
    interfaceStyle.FramePadding=ImVec2(10, 7);
    interfaceStyle.ItemSpacing=ImVec2(10, 8);
    interfaceStyle.CellPadding=ImVec2(10, 7);
    interfaceStyle.ScrollbarSize=18.0f;
    interfaceStyle.GrabMinSize=14.0f;
    // A tool, not a toy: the corners are almost square, which keeps the tables and
    // the dialogs reading as instruments rather than as cards.
    interfaceStyle.WindowRounding=2.0f;
    interfaceStyle.ChildRounding=2.0f;
    interfaceStyle.FrameRounding=2.0f;
    interfaceStyle.PopupRounding=2.0f;
    interfaceStyle.TabRounding=2.0f;
    interfaceStyle.WindowBorderSize=1.0f;
    interfaceStyle.FrameBorderSize=1.0f;
    interfaceStyle.TabBarBorderSize=1.0f;
    // The one colour the design demands: a modal is fully opaque. Every dialog here
    // reports numbers a person is about to act on, and a table of numbers read
    // through a translucent panel is a table of numbers misread. The window
    // background is opaque for the same reason, and because Dear ImGui's renderer
    // clears the frame with it: a translucent clear let whatever is behind the
    // window tint the interface.
    interfaceStyle.Colors[ImGuiCol_PopupBg].w=1.0f;
    interfaceStyle.Colors[ImGuiCol_WindowBg].w=1.0f;
}
// Loads a system font at a readable size, scaled for the display. The native
// interface speaks the native typeface: Segoe UI on Windows, DejaVu on Linux, San
// Francisco on macOS. The list is ordered so the typeface of the platform the binary
// was built for is tried first, because trying a Windows path on Linux (or the
// reverse) makes the font library print a load error for every wrong candidate
// before finding the right one. When none of them is there the built in font is
// kept, because a small font beats no interface.
#if defined(_WIN32)
static constexpr int PREFERRED_SYSTEM_FONT=0;
#elif defined(__APPLE__)
static constexpr int PREFERRED_SYSTEM_FONT=2;
#else
static constexpr int PREFERRED_SYSTEM_FONT=1;
#endif
void loadInterfaceFont(float displayScale) {
    static const char* systemFonts[]={
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        nullptr};
    constexpr int systemFontCount=
        static_cast<int>(sizeof(systemFonts) / sizeof(systemFonts[0])) - 1;
    ImGuiIO& interfaceInput=ImGui::GetIO();
    for (int fontStep=0; fontStep<systemFontCount; ++fontStep) {
        const int fontIndex=(PREFERRED_SYSTEM_FONT + fontStep) % systemFontCount;
        ImFont* interfaceFont=interfaceInput.Fonts->AddFontFromFileTTF(
            systemFonts[fontIndex], 18.0f * displayScale);
        if (interfaceFont!=nullptr) {
            return;
        }
    }
}
// The path of the settings file inside the folder the platform gives this program
// for its own files, or the empty string when the platform will not name one.
string settingsFilePath() {
    char* reportedPreferencePath=SDL_GetPrefPath("agent-db-doctor", "gui");
    if (reportedPreferencePath==nullptr) {
        return string();
    }
    const string settingsPath=string(reportedPreferencePath) + SETTINGS_FILE_NAME;
    SDL_free(reportedPreferencePath);
    return settingsPath;
}
// Reads the settings file. A missing file is a first run and not a problem, so
// nothing is reported when there is none.
bool loadSettingsFile(InterfaceSettings& settings) {
    const string settingsPath=settingsFilePath();
    if (settingsPath.empty()) {
        return false;
    }
    size_t settingsSize=0;
    void* settingsData=SDL_LoadFile(settingsPath.c_str(), &settingsSize);
    if (settingsData==nullptr) {
        return false;
    }
    const string settingsText(static_cast<const char*>(settingsData), settingsSize);
    SDL_free(settingsData);
    parseSettings(settingsText, settings);
    return true;
}
// Writes the settings file, creating the folder the platform named if it is not
// there yet.
bool saveSettingsFile(const InterfaceSettings& settings) {
    const string settingsPath=settingsFilePath();
    if (settingsPath.empty()) {
        return false;
    }
    const string settingsText=serializeSettings(settings);
    return SDL_SaveFile(settingsPath.c_str(), settingsText.data(), settingsText.size());
}
// The answer a file dialogue leaves behind. SDL may deliver it from another thread,
// so the flag and the text sit behind one lock.
struct FileDialogAnswer final {
    // The lock for the two fields below.
    mutex lock;
    // True once the dialogue has answered, whether it was answered or cancelled.
    bool hasAnswer=false;
    // What was asked for, so the answer can be acted on. Only the interface thread
    // touches this field, before the dialogue is opened.
    FileDialogRequest request;
    // The chosen path, or the empty string when the dialogue was cancelled.
    string chosenPath;
};
// The callback SDL calls with the answer. A cancelled dialogue and a failed one both
// arrive as no path at all, and both mean the same thing here: do nothing.
void SDLCALL onFileDialogAnswered(void* userdata, const char* const* fileList, int filter) {
    (void)filter;
    FileDialogAnswer* answer=static_cast<FileDialogAnswer*>(userdata);
    lock_guard<mutex> answerLock(answer->lock);
    answer->hasAnswer=true;
    if (fileList!=nullptr && fileList[0]!=nullptr) {
        answer->chosenPath=fileList[0];
    }
}
// Opens the dialogue that was asked for and clears the request, so it is opened
// once. The answer arrives later, through the callback above.
void openFileDialog(SDL_Window* window, InterfaceState& ui, FileDialogAnswer& answer) {
    const FileDialogRequest request=ui.pendingFileDialog;
    ui.pendingFileDialog=FileDialogRequest();
    {
        lock_guard<mutex> answerLock(answer.lock);
        answer.request=request;
        answer.hasAnswer=false;
        answer.chosenPath.clear();
    }
    if (request.kind==FileDialogRequest::Kind::OpenDatabase) {
        static const SDL_DialogFileFilter databaseFilters[]={
            {"SQLite databases", "db;sqlite;sqlite3"},
            {"Every file", "*"}};
        SDL_ShowOpenFileDialog(
            onFileDialogAnswered,
            &answer,
            window,
            databaseFilters,
            2,
            nullptr,
            false);
        return;
    }
    static const SDL_DialogFileFilter exportFilters[]={
        {"Markdown", "md"},
        {"JSON", "json"},
        {"HTML", "html"}};
    SDL_ShowSaveFileDialog(onFileDialogAnswered, &answer, window, exportFilters, 3, nullptr);
}
// The export format a file name asks for, or the format the interface already chose
// when the name says nothing. A person who types `session.json` into the save box
// means JSON, and the extension is the only place that was said.
string exportFormatForPath(const string& chosenPath, const string& fallbackFormat) {
    const size_t dotPosition=chosenPath.find_last_of('.');
    if (dotPosition==string::npos) {
        return fallbackFormat;
    }
    string extension=chosenPath.substr(dotPosition + 1);
    for (char& extensionCharacter : extension) {
        if (extensionCharacter>='A' && extensionCharacter<='Z') {
            extensionCharacter=static_cast<char>(extensionCharacter - 'A' + 'a');
        }
    }
    if (extension=="md" || extension=="markdown") {
        return string("markdown");
    }
    if (extension=="json") {
        return string("json");
    }
    if (extension=="html" || extension=="htm") {
        return string("html");
    }
    return fallbackFormat;
}
// Acts on the answer to a file dialogue: a database to look at, or a place to write
// a conversation. The two actions are the same commands a button would run, because
// that is the whole design of this window.
void actOnFileDialogAnswer(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    FileDialogAnswer& answer) {
    FileDialogRequest request;
    string chosenPath;
    {
        lock_guard<mutex> answerLock(answer.lock);
        request=answer.request;
        chosenPath=answer.chosenPath;
    }
    if (chosenPath.empty()) {
        return;
    }
    if (request.kind==FileDialogRequest::Kind::OpenDatabase) {
        startRun(ui, state, runner, databaseDiagnoseRequest(chosenPath, false));
        return;
    }
    if (request.kind==FileDialogRequest::Kind::SaveExport) {
        CliInvocation exportInvocation;
        exportInvocation.command="export";
        exportInvocation.databasePath=request.databasePath;
        exportInvocation.sessionIdentifier=request.sessionIdentifier;
        exportInvocation.exportFormat=exportFormatForPath(chosenPath, request.exportFormat);
        exportInvocation.outputPath=chosenPath;
        RunRequest exportRequest;
        exportRequest.purpose=RunPurpose::ExportSession;
        exportRequest.invocation=exportInvocation;
        exportRequest.description="Writing the conversation out";
        exportRequest.resultTitle="The conversation was written out";
        startRun(ui, state, runner, exportRequest);
    }
}
// Draws the menu bar. The menus are the only place the window's own choices live:
// opening a file, switching tabs, and choosing a theme.
void renderMenuBar(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    bool& shouldQuit) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open database file...", nullptr, false, !runner.isRunning())) {
            ui.pendingFileDialog.kind=FileDialogRequest::Kind::OpenDatabase;
        }
        if (ImGui::MenuItem("Rescan", "Ctrl+R", false, !runner.isRunning())) {
            startRun(ui, state, runner, scanRequest());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            shouldQuit=true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Overview", nullptr, ui.currentTab==InterfaceTab::Overview)) {
            selectTab(ui, InterfaceTab::Overview);
        }
        if (ImGui::MenuItem("Databases", nullptr, ui.currentTab==InterfaceTab::Databases)) {
            selectTab(ui, InterfaceTab::Databases);
        }
        if (ImGui::MenuItem("History", nullptr, ui.currentTab==InterfaceTab::History)) {
            selectTab(ui, InterfaceTab::History);
        }
        if (ImGui::MenuItem("Service", nullptr, ui.currentTab==InterfaceTab::Service)) {
            selectTab(ui, InterfaceTab::Service);
        }
        if (ImGui::MenuItem("Settings", nullptr, ui.currentTab==InterfaceTab::Settings)) {
            selectTab(ui, InterfaceTab::Settings);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Theme")) {
        if (ImGui::MenuItem("Follow system", nullptr, state.settings.themeMode==THEME_FOLLOW_SYSTEM)) {
            state.settings.themeMode=THEME_FOLLOW_SYSTEM;
            applyTheme(state.settings.themeMode);
            ui.settingsAreDirty=true;
        }
        if (ImGui::MenuItem("Dark", nullptr, state.settings.themeMode==THEME_DARK)) {
            state.settings.themeMode=THEME_DARK;
            applyTheme(state.settings.themeMode);
            ui.settingsAreDirty=true;
        }
        if (ImGui::MenuItem("Light", nullptr, state.settings.themeMode==THEME_LIGHT)) {
            state.settings.themeMode=THEME_LIGHT;
            applyTheme(state.settings.themeMode);
            ui.settingsAreDirty=true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About agent-db-doctor")) {
            ui.aboutOpen=true;
            // The about dialogue lists the agents the tool supports, which is a fact
            // the command line owns. Asking for it is one more command, and nothing
            // is asked for while another command is in flight.
            if (state.agents.empty() && !runner.isRunning()) {
                startRun(ui, state, runner, agentsRequest());
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}
// The keyboard shortcuts, which are the two the menus advertise. They are ignored
// while a text box has the focus, so typing an R into the confirmation box does not
// rescan.
void handleShortcuts(
    InterfaceState& ui,
    GuiState& state,
    CliRunner& runner,
    bool& shouldQuit) {
    const ImGuiIO& interfaceInput=ImGui::GetIO();
    if (interfaceInput.WantTextInput || !interfaceInput.KeyCtrl) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !runner.isRunning()) {
        startRun(ui, state, runner, scanRequest());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
        shouldQuit=true;
    }
}
// Everything the window owns while it is open, so that `runGui` reads as the loop
// it is rather than as a list of cleanups.
struct InterfaceResources final {
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    SDL_Texture* aboutLogo=nullptr;
};
// Brings the window, the renderer, and the drawing layers down in the reverse of
// the order they came up.
void releaseInterface(InterfaceResources& resources) {
    if (resources.aboutLogo!=nullptr) {
        SDL_DestroyTexture(resources.aboutLogo);
        resources.aboutLogo=nullptr;
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (resources.renderer!=nullptr) {
        SDL_DestroyRenderer(resources.renderer);
        resources.renderer=nullptr;
    }
    if (resources.window!=nullptr) {
        SDL_DestroyWindow(resources.window);
        resources.window=nullptr;
    }
    SDL_Quit();
}
}
int runGui(int argumentCount, char** argumentValues) {
    // `--smoke-test` draws a fixed number of frames and exits, so every build can
    // prove that the window, the renderer, and the drawing pipeline all come up.
    // Continuous integration runs it under a virtual display on Linux and directly
    // on Windows and macOS. It starts no command and writes no settings file, so a
    // test run never touches what a person has chosen.
    bool smokeTestMode=false;
    for (int argumentIndex=1; argumentIndex<argumentCount; ++argumentIndex) {
        if (string(argumentValues[argumentIndex])=="--smoke-test") {
            smokeTestMode=true;
        }
    }
    InterfaceState ui;
    GuiState state;
    if (!smokeTestMode) {
        loadSettingsFile(state.settings);
    }
    // The settings tab edits a fixed buffer rather than a string rebuilt every
    // frame, because Dear ImGui keeps its editing state inside the buffer.
    snprintf(
        ui.backupDestinationInput,
        sizeof(ui.backupDestinationInput),
        "%s",
        state.settings.backupDestinationPath.c_str());
    ui.logPaneIsOpen=state.settings.logPaneIsOpen;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }
    InterfaceResources resources;
    resources.window=SDL_CreateWindow(
        "agent-db-doctor",
        state.settings.windowWidth,
        state.settings.windowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (resources.window==nullptr) {
        SDL_Quit();
        return 1;
    }
    // A remembered placement is applied before the window is shown, so it never
    // flashes at one position and then jumps to another. A window with no remembered
    // placement opens in the middle of the display, and a window larger than the
    // display is shrunk to fit it: a default size that fits a large monitor leaves
    // the edges of the window off a small laptop panel, where the buttons that
    // matter are.
    if (state.settings.hasWindowPlacement) {
        SDL_SetWindowPosition(resources.window, state.settings.windowX, state.settings.windowY);
    } else {
        SDL_SetWindowPosition(
            resources.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_Rect usableBounds{0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(
            SDL_GetDisplayForWindow(resources.window), &usableBounds)) {
        int fittedWidth=state.settings.windowWidth;
        int fittedHeight=state.settings.windowHeight;
        if (fittedWidth>usableBounds.w) {
            fittedWidth=usableBounds.w;
        }
        if (fittedHeight>usableBounds.h) {
            fittedHeight=usableBounds.h;
        }
        if (fittedWidth!=state.settings.windowWidth || fittedHeight!=state.settings.windowHeight) {
            SDL_SetWindowSize(resources.window, fittedWidth, fittedHeight);
        }
    }
    SDL_ShowWindow(resources.window);
    resources.renderer=SDL_CreateRenderer(resources.window, nullptr);
    if (resources.renderer==nullptr) {
        releaseInterface(resources);
        return 1;
    }
    // The display's own refresh rate paces the loop, so a window nobody is touching
    // does not burn a processor redrawing thousands of times a second.
    SDL_SetRenderVSync(resources.renderer, 1);
    if (SDL_Surface* windowIcon=loadWindowIcon()) {
        SDL_SetWindowIcon(resources.window, windowIcon);
        SDL_DestroySurface(windowIcon);
    }
    resources.aboutLogo=loadArtworkTexture(resources.renderer, "logo-512.png");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& interfaceInput=ImGui::GetIO();
    // No ini file: the window's own settings file is the only thing this program
    // remembers, and it sits in the folder the platform gives it.
    interfaceInput.IniFilename=nullptr;
    loadInterfaceFont(SDL_GetWindowDisplayScale(resources.window));
    applyTheme(state.settings.themeMode);
    ImGui_ImplSDL3_InitForSDLRenderer(resources.window, resources.renderer);
    ImGui_ImplSDLRenderer3_Init(resources.renderer);
    CliRunner runner;
    FileDialogAnswer fileDialogAnswer;
    bool fileDialogIsOpen=false;
    // The first look at every database starts as soon as the window is up, so the
    // overview has numbers in it without anybody pressing anything.
    if (!smokeTestMode) {
        startRun(ui, state, runner, scanRequest());
    }
    bool isRunning=true;
    int renderedFrameCount=0;
    while (isRunning) {
        SDL_Event currentEvent;
        while (SDL_PollEvent(&currentEvent)) {
            ImGui_ImplSDL3_ProcessEvent(&currentEvent);
            if (currentEvent.type==SDL_EVENT_QUIT) {
                isRunning=false;
            }
        }
        // A file dialogue that was asked for is opened here, because this is where
        // the window and the event pump are, and its answer is acted on as soon as
        // SDL has delivered it.
        if (ui.pendingFileDialog.kind!=FileDialogRequest::Kind::None && !fileDialogIsOpen) {
            openFileDialog(resources.window, ui, fileDialogAnswer);
            fileDialogIsOpen=true;
        }
        if (fileDialogIsOpen) {
            bool hasAnswer=false;
            {
                lock_guard<mutex> answerLock(fileDialogAnswer.lock);
                hasAnswer=fileDialogAnswer.hasAnswer;
            }
            if (hasAnswer) {
                actOnFileDialogAnswer(ui, state, runner, fileDialogAnswer);
                {
                    lock_guard<mutex> answerLock(fileDialogAnswer.lock);
                    fileDialogAnswer.hasAnswer=false;
                }
                fileDialogIsOpen=false;
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (smokeTestMode) {
            // The smoke test walks through every tab, so a mistake in any one of the
            // five screens is found by the build rather than by a user. It asks for
            // the tab directly rather than through the menu, so that no screen asks
            // for a command while the test is running: a test that starts child
            // processes is a test that fails for reasons of its own.
            const int framesPerTab=SMOKE_TEST_FRAME_COUNT / TAB_COUNT;
            ui.currentTab=static_cast<InterfaceTab>(
                (renderedFrameCount / framesPerTab) % TAB_COUNT);
            ui.tabChangeRequested=true;
        }
        handleShortcuts(ui, state, runner, isRunning);
        // Every line the child produced since the last frame goes into the state
        // before anything draws, so a frame never shows a half applied run.
        pollRunner(ui, state, runner);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        // The window fills the frame the operating system draws, so a border of its
        // own is a line across the interface rather than an edge.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin(
            "agent-db-doctor",
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_MenuBar);
        renderMenuBar(ui, state, runner, isRunning);
        const float bottomAreaHeight=bottomBarHeight(ui.logPaneIsOpen);
        float tabAreaHeight=ImGui::GetContentRegionAvail().y - bottomAreaHeight;
        if (tabAreaHeight<MINIMUM_TAB_AREA_HEIGHT) {
            tabAreaHeight=MINIMUM_TAB_AREA_HEIGHT;
        }
        if (ImGui::BeginChild("TabArea", ImVec2(0, tabAreaHeight))) {
            // A tab that was asked for starts at its top. Without this the area keeps
            // the scroll position of the tab that was showing, and the next tab opens
            // part way down with its first row cut off.
            if (ui.tabChangeRequested) {
                ImGui::SetScrollY(0.0f);
            }
            renderTabs(ui, state, runner);
        }
        ImGui::EndChild();
        ImGui::Separator();
        renderBottomBar(ui, state, runner);
        renderDialogs(ui, state, runner);
        renderAboutDialog(
            ui.aboutOpen,
            state.agents,
            resources.aboutLogo==nullptr
                ? 0ULL
                : static_cast<unsigned long long>(
                      reinterpret_cast<uintptr_t>(resources.aboutLogo)),
            string(AGENT_DB_DOCTOR_VERSION),
            string(AGENT_DB_DOCTOR_PLATFORM));
        ImGui::End();
        ImGui::PopStyleVar();
        // A setting that changed is written once the user has finished with it, so a
        // crash costs nothing that was chosen. The window's own size and place are
        // written when it closes, because they change on every drag.
        if (!smokeTestMode && ui.settingsAreDirty) {
            saveSettingsFile(state.settings);
            ui.settingsAreDirty=false;
        }
        ImGui::Render();
        SDL_SetRenderDrawColor(
            resources.renderer,
            state.settings.themeMode==THEME_LIGHT ? 240 : 15,
            state.settings.themeMode==THEME_LIGHT ? 240 : 15,
            state.settings.themeMode==THEME_LIGHT ? 245 : 20,
            255);
        SDL_RenderClear(resources.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), resources.renderer);
        SDL_RenderPresent(resources.renderer);
        renderedFrameCount++;
        if (smokeTestMode) {
            SDL_Delay(static_cast<Uint32>(SMOKE_TEST_FRAME_DELAY_MILLISECONDS));
            if (renderedFrameCount>=SMOKE_TEST_FRAME_COUNT) {
                isRunning=false;
            }
        }
    }
    // A command still running when the window closes is stopped first, because
    // waiting for a long diagnosis before quitting would look like a hung window.
    if (runner.isRunning()) {
        runner.cancel();
    }
    runner.join();
    if (!smokeTestMode) {
        SDL_GetWindowPosition(resources.window, &state.settings.windowX, &state.settings.windowY);
        SDL_GetWindowSize(resources.window, &state.settings.windowWidth, &state.settings.windowHeight);
        state.settings.hasWindowPlacement=true;
        state.settings.logPaneIsOpen=ui.logPaneIsOpen;
        saveSettingsFile(state.settings);
    }
    releaseInterface(resources);
    return 0;
}
#if defined(_WIN32)
// Windows starts a graphical program through WinMain, and a program built as a
// window rather than a console program has no main. Both entry points call the same
// function, so there is one loop and one place where the arguments are read.
// The arguments come from the C runtime's own copies, because the raw command line
// is one unsplit string and splitting it by hand is how a path with a space in it
// becomes two arguments.
int WINAPI WinMain(
    HINSTANCE instanceHandle,
    HINSTANCE previousInstanceHandle,
    LPSTR commandLine,
    int showCommand) {
    (void)instanceHandle;
    (void)previousInstanceHandle;
    (void)commandLine;
    (void)showCommand;
    return runGui(__argc, __argv);
}
#else
int main(int argumentCount, char** argumentValues) {
    return runGui(argumentCount, argumentValues);
}
#endif
