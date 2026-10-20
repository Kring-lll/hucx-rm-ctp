#!/usr/bin/env python3
"""Fixed-baseline OSU comparison. Default: plan only; --execute launches MPI."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile

HASHES = {
 "osu_latency": "8d84cf815f5a346bfd4893e352efe3b8fae302e5f1bbd7f3f99f0095e2755268",
 "osu_bw": "dcb09f427e1e397f395e712527a62825b99a913c5084366902373300d6340ba5",
 "osu_allreduce": "597d6a4150ed63312566e322391b9f41368a473f724a366183716944c529c2db",
 "osu_bcast": "fbcd96a2963505922bc642b28e779309239931d0a1104225fbce51d7e9e09a4b",
 "osu_gatherv": "8d5c8f8546cd2f406076b6f0a55d530db70b931c59b9ed91ed6338e6cba90d2c",
 "osu_alltoall": "4d085c67a6f9fc2e8065f816cf2b1de15f9bbbd3dccff472fea9df3caefff4aa",
}

def parse_output(text, minimum, maximum, validate=False):
    if "v7.5.2" not in text or "# Size" not in text:
        raise ValueError("missing OSU version/table header")
    rows = []
    for line in text.splitlines():
        if not re.match(r"^\s*\d+\s", line):
            continue
        fields = line.split()
        size, value = int(fields[0]), float(fields[1])
        if not math.isfinite(value) or value < 0:
            raise ValueError("invalid measurement")
        if validate and fields[-1] != "Pass":
            raise ValueError("validation did not pass")
        rows.append({"bytes": size, "value": value})
    expected = []
    size = minimum
    while size <= maximum:
        expected.append(size)
        size *= 2
    if [r["bytes"] for r in rows] != expected:
        raise ValueError("missing, duplicate or unexpected message-size rows")
    if re.search(r"UCT send timeout|Validation.*Fail|PML ucx cannot", text):
        raise ValueError("runtime failure in output")
    return rows

def environment(runtime, transport):
    env = {k:v for k,v in os.environ.items()
           if not k.startswith(("UCX_", "OMPI_MCA_"))
           and k not in ("LD_PRELOAD", "RM_RESOURCE_OBSERVER_DIR", "RM_SEG_OBSERVER_DIR")}
    mpi = runtime / "mpi_run_15c7293/install"
    ucx = runtime / "hucx_run_15c7293/install"
    env.update(LD_LIBRARY_PATH=str(mpi/"lib")+":"+str(ucx/"lib"),
               UCX_MODULE_DIR=str(ucx/"lib/ucx"), UCX_NET_DEVICES="udmac0d1e2:1",
               UCX_PROTO_ENABLE="n", UCX_LOG_LEVEL="warn")
    if transport == "rm":
        env.update(UCX_TLS="rm_urma,self", UCX_RM_URMA_RX_DEPTH="1024",
                   UCX_RM_URMA_TX_DEPTH="256", UCX_RM_URMA_PRIORITY="5",
                   UCX_RM_URMA_TIMEOUT="100ms", UCX_RM_URMA_RETRIES="8",
                   UCX_RM_URMA_TRACE="n", UCX_RM_URMA_DIAG="n",
                   UCX_RM_URMA_TEST_DROP_DATA="0", UCX_RM_URMA_TEST_DROP_ACK="0")
    else:
        env["UCX_TLS"]="rc_urma,um_urma:aux,self"
        for prefix in ("RC", "UM"):
            for key, value in (("RX_DEPTH","1024"),("TX_DEPTH","256"),("TX_JFC_LEN","256")):
                env["UCX_"+prefix+"_URMA_"+key]=value
    return env

def plan(args):
    runtime, install = Path(args.runtime), Path(args.install)
    mpi=runtime/"mpi_run_15c7293/install"
    if args.phase == "gate":
        tests, low, high = ["osu_latency"], 64, 64
    elif args.phase == "pt2pt":
        tests, low, high = ["osu_latency", "osu_bw"], 1, 8388608
    else:
        tests, low, high = ["osu_allreduce","osu_bcast","osu_gatherv","osu_alltoall"], 4, 1048576
    jobs=[]
    for repeat in range(1,args.repeats+1):
        for test in tests:
            for transport in (("rm","rc") if repeat%2 else ("rc","rm")):
                env=environment(runtime,transport)
                folder="pt2pt" if test in ("osu_latency","osu_bw") else "collective"
                binary=install/"libexec/osu-micro-benchmarks/mpi"/folder/test
                cmd=["timeout","-k","10s",str(args.timeout)+"s",str(mpi/"bin/mpirun"),
                     "--allow-run-as-root","--prefix",str(mpi),"-np",str(args.ppn*2),
                     "--host",f"9.88.43.89:{args.ppn},9.88.43.97:{args.ppn}",
                     "--map-by",f"ppr:{args.ppn}:node","--bind-to",args.binding,
                     "--report-bindings","--mca","pml","ucx",
                     "--mca","pml_ucx_tls","any","--mca","pml_ucx_devices","any",
                     "--mca","coll","^hcoll,ucc"]
                for key in sorted(env):
                    if key.startswith("UCX_") or key=="LD_LIBRARY_PATH":
                        cmd += ["-x",key]
                cmd += [str(binary),"-m",f"{low}:{high}","-x",str(args.warmup),"-i",str(args.iterations)]
                if test=="osu_bw": cmd += ["-W",str(args.window)]
                if folder=="collective": cmd += ["-f"]
                if args.validate: cmd += ["-c","-u","5"]
                jobs.append(dict(name=f"{test}_{transport}_r{repeat}",transport=transport,
                                 benchmark=test,binary=str(binary),command=cmd,minimum=low,maximum=high,
                                 unit="MB/s" if test=="osu_bw" else "us",
                                 environment={k:env[k] for k in env if k.startswith("UCX_") or k=="LD_LIBRARY_PATH"}))
    return jobs

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase",choices=("gate","pt2pt","collective"))
    p.add_argument("--stage",required=True); p.add_argument("--runtime",required=True)
    p.add_argument("--install",required=True)
    p.add_argument("--ppn",type=int,choices=(1,2,4),default=1)
    p.add_argument("--binding",choices=("core","none"),default="core")
    p.add_argument("--repeats",type=int,default=3)
    p.add_argument("--warmup",type=int,default=20)
    p.add_argument("--iterations",type=int,default=100)
    p.add_argument("--window",type=int,default=64)
    p.add_argument("--timeout",type=int,default=180)
    p.add_argument("--validate",action="store_true")
    p.add_argument("--execute",action="store_true")
    args=p.parse_args()
    if min(args.repeats,args.iterations,args.window,args.timeout)<1 or args.warmup<0:
        p.error("invalid iteration/window/timeout")
    if args.phase!="collective" and args.ppn!=1: p.error("point-to-point needs ppn=1")
    jobs=plan(args)
    if not args.execute:
        print(json.dumps(jobs,indent=2)); return
    if os.uname().nodename.split(".")[0]!="node89": p.error("execute only on node89")
    stage=Path(args.stage); assert stage.is_dir()
    out=Path(tempfile.mkdtemp(prefix="osu_"+args.phase+"_",dir=stage))
    print("CASE_DIR="+str(out),flush=True)
    (out/"plan.json").write_text(json.dumps(dict(options=vars(args),jobs=jobs),indent=2))
    for binary in sorted({j["binary"] for j in jobs}):
        expected=HASHES[Path(binary).name]
        if hashlib.sha256(Path(binary).read_bytes()).hexdigest()!=expected:
            raise RuntimeError("local binary hash mismatch")
        remote=subprocess.run(["ssh","root@9.88.43.97","sha256sum",binary],capture_output=True,text=True)
        (out/(Path(binary).name+"_remote_hash.txt")).write_text(remote.stdout+remote.stderr)
        if remote.returncode or remote.stdout.split()[0]!=expected:
            raise RuntimeError("remote binary hash mismatch")
    for host in ("local","remote"):
        command=["bash","-c","uptime; vmstat 1 3; ps -eo comm | sort | uniq -c"]
        if host=="remote": command=["ssh","root@9.88.43.97","uptime; vmstat 1 3; ps -eo comm | sort | uniq -c"]
        probe=subprocess.run(command,capture_output=True,text=True)
        (out/(host+"_preflight.txt")).write_text(probe.stdout+probe.stderr)
        print(probe.stdout,flush=True)
        if probe.returncode: raise RuntimeError("preflight failed")
    results=[]
    for job in jobs:
        print("RUN="+job["name"],flush=True)
        env=environment(Path(args.runtime),job["transport"])
        log=out/(job["name"]+".log")
        with log.open("w") as f:
            run=subprocess.run(job["command"],env=env,stdout=f,stderr=subprocess.STDOUT)
        text=log.read_text(errors="replace")
        print(text,flush=True)
        record=dict(name=job["name"],exit_code=run.returncode,unit=job["unit"],status="FAIL")
        try:
            if run.returncode: raise ValueError("nonzero exit")
            record["rows"]=parse_output(text,job["minimum"],job["maximum"],args.validate)
            record["status"]="PASS"
        except (ValueError,IndexError) as exc: record["error"]=str(exc)
        results.append(record)
        (out/"summary.json").write_text(json.dumps(results,indent=2))
        print("RUN_STATUS="+record["status"],flush=True)
        if record["status"]!="PASS": raise SystemExit(1)
    print("ALL_OSU_RUN_CHECKS=PASS\nCASE_DIR="+str(out),flush=True)

if __name__=="__main__": main()
