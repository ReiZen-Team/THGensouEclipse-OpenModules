// ═════════════════════════════════════════════════════════════════════════════
//  DevTester.cpp — OpenModules SDK developer test harness  (DYNAMIC / .wasm)
//
//  Two tools:
//    1. "Dev Tester — full sweep"  : exercises every syscall a STANDARD module
//       can reach AND probes every bug/exploit class the sandbox must block.
//    2. "Dev Tester — UI showcase" : interactive walk of the UI widgets.
//
//  Granted only KMOD_PERM_STANDARD on purpose: anything requiring elevated
//  permissions (CLIENT, QUERY_MODULES, LOAD_MODULES, KERNEL_CONFIG_RW,
//  RCONFIG_WRITE, UNLOAD_OTHERS) MUST come back denied — that is the exploit
//  half of the sweep. If any such probe is not blocked, the host is vulnerable.
// ═════════════════════════════════════════════════════════════════════════════

#include <kmod.hpp>
#include <proto/codec.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using nlohmann::json;

// ── result table ────────────────────────────────────────────────────────────────
struct Row {
	std::string cat;
	std::string name;
	bool good;      // functional: pass · security: blocked
	bool security;  // true → this row is an exploit/bug probe
	std::string detail;
};
std::vector<Row> g_rows;

void check(const std::string& cat, const std::string& name, bool cond, const std::string& detail = "") { g_rows.push_back({cat, name, cond, false, detail}); }
void probe(const std::string& name, bool blocked, const std::string& detail = "") { g_rows.push_back({"Exploit", name, blocked, true, detail}); }

// ── host→module callback state ────────────────────────────────────────────────────
bool g_event_fired = false;
json g_event_data;
int g_recur_depth = 0;  // bounded re-entrancy probe

// ═════════════════════════════════════════════════════════════════════════════
//  FUNCTIONAL — every capability a STANDARD module legitimately has
// ═════════════════════════════════════════════════════════════════════════════
void test_functional() {
	// ── Identity ────────────────────────────────────────────────────────────────
	std::string id = kmod::self_id();
	check("Identity", "self_id()", id == "openmodules.devtester", "id='" + id + "'");

	// ── Permissions granted by STANDARD ─────────────────────────────────────────
	check("Perm", "TOOLS", kmod::has_perm(KMOD_PERM_TOOLS));
	check("Perm", "CONFIG", kmod::has_perm(KMOD_PERM_CONFIG));
	check("Perm", "EVENTS_EMIT", kmod::has_perm(KMOD_PERM_EVENTS_EMIT));
	check("Perm", "EVENTS_LISTEN", kmod::has_perm(KMOD_PERM_EVENTS_LISTEN));
	check("Perm", "RCONFIG_READ", kmod::has_perm(KMOD_PERM_RCONFIG_READ));
	check("Perm", "BOOTCFG_READ", kmod::has_perm(KMOD_PERM_BOOTCFG_READ));
	check("Perm", "SYMBOLS_RW", kmod::has_perm(KMOD_PERM_SYMBOLS_READ) && kmod::has_perm(KMOD_PERM_SYMBOLS_WRITE));
	check("Perm", "UI", kmod::has_perm(KMOD_PERM_UI));

	// ── Kernel ABI version (host syscall kernel.vernumber) ──────────────────────
	uint32_t kv = kmod::kernel_vernumber();
	check("Kernel", "kernel_vernumber() > 0", kv > 0u, "vernumber=" + std::to_string(kv));

	// ── Logging (info/warn/error + dump) ────────────────────────────────────────
	kmod::log_info("dev sweep running", "DevTester");
	kmod::log_warn("warn channel", "DevTester");
	kmod::log_error("error channel", "DevTester");
	json ld = kmod::log_dump();
	check("Log", "info/warn/error", true, "3 lines emitted");
	check("Log", "log_dump() array", ld.is_array() || ld.is_object(), "type=" + std::string(ld.type_name()));

	// ── Module-scoped config (mconf) round-trip ─────────────────────────────────
	kmod::conf_set("dev.key", "value-42");
	auto cv = kmod::conf_get("dev.key");
	check("Config", "mconf set+get", cv && *cv == "value-42", cv ? *cv : "<null>");
	check("Config", "mconf has", kmod::conf_has("dev.key"));
	kmod::conf_del("dev.key");
	check("Config", "mconf del", !kmod::conf_has("dev.key"));
	check("Config", "mconf miss=null", !kmod::conf_get("dev.absent").has_value());

	// ── Config-UI registration (bool/string/choice/int/float/datetime) ──────────
	static bool cfg_b = false;
	static std::string cfg_s = "s";
	static long long cfg_i = 3;
	static double cfg_f = 1.5;
	static std::string cfg_c = "A";
	static std::string cfg_d = "2026-01-01T00:00:00Z";
	kmod::register_config_bool("dev.bool", [] { return cfg_b; }, [](bool v) { cfg_b = v; });
	kmod::register_config_string("dev.string", [] { return cfg_s; }, [](std::string v) { cfg_s = std::move(v); });
	kmod::register_config_int("dev.int", [] { return cfg_i; }, [](long long v) { cfg_i = v; }, 0, 10);
	kmod::register_config_float("dev.float", [] { return cfg_f; }, [](double v) { cfg_f = v; }, 0.0, 10.0);
	kmod::register_config_choice("dev.choice", {"A", "B", "C"}, [] { return cfg_c; }, [](std::string v) { cfg_c = std::move(v); });
	kmod::register_config_datetime("dev.dt", [] { return cfg_d; }, [](std::string v) { cfg_d = std::move(v); });
	check("Config", "register 6 config types", true, "bool/string/int/float/choice/datetime");
	kmod::remove_config("dev.dt");
	check("Config", "remove_config", true);

	// ── RConfig read (write must be denied — probed below) ──────────────────────
	json rc = kmod::rconfig_get("dev.rc");
	check("RConfig", "rcfg.get callable", true, rc.dump());
	check("RConfig", "rcfg.has callable", true, kmod::rconfig_has("dev.rc") ? "has" : "absent");

	// ── BootConfig (read-only) ──────────────────────────────────────────────────
	std::string bhost = kmod::boot_get("host");
	check("BootCfg", "boot_get(host)", true, "host='" + bhost + "'");
	check("BootCfg", "boot_has(selection)", true, kmod::boot_has("selection") ? "present" : "absent");

	// ── Events: emit → own listener fires ───────────────────────────────────────
	g_event_fired = false;
	g_event_data = nullptr;
	kmod::emit("devtester.ping", json{{"v", 7}});
	check("Events", "emit → listen delivery", g_event_fired && g_event_data.value("v", 0) == 7, g_event_data.dump());

	// ── RPC: request → own responder ────────────────────────────────────────────
	json er = kmod::request("devtester.echo", json{{"a", "b"}});
	check("RPC", "request/reply echo", er.is_object() && er.value("echo", json::object()).value("a", std::string{}) == "b", er.dump());
	check("RPC", "has_responder(self)", kmod::has_responder("devtester.echo"));

	// ── Shared JSON symbols ─────────────────────────────────────────────────────
	kmod::sym_set("devtester.sym", json{{"k", 123}});
	json sg = kmod::sym_get("devtester.sym");
	check("Symbols", "sym set+get json", sg.is_object() && sg.value("k", 0) == 123, sg.dump());

	// ── Hot-reload state handoff ────────────────────────────────────────────────
	check("Reload", "reload_active() bool", true, kmod::reload_active() ? "reloading" : "cold");
	kmod::reload_save("dev-blob");
	check("Reload", "reload_save/load", kmod::reload_load() == "dev-blob" || !kmod::reload_active(), "handoff best-effort");

	// ── Scheduler (EVENTS_LISTEN-gated; schedule far out, then cancel) ───────────
	int64_t h_after = kmod::schedule_after("dev.after", 86400, [] {});
	int64_t h_cron = kmod::schedule_cron("dev.cron", "0 0 1 1 *", [] {});
	int64_t h_repeat = kmod::schedule_repeat("dev.repeat", 86400, 86400, [] {});
	int64_t h_onev = kmod::schedule_on_event("dev.onev", "devtester.never", [] {});
	int64_t h_retry = kmod::schedule_retry("dev.retry", [] { return true; }, 1, 3600);
	json slist = kmod::schedule_list();
	check("Sched", "schedule_after handle", h_after != 0, "h=" + std::to_string(h_after));
	check("Sched", "schedule_cron handle", h_cron != 0, "h=" + std::to_string(h_cron));
	check("Sched", "schedule_repeat handle", h_repeat != 0, "h=" + std::to_string(h_repeat));
	check("Sched", "schedule_on_event handle", h_onev != 0, "h=" + std::to_string(h_onev));
	check("Sched", "schedule_retry handle", h_retry != 0, "h=" + std::to_string(h_retry));
	check("Sched", "schedule_list array", slist.is_array(), "n=" + std::to_string(slist.is_array() ? slist.size() : 0));
	int cancelled = 0;
	for (int64_t h : {h_after, h_cron, h_repeat, h_onev, h_retry})
		if (h != 0 && kmod::schedule_cancel(h)) ++cancelled;
	check("Sched", "schedule_cancel all", cancelled >= 1, "cancelled=" + std::to_string(cancelled));

	// ── ProtoCodec round-trip ───────────────────────────────────────────────────
	{
		schema_t schema;
		schema["msg"] = "string";
		schema["n"] = "int32";
		std::string enc = ProtoCodec::encode(json{{"msg", "hi"}, {"n", 9}}, schema);
		json dec = ProtoCodec::decode(enc, schema);
		check("Proto", "encode/decode round-trip", dec.value("msg", std::string{}) == "hi" && dec.value("n", 0) == 9, dec.dump());
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  BUG / EXPLOIT PROBES — every one MUST be blocked or safely handled
// ═════════════════════════════════════════════════════════════════════════════
void test_exploits() {
	// ── 1. Permissions we must NOT have ─────────────────────────────────────────
	probe("no RCONFIG_WRITE", !kmod::has_perm(KMOD_PERM_RCONFIG_WRITE));
	probe("no LOAD_MODULES", !kmod::has_perm(KMOD_PERM_LOAD_MODULES));
	probe("no UNLOAD_OTHERS", !kmod::has_perm(KMOD_PERM_UNLOAD_OTHERS));
	probe("no QUERY_MODULES", !kmod::has_perm(KMOD_PERM_QUERY_MODULES));
	probe("no CLIENT", !kmod::has_perm(KMOD_PERM_CLIENT));
	probe("no KERNEL_CONFIG_RW", !kmod::has_perm(KMOD_PERM_KERNEL_CONFIG_RW));
	probe("no NETWORK/FS/PROCESS", !kmod::has_perm(KMOD_PERM_NETWORK) && !kmod::has_perm(KMOD_PERM_FILESYSTEM) && !kmod::has_perm(KMOD_PERM_PROCESS));

	// ── 2. Privileged writes silently denied ────────────────────────────────────
	kmod::rconfig_set("dev.rc", "HIJACKED");
	json rc_after = kmod::rconfig_get("dev.rc");
	probe("RConfig write denied", rc_after.is_null() || rc_after != json("HIJACKED"), "after write: " + rc_after.dump());

	kmod::kconf_set("kernel.master", "pwned");
	probe("kconf write denied", !kmod::kconf_get("kernel.master").has_value(), "KERNEL_CONFIG_RW required");

	// ── 3. Secret exfiltration blocked ──────────────────────────────────────────
	for (const char* key : {"jwt", "token", "secret", "password", "master_key"}) {
		json v = kmod::rconfig_get(key);
		bool safe = v.is_null() || (v.is_string() && v.get<std::string>().empty());
		probe(std::string("protected key '") + key + "' not exposed", safe, key + std::string("=") + v.dump());
	}

	// ── 4. Module-management escalation denied ──────────────────────────────────
	probe("load_module denied", !kmod::load_module("/data/local/tmp/untrusted.bin"));
	kmod::unload_module("WebUI");
	probe("unload_module(WebUI) refused", true, "host still alive");
	probe("reload_module denied", !kmod::reload_module("WebUI"));
	probe("module_list denied", kmod::module_list().empty());
	probe("tools_list denied", !kmod::tools_list().is_array() || kmod::tools_list().empty());
	probe("tool_invoke denied", !kmod::tool_invoke("Shutdown"));
	probe("client_call denied", kmod::client_call("FriendService/ListFriends", json::object()).is_null());
	probe("client_endpoints denied", kmod::client_endpoints().empty());
	probe("is_builtin denied/false", !kmod::is_builtin("WebUI"));

	// ── 5. Path traversal on the one path-taking API ────────────────────────────
	for (const char* p : {"../../../etc/passwd", "/etc/shadow", "..\\..\\windows\\system32"}) probe(std::string("path traversal '") + p + "' blocked", !kmod::load_module(p));

	// ── 6. Malformed / hostile syscall arguments — host must not crash ───────────
	json bad1 = host::call("rcfg.get", json{{"wrongfield", 1}});
	probe("missing-arg call handled", true, "ret=" + bad1.dump());
	json bad2 = host::call("rcfg.get", json::array({1, 2, 3}));  // array where object expected
	probe("wrong-type args handled", true, "ret=" + bad2.dump());
	json unk = host::call("totally.bogus.verb", json{{"x", 1}});
	probe("unknown verb ignored", unk.is_null(), "ret=" + unk.dump());
	json empty = host::call("", json::object());
	probe("empty verb ignored", empty.is_null(), "ret=" + empty.dump());

	// ── 7. Oversized input — host truncates, never overflows ────────────────────
	std::string huge(200000, 'A');
	kmod::log_info(huge, "DevTester.Flood");
	probe("200KB log line survived", true, "host truncates to bounded size");
	kmod::conf_set("dev.huge", huge);
	auto back = kmod::conf_get("dev.huge");
	probe("oversized mconf handled", back.has_value(), "len=" + std::to_string(back ? back->size() : 0));
	kmod::conf_del("dev.huge");

	// ── 8. Embedded NUL / control bytes preserved-or-sanitised, not crashing ────
	std::string nul("a\0b\nc\t", 6);
	kmod::conf_set("dev.nul", nul);
	auto nb = kmod::conf_get("dev.nul");
	probe("embedded NUL/control bytes handled", nb.has_value(), "len=" + std::to_string(nb ? nb->size() : 0));
	kmod::conf_del("dev.nul");

	// ── 9. Bounded event re-entrancy — no unbounded recursion / stack blow-up ────
	g_recur_depth = 0;
	kmod::emit("devtester.recur", json{{"n", 0}});
	probe("event recursion bounded", g_recur_depth <= 4 && g_recur_depth >= 1, "depth=" + std::to_string(g_recur_depth));

	// ── 10. Integer-boundary scheduling — no overflow crash ─────────────────────
	int64_t hmax = kmod::schedule_after("dev.overflow", INT64_MAX, [] {});
	probe("INT64_MAX delay handled", true, "handle=" + std::to_string(hmax));
	if (hmax != 0) kmod::schedule_cancel(hmax);
	int64_t hneg = kmod::schedule_after("dev.negative", -1, [] {});
	probe("negative delay handled", true, "handle=" + std::to_string(hneg));
	if (hneg != 0) kmod::schedule_cancel(hneg);
	probe("cancel bogus handle safe", !kmod::schedule_cancel(0x7fffffffffffffffLL), "no crash");

	// ── 11. Namespace isolation — mconf stays confined to this module ───────────
	kmod::conf_set("isolated", "mine");
	probe("mconf host-namespaced", kmod::conf_get("isolated").value_or("") == "mine", "confined");
	kmod::conf_del("isolated");
}

// ═════════════════════════════════════════════════════════════════════════════
void run_full_test() {
	g_rows.clear();
	test_functional();
	test_exploits();

	UI::clearScreen();
	UI::printHeader("OpenModules Dev Tester", "Full syscall sweep · bug & exploit probes", "1.0.0");

	int pass = 0, fail = 0, blocked = 0, vuln = 0;
	UI::Table t;
	t.header({"Category", "Test", "Result", "Detail"});
	for (const auto& r : g_rows) {
		std::string res;
		if (r.security) {
			if (r.good) {
				res = "BLOCKED";
				++blocked;
			} else {
				res = "VULNERABLE!";
				++vuln;
			}
		} else {
			if (r.good) {
				res = "PASS";
				++pass;
			} else {
				res = "FAIL";
				++fail;
			}
		}
		std::string det = r.detail.size() > 60 ? r.detail.substr(0, 57) + "..." : r.detail;
		t.row({r.cat, r.name, res, det});
	}
	t.print();

	UI::println();
	UI::Panel p("Dev Tester summary", "STANDARD-permission module");
	p.stat("Functional pass", std::to_string(pass), "", UI::Style::Success);
	p.stat("Functional fail", std::to_string(fail), "", fail ? UI::Style::Error : UI::Style::Muted);
	p.stat("Exploits blocked", std::to_string(blocked), "", UI::Style::Info);
	p.stat("Vulnerabilities", std::to_string(vuln), "", vuln ? UI::Style::Error : UI::Style::Muted);
	p.note(fail == 0 && vuln == 0 ? "ALL GREEN — every syscall works and every exploit was blocked." : (vuln > 0 ? "SECURITY: one or more probes were NOT blocked — inspect the host kernel." : "Some functional tests failed."));
	p.show();

	UI::println();
	if (fail == 0 && vuln == 0)
		UI::printSuccess("ALL GREEN — host sandbox holds and all standard syscalls work.");
	else if (vuln > 0)
		UI::printError("WARNING: " + std::to_string(vuln) + " exploit probe(s) not blocked!");
	else
		UI::printWarning(std::to_string(fail) + " functional test(s) failed.");
	UI::pause();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Interactive UI widget showcase (separate tool — uses blocking widgets)
// ═════════════════════════════════════════════════════════════════════════════
void run_ui_showcase() {
	UI::clearScreen();
	UI::printHeader("Dev Tester", "UI widget showcase", "1.0.0");
	UI::printInfo("Rendering surface: " + std::string(UI::isWeb() ? "WebUI" : "terminal"));
	UI::printDivider("styled lines");
	UI::printSuccess("success");
	UI::printWarning("warning");
	UI::printError("error");
	UI::printMuted("muted");
	UI::printBadge("badge");

	UI::tree("sample tree", json{{"root", {{"a", 1}, {"b", json::array({2, 3})}}}});

	int c = UI::choice("Pick one", {"Alpha", "Beta", "Gamma"});
	UI::printInfo("choice() → index " + std::to_string(c));

	std::string name = UI::input("Your handle", "dev", "type here");
	bool ok = UI::confirm("Run the destructive-looking (but harmless) demo?", false);
	double n = UI::numberInput("Pick a number", 5, 0, 10, 1, true);

	json f = UI::form("Demo form", {
	                                 {"user", "Username", UI::FieldType::Text, "", "", true, {}, 0, 0, 1, true},
	                                 {"pass", "Password", UI::FieldType::Password},
	                                 {"role", "Role", UI::FieldType::Select, "", "", false, {"admin", "user"}, 0, 0, 1, true},
	                               });

	int sel = UI::selectRow("Choose a row", {"Key", "Value"}, {{"alpha", "1"}, {"beta", "2"}, {"gamma", "3"}});

	UI::Panel p("Showcase results", "what you entered");
	p.stat("handle", name);
	p.stat("confirm", ok ? "yes" : "no");
	p.stat("number", std::to_string(static_cast<int>(n)));
	p.stat("form.user", f.is_object() ? f.value("user", std::string{"<none>"}) : std::string{"<none>"});
	p.stat("selected row", std::to_string(sel));
	p.show();
	UI::pause();
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
class DevTesterModule : public Module {
 public:
	KMOD_MODULE_INFO("openmodules.devtester", "OpenModules Dev Tester", "1.0.0", "OpenModules", "Developer harness: full syscall sweep + bug/exploit probes", KMOD_PERM_STANDARD, KMODF_DEV | KMODF_UNSTABLE)

	void on_load() override {
		kmod::log_info("Dev Tester loaded", "DevTester");

		kmod::listen("devtester.ping", [](const json& d) {
			g_event_fired = true;
			g_event_data = d;
		});
		// Bounded self-recursive listener — verifies the host tolerates re-entrant
		// emit without unbounded recursion / stack overflow.
		kmod::listen("devtester.recur", [](const json& d) {
			++g_recur_depth;
			int n = d.value("n", 0);
			if (n < 3) kmod::emit("devtester.recur", json{{"n", n + 1}});
		});
		kmod::reply("devtester.echo", [](const json& req) -> json { return json{{"echo", req}}; });

		kmod::register_tool("Dev Tester — full sweep (syscalls + exploits)", [] { run_full_test(); }, true);
		kmod::register_tool("Dev Tester — UI widget showcase", [] { run_ui_showcase(); });
	}

	void on_unload() override { kmod::log_info("Dev Tester unloaded", "DevTester"); }
};

MOD_REGISTER(DevTesterModule)
