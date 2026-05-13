#include "volt-ui/VoltApp.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

namespace volt {

static constexpr int BTN_W = 46;
static constexpr int BTN_COUNT = 3;

Uint32 App::GetWindowFlags() const {
    Uint32 flags = 0;
    if (config_.resizable || config_.use_topbar) flags |= SDL_WINDOW_RESIZABLE;
    if (config_.use_topbar) flags |= SDL_WINDOW_BORDERLESS;
    return flags;
}

void App::OnWindowCreated() {
    if (config_.use_topbar) {
        SDL_SetWindowHitTest(window_, HitTestCallback, this);
    }
}

bool App::OnPlatformEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_WINDOW_MAXIMIZED) {
        is_maximized_ = true;
        return true;
    }
    if (event.type == SDL_EVENT_WINDOW_RESTORED) {
        is_maximized_ = false;
        return true;
    }

    if (config_.use_topbar &&
        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.button.clicks >= 2 &&
        event.button.y < config_.topbar_height &&
        event.button.x < config_.width - BTN_W * BTN_COUNT) {
        ToggleMaximize();
        return true;
    }

    return false;
}

void App::OnPreRender() {
    if (config_.use_topbar) {
        DrawTopbar();
    }
}

void App::ToggleMaximize() {
    if (is_maximized_) {
        SDL_RestoreWindow(window_);
    } else {
        SDL_MaximizeWindow(window_);
    }
}

SDL_HitTestResult App::HitTestCallback(SDL_Window* win, const SDL_Point* area, void* data) {
    auto* app = static_cast<App*>(data);
    int width, height;
    SDL_GetWindowSize(win, &width, &height);

    int border = app->config_.resize_border;
    float topbar_h = app->config_.use_topbar ? app->config_.topbar_height : 0;

    if (app->config_.resizable) {
        if (area->x < border && area->y < border)
            return SDL_HITTEST_RESIZE_TOPLEFT;
        if (area->x > width - border && area->y < border)
            return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (area->x > width - border && area->y > height - border)
            return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (area->x < border && area->y > height - border)
            return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (area->x < border)
            return SDL_HITTEST_RESIZE_LEFT;
        if (area->y < border)
            return SDL_HITTEST_RESIZE_TOP;
        if (area->x > width - border)
            return SDL_HITTEST_RESIZE_RIGHT;
        if (area->y > height - border)
            return SDL_HITTEST_RESIZE_BOTTOM;
    }

    if (topbar_h > 0 && area->y < topbar_h) {
        int btn_area = BTN_W * BTN_COUNT;
        if (area->x < width - btn_area && area->y >= border) {
            return SDL_HITTEST_DRAGGABLE;
        }
    }

    return SDL_HITTEST_NORMAL;
}

void App::DrawTopbar() {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float w = display.x;
    float h = config_.topbar_height;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("##Topbar", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus);

    auto* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        IM_COL32(28, 28, 32, 255));
    draw->AddLine(ImVec2(pos.x, pos.y + size.y),
        ImVec2(pos.x + size.x, pos.y + size.y),
        IM_COL32(50, 50, 55, 255), 1);

    ImGui::SetCursorPos(ImVec2(12, (h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextUnformatted(config_.title.c_str());

    float btn_x = w - BTN_W;
    auto draw_button = [&](const char* id, ImU32 hover_color, auto draw_icon) {
        ImGui::SetCursorPos(ImVec2(btn_x, 0));
        ImGui::InvisibleButton(id, ImVec2((float)BTN_W, h));
        bool hovered = ImGui::IsItemHovered();
        ImVec2 bmin = ImGui::GetItemRectMin();
        ImVec2 bmax = ImGui::GetItemRectMax();
        if (hovered) {
            draw->AddRectFilled(bmin, bmax, hover_color);
        }
        draw_icon(bmin, bmax);
        btn_x -= BTN_W;
        return ImGui::IsItemActivated();
    };

    float icon_w = 14.0f;
    float px = (BTN_W - icon_w) * 0.5f;
    float py = (h - icon_w) * 0.5f;

    auto draw_close = [&](ImVec2 bmin, ImVec2 bmax) {
        draw->AddLine(ImVec2(bmin.x + px, bmin.y + py),
                      ImVec2(bmax.x - px, bmax.y - py), IM_COL32_WHITE, 2.0f);
        draw->AddLine(ImVec2(bmax.x - px, bmin.y + py),
                      ImVec2(bmin.x + px, bmax.y - py), IM_COL32_WHITE, 2.0f);
    };

    auto draw_maximize = [&](ImVec2 bmin, ImVec2 bmax) {
        if (is_maximized_) {
            float s = icon_w * 0.2f;
            draw->AddRect(ImVec2(bmin.x + px - s, bmin.y + py + s),
                          ImVec2(bmax.x - px + s, bmax.y - py - s),
                          IM_COL32_WHITE, 0, 0, 2.0f);
            draw->AddRectFilled(
                ImVec2(bmin.x + px - s + 1, bmin.y + py + s + 1),
                ImVec2(bmax.x - px + s - 1, bmax.y - py - s - 1),
                IM_COL32(28, 28, 32, 255));
            draw->AddRect(ImVec2(bmin.x + px + s, bmin.y + py - s),
                          ImVec2(bmax.x - px - s, bmax.y - py + s),
                          IM_COL32_WHITE, 0, 0, 2.0f);
        } else {
            draw->AddRect(ImVec2(bmin.x + px, bmin.y + py),
                          ImVec2(bmax.x - px, bmax.y - py),
                          IM_COL32_WHITE, 0, 0, 2.0f);
        }
    };

    auto draw_minimize = [&](ImVec2 bmin, ImVec2 bmax) {
        float cy = (bmin.y + bmax.y) * 0.5f;
        draw->AddLine(ImVec2(bmin.x + px, cy),
                      ImVec2(bmax.x - px, cy), IM_COL32_WHITE, 2.0f);
    };

    if (draw_button("##close", IM_COL32(200, 50, 50, 200), draw_close)) {
        running_ = false;
    }
    if (draw_button("##maximize", IM_COL32(60, 60, 65, 200), draw_maximize)) {
        ToggleMaximize();
    }
    if (draw_button("##minimize", IM_COL32(60, 60, 65, 200), draw_minimize)) {
        SDL_MinimizeWindow(window_);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

} // namespace volt
