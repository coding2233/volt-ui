# Volt-UI + LeanCLR 集成开发文档

## 1. 概述

将 [LeanCLR](https://github.com/focus-creative-games/leanclr) 嵌入 volt-ui，实现在 C++ 应用中运行 C# 脚本。

### 架构图

```
┌──────────────────────────────────────────────────────┐
│                  C# User Script                       │
│  [VoltUI.Button()]  [VoltUI.Text()]  [OnUpdate()]    │
└────────────────────────┬─────────────────────────────┘
                         │ InternalCall
                         ▼
┌──────────────────────────────────────────────────────┐
│              ScriptHost (C++ 桥接层)                   │
│  初始化/加载 ║ InternalCall 注册 ║ 方法调用             │
└────┬──────────────┬──────────────────┬────────────────┘
     │              │                  │
     ▼              ▼                  ▼
┌──────────┐ ┌──────────┐ ┌──────────────────────┐
│ LeanCLR  │ │  ImGui   │ │   SDL3 + SDL_Renderer │
│  Runtime │ │   API    │ │       (volt-ui)       │
└──────────┘ └──────────┘ └──────────────────────┘
```

### 核心数据流

```
每帧用户侧的编排 (在 App 子类中):

  void OnUpdate(float dt) override {
      // 1. 先执行 C++ 逻辑
      // 2. 再手动触发 C# 脚本
      if (script_host_) script_host_->OnUpdate(dt);
  }

App::Run() 内部循环:
  ProcessEvents()
  BeginFrame()                ← ImGui::NewFrame()
  OnUpdate(dt)                ← 用户在此处决定是否调用 ScriptHost
  OnRender()                  ← C++ UI 代码 / C# 通过 InternalCall 调用 ImGui
  EndFrame()                  ← ImGui::Render() + SDL_RenderPresent()
```

---

## 2. 目录结构

```
volt-ui/
├── deps/
│   ├── leanclr/                   # git submodule (focus-creative-games/leanclr)
│   │   ├── src/runtime/           # 运行时源代码 (C++17, 零依赖)
│   │   └── src/libraries/         # .NET Framework BCL 程序集
│   └── leanclr.lua                # xmake 构建包装 (由 volt-ui 管理)
├── include/volt-ui/
│   ├── VoltApp.h                  # App 框架 (不含 scripting)
│   └── ScriptHost.h               # 独立脚本宿主接口
├── src/
│   ├── VoltApp.cpp                # 生命周期 (无脚本钩子)
│   └── ScriptHost.cpp             # LeanCLR 封装 (VOLT_ENABLE_LEANCLR 门控)
├── scripts/
│   ├── VoltUI.cs                  # C# 端 ImGui 桥接 API
│   └── AppMain.cs                 # C# 用户入口示例
├── examples/
│   ├── basic/main.cpp             # 无脚本示例
│   └── scripting/main.cpp         # 启用脚本的示例 (手动管理 ScriptHost)
├── docs/
│   └── leanclr-integration.md     # 本文档
└── xmake.lua                      # 条件编译 leanclr
```

---

## 3. 构建配置

### 3.1 xmake option

```lua
option("enable_leanclr")
    set_default(false)
    set_showmenu(true)
    set_description("Enable C# scripting via LeanCLR runtime")
```

### 3.2 leanclr 构建包装 (`deps/leanclr.lua`)

LeanCLR 原生使用 CMake，volt-ui 为其编写 xmake 包装直接编译源文件。
此文件位于 `deps/leanclr.lua`，由 volt-ui 项目管理，不放在 leanclr 子模块内：

```lua
target("leanclr")
    set_kind("static")
    set_languages("c++17")
    add_defines("LEANCLR_NO_EXCEPTION", "LEANCLR_PLATFORM_LINUX",
                "LEANCLR_PLATFORM_POSIX")
    set_exceptions(false)
    -- 源文件 (路径相对于 deps/)
    for _, dir in ipairs({"alloc","codegen","core","gc","icalls","interp",
                          "intrinsics","log","metadata","misc","platform",
                          "utils","vm","public_impl"}) do
        add_files(path.join("leanclr/src/runtime", dir, "*.cpp"))
    end
    -- include 路径
    add_includedirs("leanclr/src/runtime", {public = true})
    for _, dir in ipairs({"alloc","codegen","core","gc","icalls","interp",
                          "intrinsics","log","metadata","misc","platform",
                          "utils","vm","public_impl","public","3rd"}) do
        add_includedirs(path.join("leanclr/src/runtime", dir), {public = true})
    end
```

### 3.3 volt-ui 条件编译

```lua
target("volt-ui")
    ...
    if has_config("enable_leanclr") then
        includes("deps/leanclr.lua")
        add_deps("leanclr")
        add_defines("VOLT_ENABLE_LEANCLR")
    end
```

### 3.4 BCL 程序集管理

ScriptHost 的 `bcl_path` 配置指定 BCL 程序集目录，默认值：

```
deps/leanclr/src/libraries/dotnetframework4.x/
```

运行时通过 `vm::Settings::set_file_loader()` 设置回调，当 `Runtime::initialize()`
需要加载 mscorlib 等程序集时，回调按以下顺序搜索：

1. `{bcl_path}/`
2. `{assembly_dir}/` (与 C# DLL 同目录)

---

## 4. ScriptHost 类设计

### 4.1 设计原则

ScriptHost **不依赖 App**，是一个独立的组件。用户根据业务需求自行决定：

- **是否** 接入脚本引擎
- **何时** 初始化、调用生命周期、关闭
- 在 App 子类的虚函数重写中手动编排脚本钩子

### 4.2 配置结构体

```cpp
struct ScriptHostConfig {
    std::string assembly;            // C# DLL 名称 (不含后缀, 如 "AppMain")
    std::string entry_class;         // C# 入口类名 (如 "AppMain")
    std::string bcl_path;            // BCL 程序集目录
};
```

### 4.3 公开 API

```cpp
namespace volt {

class ScriptHost {
public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    bool Initialize(const ScriptHostConfig& cfg);
    void Shutdown();
    bool IsInitialized() const;

    void OnCreate();
    void OnUpdate(float dt);
    void OnDestroy();

    void RegisterInternalCall(const char* name, void* handler);

private:
    void* impl_ = nullptr;
    bool initialized_ = false;
};

} // namespace volt
```

### 4.4 初始化流程

```
ScriptHost::Initialize(cfg):
  1. 保存 BCL 路径和用户程序集配置
  2. 设置 assembly_loader 回调
  3. vm::Runtime::initialize()
  4. 注册内置 InternalCall (VoltUI.*)
  5. 加载用户程序集 (cfg.assembly)
  6. 解析入口类中的方法指针 (OnCreate/OnUpdate/OnDestroy)
```

### 4.5 生命周期映射

| C++ 调用 | → | C# 脚本方法 |
|---|---|---|
| `ScriptHost::OnCreate()` | → | `ClassName::OnCreate()` |
| `ScriptHost::OnUpdate(dt)` | → | `ClassName::OnUpdate(float dt)` |
| `ScriptHost::OnDestroy()` | → | `ClassName::OnDestroy()` |

---

## 5. InternalCall 注册 (C# ↔ C++ 桥接)

### 5.1 C# 声明

C# 使用 `[MethodImpl(MethodImplOptions.InternalCall)]` 声明运行时内置函数，
不走 P/Invoke，直接由 CLR 分发：

```csharp
internal static class VoltUI {
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern bool Button(string label);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern void Text(string text);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern float GetDeltaTime();

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern long GetFrameCount();
}
```

### 5.2 C++ 注册

```cpp
// InternalCall invoker 签名 (由 leanclr C API 定义)
typedef void (*LeanclrMethodInvoker)(
    LeanclrMethodPointer method_ptr,
    const LeanclrMethodInfo* method,
    const LeanclrStackObject* args,   // 输入参数栈
    LeanclrStackObject* ret,          // 返回值缓冲区
    LeanclrException** exception      // 异常输出
);

// 注册
leanclr_register_internal_call_func(
    "VoltUI::Button",           // C# StaticClass::MethodName
    nullptr,                    // method_ptr (InternalCall 不需要)
    ButtonInvoker               // 实际处理函数
);
```

### 5.3 命名规则

`{C#类名}::{方法名}`，区分大小写，必须与 C# 代码完全一致。例如 `VoltUI::Button`。

### 5.4 参数读写

```c
// 读取参数
size_t offset = 0;
int32_t int_val;
leanclr_get_argument(args, &offset, &int_val, sizeof(int32_t));

// 设置返回值
bool result = true;
leanclr_set_return_value(ret, &result, sizeof(bool));
```

### 5.5 内置桥接函数

| C# 签名 | C++ handler | 说明 |
|---------|-------------|------|
| `VoltUI::Text(string)` | `ICall_VoltUI_Text` | 显示文本 |
| `VoltUI::Button(string): bool` | `ICall_VoltUI_Button` | 按钮 |
| `VoltUI::GetDeltaTime(): float` | `ICall_VoltUI_GetDeltaTime` | 获取帧时间 |
| `VoltUI::GetFrameCount(): long` | `ICall_VoltUI_GetFrameCount` | 获取帧数 |

> **字符串处理**: InternalCall 收到的 string 参数是 `RtString*` (managed object ptr)。
> 需通过 `vm::RtString` 内部 API 获取 UTF-16 char 数组，再转为 UTF-8。
> 当前实现通过包含 `vm/rt_string.h` 内部头文件完成此转换。

---

## 6. 业务集成

ScriptHost **不与 App 绑定**，由用户在子类中自行管理：

```cpp
#include <volt-ui/VoltApp.h>
#include <volt-ui/ScriptHost.h>

class MyApp : public volt::App {
public:
    MyApp(const volt::AppConfig& cfg, volt::ScriptHost* host)
        : App(cfg), script_host_(host) {}

protected:
    void OnCreate() override {
        if (script_host_) script_host_->OnCreate();
    }

    void OnUpdate(float dt) override {
        if (script_host_) script_host_->OnUpdate(dt);
    }

    void OnDestroy() override {
        if (script_host_) {
            script_host_->OnDestroy();
            script_host_->Shutdown();
        }
    }

private:
    volt::ScriptHost* script_host_;
};

int main() {
    volt::ScriptHostConfig cfg;
    cfg.assembly = "AppMain";
    cfg.entry_class = "AppMain";
    cfg.bcl_path = "deps/leanclr/src/libraries/dotnetframework4.x";

    volt::ScriptHost host;
    host.Initialize(cfg);

    MyApp app({}, &host);
    return app.Run();
}
```

### 接入方式对比

| 方式 | 说明 |
|------|------|
| **完整接入** | 子类持有 ScriptHost，在 OnCreate/OnUpdate/OnDestroy 中手动调用 |
| **条件接入** | 根据运行时配置决定是否创建 ScriptHost |
| **不接入** | 完全不使用 ScriptHost，App 无任何脚本相关代码 |

ScriptHost 的 `Initialize()` 可在 `App::Run()` 之前任意位置调用，
`OnCreate/OnUpdate/OnDestroy` 在子类对应虚函数中手动编排。

---

## 7. 示例

### 7.1 C# 脚本 (`scripts/AppMain.cs`)

```csharp
using System;

internal static class AppMain {
    public static void OnCreate() {
        Console.WriteLine("C# script started!");
    }

    public static void OnUpdate(float dt) {
        if (VoltUI.Button("Hello from C#")) {
            VoltUI.Text("Button clicked!");
        }
        VoltUI.Text($"DT: {dt * 1000:F2} ms  Frame: {VoltUI.GetFrameCount()}");
    }

    public static void OnDestroy() {
        Console.WriteLine("C# script stopped!");
    }
}
```

### 7.2 C++ 入口 (`examples/scripting/main.cpp`)

```cpp
volt::ScriptHostConfig script_cfg;
script_cfg.assembly = "AppMain";
script_cfg.entry_class = "AppMain";
script_cfg.bcl_path = "deps/leanclr/src/libraries/dotnetframework4.x";

volt::ScriptHost host;
host.Initialize(script_cfg);

volt::AppConfig cfg;
cfg.title = "Volt UI + LeanCLR";
cfg.use_topbar = true;

ScriptingApp app(cfg, &host);
return app.Run();
```

ScriptHost 在 `App::Run()` 之前独立初始化，通过构造函数注入给 App 子类。
子类在 `OnCreate/OnUpdate/OnDestroy` 中手动调用脚本生命周期方法。

---

## 8. 编译与使用

### 8.1 前提条件

- 已初始化 leanclr 子模块
- BCL 程序集已就位（位于 `deps/leanclr/src/libraries/dotnetframework4.x/`）
- C# 编译器（`mcs`/`csc`/`dotnet` 任一）

### 8.2 构建脚本

```bash
# 1. 配置项目（启用 leanclr）
xmake f --enable_leanclr=y -y

# 2. 编译 C++ 库和示例
xmake build

# 3. 编译 C# 用户脚本（生成 DLL）
mcs -target:library -out:AppMain.dll \
    scripts/VoltUI.cs scripts/AppMain.cs \
    -r:System.dll -r:mscorlib.dll
```

### 8.3 C# 脚本开发流程

1. 编写 C# 代码，引用 `VoltUI` 桥接 API
2. 用 `mcs` 编译为 DLL
3. 将 DLL 放到程序运行目录（与 `cfg.assembly` 对应）
4. 运行 C++ 程序

```bash
# 编译 C# → DLL
mcs -target:library -out:examples/scripting/AppMain.dll \
    scripts/VoltUI.cs your_script.cs \
    -r:System.dll

# 运行 C++ 程序
./build/linux/x86_64/release/scripting
```

### 8.4 示例 C# 脚本模板

```csharp
using System;

internal static class MyScript {
    public static void OnCreate() {
        VoltUI.Text("C# script loaded!");
    }

    public static void OnUpdate(float dt) {
        if (VoltUI.Button("Click me")) {
            VoltUI.Text("Button clicked!");
        }
        VoltUI.Text($"FPS: {1.0f / dt:F1}");
    }

    public static void OnDestroy() {
        // cleanup
    }
}
```

### 8.5 快速验证

```bash
# 完整构建流程
git submodule add https://github.com/focus-creative-games/leanclr deps/leanclr
xmake f --enable_leanclr=y -y
xmake build

# 编译 C# 脚本
mcs -target:library -out:AppMain.dll \
    scripts/VoltUI.cs scripts/AppMain.cs \
    -r:System.dll

# 验证: 运行脚本示例
./build/linux/x86_64/release/scripting

# (可选) 运行无脚本示例
./build/linux/x86_64/release/basic
```

---

## 9. 注意事项

| 问题 | 说明 |
|------|------|
| **C++ 异常** | leanclr 禁用 C++ 异常 (`-fno-exceptions`)，与 volt-ui 编译隔离 |
| **BCL 分发** | mscorlib.dll (~1.5MB) 等 BCL 程序集需随应用分发 |
| **GC** | LeanCLR Core 版 GC 开发中，当前依赖 BCL 的保守式 GC |
| **字符串传递** | C# string → C++ 需 `RtString` 到 `const char*` 转换，有性能开销 |
| **线程** | Core 版单线程，ScriptHost 所有操作必须在主线程 |
| **跨平台** | leanclr 纯 C++17，`LEANCLR_PLATFORM_*` 宏按需设置 |
