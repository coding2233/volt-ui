#pragma once

#include <SDL3/SDL.h>
#include <atomic>
#include <string>

#include "volt-ui/Types.h"
#include "log.h"

struct ImGuiContext;

namespace volt {
struct WinFileDropTarget;
}

namespace volt {

struct AppConfig {
    std::string title = "Volt UI";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool resizable = true;
    float scale = 1.0f;

    bool use_topbar = false;
    float topbar_height = 36.0f;
    int resize_border = 6;
};

class App {
    friend struct WinFileDropTarget;
public:
    explicit App(const AppConfig& config = {});
    virtual ~App();

    int Run();
    void Quit();

    SDL_Window* GetWindow() const { return window_; }
    SDL_Renderer* GetRenderer() const { return renderer_; }
    float GetDeltaTime() const { return delta_time_; }
    float GetFrameRate() const { return delta_time_ > 0.0f ? 1.0f / delta_time_ : 0.0f; }
    uint64_t GetFrameCount() const { return frame_count_; }
    const AppConfig& GetConfig() const { return config_; }

    void SetClearColor(const Color& color) { clear_color_ = color; }
    const Color& GetClearColor() const { return clear_color_; }

    void ToggleMaximize();
    bool IsMaximized() const { return is_maximized_; }
    float GetTopbarHeight() const { return config_.use_topbar ? config_.topbar_height : 0.0f; }

protected:
    virtual void OnCreate() {}
    virtual void OnUpdate(float dt) { (void)dt; }
    virtual void OnRender() {}
    virtual void OnEvent(const SDL_Event& event) { (void)event; }
    virtual void OnDestroy() {}

    // OS file drag onto the window. Paths are UTF-8.
    virtual void OnFileDropped(const std::string& utf8Path) { (void)utf8Path; }
    virtual void OnFileDragHover(bool active) { (void)active; }
    bool IsExternalFileDrag() const { return external_file_drag_.load(std::memory_order_relaxed); }
    void SetExternalFileDrag(bool active) { external_file_drag_.store(active, std::memory_order_relaxed); }

    virtual void DrawTopbar();

private:
    bool InitSDL();
    bool InitImGui();
    void ShutdownSDL();
    void ShutdownImGui();
    void BeginFrame();
    void EndFrame();
    void ProcessEvents();
    void InstallFileDrop();
    void RemoveFileDrop();

    static SDL_HitTestResult HitTestCallback(SDL_Window* win,
                                             const SDL_Point* area,
                                             void* data);

    AppConfig config_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    ImGuiContext* imguictx_ = nullptr;
    bool running_ = false;
    float delta_time_ = 0.0f;
    uint64_t frame_count_ = 0;
    uint64_t last_ticks_ = 0;
    Color clear_color_{0.1f, 0.1f, 0.12f, 1.0f};
    bool is_maximized_ = false;
    void* drop_target_ = nullptr;
    bool ole_inited_by_us_ = false;
    std::atomic<bool> external_file_drag_{false};
};

} // namespace volt
