#include <volt-ui/VoltApp.h>
#include <imgui.h>

class BasicApp : public volt::App {
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

        if (GetConfig().use_topbar) {
            ImGui::SetNextWindowPos(ImVec2(0, GetTopbarHeight()), ImGuiCond_Once);
            ImGui::SetNextWindowSize(ImVec2(GetConfig().width, GetConfig().height - GetTopbarHeight()), ImGuiCond_Once);
        }

        ImGui::Begin("Volt UI");

        ImGui::Text("Frame: %llu", (unsigned long long)GetFrameCount());
        ImGui::Text("FPS:   %.1f", GetFrameRate());
        ImGui::Text("DT:    %.2f ms", GetDeltaTime() * 1000.0f);

        ImGui::Separator();

        ImGui::Checkbox("ImGui Demo", &show_demo_);
        ImGui::Text("Topbar: %s", GetConfig().use_topbar ? "ON" : "OFF");

        if (ImGui::Button("Quit")) {
            Quit();
        }

        ImGui::End();
    }

    void OnEvent(const SDL_Event& event) override {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            Quit();
        }
    }

private:
    bool show_demo_ = false;
};

int main() {
    volt::AppConfig cfg;
    cfg.title = "Volt UI Demo";
    cfg.width = 1280;
    cfg.height = 720;
    cfg.use_topbar = true;
    BasicApp app(cfg);
    return app.Run();
}
