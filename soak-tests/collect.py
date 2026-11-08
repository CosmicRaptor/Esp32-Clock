#!/usr/bin/env python3
"""Collect existing cloud health reports for a bounded 24-hour soak test."""
import datetime as dt
import importlib.util
import json
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('release', ROOT / 'tools/firmware_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()

def main():
    folder = ROOT / 'soak-tests' / dt.datetime.now().strftime('%Y%m%d-%H%M%S')
    folder.mkdir()
    def write(name, value):
        (folder / name).write_text(json.dumps(value, indent=2) + '\n')
    request = release.connection()
    baseline = request('/api/management')
    original = baseline['settings']['interval']
    start = time.time()
    deadline = start + 86400
    write('run.json', {'started': utc(), 'deadline_unix': deadline, 'original_interval': original,
                       'interval': original, 'baseline': baseline})
    print('Collecting in ' + str(folder), flush=True)
    try:
        while time.time() < deadline:
            row = {'observed_at': utc()}
            try:
                row['management'] = request('/api/management')
            except (Exception, SystemExit) as error:
                row['error'] = str(error)
                try:
                    request = release.connection()
                except (Exception, SystemExit):
                    pass
            with (folder / 'samples.jsonl').open('a') as out:
                out.write(json.dumps(row) + '\n')
            time.sleep(min(60, max(0, deadline - time.time())))
    finally:
        write('finished.json', {'finished': utc(), 'cloud_settings_changed': False})
        print('Finished; cloud reporting unchanged', flush=True)

if __name__ == '__main__':
    main()
