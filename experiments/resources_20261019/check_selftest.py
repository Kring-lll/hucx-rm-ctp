#!/usr/bin/env python3
"""Check a closed self-test log; this never evaluates hardware resource savings."""
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
logs = list(root.glob('rank*_pid*.jsonl'))
if len(logs) != 1:
    raise SystemExit('SELFTEST_FAIL expected_one_log')
rows = [json.loads(line) for line in logs[0].read_text().splitlines()]
if not rows or rows[0].get('event') != 'start':
    raise SystemExit('SELFTEST_FAIL missing_start')
if len({r['pid'] for r in rows}) != 1 or any(r.get('valid', 1) != 1 for r in rows):
    raise SystemExit('SELFTEST_FAIL mixed_process_or_invalid_tracker')
kinds = {'jfc', 'jfr', 'jfs', 'jetty', 'jfce', 'target_jetty', 'target_jfr', 'binding'}
snapshots = {}
for row in rows:
    if row['event'] == 'snapshot':
        key = (row['phase'], row['kind'])
        if key in snapshots:
            raise SystemExit('SELFTEST_FAIL duplicate_snapshot')
        snapshots[key] = row
for phase in ('before', 'live', 'failed_delete', 'after', 'process_exit'):
    for kind in kinds:
        row = snapshots.get((phase, kind))
        if row is None:
            raise SystemExit('SELFTEST_FAIL missing_snapshot')
        expected = (2 if kind == 'jfc' else 1) if phase in ('live', 'failed_delete') else 0
        if row['live'] != expected or row['unique_live'] != expected:
            raise SystemExit('SELFTEST_FAIL live_or_unique_count')
        if phase in ('after', 'process_exit'):
            count = 2 if kind == 'jfc' else 1
            if (row['acquired'], row['released'], row['peak']) != (count, count, count):
                raise SystemExit('SELFTEST_FAIL lifecycle_or_peak')
            failures = 1 if kind == 'jfc' else 0
            if (row['acquire_failed'], row['release_failed']) != (failures, failures):
                raise SystemExit('SELFTEST_FAIL error_accounting')
print('OBSERVER_ACCOUNTING_SELFTEST=PASS kinds=8 backend=mock hardware_validation=NOT_RUN')
