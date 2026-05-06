#include "log.h"
#include <volt-ui/VoltApp.h>
#include <volt-ui/ScriptHost.h>
#include <imgui.h>


class ScriptingApp : public volt::App {
public:
    ScriptingApp(const volt::AppConfig& cfg, volt::ScriptHost* host)
        : App(cfg), script_host_(host) {}

protected:
    void OnCreate() override {
        SetClearColor({0.12f, 0.12f, 0.15f, 1.0f});
        if (script_host_) script_host_->OnCreate();
    }

    void OnUpdate(float dt) override {
        if (script_host_) script_host_->OnUpdate(dt);
    }

    void OnRender() override {
        ImGui::Begin("C++ Host");
        ImGui::Text("FPS: %.1f", GetFrameRate());
        ImGui::Text("Topbar: %s", GetConfig().use_topbar ? "ON" : "OFF");
        ImGui::End();

        ImGui::Begin("C# Script");
        if (script_host_) script_host_->OnRender();
        ImGui::End();
    }

    void OnDestroy() override {
        if (script_host_) {
            script_host_->OnDestroy();
            script_host_->Shutdown();
        }
    }

    void OnEvent(const SDL_Event& event) override {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            Quit();
        }
    }

private:
    volt::ScriptHost* script_host_;
};

int main() {
    volt::ScriptHostConfig script_cfg;
    script_cfg.assembly = "AppMain";
    script_cfg.entry_class = "AppMain";
    script_cfg.bcl_path = "dotnetframework4.x";

    volt::ScriptHost host;
    auto host_init_result = host.Initialize(script_cfg);
    log_debug("host_init_result: %d", host_init_result);

    volt::AppConfig cfg;
    cfg.title = "Volt UI + LeanCLR";
    cfg.width = 1280;
    cfg.height = 720;
    cfg.use_topbar = true;

    ScriptingApp app(cfg, &host);
    return app.Run();
}
