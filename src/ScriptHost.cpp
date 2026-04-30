#include "volt-ui/ScriptHost.h"
#include "log.h"

#if defined(VOLT_ENABLE_LEANCLR)

#include <imgui.h>

#include "public/leanclr.h"
#include "vm/runtime.h"
#include "vm/settings.h"
#include "vm/assembly.h"
#include "vm/class.h"
#include "vm/method.h"
#include "vm/rt_string.h"
#include "core/rt_result.h"
#include "utils/rt_span.h"
#include "alloc/general_allocation.h"

#include <fstream>
#include <vector>
#include <cstring>

namespace volt {

static std::string Utf16ToUtf8(const uint16_t* utf16, int len) {
    std::string result;
    for (int i = 0; i < len; ++i) {
        uint32_t cp = utf16[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
            uint32_t low = utf16[++i];
            cp = ((cp - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
        }
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

static std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return {};
    return data;
}

static float g_current_dt = 0.0f;
static uint64_t g_current_frame = 0;

    static void ICall_VoltUI_Text(LeanclrMethodPointer, const LeanclrMethodInfo*,
                               const LeanclrStackObject* args, LeanclrStackObject* ret,
                               LeanclrException**) {
        size_t offset = 0;
        void* str_obj = nullptr;
    leanclr_get_argument(args, &offset, &str_obj, sizeof(void*));
        if (str_obj) {
            auto* rt_str = static_cast<leanclr::vm::RtString*>(str_obj);
            int len = leanclr::vm::String::get_length(rt_str);
        auto* chars = leanclr::vm::String::get_chars_ptr(rt_str);
            std::string text = Utf16ToUtf8(reinterpret_cast<const uint16_t*>(chars), len);
            ImGui::TextUnformatted(text.c_str());
        }
        (void)ret;
    }

    static void ICall_VoltUI_Button(LeanclrMethodPointer, const LeanclrMethodInfo*,
                                  const LeanclrStackObject* args, LeanclrStackObject* ret,
                                  LeanclrException**) {
        size_t offset = 0;
        void* str_obj = nullptr;
        leanclr_get_argument(args, &offset, &str_obj, sizeof(void*));
        int32_t result = 0;
        if (str_obj) {
            auto* rt_str = static_cast<leanclr::vm::RtString*>(str_obj);
            int len = leanclr::vm::String::get_length(rt_str);
            auto* chars = leanclr::vm::String::get_chars_ptr(rt_str);
            std::string label = Utf16ToUtf8(reinterpret_cast<const uint16_t*>(chars), len);
            result = ImGui::Button(label.c_str()) ? 1 : 0;
            log_info("ICall_VoltUI_Button label:'%s' result=%d", label.c_str(), result);
        }
        leanclr_set_return_value(ret, &result, sizeof(int32_t));
    }

static void ICall_VoltUI_GetDeltaTime(LeanclrMethodPointer, const LeanclrMethodInfo*,
                                       const LeanclrStackObject* args, LeanclrStackObject* ret,
                                       LeanclrException**) {
    leanclr_set_return_value(ret, &g_current_dt, sizeof(float));
    (void)args;
}

static void ICall_VoltUI_GetFrameCount(LeanclrMethodPointer, const LeanclrMethodInfo*,
                                        const LeanclrStackObject* args, LeanclrStackObject* ret,
                                        LeanclrException**) {
    leanclr_set_return_value(ret, &g_current_frame, sizeof(uint64_t));
    (void)args;
}

struct ScriptHostImpl {
    std::string bcl_path;
    std::string assembly_dir;
    std::string entry_class;

    const LeanclrMethodInfo* method_create = nullptr;
    const LeanclrMethodInfo* method_update = nullptr;
    const LeanclrMethodInfo* method_destroy = nullptr;
    LeanclrModuleDef* main_module = nullptr;
};

static ScriptHostImpl* g_impl = nullptr;

static leanclr::RtResult<leanclr::vm::FileData> AssemblyLoadCallback(
    const char* assembly_name, const char* extension) {
    if (!g_impl) return leanclr::RtErr::FileNotFound;

    std::string name = std::string(assembly_name) + "." + extension;

    auto try_load = [&](const std::string& dir) -> leanclr::RtResult<leanclr::vm::FileData> {
        std::string path = dir + "/" + name;
        auto bytes = ReadFileBytes(path);
        if (bytes.empty()) return leanclr::RtErr::FileNotFound;

        auto* buf = static_cast<uint8_t*>(
            leanclr::alloc::GeneralAllocation::malloc(bytes.size()));
        if (!buf) return leanclr::RtErr::OutOfMemory;

        std::memcpy(buf, bytes.data(), bytes.size());
        return leanclr::vm::FileData{buf, bytes.size()};
    };

    auto r = try_load(g_impl->bcl_path);
    if (r.is_ok()) return r;
    return try_load(g_impl->assembly_dir);
}

static void InvokeNoArgs(const LeanclrMethodInfo* method) {
    if (!method) return;
    LEANCLR_DECLARING_ALLOC_METHOD_ARGUMENT_BUFFER(arg_buf, offset, method);
    LEANCLR_DECLARING_ALLOC_METHOD_RETURN_BUFFER(ret_buf, method);
    LeanclrException* ex = nullptr;
    leanclr_invoke_with_buffer(method, arg_buf, ret_buf, &ex);
    if (ex) {
        log_error("InvokeNoArgs: exception occurred (ex=%p)", (void*)ex);
    }
}

static void InvokeFloatArg(const LeanclrMethodInfo* method, float value) {
    if (!method) return;
    LEANCLR_DECLARING_ALLOC_METHOD_ARGUMENT_BUFFER(arg_buf, offset, method);
    LEANCLR_DECLARING_ALLOC_METHOD_RETURN_BUFFER(ret_buf, method);
    leanclr_push_argument(arg_buf, &offset, &value, sizeof(float));
    LeanclrException* ex = nullptr;
    leanclr_invoke_with_buffer(method, arg_buf, ret_buf, &ex);
    if (ex) {
        log_error("InvokeFloatArg: exception occurred (ex=%p)", (void*)ex);
    }
}

ScriptHost::ScriptHost() = default;
ScriptHost::~ScriptHost() { Shutdown(); }

bool ScriptHost::Initialize(const ScriptHostConfig& cfg) {
    if (initialized_) return true;

    auto* impl = new ScriptHostImpl();
    impl_ = impl;
    impl->bcl_path = cfg.bcl_path;
    impl->entry_class = cfg.entry_class;
    impl->assembly_dir = ".";
    impl->method_create = nullptr;
    impl->method_update = nullptr;
    impl->method_destroy = nullptr;
    impl->main_module = nullptr;

    std::string ass_path = cfg.assembly;
    auto sep = ass_path.rfind('/');
    if (sep == std::string::npos) sep = ass_path.rfind('\\');
    if (sep != std::string::npos) {
        impl->assembly_dir = ass_path.substr(0, sep);
        ass_path = ass_path.substr(sep + 1);
    }
    auto dot = ass_path.rfind('.');
    if (dot != std::string::npos) ass_path = ass_path.substr(0, dot);
    std::string assembly_name = ass_path;

    g_impl = impl;
    g_current_dt = 0.0f;
    g_current_frame = 0;

    leanclr::vm::Settings::set_file_loader(AssemblyLoadCallback);
    auto result = leanclr::vm::Runtime::initialize();
    if (result.is_err()) {
        auto err_val = static_cast<int>(result.unwrap_err());
        log_error("ScriptHost::Initialize failed: RtErr=%d. Check that BCL assemblies exist at '%s' and the C# DLL is in '%s'.",
                  err_val, impl->bcl_path.c_str(), impl->assembly_dir.c_str());
        Shutdown();
        return false;
    }

    leanclr_register_internal_call_func("VoltUI::Text", nullptr, ICall_VoltUI_Text);
    leanclr_register_internal_call_func("VoltUI::Button", nullptr, ICall_VoltUI_Button);
    leanclr_register_internal_call_func("VoltUI::GetDeltaTime", nullptr, ICall_VoltUI_GetDeltaTime);
    leanclr_register_internal_call_func("VoltUI::GetFrameCount", nullptr, ICall_VoltUI_GetFrameCount);
    log_info("ScriptHost: Registered InternalCalls for VoltUI");

    LeanclrException* ex = nullptr;
    auto* ass = leanclr_load_assembly(assembly_name.c_str(), &ex);
    if (!ass) {
        log_error("ScriptHost::Initialize failed: leanclr_load_assembly('%s') returned null (ex=%p). Check that the DLL exists in '%s/'.",
                  assembly_name.c_str(), (void*)ex, impl->assembly_dir.c_str());
        Shutdown();
        return false;
    }
    log_debug("ScriptHost: assembly '%s' loaded OK", assembly_name.c_str());

    impl->main_module = leanclr_get_assembly_by_module(ass);

    auto* klass = leanclr_get_class_by_name(impl->main_module,
                                             impl->entry_class.c_str(), false, &ex);
    if (!klass) {
        log_error("ScriptHost::Initialize failed: class '%s' not found in assembly",
                  impl->entry_class.c_str());
        Shutdown();
        return false;
    }
    log_debug("ScriptHost: class '%s' found OK", impl->entry_class.c_str());

    impl->method_create = leanclr_get_class_method_by_name(klass, "OnCreate", &ex);
    impl->method_update = leanclr_get_class_method_by_name(klass, "OnUpdate", &ex);
    impl->method_destroy = leanclr_get_class_method_by_name(klass, "OnDestroy", &ex);
    log_info("ScriptHost: methods found - OnCreate=%p OnUpdate=%p OnDestroy=%p",
             (void*)impl->method_create, (void*)impl->method_update, (void*)impl->method_destroy);

    initialized_ = true;
    return true;
}

void ScriptHost::Shutdown() {
    if (impl_) {
        g_impl = nullptr;
        leanclr::vm::Runtime::shutdown();
        delete static_cast<ScriptHostImpl*>(impl_);
        impl_ = nullptr;
    }
    initialized_ = false;
}

void ScriptHost::OnCreate() {
    auto* i = static_cast<ScriptHostImpl*>(impl_);
    InvokeNoArgs(i ? i->method_create : nullptr);
}

void ScriptHost::OnUpdate(float dt) {
    auto* i = static_cast<ScriptHostImpl*>(impl_);
    if (!i) return;
    g_current_dt = dt;
    if (i->method_update) {
        InvokeFloatArg(i->method_update, dt);
    } else {
        log_error("ScriptHost::OnUpdate: method_update is null, skipping C# invocation");
    }
}

void ScriptHost::OnDestroy() {
    auto* i = static_cast<ScriptHostImpl*>(impl_);
    InvokeNoArgs(i ? i->method_destroy : nullptr);
}

void ScriptHost::RegisterInternalCall(const char* name, void* handler) {
    leanclr_register_internal_call_func(
        name, nullptr, reinterpret_cast<LeanclrMethodInvoker>(handler));
}

} // namespace volt

#else // !VOLT_ENABLE_LEANCLR

namespace volt {

ScriptHost::ScriptHost() = default;
ScriptHost::~ScriptHost() = default;
bool ScriptHost::Initialize(const ScriptHostConfig&) { return false; }
void ScriptHost::Shutdown() {}
void ScriptHost::OnCreate() {}
void ScriptHost::OnUpdate(float) {}
void ScriptHost::OnDestroy() {}
void ScriptHost::RegisterInternalCall(const char*, void*) {}

} // namespace volt

#endif
