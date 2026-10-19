#!/usr/bin/env python3
"""Memory variant: same star topology, depth-aligned queues and resource checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys
import tempfile

OBSERVER_SHA = "0b129d8a0e8bccd5b3690a03d0e9fb77737d7d9f7f378c7b095e8c662e7411dd"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("transport", choices=("rm", "rc"))
    parser.add_argument("peers", type=int, choices=(1, 2, 4, 8))
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--center", default="9.88.43.89")
    parser.add_argument("--remote", default="9.88.43.97")
    parser.add_argument("--center-name", default="node89")
    parser.add_argument("--remote-name", default="node97")
    parser.add_argument("--profile", choices=("baseline_20261019", "depth1024_priority5"),
                        default="depth1024_priority5")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--binary-sha256", required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    for host in (args.center, args.remote, args.center_name, args.remote_name):
        if not host or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-" for c in host):
            parser.error("invalid host")
    if not args.stage.is_absolute() or not args.runtime.is_absolute():
        parser.error("absolute paths required")
    if os.environ.get("LD_PRELOAD"):
        parser.error("remove inherited LD_PRELOAD from launcher environment")
    if not args.build_dir.is_absolute(): parser.error("absolute build directory required")
    if len(args.binary_sha256) != 64 or any(c not in "0123456789abcdef" for c in args.binary_sha256):
        parser.error("invalid expected binary SHA256")
    binary_sha = args.binary_sha256
    mpi = args.runtime / "mpi_run_15c7293/install"
    hucx = args.runtime / "hucx_run_15c7293/install"
    binary = args.build_dir / "mpi_memory_star"
    local_lib = args.stage / "observer_build_d886fc3/liburma_resource_observer.so"
    remote_lib = args.stage / "observer_runtime_d886fc3/liburma_resource_observer.so"
    config = {"transport": args.transport, "peers": args.peers, "profile": args.profile,
              "rank_hosts": [args.center_name, args.remote_name], "stage": str(args.stage),
              "runtime": str(args.runtime), "observer_sha256": OBSERVER_SHA, "binary_sha256": binary_sha,
              "queue_alignment": "NOT_APPLIED", "mpi_exit_code": None}
    ucx = {"UCX_MODULE_DIR": str(hucx / "lib/ucx"),
           "UCX_TLS": "rm_urma,self" if args.transport == "rm" else "rc_urma,um_urma:aux,self",
           "UCX_NET_DEVICES": "udmac0d1e2:1", "UCX_PROTO_ENABLE": "n",
           "UCX_RNDV_THRESH": "inf", "UCX_LOG_LEVEL": "info"}
    if args.transport == "rm":
        ucx.update(UCX_RM_URMA_RX_DEPTH="1024", UCX_RM_URMA_PRIORITY="1",
                   UCX_RM_URMA_TIMEOUT="100ms", UCX_RM_URMA_RETRIES="8",
                   UCX_RM_URMA_TEST_DROP_DATA="0", UCX_RM_URMA_TEST_DROP_ACK="0",
                   UCX_RM_URMA_DIAG="n", UCX_RM_URMA_TRACE="n")
    if args.profile == "depth1024_priority5":
        if args.transport == "rm":
            ucx.update(UCX_RM_URMA_TX_DEPTH="256", UCX_RM_URMA_RX_DEPTH="1024",
                       UCX_RM_URMA_PRIORITY="5")
        else:
            for prefix in ("UCX_RC_URMA_", "UCX_UM_URMA_"):
                ucx[prefix + "TX_DEPTH"] = "256"
                ucx[prefix + "RX_DEPTH"] = "1024"
                ucx[prefix + "TX_JFC_LEN"] = "256"
        config["queue_alignment"] = "REQUESTED_REQUIRES_EVENT_CHECK"
        config["priority_evidence"] = "RM requested 5; RC/UM source constant 5; not logged by observer"
    config["ucx_env"] = ucx
    if args.dry_run:
        print(json.dumps(config, indent=2)); return 0
    if socket.gethostname().split(".")[0] != args.center_name:
        parser.error("run on center node")
    if sha(binary) != binary_sha or sha(local_lib) != OBSERVER_SHA:
        parser.error("local binary hash mismatch")
    remote_check = "set -eu; " + "; ".join(
        "test \"$(sha256sum " + shlex.quote(str(path)) + " | cut -d ' ' -f1)\" = " + expected
        for path, expected in ((binary, binary_sha), (remote_lib, OBSERVER_SHA)))
    subprocess.run(["ssh", "root@" + args.remote, remote_check], check=True)
    subprocess.run(["sha256sum", "-c", str(args.runtime / "runtime_15c7293_BINARY_SHA256SUMS")], check=True)
    case = Path(tempfile.mkdtemp(prefix="memory_star_%s_%s_peers%d_" % (args.profile, args.transport, args.peers), dir=args.stage))
    subprocess.run(["ssh", "root@" + args.remote,
                    "test ! -e " + shlex.quote(str(case)) + " && mkdir " + shlex.quote(str(case))], check=True)
    env = os.environ.copy()
    # Discard inherited UCX configuration so runs depend on the recorded profile.
    for key in list(env):
        if key.startswith("UCX_"):
            del env[key]
    env["OPAL_PREFIX"] = str(mpi)
    env["PATH"] = str(mpi / "bin") + ":" + str(hucx / "bin") + ":" + env.get("PATH", "")
    env["LD_LIBRARY_PATH"] = str(mpi / "lib") + ":" + str(hucx / "lib")
    cmd = ["timeout", "-k", "10s", "90s", str(mpi / "bin/mpirun"), "--allow-run-as-root",
           "--prefix", str(mpi), "--host", "%s:1,%s:%d" % (args.center, args.remote, args.peers),
           "-np", str(args.peers + 1), "--map-by", "slot", "--bind-to", "none",
           "--mca", "pml", "ucx", "--mca", "pml_ucx_tls", "any", "--mca", "pml_ucx_devices", "any"]
    for key in ("PATH", "LD_LIBRARY_PATH", "OPAL_PREFIX"):
        cmd += ["-x", key]
    for key, value in ucx.items():
        cmd += ["-x", key + "=" + value]
    rank_script = ('case "$(hostname -s)" in "$1") lib=$3;; "$2") lib=$4;; *) exit 2;; esac\n'
                   'export LD_PRELOAD="$lib" RM_RESOURCE_OBSERVER_DIR="$5"\nexec "$6"')
    cmd += ["/bin/bash", "-c", rank_script, "observed-star", args.center_name, args.remote_name,
            str(local_lib), str(remote_lib), str(case), str(binary)]
    config["command"] = cmd
    (case / "run.json").write_text(json.dumps(config, indent=2) + "\n")
    print("CASE_DIR=" + str(case), flush=True)
    with (case / "mpi.log").open("w") as log:
        config["mpi_exit_code"] = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
    (case / "run.json").write_text(json.dumps(config, indent=2) + "\n")
    (case / "remote_logs").mkdir()
    remote_source = "root@%s:%s/." % (args.remote, shlex.quote(str(case)))
    subprocess.run(["scp", "-r", remote_source, str(case / "remote_logs")], check=True)
    print("MPI_EXIT_CODE=" + str(config["mpi_exit_code"]))
    if config["mpi_exit_code"] != 0:
        print((case / "mpi.log").read_text(errors="replace")[-12000:]); return 2
    from check_star_logs import validate
    result = validate(case, args.peers)
    from check_memory_logs import validate_memory
    result["memory"] = validate_memory(case, result)
    (case / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    center = result["ranks"][0]["after_exchange"]
    print("CENTER_AFTER_EXCHANGE=" + json.dumps({k: v["live"] for k, v in center.items()}))
    if args.profile == "depth1024_priority5":
        print("DEPTH_CONFIG_CHECK=PASS priority_hardware_observation=NOT_RUN")
    print("CENTER_MEMORY_DELTA_KiB=" + json.dumps(result["memory"]["ranks"][0]["delta_KiB"]))
    print("MEMORY_SAMPLES_CHECK=PASS")
    print("STAR_RUN_AND_LOG_CHECK=PASS")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("STAR_RUN=FAIL " + str(error), file=sys.stderr)
        sys.exit(2)
