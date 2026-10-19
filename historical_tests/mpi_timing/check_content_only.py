#!/usr/bin/env python3
import re
import sys
from pathlib import Path

log, mode, size, ranks, iters, rc = sys.argv[1:]
seen = set()
for line in Path(log).read_text(errors="replace").splitlines():
    if re.search(r"TEST_CONFIG|MPI_PATH_SETUP|PASS MPI|FAIL|WARN|ERROR|inter-node cfg|MPI_EXIT_CODE", line):
        print(line)
    match = re.search(r"\bPASS MPI mode=(\w+) rank=(\d+) ranks=(\d+) bytes=(\d+) iterations=(\d+)\b", line)
    if match and (match[1], match[3], match[4], match[5]) == (mode, ranks, size, iters):
        seen.add(int(match[2]))
print("PATH_CHECK=NOT_EVALUATED trace=n")
if int(rc):
    raise SystemExit(int(rc))
if seen != set(range(int(ranks))):
    print("CONTENT_CHECK=FAIL missing_rank")
    raise SystemExit(2)
print("CONTENT_CHECK=PASS")
