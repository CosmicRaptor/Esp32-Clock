'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const A = require('../../public/face-assets.js');

const LIM = { sprite_frame: 160, sprite_w: 1280, sprite_h: 160, sprite_pixels: 51200 };

test('RGB565A8: header, RGB565 plane, then the alpha plane', () => {
  // 3x2 pixels: red, green, blue / white (half see-through), black (invisible), 0x88ccff
  const rgba = Uint8Array.from([
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
    255, 255, 255, 128, 0, 0, 0, 0, 0x88, 0xcc, 0xff, 7,
  ]);
  const out = A.encodeSprite(rgba, 3, 2);
  assert.equal(out.length, 12 + 3 * 2 * 3);
  // lv_image_header_t: magic 0x19, cf 0x14 (RGB565A8), flags 0, w, h, stride = 2 * w, reserved
  assert.deepEqual([...out.slice(0, 12)], [0x19, 0x14, 0, 0, 3, 0, 2, 0, 6, 0, 0, 0]);
  // RGB565, little-endian: red 0xF800, green 0x07E0, blue 0x001F, white 0xFFFF, black 0, 0x88ccff -> 0x8E7F... computed below
  const rgb = [...out.slice(12, 12 + 12)];
  assert.deepEqual(rgb.slice(0, 10), [0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF, 0x00, 0x00]);
  const v = ((0x88 >> 3) << 11) | ((0xcc >> 2) << 5) | (0xff >> 3);
  assert.deepEqual(rgb.slice(10), [v & 255, v >> 8]);
  assert.deepEqual([...out.slice(24)], [255, 255, 255, 128, 0, 7]);      // the A8 plane, row by row
});

test('the encoder\'s output is the fixture the Python side validates', () => {
  const rgba = Uint8Array.from([
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
    255, 255, 255, 128, 0, 0, 0, 0, 0x88, 0xcc, 0xff, 7,
  ]);
  const fixture = fs.readFileSync(path.join(__dirname, '..', 'vectors', 'sprite_3x2.bin'));
  assert.deepEqual([...A.encodeSprite(rgba, 3, 2)], [...fixture]);
});

test('wrong pixel count is refused', () => {
  assert.throws(() => A.encodeSprite(new Uint8Array(7), 2, 1));
});

test('sizes: never enlarge, fit 160x160 per frame, keep the sheet within the pixel budget', () => {
  assert.deepEqual(A.spriteSize(40, 30, 1, LIM), { w: 40, h: 30, fw: 40 });
  assert.deepEqual(A.spriteSize(320, 160, 1, LIM), { w: 160, h: 80, fw: 160 });
  assert.deepEqual(A.spriteSize(1000, 1000, 1, LIM), { w: 160, h: 160, fw: 160 });
  const sheet = A.spriteSize(1600, 200, 8, LIM);        // 8 frames of 200x200
  assert.equal(sheet.w % 8, 0);
  assert.ok(sheet.fw <= 160 && sheet.h <= 160 && sheet.w * sheet.h <= 51200, JSON.stringify(sheet));
  assert.ok(sheet.w <= 1280);
  const four = A.spriteSize(640, 160, 4, LIM);           // 4 frames of 160x160 = 102400 px: too many, shrinks
  assert.ok(four.w * four.h <= 51200, JSON.stringify(four));
});

test('too many frames for the image', () => {
  assert.throws(() => A.spriteSize(3, 10, 4, LIM));
});
