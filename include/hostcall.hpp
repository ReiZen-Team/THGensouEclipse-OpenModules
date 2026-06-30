#pragma once
// ═════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace host {

extern "C" {
__attribute__((import_module("env"), import_name("host_syscall")))
long long __host_syscall(const char* verb, int verb_len, const char* args, int args_len);

__attribute__((import_module("env"), import_name("host_free")))
void __host_free(unsigned result_off);
}

// ─────────────────────────────────────────────────────────────────────────────
inline nlohmann::json call(const std::string& verb, const nlohmann::json& args = nlohmann::json(nullptr)) {
    std::string a = args.is_null() ? std::string{} : args.dump();
    long long   r = __host_syscall(verb.data(), static_cast<int>(verb.size()),
                                   a.empty() ? "" : a.data(), static_cast<int>(a.size()));
    if (r == 0) return nullptr;

    unsigned long long u   = static_cast<unsigned long long>(r);
    unsigned           off = static_cast<unsigned>(u >> 32);
    unsigned           len = static_cast<unsigned>(u & 0xFFFFFFFFu);
    if (off == 0 || len == 0) return nullptr;

    const char*    p   = reinterpret_cast<const char*>(static_cast<uintptr_t>(off));
    nlohmann::json out  = nlohmann::json::parse(std::string(p, len), nullptr, false);
    if (out.is_discarded()) out = nullptr;
    __host_free(off);
    return out;
}

inline void notify(const std::string& verb, const nlohmann::json& args = nlohmann::json(nullptr)) {
    call(verb, args);
}

}
