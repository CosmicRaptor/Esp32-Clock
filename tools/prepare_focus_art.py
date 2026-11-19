#!/usr/bin/env python3
"""Resize the chosen focus illustration and regenerate its SD provisioning bytes."""
from pathlib import Path
from PIL import Image

root=Path(__file__).resolve().parents[1]
folder=root/'assets/focus-study'
image=Image.open(folder/'study-panda-source.png').convert('RGB').resize((320,240),Image.Resampling.LANCZOS)
path=folder/'sd/ui/focus-study-v1.jpg'
path.parent.mkdir(parents=True,exist_ok=True)
image.save(path,quality=94,subsampling=0,progressive=False)
data=path.read_bytes()
header='#pragma once\n#include <stdint.h>\n// Generated from assets/focus-study/sd/ui/focus-study-v1.jpg; do not hand-edit.\n'
header+='static const uint8_t FOCUS_STUDY_JPG[] = {\n'
header+='\n'.join('  '+','.join('0x%02x'%v for v in data[i:i+20])+',' for i in range(0,len(data),20))+'\n};\n'
(root/'firmware/BhoomiClock/focus_study_jpg.h').write_text(header)
print(path)
