# OpenModules SDK — Hướng dẫn chuyên sâu (Tiếng Việt)

> 🇬🇧 English version: [`en.md`](en.md) · Tổng quan: [`../README.md`](../README.md)

Đây là tài liệu tham chiếu đầy đủ để viết, build, ký và nạp module **DYNAMIC**
cho kernel THGensouEclipse. Module là một **reactor `wasm32` chạy trong
sandbox**, giao tiếp với host qua **một syscall duy nhất đã được kiểm tra quyền**.

---

## Mục lục

1. [Mô hình thực thi](#1-mô-hình-thực-thi)
2. [Yêu cầu & toolchain](#2-yêu-cầu--toolchain)
3. [Cấu trúc một module](#3-cấu-trúc-một-module)
4. [Tham chiếu API](#4-tham-chiếu-api)
5. [Tham chiếu UI](#5-tham-chiếu-ui)
6. [Quyền & cờ (flags)](#6-quyền--cờ-flags)
7. [Codec Protobuf / gRPC](#7-codec-protobuf--grpc)
8. [Build](#8-build)
9. [Ký & nạp module](#9-ký--nạp-module)
10. [ABI wasm (đi sâu)](#10-abi-wasm-đi-sâu)
11. [Hot-reload](#11-hot-reload)
12. [Lỗi thường gặp](#12-lỗi-thường-gặp)

---

## 1. Mô hình thực thi

Module DYNAMIC là một **reactor** WebAssembly (`-mexec-model=reactor`): không có
`main()`. Thay vào đó nó **export** một số hàm để host gọi, và **import** đúng
một hàm để gọi ngược lại host.

* **Export (module → host):** `__kmod_info`, `on_load`, `on_unload`,
  `__kmod_dispatch`, cùng `malloc`/`free`. Tất cả do `MOD_REGISTER` sinh ra giúp bạn.
* **Import (module → host):** `env.host_syscall` và `env.host_free`, khai báo
  trong `hostcall.hpp`.

Mọi thứ bạn gọi trong namespace `kmod::` và `UI::` rốt cuộc đều: tuần tự hoá một
**verb** (chuỗi ngắn như `"tool.register"` hay `"ui.header"`) cùng một **đối số
JSON**, đưa cho `host_syscall`, rồi phân tích kết quả JSON trả về. Host sẽ:

1. Xác định module gọi từ **execution context** đã xác thực — bạn không thể giả
   mạo thành module khác.
2. Kiểm tra verb có nằm trong quyền bạn đã khai báo hay không.
3. Thực thi trên dịch vụ nội bộ và trả JSON (hoặc không trả gì).

Nếu host **từ chối**, thiếu verb, hay ABI không tương thích, wrapper nhận kết quả
0 và trả về giá trị rỗng/`null`. **Lệnh bị từ chối là no-op im lặng** — không bao
giờ ném lỗi và không làm hỏng trạng thái host.

---

## 2. Yêu cầu & toolchain

| Công cụ | Phiên bản | Ghi chú |
|---------|-----------|---------|
| Trình biên dịch C++ target `wasm32` | clang/clang++ | từ **wasi-sdk** hoặc clang hệ thống + wasi-sysroot |
| CMake | ≥ 3.18 | |
| nlohmann/json | ≥ 3.9 | `apt install nlohmann-json3-dev` |
| Python 3 | bất kỳ | chạy `tools/sign.py` để ký module |
| `wamrc` | tuỳ chọn | trình biên dịch AOT của WAMR, để xuất thêm `<name>.aot` |

Có hai cách cung cấp toolchain wasm.

### A) Dùng wasi-sdk đầy đủ (Linux/macOS desktop — khuyến nghị)

```sh
cmake -S . -B build -DWASI_SDK=/opt/wasi-sdk
cmake --build build -j
```

`WASI_SDK` phải chứa `bin/clang++` và `share/wasi-sysroot`. CMake tự suy ra trình
biên dịch và sysroot. CI trong repo này dùng **wasi-sdk 25**.

### B) Clang hệ thống + wasi-sysroot rời (Termux / Android)

wasi-sdk không có bản ARM, nhưng `clang` của Termux đã target được `wasm32`, còn
sysroot thì độc lập kiến trúc:

```sh
pkg install clang lld cmake python
cmake -S . -B build \
  -DWASI_CLANGXX=$(command -v clang++) \
  -DWASI_SYSROOT=$HOME/wasi/wasi-sysroot
cmake --build build -j
```

#### Vì sao thứ tự include lại quan trọng

Clang hệ thống mặc định dùng `/usr/include` của máy chủ, vốn xung đột với
libc/libc++ của wasm (sai `<math.h>`, kéo theo `asm/types.h`, …). Vì vậy build
bỏ **toàn bộ** include mặc định (`-nostdinc`) và nạp đúng một tập có thứ tự:

1. **libc++** (`include/.../c++/v1`) — các wrapper `<math.h>`/`<stdlib.h>` của nó phải thắng,
2. **builtin của clang** (`stddef.h`, `stdarg.h`, …),
3. **wasi-libc** (header C thật, truy cập qua `#include_next`).

`nlohmann/json.hpp` thường nằm ở `/usr/include` của máy chủ, nên được thêm ở mức
**ưu tiên thấp nhất** (`-idirafter`) — chỉ cung cấp `nlohmann/`, không bao giờ
cung cấp thư viện C. Sysroot đa biến thể (wasi-sdk 25+) đặt libc++ tại
`include/<target>/<eh|noeh>/c++/v1`; layout phẳng thì lùi về `include/c++/v1`.
Biến `WASI_EH` (`eh`|`noeh`, mặc định `noeh`) chọn biến thể.

### Các biến cache CMake liên quan

| Biến | Mặc định | Ý nghĩa |
|------|----------|---------|
| `WASI_SDK` | `""` | thư mục cài wasi-sdk (đặt cả compiler + sysroot) |
| `WASI_CLANGXX` | tự dò | clang++ target được wasm32 |
| `WASI_SYSROOT` | tự dò | đường dẫn wasi-sysroot (bắt buộc khi dùng clang hệ thống) |
| `WASI_TARGET` | `wasm32-wasi` | triple `--target` (vd `wasm32-wasip1`) |
| `WASI_EH` | `noeh` | biến thể exception trong sysroot đa biến thể |
| `WAMRC` | `""` | đường dẫn `wamrc` để xuất thêm `.aot` |
| `WAMRC_TARGET` | `aarch64v8` | `wamrc --target` cho bản AOT trên thiết bị |

---

## 3. Cấu trúc một module

```cpp
#include <kmod.hpp>          // header tổng — kéo theo mọi thứ

class MyMod : public Module {
public:
    MyMod() : Module(ModuleType::DYNAMIC) {}     // tuỳ chọn; DYNAMIC là mặc định

    KMOD_MODULE_INFO(
        "com.me.mymod",      // id — chuỗi reverse-domain duy nhất
        "My Module",         // tên hiển thị
        "1.0.0",             // phiên bản
        "Me",                // tác giả
        "Làm việc gì đó",    // mô tả
        KMOD_PERM_TOOLS | KMOD_PERM_CONFIG,      // bitmask quyền
        KMODF_NONE)          // cờ

    void on_load()   override { /* đăng ký tool/config/event tại đây */ }
    void on_unload() override { /* dọn dẹp tại đây */ }
};

MOD_REGISTER(MyMod)          // BẮT BUỘC, ở phạm vi file (ngoài class)
```

**Ba điều bắt buộc:**

1. Kế thừa `Module`. (Tham số constructor mặc định là `ModuleType::DYNAMIC`, nên
   bạn có thể bỏ constructor riêng.)
2. Khai báo metadata bằng `KMOD_MODULE_INFO(...)` **bên trong** thân class. Dùng
   `KMOD_MODULE_INFO_D(..., "dep.id.a,dep.id.b")` nếu module phụ thuộc id khác.
3. Gọi `MOD_REGISTER(TênClass)` **ở phạm vi file**. Macro này sinh các export
   wasm và factory để runtime khởi tạo class của bạn đúng một lần.

### Vòng đời

| Hook | Khi nào | Dùng để |
|------|---------|---------|
| `on_load()` | module được nạp / reload | đăng ký tool, config, listener sự kiện, RPC, lịch |
| `on_unload()` | module bị gỡ / trước khi reload | huỷ lịch, lưu trạng thái, giải phóng tài nguyên |

`get_id()` và `get_type()` có sẵn ở lớp cơ sở.

> **Callback là token, không phải con trỏ vượt biên.** Khi bạn đăng ký
> tool/listener/responder/config, SDK lưu `std::function` của bạn vào một bảng
> riêng theo module và đưa cho host một **token** số nguyên. Sau đó host gọi
> `__kmod_dispatch(kind, token, args, len)` và runtime gọi đúng closure. Bạn
> không bao giờ lộ con trỏ hàm thô ra host.

---

## 4. Tham chiếu API

Mọi hàm tự do nằm trong namespace `kmod`. Có macro tiện lợi cho các hàm phổ biến
(ghi kèm bên dưới). Trừ khi nói khác, lệnh mà bạn không có quyền sẽ trả
rỗng/`null`/`false` và được ghi log phía host.

### Logging — *không cần quyền*

```cpp
kmod::log_info (const std::string& msg, const std::string& ns = "MOD");
kmod::log_warn (const std::string& msg, const std::string& ns = "MOD");
kmod::log_error(const std::string& msg, const std::string& ns = "MOD");
nlohmann::json kmod::log_dump();     // bộ đệm log hiện tại của host
void           kmod::log_clear();
```

Macro: `KLOG_INFO(msg)`, `KLOG_WARN(msg)`, `KLOG_ERROR(msg)`.

### Định danh & quyền — *không cần quyền*

```cpp
std::string kmod::self_id();               // id module của bạn
bool        kmod::has_perm(uint64_t perm); // vd has_perm(KMOD_PERM_CLIENT)
```

### Tool — `TOOLS`

```cpp
kmod::register_tool(const std::string& name, std::function<void()> fn,
                    bool special = false);
```

*Tool* là điểm vào có tên hiển thị trong Module Manager / WebUI. `special` đánh
dấu nó là tool hệ thống/đặc biệt. Callback không nhận tham số và tự vẽ màn hình
qua `UI::`.

### Bộ phóng tool — `QUERY_MODULES`

```cpp
nlohmann::json kmod::tools_list();                 // [{name, special}, …]
bool kmod::tool_invoke(const std::string& name, bool special = false);
```

### Sự kiện — `EVENTS_EMIT` / `EVENTS_LISTEN`

```cpp
kmod::emit(const std::string& event, const json& data = nullptr);                  // EVENTS_EMIT
int  kmod::listen(const std::string& event, std::function<void(const json&)> fn);  // EVENTS_LISTEN
```

Macro: `KEMIT(event, data)`, `KLISTEN(event, fn)`.

### Request / reply RPC — `EVENTS_LISTEN` (reply) / `EVENTS_EMIT` (request)

```cpp
bool kmod::reply(const std::string& topic, std::function<json(const json&)> fn);
void kmod::unreply(const std::string& topic);
json kmod::request(const std::string& topic, const json& data = nullptr);
bool kmod::has_responder(const std::string& topic);
```

Macro: `KREPLY(topic, fn)`, `KREQUEST(topic, data)`.

### Lập lịch — `SYMBOLS_READ`

Tất cả trả về `int64_t` handle (0 nếu thất bại). Cron theo UTC; callback chạy trên
luồng scheduler của host.

```cpp
int64_t kmod::schedule_cron  (name, const std::string& cron, fn);
int64_t kmod::schedule_after (name, int64_t delay_sec, fn);
int64_t kmod::schedule_repeat(name, int64_t delay_sec, int64_t interval_sec, fn);
int64_t kmod::schedule_retry (name, std::function<bool()> fn,        // thử lại tới khi trả về true
                              int max_attempts = 3, int64_t base_delay_sec = 2,
                              double factor = 2.0, int64_t max_delay_sec = 300);
int64_t kmod::schedule_on_event(name, const std::string& event, fn);
bool    kmod::schedule_cancel(int64_t handle);
json    kmod::schedule_list();
```

### Config bền theo module (mconf) — `CONFIG`

Cặp key/value chuỗi do host lưu, **giới hạn trong namespace của module bạn** — bạn
không đọc được key của module khác hay của kernel.

```cpp
std::optional<std::string> kmod::conf_get(const std::string& key);
void kmod::conf_set(const std::string& key, const std::string& val);
bool kmod::conf_has(const std::string& key);
void kmod::conf_del(const std::string& key);
```

Macro: `KCONF_GET/SET/HAS/DEL`.

### Đăng ký Config-UI — `CONFIG`

Phơi bày thiết lập có kiểu để host vẽ thành màn hình config. Mỗi hàm nhận getter
và setter; bạn tự lưu giá trị (thường qua `conf_set`).

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
json kmod::rconfig_get  (const std::string& key);                 // RCONFIG_READ
void kmod::rconfig_set  (const std::string& key, const json& v);  // RCONFIG_WRITE
bool kmod::rconfig_has  (const std::string& key);
void kmod::rconfig_clear(const std::string& key);
```

Bí mật phiên được bảo vệ (token xác thực, salt, …) không bao giờ lộ qua RConfig,
kể cả khi có `RCONFIG_READ`.

### Boot config (chỉ đọc) — `BOOTCFG_READ`

```cpp
std::string kmod::boot_get(const std::string& key);
bool        kmod::boot_has(const std::string& key);
```

### Config kernel toàn cục — `KERNEL_CONFIG_RW`

```cpp
std::optional<std::string> kmod::kconf_get(const std::string& key);
void kmod::kconf_set(const std::string& key, const std::string& val);
bool kmod::kconf_has(const std::string& key);
void kmod::kconf_del(const std::string& key);
```

### Bảng symbol JSON dùng chung — `SYMBOLS_WRITE` / `SYMBOLS_READ`

Không gian key/value JSON liên-module để công bố và đọc giá trị chung.

```cpp
void kmod::sym_set(const std::string& name, const json& v);   // SYMBOLS_WRITE
json kmod::sym_get(const std::string& name);                  // SYMBOLS_READ
```

### Game client — `CLIENT`

```cpp
json kmod::client_call(const std::string& endpoint, const json& body = json::object());
std::vector<std::string> kmod::client_endpoints();
```

### Quản lý & truy vấn module

```cpp
bool kmod::load_module  (const std::string& path);   // LOAD_MODULES
void kmod::unload_module(const std::string& id);     // LOAD_MODULES (+ UNLOAD_OTHERS cho module khác)
bool kmod::reload_module(const std::string& id);     // LOAD_MODULES
std::vector<std::string> kmod::module_list();        // QUERY_MODULES
bool kmod::is_builtin(const std::string& id);        // QUERY_MODULES
bool kmod::is_kupd   (const std::string& id);        // QUERY_MODULES
```

### Lối thoát thô

Mọi wrapper đều dựng trên:

```cpp
nlohmann::json host::call  (const std::string& verb, const json& args = nullptr);
void           host::notify(const std::string& verb, const json& args = nullptr);
```

Bình thường bạn không gọi trực tiếp, nhưng chúng hữu ích để dò verb lạ (xem
`Modules/Testerv2.cpp`).

---

## 5. Tham chiếu UI

Vẽ bằng namespace **`UI::`**. Cùng một lời gọi sẽ render ra backend đang phục vụ
phiên — **WebUI** (trình duyệt) hoặc **terminal** — nên module viết một lần chạy
được cả hai. SDK này **không** có API riêng cho terminal cần bận tâm.

### Xuất ra màn hình

```cpp
UI::clearScreen();
UI::printHeader(title, subtitle = "", version = "");
UI::println(s = ""); UI::printMuted(s); UI::printSuccess(s);
UI::printError(s);   UI::printWarning(s); UI::printInfo(s); UI::printBadge(s);
UI::printDivider(label = "");
UI::logLine(UI::Style style, text);
bool UI::isWeb();    // true nếu backend đang dùng là WebUI trình duyệt
```

`UI::Style` ∈ `{ Plain, Muted, Success, Error, Warning, Info, Badge }`.

### Nhập liệu & prompt

```cpp
int    UI::choice(prompt, std::vector<std::string> options);          // chỉ số, hoặc -1
int    UI::menu(prompt, std::vector<UI::MenuGroup> groups);           // menu phân nhóm
std::vector<int> UI::multiChoice(prompt, options, preselected = {}, maxSel = -1);
std::string UI::input(prompt, def = "", placeholder = "", password = false);
double UI::numberInput(prompt, def = 0, min = 0, max = 0, step = 1, integer = true, placeholder = "");
bool   UI::confirm(prompt, defaultYes = true);
void   UI::pause(msg = "Press any key to continue…");
nlohmann::json UI::form(title, std::vector<UI::FormField> fields);    // {key: value, …}
```

`UI::FormField` mang `{ key, label, type, defValue, placeholder, required,
options, min, max, step, integer }`; `UI::FieldType` ∈ `{ Text, Number, Password,
Bool, Select }`.

### Bảng & chọn hàng

```cpp
UI::Table t;
t.header({"Cột 1", "Cột 2"});
t.row({"a", "b"});
t.print();

int  UI::selectRow (prompt, columns, rows);                  // chọn một, -1 nếu không
std::vector<int> UI::selectRows(prompt, columns, rows, multi = false, pageSize = 12);
```

### Panel

```cpp
UI::Panel("Tiêu đề", "phụ đề")
    .stat("Tốc độ", "12.3/s", "gợi ý", UI::Style::Success)
    .note("ghi chú tự do")
    .action("Bắt đầu", "khởi chạy")
    .action("Xoá", "nguy hiểm", /*danger=*/true)
    .show();   // trả chỉ số action được chọn, hoặc -1
```

### Sub-header ghim

Tối đa `UI::kSubheaders` (64) ô ghim trên cùng màn hình — không bao giờ bị cuộn
mất. Lý tưởng cho trạng thái trực tiếp (tốc độ, bộ đếm, module hiện tại).

```cpp
UI::subheader(0, "Module: " + get_id(), UI::Style::Info);
UI::subheader(1, "Tốc độ: 12.3/s",       UI::Style::Success);
UI::clearSubheaders();
```

### Đa màn hình

Một tác vụ theo lịch có thể sở hữu màn hình riêng để không tranh chấp UI tiền cảnh:

```cpp
{
    UI::ScreenScope screen("Tác vụ nền");       // RAII: mở khi khởi tạo…
    if (screen.opened()) {
        UI::printInfo("đang chạy…");
    }
}                                               // …đóng khi ra khỏi scope
// hoặc thủ công: UI::open_screen(title) / UI::close_screen()
```

### View có cấu trúc

```cpp
nlohmann::json UI::view(name, data = {});   // render view có tên phía host
void           UI::tree(title, data);       // render cây JSON
```

---

## 6. Quyền & cờ (flags)

Khai báo **bitmask** quyền mà module cần làm đối số thứ 6 của
`KMOD_MODULE_INFO`. `TOOLS` và `CONFIG` được cấp mặc định cho mọi module; phần còn
lại phải do người dùng/operator cấp. Lệnh bạn không khai báo (hoặc chưa được cấp)
sẽ lặng lẽ trả rỗng.

| Quyền | Cho phép |
|-------|----------|
| `KMOD_PERM_TOOLS` | đăng ký tool/lệnh *(mặc định)* |
| `KMOD_PERM_CONFIG` | đăng ký config + dùng mconf *(mặc định)* |
| `KMOD_PERM_EVENTS_EMIT` / `_LISTEN` | bus sự kiện |
| `KMOD_PERM_RCONFIG_READ` / `_WRITE` | runtime config |
| `KMOD_PERM_SYMBOLS_WRITE` / `_READ` | bảng symbol dùng chung |
| `KMOD_PERM_LOAD_MODULES` | nạp / gỡ / reload module |
| `KMOD_PERM_UNLOAD_OTHERS` | ép gỡ module *khác* |
| `KMOD_PERM_INTERMOD_CALL` | gọi symbol của module khác |
| `KMOD_PERM_QUERY_MODULES` | liệt kê module, phóng tool |
| `KMOD_PERM_BOOTCFG_READ` | đọc boot config |
| `KMOD_PERM_KERNEL_CONFIG_RW` | đọc/ghi config kernel toàn cục |
| `KMOD_PERM_CLIENT` | lấy game client |
| `KMOD_PERM_UI` | đăng ký phần tử UI / menu |
| `KMOD_PERM_NETWORK` | mạng / HTTP |
| `KMOD_PERM_FILESYSTEM` / `_WRITE` | đọc / ghi filesystem |
| `KMOD_PERM_PROCESS` | tạo tiến trình con |
| `KMOD_PERM_NATIVE_LOAD` | nạp thư viện native |

### Gói quyền

| Gói | Bao gồm |
|-----|---------|
| `KMOD_PERM_OBSERVER` | `EVENTS_LISTEN \| RCONFIG_READ \| BOOTCFG_READ \| SYMBOLS_READ` |
| `KMOD_PERM_STANDARD` | tools, config, events (cả hai), `RCONFIG_READ`, `BOOTCFG_READ`, symbols (cả hai), `UI`, `INTERMOD_CALL` |
| `KMOD_PERM_ELEVATED` | `STANDARD` + `RCONFIG_WRITE` + `LOAD_MODULES` + `KERNEL_CONFIG_RW` + `CLIENT` |
| `KMOD_PERM_FULL` | tất cả (`0xFFFF…FFFF`) |

### Cờ (`KModFlag`)

| Cờ | Ý nghĩa |
|----|---------|
| `KMODF_NONE` | không cờ |
| `KMODF_DEV` | bản phát triển |
| `KMODF_UNSTABLE` | chưa ổn định / thử nghiệm |
| `KMODF_HIDDEN` | ẩn khỏi danh sách thông thường |
| `KMODF_AUTOLOAD` | tự nạp khi khởi động |
| `KMODF_LIVE` | chạy ngay phần việc trong `on_load` (vd menu một-lần) |

---

## 7. Codec Protobuf / gRPC

`#include <proto/codec.hpp>` để có một codec protobuf nhỏ, điều khiển bằng schema
— hữu ích khi nói chuyện với endpoint game kiểu gRPC.

Một *schema* là object JSON có thứ tự, ánh xạ tên field → kiểu. Số field gán theo
**thứ tự khai báo** (field đầu = 1). Kiểu nguyên thuỷ hỗ trợ: `"string"`,
`"bool"`, `"double"`, `"float"`, `"int32"` (và mọi chuỗi khác coi như số nguyên
varint). Giá trị là object lồng nhau được mã hoá thành sub-message. Mảng mã hoá
thành field lặp.

```cpp
#include <proto/codec.hpp>

schema_t schema;          // schema_t == nlohmann::ordered_json (thứ tự quan trọng!)
schema["msg"] = "string";
schema["n"]   = "int32";

std::string wire = ProtoCodec::encode({{"msg", "hi"}, {"n", 9}}, schema);
nlohmann::json back = ProtoCodec::decode(wire, schema);   // {"msg":"hi","n":9}

// Khung gRPC có tiền tố độ dài (1 byte cờ + uint32 big-endian độ dài):
std::string framed = ProtoCodec::grpc_frame(wire);
std::string body   = ProtoCodec::grpc_unframe(framed);
```

> Vì module build với `-fno-exceptions`, lỗi codec sẽ `std::abort()` (làm trap VM)
> thay vì ném ngoại lệ. Hãy kiểm tra dữ liệu không tin cậy trước.

---

## 8. Build

`CMakeLists.txt` cung cấp một hàm và tự quét `Modules/`:

```cmake
add_kmod(MyMod  src/MyMod.cpp)            # → MyMod.wasm
add_kmod(MyMod  src/MyMod.cpp  Util.cpp)  # nhiều file nguồn
```

Mọi file bạn bỏ vào `Modules/*.cpp` đều được build tự động thành `<name>.wasm` —
không cần sửa `CMakeLists.txt`. `example/HelloWorld.cpp` chỉ build khi
`KMOD_BUILD_EXAMPLE=ON`.

```sh
cmake -S . -B build -DWASI_SDK=/opt/wasi-sdk -DKMOD_BUILD_EXAMPLE=ON
cmake --build build -j
```

### Tuỳ chọn build

| Tuỳ chọn | Mặc định | Tác dụng |
|----------|----------|----------|
| `KMOD_BUILD_EXAMPLE` | `OFF` | build thêm `HelloWorld.wasm` |
| `KMOD_SIGN_MODULES` | `ON` | ký Ed25519 mỗi module sau khi build |
| `KMOD_STRIP_MODULES` | `ON` | strip debug info / name section khi link |

Mỗi module biên dịch `-Oz -fno-rtti -fno-exceptions -DJSON_NOEXCEPTION` dạng
reactor với `--no-entry --allow-undefined` (import `host_syscall`/`host_free` do
host phân giải lúc khởi tạo). Strip xảy ra khi link **trước** khi ký, nên section
chữ ký được nối thêm không bao giờ bị strip.

### CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) cài
wasi-sdk + nlohmann-json, build mọi module, ký chúng, và tải lên artifact
`OpenModules-wasm`. Kích hoạt thủ công từ tab Actions (`workflow_dispatch`) với các
công tắc cho ký và ví dụ.

---

## 9. Ký & nạp module

Host **từ chối module chưa ký** (trừ khi tắt kiểm tra chữ ký trong `seccfg` của
host). `tools/sign.py` ký artifact đã build bằng khoá Ed25519:

* Với file `.wasm`, chữ ký được nhúng vào một custom section tên `signature` —
  runtime wasm bỏ qua custom section lạ, nên module vẫn parse và nạp bình thường.
* **Không có khoá nào nhúng sẵn trong `sign.py`.** Nó đọc khoá riêng từ biến môi
  trường `KMOD_SIGNING_KEY`, hoặc nhận làm đối số CLI đầu tiên.
* Build tự ký chỉ khi `KMOD_SIGN_MODULES=ON` **và** `KMOD_SIGNING_KEY` có trong
  môi trường; nếu không sẽ build không ký và cảnh báo. Khoá không bao giờ bị ghi
  vào file CMake/build.

```sh
# ký ngay khi build — khoá cấp qua môi trường
export KMOD_SIGNING_KEY=<private_key_hex>
cmake -S . -B build -DWASI_SDK=/opt/wasi-sdk && cmake --build build -j

# hoặc ký một artifact thủ công
python3 tools/sign.py <private_key_hex> build/MyMod.wasm
KMOD_SIGNING_KEY=<private_key_hex> python3 tools/sign.py build/MyMod.wasm
```

> Ký bằng **khoá riêng mà host tin cậy**. Giữ nó NGOÀI repository — dùng biến môi
> trường cục bộ, một secrets manager, hoặc (trong CI) secret `MODULE_SIGNING_KEY`
> mà workflow build sử dụng.

**Nạp.** Bỏ file `.wasm` (hoặc `.aot`) đã ký vào thư mục module của host để được
phát hiện khi quét, hoặc nạp lúc chạy từ một module có quyền `LOAD_MODULES`:

```cpp
kmod::load_module("/path/to/MyMod.wasm");
```

Tool của bạn sau đó xuất hiện trong Module Manager và WebUI.

---

## 10. ABI wasm (đi sâu)

Hiếm khi cần, nhưng phần này giải thích `kmod::`/`UI::` thực sự hoạt động ra sao.

### Export (do `MOD_REGISTER` sinh)

| Export | Chữ ký | Mục đích |
|--------|--------|----------|
| `__kmod_info` | `() -> long long` | trả metadata JSON đã đóng gói |
| `on_load` | `() -> void` | gọi `on_load()` của bạn |
| `on_unload` | `() -> void` | gọi `on_unload()` của bạn |
| `__kmod_dispatch` | `(int kind, int token, const char* args, int alen) -> long long` | định tuyến callback host→module |
| `malloc` / `free` | libc | cho host ghi kết quả vào bộ nhớ module |

### Import duy nhất

```cpp
long long host_syscall(const char* verb, int verb_len,
                       const char* args, int args_len);   // env.host_syscall
void      host_free(unsigned result_off);                 // env.host_free
```

### Giá trị trả đóng gói

Cả kết quả `host_syscall` lẫn giá trị module trả về đều dùng một `long long` đóng
gói con trỏ + độ dài:

```
result = (uint64_t(offset) << 32) | uint32_t(length)
```

`offset` là offset trong bộ nhớ tuyến tính wasm của một chuỗi JSON dài `length`
byte; `0` nghĩa là "không có giá trị / null". Sau khi đọc kết quả từ host, SDK gọi
`host_free(offset)` để giải phóng bộ đệm do host cấp.

### Các `kind` của dispatch

`__kmod_dispatch(kind, token, …)` ghép kênh mọi callback host→module theo `kind`:

| `kind` | Bảng callback | Trả về |
|--------|---------------|--------|
| 1 | tool | — |
| 2 | listener sự kiện (trường `data`) | — |
| 3 | RPC responder (trường `data`) | JSON đã đóng gói |
| 4 | config getter | `{"value": …}` đã đóng gói |
| 5 | config setter (trường `value`) | — |
| 6 | tác vụ theo lịch | — |
| 7 | tác vụ retry | `{"ok": bool}` đã đóng gói |

`token` đánh chỉ số vào bảng riêng theo module mà SDK đã điền khi bạn đăng ký
callback.

### Metadata & phiên bản

`KModInfo` (trong `ModInfo.hpp`) là hợp đồng nhị phân: magic `KMODINFO`,
`struct_ver = 3`, mask quyền 64-bit, và các field định danh độ rộng cố định
(`sizeof(KModInfo) == 840`). Nếu host và SDK bất đồng về struct version, host từ
chối module thay vì đọc sai.

---

## 11. Hot-reload

Host có thể reload module tại chỗ, chuyển trạng thái từ instance cũ sang mới:

```cpp
void on_unload() override {
    if (kmod::reload_active())                 // có đang reload (khác với gỡ hẳn)?
        kmod::reload_save(serialise_state());  // cất một blob cho instance kế tiếp
}

void on_load() override {
    std::string blob = kmod::reload_load();    // khôi phục (rỗng nếu không phải reload)
    if (!blob.empty()) restore_state(blob);
}
```

---

## 12. Lỗi thường gặp

| Triệu chứng | Nguyên nhân & cách sửa |
|-------------|------------------------|
| Host không nhận diện module | Thiếu `MOD_REGISTER(TênClass)` ở phạm vi file. |
| Module bị từ chối nạp | `.wasm` chưa ký — build với `KMOD_SIGN_MODULES=ON`, hoặc tắt kiểm tra trong `seccfg` của host. |
| Một syscall trả rỗng / không làm gì | Thiếu quyền — thêm bit vào `KMOD_MODULE_INFO` và để được cấp. |
| Lỗi biên dịch về `<math.h>`/`asm/types.h` | Lọt include hệ thống — dùng `-DWASI_SDK=…`, hoặc đặt `WASI_SYSROOT` cho clang hệ thống (xem §2). |
| Macro hỏng vì dấu phẩy | Khởi tạo `{}` có dấu phẩy bên trong đối số macro, vd `KEMIT("e", {{"k", v}})`. Bọc lại: `KEMIT("e", (nlohmann::json{{"k", v}}))`. |
| Crash thay vì ném lỗi trong codec | Module build `-fno-exceptions`; `ProtoCodec` abort (trap VM) khi dữ liệu sai. Kiểm tra trước. |
