// =====================================================================================================
// TAPESTRY · src/tape/entry.h · the v0 wire form of a tape entry, and the strict scanner that reads it
//
// Blueprint §3.1. One JSON object per line, written in a FIXED FIELD ORDER, with `prev` and `h`
// appended last:
//
//   {"pos":N,"term":T,"t_epoch_ns":E,"k":"kind","cell":C,"cls":L,"by":"who","body":{...},"prev":"…","h":"…"}
//
// The hashed bytes are the literal on-disk bytes of the row preceding `,"prev":"`, byte for byte, and
//   h = blake2b256(prev_hex ‖ those bytes)
// which is exactly fusord's `chain_hash` over `Tape::put`'s body and exactly what the estate's
// verify_chain.py checks. There is no canonicalization step and the word does not appear here.
//
// THE SCANNER IS STRICT ON PURPOSE. It does not parse JSON; it asserts the wire order literally,
// field by field. A row that a permissive parser would accept but that this writer would never have
// produced is a malformed row, and saying so is the point: QC-1 F6 showed a verifier that accepts
// either answer is not a falsifier.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <climits>
#include <string>
#include "../core/json.h"
#include "../core/bytes.h"

namespace tapestry {

// ---- kinds -------------------------------------------------------------------------------------
// §3.1's table, plus two system kinds R0 needs and the blueprint's prose already uses:
//   `seg`  — the first entry of every segment, carrying `first_pos` and `prev_of_first_entry`
//            (§4.2 requires a segment header; making it a CHAINED ENTRY rather than a sidecar means
//            the chain crosses a roll with zero seams and the header cannot be lost separately from
//            the segment. Recorded as an R0 delta against §3.1's kind table for the next QC.)
//   `warn` — an operational record: torn bytes discarded at open, a rename that would not take.
//            §4.2 and §4.1 both name a `warn` entry; the kind table omits it. Same delta.
namespace kind {
inline const char* const TX      = "tx";
inline const char* const MARGIN  = "margin";
inline const char* const VERB    = "verb";
inline const char* const HOLD    = "hold";
inline const char* const REFUSE  = "refuse";
inline const char* const TICK    = "tick";
inline const char* const TERM    = "term";
inline const char* const SNAPSHOT= "snapshot";
inline const char* const JUDGE   = "judge";
inline const char* const DISSENT = "dissent";
inline const char* const SEG     = "seg";
inline const char* const WARN    = "warn";
}

// ---- provenance --------------------------------------------------------------------------------
namespace by {
inline const char* const SYSTEM = "system";
inline std::string seat(const std::string& id)      { return "seat:" + id; }
inline std::string seam(const std::string& codehash){ return "seam:" + codehash; }
inline std::string effector(const std::string& id)  { return "effector:" + id; }
inline std::string rule(const std::string& key_id)  { return "rule:" + key_id; }
inline std::string judge(const std::string& weight_sha256, const std::string& serve_pin) {
    return "judge:" + weight_sha256 + "@" + serve_pin;
}
}

// ---- the header, in wire order -------------------------------------------------------------------
struct EntryHdr {
    uint64_t    pos = 0;
    uint32_t    term = 0;
    uint64_t    t_epoch_ns = 0;
    std::string k;
    uint64_t    cell = 0;
    uint32_t    cls = 0;
    std::string by;
};

// What the writer produces for one entry. `prefix` is the hashed byte range; `line` is prefix plus
// the prev/h tail plus '\n'.
struct BuiltRow {
    std::string prefix;
    std::string line;
    std::string h;
};

inline const char* const PREV_MARK = ",\"prev\":\"";

// Build one row. Returns false with a typed reason from json.h (invalid_utf8, float_forbidden,
// duplicate_key, body_not_object) — the boundary refuses rather than writing a lossy row.
inline bool build_row(const EntryHdr& hd, const json::Value& body, const json::WriteOpts& opts,
                      const std::string& prev_hex, BuiltRow* out, std::string* reason) {
    if (body.t != json::Value::Obj) { if (reason) *reason = json::R_BODY_NOT_OBJECT; return false; }
    if (!is_hex64(prev_hex))        { if (reason) *reason = "bad_prev"; return false; }

    std::string p;
    p.reserve(256 + body.obj.size() * 32);
    p += "{\"pos\":";        json::u64_into(hd.pos, p);
    p += ",\"term\":";       json::u64_into((uint64_t)hd.term, p);
    p += ",\"t_epoch_ns\":"; json::u64_into(hd.t_epoch_ns, p);
    p += ",\"k\":";
    { size_t bad = 0; if (!utf8_valid(hd.k, &bad)) { if (reason) *reason = json::R_INVALID_UTF8; return false; }
      json::escape_utf8_into(hd.k, p); }
    p += ",\"cell\":";       json::u64_into(hd.cell, p);
    p += ",\"cls\":";        json::u64_into((uint64_t)hd.cls, p);
    p += ",\"by\":";
    { size_t bad = 0; if (!utf8_valid(hd.by, &bad)) { if (reason) *reason = json::R_INVALID_UTF8; return false; }
      json::escape_utf8_into(hd.by, p); }
    p += ",\"body\":";
    if (!json::write_value(body, p, opts, reason)) return false;

    const std::string h = chain_hash(prev_hex, p);
    out->prefix = p;
    out->h = h;
    out->line = p;
    out->line += PREV_MARK; out->line += prev_hex;
    out->line += "\",\"h\":\""; out->line += h; out->line += "\"}\n";
    return true;
}

// ---- the strict scanner --------------------------------------------------------------------------
struct ScannedRow {
    EntryHdr    hdr;
    std::string prefix;     // the hashed bytes
    std::string body;       // the raw JSON text of `body`, unparsed
    std::string prev;
    std::string h;
};

namespace detail {
inline bool eat_lit(const std::string& s, size_t& i, const char* lit) {
    const size_t n = std::char_traits<char>::length(lit);
    if (s.compare(i, n, lit) != 0) return false;
    i += n; return true;
}
inline bool eat_u64(const std::string& s, size_t& i, uint64_t& out) {
    const size_t start = i;
    uint64_t v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        const uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (0xFFFFFFFFFFFFFFFFull - d) / 10ull) return false;   // never truncate a clock
        v = v * 10ull + d; ++i;
    }
    if (i == start) return false;
    if (i - start > 1 && s[start] == '0') return false;              // no leading zeros: one spelling only
    out = v; return true;
}
inline bool eat_i64(const std::string& s, size_t& i, int64_t& out) {
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; ++i; }
    uint64_t v = 0;
    if (!eat_u64(s, i, v)) return false;
    if (neg) { if (v > 9223372036854775808ull) return false; out = (v == 9223372036854775808ull) ? INT64_MIN : -(int64_t)v; }
    else     { if (v > 9223372036854775807ull) return false; out = (int64_t)v; }
    return true;
}
inline bool eat_bool(const std::string& s, size_t& i, bool& out) {
    if (s.compare(i, 4, "true") == 0)  { i += 4; out = true;  return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = false; return true; }
    return false;
}
// A JSON string literal, unescaped back into raw bytes. Rejects anything this writer would not emit.
inline bool eat_string(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i; out.clear();
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') { ++i; return true; }
        if (c == '\\') {
            if (i + 1 >= s.size()) return false;
            const char e = s[i + 1];
            switch (e) {
                case '"':  out += '"';  i += 2; break;
                case '\\': out += '\\'; i += 2; break;
                case '/':  out += '/';  i += 2; break;
                case 'b':  out += '\b'; i += 2; break;
                case 'f':  out += '\f'; i += 2; break;
                case 'n':  out += '\n'; i += 2; break;
                case 'r':  out += '\r'; i += 2; break;
                case 't':  out += '\t'; i += 2; break;
                case 'u': {
                    if (i + 5 >= s.size()) return false;
                    int v = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int d = hex_nib(s[i + 2 + k]);
                        if (d < 0) return false;
                        v = (v << 4) | d;
                    }
                    if (v >= 0x80) return false;      // this writer only ever \u-escapes C0 and DEL
                    out += (char)v; i += 6; break;
                }
                default: return false;
            }
        } else {
            if ((unsigned char)c < 0x20) return false;   // a raw control byte in a string is malformed
            out += c; ++i;
        }
    }
    return false;
}
// Skip one JSON value, tracking nesting, so `body` can be lifted out verbatim without parsing it.
inline bool skip_value(const std::string& s, size_t& i) {
    int depth = 0;
    bool in_str = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (in_str) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{' || c == '[') { ++depth; continue; }
        if (c == '}' || c == ']') {
            --depth;
            if (depth == 0) { ++i; return true; }
            if (depth < 0) return false;
            continue;
        }
        if (depth == 0 && (c == ',')) return true;   // a scalar value ended
    }
    return depth == 0;
}
} // namespace detail

// Scan one row (no trailing newline). Verifies the wire order, lifts the header, the body text, prev
// and h, and — when `check_hash` — recomputes the chain hash over the literal prefix.
inline bool scan_row(const std::string& line, ScannedRow* out, std::string* reason, bool check_hash = true) {
    auto fail = [&](const char* r) { if (reason) *reason = r; return false; };
    const size_t mark = line.rfind(PREV_MARK);
    if (mark == std::string::npos) return fail("no_prev_field");
    out->prefix = line.substr(0, mark);
    // The tail is fixed-width by construction:  <64 hex> ","h":" <64 hex> "}   = 64+7+64+2 = 137 bytes.
    const std::string tail = line.substr(mark + std::char_traits<char>::length(PREV_MARK));
    if (tail.size() != 137) return fail("bad_tail_width");
    out->prev = tail.substr(0, 64);
    if (!is_hex64(out->prev)) return fail("bad_prev");
    if (tail.compare(64, 7, "\",\"h\":\"") != 0) return fail("no_h_field");
    out->h = tail.substr(71, 64);
    if (!is_hex64(out->h)) return fail("bad_h");
    if (tail.compare(135, 2, "\"}") != 0) return fail("bad_row_end");

    size_t i = 0;
    const std::string& p = out->prefix;
    if (!detail::eat_lit(p, i, "{\"pos\":"))          return fail("order_pos");
    if (!detail::eat_u64(p, i, out->hdr.pos))         return fail("bad_pos");
    if (!detail::eat_lit(p, i, ",\"term\":"))         return fail("order_term");
    { uint64_t v; if (!detail::eat_u64(p, i, v) || v > 0xFFFFFFFFull) return fail("bad_term");
      out->hdr.term = (uint32_t)v; }
    if (!detail::eat_lit(p, i, ",\"t_epoch_ns\":"))   return fail("order_t_epoch_ns");
    if (!detail::eat_u64(p, i, out->hdr.t_epoch_ns))  return fail("bad_t_epoch_ns");
    if (!detail::eat_lit(p, i, ",\"k\":"))            return fail("order_k");
    if (!detail::eat_string(p, i, out->hdr.k))        return fail("bad_k");
    if (!detail::eat_lit(p, i, ",\"cell\":"))         return fail("order_cell");
    if (!detail::eat_u64(p, i, out->hdr.cell))        return fail("bad_cell");
    if (!detail::eat_lit(p, i, ",\"cls\":"))          return fail("order_cls");
    { uint64_t v; if (!detail::eat_u64(p, i, v) || v > 0xFFFFFFFFull) return fail("bad_cls");
      out->hdr.cls = (uint32_t)v; }
    if (!detail::eat_lit(p, i, ",\"by\":"))           return fail("order_by");
    if (!detail::eat_string(p, i, out->hdr.by))       return fail("bad_by");
    if (!detail::eat_lit(p, i, ",\"body\":"))         return fail("order_body");
    const size_t bstart = i;
    if (!detail::skip_value(p, i))                    return fail("bad_body");
    if (i != p.size())                                return fail("trailing_after_body");
    out->body = p.substr(bstart, i - bstart);

    if (check_hash) {
        const std::string want = chain_hash(out->prev, out->prefix);
        if (want != out->h) return fail("hash_mismatch");
    }
    return true;
}

} // namespace tapestry
