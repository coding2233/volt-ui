#include "volt-ui/VoltApp.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

namespace volt {

App::App(const AppConfig& config) : config_(config) {}

App::~App() {
    ShutdownImGui();
    ShutdownSDL();
}

int App::Run() {
    InitSDL();
    InitImGui();
    OnCreate();

    running_ = true;
    last_ticks_ = SDL_GetTicks();

    while (running_) {
        uint64_t now = SDL_GetTicks();
        delta_time_ = (now - last_ticks_) / 1000.0f;
        if (delta_time_ < 0.0f) delta_time_ = 0.0f;
        if (delta_time_ > 0.1f) delta_time_ = 0.1f;
        last_ticks_ = now;

        ProcessEvents();
        BeginFrame();
        OnUpdate(delta_time_);
        OnRender();
        EndFrame();
        frame_count_++;
    }

    OnDestroy();
    return 0;
}

void App::Quit() {
    running_ = false;
}

void App::InitSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return;
    }

    Uint32 flags = 0;
    if (config_.resizable) flags |= SDL_WINDOW_RESIZABLE;

    window_ = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height, flags);
    if (!window_) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        return;
    }

    SDL_SetRenderVSync(renderer_, config_.vsync ? 1 : 0);
}

void App::ShutdownSDL() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void App::InitImGui() {
    IMGUI_CHECKVERSION();
    imguictx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imguictx_);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (config_.scale > 0.0f && config_.scale != 1.0f) {
        ImGui::GetStyle().ScaleAllSizes(config_.scale);
    }

    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
}

void App::ShutdownImGui() {
    if (imguictx_) {
        ImGui::SetCurrentContext(imguictx_);
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(imguictx_);
        imguictx_ = nullptr;
    }
}

void App::BeginFrame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void App::EndFrame() {
    ImGui::Render();

    SDL_SetRenderDrawColor(renderer_,
        Uint8(clear_color_.r * 255.0f),
        Uint8(clear_color_.g * 255.0f),
        Uint8(clear_color_.b * 255.0f),
        Uint8(clear_color_.a * 255.0f));
    SDL_RenderClear(renderer_);

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

void App::ProcessEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        OnEvent(event);

        if (event.type == SDL_EVENT_QUIT) {
            running_ = false;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            SDL_GetWindowID(window_) == event.window.windowID) {
            running_ = false;
        }
    }
}

} // namespace volt
