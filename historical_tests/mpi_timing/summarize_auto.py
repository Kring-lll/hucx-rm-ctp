#!/usr/bin/env python3
"""Summarize observations; never turn a missing RMA opcode into RMA success."""
import collections
import re
import sys
from pathlib import Path


def main():
    log, mode, byte_arg, rank_arg, iter_arg, exit_arg = sys.argv[1:]
    size, ranks, iterations, mpi_exit = map(int, (byte_arg, rank_arg, iter_arg, exit_arg))
    lines = Path(log).read_text(errors="replace").splitlines()
    interesting = re.compile(r"TEST_CONFIG|MPI_PATH_SETUP|PASS MPI|FAIL|WARN|ERROR|inter-node cfg|MPI_EXIT_CODE|JOB_WALL_SECONDS_APPROX")
    for line in [line for line in lines if interesting.search(line)][-65:]:
        print(line)
    posts = []
    for line in lines:
        match = re.search(r"RM_CTP_POST id=\d+ kind=\d+ opcode=(SEND|READ|WRITE) bytes=(\d+) attempt=\d+", line)
        if match:
            posts.append((match.group(1), int(match.group(2)), line))
    counts = collections.Counter(opcode for opcode, _, _ in posts)
    byte_counts = collections.Counter()
    for opcode, length, _ in posts:
        byte_counts[opcode] += length
    print("POST_COUNTS " + " ".join(f"{key}={counts[key]}" for key in ("SEND", "READ", "WRITE")))
    print(f"RMA_POST_BYTES READ={byte_counts['READ']} WRITE={byte_counts['WRITE']}")
    for _, _, line in [post for post in posts if post[0] != "SEND"][:6]:
        print(line)
    observed = set()
    for line in lines:
        match = re.search(r"\bPASS MPI mode=(\w+) rank=(\d+) ranks=(\d+) bytes=(\d+) iterations=(\d+)\b", line)
        if match and (match[1], int(match[3]), int(match[4]), int(match[5])) == (mode, ranks, size, iterations):
            observed.add(int(match[2]))
    bad_cqe = any(int(match[1]) != 0 for line in lines
                  for match in [re.search(r"RM_CTP_CQE id=\d+ status=(\d+)", line)] if match)
    cross_node = any("inter-node cfg" in line and "rm_urma/udmac0d1e2:1" in line for line in lines)
    active = [key for key in ("READ", "WRITE") if counts[key]]
    path = "+".join(active) if active else "SEND_ONLY" if counts["SEND"] else "NO_OPCODE"
    result_lines = [f"AUTO_OBSERVED_PATH={path}"]
    if mpi_exit:
        result_lines.append(f"AUTO_CHECK=FAIL reason=mpi_exit_{mpi_exit}")
        result = mpi_exit
    elif observed != set(range(ranks)) or bad_cqe or not cross_node:
        result_lines.append("AUTO_CHECK=FAIL reason=content_checks_cqe_or_rm_transport")
        result = 2
    elif size <= 4096:
        good = counts["SEND"] > 0 and not active
        result_lines.append("CONTENT_CHECK=PASS")
        result_lines.append("AUTO_CHECK=PASS small_message=SEND" if good else "AUTO_CHECK=UNEXPECTED small_message_path")
        result = 0 if good else 2
    elif size >= 1048576:
        result_lines.append("CONTENT_CHECK=PASS")
        result_lines.append(f"AUTO_CHECK=PASS large_message={path}" if active else
                            "AUTO_CHECK=NOT_CONFIRMED large_message=SEND_ONLY data_correct=yes")
        result = 0 if active else 2
    else:
        result_lines.extend(["CONTENT_CHECK=PASS", f"AUTO_CHECK=OBSERVATION_ONLY intermediate_message={path}"])
        result = 0
    for line in result_lines:
        print(line)
    with Path(log).open("a") as handle:
        handle.write("\n".join(result_lines) + "\n")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
