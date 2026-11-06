// Browser-side image work for the Faces page: turn what the user uploads into what the clock wants.
//   background -> a 320x240 baseline JPEG (cropped to fill)
//   sprite     -> an LVGL 9 "RGB565A8" raw image (so the clock only copies bytes) + a PNG for the preview
// encodeSprite() is plain maths on bytes, so it is unit-tested in node (tests/js/face-assets.test.js).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.FaceAssets = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const SCREEN_W = 320, SCREEN_H = 240;
  const LV_MAGIC = 0x19, LV_CF_RGB565A8 = 0x14, HEADER = 12;

  // RGBA bytes (straight alpha, as getImageData returns them) -> the LVGL 9 RGB565A8 file: the 12-byte
  // lv_image_header_t (magic, color format, flags, w, h, stride = 2 * w, reserved; little-endian), then the RGB565
  // plane (h rows of w little-endian 16-bit pixels), then the A8 plane (h rows of w bytes).
  function encodeSprite(rgba, w, h) {
    if (rgba.length !== w * h * 4) throw new Error('pixel data does not match the size');
    const out = new Uint8Array(HEADER + w * h * 3);
    out[0] = LV_MAGIC; out[1] = LV_CF_RGB565A8;                  // flags (2 bytes) stay 0
    out[4] = w & 255; out[5] = w >> 8; out[6] = h & 255; out[7] = h >> 8;
    const stride = w * 2;
    out[8] = stride & 255; out[9] = stride >> 8;                  // reserved (2 bytes) stays 0
    const alpha = HEADER + w * h * 2;
    for (let i = 0, p = HEADER; i < w * h; i++, p += 2) {
      const r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
      const v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      out[p] = v & 255; out[p + 1] = v >> 8;
      out[alpha + i] = rgba[i * 4 + 3];
    }
    return out;
  }

  // The size a sprite is shrunk to: each frame at most `frame` x `frame`, and the whole sheet within the pixel budget.
  // Never enlarges. Returns {w, h, fw} (the whole width, the height, one frame's width).
  function spriteSize(srcW, srcH, frames, lim) {
    const fw0 = Math.floor(srcW / frames);
    if (fw0 < 1 || srcH < 1) throw new Error('That image is too small for that many frames.');
    const s = Math.min(1, lim.sprite_frame / fw0, lim.sprite_frame / srcH, Math.sqrt(lim.sprite_pixels / (fw0 * frames * srcH)),
      lim.sprite_h / srcH, lim.sprite_w / (fw0 * frames));
    const fw = Math.max(1, Math.floor(fw0 * s)), h = Math.max(1, Math.floor(srcH * s));
    return { w: fw * frames, h, fw };
  }

  // ---- everything below needs a browser ----

  async function decode(file) {
    try { return await createImageBitmap(file, { imageOrientation: 'from-image' }); }
    catch (e) {
      return await new Promise((res, rej) => {
        const i = new Image();
        i.onload = () => res(i);
        i.onerror = () => rej(new Error("This browser can't open that image. Try a PNG or JPEG."));
        i.src = URL.createObjectURL(file);
      });
    }
  }

  function canvas(w, h) {
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    return c;
  }

  // Draw `src` (sx, sy, sw, sh) into a new w x h canvas, halving first when shrinking a lot (it keeps edges sharp).
  function resample(src, sx, sy, sw, sh, w, h) {
    let cur = src, cx = sx, cy = sy, cw = sw, ch = sh;
    while (cw / 2 >= w && ch / 2 >= h) {
      const nw = Math.max(w, Math.round(cw / 2)), nh = Math.max(h, Math.round(ch / 2));
      const c = canvas(nw, nh), x = c.getContext('2d');
      x.imageSmoothingQuality = 'high';
      x.drawImage(cur, cx, cy, cw, ch, 0, 0, nw, nh);
      cur = c; cx = 0; cy = 0; cw = nw; ch = nh;
    }
    const out = canvas(w, h), x = out.getContext('2d');
    x.imageSmoothingQuality = 'high';
    x.drawImage(cur, cx, cy, cw, ch, 0, 0, w, h);
    return out;
  }

  const toBlob = (c, type, q) => new Promise(res => c.toBlob(res, type, q));

  // Any image -> a 320x240 baseline JPEG, centre-cropped to fill. Returns {blob, cropped}.
  async function makeBackground(file) {
    const img = await decode(file);
    const w = img.width, h = img.height;
    const target = SCREEN_W / SCREEN_H;
    let sw = w, sh = h;
    if (w / h > target) sw = Math.round(h * target); else sh = Math.round(w / target);
    const sx = Math.round((w - sw) / 2), sy = Math.round((h - sh) / 2);
    const flat = canvas(SCREEN_W, SCREEN_H), x = flat.getContext('2d');   // transparent PNGs go on black
    x.fillStyle = '#000';
    x.fillRect(0, 0, SCREEN_W, SCREEN_H);
    x.drawImage(resample(img, sx, sy, sw, sh, SCREEN_W, SCREEN_H), 0, 0);
    for (const q of [0.92, 0.85, 0.75, 0.65]) {
      const blob = await toBlob(flat, 'image/jpeg', q);
      if (!blob) throw new Error("This browser couldn't make a JPEG.");
      if (blob.size < 180000 || q === 0.65) return { blob, cropped: Math.abs(w / h - target) > 0.02, canvas: flat };
    }
  }

  // Any image -> {bin, png, w, h, canvas}: the clock's file, the preview PNG, and the size. `frames` > 1 means the
  // image is a sheet of that many equal frames side by side.
  async function makeSprite(file, frames, lim) {
    const img = await decode(file);
    frames = Math.max(1, Math.floor(frames) || 1);
    const fw0 = Math.floor(img.width / frames);
    const size = spriteSize(img.width, img.height, frames, lim);
    const c = resample(img, 0, 0, fw0 * frames, img.height, size.w, size.h);
    const data = c.getContext('2d').getImageData(0, 0, size.w, size.h).data;
    const png = await toBlob(c, 'image/png');
    if (!png) throw new Error("This browser couldn't make a PNG.");
    return { bin: encodeSprite(data, size.w, size.h), png, w: size.w, h: size.h, canvas: c };
  }

  return { SCREEN_W, SCREEN_H, encodeSprite, spriteSize, makeBackground, makeSprite };
});
