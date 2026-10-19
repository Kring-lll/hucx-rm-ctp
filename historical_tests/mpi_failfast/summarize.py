#!/usr/bin/env python3
"""Bounded summaries. A nonzero MPI status alone never passes a negative case."""
from collections import Counter
from pathlib import Path
import re
import sys


def evaluate(text, case, size, iterations, path, rc):
    reasons = []
    posts = Counter()
    opcodes = Counter()
    for line in text.splitlines():
        match = re.search(r'\[([^\]\s]+:\d+:\d+)\].*RM_CTP_POST id=(\d+) kind=(\d+) opcode=(\w+)', line)
        if match:
            process, op, kind, opcode = match.groups()
            posts[(process, op, kind)] += 1
            opcodes[opcode] += 1
    drop_data = len(re.findall(r'RM_CTP_TEST_DROP type=DATA\b', text))
    drop_ack = len(re.findall(r'RM_CTP_TEST_DROP type=ACK\b', text))
    duplicate = len(re.findall(r'RM_CTP_DUPLICATE\b', text))
    fail_marker = re.search(
        r'HMPI_RM_FAIL_FAST peer=\d+ status=Endpoint timeout action=abort_job callback_deferred=1 exit_code=86', text)
    peers = set(re.findall(r'HMPI_RM_FAIL_FAST_ARMED peer=(\d+)\b', text))
    passes = set()
    for match in re.finditer(r'PASS MPI mode=p2p rank=(\d+) ranks=2 bytes=(\d+) iterations=(\d+)', text):
        rank, count, count_iterations = map(int, match.groups())
        if count == size and count_iterations == iterations:
            passes.add(rank)
    if not {'0', '1'}.issubset(peers):
        reasons.append('callback_registration_not_observed_for_both_peer_ranks')
    if 'inter-node cfg' not in text or not re.search(r'inter-node cfg[^\n]*tag\(rm_urma/udmac0d1e2:1\)', text):
        reasons.append('RM_inter_node_path_not_observed')
    if rc in (124, 137):
        reasons.append('outer_timeout_or_force_kill')
    if rc >= 128:
        reasons.append('signal_or_unexpected_high_exit_status')
    if re.search(r'Caught signal|Segmentation fault|AddressSanitizer|runtime error:', text):
        reasons.append('crash_or_sanitizer_diagnostic')
    if 'no error callback is installed' in text:
        reasons.append('endpoint_without_error_callback')
    nonzero_cqe = bool(re.search(r'RM_CTP_CQE[^\n]*status=[1-9]\d*\b', text))
    if nonzero_cqe:
        reasons.append('unexpected_device_CQE_error')
    if case == 'exhaust':
        if rc == 0:
            reasons.append('MPI_unexpected_success')
        if not fail_marker:
            reasons.append('missing_deferred_endpoint_timeout_abort')
        if not re.search(r'UCT send timeout', text):
            reasons.append('missing_transport_retry_timeout')
        if drop_data == 0 or drop_ack:
            reasons.append('incorrect_injection_observation')
        exhausted = [n for (process, op, kind), n in posts.items() if kind == '1' and n == 9]
        if not exhausted:
            reasons.append('no_DATA_operation_with_initial_plus_8_retries')
        if any(n > 9 and kind == '1' for (_, _, kind), n in posts.items()):
            reasons.append('DATA_operation_exceeded_retry_limit')
        if passes:
            reasons.append('unexpected_application_PASS_in_negative_case')
    else:
        if rc != 0:
            reasons.append('MPI_nonzero_exit')
        if passes != {0, 1}:
            reasons.append('missing_distinct_rank_content_checks')
        if 'FAIL MPI' in text or 'FAIL DATA' in text or 'HMPI_RM_FAIL_FAST peer=' in text:
            reasons.append('unexpected_failure')
        if case == 'baseline' and (drop_data or drop_ack):
            reasons.append('unexpected_injection')
        if case == 'data1':
            if drop_data != 1 or drop_ack:
                reasons.append('expected_exactly_one_DATA_drop')
            if not any(n > 1 and kind == '1' for (_, _, kind), n in posts.items()):
                reasons.append('DATA_retransmission_not_observed')
        if case == 'ack1' and (drop_ack != 1 or drop_data):
            reasons.append('expected_exactly_one_ACK_drop')
        if size <= 4096 and (not opcodes['SEND'] or opcodes['READ'] or opcodes['WRITE']):
            reasons.append('small_message_SEND_path_not_confirmed')
        if size >= 1048576:
            expected = path.upper() if path in ('read', 'write') else None
            if expected and not opcodes[expected]:
                reasons.append('requested_RMA_opcode_not_observed')
            elif not expected and not (opcodes['READ'] or opcodes['WRITE']):
                reasons.append('large_message_RMA_not_observed')
            if expected == 'READ' and opcodes['WRITE'] or expected == 'WRITE' and opcodes['READ']:
                reasons.append('conflicting_RMA_path')
    return reasons, posts, opcodes, drop_data, drop_ack, duplicate


def cli():
    if len(sys.argv) != 7:
        raise SystemExit('usage: summarize.py LOG CASE BYTES ITERS PATH MPI_EXIT')
    filename, case, size, iterations, path, rc = sys.argv[1:]
    text = Path(filename).read_text(errors='replace')
    size, iterations, rc = int(size), int(iterations), int(rc)
    reasons, posts, opcodes, dd, da, duplicate = evaluate(text, case, size, iterations, path, rc)
    print('MPI_EXIT_CODE=' + str(rc))
    for line in text.splitlines():
        if any(word in line for word in ('FAULT_RANK_CONFIG', 'HMPI_RM_FAIL_FAST', 'PASS MPI',
                                         'UCT send timeout', 'no error callback is installed',
                                         'JOB_WALL_SECONDS_APPROX')):
            print(line)
    print(f'OBSERVED_DROP DATA={dd} ACK={da} DUPLICATE={duplicate}')
    print('POST_COUNTS ' + ' '.join(f'{op}={opcodes[op]}' for op in ('SEND', 'READ', 'WRITE')))
    for (process, op, kind), count in sorted(posts.items()):
        if count > 1:
            print(f'REPOST process={process} id={op} kind={kind} posts={count}')
    if case == 'ack1' and not any(n > 1 and k == '1' for (_, _, k), n in posts.items()):
        print('ACK_RETRANSMISSION=NOT_OBSERVED cumulative_ACK_may_cover_loss')
    print(('NEGATIVE_CHECK' if case == 'exhaust' else 'FUNCTION_CHECK') + '=' +
          ('FAIL' if reasons else 'PASS'))
    for reason in reasons:
        print('CHECK_REASON=' + reason)
    if case == 'exhaust':
        print('POLICY=abort_job; MPI_request_error_return_and_recovery_not_validated')
    raise SystemExit(1 if reasons else 0)


if __name__ == '__main__':
    cli()
