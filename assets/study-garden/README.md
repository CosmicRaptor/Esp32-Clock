# Study garden artwork

`concept.png` is the user-approved image-generation concept. `background-source.png` and `bamboo-source.png` are separately generated assets matching it; the latter has transparency. `tools/prepare_garden_art.py` scales and composes five 320x240 JPEG stages and regenerates `firmware/BhoomiClock/garden_jpg.h`. The live session count and Back button are drawn by LVGL, not baked into the artwork. `assets/focus-study/previews/garden.png` is a host render of the actual garden screen.

The generation prompts requested the existing focus screen's warm hand-painted panda style, a cozy lavender-peach garden with a panda reading, and a separate transparent bamboo stalk. The background deliberately omits text and bamboo so the interface and growth stages can be composed separately.
