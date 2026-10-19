#!/usr/bin/env python3
"""Validate a collected star run without asserting a predicted resource count."""
import argparse
import json
import re
from pathlib import Path

KINDS = {"jfc", "jfr", "jfs", "jetty", "jfce", "target_jetty", "target_jfr", "binding"}
PHASES = ("after_init", "after_exchange", "after_finalize", "process_exit")

def require(ok, message):
    if not ok:
        raise ValueError(message)

def validate(root, peers):
    meta = json.loads((root / "run.json").read_text())
    require(meta["peers"] == peers and meta["mpi_exit_code"] == 0, "run status")
    passes = re.findall(r"^STAR_CONTENT_PASS rank=(\d+) ranks=(\d+) peers=(\d+)$",
                        (root / "mpi.log").read_text(errors="replace"), re.M)
    require(len(passes) == peers + 1, "content pass count")
    require({int(r) for r, _, _ in passes} == set(range(peers + 1)), "content ranks")
    require(all(int(n) == peers + 1 and int(p) == peers for _, n, p in passes), "content topology")
    setups = re.findall(r"^STAR_SETUP rank=(\d+) ranks=(\d+) node=(\S+) bytes=64 topology=star_sequential$",
                        (root / "mpi.log").read_text(errors="replace"), re.M)
    expected_hosts = meta["rank_hosts"]
    require(len(setups) == peers + 1, "setup count")
    require({int(r): h for r, n, h in setups} ==
            {i: expected_hosts[0 if i == 0 else 1] for i in range(peers + 1)}, "rank placement")
    files = list(root.glob("rank*.jsonl")) + list((root / "remote_logs").glob("rank*.jsonl"))
    require(len(files) == peers + 1, "observer file count")
    seen = set()
    summary = []
    for path in files:
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        require(rows and rows[0].get("event") == "start", str(path) + ": start")
        rank, pid = rows[0]["rank"], rows[0]["pid"]
        require(rank in range(peers + 1) and rank not in seen, "observer rank")
        require((path.parent == root) == (rank == 0), "observer location")
        seen.add(rank)
        require(all(r["rank"] == rank and r["pid"] == pid for r in rows), "identity")
        seq = [r["seq"] for r in rows if "seq" in r]
        require(seq == list(range(1, len(seq) + 1)), "sequence")
        require(all(r.get("valid", 1) == 1 for r in rows), "tracker invalid")
        depth_check = "NOT_REQUESTED"
        if meta.get("profile") == "depth1024_priority5":
            from collections import Counter
            events = [r for r in rows if r.get("event") == "acquire" and r.get("rc") == 0]
            for kind, depth in (("jetty", 256), ("jfr", 1024)):
                group = [r for r in events if r["kind"] == kind]
                require(group and all(r["depth"] == depth for r in group), "depth mismatch " + kind)
            expected_jfc = Counter({256: 1, 1024: 1}) if meta["transport"] == "rm" else Counter({256: 2, 1024: 2})
            require(Counter(r["depth"] for r in events if r["kind"] == "jfc") == expected_jfc,
                    "JFC depth or count mismatch")
            depth_check = "PASS"
        snapshots = {}
        for phase in PHASES:
            group = [r for r in rows if r["event"] == "snapshot" and r["phase"] == phase]
            require(len(group) == 8 and {r["kind"] for r in group} == KINDS, "snapshot set " + phase)
            for row in group:
                require(row["acquire_failed"] == row["release_failed"] == 0, "API failure")
                require(row["acquired"] - row["released"] == row["live"], "balance")
                require(0 <= row["unique_live"] <= row["live"] <= row["peak"], "bounds")
                if phase in ("after_finalize", "process_exit"):
                    require(row["live"] == row["unique_live"] == 0, "resource remains")
            snapshots[phase] = {r["kind"]: r for r in group}
        require(rows[-1].get("phase") == "process_exit", "incomplete end")
        positions = [next(i for i, r in enumerate(rows) if r.get("phase") == phase) for phase in PHASES]
        require(positions == sorted(positions), "phase order")
        summary.append({"rank": rank, "depth_check": depth_check, "after_init": snapshots["after_init"],
                        "after_exchange": snapshots["after_exchange"],
                        "process_exit": snapshots["process_exit"]})
    require(seen == set(range(peers + 1)), "missing rank")
    return {"result": "PASS", "peers": peers, "transport": meta["transport"],
            "scope": "API objects; not hardware TP or memory bytes",
            "ranks": sorted(summary, key=lambda r: r["rank"])}

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("case_dir", type=Path)
    parser.add_argument("--peers", type=int, required=True, choices=(1, 2, 4, 8))
    args = parser.parse_args()
    try:
        result = validate(args.case_dir, args.peers)
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(2, "STAR_LOG_CHECK=FAIL " + str(error) + "\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))
