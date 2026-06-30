# THGensouEclipse — OpenModules SDK

The **public SDK** for building third-party **DYNAMIC** modules (`.wasm`) for the
THGensouEclipse kernel.

A module compiled here runs as a **sandboxed WebAssembly (wasm32) reactor**. It
can reach the host *only* through a single, versioned, permission-checked
syscall door — never the kernel's internal classes directly. This is the
boundary that makes it safe to run untrusted, third-party code inside the host.

```
        your .wasm module                    host kernel (THGensouEclipse)
   ┌────────────────────────┐            ┌──────────────────────────────────┐
   │  kmod::register_tool    │            │  ModuleManager / KernelConfig /   │
   │  kmod::emit / listen    │            │  RConfig / BootConfig / Client …  │
   │  UI::printHeader / ...   │            │  (internal — NOT visible to you)  │
   │        │                │  one import │            ▲                      │
   │        ▼                │  symbol     │            │ permission check     │
   │  env.host_syscall ──────┼─────────────┼──► host syscall dispatch          │
   │  (verb + JSON args)     │            │   (derives caller from exec-ctx)  │
   └────────────────────────┘            └──────────────────────────────────┘
```

* **One door.** Every kernel call goes through the imported `host_syscall`
  symbol with a verb string and JSON arguments.
* **No spoofing.** You never pass your module id to a syscall; the host derives
  the caller from the authenticated execution context.
* **Fail closed.** If a call is denied or the host is incompatible, the wrapper
  returns an empty/zero value and becomes a safe no-op.
* **Least privilege.** Each syscall checks the permissions you declared in
  `KMOD_MODULE_INFO`.

## Documentation

In-depth guides — read these before writing a module:

| Language | File |
|----------|------|
| 🇻🇳 Tiếng Việt | [`docs/vi.md`](docs/vi.md) |
| 🇬🇧 English    | [`docs/en.md`](docs/en.md) |

Both cover the full API surface, the security model, the build/sign pipeline,
the wasm ABI, and worked examples.

## Quick start

```cpp
// MyMod.cpp
#include <kmod.hpp>

class MyMod : public Module {
public:
    KMOD_MODULE_INFO(
        "com.me.mymod", "My Module", "1.0.0", "Me", "Does something",
        KMOD_PERM_TOOLS | KMOD_PERM_CONFIG | KMOD_PERM_EVENTS_LISTEN,
        KMODF_NONE)

    void on_load() override {
        kmod::log_info("loaded", "MyMod");
        kmod::register_tool("hello", [this] {
            UI::printHeader("My Module", "Hello");
            UI::printSuccess("Hello from " + get_id() + "!");
            UI::pause();
        });
    }
};

MOD_REGISTER(MyMod)
```

Drop the file in `Modules/`, then:

```sh
cmake -S . -B build -DWASI_SDK=/opt/wasi-sdk   # see docs for the Termux path
cmake --build build -j
# → build/MyMod.wasm  (stripped + Ed25519-signed)
```

Modules are built and signed automatically in CI by
[`.github/workflows/build.yml`](.github/workflows/build.yml).

## Repository layout

```
.
├── include/            SDK headers — #include <kmod.hpp> and you're set
│   ├── kmod.hpp        umbrella header: runtime, syscall wrappers, macros
│   ├── ModInfo.hpp     KModInfo struct, permission/flag enums
│   ├── Module.hpp      Module base class + KMOD_MODULE_INFO
│   ├── hostcall.hpp    the host_syscall import + JSON marshalling
│   ├── ui.hpp          UI:: dual-render widgets (WebUI + terminal)
│   └── proto/codec.hpp protobuf / gRPC wire codec
├── example/HelloWorld.cpp   minimal example
├── Modules/                 every *.cpp here is auto-built to <name>.wasm
│   ├── EmergencyMenu.cpp
│   └── Testerv2.cpp         full syscall + sandbox self-test
├── tools/sign.py            Ed25519 signer (embeds a wasm custom section)
├── CMakeLists.txt           add_kmod() build system
└── docs/                    detailed bilingual guides
```

## Relationship to THGensouEclipseAPI

This repository was split out of
[`THGensouEclipseAPI`](https://github.com/ReiZen-Team/THGensouEclipseAPI) and is
embedded back into it as a **git submodule** at `OpenModules/`. The host kernel
and the SDK therefore share one source of truth for the module ABI
(`KModInfo`, the permission bits, and the `host_syscall` contract).

To work on both together:

```sh
git clone --recurse-submodules https://github.com/ReiZen-Team/THGensouEclipseAPI
# or, in an existing checkout:
git submodule update --init --recursive
```
