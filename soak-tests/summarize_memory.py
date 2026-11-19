#!/usr/bin/env python3
"""Summarize USB memstats captures without printing private serial messages."""
import argparse
import datetime as dt
import json
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('captures',nargs='+',type=Path)
args=parser.parse_args()
samples=[];gaps=[];errors=[];transitions=0
for path in args.captures:
    if path.is_dir():path=path/'serial.jsonl'
    ui={};previous=None;count=0
    for line in path.read_text().splitlines():
        try:record=json.loads(line)
        except ValueError:continue
        text=record.get('serial','')
        if '[uistats] ' in text:
            try:ui=json.loads(text.split('[uistats] ',1)[1])
            except ValueError:pass
        if '[ui] page ' in text:transitions+=1
        if any(s in text for s in ['assert failed:','Backtrace:','Rebooting...']):
            errors.append({'at':record['at'],'type':'crash/reboot marker','capture':str(path)})
        if '[memstats] ' not in text:continue
        try:memory=json.loads(text.split('[memstats] ',1)[1]);at=dt.datetime.fromisoformat(record['at'])
        except (ValueError,KeyError):continue
        if previous:
            gap=(at-previous).total_seconds()
            if gap>30:gaps.append({'after':previous.isoformat(),'seconds':round(gap,1),'capture':str(path)})
        previous=at;count+=1
        samples.append({'at':at,'memory':memory,'ui':dict(ui)})
    print(json.dumps({'capture':str(path),'samples':count}))
if not samples:raise SystemExit('No memory samples found')
fields=['heap','min_heap','largest_block','psram','min_psram','lv_free','lv_largest','lv_frag_pct']
def ranges(rows):
    return {key:{'min':min(r['memory'][key] for r in rows),'max':max(r['memory'][key] for r in rows)} for key in fields}
groups={}
for name in sorted({r['ui'].get('mode','unknown') for r in samples}):
    rows=[r for r in samples if r['ui'].get('mode','unknown')==name]
    groups[name]={'samples':len(rows),'ranges':ranges(rows),
        'artwork_bytes':sorted({r['ui'].get('focus_art_bytes',-1) for r in rows})}
summary={'samples':len(samples),'first':samples[0]['at'].isoformat(),'last':samples[-1]['at'].isoformat(),
    'first_uptime_s':samples[0]['memory']['uptime_s'],'last_uptime_s':samples[-1]['memory']['uptime_s'],
    'ranges':ranges(samples),'modes':groups,'page_transitions':transitions,'gaps_over_30s':gaps,
    'crash_markers':errors,'max_scroll_recoveries':max(r['ui'].get('recoveries',0) for r in samples)}
print(json.dumps(summary,indent=2))
