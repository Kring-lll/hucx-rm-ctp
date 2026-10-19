"""Validate full successful MPI segment logs, retaining diagnostic RSS samples."""
import json
from collections import Counter

def validate_segments(root, resource):
    identities = {r["rank"]: r["process_exit"]["jetty"]["pid"] for r in resource["ranks"]}
    files = list(root.glob("segment_rank*.jsonl")) + list((root/"remote_logs").glob("segment_rank*.jsonl"))
    if len(files) != len(identities): raise ValueError("segment file count")
    seen = set(); result = []
    for path in files:
        rows = [json.loads(x) for x in path.read_text().splitlines()]
        if not rows: raise ValueError("empty segment log")
        rank = rows[0]["rank"]
        if rank not in identities or rank in seen: raise ValueError("segment rank")
        seen.add(rank)
        if (path.parent == root) != (rank == 0): raise ValueError("segment log placement")
        if [r["seq"] for r in rows] != list(range(1,len(rows)+1)): raise ValueError("segment sequence")
        registered = unregistered = live = total = live_bytes = peak = 0
        histogram = Counter(); samples = []
        for i, row in enumerate(rows):
            if row["pid"] != identities[rank] or row["rank"] != rank or row["valid"] != 1:
                raise ValueError("segment identity or tracker invalid")
            if row["register_failed"] or row["unregister_failed"] or row["rc"] != 0:
                raise ValueError("segment API failure")
            event, length = row["event"], row["length"]
            if type(length) is not int or length < 0: raise ValueError("segment length")
            if event == "register":
                registered += 1; live += 1; total += length; live_bytes += length
                peak = max(peak,live); histogram[length] += 1
            elif event == "unregister":
                unregistered += 1; live -= 1; live_bytes -= length
            elif event != "process_exit" or i != len(rows)-1:
                raise ValueError("unexpected segment event")
            if live < 0 or live_bytes < 0: raise ValueError("segment underflow")
            for key, expected in (("registered",registered),("unregistered",unregistered),
                ("live",live),("bytes_total",total),("bytes_live",live_bytes),("peak",peak)):
                if row[key] != expected: raise ValueError("segment balance " + key)
            if row["sampled"]:
                if row["rss_before_KiB"] < 0 or row["rss_after_KiB"] < 0:
                    raise ValueError("segment RSS sample missing")
                samples.append({k:row[k] for k in ("event","registered","unregistered","length",
                    "rss_before_KiB","rss_after_KiB")})
        if rows[-1]["event"] != "process_exit" or live or live_bytes or registered == 0:
            raise ValueError("incomplete/unbalanced segment end")
        result.append({"rank":rank,"registered":registered,"unregistered":unregistered,
            "requested_bytes_total":total,"peak_references":peak,
            "registered_length_histogram":dict(sorted(histogram.items())),"RSS_samples":samples})
    return {"result":"PASS","ranks":sorted(result,key=lambda r:r["rank"]),
            "scope":"API reference/request bytes and sampled process RSS; not unique pages or causal SDK cost"}
