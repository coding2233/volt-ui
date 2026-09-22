#include "volt-ui/VoltApp.h"
#include "log.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <SDL3/SDL_properties.h>
#endif

namespace volt {

static constexpr int BTN_W = 46;
static constexpr int BTN_COUNT = 3;

volt::App::App(const AppConfig& config) : config_(config) {}

App::~App() {
    ShutdownImGui();
    ShutdownSDL();
}

int App::Run() {
    log_info("App::Run: InitSDL");
    if (!InitSDL()) {
        log_error("Failed to initialize SDL");
        return -1;
    }
    log_info("App::Run: InitImGui");
    if (!InitImGui()) {
        log_error("Failed to initialize ImGui");
        ShutdownSDL();
        return -1;
    }
    log_info("App::Run: OnCreate");
    OnCreate();
    log_info("App::Run: OnCreate completed");

    running_ = true;
    last_ticks_ = SDL_GetTicks();
    log_info("App::Run: Entering main loop");

    while (running_) {
        uint64_t now = SDL_GetTicks();
        delta_time_ = (now - last_ticks_) / 1000.0f;
        if (delta_time_ < 0.0f) delta_time_ = 0.0f;
        if (delta_time_ > 0.1f) delta_time_ = 0.1f;
        last_ticks_ = now;

        // log_info("App::Run: ProcessEvents");
        ProcessEvents();
        // log_info("App::Run: BeginFrame");
        BeginFrame();
        // log_info("App::Run: OnUpdate");
        OnUpdate(delta_time_);
        // log_info("App::Run: OnRender");
        OnRender();
        // log_info("App::Run: EndFrame");
        EndFrame();
        // log_info("App::Run: Frame end");
        frame_count_++;
    }

    log_info("App::Run: OnDestroy");
    OnDestroy();
    return 0;
}

void App::Quit() {
    running_ = false;
}

void App::ToggleMaximize() {
    if (is_maximized_) {
        SDL_RestoreWindow(window_);
    } else {
        SDL_MaximizeWindow(window_);
    }
}

bool App::InitSDL() {
    log_info("App::InitSDL: Start");
    bool sdl_ret = SDL_Init(SDL_INIT_VIDEO);
    log_info("App::InitSDL: SDL_Init returned %d", sdl_ret);
    if (!sdl_ret) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }
    log_info("App::InitSDL: SDL_Init succeeded");

    {
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        SDL_Rect displayBounds;
        if (display && SDL_GetDisplayBounds(display, &displayBounds)) {
            config_.width = int(displayBounds.w * 0.8f);
            config_.height = int(displayBounds.h * 0.8f);
        }
    }

    Uint32 flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config_.resizable || config_.use_topbar) flags |= SDL_WINDOW_RESIZABLE;
    if (config_.use_topbar) flags |= SDL_WINDOW_BORDERLESS;

    window_ = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height, flags);
    if (!window_) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return false;
    }
    log_info("App::InitSDL: Window created: %p", window_);

    if (config_.use_topbar) {
        SDL_SetWindowHitTest(window_, HitTestCallback, this);
        log_info("App::InitSDL: HitTest set");
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        return false;
    }
    log_info("App::InitSDL: Renderer created: %p", renderer_);

    SDL_SetRenderVSync(renderer_, config_.vsync ? 1 : 0);
    log_info("App::InitSDL: VSync set");
    InstallFileDrop();
    return true;
}

void App::ShutdownSDL() {
    RemoveFileDrop();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool App::InitImGui() {
    IMGUI_CHECKVERSION();
    imguictx_ = ImGui::CreateContext();
    if (!imguictx_) {
        log_error("Failed to create ImGui context");
        return false;
    }
    ImGui::SetCurrentContext(imguictx_);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    {
        float detected = SDL_GetWindowDisplayScale(window_);
        if (detected <= 0.0f)
            detected = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window_));
        if (detected >= 1.25f)
            config_.scale = detected;
    }

    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
    return true;
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
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui::NewFrame();
}

void App::EndFrame() {
    if (config_.use_topbar) {
        DrawTopbar();
    }

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

        if (event.type == SDL_EVENT_WINDOW_MAXIMIZED) {
            is_maximized_ = true;
        }
        if (event.type == SDL_EVENT_WINDOW_RESTORED) {
            is_maximized_ = false;
        }

        if (config_.use_topbar &&
            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.clicks >= 2 &&
            event.button.y < config_.topbar_height &&
            event.button.x < config_.width - BTN_W * BTN_COUNT) {
            ToggleMaximize();
        }
    }
}

#ifdef _WIN32

namespace {

std::string WideToUtf8(const wchar_t* text, int chars)
{
    if (!text || chars <= 0)
        return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, chars, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, chars, out.data(), bytes, nullptr, nullptr);
    return out;
}

bool DataHasFiles(IDataObject* data)
{
    if (!data)
        return false;
    FORMATETC fmt{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return data->QueryGetData(&fmt) == S_OK;
}

} // namespace

struct WinFileDropTarget : IDropTarget {
    explicit WinFileDropTarget(App* app) : app_(app) {}
    void Detach() { app_ = nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (!object)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG left = static_cast<ULONG>(InterlockedDecrement(&refs_));
        if (left == 0)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override
    {
        bool ok = DataHasFiles(data);
        if (effect)
            *effect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        if (app_) {
            app_->external_file_drag_.store(ok, std::memory_order_relaxed);
            app_->OnFileDragHover(ok);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override
    {
        if (effect)
            *effect = app_ && app_->external_file_drag_.load(std::memory_order_relaxed)
                ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        if (app_) {
            app_->external_file_drag_.store(false, std::memory_order_relaxed);
            app_->OnFileDragHover(false);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override
    {
        if (effect)
            *effect = DROPEFFECT_COPY;
        if (app_)
            app_->external_file_drag_.store(false, std::memory_order_relaxed);

        FORMATETC fmt{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium{};
        if (data && SUCCEEDED(data->GetData(&fmt, &medium))) {
            // HDROP is the HGLOBAL itself; DragQueryFile does not want a lock pointer.
            HDROP drop = static_cast<HDROP>(medium.hGlobal);
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < count; ++i) {
                UINT chars = DragQueryFileW(drop, i, nullptr, 0);
                if (chars == 0)
                    continue;
                std::wstring wide(chars + 1, L'\0');
                DragQueryFileW(drop, i, wide.data(), chars + 1);
                wide.resize(chars);
                std::string utf8 = WideToUtf8(wide.c_str(), static_cast<int>(wide.size()));
                if (app_ && !utf8.empty())
                    app_->OnFileDropped(utf8);
            }
            ReleaseStgMedium(&medium);
        }

        if (app_)
            app_->OnFileDragHover(false);
        return S_OK;
    }

    App* app_ = nullptr;
    LONG refs_ = 1;
};

static HWND WindowHwnd(SDL_Window* window)
{
    if (!window)
        return nullptr;
    return static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

#endif // _WIN32

void App::InstallFileDrop()
{
#ifdef _WIN32
    // SDL's own drop target fights a borderless hit-tested window. Take the
    // HWND ourselves so Explorer drops arrive as UTF-8 paths.
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
    SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);

    HWND hwnd = WindowHwnd(window_);
    if (!hwnd)
        return;

    ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);

    HRESULT ole = OleInitialize(nullptr);
    ole_inited_by_us_ = (ole == S_OK);
    if (FAILED(ole) && ole != RPC_E_CHANGED_MODE) {
        DragAcceptFiles(hwnd, TRUE);
        SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
        log_info("App::InstallFileDrop: OLE unavailable, WM_DROPFILES fallback");
        return;
    }

    // Drop any target SDL registered at window creation, then take the HWND.
    RevokeDragDrop(hwnd);
    auto* target = new WinFileDropTarget(this);
    HRESULT hr = RegisterDragDrop(hwnd, target);
    if (SUCCEEDED(hr)) {
        drop_target_ = target;
        log_info("App::InstallFileDrop: IDropTarget registered");
        return;
    }
    target->Release();
    DragAcceptFiles(hwnd, TRUE);
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
    log_info("App::InstallFileDrop: RegisterDragDrop failed %08lx, SDL fallback",
             static_cast<unsigned long>(hr));
#else
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
    SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, true);
#endif
}

void App::RemoveFileDrop()
{
#ifdef _WIN32
    HWND hwnd = WindowHwnd(window_);
    if (hwnd && drop_target_) {
        auto* target = static_cast<WinFileDropTarget*>(drop_target_);
        target->Detach();
        RevokeDragDrop(hwnd);
        target->Release();
        drop_target_ = nullptr;
    }
    if (ole_inited_by_us_) {
        OleUninitialize();
        ole_inited_by_us_ = false;
    }
#endif
    external_file_drag_.store(false, std::memory_order_relaxed);
}

SDL_HitTestResult App::HitTestCallback(SDL_Window* win, const SDL_Point* area, void* data) {
    auto* app = static_cast<App*>(data);
    // A file drag must hit the client area; caption hit-testing swallows the drop.
    if (app->external_file_drag_.load(std::memory_order_relaxed))
        return SDL_HITTEST_NORMAL;

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
    auto* vp = ImGui::GetMainViewport();
    float w = vp->Size.x;
    float h = config_.topbar_height;

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y));
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
