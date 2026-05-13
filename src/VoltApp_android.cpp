#include "volt-ui/VoltApp.h"

namespace volt {

Uint32 App::GetWindowFlags() const {
    return 0;
}

void App::OnWindowCreated() {
}

bool App::OnPlatformEvent(const SDL_Event& event) {
    (void)event;
    return false;
}

void App::OnPreRender() {
}

void App::DrawTopbar() {
}

void App::ToggleMaximize() {
}

SDL_HitTestResult App::HitTestCallback(SDL_Window* win, const SDL_Point* area, void* data) {
    (void)win;
    (void)area;
    (void)data;
    return SDL_HITTEST_NORMAL;
}

} // namespace volt
