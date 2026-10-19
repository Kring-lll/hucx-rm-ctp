#!/usr/bin/env python3
"""Local durations only; launcher partition uses rank0/node89 exclusively."""
import math
import re
import sys
from pathlib import Path


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def main():
    log, rank_arg, iter_arg = sys.argv[1:]
    ranks, iterations = int(rank_arg), int(iter_arg)
    lines = Path(log).read_text(errors="replace").splitlines()
    summaries, rows = {}, {}
    start, end = None, None
    keys = ["init_ms", "setup_ms", "fill_ms", "prepare_ms", "communication_ms", "verify_ms",
            "allreduce_ms", "cleanup_ms", "finalize_ms", "other_ms", "app_ms"]
    try:
        for line in lines:
            if line.startswith("MPI_TIMING_SUMMARY "):
                row = fields(line)
                rank = int(row["rank"])
                if rank in summaries:
                    raise ValueError("duplicate_rank_summary")
                summaries[rank] = row
            elif line.startswith("MPI_TIMING_ITER "):
                row = fields(line)
                key = (int(row["rank"]), int(row["iteration"]))
                if key in rows:
                    raise ValueError("duplicate_iteration")
                rows[key] = row
            elif line.startswith("LAUNCHER_START "):
                start = fields(line)
            elif line.startswith("LAUNCHER_END "):
                end = fields(line)
        if set(summaries) != set(range(ranks)):
            raise ValueError("missing_rank_summary")
        if set(rows) != {(rank, i) for rank in range(ranks) for i in range(iterations)}:
            raise ValueError("missing_iteration")
        for rank, row in sorted(summaries.items()):
            values = [float(row[key]) for key in keys]
            if not all(math.isfinite(value) and value >= 0 for value in values):
                raise ValueError("invalid_duration")
            if abs(sum(values[:-1])-values[-1]) > 0.02:
                raise ValueError("stage_sum_mismatch")
            if int(row["finalize_end_ns"]) < int(row["entry_ns"]):
                raise ValueError("negative_app_duration")
            if abs((int(row["finalize_end_ns"])-int(row["entry_ns"]))/1e6-values[-1]) > 0.02:
                raise ValueError("app_duration_mismatch")
            print("TIMING_RANK " + " ".join(f"{key}={row[key]}" for key in ["rank", "node"]+keys))
            for stage in ["fill_ms", "prepare_ms", "communication_ms", "verify_ms", "allreduce_ms"]:
                measured = [float(rows[rank, i][stage]) for i in range(iterations)]
                if not all(math.isfinite(v) and v >= 0 for v in measured):
                    raise ValueError("invalid_iteration_duration")
                if abs(sum(measured)-float(row[stage])) > 0.02:
                    raise ValueError("iteration_sum_mismatch")
            first = rows[rank, 0]
            last = rows[rank, iterations-1]
            print(f"TIMING_ITER_EDGE rank={rank} first_communication_ms={first['communication_ms']} "
                  f"last_communication_ms={last['communication_ms']}")
        if not start or not end or start.get("node") != "node89" or summaries[0].get("node") != "node89":
            raise ValueError("rank0_launcher_clock_domain_unconfirmed")
        a, b = int(start["start_ns"]), int(summaries[0]["entry_ns"])
        c, d = int(summaries[0]["finalize_end_ns"]), int(end["end_ns"])
        if not a <= b <= c <= d:
            raise ValueError("invalid_local_launcher_timeline")
        reported_job = float(end["job_ms"])
        if not math.isfinite(reported_job) or reported_job < 0:
            raise ValueError("invalid_launcher_duration")
        if abs((d-a)/1e6-reported_job) > 0.02:
            raise ValueError("launcher_duration_mismatch")
        print(f"TIMING_JOB total_ms={(d-a)/1e6:.6f} launch_to_rank0_main_ms={(b-a)/1e6:.6f} "
              f"rank0_app_ms={(c-b)/1e6:.6f} rank0_finalize_to_job_end_ms={(d-c)/1e6:.6f}")
        print("TIMING_NOTE communication_ms_includes_MPI_processing_progress_and_peer_wait; clocks_never_subtracted_across_nodes")
        print("TIMING_CHECK=PASS")
        return 0
    except (KeyError, ValueError) as exc:
        print(f"TIMING_CHECK=FAIL reason={exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
