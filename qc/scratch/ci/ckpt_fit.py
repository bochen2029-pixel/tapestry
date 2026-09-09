"""Least-squares fit of checkpoint bytes against token count over every fusord tape on disk.

bytes = fixed + per_token * toks. The fixed part is the recurrent state (plus headers); the slope
is the attention KV per token at q8_0. Prints every distinct (toks, bytes) point, the fit, and the
largest residual, so the linearity claim in the convergence document (section 7.3) is checked on
more than two points.
"""
import json
import os
import re
import sys

roots = [r"C:/fusor1/build", r"C:/fusor1/converge/smoke", r"C:/fusor1/sandbox", r"C:/fusor1/converge"]
pat = re.compile(r'\{"k":"(ckpt|checkpoint)"[^}]*\}')
points = {}
files = 0
for root in roots:
    for dp, dn, fn in os.walk(root):
        for f in fn:
            if not f.endswith(".jsonl"):
                continue
            p = os.path.join(dp, f)
            try:
                with open(p, "r", encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
            except OSError:
                continue
            hit = False
            for m in pat.finditer(text):
                try:
                    row = json.loads(m.group(0))
                except json.JSONDecodeError:
                    continue
                if not row.get("ok", True):
                    continue
                t, b = row.get("toks"), row.get("bytes")
                if isinstance(t, int) and isinstance(b, int) and b > 0:
                    points.setdefault((t, b), set()).add(p.replace("\\", "/"))
                    hit = True
            files += hit

pts = sorted(points)
print(f"tapes with checkpoint rows: {files}; distinct (toks, bytes) points: {len(pts)}")
for t, b in pts:
    print(f"  toks={t:5d}  bytes={b:12,d}  ({len(points[(t, b)])} tape(s))")

if len(pts) >= 2:
    n = len(pts)
    sx = sum(t for t, _ in pts)
    sy = sum(b for _, b in pts)
    sxx = sum(t * t for t, _ in pts)
    sxy = sum(t * b for t, b in pts)
    slope = (n * sxy - sx * sy) / (n * sxx - sx * sx)
    fixed = (sy - slope * sx) / n
    resid = max(abs(b - (fixed + slope * t)) for t, b in pts)
    print(f"\nfit: bytes = {fixed:,.0f} + {slope:,.2f} * toks")
    print(f"fixed part = {fixed / 1e6:,.2f} MB   per-token = {slope:,.0f} B   max |residual| = {resid:,.0f} B")
    lo, hi = pts[0], pts[-1]
    print(f"two-point check on the extremes ({lo[0]} and {hi[0]} toks): slope {(hi[1] - lo[1]) / (hi[0] - lo[0]):,.2f} B/token")
else:
    print("not enough points", file=sys.stderr)
