"""Concatenate a TAPESTRY tape's segments in position order into one file.

The estate's verifier (C:\\fusor1\\convergence_tools\\verify_chain.py) reads ONE chain per file and
resets its head between files, so a per-segment run reports a "seam" at every roll and calls it legal.
Concatenating in order is the honest cross-check: one file, one chain, and any seam or break is real.

    python catsegs.py <tape_dir> <out_file>
"""
import os, re, sys

PAT = re.compile(r"^seg-(\d{20})\.jsonl$")

def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    d, out = sys.argv[1], sys.argv[2]
    segs = []
    for name in os.listdir(d):
        m = PAT.match(name)
        if m:
            segs.append((int(m.group(1)), name))
    segs.sort()
    n = 0
    with open(out, "wb") as w:
        for _, name in segs:
            with open(os.path.join(d, name), "rb") as f:
                b = f.read()
            w.write(b)
            n += b.count(b"\n")
    print(f"{out}: {len(segs)} segments, {n} rows")
    return 0

if __name__ == "__main__":
    sys.exit(main())
