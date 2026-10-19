#!/usr/bin/env python3
"""Patch only a new source copy; reject an unfamiliar PML layout."""
import difflib
import hashlib
from pathlib import Path
import re
import shutil
import sys


def transform(text):
    if 'pml_ucx_rm_failfast.h' in text or 'hmpi_rm_ff_' in text:
        raise ValueError('source already patched')
    include = '#include "pml_ucx.h"'
    call = 'ucp_worker_progress(ompi_pml_ucx.ucp_worker)'
    if text.count(include) != 1 or text.count(call) != 3:
        raise ValueError('expected one PML include and exactly three worker progress calls')
    function = re.search(
        r'static\s+ucp_ep_h\s+mca_pml_ucx_add_proc_common\s*\([^)]*\)\s*\{', text)
    if not function:
        raise ValueError('expected add_proc_common function')
    create = re.search(
        r'(?m)^(?P<indent>[ \t]*)status = ucp_ep_create\(ompi_pml_ucx\.ucp_worker, &ep_params, &ep\);',
        text[function.end():])
    if not create or text.count('status = ucp_ep_create(ompi_pml_ucx.ucp_worker, &ep_params, &ep);') != 1:
        raise ValueError('expected unique endpoint creation')
    at = function.end() + create.start()
    before_create = text[function.end():at]
    required = ('UCP_EP_PARAM_FIELD_REMOTE_ADDRESS', 'UCP_EP_PARAM_FIELD_PEER_HOST_ADDR',
                'ep_params.peer_hostname', 'ep_params.vpid')
    if not all(item in before_create for item in required):
        raise ValueError('HMPI peer address setup differs from the reviewed node')
    if any(item in before_create for item in
           ('ERR_HANDLER', 'ERR_HANDLING_MODE', 'USER_DATA', 'err_handler', 'err_mode')):
        raise ValueError('existing error/user-data policy requires manual review')
    # All existing initializations, including PEER_HOST_ADDR, remain intact.
    injected = '''    /* HMPI_RM_FAILFAST_20261016_V1: opt-in job abort, not PEER recovery. */
    if (hmpi_rm_ff_enabled()) {
        ep_params.field_mask |= UCP_EP_PARAM_FIELD_ERR_HANDLER;
        ep_params.err_handler.cb = hmpi_rm_ff_error_cb;
        ep_params.err_handler.arg = (void *)(uintptr_t)proc->super.proc_name.vpid;
        fprintf(stderr, "HMPI_RM_FAIL_FAST_ARMED peer=%u policy=abort_job callback=deferred\\n",
                (unsigned)proc->super.proc_name.vpid);
    }

'''
    text = text[:at] + injected + text[at:]
    text = text.replace(call, 'hmpi_rm_ff_worker_progress()')
    text = text.replace(include, include + '\n#include "pml_ucx_rm_failfast.h"', 1)
    return text


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: patch_source.py NEW_SOURCE_DIRECTORY')
    source = Path(sys.argv[1]).resolve()
    target = source / 'ompi/mca/pml/ucx/pml_ucx.c'
    header = target.with_name('pml_ucx_rm_failfast.h')
    if header.exists():
        raise SystemExit('STOP: destination header already exists')
    old = target.read_text()
    new = transform(old)
    patch = ''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                    fromfile='a/ompi/mca/pml/ucx/pml_ucx.c',
                    tofile='b/ompi/mca/pml/ucx/pml_ucx.c'))
    patch += ''.join(difflib.unified_diff([], Path(__file__).with_name(
        'pml_ucx_rm_failfast.h').read_text().splitlines(True), fromfile='/dev/null',
        tofile='b/ompi/mca/pml/ucx/pml_ucx_rm_failfast.h'))
    target.write_text(new)
    shutil.copyfile(Path(__file__).with_name('pml_ucx_rm_failfast.h'), header)
    (source.parent / 'rm_failfast.patch').write_text(patch)
    print('PATCH_OK progress_calls=3 peer_error_mode=unchanged default=off')
    print('PML_SOURCE_SHA256=' + hashlib.sha256(new.encode()).hexdigest())


if __name__ == '__main__':
    main()
