"""QC probes for TAPESTRY blueprint sections 2.1 / 3.1 / 3.2 / 4 / 6 / falsifiers 1,3,4,9.
Pure stdlib. Run: python C:/TAPESTRY/qc/scratch/tape/probe.py
"""
import hashlib, json, struct, math

def chain_hash(prev_hex, body):
    """fusord.cpp:312 -- blake2b256(prev_hex_ascii || body_bytes)."""
    b = hashlib.blake2b(digest_size=32)
    b.update(prev_hex.encode('ascii'))
    b.update(body.encode('utf-8'))
    return b.hexdigest()

GENESIS = '0' * 64
print('== 1. fusord chain construction, reproduced ==')
# Tape::put(body) writes: body + ',"prev":"P","h":"H"}\n' and hashes only `body`.
body = '{"k":"tick","ms":12,"gap_s":3'
h = chain_hash(GENESIS, body)
row = body + ',"prev":"%s","h":"%s"}' % (GENESIS, h)
print('  body hashed :', body)
print('  row on disk :', row[:60] + '...')
print('  h           :', h)
print('  -> prev and h are NOT inside the hashed bytes; the hashed bytes are the')
print('     literal on-disk prefix in EMISSION order, not any canonical order.')

print()
print('== 2. can a key-sorted canonical row carry its own h? ==')
fields = ['body', 'by', 'cell', 'cls', 'h', 'k', 'pos', 'prev', 't_mono_ns', 't_wall_ms']
print('  blueprint 2.1 fields, sorted:', ','.join(sorted(fields)))
print('  index of "h" in sorted order :', sorted(fields).index('h'), 'of', len(fields))
print('  -> in canonical (sorted) order h lands in the MIDDLE. A writer cannot emit')
print('     h before it has hashed the bytes that follow it. So either the on-disk')
print('     bytes are NOT canonical (and a byte-level verifier such as verify_chain.py')
print('     fails) or h cannot be a member of the canonical object.')

print()
print('== 3. float shortest-round-trip: same digits, different renderings ==')
cases = [1e16, 1e21, 1e-5, 1e-7, 0.1, -0.0, 1/3, 5e-324, 1.7976931348623157e308,
         float(2**53), 123456789012345678.0]
for v in cases:
    py = repr(v)
    # ECMAScript Number::toString (what RFC 8785 mandates) differs at the exponent
    # thresholds: JS uses exponent form for |x|>=1e21 or <1e-6, and writes e+21 / e-7
    # with NO zero padding. Python pads the exponent to two digits (1e-05).
    print('  %-24s python repr=%-26s' % (v, py))
print('  -> Python pads exponents to 2 digits ("1e-05"); ECMAScript/RFC8785 does not ("1e-5").')
print('     Python switches to exponent form at 1e16; ECMAScript at 1e21.')
print('     C++ std::to_chars(general) gives yet a third set of thresholds.')
print('     Shortest-round-trip fixes the DIGITS, never the RENDERING. Three')
print('     conforming implementations therefore produce three different hashes.')

print()
print('== 4. uint64 through a JSON number ==')
pos = 2**63 + 12345
s = json.dumps({'pos': pos})
print('  python json.dumps  :', s)
print('  round-trip via float (what a JS/JSON-schema reader does):', int(float(pos)))
print('  lossless?          :', int(float(pos)) == pos)
print('  -> `pos` is uint64. Any reader whose JSON numbers are IEEE doubles silently')
print('     truncates above 2**53 = %d. The chain then verifies on one reader and not' % (2**53))
print('     another, with no error raised.')

print()
print('== 5. fusord jesc() is LOSSY on control bytes (fusord.cpp:187-195) ==')
def jesc(s_bytes):
    """Faithful port of fusord.cpp:187. Input is a byte string (char, signed on MSVC)."""
    o = []
    for byte in s_bytes:
        c = byte if byte < 128 else byte - 256   # C++ signed char
        ch = chr(byte)
        if ch == '"':   o.append('\\"')
        elif ch == '\\': o.append('\\\\')
        elif ch == '\n': o.append('\\n')
        elif ch == '\t': o.append('\\t')
        elif ch == '\r': pass                     # DROPPED
        elif byte >= 0x20 or c < 0: o.append(ch)  # else: DROPPED
    return ''.join(o)

payload = b'ok\x01\x02BEL\x07\rEND\x1f!'
print('  input bytes :', payload)
print('  after jesc  :', repr(jesc(payload)))
print('  bytes lost  :', len(payload) - len(jesc(payload).replace('\\\\','\\')))
print('  -> 0x00-0x1F (other than \\n and \\t) and every \\r are SILENTLY DELETED.')
print('     The chain then attests to a value that is not what the world said.')
print('     A `fact` row carrying before/after from a real column loses those bytes.')

print()
print('== 6. jesc() does not validate UTF-8 ==')
bad = b'caf\xe9'          # latin-1 e-acute, not valid UTF-8
out = jesc(bad)
print('  latin-1 bytes pass through jesc unchanged (c < 0 branch): ', repr(out))
try:
    json.loads('{"x":"' + out + '"}')
    print('  python json.loads: OK')
except Exception as e:
    print('  python json.loads:', type(e).__name__, e)
print('  -> a C++ writer emits a row a strict reader cannot parse at all, so the')
print('     "two implementations disagree on h" conformance test never even runs.')

print()
print('== 7. falsifier 9: the 32-bit truncation, simulated (osv_ingest.h:284,317-318,337) ==')
class Cell:
    def __init__(self): self.src_rev = 0
def apply(cur, rev):
    """osv_ingest.h:316-319 then :284 -- guard compares uint64 rev to uint32 src_rev."""
    if rev == cur.src_rev: return 'duplicate(idempotent no-op)'
    if rev <  cur.src_rev: return 'stale_refused'
    cur.src_rev = rev & 0xFFFFFFFF          # (uint32_t)r.rev
    return 'applied'
c = Cell()
base = 2**32
for rev, label in [(base + 10, 'fresh #1'),
                   (base + 3,  'STALE (arrived from the past)'),
                   (base + 10, 'REPLAY of #1')]:
    print('  rev=%-12d stored_before=%-6d -> %-28s stored_after=%d'
          % (rev, c.src_rev, apply(c, rev), c.src_rev))
print('  -> a stale row is ACCEPTED and overwrites the present;')
print('     an exact replay is NOT detected and is counted as an update.')
print('  Both halves of falsifier 9 fail once source revisions exceed 2**32.')

print()
print('== 8. does a 64-bit pos_last close it? ==')
print('  Only if pos_last carries the SOURCE revision. The blueprint (2.2) defines')
print('  pos_last as "the tape position that last touched this cell". Tape positions')
print('  are assigned by the transactor at append time and are monotone BY')
print('  CONSTRUCTION, so `rev < pos_last` can never fire for a stale source row:')
print('  the stale row gets a HIGHER tape position than the fresh one it overwrites.')
print('  The 64-byte Cell has no room left for a separate source_rev:')
sizes = [('id',8),('opened_ns',8),('due_ns',8),('blocked_by',8),('pos_last',8),
         ('amount',4),('margin',4),('cls',4),('seg',4),('seat',4),
         ('state/flags/verb/gear',4)]
tot = sum(n for _, n in sizes)
print('   ', ' + '.join('%s(%d)' % (k, n) for k, n in sizes))
print('    = %d bytes; padding left = %d' % (tot, 64 - tot))

print()
print('== 9. checkpoint arithmetic against the receipt ==')
b434, b512 = 60257248, 61616944
marg = (b512 - b434) / (512 - 434)
fixed = b434 - marg * 434
print('  marginal B/token : %.1f  (doc says 17,432)' % marg)
print('  fixed recurrent  : %.3f MB  (doc says about 52.7 MB)' % (fixed / 1e6))
print('  at 24,576 tokens : %.0f MB' % ((fixed + marg * 24576) / 1e6))
print('  at 65,536 tokens : %.2f GB' % ((fixed + marg * 65536) / 1e9))

print()
print('== 10. blueprint 5 sizing: 17 KB/token vs the KV-page budget ==')
root = 8000 * 17432
suffix = 500 * 17432
print('  8,000-token root  : %.1f MB   (blueprint says "about 136 MB")' % (root / 1e6))
print('  500-token suffix  : %.2f MB   (blueprint says "about 8.5 MB")' % (suffix / 1e6))
print('  BUT 17,432 B/token is the MARGINAL cost measured on a 3:1 RECURRENT HYBRID')
print('  whose 52.7 MB fixed recurrent state is PER SEQUENCE, not per token.')
print('  15,000 warm cells x 52.7 MB fixed = %.1f TB of recurrent state alone.' % (15000 * fixed / 1e12))
print('  A fork that shares pages cannot share a recurrent state that must diverge.')
