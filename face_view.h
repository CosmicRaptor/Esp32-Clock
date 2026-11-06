#pragma once
#include <lvgl.h>
#include <time.h>

#include "portal_client.h"

// Draws one clock face from the portal (portal/README.md, "Faces"): the background, sprites with their motions,
// particle emitters, and the time and date text. ui.cpp decides which face to show and when; this only draws it.
// Everything runs under app::Lock, like the rest of the UI.
namespace faceview {

// Makes the (hidden) container, full screen, on `parent`. The container is also where ui.cpp parks the
// now-playing strip while a face is up, so the strip is never part of what show() replaces.
void begin(lv_obj_t *parent);
lv_obj_t *root();

// Builds `f`. Everything the face needs is copied out of the portal client's memory (the downloaded sprites stay
// there), so the face keeps working while the client replaces its cache. Returns false, and leaves what is showing
// as it was, if the face can't be built (a file missing or broken, out of memory).
bool show(const portal::Face &f);
void clear();
bool active();

// Advance the animation and the time and date text. Call ~15 times a second while the face is on screen.
void tick(const struct tm &now);

}  // namespace faceview
