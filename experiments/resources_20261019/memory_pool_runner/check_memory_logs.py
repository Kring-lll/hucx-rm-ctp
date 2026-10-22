#!/usr/bin/env python3
"""Memory snapshots augment, never replace, content and resource checks."""
import json
PHASES = ("before_init", "after_init", "after_exchange", "after_finalize")
METRICS = ("rss", "pss", "private_sum")

def validate_memory(root, resource_result):
    expected = {r["rank"]: r["process_exit"]["jetty"]["pid"] for r in resource_result["ranks"]}
    groups = {rank: [] for rank in expected}
    for line in (root / "mpi.log").read_text(errors="replace").splitlines():
        if not line.startswith("STAR_MEMORY "): continue
        row = json.loads(line[len("STAR_MEMORY "):])
        rank = row["rank"]
        if rank not in expected or row["pid"] != expected[rank] or row["unit"] != "KiB":
            raise ValueError("memory identity or unit mismatch")
        for key in METRICS + ("private_clean", "private_dirty", "vm_size", "time_ns"):
            if type(row[key]) is not int or row[key] < 0:
                raise ValueError("memory invalid metric " + key)
        if row["private_sum"] != row["private_clean"] + row["private_dirty"]:
            raise ValueError("private memory sum mismatch")
        groups[rank].append(row)
    result = []
    for rank, rows in sorted(groups.items()):
        if tuple(row["phase"] for row in rows) != PHASES:
            raise ValueError("missing/duplicate/out-of-order memory phase")
        if [r["time_ns"] for r in rows] != sorted(r["time_ns"] for r in rows):
            raise ValueError("nonmonotonic memory samples")
        phases = {r["phase"]: r for r in rows}
        delta = {}
        for label, before, after in (("init_minus_preinit", "before_init", "after_init"),
                                      ("exchange_minus_init", "after_init", "after_exchange")):
            delta[label] = {k: phases[after][k] - phases[before][k] for k in METRICS}
        result.append({"rank": rank, "phases": phases, "delta_KiB": delta})
    return {"result": "PASS", "unit": "KiB", "ranks": result,
            "scope": "procfs process resident/proportional/private memory; not total driver/device memory",
            "sampling": "rank-local samples, not simultaneous; observer and procfs overhead included"}
