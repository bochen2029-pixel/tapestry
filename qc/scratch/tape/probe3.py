"""Probe 3: (a) build synthetic tapes and run the estate's own verify_chain.py
against them, to see what it does and does not catch; (b) an actual
FMA-vs-mul-add divergence in step_cell's arithmetic shape."""
import hashlib, os, subprocess, sys, math

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = '0' * 64

def put(prev, body):
    h = hashlib.blake2b(prev.encode() + body.encode(), digest_size=32).hexdigest()
    return h, body + ',"prev":"%s","h":"%s"}' % (prev, h)

def write_tape(name, bodies, restart_at=None):
    p = os.path.join(HERE, name)
    prev = GEN
    with open(p, 'w', newline='\n', encoding='utf-8') as f:
        for i, b in enumerate(bodies):
            if restart_at is not None and i == restart_at:
                prev = GEN                     # a fresh segment file opened at size 0
            h, row = put(prev, b)
            f.write(row + '\n')
            prev = h
    return p

def run_verify(p):
    r = subprocess.run([sys.executable, 'C:/fusor1/convergence_tools/verify_chain.py', p],
                       capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()

print('== A. a clean tape ==')
p = write_tape('t_ok.jsonl', ['{"k":"fact","pos":%d' % i for i in range(5)])
rc, out = run_verify(p); print(out); print('  exit', rc)

print()
print('== B. the 3.2 segment roll: the chain restarts at genesis mid-tape ==')
print('   (fusord Tape::open recovers prev from the file it is opening; a NEW')
print('    64 MiB segment file is empty, so prev = GENESIS -- fusord.cpp:860,863-897)')
p = write_tape('t_seg.jsonl', ['{"k":"fact","pos":%d' % i for i in range(6)], restart_at=3)
rc, out = run_verify(p); print(out); print('  exit', rc)
print('  -> the fork is printed as a "seam", NOT counted as a break, and the tool')
print('     EXITS 0 / prints CHAIN OK. A segmented tape that forks at every roll')
print('     passes the estate\'s own chain verifier.')

print()
print('== C. a fact body carrying the literal text of the prev field ==')
evil = '{"k":"fact","after":"x,\\"prev\\":\\"' + 'a'*64 + '\\",\\"h\\":\\"' + 'b'*64 + '\\"}"'
p = write_tape('t_evil.jsonl', ['{"k":"fact","pos":0', evil, '{"k":"fact","pos":2'])
rc, out = run_verify(p); print(out); print('  exit', rc)
print('  -> verify_chain.py splits on rfind(\',"prev":"\'), which is inside the')
print('     escaped string. On fusord tapes the bodies are kernel-generated so this')
print('     never occurs; a TAPESTRY `fact` row carries the WORLD\'s bytes.')

print()
print('== D. an actual FMA vs mul-add divergence in step_cell\'s shape ==')
if hasattr(math, 'fma'):
    # lap = lap + k*(x - d0)   -- the exact shape of osv_core.cuh:194-197
    found = 0
    import random
    random.seed(3)
    for _ in range(200000):
        k   = random.uniform(0.05, 0.95)
        x   = random.uniform(-10, 10)
        d0  = random.uniform(-10, 10)
        lap = random.uniform(-10, 10)
        a = lap + k * (x - d0)          # MSVC /fp:precise : two roundings
        b = math.fma(k, (x - d0), lap)  # nvcc -fmad=true  : one rounding
        if a != b:
            found += 1
            if found == 1:
                print('  first divergence: k=%.17g x=%.17g d0=%.17g lap=%.17g' % (k, x, d0, lap))
                print('    mul-then-add = %.17g' % a)
                print('    fused        = %.17g' % b)
                print('    ulp gap      = %.3e' % abs(a - b))
    print('  divergent in %d of 200000 random draws (%.1f%%)' % (found, 100.0 * found / 200000))
    print('  -> the same C++ source, compiled by nvcc (contraction on by default)')
    print('     and by MSVC (contraction off by default), computes a DIFFERENT')
    print('     lattice. The header of osv_core.cuh claims the opposite:')
    print('     "the same translation unit as a property".')
else:
    print('  math.fma unavailable on this interpreter (%s)' % sys.version.split()[0])

print()
print('== E. what IS safe: red-black colouring, checked ==')
NCLS, NSLOT = 12, 16
bad = [(c, s) for c in range(NCLS) for s in range(NSLOT)
       for (nc, ns) in ((c-1, s), (c+1, s), (c, s-1), (c, s+1))
       if 0 <= nc < NCLS and 0 <= ns < NSLOT and ((c+s) & 1) == ((nc+ns) & 1)]
print('  same-colour neighbour pairs in the 5-point (cls,slot) stencil:', len(bad))
print('  -> a half-sweep of one colour reads only the other colour, so within one')
print('     binary the sweep IS order-independent. That part of the claim holds.')
