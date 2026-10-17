// The graphical interface: window, menus, dashboard, and the main loop.
//
// This file owns the SDL3 window and the Dear ImGui context. It renders one
// full screen window: a menu bar, the dashboard screen, and a status bar. A
// background worker scans the agents (see BackgroundScan.cpp) while this loop
// keeps drawing at full frame rate. Long destructive work never runs here
// either: it runs on a worker thread and publishes through the progress sink,
// which the action screens of the next change read once per frame.
// Provides `uintptr_t`, the integer type an ImGui texture handle travels through.
#include <cstdint>
// Provides `string`, used for the asset paths beside the binary.
#include <string>
// Provides SDL3: window, renderer, events, and the system theme query.
#include <SDL3/SDL.h>
// Provides Dear ImGui: the immediate mode widgets of every screen.
#include <imgui.h>
// Provides the SDL3 platform layer for Dear ImGui.
#include <imgui_impl_sdl3.h>
// Provides the SDL_Renderer drawing layer for Dear ImGui.
#include <imgui_impl_sdlrenderer3.h>
// Provides the PNG decoder for the window icon and the about logo.
#include <stb_image.h>
// Provides the shared state between this loop and the background worker.
#include "GuiState.hpp"
// Provides the dashboard screen: agents, databases, detail, and the log.
#include "DashboardScreen.hpp"
// Provides the action center: worker, activity bar, and dialogues.
#include "ActionScreens.hpp"
// Provides the registry, which names every supported agent for the browser.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
// The tool version, stamped by CMake so `--version` and the title bar agree.
#ifndef AGENT_DB_DOCTOR_VERSION
#define AGENT_DB_DOCTOR_VERSION "1.0.0"
#endif
// The directory the artwork was copied beside the binary at build time.
#ifndef AGENT_DB_DOCTOR_ASSET_DIR
#define AGENT_DB_DOCTOR_ASSET_DIR "."
#endif
using namespace AgentDbDoctor::Gui;
using namespace AgentDbDoctor::Agents;
namespace {
// What the main area shows: the dashboard, or the service view.
enum class GuiView {
    Dashboard,
    Service
};
// The folder the artwork is found in: the folder holding the running binary,
// which is where the build copies the PNGs and where a package keeps them
// beside the program. The compiled-in location is only a fallback for the
// case where the platform refuses to report the binary's folder, because a
// path baked in at build time points at the builder's machine, not the
// user's. SDL_GetBasePath returns a path that already ends with a separator,
// and the buffer belongs to SDL, so it is copied and not freed.
std::string artworkDirectoryPath() {
    std::string basePath;
    const char* reportedBasePath = SDL_GetBasePath();
    if (reportedBasePath != nullptr) {
        basePath = reportedBasePath;
    }
    if (basePath.empty()) {
        basePath = AGENT_DB_DOCTOR_ASSET_DIR;
        if (!basePath.empty() && basePath.back() != '/' && basePath.back() != '\\') {
            basePath += "/";
        }
    }
    return basePath;
}
// Loads a PNG beside the binary into an SDL texture, or null when it is
// missing. Missing artwork is never an error: the interface simply shows one
// fewer picture, because pictures must never stop the tool from working.
SDL_Texture* loadArtworkTexture(SDL_Renderer* renderer, const char* fileName) {
    const std::string artworkPath = artworkDirectoryPath() + fileName;
    int artworkWidth = 0;
    int artworkHeight = 0;
    int ignoredChannels = 0;
    unsigned char* artworkPixels = stbi_load(
        artworkPath.c_str(), &artworkWidth, &artworkHeight, &ignoredChannels, STBI_rgb_alpha);
    if (artworkPixels == nullptr || artworkWidth <= 0 || artworkHeight <= 0) {
        return nullptr;
    }
    SDL_Surface* artworkSurface =
        SDL_CreateSurfaceFrom(artworkWidth, artworkHeight, SDL_PIXELFORMAT_RGBA32, artworkPixels, artworkWidth * 4);
    if (artworkSurface == nullptr) {
        stbi_image_free(artworkPixels);
        return nullptr;
    }
    SDL_Texture* artworkTexture = SDL_CreateTextureFromSurface(renderer, artworkSurface);
    SDL_DestroySurface(artworkSurface);
    stbi_image_free(artworkPixels);
    return artworkTexture;
}
// Loads the window icon, or null when it is missing, for the same reason.
SDL_Surface* loadWindowIcon() {
    const std::string iconPath = artworkDirectoryPath() + "favicon-64.png";
    int iconWidth = 0;
    int iconHeight = 0;
    int ignoredChannels = 0;
    unsigned char* iconPixels =
        stbi_load(iconPath.c_str(), &iconWidth, &iconHeight, &ignoredChannels, STBI_rgb_alpha);
    if (iconPixels == nullptr || iconWidth <= 0 || iconHeight <= 0) {
        return nullptr;
    }
    SDL_Surface* iconSurface = SDL_CreateSurface(iconWidth, iconHeight, SDL_PIXELFORMAT_RGBA32);
    if (iconSurface == nullptr) {
        stbi_image_free(iconPixels);
        return nullptr;
    }
    SDL_memcpy(iconSurface->pixels, iconPixels, static_cast<size_t>(iconWidth * iconHeight * 4));
    stbi_image_free(iconPixels);
    return iconSurface;
}
// Applies the theme: dark for a dark system, light otherwise. The menu
// reapplies it on every pick, so the stored preference is a starting point,
// never a lock. Sizes are set here too, because a thirteen pixel default
// font is too small to use: everything is laid out for eighteen point text.
void applyTheme(bool useDarkTheme) {
    if (useDarkTheme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }
    ImGuiStyle& interfaceStyle = ImGui::GetStyle();
    interfaceStyle.WindowPadding = ImVec2(12, 12);
    interfaceStyle.FramePadding = ImVec2(10, 7);
    interfaceStyle.ItemSpacing = ImVec2(10, 8);
    interfaceStyle.CellPadding = ImVec2(10, 7);
    interfaceStyle.ScrollbarSize = 18.0f;
    interfaceStyle.GrabMinSize = 14.0f;
}
// Loads a system font at a readable size, scaled for the display. The native
// interface speaks the native typeface: Segoe UI on Windows, DejaVu on Linux,
// San Francisco on macOS. The list is ordered so the typeface of the platform
// the binary was built for is tried first, because trying a Windows path on
// Linux (or the reverse) made ImGui print a load error for every wrong
// candidate before finding the right one. Both known names of the Apple font
// are listed because the newer systems ship `SFNS.ttf` and the older ones
// `SFNSDisplay.ttf`. When none of them is there the built in font is kept,
// because a small font beats no interface.
#if defined(_WIN32)
static constexpr int preferredSystemFont=0;
#elif defined(__APPLE__)
static constexpr int preferredSystemFont=2;
#else
static constexpr int preferredSystemFont=1;
#endif
void loadInterfaceFont(float displayScale) {
    static const char* systemFonts[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSDisplay.ttf",
        nullptr};
    constexpr int systemFontCount=static_cast<int>(sizeof(systemFonts)/sizeof(systemFonts[0]))-1;
    ImGuiIO& interfaceInput = ImGui::GetIO();
    for (int fontStep = 0; fontStep < systemFontCount; ++fontStep) {
        const int fontIndex = (preferredSystemFont + fontStep) % systemFontCount;
        ImFont* interfaceFont = interfaceInput.Fonts->AddFontFromFileTTF(
            systemFonts[fontIndex], 18.0f * displayScale);
        if (interfaceFont != nullptr) {
            return;
        }
    }
}
} // namespace
int main(int argumentCount, char** argumentValues) {
    // `--smoke-test` draws a fixed number of frames and exits, so every build can
    // prove that the window, the renderer, and the drawing pipeline all come up.
    // Continuous integration runs it under a virtual display on Linux and directly
    // on Windows and macOS.
    bool smokeTestMode=false;
    for (int argumentIndex=1; argumentIndex<argumentCount; ++argumentIndex) {
        if (std::string(argumentValues[argumentIndex])=="--smoke-test") {
            smokeTestMode=true;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "agent-db-doctor", 1360, 860, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // The display's own refresh rate paces the loop, so a window nobody is
    // touching does not burn a processor redrawing thousands of times a second.
    SDL_SetRenderVSync(renderer, 1);
    if (SDL_Surface* windowIcon = loadWindowIcon()) {
        SDL_SetWindowIcon(window, windowIcon);
        SDL_DestroySurface(windowIcon);
    }
    SDL_Texture* aboutLogo = loadArtworkTexture(renderer, "logo-512.png");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& interfaceInput = ImGui::GetIO();
    interfaceInput.IniFilename = nullptr;
    loadInterfaceFont(SDL_GetWindowDisplayScale(window));
    bool useDarkTheme = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
    applyTheme(useDarkTheme);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    AgentRegistry registry;
    const std::vector<AgentDescriptor> agents = registry.allAgents();
    GuiState state;
    requestBackgroundScan(state);
    ActionRunner actionRunner;
    ActionUiState actionUi;
    GuiView currentView = GuiView::Dashboard;
    bool isRunning = true;
    bool showAboutWindow = false;
    int renderedFrameCount=0;
    while (isRunning) {
        SDL_Event currentEvent;
        while (SDL_PollEvent(&currentEvent)) {
            ImGui_ImplSDL3_ProcessEvent(&currentEvent);
            if (currentEvent.type == SDL_EVENT_QUIT) {
                isRunning = false;
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin(
            "agent-db-doctor",
            nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                // Disabled while a scan runs, because a second request would be
                // dropped, and a menu entry that does nothing reads as broken.
                if (ImGui::MenuItem(
                        "Rescan databases", nullptr, false, !state.scanRunning.load())) {
                    requestBackgroundScan(state);
                }
                if (ImGui::MenuItem("Quit")) {
                    isRunning = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Theme")) {
                if (ImGui::MenuItem("Follow system")) {
                    useDarkTheme = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
                    applyTheme(useDarkTheme);
                }
                if (ImGui::MenuItem("Dark")) {
                    useDarkTheme = true;
                    applyTheme(useDarkTheme);
                }
                if (ImGui::MenuItem("Light")) {
                    useDarkTheme = false;
                    applyTheme(useDarkTheme);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About agent-db-doctor")) {
                    showAboutWindow = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem(
                        "Dashboard", nullptr, currentView == GuiView::Dashboard)) {
                    currentView = GuiView::Dashboard;
                }
                if (ImGui::MenuItem(
                        "Service", nullptr, currentView == GuiView::Service)) {
                    currentView = GuiView::Service;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        pollActionResult(actionRunner, actionUi);
        if (currentView == GuiView::Dashboard) {
            renderDashboard(state, agents, actionRunner, actionUi);
        } else {
            renderServiceView(actionRunner, actionUi);
        }
        renderActivityBar(actionRunner);
        renderActionDialogues(actionRunner, actionUi);
        if (showAboutWindow) {
            ImGui::OpenPopup("About agent-db-doctor");
            showAboutWindow = false;
        }
        if (ImGui::BeginPopupModal("About agent-db-doctor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (aboutLogo != nullptr) {
                ImGui::Image(
                    static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(aboutLogo)),
                    ImVec2(160, 160));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::Text("agent-db-doctor %s", AGENT_DB_DOCTOR_VERSION);
            ImGui::TextWrapped("A doctor for AI coding agent databases.");
            ImGui::Text("%d agents supported.", static_cast<int>(agents.size()));
            ImGui::Text("Diagnose, prune, compact, recover, and monitor.");
            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndGroup();
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(
            renderer,
            useDarkTheme ? 15 : 240,
            useDarkTheme ? 15 : 240,
            useDarkTheme ? 20 : 245,
            255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        renderedFrameCount++;
        if (smokeTestMode) {
            // A fixed pace and a fixed frame count make the smoke run last about
            // two seconds, which also gives the background worker time to start.
            SDL_Delay(16);
            if (renderedFrameCount>=120) {
                isRunning = false;
            }
        }
    }
    joinBackgroundScan(state);
    if (aboutLogo != nullptr) {
        SDL_DestroyTexture(aboutLogo);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
