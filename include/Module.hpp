#pragma once
// ═════════════════════════════════════════════════════════════════════════════

#include "ModInfo.hpp"

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
enum class ModuleType : uint8_t {
    DYNAMIC = 1,
};

// ─────────────────────────────────────────────────────────────────────────────
struct ModuleInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string author;
};

// ─────────────────────────────────────────────────────────────────────────────
#if defined(__GNUC__) || defined(__clang__)
    #define MODULE_EXPORT __attribute__((visibility("default")))
#else
    #define MODULE_EXPORT
#endif

class Module {
public:
    explicit Module(ModuleType type = ModuleType::DYNAMIC) : type_(type) {}
    virtual ~Module() = default;

    // ── Identity ──────────
    virtual ModuleInfo get_info() const = 0;
    virtual KModInfo   kmodinfo() const { KModInfo r{}; return r; }

    std::string get_id()   const { return get_info().id; }
    ModuleType  get_type() const { return type_; }

    // ── Lifecycle ──────────
    virtual void on_load()   {}
    virtual void on_unload() {}

protected:
    ModuleType type_;
};

// ═════════════════════════════════════════════════════════════════════════════
#define KMOD_MODULE_INFO_IMPL(id_, name_, ver_, author_, desc_, perms_, flags_, deps_) \
    struct KMeta {                                                                      \
        static constexpr const char* kId      = (id_);                                 \
        static constexpr const char* kName    = (name_);                               \
        static constexpr const char* kVersion = (ver_);                                \
        static constexpr const char* kAuthor  = (author_);                             \
        static constexpr const char* kDesc    = (desc_);                               \
        static constexpr uint64_t    kPerms   = static_cast<uint64_t>(perms_);         \
        static constexpr uint16_t    kFlags   = static_cast<uint16_t>(flags_);         \
        static constexpr const char* kDeps    = (deps_);                               \
    };                                                                                  \
    ModuleInfo get_info() const override {                                              \
        return {KMeta::kId, KMeta::kName, KMeta::kVersion, KMeta::kDesc, KMeta::kAuthor}; \
    }                                                                                   \
    KModInfo kmodinfo() const override {                                                \
        return ::kmod_make_info(KMeta::kFlags, KMeta::kPerms, KMeta::kId, KMeta::kName, \
                                KMeta::kVersion, KMeta::kAuthor, KMeta::kDesc,          \
                                "DYNAMIC", KMeta::kDeps);                               \
    }

#define KMOD_MODULE_INFO(id_, name_, ver_, author_, desc_, perms_, flags_) \
    KMOD_MODULE_INFO_IMPL(id_, name_, ver_, author_, desc_, perms_, flags_, "")

#define KMOD_MODULE_INFO_D(id_, name_, ver_, author_, desc_, perms_, flags_, deps_) \
    KMOD_MODULE_INFO_IMPL(id_, name_, ver_, author_, desc_, perms_, flags_, deps_)
