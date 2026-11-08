#!/usr/bin/env python3
"""Convert a short video to JPEG frames and WAV audio for the clock's SD card."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('video', type=Path)
p.add_argument('destination', type=Path, help='new mood directory on the SD card, e.g. /Volumes/SD/mindfulness/3')
a = p.parse_args()
if a.destination.exists():
    p.error('destination already exists; choose a new directory or move the old clip aside')
a.destination.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='.mood-video-', dir=a.destination.parent) as tmp:
    frames = Path(tmp) / 'frames'
    frames.mkdir()
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(a.video), '-t', '20', '-an',
                    '-vf', 'fps=5,scale=320:240:force_original_aspect_ratio=increase,crop=320:240',
                    '-pix_fmt', 'yuvj420p', '-q:v', '5', '-start_number', '0',
                    str(frames / '%04d.jpg')], check=True)
    probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0',
                            '-show_entries', 'stream=index', '-of', 'csv=p=0', str(a.video)],
                           check=True, capture_output=True, text=True)
    if probe.stdout.strip():
        subprocess.run(['ffmpeg', '-v', 'error', '-i', str(a.video), '-t', '20', '-vn',
                        '-ac', '1', '-ar', '22050', '-c:a', 'pcm_s16le', str(frames / 'audio.wav')], check=True)
    images = sorted(frames.glob('*.jpg'))
    if not images or any(f.stat().st_size > 100000 for f in images):
        raise SystemExit('No frames or a frame is over 100 KB; try a simpler or shorter clip.')
    frames.rename(a.destination)
print(f'Prepared {len(images)} frames at 5 fps, with audio when present in {a.destination}')
