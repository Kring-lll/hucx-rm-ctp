import json
import sys
from pathlib import Path
files = list(Path(sys.argv[1]).glob("segment_*.jsonl"))
assert len(files) == 1
rows = [json.loads(x) for x in files[0].read_text().splitlines()]
assert [r["seq"] for r in rows] == list(range(1,8))
assert all(r["valid"] == 1 for r in rows)
assert rows[0]["register_failed"] == 1 and rows[0]["live"] == 0
assert rows[3]["unregister_failed"] == 1 and rows[3]["live"] == 2
last = rows[-1]
assert last["event"] == "process_exit"
assert last["registered"] == last["unregistered"] == 2
assert last["bytes_total"] == 12288 and last["bytes_live"] == last["live"] == 0
assert last["register_failed"] == last["unregister_failed"] == 1
assert all(r["rss_before_KiB"] >= 0 and r["rss_after_KiB"] >= 0 for r in rows[:-1])
print("SEGMENT_ACCOUNTING_SELFTEST=PASS hardware_validation=NOT_RUN")
