#!/usr/bin/env python3
import socket
import subprocess
import sys
import time


def main():
    log, *command = sys.argv[1:]
    if not command:
        raise SystemExit("missing launch command")
    with open(log, "a", buffering=1) as handle:
        begin = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        handle.write(f"LAUNCHER_START node={socket.gethostname().split('.')[0]} start_ns={begin}\n")
        try:
            rc = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT).returncode
            rc = rc if rc >= 0 else 128-rc
        except OSError as exc:
            handle.write(f"LAUNCHER_ERROR {exc}\n")
            rc = 127
        end = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        handle.write(f"LAUNCHER_END end_ns={end} job_ms={(end-begin)/1e6:.6f}\nMPI_EXIT_CODE={rc}\n")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
