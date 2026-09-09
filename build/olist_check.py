"""Falsifier 19 — the ledger agrees with the world.

    python olist_check.py <olist_data_dir> <dump.csv> [--sample N]

Blueprint §10: "The ledger agrees with the world: a sample of cells recomputed from the source tables
by an INDEPENDENT query matches. Lie: last-row-wins on a summed column."

Independence is the whole point, so this shares no code with the store: a different language, a
different CSV parser, a different decimal reader, and a straight recomputation from the source tables.
If both sides ran the same code the check would only prove the code is consistent with itself.

Exit 0 when every sampled cell agrees; 1 on any disagreement, with the first ones printed.
"""
import csv
import sys
from collections import defaultdict
from decimal import Decimal


def minor(s):
    """'58.90' -> 5890, exactly. Decimal, not float: this is money."""
    if s is None or s == "":
        return 0
    return int((Decimal(s) * 100).to_integral_value())


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    data, dump = sys.argv[1], sys.argv[2]
    sample = 0
    if "--sample" in sys.argv:
        sample = int(sys.argv[sys.argv.index("--sample") + 1])

    # The world, recomputed. Sum per order, because an order has many line items — which is the
    # whole reason this dataset is the right one for this falsifier.
    price = defaultdict(int)
    freight = defaultdict(int)
    rows = 0
    with open(f"{data}/olist_order_items_dataset.csv", newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            price[r["order_id"]] += minor(r["price"])
            freight[r["order_id"]] += minor(r["freight_value"])
            rows += 1

    est = {}
    status = {}
    with open(f"{data}/olist_orders_dataset.csv", newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            est[r["order_id"]] = r["order_estimated_delivery_date"]
            status[r["order_id"]] = r["order_status"]

    # What the store says.
    checked = 0
    bad = []
    with open(dump, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            oid = r["order_id"]
            want_amount = price.get(oid, 0)
            want_freight = freight.get(oid, 0)
            got_amount = int(r["amount_minor"])
            got_freight = int(r["freight_minor"])
            if got_amount != want_amount or got_freight != want_freight:
                bad.append((oid, want_amount, got_amount, want_freight, got_freight,
                            len([1 for _ in ()])))
            checked += 1
            if sample and checked >= sample:
                break

    print(f"order_items rows read independently: {rows}")
    print(f"cells checked: {checked}")
    print(f"disagreements: {len(bad)}")
    for b in bad[:10]:
        print(f"  {b[0]}  amount want {b[1]} got {b[2]}   freight want {b[3]} got {b[4]}")
    if bad:
        print("LEDGER DISAGREES WITH THE WORLD")
        return 1
    print("THE LEDGER AGREES WITH THE WORLD")
    return 0


if __name__ == "__main__":
    sys.exit(main())
