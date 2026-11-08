// SPDX-License-Identifier: GPL-3.0-or-later
// The approved prototype's own pixels for chrome the native shell draws as fixed images: each element is rendered
// alone (everything else hidden, its backdrop filters off) over black and over white, and its premultiplied colour and
// alpha follow per pixel and channel from the two captures: premultiplied colour = black, alpha = 255 - (white - black).
// The native shell composes the sprite over whatever is beneath, as Chromium composed it over the backdrop.
// Measured, not re-rendered: librsvg and GDI draw the same SVG and text up to 66 levels away from Skia.
//
//   node tools/measure-prototype-sprites.mjs <out.json> <open> <theme> <name>=<selector>[!<hide>][~hover|~press][@WxH] ...
//     the first match of each selector; !<hide> hides its descendants matching <hide> (a plate without its glyph);
//     ~hover / ~press hold the pointer over it (pressed); @WxH crops its box to that size from its top left
//     writes {url, theme, sprites: {name: {x, y, w, h, black, white: base64 of w*h*3 RGB}}}
import { writeFileSync } from 'node:fs';
import { connect, decode, page } from './pw_prototype_capture.mjs';

const [out, open, theme, ...wanted] = process.argv.slice(2);
const base = process.env.PROTOTYPE_BASE ?? 'http://127.0.0.1:8778/prototypes/windows11-shell/index.html';
const url = `${base}?fresh&${theme}${open === '-' ? '' : `&open=${open}`}`;
const session = await connect(process.env.CDP_PORT ?? '9333');
const view = await page(session, url);
await new Promise((settle) => setTimeout(settle, 250));  // the dialogs open on a timer after the page's load
// Nothing moves, and the acrylic's noise lies under the chrome, so it belongs to the backdrop, not to a sprite.
const still = `(() => { const s = document.createElement('style');
  s.textContent = '*{transition:none!important;animation:none!important;caret-color:transparent!important}' +
                  '.acrylic::before,.acrylic-base::before{display:none!important}';
  document.head.appendChild(s); })()`;
await view.evaluate(still);
const result = { url, theme, sprites: {} };
for (const item of wanted) {
  const [, name, selector, hide, pointer, size] = item.match(/^([\w-]+)=(.+?)(?:!(.+?))?(?:~(hover|press))?(?:@(\d+x\d+))?$/);
  const box = await view.evaluate(`(() => {
    const el = document.querySelector(${JSON.stringify(selector)}); if (!el) return null;
    const r = el.getBoundingClientRect(); return {x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height)};
  })()`);
  if (!box) throw new Error(`${name}: nothing matches ${selector}`);
  if (size) [box.w, box.h] = size.split('x').map(Number);
  if (pointer) {
    await view.mouse('mouseMoved', box.x + box.w / 2, box.y + box.h / 2);
    if (pointer === 'press') await view.mouse('mousePressed', box.x + box.w / 2, box.y + box.h / 2);
  }
  const shots = [];
  for (const ground of ['#000', '#fff']) {
    // Only the sprite's own subtree paints: every other box is hidden, and its ancestors draw nothing.
    await view.evaluate(`(() => {
      const el = document.querySelector(${JSON.stringify(selector)});
      const keep = new Set(); for (let e = el.parentElement; e; e = e.parentElement) keep.add(e);
      for (const e of document.querySelectorAll('body *')) {
        if (keep.has(e)) for (const [k, v] of [['background', 'transparent'], ['box-shadow', 'none'], ['border-color', 'transparent'],
                                             ['backdrop-filter', 'none'], ['-webkit-backdrop-filter', 'none']]) e.style.setProperty(k, v, 'important');
        else if (e !== el && !el.contains(e)) e.style.setProperty('visibility', 'hidden', 'important');
      }
      ${hide ? `for (const e of el.querySelectorAll(${JSON.stringify(hide)})) e.style.setProperty('visibility', 'hidden', 'important');` : ''}
      for (const e of [document.documentElement, document.body]) e.style.setProperty('background', '${ground}', 'important');
    })()`);
    shots.push(decode(await view.png()));
  }
  if (pointer === 'press') await view.mouse('mouseReleased', box.x + box.w / 2, box.y + box.h / 2);
  if (pointer) await view.mouse('mouseMoved', 1, 1);
  // Lossless: the element over black is its premultiplied colour; white minus black is 255 minus its alpha, per channel.
  const black = Buffer.alloc(box.w * box.h * 3), white = Buffer.alloc(box.w * box.h * 3);
  for (let y = 0; y < box.h; ++y) for (let x = 0; x < box.w; ++x) {
    const b = shots[0].at(box.x + x, box.y + y), w = shots[1].at(box.x + x, box.y + y);
    for (let k = 0; k < 3; ++k) { black[(y * box.w + x) * 3 + k] = b[k]; white[(y * box.w + x) * 3 + k] = w[k]; }
  }
  result.sprites[name] = { ...box, black: black.toString('base64'), white: white.toString('base64') };
  console.log(`${name}: ${box.w}x${box.h} at ${box.x},${box.y}`);
  // The next sprite starts from the page as it loads (the hiding above is undone with it).
  await view.evaluate(`location.reload()`); await new Promise((r) => setTimeout(r, 3500));
  await view.evaluate(`window.Shell && (Shell.set('brightness', 100), Shell.set('nightLight', false), Shell.set('animations', false))`);
  if (process.env.PROTOTYPE_SETUP) { await view.evaluate(process.env.PROTOTYPE_SETUP); await new Promise((r) => setTimeout(r, 500)); }
  await view.evaluate(still); await new Promise((r) => setTimeout(r, 300));
}
writeFileSync(out, JSON.stringify(result));
await view.close(); session.close();
