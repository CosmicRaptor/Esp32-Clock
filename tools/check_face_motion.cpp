// Prints the face motion maths for a fixed set of cases; tools/check_face_motion.js prints the same from the portal's
// JS, and tools/check_face_motion.sh compares the two. Build: see the script.
#include <stdio.h>
#include <string.h>

#include "../face_motion.h"

using namespace facemotion;
using M = bhoomi_clock_v1_Motion_Type;

static Motion mo(M type, uint32_t period, uint32_t phase = 0) {
  Motion m = {};
  m.type = type;
  m.period_ms = period;
  m.phase_ms = phase;
  return m;
}

int main() {
  Layer L = {};
  L.stagger_phase_ms = -250;
  L.stagger_x = 7;
  L.stagger_y = -3;
  L.x = 5;
  L.y = 9;
  L.motion_count = 3;
  L.motion[0] = mo(bhoomi_clock_v1_Motion_Type_BOB, 3200, 100);
  L.motion[0].amp = 4;
  L.motion[1] = mo(bhoomi_clock_v1_Motion_Type_PATH, 8000);
  L.motion[1].dx = -30;
  L.motion[1].dy = 12;
  L.motion[2] = mo(bhoomi_clock_v1_Motion_Type_FADE, 2000, 300);
  L.motion[2].keys_count = 3;
  L.motion[2].keys[0] = {0, 100};
  L.motion[2].keys[1] = {400, 20};
  L.motion[2].keys[2] = {800, 70};
  Layer L2 = {};
  L2.motion_count = 3;
  L2.motion[0] = mo(bhoomi_clock_v1_Motion_Type_SWAY, 1000, 250);
  L2.motion[0].amp = -6;
  L2.motion[1] = mo(bhoomi_clock_v1_Motion_Type_PULSE, 1500);
  L2.motion[1].min_pct = 90;
  L2.motion[1].max_pct = 110;
  L2.motion[2] = mo(bhoomi_clock_v1_Motion_Type_BLINK, 1000, 900);
  L2.motion[2].on_ms = 150;
  Layer L3 = {};
  L3.motion_count = 2;
  L3.motion[0] = mo(bhoomi_clock_v1_Motion_Type_FRAMES, 667);
  L3.motion[0].frames = 4;
  L3.motion[0].fps = 6;
  L3.motion[1] = mo(bhoomi_clock_v1_Motion_Type_PATH, 1000);
  L3.motion[1].dx = 40;
  L3.motion[1].loop = true;
  Layer L4 = {};  // scattered and scaled down, with a bob
  L4.scale_pct = 60;
  L4.scatter = true;
  L4.scatter_area = {10, 20, 200, 150};
  L4.scatter_seed = 4242;
  L4.motion_count = 2;
  L4.motion[0] = mo(bhoomi_clock_v1_Motion_Type_BOB, 2000);
  L4.motion[0].amp = 3;
  L4.motion[1] = mo(bhoomi_clock_v1_Motion_Type_PULSE, 900, 100);
  L4.motion[1].min_pct = 90;
  L4.motion[1].max_pct = 120;
  const Layer *layers[4] = {&L, &L2, &L3, &L4};
  for (int copy = 0; copy < 5; copy++) {
    double x, y;
    copyOrigin(L4, copy, 24, 22, x, y);
    printf("origin scatter %d %.3f %.3f\n", copy, x, y);
    copyOrigin(L, copy, 24, 22, x, y);
    printf("origin row %d %.3f %.3f\n", copy, x, y);
  }
  const uint32_t times[] = {0, 137, 500, 1234, 2999, 9999, 123456};
  for (int li = 0; li < 4; li++)
    for (int copy = 0; copy < 5; copy++)
      for (uint32_t t : times) {
        Pose p = layerPose(*layers[li], copy, t);
        printf("pose %d %d %u %.3f %.3f %.3f %.3f %d\n", li, copy, t, p.dx, p.dy, p.opacity, p.scale, p.frame);
      }

  const uint32_t seeds[][2] = {{1, 0}, {1234, 7}, {4294967295u, 39}};
  for (auto &s : seeds) {
    uint16_t r[6];
    draws(s[0], s[1], r);
    printf("mix %u %u %u\n", s[0], s[1], mix32(s[0], s[1]));
    printf("draws %u %u %u %u %u %u %u %u\n", s[0], s[1], r[0], r[1], r[2], r[3], r[4], r[5]);
  }
  for (int kind = 0; kind < 3; kind++) {
    Emitter E = {};
    E.kind = (bhoomi_clock_v1_Emitter_Kind)kind;
    E.sprites_count = 3;
    E.area = {20, 30, 200, 150};
    E.speed_min = 20;
    E.speed_max = 40;
    E.sway = 10;
    E.period_ms = 4000;
    E.phase_ms = 500;
    E.seed = 1234;
    for (int i = 0; i < 6; i++) {
      ParticleParams q = particleParams(E, i);
      for (uint32_t t : times) {
        int sw = 8 + 2 * q.sprite, sh = 10 + q.sprite;
        ParticlePos p = particlePos(E, q, sw, sh, t);
        printf("part %d %d %u %d %d %d %.3f\n", kind, i, t, q.sprite, p.x, p.y, p.opacity);
      }
    }
  }
}
