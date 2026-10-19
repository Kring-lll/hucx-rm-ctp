#!/usr/bin/env python3
"""Compare the candidate against the node snapshot without extracting archives.

Standard library only; no network, subprocess, symlink creation, source execution
or writes to either input. A new output directory is required.
Exit 0: exact tree match. Exit 2: comparison completed, differences need review.
Exit 1: integrity, format, or invocation error.
"""
import argparse
import hashlib
import json
import re
import sys
import tarfile
from pathlib import Path, PurePosixPath

SNAPSHOT_SHA = '1d62cb1dfb5d8575a6bd4bb67af49510c96067e235018699f8aa331cd7787492'
ROOTS = {
    'hucx': 'hucx_rm_ctp_v1_20261008/source/',
    'hmpi': 'hmpi_rm_verify_20261016/source_Cqh0sQ/hmpi-main/',
    'hmpi_failfast': 'hmpi_rm_verify_20261016/mpi_failfast_run_vpeR2G/source/',
}

def digest_file(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def canonical(name):
    while name.startswith('./'):
        name = name[2:]
    if not name or name.startswith('/') or '\\' in name or '..' in PurePosixPath(name).parts:
        raise ValueError('Non-canonical archive member: ' + repr(name))
    return name.rstrip('/')

def scan(path, capture):
    entries, text, hardlinks = {}, {}, {}
    with tarfile.open(path, 'r|gz') as t:
        for m in t:
            name = canonical(m.name)
            if m.isdir():
                continue
            if name in entries:
                raise ValueError('Duplicate archive member: ' + name)
            item = {'mode': m.mode & 0o777}
            if m.issym():
                item.update(type='symlink', target=m.linkname)
            elif m.islnk():
                item.update(type='hardlink', target=canonical(m.linkname))
                hardlinks[name] = item['target']
            elif m.isfile():
                h, size, captured = hashlib.sha256(), 0, []
                f = t.extractfile(m)
                if f is None:
                    raise ValueError('Cannot read file: ' + name)
                with f:
                    for block in iter(lambda: f.read(1024 * 1024), b''):
                        size += len(block)
                        h.update(block)
                        if name in capture:
                            captured.append(block)
                if size != m.size:
                    raise ValueError('Truncated member: ' + name)
                item.update(type='file', sha256=h.hexdigest(), size=size)
                if name in capture:
                    text[name] = b''.join(captured).decode('utf-8')
            else:
                raise ValueError('Unsupported member type: ' + name)
            entries[name] = item
    for name in hardlinks:
        target, seen = name, set()
        while target in hardlinks:
            if target in seen:
                raise ValueError('Hardlink cycle: ' + name)
            seen.add(target)
            target = hardlinks[target]
        base = entries.get(target)
        if not base or base['type'] != 'file':
            raise ValueError('Unresolved hardlink: ' + name)
        entries[name] = dict(base, mode=entries[name]['mode'], hardlink_target=hardlinks[name])
    return entries, text

def unescape_gnu(name):
    return re.sub(r'\\([\\nr])', lambda m: {'\\': '\\', 'n': '\n', 'r': '\r'}[m[1]], name)

def check_node_manifest(text, entries):
    errors, checked, seen = [], 0, set()
    for line in text.splitlines():
        match = re.fullmatch(r'(\\?)([0-9a-f]{64}) [ *](.*)', line)
        if not match:
            raise ValueError('Invalid SOURCE_SHA256SUMS line')
        escaped, want, name = match.groups()
        name = canonical(unescape_gnu(name) if escaped else name)
        if name in seen:
            raise ValueError('Duplicate manifest path: ' + name)
        seen.add(name)
        item = entries.get(name)
        if not item or item.get('sha256') != want:
            errors.append(name)
        checked += 1
    if errors:
        raise ValueError('Node manifest mismatches: ' + repr(errors[:10]))
    return checked

def same_content(a, b):
    if a['type'] != b['type']:
        return False
    if a['type'] == 'file':
        return a['sha256'] == b['sha256'] and a['size'] == b['size']
    return a['target'] == b['target']

def subtree(entries, prefix):
    return {name[len(prefix):]: item for name, item in entries.items() if name.startswith(prefix)}

def compare(expected, actual):
    different, missing, modes, matched = [], [], [], 0
    for path, want in sorted(expected.items()):
        got = actual.get(path)
        if got is None:
            missing.append(path)
        elif not same_content(want, got):
            different.append({'path': path, 'candidate': want, 'node': got})
        else:
            matched += 1
            if want.get('mode') != got.get('mode'):
                modes.append({'path': path, 'candidate': want.get('mode'), 'node': got.get('mode')})
    extra = sorted(set(actual) - set(expected))
    return {'matched': matched, 'different': different, 'missing_on_node': missing,
            'node_only': extra, 'mode_differences': modes,
            'exact_match': not (different or missing or extra or modes)}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--snapshot', required=True, type=Path)
    p.add_argument('--candidate', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--candidate-sha256', help='Optional externally supplied candidate digest')
    args = p.parse_args()
    if args.out.exists():
        raise ValueError('Output directory exists; choose a new name')
    snapshot_sha = digest_file(args.snapshot)
    candidate_sha = digest_file(args.candidate)
    if snapshot_sha != SNAPSHOT_SHA:
        raise ValueError('Snapshot archive SHA256 mismatch')
    if args.candidate_sha256 and candidate_sha != args.candidate_sha256:
        raise ValueError('Candidate archive SHA256 mismatch')
    # Discover the candidate root without extracting any members.
    with tarfile.open(args.candidate, 'r:gz') as t:
        roots = {canonical(m.name).split('/')[0] for m in t.getmembers()}
    if len(roots) != 1:
        raise ValueError('Candidate must have one root directory')
    root = roots.pop() + '/'
    cand, cand_text = scan(args.candidate, {root+'versions.lock.json', root+'SOURCE_MAP.json'})
    node, node_text = scan(args.snapshot, {'SOURCE_SHA256SUMS', 'SYMLINKS.txt'})
    count = check_node_manifest(node_text['SOURCE_SHA256SUMS'], node)
    expected_links = {}
    for line in node_text['SYMLINKS.txt'].splitlines():
        name, sep, target = line.partition(' -> ')
        if not sep:
            raise ValueError('Invalid SYMLINKS.txt line')
        expected_links[canonical(name)] = target
    actual_links = {name: item['target'] for name, item in node.items()
                    if item['type']=='symlink' and not name.startswith('sdk_headers/')}
    if actual_links != expected_links:
        raise ValueError('Snapshot symbolic-link metadata does not match its manifest')
    maps = json.loads(cand_text[root+'SOURCE_MAP.json'])
    expected_views = {
        'hucx': subtree(cand, root+'hucx/'),
        'hmpi': subtree(cand, root+'hmpi/'),
    }
    overlay = subtree(cand, root+'variants/hmpi_failfast/')
    for label, view in [('hucx', expected_views['hucx']), ('hmpi', expected_views['hmpi']),
                        ('hmpi_failfast_overlay', overlay)]:
        # SOURCE_MAP describes type/content, not permissions; compare those now.
        declared = maps[label]
        if set(declared) != set(view) or any(not same_content(declared[n], view[n]) for n in declared):
            raise ValueError('Candidate SOURCE_MAP mismatch: ' + label)
    expected_views['hmpi_failfast'] = dict(expected_views['hmpi'], **overlay)
    report = {'snapshot_sha256': snapshot_sha, 'candidate_sha256': candidate_sha,
              'node_manifest_checked': count, 'node_symlinks_checked': len(actual_links),
              'versions': json.loads(cand_text[root+'versions.lock.json']), 'comparisons': {}}
    lines = ['ARCHIVE_INTEGRITY=PASS', 'SOURCE_SHA256SUMS_CHECKED='+str(count),
             'SYMLINKS_CHECKED='+str(len(actual_links)), 'EXTRACTION=NONE']
    for label, prefix in ROOTS.items():
        result = compare(expected_views[label], subtree(node, prefix))
        report['comparisons'][label] = result
        lines.append('{} matched={} different={} missing_on_node={} node_only={} mode_differences={}'.format(
            label, result['matched'], len(result['different']), len(result['missing_on_node']),
            len(result['node_only']), len(result['mode_differences'])))
        for field in ('different', 'missing_on_node', 'node_only', 'mode_differences'):
            for item in result[field][:12]:
                path = item['path'] if isinstance(item, dict) else item
                lines.append('  {}: {}'.format(field, path))
    exact = all(x['exact_match'] for x in report['comparisons'].values())
    report['status'] = 'EXACT_SOURCE_MATCH' if exact else 'REVIEW_REQUIRED'
    lines.append('RESULT='+report['status'])
    lines.append('NOTE=Generated-file and permission differences require review; no automatic exclusions.')
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out/'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (args.out/'summary.txt').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print('\n'.join(lines))
    print('REPORT='+str(args.out/'comparison.json'))
    return 0 if exact else 2

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, tarfile.TarError) as exc:
        print('COMPARISON_ERROR='+str(exc), file=sys.stderr)
        sys.exit(1)
