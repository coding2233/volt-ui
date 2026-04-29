#pragma once

#include <string>

namespace volt {

struct ScriptHostConfig {
    std::string assembly;
    std::string entry_class;
    std::string bcl_path = "deps/leanclr/src/libraries/dotnetframework4.x";
};

class ScriptHost {
public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    bool Initialize(const ScriptHostConfig& cfg);
    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    void OnCreate();
    void OnUpdate(float dt);
    void OnDestroy();

    void RegisterInternalCall(const char* name, void* handler);

private:
    void* impl_ = nullptr;
    bool initialized_ = false;
};

} // namespace volt
