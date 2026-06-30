// ═════════════════════════════════════════════════════════════════════════════

#include <kmod.hpp>

#include <string>
#include <vector>

class HelloWorld : public Module {
public:
    KMOD_MODULE_INFO(
        "com.example.hello",
        "Hello World",
        "1.0.0",
        "Example Author",
        "Minimal example module",
        KMOD_PERM_TOOLS | KMOD_PERM_CONFIG | KMOD_PERM_EVENTS_LISTEN | KMOD_PERM_EVENTS_EMIT,
        KMODF_DEV)

    void on_load() override {
        kmod::log_info("on_load", "HelloWorld");

        kmod::register_tool("hello", [this] {
            UI::clearScreen();
            UI::printHeader("Hello World", "Example Module", "1.0.0");
            std::vector<std::string> opts = {"Say hello", "Enter a name", "Back"};
            int sel = UI::choice("Action", opts);
            if (sel == 0) {
                UI::printSuccess("Hello from " + get_id() + "!");
                UI::pause();
            } else if (sel == 1) {
                std::string name = UI::input("Your name", "", "type here");
                UI::printSuccess("Hello, " + (name.empty() ? "stranger" : name) + "!");
                UI::pause();
            }
        });

        KLISTEN("app.ready", [](const nlohmann::json& data) {
            kmod::log_info("app.ready received: " + data.dump(), "HelloWorld");
        });

        kmod::register_config_bool(
            "verbose",
            [this] { return verbose_; },
            [this](bool v) { verbose_ = v; kmod::conf_set("verbose", v ? "1" : "0"); });

        auto saved = kmod::conf_get("verbose");
        if (saved) verbose_ = (*saved == "1");

        KEMIT("hello.loaded", (nlohmann::json{{"module", get_id()}}));
    }

    void on_unload() override { kmod::log_info("on_unload", "HelloWorld"); }

private:
    bool verbose_ = false;
};

MOD_REGISTER(HelloWorld)
