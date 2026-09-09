"""Probe 2: the UTF-8 / control-byte claim, tested on real bytes, and the
FMA / contraction question for step_cell, and the o_step_batch vacuity."""
import json, io, os, hashlib

print('== A. a row containing a non-UTF-8 source byte, as jesc would emit it ==')
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bad_row.jsonl')
# jesc passes any byte >= 0x80 straight through (the `c < 0` branch, fusord.cpp:192).
raw = b'{"k":"fact","after":"caf\xe9","prev":"' + b'0'*64 + b'","h":"' + b'0'*64 + b'"}\n'
open(p, 'wb').write(raw)
try:
    with open(p, 'r', encoding='utf-8') as f:
        json.loads(f.readline())
    print('  strict utf-8 read: OK')
except Exception as e:
    print('  strict utf-8 read:', type(e).__name__, '-', e)
try:
    with open(p, 'rb') as f:
        json.loads(f.readline())          # json module decodes bytes as utf-8
    print('  json.loads(bytes) : OK')
except Exception as e:
    print('  json.loads(bytes) :', type(e).__name__, '-', e)
print('  -> a Python judge/fold subscribing to the tape cannot even read the row,')
print('     let alone disagree about its hash.')
os.remove(p)

print()
print('== B. two canonicalizations of one entry, both defensible, different h ==')
entry = {'k': 'fact', 'pos': 42, 'cell': 7, 'body': {'amount': 1e-5, 'note': 'a\u00e9b'}}
def canon_py(o):
    return json.dumps(o, sort_keys=True, separators=(',', ':'), ensure_ascii=False)
def canon_ascii(o):
    return json.dumps(o, sort_keys=True, separators=(',', ':'), ensure_ascii=True)
a, b = canon_py(entry), canon_ascii(entry)
print('  ensure_ascii=False:', a)
print('  ensure_ascii=True :', b)
for name, s in (('utf8 ', a), ('ascii', b)):
    print('  h(%s) = %s' % (name, hashlib.blake2b(s.encode('utf-8'), digest_size=32).hexdigest()[:32]))
print('  Both satisfy blueprint 2.1 ("keys sorted, no whitespace"). Neither is')
print('  named by it. RFC 8785 picks one (raw UTF-8, no \\u except for the')
print('  mandatory set); the blueprint does not say so, so two conforming')
print('  implementations fork the chain.')
print('  Also note 1e-05 above: RFC 8785 would write 1e-5.')

print()
print('== C. o_step_batch (osv_core_test.cpp:228-248) is vacuous ==')
src = open(r'C:/Websites/aorta-site/_upload/osv_core_test.cpp', encoding='utf-8').read()
start = src.index('static void o_step_batch')
blk = src[start:src.index('\n}', start)]
print('  the lambda signature :', [l.strip() for l in blk.splitlines() if 'auto run' in l][0])
print('  the two calls        :', [l.strip() for l in blk.splitlines() if 'const auto all' in l][0])
print('  the first lambda parameter is UNNAMED and UNUSED; run(0,0.0f) and')
print('  run(1,0.0f) execute the identical loop nest over the identical data.')
print('  `identical` is true by construction. The test named "one numerical path')
print('  whatever the size" never varies the size or the batching.')

print()
print('== D. is to_fix() safe under FMA contraction? ==')
print('  to_fix(v) = (int64_t)(v * 65536.0f + 0.5f) is a mul-add: nvcc contracts it')
print('  (-fmad=true is the DEFAULT), MSVC /fp:precise does not.')
print('  It survives ONLY because 65536 is a power of two, so v*65536.0f is exact')
print('  and fma(v,65536,0.5) == (v*65536)+0.5 to the bit.')
print('  Nothing in the source records that dependence: FIX_SCALE is a `double`')
print('  named 65536.0 with no static_assert that it is a power of two.')
print('  step_cell() is NOT protected the same way. Its multiply-adds are:')
for line in ['lap += L.k_cls  * (dev[j] - d0)',
             'lap += L.k_slot * (dev[j] - d0)',
             'r  = src + lap - L.decay * d0',
             'nd = d0 + L.omega * r / diag']:
    print('    ', line)
print('  k_cls=0.25, k_slot=0.25, decay=1.0, omega=1.0 are powers of two TODAY,')
print('  but they are mutable Lattice fields with no such constraint, and the')
print('  operands (dev[j]-d0) are arbitrary. fma(k, x, lap) != (k*x)+lap in')
print('  general. Host-vs-device bit-identity therefore depends on compiler')
print('  flags that no build file in the estate pins.')
