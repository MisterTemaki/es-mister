#!/usr/bin/env python3
"""Read-only 1 Hz process CPU/fault sampler; stores output only after process exit."""
import argparse,json,os,pathlib,time
p=argparse.ArgumentParser();p.add_argument('pid',type=int);p.add_argument('output',type=pathlib.Path);args=p.parse_args()
path=pathlib.Path('/proc')/str(args.pid)/'stat';rows=[];identity=None
while True:
    try:fields=path.read_text().rsplit(')',1)[1].split()
    except (FileNotFoundError,ProcessLookupError):break
    if fields[0]=='Z':break
    if identity is None:identity=fields[19]
    if fields[19]!=identity:break  # PID reused: never sample another process.
    rows.append(dict(at_ns=time.monotonic_ns(),user_ticks=int(fields[11]),system_ticks=int(fields[12]),
        minor_faults=int(fields[7]),major_faults=int(fields[9]),rss_pages=int(fields[21]),threads=int(fields[17])))
    time.sleep(1)
args.output.write_text(json.dumps(dict(clock_ticks=os.sysconf('SC_CLK_TCK'),page_bytes=os.sysconf('SC_PAGE_SIZE'),samples=rows),indent=2))
