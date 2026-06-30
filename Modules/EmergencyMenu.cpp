// ═════════════════════════════════════════════════════════════════════════════

#include <kmod.hpp>

#include <string>
#include <vector>

class EmergencyToolAccess : public Module {
public:
    KMOD_MODULE_INFO(
        "EmergencyAccess",
        "Emergency Tool Access Menu",
        "1.0.0",
        "ReiZen-Team",
        "One-shot menu for direct access to system tools",
        KMOD_PERM_QUERY_MODULES | KMOD_PERM_CLIENT | KMOD_PERM_UI | KMOD_PERM_TOOLS,
        KMODF_LIVE)

    void on_load() override {
        while (true) {
            UI::clearScreen();
            UI::printHeader("EMERGENCY TOOLS", "Direct access");
            UI::println();

            auto tools = kmod::tools_list();
            if (!tools.is_array() || tools.empty()) {
                UI::printInfo("No Tools or Special Tools currently registered.");
                UI::println();
            }

            std::vector<std::string> opts;
            for (const auto& t : tools) {
                std::string tag = t.value("special", false) ? "[Special] " : "[Tool] ";
                opts.push_back(tag + t.value("name", std::string{}));
            }
            opts.push_back("Exit Menu");

            int sel = UI::choice("Select tool to execute", opts);
            if (sel < 0 || sel >= (int)tools.size()) break;

            const auto& t = tools[sel];
            kmod::tool_invoke(t.value("name", std::string{}), t.value("special", false));
        }
    }
};

MOD_REGISTER(EmergencyToolAccess)
