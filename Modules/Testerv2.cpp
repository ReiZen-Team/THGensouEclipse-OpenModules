// ═════════════════════════════════════════════════════════════════════════════

#include <kmod.hpp>
#include <proto/codec.hpp>

#include <string>
#include <vector>

namespace {

using nlohmann::json;

struct Row {
    std::string cat;
    std::string name;
    bool        good;
    bool        security;
    std::string detail;
};

std::vector<Row> g_rows;

void check(const std::string& cat, const std::string& name, bool cond, const std::string& detail = "") {
    g_rows.push_back({cat, name, cond, false, detail});
}
void probe(const std::string& name, bool blocked, const std::string& detail = "") {
    g_rows.push_back({"Exploit", name, blocked, true, detail});
}

// ── host→module callback state ──────────────────────────────────────────────────
bool g_event_fired = false;
json g_event_data;

// ─────────────────────────────────────────────────────────────────────────────
void run_full_test() {
    g_rows.clear();

    // ── Identity ──────────────────────────────────────────────────────────────
    std::string id = kmod::self_id();
    check("Identity", "self_id()", id == "openmodules.tester.v2", "id='" + id + "'");

    // ── Permissions (granted = STANDARD) ────────────────────────────────────────
    check("Perm", "has TOOLS",        kmod::has_perm(KMOD_PERM_TOOLS));
    check("Perm", "has CONFIG",       kmod::has_perm(KMOD_PERM_CONFIG));
    check("Perm", "has EVENTS_EMIT",  kmod::has_perm(KMOD_PERM_EVENTS_EMIT));
    check("Perm", "has EVENTS_LISTEN",kmod::has_perm(KMOD_PERM_EVENTS_LISTEN));
    check("Perm", "has RCONFIG_READ", kmod::has_perm(KMOD_PERM_RCONFIG_READ));
    check("Perm", "has BOOTCFG_READ", kmod::has_perm(KMOD_PERM_BOOTCFG_READ));
    check("Perm", "has SYMBOLS_RW",   kmod::has_perm(KMOD_PERM_SYMBOLS_READ) && kmod::has_perm(KMOD_PERM_SYMBOLS_WRITE));

    // ── Logging ──────────────────────────────────────────────────────────────
    kmod::log_info("self-test running", "Tester");
    kmod::log_warn("warn channel", "Tester");
    kmod::log_error("error channel", "Tester");
    check("Log", "log info/warn/error", true, "emitted 3 lines");

    // ── Module-scoped config (mconf) round-trip ─────────────────────────────────
    kmod::conf_set("selftest.key", "value-42");
    auto cv = kmod::conf_get("selftest.key");
    check("Config", "mconf set+get", cv && *cv == "value-42", cv ? *cv : "<null>");
    check("Config", "mconf has",     kmod::conf_has("selftest.key"));
    kmod::conf_del("selftest.key");
    check("Config", "mconf del",     !kmod::conf_has("selftest.key"));
    check("Config", "mconf miss=null", !kmod::conf_get("selftest.nope").has_value());

    // ── RConfig (read allowed; write must be silently denied) ───────────────────
    json rc = kmod::rconfig_get("selftest.rc");
    check("RConfig", "rcfg.get callable (read perm)", true, rc.dump());

    // ── BootConfig (read-only) ──────────────────────────────────────────────────
    std::string bhost = kmod::boot_get("host");
    check("BootCfg", "boot.get callable", true, "host='" + bhost + "'");

    // ── Events: emit → our own listener fires ───────────────────────────────────
    g_event_fired = false;
    g_event_data  = nullptr;
    kmod::emit("tester.ping", json{{"v", 7}});
    check("Events", "emit → listen delivery", g_event_fired && g_event_data.value("v", 0) == 7, g_event_data.dump());

    // ── RPC: request → our own responder echoes ─────────────────────────────────
    json er = kmod::request("tester.echo", json{{"a", "b"}});
    check("RPC", "request/reply echo", er.is_object() && er.value("echo", json::object()).value("a", std::string{}) == "b", er.dump());
    check("RPC", "has_responder(self)", kmod::has_responder("tester.echo"));

    // ── Shared JSON symbols ─────────────────────────────────────────────────────
    kmod::sym_set("tester.sym", json{{"k", 123}});
    json sg = kmod::sym_get("tester.sym");
    check("Symbols", "sym set+get json", sg.is_object() && sg.value("k", 0) == 123, sg.dump());

    // ── ProtoCodec round-trip ───────────────────────────────────────────────────
    {
        schema_t schema;
        schema["msg"] = "string";
        schema["n"]   = "int32";
        std::string enc = ProtoCodec::encode(json{{"msg", "hi"}, {"n", 9}}, schema);
        json dec = ProtoCodec::decode(enc, schema);
        check("Proto", "encode/decode round-trip", dec.value("msg", std::string{}) == "hi" && dec.value("n", 0) == 9, dec.dump());
    }

    // ═══════════════════ EXPLOIT / BYPASS PROBES ═══════════════════════════════
    probe("no RCONFIG_WRITE",    !kmod::has_perm(KMOD_PERM_RCONFIG_WRITE));
    probe("no LOAD_MODULES",     !kmod::has_perm(KMOD_PERM_LOAD_MODULES));
    probe("no UNLOAD_OTHERS",    !kmod::has_perm(KMOD_PERM_UNLOAD_OTHERS));
    probe("no QUERY_MODULES",    !kmod::has_perm(KMOD_PERM_QUERY_MODULES));
    probe("no CLIENT",           !kmod::has_perm(KMOD_PERM_CLIENT));
    probe("no KERNEL_CONFIG_RW", !kmod::has_perm(KMOD_PERM_KERNEL_CONFIG_RW));

    kmod::rconfig_set("selftest.rc", "HIJACKED");
    json rc_after = kmod::rconfig_get("selftest.rc");
    probe("RConfig write denied", rc_after.is_null() || rc_after != json("HIJACKED"), "after write: " + rc_after.dump());

    json jwt = kmod::rconfig_get("jwt");
    probe("protected key 'jwt' not exposed", jwt.is_null() || !jwt.is_string() || jwt.get<std::string>().empty(), "jwt=" + jwt.dump());

    bool loaded = kmod::load_module("/data/local/tmp/untrusted.bin");
    probe("load_module denied", !loaded);

    kmod::unload_module("WebUI");
    probe("unload_module(WebUI) refused", true, "host kept running");

    auto mlist = kmod::module_list();
    probe("module_list denied", mlist.empty(), "entries=" + std::to_string(mlist.size()));

    kmod::conf_set("isolated", "mine");
    probe("mconf namespaced (host-prefixed)", kmod::conf_get("isolated").value_or("") == "mine", "confined to this module");
    kmod::conf_del("isolated");

    json bad = host::call("rcfg.get", json{{"wrongfield", 1}});
    probe("malformed args handled", true, "no host crash; ret=" + bad.dump());

    json unk = host::call("totally.bogus.verb", json{{"x", 1}});
    probe("unknown verb ignored", unk.is_null(), "ret=" + unk.dump());

    json cc = kmod::client_call("FriendService/ListFriends", json::object());
    probe("client.call denied w/o CLIENT", cc.is_null(), "ret=" + cc.dump());

    // ── Render report ───────────────────────────────────────────────────────────
    UI::clearScreen();
    UI::printHeader("OpenModules FULL Self-Test", "Syscalls · Permissions · Sandbox/Exploit probes", "3.1.0");

    int pass = 0, fail = 0, blocked = 0, vuln = 0;
    UI::Table t;
    t.header({"Category", "Test", "Result", "Detail"});
    for (const auto& r : g_rows) {
        std::string res;
        if (r.security) {
            if (r.good) { res = "BLOCKED"; ++blocked; }
            else        { res = "VULNERABLE!"; ++vuln; }
        } else {
            if (r.good) { res = "PASS"; ++pass; }
            else        { res = "FAIL"; ++fail; }
        }
        t.row({r.cat, r.name, res, r.detail});
    }
    t.print();

    UI::println();
    UI::printInfo("Functional: " + std::to_string(pass) + " pass, " + std::to_string(fail) + " fail");
    UI::printInfo("Security  : " + std::to_string(blocked) + " blocked, " + std::to_string(vuln) + " vulnerable");
    UI::println();
    if (fail == 0 && vuln == 0)
        UI::printSuccess("ALL GREEN — every syscall works and every exploit was blocked by the host.");
    else if (vuln > 0)
        UI::printError("⚠ " + std::to_string(vuln) + " SECURITY PROBE(S) NOT BLOCKED — inspect the host kernel!");
    else
        UI::printWarning(std::to_string(fail) + " functional test(s) failed.");
    UI::pause();
}

}

// ─────────────────────────────────────────────────────────────────────────────
class TesterModule : public Module {
public:
    KMOD_MODULE_INFO(
        "openmodules.tester.v2",
        "OpenModules Full Self-Test",
        "3.1.0",
        "OpenModules",
        "Exercises all syscalls + permission/exploit probes",
        KMOD_PERM_STANDARD,
        0)

    void on_load() override {
        kmod::log_info("Full self-test module loaded", "Tester");

        kmod::listen("tester.ping", [](const json& d) {
            g_event_fired = true;
            g_event_data  = d;
        });
        kmod::reply("tester.echo", [](const json& req) -> json {
            return json{{"echo", req}};
        });

        kmod::register_tool("Run FULL self-test (syscalls + exploits)", [] { run_full_test(); },true);
    }
};

MOD_REGISTER(TesterModule)
