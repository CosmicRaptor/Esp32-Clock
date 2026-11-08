const test = require('node:test');
const assert = require('node:assert/strict');
const mood = require('../../public/mood-video.js');
test('packs frames and WAV audio in the staged container', async () => {
  const wav = mood.wavFromAudio({sampleRate:22050, numberOfChannels:1, getChannelData:()=>new Float32Array([0, .5, -1])}, 3/22050);
  const data = new DataView(await mood.pack([new Blob([Uint8Array.from([1,2,3])])], wav).arrayBuffer());
  assert.equal(Buffer.from(data.buffer, 0, 4).toString(), 'BCV1');
  assert.equal(data.getUint32(4, true), 1);
  assert.equal(data.getUint32(8, true), 50);
  assert.equal(data.getUint32(12, true), 3);
  assert.equal(Buffer.from(data.buffer, 19, 4).toString(), 'RIFF');
  assert.equal(data.getInt16(19+44+2, true), 16383);
  assert.equal(data.getInt16(19+44+4, true), -32768);
});
test('rejects missing or oversized frames', () => {
  assert.throws(() => mood.pack([], new Blob(['x'])));
  assert.throws(() => mood.pack([new Blob([new Uint8Array(100001)])], new Blob(['x'])));
});
