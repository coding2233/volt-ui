#include <volt-ui/VoltApp.h>
#include <imgui.h>

class AndroidApp : public volt::App {
public:
    using volt::App::App;

protected:
    void OnCreate() override {
        SetClearColor({0.12f, 0.12f, 0.15f, 1.0f});
    }

    void OnRender() override {
        if (show_demo_) {
            ImGui::ShowDemoWindow(&show_demo_);
        }

        ImGui::Begin("Volt UI - Android");
        ImGui::Text("Frame: %llu", (unsigned long long)GetFrameCount());
        ImGui::Text("FPS:   %.1f", GetFrameRate());
        ImGui::Text("DT:    %.2f ms", GetDeltaTime() * 1000.0f);
        ImGui::Separator();
        ImGui::Checkbox("ImGui Demo", &show_demo_);

        if (ImGui::Button("Quit")) {
            Quit();
        }

        ImGui::End();
    }

    void OnEvent(const SDL_Event& event) override {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_AC_BACK) {
            Quit();
        }
    }

private:
    bool show_demo_ = false;
};

int main() {
    volt::AppConfig cfg;
    cfg.title = "Volt UI";
    cfg.width = 0;
    cfg.height = 0;
    cfg.vsync = true;
    cfg.resizable = false;
    cfg.use_topbar = false;
    cfg.scale = 2.0f;

    AndroidApp app(cfg);
    return app.Run();
}
