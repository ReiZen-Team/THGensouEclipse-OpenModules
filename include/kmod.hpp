#pragma once
// ═════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ModInfo.hpp"
#include "Module.hpp"
#include "hostcall.hpp"
#include "ui.hpp"

namespace kmod {

using json = nlohmann::json;

Module* __kmod_make_module();

// ─────────────────────────────────────────────────────────────────────────────
class Runtime {
public:
    static Runtime& get() { static Runtime r; return r; }

    Module* module() {
        if (!mod_) mod_.reset(__kmod_make_module());
        return mod_.get();
    }

    int add_tool(std::function<void()> fn)               { tools_.push_back(std::move(fn));  return (int)tools_.size()  - 1; }
    int add_task(std::function<void()> fn)               { tasks_.push_back(std::move(fn));  return (int)tasks_.size()  - 1; }
    int add_retry(std::function<bool()> fn)              { retries_.push_back(std::move(fn)); return (int)retries_.size() - 1; }
    int add_event(std::function<void(const json&)> fn)   { events_.push_back(std::move(fn)); return (int)events_.size() - 1; }
    int add_rpc(std::function<json(const json&)> fn)      { rpc_.push_back(std::move(fn));    return (int)rpc_.size()    - 1; }
    int add_cfg(std::function<json()> g, std::function<void(const json&)> s) {
        cfg_get_.push_back(std::move(g));
        cfg_set_.push_back(std::move(s));
        return (int)cfg_get_.size() - 1;
    }

    long long info_packed() {
        Module*    m  = module();
        KModInfo   ki = m ? m->kmodinfo() : KModInfo{};
        ModuleInfo mi = m ? m->get_info() : ModuleInfo{};
        json meta = {
            {"id", mi.id}, {"name", mi.name}, {"version", mi.version},
            {"author", mi.author}, {"description", mi.description},
            {"permissions", ki.permissions}, {"flags", ki.flags},
            {"deps", std::string(ki.depends)},
        };
        return pack(meta.dump());
    }
    void on_load()   { if (auto* m = module()) m->on_load(); }
    void on_unload() { if (auto* m = module()) m->on_unload(); }

    long long dispatch(int kind, int token, const char* args, int alen) {
        json a = json::object();
        if (args && alen > 0) {
            json parsed = json::parse(std::string(args, alen), nullptr, false);
            if (!parsed.is_discarded()) a = parsed;
        }
        switch (kind) {
            case 1:
                if (token >= 0 && token < (int)tools_.size()) tools_[token]();
                return 0;
            case 2:
                if (token >= 0 && token < (int)events_.size()) events_[token](a.value("data", json(nullptr)));
                return 0;
            case 3:
                if (token >= 0 && token < (int)rpc_.size()) return pack(rpc_[token](a.value("data", json(nullptr))).dump());
                return 0;
            case 4:
                if (token >= 0 && token < (int)cfg_get_.size()) return pack(json{{"value", cfg_get_[token]()}}.dump());
                return 0;
            case 5:
                if (token >= 0 && token < (int)cfg_set_.size()) cfg_set_[token](a.value("value", json(nullptr)));
                return 0;
            case 6:
                if (token >= 0 && token < (int)tasks_.size()) tasks_[token]();
                return 0;
            case 7:
                if (token >= 0 && token < (int)retries_.size()) return pack(json{{"ok", retries_[token]()}}.dump());
                return 0;
        }
        return 0;
    }

private:
    long long pack(const std::string& s) {
        if (s.empty()) return 0;
        ret_ = s;
        unsigned off = static_cast<unsigned>(reinterpret_cast<uintptr_t>(ret_.data()));
        return (static_cast<long long>(off) << 32) | static_cast<long long>(ret_.size() & 0xFFFFFFFFu);
    }

    std::unique_ptr<Module>                        mod_;
    std::string                                    ret_;
    std::vector<std::function<void()>>             tools_;
    std::vector<std::function<void()>>             tasks_;
    std::vector<std::function<bool()>>             retries_;
    std::vector<std::function<void(const json&)>>  events_;
    std::vector<std::function<json(const json&)>>  rpc_;
    std::vector<std::function<json()>>             cfg_get_;
    std::vector<std::function<void(const json&)>>  cfg_set_;
};

inline Runtime& runtime() { return Runtime::get(); }

// ── Logging ────────────────────────────────────────────────────────────────────
inline void log(int level, const std::string& ns, const std::string& msg) {
    host::notify("log", {{"level", level}, {"ns", ns}, {"msg", msg}});
}
inline void log_info(const std::string& m, const std::string& ns = "MOD")  { log(0, ns, m); }
inline void log_warn(const std::string& m, const std::string& ns = "MOD")  { log(1, ns, m); }
inline void log_error(const std::string& m, const std::string& ns = "MOD") { log(2, ns, m); }
inline json log_dump()  { return host::call("log.dump"); }
inline void log_clear() { host::notify("log.clear"); }

// ── Hot-reload state handoff ───────────────────────────────────────────────────────
inline bool        reload_active()                  { auto r = host::call("reload.active"); return r.is_boolean() && r.get<bool>(); }
inline void        reload_save(const std::string& blob) { host::notify("reload.save", {{"blob", blob}}); }
inline std::string reload_load()                    { auto r = host::call("reload.load"); return r.is_string() ? r.get<std::string>() : std::string{}; }

// ── Global kernel config (requires KERNEL_CONFIG_RW) ───────────────────────────────
inline std::optional<std::string> kconf_get(const std::string& key) { auto r = host::call("kconf.get", {{"key", key}}); if (r.is_string()) return r.get<std::string>(); return std::nullopt; }
inline void kconf_set(const std::string& key, const std::string& val) { host::notify("kconf.set", {{"key", key}, {"value", val}}); }
inline bool kconf_has(const std::string& key) { auto r = host::call("kconf.has", {{"key", key}}); return r.is_boolean() && r.get<bool>(); }
inline void kconf_del(const std::string& key) { host::notify("kconf.del", {{"key", key}}); }

// ── Identity / permissions ──────────────────────────────────────────────────────
inline std::string self_id() {
    auto r = host::call("id");
    return r.is_string() ? r.get<std::string>() : std::string{};
}
inline bool has_perm(uint64_t perm) {
    auto r = host::call("perm", {{"perm", perm}});
    return r.is_boolean() && r.get<bool>();
}

// ── Kernel ABI ──────────────────────────────────────────────────────────────────
inline uint32_t kernel_vernumber() {
    auto r = host::call("kernel.vernumber");
    return r.is_number() ? r.get<uint32_t>() : 0u;
}

// ── Events ──────────────────────────────────────────────────────────────────────
inline void emit(const std::string& ev, const json& data = json(nullptr)) {
    host::notify("evt.emit", {{"event", ev}, {"data", data}});
}
inline int listen(const std::string& ev, std::function<void(const json&)> fn) {
    int token = runtime().add_event(std::move(fn));
    host::call("evt.listen", {{"event", ev}, {"token", token}});
    return token;
}

// ── Task scheduler (cron in UTC; callbacks fire on the host scheduler thread) ──────
inline int64_t schedule_cron(const std::string& name, const std::string& cron, std::function<void()> fn) {
    int  token = runtime().add_task(std::move(fn));
    auto r     = host::call("sched.cron", {{"name", name}, {"cron", cron}, {"token", token}});
    return r.is_object() ? r.value("handle", static_cast<int64_t>(0)) : 0;
}
inline int64_t schedule_after(const std::string& name, int64_t delay_sec, std::function<void()> fn) {
    int  token = runtime().add_task(std::move(fn));
    auto r     = host::call("sched.after", {{"name", name}, {"delay_sec", delay_sec}, {"token", token}});
    return r.is_object() ? r.value("handle", static_cast<int64_t>(0)) : 0;
}
inline int64_t schedule_repeat(const std::string& name, int64_t delay_sec, int64_t interval_sec, std::function<void()> fn) {
    int  token = runtime().add_task(std::move(fn));
    auto r     = host::call("sched.repeat", {{"name", name}, {"delay_sec", delay_sec}, {"interval_sec", interval_sec}, {"token", token}});
    return r.is_object() ? r.value("handle", static_cast<int64_t>(0)) : 0;
}
inline int64_t schedule_retry(const std::string& name, std::function<bool()> fn,
                              int max_attempts = 3, int64_t base_delay_sec = 2, double factor = 2.0, int64_t max_delay_sec = 300) {
    int  token = runtime().add_retry(std::move(fn));
    auto r     = host::call("sched.retry", {{"name", name}, {"token", token},
                                            {"max_attempts", max_attempts}, {"base_delay_sec", base_delay_sec},
                                            {"factor", factor}, {"max_delay_sec", max_delay_sec}});
    return r.is_object() ? r.value("handle", static_cast<int64_t>(0)) : 0;
}
inline int64_t schedule_on_event(const std::string& name, const std::string& event, std::function<void()> fn) {
    int  token = runtime().add_task(std::move(fn));
    auto r     = host::call("sched.on_event", {{"name", name}, {"event", event}, {"token", token}});
    return r.is_object() ? r.value("handle", static_cast<int64_t>(0)) : 0;
}
inline bool schedule_cancel(int64_t handle) {
    auto r = host::call("sched.cancel", {{"handle", handle}});
    return r.is_boolean() && r.get<bool>();
}
inline json schedule_list() { return host::call("sched.list"); }

// ── Request / reply ───────────────────────────────────────────────────────────
inline bool reply(const std::string& topic, std::function<json(const json&)> fn) {
    int  token = runtime().add_rpc(std::move(fn));
    auto r     = host::call("rpc.reply", {{"topic", topic}, {"token", token}});
    return r.is_boolean() && r.get<bool>();
}
inline void unreply(const std::string& topic) { host::notify("rpc.unreply", {{"topic", topic}}); }
inline json request(const std::string& topic, const json& data = json(nullptr)) {
    return host::call("rpc.request", {{"topic", topic}, {"data", data}});
}
inline bool has_responder(const std::string& topic) {
    auto r = host::call("rpc.has", {{"topic", topic}});
    return r.is_boolean() && r.get<bool>();
}

// ── Tools ───────────────────────────────────────────────────────────────────────
inline void register_tool(const std::string& name, std::function<void()> fn, bool special = false) {
    int token = runtime().add_tool(std::move(fn));
    host::call("tool.register", {{"name", name}, {"special", special}, {"token", token}});
}

// ── Tool launcher (requires QUERY_MODULES) ─────────────────────────────────────────
inline json tools_list() { return host::call("tools.list"); }
inline bool tool_invoke(const std::string& name, bool special = false) {
    auto r = host::call("tools.invoke", {{"name", name}, {"special", special}});
    return r.is_boolean() && r.get<bool>();
}

// ── Config UI registration ───────────────────────────────────────────────────────
inline void register_config_bool(const std::string& name, std::function<bool()> getter, std::function<void(bool)> setter) {
    int token = runtime().add_cfg(
        [getter] { return json(getter()); },
        [setter](const json& v) { setter(v.is_boolean() ? v.get<bool>() : false); });
    host::call("cfgreg.bool", {{"name", name}, {"token", token}});
}
inline void register_config_string(const std::string& name, std::function<std::string()> getter, std::function<void(std::string)> setter) {
    int token = runtime().add_cfg(
        [getter] { return json(getter()); },
        [setter](const json& v) { setter(v.is_string() ? v.get<std::string>() : std::string{}); });
    host::call("cfgreg.string", {{"name", name}, {"token", token}});
}
inline void register_config_choice(const std::string& name, std::vector<std::string> options, std::function<std::string()> getter, std::function<void(std::string)> setter) {
    int token = runtime().add_cfg(
        [getter] { return json(getter()); },
        [setter](const json& v) { setter(v.is_string() ? v.get<std::string>() : std::string{}); });
    host::call("cfgreg.choice", {{"name", name}, {"token", token}, {"options", options}});
}
inline void register_config_int(const std::string& name, std::function<long long()> getter, std::function<void(long long)> setter,
                                long long min_v = INT64_MIN, long long max_v = INT64_MAX) {
    int token = runtime().add_cfg(
        [getter] { return json(getter()); },
        [setter](const json& v) { setter(v.is_number() ? v.get<long long>() : 0); });
    host::call("cfgreg.int", {{"name", name}, {"token", token}, {"min", min_v}, {"max", max_v}});
}
inline void register_config_float(const std::string& name, std::function<double()> getter, std::function<void(double)> setter,
                                  double min_v = -std::numeric_limits<double>::infinity(), double max_v = std::numeric_limits<double>::infinity()) {
    int token = runtime().add_cfg(
        [getter] { return json(getter()); },
        [setter](const json& v) { setter(v.is_number() ? v.get<double>() : 0.0); });
    host::call("cfgreg.float", {{"name", name}, {"token", token}, {"min", min_v}, {"max", max_v}});
}
inline void register_config_datetime(const std::string& name, std::function<std::string()> getter, std::function<void(std::string)> setter) {
    int token = runtime().add_cfg(
        [getter] { return json(getter()); },
        [setter](const json& v) { setter(v.is_string() ? v.get<std::string>() : std::string{}); });
    host::call("cfgreg.datetime", {{"name", name}, {"token", token}});
}
inline void remove_config(const std::string& name) { host::notify("cfgreg.remove", {{"name", name}}); }

// ── Module-scoped persistent config (mconf) ──────────────────────────────────────
inline std::optional<std::string> conf_get(const std::string& key) {
    auto r = host::call("cfg.get", {{"key", key}});
    if (r.is_string()) return r.get<std::string>();
    return std::nullopt;
}
inline void conf_set(const std::string& key, const std::string& val) { host::notify("cfg.set", {{"key", key}, {"value", val}}); }
inline bool conf_has(const std::string& key) { auto r = host::call("cfg.has", {{"key", key}}); return r.is_boolean() && r.get<bool>(); }
inline void conf_del(const std::string& key) { host::notify("cfg.del", {{"key", key}}); }

// ── Runtime config (RConfig) ─────────────────────────────────────────────────────
inline json rconfig_get(const std::string& key) { return host::call("rcfg.get", {{"key", key}}); }
inline void rconfig_set(const std::string& key, const json& v) { host::notify("rcfg.set", {{"key", key}, {"value", v}}); }
inline bool rconfig_has(const std::string& key) { auto r = host::call("rcfg.has", {{"key", key}}); return r.is_boolean() && r.get<bool>(); }
inline void rconfig_clear(const std::string& key) { host::notify("rcfg.clear", {{"key", key}}); }

// ── Boot config (read-only) ──────────────────────────────────────────────────────
inline std::string boot_get(const std::string& key) { auto r = host::call("boot.get", {{"key", key}}); return r.is_string() ? r.get<std::string>() : std::string{}; }
inline bool        boot_has(const std::string& key) { auto r = host::call("boot.has", {{"key", key}}); return r.is_boolean() && r.get<bool>(); }

// ── Shared JSON symbol table ─────────────────────────────────────────────────────
inline void sym_set(const std::string& name, const json& v) { host::notify("sym.set", {{"name", name}, {"value", v}}); }
inline json sym_get(const std::string& name) { return host::call("sym.get", {{"name", name}}); }

// ── Game client (requires CLIENT permission) ──────────────────────────────────────
inline json client_call(const std::string& endpoint, const json& body = json::object()) {
    return host::call("client.call", {{"endpoint", endpoint}, {"body", body}});
}
inline std::vector<std::string> client_endpoints() {
    auto r = host::call("client.endpoints");
    std::vector<std::string> out;
    if (r.is_array()) for (const auto& e : r) out.push_back(e.get<std::string>());
    return out;
}

// ── Module management / queries ───────────────────────────────────────────────────
inline bool load_module(const std::string& path) { auto r = host::call("mod.load", {{"path", path}}); return r.is_boolean() && r.get<bool>(); }
inline void unload_module(const std::string& id) { host::notify("mod.unload", {{"id", id}}); }
inline bool reload_module(const std::string& id) { auto r = host::call("mod.reload", {{"id", id}}); return r.is_boolean() && r.get<bool>(); }
inline std::vector<std::string> module_list() {
    auto r = host::call("mod.list");
    std::vector<std::string> v;
    if (r.is_array()) for (const auto& e : r) v.push_back(e.get<std::string>());
    return v;
}
inline bool is_builtin(const std::string& id) { auto r = host::call("mod.is_builtin", {{"id", id}}); return r.is_boolean() && r.get<bool>(); }
inline bool is_kupd(const std::string& id)    { auto r = host::call("mod.is_kupd",    {{"id", id}}); return r.is_boolean() && r.get<bool>(); }

}

// ── Author-facing convenience macros ──────────────────────────────────────────────
#define KEMIT(ev, data)            ::kmod::emit((ev), (data))
#define KLISTEN(ev, fn)            ::kmod::listen((ev), (fn))
#define KREPLY(topic, fn)          ::kmod::reply((topic), (fn))
#define KREQUEST(topic, data)      ::kmod::request((topic), (data))
#define KCONF_GET(key)             ::kmod::conf_get((key))
#define KCONF_SET(key, val)        ::kmod::conf_set((key), (val))
#define KCONF_HAS(key)             ::kmod::conf_has((key))
#define KCONF_DEL(key)             ::kmod::conf_del((key))
#define KLOG_INFO(msg)             ::kmod::log_info((msg))
#define KLOG_WARN(msg)             ::kmod::log_warn((msg))
#define KLOG_ERROR(msg)            ::kmod::log_error((msg))

// ── MOD_REGISTER — emit the VM module exports + factory (exactly once) ─────────────
#define MOD_REGISTER(CLASS_NAME)                                                                  \
    namespace kmod { Module* __kmod_make_module() { return new CLASS_NAME(); } }                  \
    extern "C" {                                                                                  \
    __attribute__((export_name("__kmod_info")))    long long __kmod_info()       { return ::kmod::runtime().info_packed(); } \
    __attribute__((export_name("on_load")))        void      __kmod_on_load()    { ::kmod::runtime().on_load(); }            \
    __attribute__((export_name("on_unload")))      void      __kmod_on_unload()  { ::kmod::runtime().on_unload(); }          \
    __attribute__((export_name("__kmod_dispatch"))) long long __kmod_dispatch(int kind, int token, const char* args, int alen) { \
        return ::kmod::runtime().dispatch(kind, token, args, alen);                                \
    }                                                                                             \
    }
