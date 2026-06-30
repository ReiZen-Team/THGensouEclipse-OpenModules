# OpenModules SDK — In-depth guide (English)

> 🇻🇳 Bản tiếng Việt: [`vi.md`](vi.md) · Overview: [`../README.md`](../README.md)

This is the complete reference for writing, building, signing, and loading
**DYNAMIC** modules for the THGensouEclipse kernel. Modules are sandboxed
`wasm32` reactors that talk to the host through one permission-checked syscall.

---

## Table of contents

1. [The execution model](#1-the-execution-model)
2. [Requirements & toolchain](#2-requirements--toolchain)
3. [Anatomy of a module](#3-anatomy-of-a-module)
4. [API reference](#4-api-reference)
5. [UI reference](#5-ui-reference)
6. [Permissions & flags](#6-permissions--flags)
7. [Protobuf / gRPC codec](#7-protobuf--grpc-codec)
8. [Building](#8-building)
9. [Signing & loading](#9-signing--loading)
10. [The wasm ABI (deep dive)](#10-the-wasm-abi-deep-dive)
11. [Hot-reload](#11-hot-reload)
12. [Common pitfalls](#12-common-pitfalls)

---

## 1. The execution model

A DYNAMIC module is a WebAssembly **reactor** (`-mexec-model=reactor`): it has no
`main()`. Instead it exports a handful of functions the host calls, and imports
exactly one function it uses to call the host.

* **Exports (module → host):** `__kmod_info`, `on_load`, `on_unload`,
  `__kmod_dispatch`, plus `malloc`/`free`. All are emitted for you by
  `MOD_REGISTER`.
* **Imports (host → … no, module → host):** `env.host_syscall` and
  `env.host_free`, declared in `hostcall.hpp`.

Everything you call in the `kmod::` and `UI::` namespaces ultimately serialises a
**verb** (a short string like `"tool.register"` or `"ui.header"`) plus a **JSON
argument object**, hands them to `host_syscall`, and parses the JSON result. The
host:

1. Identifies the calling module from the authenticated execution context — you
   cannot impersonate another module.
2. Checks that the verb is allowed by the permissions you declared.
3. Executes against its internal services and returns JSON (or nothing).

If the host denies the call, is missing the verb, or is an incompatible ABI, the
wrapper sees a zero result and returns an empty/`null` value. **Denied calls are
silent no-ops** — they never throw and never corrupt host state.

---

## 2. Requirements & toolchain

| Tool | Version | Notes |
|------|---------|-------|
| C++ compiler targeting `wasm32` | clang/clang++ | from **wasi-sdk** or a system clang with a wasi-sysroot |
| CMake | ≥ 3.18 | |
| nlohmann/json | ≥ 3.9 | `apt install nlohmann-json3-dev` |
| Python 3 | any | runs `tools/sign.py` to sign modules |
| `wamrc` | optional | WAMR AOT compiler, to also emit `<name>.aot` |

There are two supported ways to provide the wasm toolchain.

### A) Full wasi-sdk (desktop Linux / macOS — recommended)

```sh
# one-time: download a wasi-sdk release and point CMake at it
cmake -S . -B build -DWASI_SDK=/opt/wasi-sdk
cmake --build build -j
```

`WASI_SDK` must contain `bin/clang++` and `share/wasi-sysroot`. CMake derives the
compiler and sysroot automatically. CI in this repo uses **wasi-sdk 25**.

### B) System clang + a standalone wasi-sysroot (Termux / Android)

wasi-sdk has no native ARM build, but Termux's `clang` already targets `wasm32`
and the sysroot is architecture-independent:

```sh
pkg install clang lld cmake python
cmake -S . -B build \
  -DWASI_CLANGXX=$(command -v clang++) \
  -DWASI_SYSROOT=$HOME/wasi/wasi-sysroot
cmake --build build -j
```

#### Why the include order matters

A system clang defaults to the host's `/usr/include`, which collides with the
wasm libc/libc++ (wrong `<math.h>`, pulls in `asm/types.h`, …). The build
therefore drops **all** default include directories (`-nostdinc`) and feeds an
exact, ordered set:

1. **libc++** (`include/.../c++/v1`) — its `<math.h>`/`<stdlib.h>` wrappers must win,
2. **clang builtins** (`stddef.h`, `stdarg.h`, …),
3. **wasi-libc** (the real C headers, reached via `#include_next`).

`nlohmann/json.hpp` usually lives in the host `/usr/include`, so it is added at
**lowest** priority (`-idirafter`) — it supplies only `nlohmann/`, never the C
library. Multi-variant sysroots (wasi-sdk 25+) nest libc++ under
`include/<target>/<eh|noeh>/c++/v1`; flat layouts fall back to `include/c++/v1`.
The `WASI_EH` cache variable (`eh`|`noeh`, default `noeh`) selects the variant.

### Relevant CMake cache variables

| Variable | Default | Meaning |
|----------|---------|---------|
| `WASI_SDK` | `""` | wasi-sdk install prefix (sets compiler + sysroot) |
| `WASI_CLANGXX` | auto | clang++ able to target wasm32 |
| `WASI_SYSROOT` | auto | wasi-sysroot path (required with a system clang) |
| `WASI_TARGET` | `wasm32-wasi` | `--target` triple (e.g. `wasm32-wasip1`) |
| `WASI_EH` | `noeh` | exception variant subdir in a multi-variant sysroot |
| `WAMRC` | `""` | path to `wamrc` to also emit `.aot` |
| `WAMRC_TARGET` | `aarch64v8` | `wamrc --target` for the device AOT build |

---

## 3. Anatomy of a module

```cpp
#include <kmod.hpp>          // umbrella header — pulls in everything

class MyMod : public Module {
public:
    MyMod() : Module(ModuleType::DYNAMIC) {}     // optional; DYNAMIC is the default

    KMOD_MODULE_INFO(
        "com.me.mymod",      // id — unique reverse-domain string
        "My Module",         // display name
        "1.0.0",             // version
        "Me",                // author
        "Does something",    // description
        KMOD_PERM_TOOLS | KMOD_PERM_CONFIG,      // permission bitmask
        KMODF_NONE)          // flags

    void on_load()   override { /* register tools/config/events here */ }
    void on_unload() override { /* clean up here */ }
};

MOD_REGISTER(MyMod)          // REQUIRED, at file scope (outside the class)
```

**Three things are mandatory:**

1. Inherit from `Module`. (The constructor argument defaults to
   `ModuleType::DYNAMIC`, so you may omit your own constructor.)
2. Declare metadata with `KMOD_MODULE_INFO(...)` **inside** the class body.
   Use `KMOD_MODULE_INFO_D(..., "dep.id.a,dep.id.b")` if your module depends on
   other module ids.
3. Call `MOD_REGISTER(ClassName)` **at file scope**. This emits the wasm exports
   and the factory the runtime uses to instantiate your class exactly once.

### Lifecycle

| Hook | When | Typical use |
|------|------|-------------|
| `on_load()` | module is loaded / reloaded | register tools, config, event listeners, RPC responders, schedules |
| `on_unload()` | module is unloaded / before reload | cancel schedules, persist state, free resources |

`get_id()` and `get_type()` are available on the base class.

> **Callbacks are tokens, not pointers across the boundary.** When you register a
> tool / listener / responder / config entry, the SDK stores your `std::function`
> in a per-module table and gives the host an integer **token**. The host later
> calls `__kmod_dispatch(kind, token, args, len)` and the runtime invokes the
> right closure. You never expose a raw function pointer to the host.

---

## 4. API reference

All free functions live in namespace `kmod`. Convenience macros exist for the
most common ones (listed where applicable). Unless noted, a call that you lack
permission for returns an empty/`null`/`false` value and is logged host-side.

### Logging — *no permission required*

```cpp
kmod::log_info (const std::string& msg, const std::string& ns = "MOD");
kmod::log_warn (const std::string& msg, const std::string& ns = "MOD");
kmod::log_error(const std::string& msg, const std::string& ns = "MOD");
nlohmann::json kmod::log_dump();     // current host log buffer
void           kmod::log_clear();
```

Macros: `KLOG_INFO(msg)`, `KLOG_WARN(msg)`, `KLOG_ERROR(msg)`.

### Identity & permissions — *no permission required*

```cpp
std::string kmod::self_id();             // your module id
bool        kmod::has_perm(uint64_t perm); // e.g. has_perm(KMOD_PERM_CLIENT)
```

### Tools — `TOOLS`

```cpp
kmod::register_tool(const std::string& name, std::function<void()> fn,
                    bool special = false);
```

A *tool* is a named entry point shown in the Module Manager / WebUI. `special`
marks it as a system/special tool. The callback takes no arguments and draws its
own screen via `UI::`.

### Tool launcher — `QUERY_MODULES`

```cpp
nlohmann::json kmod::tools_list();                 // [{name, special}, …]
bool kmod::tool_invoke(const std::string& name, bool special = false);
```

### Events — `EVENTS_EMIT` / `EVENTS_LISTEN`

```cpp
kmod::emit(const std::string& event, const json& data = nullptr);     // EVENTS_EMIT
int  kmod::listen(const std::string& event, std::function<void(const json&)> fn); // EVENTS_LISTEN
```

Macros: `KEMIT(event, data)`, `KLISTEN(event, fn)`.

### Request / reply RPC — `EVENTS_LISTEN` (reply) / `EVENTS_EMIT` (request)

```cpp
bool kmod::reply(const std::string& topic, std::function<json(const json&)> fn);
void kmod::unreply(const std::string& topic);
json kmod::request(const std::string& topic, const json& data = nullptr);
bool kmod::has_responder(const std::string& topic);
```

Macros: `KREPLY(topic, fn)`, `KREQUEST(topic, data)`.

### Scheduler — `SYMBOLS_READ`

All return an `int64_t` handle (0 on failure). Cron is UTC; callbacks fire on the
host scheduler thread.

```cpp
int64_t kmod::schedule_cron  (name, const std::string& cron, fn);
int64_t kmod::schedule_after (name, int64_t delay_sec, fn);
int64_t kmod::schedule_repeat(name, int64_t delay_sec, int64_t interval_sec, fn);
int64_t kmod::schedule_retry (name, std::function<bool()> fn,        // retry until it returns true
                              int max_attempts = 3, int64_t base_delay_sec = 2,
                              double factor = 2.0, int64_t max_delay_sec = 300);
int64_t kmod::schedule_on_event(name, const std::string& event, fn);
bool    kmod::schedule_cancel(int64_t handle);
json    kmod::schedule_list();
```

### Module-scoped persistent config (mconf) — `CONFIG`

Key/value strings persisted by the host, **namespaced to your module** — you
cannot read another module's or the kernel's keys.

```cpp
std::optional<std::string> kmod::conf_get(const std::string& key);
void kmod::conf_set(const std::string& key, const std::string& val);
bool kmod::conf_has(const std::string& key);
void kmod::conf_del(const std::string& key);
```

Macros: `KCONF_GET/SET/HAS/DEL`.

### Config-UI registration — `CONFIG`

Expose typed settings that the host renders as a config screen. Each takes a
getter and a setter; persist the value yourself (commonly via `conf_set`).

```cpp
kmod::register_config_bool    (name, ()->bool,        (bool)->void);
kmod::register_config_string  (name, ()->std::string, (std::string)->void);
kmod::register_config_choice  (name, std::vector<std::string> options, getter, setter);
kmod::register_config_int     (name, ()->long long,   (long long)->void, min = INT64_MIN, max = INT64_MAX);
kmod::register_config_float   (name, ()->double,      (double)->void,    min = -inf, max = +inf);
kmod::register_config_datetime(name, ()->std::string, (std::string)->void);
void kmod::remove_config(const std::string& name);
```

### Runtime config (RConfig) — `RCONFIG_READ` / `RCONFIG_WRITE`

```cpp
json kmod::rconfig_get  (const std::string& key);   // RCONFIG_READ
void kmod::rconfig_set  (const std::string& key, const json& v);  // RCONFIG_WRITE
bool kmod::rconfig_has  (const std::string& key);
void kmod::rconfig_clear(const std::string& key);
```

Protected session secrets (auth tokens, salts, …) are never exposed through
RConfig, even with `RCONFIG_READ`.

### Boot config (read-only) — `BOOTCFG_READ`

```cpp
std::string kmod::boot_get(const std::string& key);
bool        kmod::boot_has(const std::string& key);
```

### Global kernel config — `KERNEL_CONFIG_RW`

```cpp
std::optional<std::string> kmod::kconf_get(const std::string& key);
void kmod::kconf_set(const std::string& key, const std::string& val);
bool kmod::kconf_has(const std::string& key);
void kmod::kconf_del(const std::string& key);
```

### Shared JSON symbol table — `SYMBOLS_WRITE` / `SYMBOLS_READ`

A cross-module JSON key/value space for publishing and reading shared values.

```cpp
void kmod::sym_set(const std::string& name, const json& v);   // SYMBOLS_WRITE
json kmod::sym_get(const std::string& name);                  // SYMBOLS_READ
```

### Game client — `CLIENT`

```cpp
json kmod::client_call(const std::string& endpoint, const json& body = json::object());
std::vector<std::string> kmod::client_endpoints();
```

### Module management & queries

```cpp
bool kmod::load_module  (const std::string& path);   // LOAD_MODULES
void kmod::unload_module(const std::string& id);     // LOAD_MODULES (+ UNLOAD_OTHERS for others)
bool kmod::reload_module(const std::string& id);     // LOAD_MODULES
std::vector<std::string> kmod::module_list();        // QUERY_MODULES
bool kmod::is_builtin(const std::string& id);        // QUERY_MODULES
bool kmod::is_kupd   (const std::string& id);        // QUERY_MODULES
```

### Raw escape hatch

Every wrapper is built on:

```cpp
nlohmann::json host::call  (const std::string& verb, const json& args = nullptr);
void           host::notify(const std::string& verb, const json& args = nullptr);
```

You normally never call these directly, but they are useful for probing unknown
verbs (see `Modules/Testerv2.cpp`).

---

## 5. UI reference

Draw with the **`UI::`** namespace. The same calls render to whichever backend
is serving the session — the **WebUI** (browser) or the **terminal** — so a
module written once works in both. There is no separate terminal-only API to
worry about in this SDK.

### Output

```cpp
UI::clearScreen();
UI::printHeader(title, subtitle = "", version = "");
UI::println(s = ""); UI::printMuted(s); UI::printSuccess(s);
UI::printError(s);   UI::printWarning(s); UI::printInfo(s); UI::printBadge(s);
UI::printDivider(label = "");
UI::logLine(UI::Style style, text);
bool UI::isWeb();    // true if the active backend is the browser WebUI
```

`UI::Style` ∈ `{ Plain, Muted, Success, Error, Warning, Info, Badge }`.

### Input & prompts

```cpp
int    UI::choice(prompt, std::vector<std::string> options);          // index, or -1
int    UI::menu(prompt, std::vector<UI::MenuGroup> groups);           // grouped menu
std::vector<int> UI::multiChoice(prompt, options, preselected = {}, maxSel = -1);
std::string UI::input(prompt, def = "", placeholder = "", password = false);
double UI::numberInput(prompt, def = 0, min = 0, max = 0, step = 1, integer = true, placeholder = "");
bool   UI::confirm(prompt, defaultYes = true);
void   UI::pause(msg = "Press any key to continue…");
nlohmann::json UI::form(title, std::vector<UI::FormField> fields);    // {key: value, …}
```

`UI::FormField` carries `{ key, label, type, defValue, placeholder, required,
options, min, max, step, integer }`; `UI::FieldType` ∈ `{ Text, Number, Password,
Bool, Select }`.

### Tables & row selection

```cpp
UI::Table t;
t.header({"Col 1", "Col 2"});
t.row({"a", "b"});
t.print();

int  UI::selectRow (prompt, columns, rows);                  // single, -1 if none
std::vector<int> UI::selectRows(prompt, columns, rows, multi = false, pageSize = 12);
```

### Panels

```cpp
UI::Panel("Title", "subtitle")
    .stat("Speed", "12.3/s", "hint", UI::Style::Success)
    .note("free-form note")
    .action("Start", "begin work")
    .action("Delete", "danger", /*danger=*/true)
    .show();   // returns the chosen action index, or -1
```

### Pinned sub-headers

Up to `UI::kSubheaders` (64) slots pinned at the top of the screen — they never
scroll away. Ideal for live status (rate, counters, current module).

```cpp
UI::subheader(0, "Module: " + get_id(), UI::Style::Info);
UI::subheader(1, "Speed: 12.3/s",        UI::Style::Success);
UI::clearSubheaders();
```

### Multi-screen

A scheduled task can own its own screen so it does not fight the foreground UI:

```cpp
{
    UI::ScreenScope screen("Background job");   // RAII: opens on construct…
    if (screen.opened()) {
        UI::printInfo("working…");
    }
}                                               // …closes on scope exit
// or manually: UI::open_screen(title) / UI::close_screen()
```

### Structured views

```cpp
nlohmann::json UI::view(name, data = {});   // render a named host-side view
void           UI::tree(title, data);       // render a JSON tree
```

---

## 6. Permissions & flags

Declare the permission **bitmask** your module needs as the 6th argument of
`KMOD_MODULE_INFO`. `TOOLS` and `CONFIG` are granted to every module by default;
everything else must be granted by the user/operator. A call you did not declare
(or were not granted) silently returns empty.

| Permission | Grants |
|------------|--------|
| `KMOD_PERM_TOOLS` | register tools/commands *(default)* |
| `KMOD_PERM_CONFIG` | register config + use mconf *(default)* |
| `KMOD_PERM_EVENTS_EMIT` / `_LISTEN` | event bus |
| `KMOD_PERM_RCONFIG_READ` / `_WRITE` | runtime config |
| `KMOD_PERM_SYMBOLS_WRITE` / `_READ` | shared symbol table |
| `KMOD_PERM_LOAD_MODULES` | load / unload / reload modules |
| `KMOD_PERM_UNLOAD_OTHERS` | force-unload *other* modules |
| `KMOD_PERM_INTERMOD_CALL` | call other modules' symbols |
| `KMOD_PERM_QUERY_MODULES` | list modules, launch tools |
| `KMOD_PERM_BOOTCFG_READ` | read boot config |
| `KMOD_PERM_KERNEL_CONFIG_RW` | read/write global kernel config |
| `KMOD_PERM_CLIENT` | acquire the game client |
| `KMOD_PERM_UI` | register UI elements / menus |
| `KMOD_PERM_NETWORK` | network / HTTP |
| `KMOD_PERM_FILESYSTEM` / `_WRITE` | filesystem read / write |
| `KMOD_PERM_PROCESS` | spawn subprocesses |
| `KMOD_PERM_NATIVE_LOAD` | load native libraries |

### Bundles

| Bundle | Contents |
|--------|----------|
| `KMOD_PERM_OBSERVER` | `EVENTS_LISTEN \| RCONFIG_READ \| BOOTCFG_READ \| SYMBOLS_READ` |
| `KMOD_PERM_STANDARD` | tools, config, events (both), `RCONFIG_READ`, `BOOTCFG_READ`, symbols (both), `UI`, `INTERMOD_CALL` |
| `KMOD_PERM_ELEVATED` | `STANDARD` + `RCONFIG_WRITE` + `LOAD_MODULES` + `KERNEL_CONFIG_RW` + `CLIENT` |
| `KMOD_PERM_FULL` | everything (`0xFFFF…FFFF`) |

### Flags (`KModFlag`)

| Flag | Meaning |
|------|---------|
| `KMODF_NONE` | no flags |
| `KMODF_DEV` | development build |
| `KMODF_UNSTABLE` | unstable / experimental |
| `KMODF_HIDDEN` | hidden from normal listings |
| `KMODF_AUTOLOAD` | load automatically at startup |
| `KMODF_LIVE` | runs its work immediately in `on_load` (e.g. one-shot menus) |

---

## 7. Protobuf / gRPC codec

`#include <proto/codec.hpp>` for a small, schema-driven protobuf wire codec —
useful for talking to gRPC-style game endpoints.

A *schema* is an ordered JSON object mapping field name → type. Field numbers are
assigned by **declaration order** (first field = 1). Supported primitive types:
`"string"`, `"bool"`, `"double"`, `"float"`, `"int32"` (and any other string is
treated as a varint integer). A nested object value is encoded as a sub-message.
Arrays encode as repeated fields.

```cpp
#include <proto/codec.hpp>

schema_t schema;          // schema_t == nlohmann::ordered_json (order matters!)
schema["msg"] = "string";
schema["n"]   = "int32";

std::string wire = ProtoCodec::encode({{"msg", "hi"}, {"n", 9}}, schema);
nlohmann::json back = ProtoCodec::decode(wire, schema);   // {"msg":"hi","n":9}

// gRPC length-prefixed framing (1 flag byte + big-endian uint32 length):
std::string framed = ProtoCodec::grpc_frame(wire);
std::string body   = ProtoCodec::grpc_unframe(framed);
```

> Because modules build with `-fno-exceptions`, codec failures `std::abort()`
> (which traps the VM) instead of throwing. Validate untrusted input first.

---

## 8. Building

`CMakeLists.txt` exposes one function and auto-scans `Modules/`:

```cmake
add_kmod(MyMod  src/MyMod.cpp)            # → MyMod.wasm
add_kmod(MyMod  src/MyMod.cpp  Util.cpp)  # multiple sources
```

Anything you drop in `Modules/*.cpp` is built automatically as `<name>.wasm` — no
edit to `CMakeLists.txt` required. `example/HelloWorld.cpp` builds only when
`KMOD_BUILD_EXAMPLE=ON`.

```sh
cmake -S . -B build -DWASI_SDK=/opt/wasi-sdk -DKMOD_BUILD_EXAMPLE=ON
cmake --build build -j
```

### Build options

| Option | Default | Effect |
|--------|---------|--------|
| `KMOD_BUILD_EXAMPLE` | `OFF` | also build `HelloWorld.wasm` |
| `KMOD_SIGN_MODULES` | `ON` | Ed25519-sign each module after build |
| `KMOD_STRIP_MODULES` | `ON` | strip debug info / the name section at link time |

Each module is compiled `-Oz -fno-rtti -fno-exceptions -DJSON_NOEXCEPTION` as a
reactor with `--no-entry --allow-undefined` (the `host_syscall`/`host_free`
imports are resolved by the host at instantiation). Stripping happens at link
time **before** signing, so the appended signature section is never stripped.

### CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) installs
wasi-sdk + nlohmann-json, builds every module, signs them, and uploads the
`OpenModules-wasm` artifact. Trigger it manually from the Actions tab
(`workflow_dispatch`) with toggles for signing and the example.

---

## 9. Signing & loading

The host **rejects unsigned modules** (unless signature checks are disabled in
its `seccfg`). `tools/sign.py` signs a built artifact with an Ed25519 key:

* For a `.wasm` file the signature is embedded as a custom section named
  `signature` — wasm runtimes ignore unknown custom sections, so the module
  still parses and loads normally.
* **No key is baked into `sign.py`.** It reads the private key from the
  `KMOD_SIGNING_KEY` environment variable, or takes it as the first CLI argument.
* The build signs automatically only when `KMOD_SIGN_MODULES=ON` **and**
  `KMOD_SIGNING_KEY` is set in the environment; otherwise it builds unsigned and
  warns. The key is never written into CMake/build files.

```sh
# sign during the build — key supplied via the environment
export KMOD_SIGNING_KEY=<private_key_hex>
cmake -S . -B build -DWASI_SDK=/opt/wasi-sdk && cmake --build build -j

# or sign a single artifact manually
python3 tools/sign.py <private_key_hex> build/MyMod.wasm
KMOD_SIGNING_KEY=<private_key_hex> python3 tools/sign.py build/MyMod.wasm
```

> Sign with the **private key the host trusts**. Keep it out of the repository —
> use a local environment variable, a secrets manager, or (in CI) the
> `MODULE_SIGNING_KEY` repository secret consumed by the build workflow.

**Loading.** Drop the signed `.wasm` (or `.aot`) into the host's module
directory so it is discovered on scan, or load it at runtime from a module that
holds `LOAD_MODULES`:

```cpp
kmod::load_module("/path/to/MyMod.wasm");
```

Your tools then appear in the Module Manager and the WebUI.

---

## 10. The wasm ABI (deep dive)

You rarely need this, but it explains how `kmod::`/`UI::` actually work.

### Exports (emitted by `MOD_REGISTER`)

| Export | Signature | Purpose |
|--------|-----------|---------|
| `__kmod_info` | `() -> long long` | returns packed JSON metadata |
| `on_load` | `() -> void` | calls your `on_load()` |
| `on_unload` | `() -> void` | calls your `on_unload()` |
| `__kmod_dispatch` | `(int kind, int token, const char* args, int alen) -> long long` | routes host→module callbacks |
| `malloc` / `free` | libc | let the host write results into module memory |

### The single import

```cpp
long long host_syscall(const char* verb, int verb_len,
                       const char* args, int args_len);   // env.host_syscall
void      host_free(unsigned result_off);                 // env.host_free
```

### Packed return values

Both `host_syscall` results and module return values use one `long long` that
packs a pointer + length:

```
result = (uint64_t(offset) << 32) | uint32_t(length)
```

`offset` is a wasm linear-memory offset of a JSON string of `length` bytes; `0`
means "no value / null". After reading a host result the SDK calls
`host_free(offset)` to release the host-allocated buffer.

### Dispatch kinds

`__kmod_dispatch(kind, token, …)` multiplexes every host→module callback by
`kind`:

| `kind` | Callback table | Returns |
|--------|----------------|---------|
| 1 | tool | — |
| 2 | event listener (`data` field) | — |
| 3 | RPC responder (`data` field) | packed JSON result |
| 4 | config getter | packed `{"value": …}` |
| 5 | config setter (`value` field) | — |
| 6 | scheduled task | — |
| 7 | retry task | packed `{"ok": bool}` |

`token` indexes the per-module table the SDK filled when you registered the
callback.

### Metadata & versioning

`KModInfo` (in `ModInfo.hpp`) is the binary contract: magic `KMODINFO`,
`struct_ver = 3`, a 64-bit permission mask, and fixed-width identity fields
(`sizeof(KModInfo) == 840`). If the host and SDK disagree on the struct version,
the host refuses the module rather than misreading it.

---

## 11. Hot-reload

The host can reload a module in place, handing state from the old instance to the
new one:

```cpp
void on_unload() override {
    if (kmod::reload_active())              // are we being reloaded (vs. fully unloaded)?
        kmod::reload_save(serialise_state());  // stash a blob for the next instance
}

void on_load() override {
    std::string blob = kmod::reload_load(); // recover it (empty if not a reload)
    if (!blob.empty()) restore_state(blob);
}
```

---

## 12. Common pitfalls

| Symptom | Cause & fix |
|---------|-------------|
| Host can't discover your module | Missing `MOD_REGISTER(ClassName)` at file scope. |
| Module refuses to load | Unsigned `.wasm` — build with `KMOD_SIGN_MODULES=ON`, or disable checks in the host `seccfg`. |
| A syscall returns empty / does nothing | Missing permission — add the bit to `KMOD_MODULE_INFO` and have it granted. |
| Compile errors about `<math.h>`/`asm/types.h` | System include leaked in — use `-DWASI_SDK=…`, or set `WASI_SYSROOT` for a system clang (see §2). |
| Macro breaks on a comma | A `{}` braced-init with a comma inside a macro argument, e.g. `KEMIT("e", {{"k", v}})`. Wrap it: `KEMIT("e", (nlohmann::json{{"k", v}}))`. |
| Crash instead of a thrown error in the codec | Modules build `-fno-exceptions`; `ProtoCodec` aborts (VM trap) on malformed input. Validate first. |
