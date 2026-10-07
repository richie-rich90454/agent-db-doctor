// The graphical interface: window, renderer, and the main loop.
//
// This file owns the SDL3 window and the Dear ImGui context. It renders one
// full screen window and drives every screen of the tool from it. Long work
// never runs here: commands run on a worker thread and publish through the
// progress sink, and this loop only polls the numbers once per frame, so the
// interface stays at full frame rate while a prune runs.
#include <string>
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
// Provides the PNG decoder for the window icon.
#include <stb_image.h>
// The tool version, stamped by CMake so `--version` and the title bar agree.
#ifndef AGENT_DB_DOCTOR_VERSION
#define AGENT_DB_DOCTOR_VERSION "1.0.0"
#endif
// The directory the icon was copied beside the binary at build time.
#ifndef AGENT_DB_DOCTOR_ASSET_DIR
#define AGENT_DB_DOCTOR_ASSET_DIR "."
#endif
namespace {
// Loads the window icon, or null when it is missing. A missing icon is not an
// error: the window simply shows the default, because artwork must never stop
// the tool from working.
SDL_Surface* loadWindowIcon() {
    std::string iconPath = std::string(AGENT_DB_DOCTOR_ASSET_DIR) + "/favicon-64.png";
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
// Applies the theme: dark for a dark system, light otherwise. The choice is
// reapplied whenever the user picks from the menu, so the stored preference
// is only a starting point, never a lock.
void applyTheme(bool useDarkTheme) {
    if (useDarkTheme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }
}
} // namespace
int main(int /*argumentCount*/, char** /*argumentValues*/) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "agent-db-doctor", 1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    if (SDL_Surface* windowIcon = loadWindowIcon()) {
        SDL_SetWindowIcon(window, windowIcon);
        SDL_DestroySurface(windowIcon);
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& interfaceInput = ImGui::GetIO();
    interfaceInput.IniFilename = nullptr;
    bool useDarkTheme = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
    applyTheme(useDarkTheme);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    bool isRunning = true;
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
            if (ImGui::BeginMenu("Theme")) {
                bool followSystemTheme = false;
                if (ImGui::MenuItem("Follow system")) {
                    followSystemTheme = true;
                }
                if (ImGui::MenuItem("Dark")) {
                    useDarkTheme = true;
                }
                if (ImGui::MenuItem("Light")) {
                    useDarkTheme = false;
                }
                if (followSystemTheme) {
                    useDarkTheme = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
                }
                applyTheme(useDarkTheme);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        ImGui::Text("agent-db-doctor %s", AGENT_DB_DOCTOR_VERSION);
        ImGui::Text("The graphical interface. The dashboard arrives next.");
        if (ImGui::Button("Quit")) {
            isRunning = false;
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
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
