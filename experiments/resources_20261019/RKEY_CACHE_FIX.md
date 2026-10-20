# UB rkey cache lifecycle fix candidate — node acceptance pending

## Evidence
Valid diagnostic directory osu_guard_forcedlibs_diag_wpmc6j40 loaded independent libucp on both nodes. Diagnostic module source5ea75df, moduleSHA5453d6feb84b3dabb457bf572f998b87ae47a22e4f1ee8b24df7641173f29b92. At 256→512 KiB transition node89 logged UB_RKEY_CACHE_MISMATCH cached_len262144 incoming_len524288 attrs8649/8649 refs0, followed by RM_RMA_REJECT remote_or_size read1 bytes524288 remote_len262144 offset0, EID/UASID matching and device_max268435456. MPI_ERR_INTERN exit17. This directly demonstrates stale cached descriptor causes this specific range rejection; not a packet-size hardware limit. No original range check should be removed.

Code path: rkey_release decrements to zero but returns NO_RESOURCE when import_tseg is NULL, retaining the hash entry. RM holds imported segments per operation, not in the shared rkey import_tseg, so last release can leave refs0. Hash key previously omitted descriptor length/attrs; subsequent unpack returns the stale descriptor rather than the newly packed length. Changes are in common UB MD, so RC/UM regressions also required.

## Candidate changes
1. Key includes full packed descriptor identity EID/UASID/VA/token_id/length/attrs, in hash/equality, unpack and release.
2. Last release removes/frees an entry even if it has no imported segment. Imported segments are unimported before removal; on unimport failure retain the reference/ownership for retry. Validate matching object and nonzero refcount before decrement.
3. On descriptor allocation failure roll back newly inserted hash bucket.
4. Keep prior diagnostic rejection logs while validating; diagnostics are not performance fixes. No wire-format change, no relaxation of RM memory bounds, no SEND/READ replay change, no receive-buffer pooling change.

## Local verification
Actual unpack/release/equality functions are extracted from edited source and compiled with mocked SDK/hash table. Seven cases cover changed length, shared references/no import final release, changed attrs, imported segment cleanup, failed unimport ownership/retry, allocation-failure bucket rollback, zero-ref underflow guard. Local test PASS. Original15c7293 source negative control fails length-alias assertion as expected. This is not real SDK compilation, real khash concurrency test or hardware acceptance. Builder also syntax-checked; node build and MPI pending.

## Deployment
build_rkey_cache_fix.py validates three original source hashes against15c7293, copies 21 MiB source to new unique rkey_cache_fix_build directory, applies full overlay, executes mocked lifecycle test using copied actual functions, configures with original flags/new prefix and builds full independent HUCX. Core MPI and accepted HUCX installs unchanged. Need transfer identical new libs to node97 and explicitly preload new UCX libraries for OSU applications only, because earlier LD_LIBRARY_PATH attempt still selected old libucp. Verify actual load paths.

## Required acceptance
First reproduce 256→512 KiB transition with validation and new libs; confirm absence of stale range rejection and valid data, then isolated512 KiB, full latency sweep1 B–8 MiB. Regress RC as MD is shared. Next bw and collective smoke/validation before performance repeats; independent resource/segment balance checks as appropriate. Do not call fixed until node output confirms. Retain failing and passing versions, commands, logs and hashes; publish structured summary only.
