"""Red-team scratch: (1) print non-tokenizer GGUF metadata keys of the 9B judge so the
recurrent-state size can be corroborated from architecture; (2) pin the sizing arithmetic
used in the QC-6 report so every number in it is reproducible from receipts."""
import struct, sys

PATH = "C:/models/Qwen3.5-9B-emit-v11-Q5_K_M.gguf"

def rd(f, fmt):
    n = struct.calcsize(fmt)
    return struct.unpack("<" + fmt, f.read(n))

def rd_str(f):
    (n,) = rd(f, "Q")
    return f.read(n).decode("utf-8", "replace")

SCALAR = {0: "B", 1: "b", 2: "H", 3: "h", 4: "I", 5: "i", 6: "f", 7: "?", 10: "Q", 11: "q", 12: "d"}

def rd_val(f, t):
    if t in SCALAR:
        return rd(f, SCALAR[t])[0]
    if t == 8:
        return rd_str(f)
    if t == 9:
        (et,) = rd(f, "I")
        (cnt,) = rd(f, "Q")
        if et in SCALAR:
            sz = struct.calcsize(SCALAR[et])
            f.seek(cnt * sz, 1)
            return f"<array {cnt} x type{et}>"
        if et == 8:
            for _ in range(cnt):
                (n,) = rd(f, "Q"); f.seek(n, 1)
            return f"<array {cnt} x str>"
        vals = [rd_val(f, et) for _ in range(cnt)]
        return vals
    raise ValueError(t)

def gguf_keys(path):
    out = {}
    with open(path, "rb") as f:
        magic = f.read(4)
        assert magic == b"GGUF", magic
        (ver,) = rd(f, "I"); (nt,) = rd(f, "Q"); (nkv,) = rd(f, "Q")
        for _ in range(nkv):
            k = rd_str(f); (t,) = rd(f, "I"); v = rd_val(f, t)
            if not k.startswith("tokenizer."):
                out[k] = v
    return out

def main():
    try:
        keys = gguf_keys(PATH)
        print("== GGUF metadata (non-tokenizer) ==")
        for k in sorted(keys):
            print(f"{k} = {keys[k]}")
    except Exception as e:  # noqa
        print("GGUF read failed:", e)

    print()
    print("== Sizing arithmetic (receipts: convergence §7.3; converge smoke ckpt rows) ==")
    pairs = [(434, 60_257_248), (512, 61_616_944), (1005, 70_210_920), (1247, 74_429_464), (2343, 93_534_936)]
    (t0, b0), (t1, b1) = pairs[2], pairs[3]
    per_tok = (b1 - b0) / (t1 - t0)
    fixed = b0 - t0 * per_tok
    print(f"marginal bytes/token from ({t0},{b0})-({t1},{b1}): {per_tok:,.1f}")
    print(f"fixed bytes (recurrent state + header): {fixed:,.0f}  = {fixed/1e6:.1f} MB")
    for t, b in pairs:
        pred = fixed + t * per_tok
        print(f"  toks {t:5d}: on-disk {b:,}  predicted {pred:,.0f}  delta {b-pred:+,.0f}")

    card = 141e9; weights = 7e9; root_tokens = 8000; suffix_tokens = 500
    kv8 = 17_432; kv4 = kv8 / 2  # q8_0 receipt; 4-bit halves the attention KV only
    rec = 52_691_760
    root = root_tokens * kv8 + rec
    free = card - weights - root
    print(f"\nfree after weights+root: {free/1e9:.1f} GB (root {root/1e6:.0f} MB incl. one recurrent state)")
    for label, kv in (("q8_0 KV (the receipt's quant)", kv8), ("4-bit KV", kv4)):
        suffix = suffix_tokens * kv
        per_cell_bp = suffix                 # blueprint's per-cell cost: attention suffix only
        per_cell_hy = suffix + rec           # hybrid: plus one full recurrent state per sequence
        print(f"{label}: suffix {suffix/1e6:.1f} MB; blueprint warm cells {free/per_cell_bp:,.0f}; "
              f"hybrid warm cells {free/per_cell_hy:,.0f}  (ratio {per_cell_hy/per_cell_bp:.1f}x)")
    print(f"\n15,000 sequences x recurrent state = {15000*rec/1e9:,.0f} GB reserved at context init")
    print(f"45,000 sequences x recurrent state = {45000*rec/1e9:,.0f} GB")

    judgments_day = 50_000 * 5
    print(f"\njudgments/day {judgments_day:,}; at 110 ms serial = {judgments_day*0.110/3600:.1f} h/day")
    print(f"one year of replay at the same constant = {judgments_day*365*0.110/3600:,.0f} card-hours = {judgments_day*365*0.110/86400:,.0f} card-days")
    print(f"unified-KV cells if 15,000 warm x 500 + 8,000 root = {15000*500+8000:,} cells; receipt regime was ~434..2,343 cells")

if __name__ == "__main__":
    main()
