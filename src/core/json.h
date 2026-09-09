// =====================================================================================================
// TAPESTRY · src/core/json.h · the lossless writer
//
// Blueprint §3.1: "The writer must be total and lossless: every C0 byte and carriage return is
// escaped, UTF-8 is validated at the boundary, and a non-UTF-8 column value is carried base64 with a
// type tag or refused with a typed reason. The reused escaper deletes control bytes and passes
// invalid UTF-8; it is replaced."
//
// This is that replacement. Three properties it must have, because the tape's bytes ARE the truth:
//   1. TOTAL      — every byte sequence a caller can hand it either becomes a row or becomes a typed
//                   refusal. It never silently drops a byte (fusord's jesc drops '\r' outright).
//   2. LOSSLESS   — the bytes read back out are the bytes put in. Invalid UTF-8 survives as base64
//                   under a type tag, so a Latin-1 column from a world system is carried, not eaten.
//   3. ORDERED    — object members are emitted in INSERTION ORDER. There is no sort and there is no
//                   canonicalization: v0 hashes the literal on-disk bytes, so the writer's only job
//                   is to be reproducible, which insertion order already is.
//
// Deliberately NOT here: a parser. The tape's readers are byte-level (verify_chain.py's shape), and
// the header is read by the fixed-order scanner in tape/entry.h, which refuses a row that does not
// have exactly the wire order rather than accepting whatever a permissive parser would take.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <utility>
#include <cmath>
#include "bytes.h"

namespace tapestry {
namespace json {

// The type tag a non-UTF-8 payload is carried under. A reader that sees an object with exactly this
// one member knows the value is raw bytes, not text.
inline const char* const B64_TAG = "$b64";

struct Value {
    enum T { Null, Bool, I64, U64, F64, Str, Bytes, Arr, Obj };
    T t = Null;
    bool        b = false;
    int64_t     i = 0;
    uint64_t    u = 0;
    double      f = 0.0;
    std::string s;                                        // Str text, or Bytes payload
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;       // insertion order is the wire order

    Value() = default;

    static Value null()                    { Value v; v.t = Null; return v; }
    static Value boolean(bool x)           { Value v; v.t = Bool; v.b = x; return v; }
    static Value i64(int64_t x)            { Value v; v.t = I64;  v.i = x; return v; }
    static Value u64(uint64_t x)           { Value v; v.t = U64;  v.u = x; return v; }
    static Value f64(double x)             { Value v; v.t = F64;  v.f = x; return v; }
    static Value str(std::string x)        { Value v; v.t = Str;  v.s = std::move(x); return v; }
    static Value bytes(std::string x)      { Value v; v.t = Bytes;v.s = std::move(x); return v; }
    static Value array()                   { Value v; v.t = Arr;  return v; }
    static Value object()                  { Value v; v.t = Obj;  return v; }

    Value& add(Value x)                    { arr.push_back(std::move(x)); return *this; }
    Value& set(std::string k, Value x)     { obj.emplace_back(std::move(k), std::move(x)); return *this; }

    const Value* find(const std::string& k) const {
        for (const auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct WriteOpts {
    // What to do with a string whose bytes are not valid UTF-8.
    enum Utf8Policy { CarryBase64, RefuseInvalid };
    Utf8Policy utf8 = CarryBase64;
    // Floats on the tape are a determinism hazard (a fold that sums them is not portable, QC-1/QC-2's
    // FMA probes). Paths that feed folds set this false and get a typed refusal instead of a row.
    bool allow_f64 = true;
    // A duplicate member name is lossy for every reader; it is a defect at the boundary, not on disk.
    bool refuse_duplicate_keys = true;
};

// The typed reasons this writer can refuse with. They are refusal reasons in the transactor's
// entry class (blueprint §4.1), so they are spelled here once and reused there.
inline const char* const R_INVALID_UTF8      = "invalid_utf8";
inline const char* const R_NONFINITE_FLOAT   = "nonfinite_float";
inline const char* const R_FLOAT_FORBIDDEN   = "float_forbidden";
inline const char* const R_DUPLICATE_KEY     = "duplicate_key";
inline const char* const R_BODY_NOT_OBJECT   = "body_not_object";

// Escape one string's bytes into a JSON string literal, assuming they are already valid UTF-8.
// Every C0 byte (0x00..0x1F) is escaped — the short forms where JSON has them, \u00XX otherwise —
// and 0x7F is escaped too so a row never carries a bare DEL. Nothing is ever dropped.
inline void escape_utf8_into(const std::string& in, std::string& out) {
    static const char* H = "0123456789abcdef";
    out += '"';
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\"";  break;
            case '\\': out += "\\\\";  break;
            case '\b': out += "\\b";   break;
            case '\f': out += "\\f";   break;
            case '\n': out += "\\n";   break;
            case '\r': out += "\\r";   break;   // fusord deleted this byte; here it survives
            case '\t': out += "\\t";   break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    out += "\\u00"; out += H[(c >> 4) & 15]; out += H[c & 15];
                } else {
                    out += (char)c;
                }
        }
    }
    out += '"';
}

// A base64 payload under its type tag: {"$b64":"...."}
inline void bytes_into(const std::string& raw, std::string& out) {
    out += "{\""; out += B64_TAG; out += "\":\"";
    out += b64_encode(raw);
    out += "\"}";
}

// Integers are written bare. u64 above 2^53 is exact on the tape (the bytes are the truth) but is
// NOT exact for a reader that parses JSON numbers as doubles; the v1 binary form in §3.1 removes the
// hazard, and until then the fixed-order scanner reads these fields as text, never as doubles.
inline void u64_into(uint64_t v, std::string& out) {
    char b[24]; std::snprintf(b, sizeof(b), "%llu", (unsigned long long)v); out += b;
}
inline void i64_into(int64_t v, std::string& out) {
    char b[24]; std::snprintf(b, sizeof(b), "%lld", (long long)v); out += b;
}

inline bool write_value(const Value& v, std::string& out, const WriteOpts& o, std::string* reason) {
    auto fail = [&](const char* r) { if (reason) *reason = r; return false; };
    switch (v.t) {
        case Value::Null: out += "null"; return true;
        case Value::Bool: out += (v.b ? "true" : "false"); return true;
        case Value::I64:  i64_into(v.i, out); return true;
        case Value::U64:  u64_into(v.u, out); return true;
        case Value::F64: {
            if (!o.allow_f64) return fail(R_FLOAT_FORBIDDEN);
            if (!std::isfinite(v.f)) return fail(R_NONFINITE_FLOAT);   // NaN and Inf have no JSON form
            char b[40]; std::snprintf(b, sizeof(b), "%.17g", v.f); out += b; return true;
        }
        case Value::Str: {
            size_t bad = 0;
            if (!utf8_valid(v.s, &bad)) {
                if (o.utf8 == WriteOpts::RefuseInvalid) return fail(R_INVALID_UTF8);
                bytes_into(v.s, out);                                  // carried, never dropped
                return true;
            }
            escape_utf8_into(v.s, out); return true;
        }
        case Value::Bytes: bytes_into(v.s, out); return true;
        case Value::Arr: {
            out += '[';
            for (size_t k = 0; k < v.arr.size(); ++k) {
                if (k) out += ',';
                if (!write_value(v.arr[k], out, o, reason)) return false;
            }
            out += ']'; return true;
        }
        case Value::Obj: {
            if (o.refuse_duplicate_keys && v.obj.size() > 1 && v.obj.size() < 512) {
                for (size_t a = 0; a + 1 < v.obj.size(); ++a)
                    for (size_t b2 = a + 1; b2 < v.obj.size(); ++b2)
                        if (v.obj[a].first == v.obj[b2].first) return fail(R_DUPLICATE_KEY);
            }
            out += '{';
            for (size_t k = 0; k < v.obj.size(); ++k) {
                if (k) out += ',';
                size_t bad = 0;
                if (!utf8_valid(v.obj[k].first, &bad)) return fail(R_INVALID_UTF8);   // a key is never base64
                escape_utf8_into(v.obj[k].first, out);
                out += ':';
                if (!write_value(v.obj[k].second, out, o, reason)) return false;
            }
            out += '}'; return true;
        }
    }
    return fail("unreachable_value_type");
}

inline bool write(const Value& v, std::string& out, const WriteOpts& o, std::string* reason) {
    return write_value(v, out, o, reason);
}

} // namespace json
} // namespace tapestry
