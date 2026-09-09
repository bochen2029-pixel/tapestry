// =====================================================================================================
// TAPESTRY · src/net/protocol.h · one request grammar, one response grammar
//
// §4.8 lists twelve calls; R0 serves the four it has something true to say about — `write`, `tick`,
// `health` and `head`. The rest are not stubbed: a call that answers with a placeholder is worse than
// a call that is absent, because a client will believe it.
//
// One shape each way, every field always present, fixed order, read by the same strict scanner the
// tape uses. `body` is the tape's own tx body, byte for byte — the wire and the log speak one
// language, so there is no second grammar to drift out of step with the first.
// =====================================================================================================
#pragma once

#include <string>
#include "../core/json.h"
#include "../tape/entry.h"

namespace tapestry {
namespace net {

struct Request {
    std::string call;    // write | tick | health | head
    std::string by;      // the principal, for write
    std::string arg;     // the cadence rule, for tick
    std::string body;    // raw tx-body text, or "" when there is none
};

struct Response {
    bool        ok = false;
    uint64_t    pos = 0;
    std::string h;
    std::string refusal;        // entry-class reason
    std::string constraint_id;
    std::string fault;          // counter-class reason
    std::string info;           // health and head render here
};

inline std::string enc_request(const Request& r) {
    std::string out = "{\"call\":";
    json::escape_utf8_into(r.call, out);
    out += ",\"by\":";  json::escape_utf8_into(r.by, out);
    out += ",\"arg\":"; json::escape_utf8_into(r.arg, out);
    out += ",\"body\":";
    if (r.body.empty()) out += "null"; else out += r.body;
    out += "}";
    return out;
}

inline bool dec_request(const std::string& s, Request* r) {
    using namespace detail;
    size_t i = 0;
    if (!eat_lit(s, i, "{\"call\":") || !eat_string(s, i, r->call)) return false;
    if (!eat_lit(s, i, ",\"by\":")   || !eat_string(s, i, r->by))   return false;
    if (!eat_lit(s, i, ",\"arg\":")  || !eat_string(s, i, r->arg))  return false;
    if (!eat_lit(s, i, ",\"body\":")) return false;
    if (s.compare(i, 4, "null") == 0) { i += 4; r->body.clear(); }
    else {
        const size_t start = i;
        if (!skip_value(s, i)) return false;
        r->body = s.substr(start, i - start);
    }
    return eat_lit(s, i, "}") && i == s.size();
}

inline std::string enc_response(const Response& r) {
    std::string out = "{\"ok\":";
    out += (r.ok ? "true" : "false");
    out += ",\"pos\":"; json::u64_into(r.pos, out);
    out += ",\"h\":";             json::escape_utf8_into(r.h, out);
    out += ",\"refusal\":";       json::escape_utf8_into(r.refusal, out);
    out += ",\"constraint_id\":"; json::escape_utf8_into(r.constraint_id, out);
    out += ",\"fault\":";         json::escape_utf8_into(r.fault, out);
    out += ",\"info\":";          json::escape_utf8_into(r.info, out);
    out += "}";
    return out;
}

inline bool dec_response(const std::string& s, Response* r) {
    using namespace detail;
    size_t i = 0;
    if (!eat_lit(s, i, "{\"ok\":") || !eat_bool(s, i, r->ok)) return false;
    if (!eat_lit(s, i, ",\"pos\":") || !eat_u64(s, i, r->pos)) return false;
    if (!eat_lit(s, i, ",\"h\":")             || !eat_string(s, i, r->h)) return false;
    if (!eat_lit(s, i, ",\"refusal\":")       || !eat_string(s, i, r->refusal)) return false;
    if (!eat_lit(s, i, ",\"constraint_id\":") || !eat_string(s, i, r->constraint_id)) return false;
    if (!eat_lit(s, i, ",\"fault\":")         || !eat_string(s, i, r->fault)) return false;
    if (!eat_lit(s, i, ",\"info\":")          || !eat_string(s, i, r->info)) return false;
    return eat_lit(s, i, "}") && i == s.size();
}

} // namespace net
} // namespace tapestry
