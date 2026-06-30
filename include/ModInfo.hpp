#pragma once
// ═════════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────

struct KModInfo {
    // ── Header ──────────
    char     magic[8];
    uint16_t struct_ver;
    uint16_t flags;
    uint32_t _reserved0;
    uint64_t permissions;

    // ── Identity ──────────
    char id[64];
    char name[128];
    char mod_version[32];
    char author[64];
    char description[256];

    // ── Type tag ───────────
    char type_tag[16];

    // ── Dependencies ───────
    char depends[256];

    static constexpr char     MAGIC[8]      = {'K','M','O','D','I','N','F','O'};
    static constexpr uint16_t CURRENT_VER   = 3;

    bool is_valid() const {
        return std::memcmp(magic, MAGIC, 8) == 0
            && struct_ver == CURRENT_VER;
    }
};

static_assert(sizeof(KModInfo) == 840, "KModInfo layout changed — update CURRENT_VER");

// ─────────────────────────────────────────────────────────────────────────────

enum KModFlag : uint16_t {
    KMODF_NONE     = 0,
    KMODF_DEV      = 1 << 0,
    KMODF_UNSTABLE = 1 << 1,
    KMODF_HIDDEN   = 1 << 2,
    KMODF_AUTOLOAD = 1 << 3,
    KMODF_LIVE     = 1 << 4,
};

// ─────────────────────────────────────────────────────────────────────────────

enum KModPerm : uint64_t {
    KMOD_PERM_NONE = 0,

    // ── Kernel API capabilities ───────────────────────────────────────────────
    KMOD_PERM_TOOLS         = 1ull << 0,
    KMOD_PERM_CONFIG        = 1ull << 1,
    KMOD_PERM_EVENTS_EMIT   = 1ull << 2,
    KMOD_PERM_EVENTS_LISTEN = 1ull << 3,
    KMOD_PERM_RCONFIG_READ  = 1ull << 4,
    KMOD_PERM_RCONFIG_WRITE = 1ull << 5,
    KMOD_PERM_SYMBOLS_WRITE = 1ull << 6,
    KMOD_PERM_SYMBOLS_READ  = 1ull << 7,
    KMOD_PERM_LOAD_MODULES  = 1ull << 8,

    // ── System capabilities ───────────────────────────────────────────────────
    KMOD_PERM_NETWORK          = 1ull << 9,
    KMOD_PERM_FILESYSTEM       = 1ull << 10,
    KMOD_PERM_FILESYSTEM_WRITE = 1ull << 11,
    KMOD_PERM_PROCESS          = 1ull << 12,
    KMOD_PERM_NATIVE_LOAD      = 1ull << 13,
    KMOD_PERM_UI               = 1ull << 14,
    KMOD_PERM_UNLOAD_OTHERS    = 1ull << 15,
    KMOD_PERM_INTERMOD_CALL    = 1ull << 16,
    KMOD_PERM_KERNEL_CONFIG_RW = 1ull << 17,
    KMOD_PERM_QUERY_MODULES    = 1ull << 18,
    KMOD_PERM_BOOTCFG_READ     = 1ull << 19,
    KMOD_PERM_CLIENT           = 1ull << 20,

    // ── Named bundles ─────────────────────────────────────────────────────────
    KMOD_PERM_OBSERVER = KMOD_PERM_EVENTS_LISTEN
                       | KMOD_PERM_RCONFIG_READ
                       | KMOD_PERM_BOOTCFG_READ
                       | KMOD_PERM_SYMBOLS_READ,

    KMOD_PERM_STANDARD = KMOD_PERM_TOOLS
                       | KMOD_PERM_CONFIG
                       | KMOD_PERM_EVENTS_EMIT
                       | KMOD_PERM_EVENTS_LISTEN
                       | KMOD_PERM_RCONFIG_READ
                       | KMOD_PERM_BOOTCFG_READ
                       | KMOD_PERM_SYMBOLS_WRITE
                       | KMOD_PERM_SYMBOLS_READ
                       | KMOD_PERM_UI
                       | KMOD_PERM_INTERMOD_CALL,

    KMOD_PERM_ELEVATED = KMOD_PERM_STANDARD
                       | KMOD_PERM_RCONFIG_WRITE
                       | KMOD_PERM_LOAD_MODULES
                       | KMOD_PERM_KERNEL_CONFIG_RW
                       | KMOD_PERM_CLIENT,

    KMOD_PERM_SPECIAL_MASK    = 0xFFull << 56,
    KMOD_PERM_NORMAL_MASK     = ~KMOD_PERM_SPECIAL_MASK,
    KMOD_PERM_BUILTIN_DEFAULT = 0x00FFFFFFFFFFFFFFull,
    KMOD_PERM_FULL            = 0xFFFFFFFFFFFFFFFFull,
};

inline constexpr KModPerm operator|(KModPerm a, KModPerm b) noexcept {
    return static_cast<KModPerm>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}
inline constexpr KModPerm operator&(KModPerm a, KModPerm b) noexcept {
    return static_cast<KModPerm>(static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}
inline constexpr bool kmod_has_perm(uint64_t granted, KModPerm required) noexcept {
    return (granted & static_cast<uint64_t>(required)) != 0;
}

inline constexpr uint32_t KMOD_BASELINE_PERMS =
    static_cast<uint32_t>(KMOD_PERM_CONFIG) | static_cast<uint32_t>(KMOD_PERM_TOOLS);

// ─────────────────────────────────────────────────────────────────────────────

struct KModPermInfo {
    KModPerm    bit;
    const char* name;
    const char* description;
};

inline const std::vector<KModPermInfo>& kmod_all_perms() {
    static const std::vector<KModPermInfo> tbl = {
        { KMOD_PERM_TOOLS,             "TOOLS",             "Register tools/commands"                          },
        { KMOD_PERM_CONFIG,            "CONFIG",            "Register config entries"                          },
        { KMOD_PERM_EVENTS_EMIT,       "EVENTS_EMIT",       "Emit kernel events"                               },
        { KMOD_PERM_EVENTS_LISTEN,     "EVENTS_LISTEN",     "Subscribe to kernel events"                       },
        { KMOD_PERM_RCONFIG_READ,      "RCONFIG_READ",      "Read runtime config"                              },
        { KMOD_PERM_RCONFIG_WRITE,     "RCONFIG_WRITE",     "Write runtime config"                             },
        { KMOD_PERM_SYMBOLS_WRITE,     "SYMBOLS_WRITE",     "Export symbols to other modules"                  },
        { KMOD_PERM_SYMBOLS_READ,      "SYMBOLS_READ",      "Read symbols from other modules"                  },
        { KMOD_PERM_LOAD_MODULES,      "LOAD_MODULES",      "Load/unload other modules"                        },
        { KMOD_PERM_NETWORK,           "NETWORK",           "Network / HTTP access"                            },
        { KMOD_PERM_FILESYSTEM,        "FILESYSTEM",        "Read filesystem paths"                            },
        { KMOD_PERM_FILESYSTEM_WRITE,  "FILESYSTEM_WRITE",  "Write to filesystem"                              },
        { KMOD_PERM_PROCESS,           "PROCESS",           "Spawn / exec subprocesses"                        },
        { KMOD_PERM_NATIVE_LOAD,       "NATIVE_LOAD",       "Load additional native libraries"                 },
        { KMOD_PERM_UI,                "UI",                "Register UI elements / menus"                     },
        { KMOD_PERM_UNLOAD_OTHERS,     "UNLOAD_OTHERS",     "Force-unload other modules"                       },
        { KMOD_PERM_INTERMOD_CALL,     "INTERMOD_CALL",     "Call other modules' symbols"                      },
        { KMOD_PERM_KERNEL_CONFIG_RW,  "KERNEL_CONFIG_RW",  "Read / write global kernel config"                },
        { KMOD_PERM_QUERY_MODULES,     "QUERY_MODULES",     "List modules / read handle & instance addresses"  },
        { KMOD_PERM_BOOTCFG_READ,      "BOOTCFG_READ",      "Read boot config (read-only table)"               },
        { KMOD_PERM_CLIENT,            "CLIENT",            "Acquire the game client handle (KCLIENT)"         },
    };
    return tbl;
}

inline const char* kmod_perm_name(KModPerm bit) {
    for (auto& pi : kmod_all_perms()) if (pi.bit == bit) return pi.name;
    return "UNKNOWN";
}
inline const char* kmod_perm_desc(KModPerm bit) {
    for (auto& pi : kmod_all_perms()) if (pi.bit == bit) return pi.description;
    return "";
}

// ─────────────────────────────────────────────────────────────────────────────

#if defined(__GNUC__) || defined(__clang__)
    #define MODINFO_SECTION __attribute__((section(".kmodinfo"), used, aligned(1)))
    #define MODULE_EXPORT   __attribute__((visibility("default")))
#elif defined(_MSC_VER)
    #pragma section(".kmodinf", read)
    #define MODINFO_SECTION __declspec(allocate(".kmodinf"))
    #define MODULE_EXPORT   __declspec(dllexport)
#else
    #define MODINFO_SECTION
    #define MODULE_EXPORT
#endif

// ─────────────────────────────────────────────────────────────────────────────

#define DECLARE_MODINFO(id_, name_, ver_, author_, desc_, type_tag_, flags_, perms_, deps_) \
    static const ::KModInfo __kmod_section_info MODINFO_SECTION = {                         \
        {'K','M','O','D','I','N','F','O'},                                                   \
        ::KModInfo::CURRENT_VER,                                                             \
        static_cast<uint16_t>(flags_),                                                       \
        0u,                                                                                  \
        static_cast<uint64_t>(perms_),                                                       \
        id_, name_, ver_, author_, desc_, type_tag_, deps_                                  \
    };                                                                                       \
    extern "C" {                                                                             \
    MODULE_EXPORT                                                                            \
    const ::KModInfo* kmod_info() { return &__kmod_section_info; }                          \
    }

// ─────────────────────────────────────────────────────────────────────────────

namespace kmod_detail {
constexpr void str_copy(char* dst, const char* src, int cap) noexcept {
    int i = 0;
    for (; i < cap - 1 && src && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}
}

constexpr inline KModInfo kmod_make_info(
    uint16_t    flags,
    uint64_t    perms,
    const char* id,
    const char* name,
    const char* ver,
    const char* author,
    const char* desc,
    const char* type_tag = "DYNAMIC",
    const char* depends  = nullptr) noexcept
{
    KModInfo r{};
    r.magic[0]='K'; r.magic[1]='M'; r.magic[2]='O'; r.magic[3]='D';
    r.magic[4]='I'; r.magic[5]='N'; r.magic[6]='F'; r.magic[7]='O';
    r.struct_ver  = KModInfo::CURRENT_VER;
    r.flags       = flags;
    r.permissions = perms;
    kmod_detail::str_copy(r.id,          id,       64);
    kmod_detail::str_copy(r.name,        name,    128);
    kmod_detail::str_copy(r.mod_version, ver,      32);
    kmod_detail::str_copy(r.author,      author,   64);
    kmod_detail::str_copy(r.description, desc,    256);
    kmod_detail::str_copy(r.type_tag,    type_tag, 16);
    if (depends) kmod_detail::str_copy(r.depends, depends, 256);
    return r;
}
