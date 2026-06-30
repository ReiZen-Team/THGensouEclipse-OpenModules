#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// ── Failure path: throw with exceptions, abort (→ VM trap) without ─────────────
#if defined(__cpp_exceptions)
    #define PROTO_FAIL(msg) throw std::runtime_error(msg)
#else
    #include <cstdlib>
    #define PROTO_FAIL(msg) std::abort()
#endif

using schema_t = nlohmann::ordered_json;

namespace ProtoCodec {

// ── Wire types ──────────────
constexpr int WT_VARINT = 0;
constexpr int WT_I64    = 1;
constexpr int WT_LEN    = 2;
constexpr int WT_I32    = 5;

// ── Varint helpers ───────────

inline void write_varint(std::string& out, uint64_t v) {
    do {
        uint8_t b = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;
        if (v) b |= 0x80;
        out += static_cast<char>(b);
    } while (v);
}

inline uint64_t read_varint(const uint8_t* data, size_t size, size_t& pos) {
    uint64_t result = 0;
    int shift = 0;
    while (pos < size) {
        uint8_t b = data[pos++];
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return result;
        shift += 7;
        if (shift >= 64) PROTO_FAIL("proto: varint overflow");
    }
    PROTO_FAIL("proto: truncated varint");
}

inline void write_tag(std::string& out, int field_num, int wire_type) {
    write_varint(out, static_cast<uint64_t>((field_num << 3) | wire_type));
}

inline void skip_field(const uint8_t* d, size_t sz, size_t& pos, int wt) {
    switch (wt) {
        case WT_VARINT:
            while (pos < sz && (d[pos++] & 0x80));
            break;
        case WT_I64: pos += 8; break;
        case WT_LEN: {
            uint64_t len = read_varint(d, sz, pos);
            if (len > sz - pos) PROTO_FAIL("proto: length exceeds buffer");
            pos += static_cast<size_t>(len);
            break;
        }
        case WT_I32: pos += 4; break;
        default:
            PROTO_FAIL("proto: unknown wire type " + std::to_string(wt));
    }
}

// ── Encoder ─────

inline std::string encode(const nlohmann::json& data, const schema_t& schema);

inline void encode_value(std::string& out,
                         int field_num,
                         const nlohmann::json& val,
                         const schema_t& type_or_sub)
{
    if (type_or_sub.is_string()) {
        // ── Primitive ──────────────────
        const std::string& t = type_or_sub.get_ref<const std::string&>();

        if (t == "string") {
            const std::string& s = val.get_ref<const std::string&>();
            write_tag(out, field_num, WT_LEN);
            write_varint(out, s.size());
            out += s;
        } else if (t == "bool") {
            write_tag(out, field_num, WT_VARINT);
            write_varint(out, val.get<bool>() ? 1u : 0u);
        } else if (t == "double") {
            double d = val.get<double>();
            write_tag(out, field_num, WT_I64);
            char buf[8]; std::memcpy(buf, &d, 8);
            out.append(buf, 8);
        } else if (t == "float") {
            float f = static_cast<float>(val.get<double>());
            write_tag(out, field_num, WT_I32);
            char buf[4]; std::memcpy(buf, &f, 4);
            out.append(buf, 4);
        } else {
            int64_t iv = val.is_number_integer() ? val.get<int64_t>()
                                                  : static_cast<int64_t>(val.get<double>());
            write_tag(out, field_num, WT_VARINT);
            write_varint(out, static_cast<uint64_t>(iv));
        }
    } else if (type_or_sub.is_object()) {
        // ── Sub-message ─────────────────
        std::string sub = encode(val, type_or_sub);
        write_tag(out, field_num, WT_LEN);
        write_varint(out, sub.size());
        out += sub;
    }
}

inline std::string encode(const nlohmann::json& data, const schema_t& schema) {
    std::string out;
    int field_num = 1;

    for (auto it = schema.begin(); it != schema.end(); ++it, ++field_num) {
        const std::string& key       = it.key();
        const schema_t&    type_info = it.value();

        if (!data.contains(key) || data[key].is_null()) continue;

        const nlohmann::json& val = data[key];

        if (val.is_array()) {
            for (const auto& elem : val) {
                encode_value(out, field_num, elem, type_info);
            }
        } else {
            encode_value(out, field_num, val, type_info);
        }
    }
    return out;
}

// ── Decoder ───────────

inline nlohmann::json decode(const std::string& bytes, const schema_t& schema) {
    const uint8_t* d  = reinterpret_cast<const uint8_t*>(bytes.data());
    const size_t   sz = bytes.size();

    struct FieldInfo { std::string name; schema_t type_info; };
    std::vector<FieldInfo> fields;
    fields.reserve(schema.size());
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        fields.push_back({ it.key(), it.value() });
    }

    nlohmann::json result = nlohmann::json::object();
    size_t pos = 0;

    while (pos < sz) {
        uint64_t tag_val  = read_varint(d, sz, pos);
        int field_num     = static_cast<int>(tag_val >> 3);
        int wire_type     = static_cast<int>(tag_val & 0x7);

        if (field_num < 1 || field_num > static_cast<int>(fields.size())) {
            skip_field(d, sz, pos, wire_type);
            continue;
        }

        const FieldInfo& fi = fields[field_num - 1];
        const std::string& name = fi.name;
        const schema_t& ti     = fi.type_info;

        nlohmann::json decoded_val;

        if (ti.is_string()) {
            // ── Primitive ───────────────
            const std::string& t = ti.get_ref<const std::string&>();

            if (wire_type == WT_VARINT) {
                uint64_t v = read_varint(d, sz, pos);
                if (t == "bool")  decoded_val = (v != 0);
                else              decoded_val = static_cast<int64_t>(v);
            } else if (wire_type == WT_LEN) {
                uint64_t len = read_varint(d, sz, pos);
                if (len > sz - pos) PROTO_FAIL("proto: length exceeds buffer");
                decoded_val = std::string(reinterpret_cast<const char*>(d + pos), len);
                pos += static_cast<size_t>(len);
            } else if (wire_type == WT_I64) {
                double dbl; std::memcpy(&dbl, d + pos, 8); pos += 8;
                decoded_val = dbl;
            } else if (wire_type == WT_I32) {
                float fl; std::memcpy(&fl, d + pos, 4); pos += 4;
                decoded_val = static_cast<double>(fl);
            } else {
                skip_field(d, sz, pos, wire_type); continue;
            }
        } else if (ti.is_object() && wire_type == WT_LEN) {
            // ── Sub-message ─────────────
            uint64_t len = read_varint(d, sz, pos);
            if (len > sz - pos) PROTO_FAIL("proto: length exceeds buffer");
            std::string sub(reinterpret_cast<const char*>(d + pos), len);
            pos += static_cast<size_t>(len);
            decoded_val = decode(sub, ti);
        } else {
            skip_field(d, sz, pos, wire_type); continue;
        }

        if (result.contains(name)) {
            if (!result[name].is_array()) {
                result[name] = nlohmann::json::array({ result[name] });
            }
            result[name].push_back(std::move(decoded_val));
        } else {
            result[name] = std::move(decoded_val);
        }
    }
    return result;
}

// ── gRPC-over-HTTP/2 framing ────────────

inline void write_be32(char* dst, uint32_t v) {
    dst[0] = static_cast<char>((v >> 24) & 0xFF);
    dst[1] = static_cast<char>((v >> 16) & 0xFF);
    dst[2] = static_cast<char>((v >>  8) & 0xFF);
    dst[3] = static_cast<char>( v        & 0xFF);
}

inline uint32_t read_be32(const char* src) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(src[0])) << 24)
         | (static_cast<uint32_t>(static_cast<uint8_t>(src[1])) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(src[2])) <<  8)
         |  static_cast<uint32_t>(static_cast<uint8_t>(src[3]));
}

inline std::string grpc_frame(const std::string& proto_body) {
    std::string frame(5, '\0');
    frame[0] = 0x00;
    write_be32(&frame[1], static_cast<uint32_t>(proto_body.size()));
    return frame + proto_body;
}

inline std::string grpc_unframe(const std::string& raw) {
    if (raw.size() < 5)
        PROTO_FAIL("grpc_unframe: frame too short");
    uint32_t len = read_be32(raw.data() + 1);
    if (raw.size() < static_cast<size_t>(5 + len))
        PROTO_FAIL("grpc_unframe: truncated body");
    return raw.substr(5, len);
}

}
