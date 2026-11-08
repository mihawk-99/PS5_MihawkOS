// SPDX-License-Identifier: GPL-3.0-or-later
// The four gradient themes of the approved prototype's Settings (prototypes/windows11-shell js/core.js Shell.wallpapers:
// glow, captured-motion, sunrise, flow) as 1920 x 1080 pictures, drawn by Chromium from the prototype's own CSS so the
// desktop shows what its theme tiles show. Run with a headless Chromium on --remote-debugging-port (tools/pw_prototype_capture.mjs):
//
//   node tools/render-theme-gradients.mjs themes/windows11/wallpapers
//
// Writes <out>/theme-<id>.png. The CSS is read from core.js, so a change there is a change here.
import { readFileSync, writeFileSync } from 'node:fs';
import { connect } from './pw_prototype_capture.mjs';

const [out] = process.argv.slice(2);
const core = readFileSync(new URL('../prototypes/windows11-shell/js/core.js', import.meta.url), 'utf8');
const session = await connect(process.env.CDP_PORT ?? '9333');
const { send } = session;
for (const id of ['glow', 'captured-motion', 'sunrise', 'flow']) {
  const css = core.match(new RegExp(`id: '${id}',[^}]*css: '([^']+)'`))[1];
  const html = `<!doctype html><html><body style="margin:0;width:1920px;height:1080px;background:${css}"></body></html>`;
  const { result: { targetId } } = await send('Target.createTarget', { url: 'about:blank' });
  const { result: { sessionId: s } } = await send('Target.attachToTarget', { targetId, flatten: true });
  await send('Emulation.setDeviceMetricsOverride', { width: 1920, height: 1080, deviceScaleFactor: 1, mobile: false }, s);
  await send('Page.navigate', { url: 'data:text/html;base64,' + Buffer.from(html).toString('base64') }, s);
  await new Promise((r) => setTimeout(r, 800));
  const shot = await send('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width: 1920, height: 1080, scale: 1 } }, s);
  writeFileSync(`${out}/theme-${id}.png`, Buffer.from(shot.result.data, 'base64'));
  await send('Target.closeTarget', { targetId });
  console.log(`theme-${id}.png`);
}
session.close();
