#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>

namespace volt {

struct TrayMenuItem {
    std::string id;
    std::string label;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
};

enum class TrayEventType {
    None,
    LeftClick,
    RightClick,
    DoubleClick,
    MenuSelect,
};

struct TrayEvent {
    TrayEventType type = TrayEventType::None;
    std::string menuId;
    int x = 0, y = 0;
};

using TrayCallback = std::function<void(const TrayEvent&)>;

class TrayIcon {
public:
    using Ptr = std::shared_ptr<TrayIcon>;

    static Ptr Create();

    TrayIcon();
    ~TrayIcon();

    bool Init(const std::string& iconPath, const std::string& tooltip = "");
    bool InitFromData(const uint8_t* rgba, int w, int h, const std::string& tooltip = "");

    void SetMenu(const std::vector<TrayMenuItem>& items);
    void UpdateMenu();
    void Show();
    void Hide();
    bool IsVisible() const;

    void SetTooltip(const std::string& tooltip);
    void SetIcon(const std::string& iconPath);

    void SetCallback(TrayCallback cb) { m_callback = cb; }

    void PollEvents();

    void Process();

    void PushEvent(TrayEvent ev);

private:
    TrayCallback m_callback;
    std::atomic<bool> m_visible{false};
    std::vector<TrayMenuItem> m_menuItems;

    std::mutex m_eventMutex;
    std::deque<TrayEvent> m_eventQueue;

    struct PlatformData;
    std::unique_ptr<PlatformData> m_data;

    bool PopEvent(TrayEvent& ev);

    bool PlatformInit(const std::string& iconPath, const std::string& tooltip);
    bool PlatformInitFromData(const uint8_t* rgba, int w, int h, const std::string& tooltip);
    void PlatformShow();
    void PlatformHide();
    void PlatformUpdateMenu();
    void PlatformSetTooltip(const std::string& tooltip);
    void PlatformSetIcon(const std::string& iconPath);
    bool PlatformIsVisible() const;
};

} // namespace volt
