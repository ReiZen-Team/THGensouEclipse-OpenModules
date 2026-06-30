#pragma once
// ═════════════════════════════════════════════════════════════════════════════
//  ui.hpp  —  OpenModules SDK
// ═════════════════════════════════════════════════════════════════════════════

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "hostcall.hpp"

namespace UI {

// ─────────────────────────────────────────────────────────────────────────────
enum class Style { Plain, Muted, Success, Error, Warning, Info, Badge };
enum class FieldType { Text, Number, Password, Bool, Select };

struct MenuItem {
    std::string label;
    std::string desc;
};
struct MenuGroup {
    std::string           title;
    std::vector<MenuItem> items;
};
struct FormField {
    std::string              key;
    std::string              label;
    FieldType                type = FieldType::Text;
    std::string              defValue;
    std::string              placeholder;
    bool                     required = false;
    std::vector<std::string> options;
    double                   min     = 0;
    double                   max     = 0;
    double                   step    = 1;
    bool                     integer = true;
};
struct PanelStat {
    std::string label;
    std::string value;
    std::string hint;
    Style       style = Style::Plain;
};
struct PanelAction {
    std::string label;
    std::string desc;
    bool        danger = false;
};

namespace detail {

inline nlohmann::json call(const std::string& verb, const nlohmann::json& args) {
    return host::call("ui." + verb, args);
}

inline const char* style_name(Style s) {
    switch (s) {
        case Style::Muted:   return "muted";
        case Style::Success: return "success";
        case Style::Error:   return "error";
        case Style::Warning: return "warning";
        case Style::Info:    return "info";
        case Style::Badge:   return "badge";
        default:             return "plain";
    }
}
inline const char* field_name(FieldType t) {
    switch (t) {
        case FieldType::Number:   return "number";
        case FieldType::Password: return "password";
        case FieldType::Bool:     return "bool";
        case FieldType::Select:   return "select";
        default:                  return "text";
    }
}

}

// ─────────────────────────────────────────────────────────────────────────────
inline bool isWeb() {
    return detail::call("isWeb", nlohmann::json::object()) == true;
}

inline void clearScreen() { detail::call("clear", nlohmann::json::object()); }

inline void printHeader(const std::string& title, const std::string& subtitle = "", const std::string& version = "") {
    detail::call("header", {{"title", title}, {"subtitle", subtitle}, {"version", version}});
}

inline void println(const std::string& s = "")    { detail::call("line", {{"style", "plain"},   {"text", s}}); }
inline void printMuted(const std::string& s)       { detail::call("line", {{"style", "muted"},   {"text", s}}); }
inline void printSuccess(const std::string& s)     { detail::call("line", {{"style", "success"}, {"text", s}}); }
inline void printError(const std::string& s)       { detail::call("line", {{"style", "error"},   {"text", s}}); }
inline void printWarning(const std::string& s)     { detail::call("line", {{"style", "warning"}, {"text", s}}); }
inline void printInfo(const std::string& s)        { detail::call("line", {{"style", "info"},    {"text", s}}); }
inline void printBadge(const std::string& s)       { detail::call("line", {{"style", "badge"},   {"text", s}}); }
inline void printDivider(const std::string& l = ""){ detail::call("divider", {{"label", l}}); }

inline void logLine(Style style, const std::string& text) {
    detail::call("log", {{"style", detail::style_name(style)}, {"text", text}});
}

inline int choice(const std::string& prompt, const std::vector<std::string>& options) {
    auto r = detail::call("choice", {{"prompt", prompt}, {"options", options}});
    return r.is_number_integer() ? r.get<int>() : -1;
}

inline int menu(const std::string& prompt, const std::vector<MenuGroup>& groups) {
    nlohmann::json g = nlohmann::json::array();
    for (const auto& grp : groups) {
        nlohmann::json items = nlohmann::json::array();
        for (const auto& it : grp.items) items.push_back({{"label", it.label}, {"desc", it.desc}});
        g.push_back({{"title", grp.title}, {"items", items}});
    }
    auto r = detail::call("menu", {{"prompt", prompt}, {"groups", g}});
    return r.is_number_integer() ? r.get<int>() : -1;
}

inline std::vector<int> multiChoice(const std::string& prompt, const std::vector<std::string>& options,
                                    const std::vector<int>& preselected = {}, int maxSel = -1) {
    auto r = detail::call("multi", {{"prompt", prompt}, {"options", options}, {"preselected", preselected}, {"maxSel", maxSel}});
    std::vector<int> out;
    if (r.is_array())
        for (const auto& e : r) out.push_back(e.get<int>());
    return out;
}

inline std::string input(const std::string& prompt, const std::string& defValue = "",
                         const std::string& placeholder = "", bool password = false) {
    auto r = detail::call("input", {{"prompt", prompt}, {"default", defValue}, {"placeholder", placeholder}, {"password", password}});
    return r.is_string() ? r.get<std::string>() : std::string{};
}

inline double numberInput(const std::string& prompt, double def = 0, double min = 0, double max = 0,
                          double step = 1, bool integer = true, const std::string& placeholder = "") {
    auto r = detail::call("number", {{"prompt", prompt}, {"def", def}, {"min", min}, {"max", max}, {"step", step}, {"integer", integer}, {"placeholder", placeholder}});
    return r.is_number() ? r.get<double>() : def;
}

inline bool confirm(const std::string& prompt, bool defaultYes = true) {
    auto r = detail::call("confirm", {{"prompt", prompt}, {"defaultYes", defaultYes}});
    return r.is_boolean() ? r.get<bool>() : defaultYes;
}

inline void pause(const std::string& msg = "Press any key to continue\xe2\x80\xa6") {
    detail::call("pause", {{"msg", msg}});
}

inline nlohmann::json form(const std::string& title, const std::vector<FormField>& fields) {
    nlohmann::json fs = nlohmann::json::array();
    for (const auto& f : fields)
        fs.push_back({{"key", f.key}, {"label", f.label}, {"type", detail::field_name(f.type)}, {"default", f.defValue}, {"placeholder", f.placeholder}, {"required", f.required}, {"options", f.options}, {"min", f.min}, {"max", f.max}, {"step", f.step}, {"integer", f.integer}});
    return detail::call("form", {{"title", title}, {"fields", fs}});
}

inline std::vector<int> selectRows(const std::string& prompt, const std::vector<std::string>& columns,
                                   const std::vector<std::vector<std::string>>& rows, bool multi = false, int pageSize = 12) {
    auto r = detail::call("select", {{"prompt", prompt}, {"columns", columns}, {"rows", rows}, {"multi", multi}, {"pageSize", pageSize}});
    std::vector<int> out;
    if (r.is_array())
        for (const auto& e : r) out.push_back(e.get<int>());
    return out;
}

inline int selectRow(const std::string& prompt, const std::vector<std::string>& columns,
                     const std::vector<std::vector<std::string>>& rows) {
    auto v = selectRows(prompt, columns, rows, false);
    return v.empty() ? -1 : v[0];
}

inline nlohmann::json view(const std::string& name, const nlohmann::json& data = {}) {
    return detail::call("view", {{"name", name}, {"data", data}});
}

inline void tree(const std::string& title, const nlohmann::json& data) {
    detail::call("tree", {{"title", title}, {"data", data}});
}

// ── Multi-screen ──────────────────────────────────────────────────────────────
inline bool open_screen(const std::string& title) {
    auto r = detail::call("screen.open", {{"title", title}});
    return r.is_boolean() ? r.get<bool>() : false;
}
inline void close_screen() { detail::call("screen.close", nlohmann::json::object()); }

class ScreenScope {
    bool opened_;

 public:
    explicit ScreenScope(const std::string& title) : opened_(open_screen(title)) {}
    ~ScreenScope() { if (opened_) close_screen(); }

    ScreenScope(const ScreenScope&) = delete;
    ScreenScope& operator=(const ScreenScope&) = delete;

    bool opened() const { return opened_; }
};

// ─────────────────────────────────────────────────────────────────────────────
inline constexpr int kSubheaders = 64;

inline void subheader(int index, const std::string& text, Style style = Style::Plain) {
    detail::call("subheader", {{"index", index}, {"text", text}, {"style", detail::style_name(style)}});
}

inline void clearSubheaders() {
    detail::call("clearSubheaders", nlohmann::json::object());
}

// ─────────────────────────────────────────────────────────────────────────────
class Table {
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;

public:
    Table& header(std::vector<std::string> h) { headers_ = std::move(h); return *this; }
    Table& row(std::vector<std::string> r)    { rows_.push_back(std::move(r)); return *this; }
    void print() const {
        detail::call("table", {{"headers", headers_}, {"rows", rows_}});
    }
};

// ─────────────────────────────────────────────────────────────────────────────
class Panel {
    std::string              title_;
    std::string              subtitle_;
    std::vector<PanelStat>   stats_;
    std::vector<std::string> notes_;
    std::vector<PanelAction> actions_;

public:
    explicit Panel(std::string title, std::string subtitle = "") : title_(std::move(title)), subtitle_(std::move(subtitle)) {}

    Panel& stat(std::string label, std::string value, std::string hint = "", Style style = Style::Plain) {
        stats_.push_back({std::move(label), std::move(value), std::move(hint), style});
        return *this;
    }
    Panel& note(std::string text) {
        notes_.push_back(std::move(text));
        return *this;
    }
    Panel& action(std::string label, std::string desc = "", bool danger = false) {
        actions_.push_back({std::move(label), std::move(desc), danger});
        return *this;
    }
    int show() const {
        nlohmann::json st = nlohmann::json::array();
        for (const auto& s : stats_) st.push_back({{"label", s.label}, {"value", s.value}, {"hint", s.hint}, {"style", detail::style_name(s.style)}});
        nlohmann::json ac = nlohmann::json::array();
        for (const auto& a : actions_) ac.push_back({{"label", a.label}, {"desc", a.desc}, {"danger", a.danger}});
        auto r = detail::call("panel", {{"title", title_}, {"subtitle", subtitle_}, {"stats", st}, {"notes", notes_}, {"actions", ac}});
        return r.is_number_integer() ? r.get<int>() : -1;
    }
};

}
