#pragma once
#include <Arduino.h>
static const char MOOD_VIDEO_JS[] PROGMEM = R"MOODJS(// Browser-side preparation of a mood video. BCV1 is a staged container, not an MP4.
(function (root) {
  'use strict';
  const FPS = 5, SECONDS = 20, RATE = 22050, MAX_SOURCE = 100 * 1024 * 1024;
  const MAX_FRAME = 100000, MAX_PACKAGE = 12 * 1024 * 1024;
  function pack(frames, wav) {
    if (!frames.length || frames.length > FPS * SECONDS || !wav || frames.some(f => !f.size || f.size > MAX_FRAME)) throw new Error('A video frame is too large or missing.');
    const size = 12 + frames.length * 4 + frames.reduce((n, f) => n + f.size, 0) + wav.size;
    if (size > MAX_PACKAGE) throw new Error('The converted video is too large. Try a shorter or simpler clip.');
    const header = new ArrayBuffer(12 + frames.length * 4), d = new DataView(header);
    for (let i = 0; i < 4; i++) d.setUint8(i, 'BCV1'.charCodeAt(i));
    d.setUint32(4, frames.length, true); d.setUint32(8, wav.size, true);
    frames.forEach((f, i) => d.setUint32(12 + i * 4, f.size, true));
    return new Blob([header, ...frames, wav], {type: 'application/octet-stream'});
  }
  function wavFromAudio(audio, duration) {
    const count = Math.floor(duration * RATE), b = new ArrayBuffer(44 + count * 2), d = new DataView(b);
    const put = (at, s) => { for (let i = 0; i < s.length; i++) d.setUint8(at + i, s.charCodeAt(i)); };
    put(0, 'RIFF'); d.setUint32(4, 36 + count * 2, true); put(8, 'WAVEfmt ');
    d.setUint32(16, 16, true); d.setUint16(20, 1, true); d.setUint16(22, 1, true);
    d.setUint32(24, RATE, true); d.setUint32(28, RATE * 2, true); d.setUint16(32, 2, true); d.setUint16(34, 16, true);
    put(36, 'data'); d.setUint32(40, count * 2, true);
    const channels = Array.from({length: audio.numberOfChannels}, (_, i) => audio.getChannelData(i));
    for (let i = 0; i < count; i++) {
      const pos = i * audio.sampleRate / RATE, lo = Math.floor(pos), frac = pos - lo;
      let sample = 0;
      for (const c of channels) sample += ((c[lo] || 0) * (1 - frac) + (c[lo + 1] || 0) * frac) / channels.length;
      sample = Math.max(-1, Math.min(1, sample)); d.setInt16(44 + i * 2, sample < 0 ? sample * 32768 : sample * 32767, true);
    }
    return new Blob([b], {type:'audio/wav'});
  }
  function event(target, ok, fail = 'error', timeout = 20000) {
    return new Promise((resolve, reject) => {
      const id = setTimeout(() => done(new Error('Video decoding timed out.')), timeout);
      function done(err) { clearTimeout(id); target.removeEventListener(ok, success); target.removeEventListener(fail, failure); err ? reject(err) : resolve(); }
      function success() { done(); } function failure() { done(new Error('Your browser could not decode this video.')); }
      target.addEventListener(ok, success, {once:true}); target.addEventListener(fail, failure, {once:true});
    });
  }
  async function prepare(file, progress = () => {}) {
    if (!file || !file.size || file.size > MAX_SOURCE) throw new Error('Choose a video smaller than 100 MB.');
    const url = URL.createObjectURL(file), video = document.createElement('video');
    video.preload = 'auto'; video.muted = true; video.playsInline = true;
    let context;
    try {
      const ready = event(video, 'loadedmetadata'); video.src = url; await ready;
      if (!Number.isFinite(video.duration) || video.duration <= 0 || !video.videoWidth || !video.videoHeight) throw new Error('This video has no usable picture or duration.');
      if (video.duration > 120) throw new Error('Choose a video no longer than 2 minutes; the first 20 seconds will be used.');
      const duration = Math.min(SECONDS, video.duration);
      const AudioCtx = root.AudioContext || root.webkitAudioContext;
      if (!AudioCtx) throw new Error('Audio conversion is not available in this browser.');
      progress('Preparing audio…'); context = new AudioCtx();
      let audio;
      try { audio = await context.decodeAudioData(await file.arrayBuffer()); }
      catch (_) { throw new Error('This browser could not decode the video’s audio. Try another browser or video format.'); }
      const wav = wavFromAudio(audio, duration);
      const canvas = document.createElement('canvas'); canvas.width = 320; canvas.height = 240;
      const ctx = canvas.getContext('2d');
      const scale = Math.max(320 / video.videoWidth, 240 / video.videoHeight), w = video.videoWidth * scale, h = video.videoHeight * scale;
      const frames = [], count = Math.max(1, Math.ceil(duration * FPS));
      for (let i = 0; i < count; i++) {
        progress(`Preparing frame ${i + 1} of ${count}…`);
        const seek = event(video, 'seeked'); video.currentTime = Math.min(i / FPS + 0.001, Math.max(0.001, duration - 0.001)); await seek;
        ctx.drawImage(video, (320 - w) / 2, (240 - h) / 2, w, h);
        const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/jpeg', 0.8));
        if (!blob) throw new Error('Could not convert a video frame.');
        frames.push(blob);
      }
      return pack(frames, wav);
    } finally { if (context) await context.close(); video.removeAttribute('src'); video.load(); URL.revokeObjectURL(url); }
  }
  root.MoodVideo = {prepare, pack, wavFromAudio};
  if (typeof module === 'object' && module.exports) module.exports = root.MoodVideo;
})(typeof window === 'undefined' ? globalThis : window);
)MOODJS";
