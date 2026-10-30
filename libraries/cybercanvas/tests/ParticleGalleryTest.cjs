// Uses an installed playwright-core module and its matching cached Chromium.
// node ParticleGalleryTest.cjs <playwright-core path> <gallery directory> <report directory> [Chromium executable]
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { pathToFileURL } = require('node:url');
const { chromium } = require(process.argv[2]);
const gallery = path.resolve(process.argv[3]);
const output = path.resolve(process.argv[4]);
const checks = [], errors = [];

function check(name, condition) {
  checks.push({ name, passed: Boolean(condition) });
  if (!condition) throw new Error(name);
}

const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

(async () => {
  fs.mkdirSync(output, { recursive: true });
  const sandbox = { window: {} };
  vm.runInNewContext(fs.readFileSync(path.join(gallery, 'effects-data.js'), 'utf8'), sandbox);
  const data = sandbox.window.PARTICLE_EFFECTS;
  check('Twelve native scenes, 60 FPS, 240 frames', data.scenes.length === 12 && data.fps === 60 && data.scenes.every(s => s.frames.length === 240));
  check('Every exported particle value is finite', data.scenes.every(s => s.frames.every(f => f[0].every(p => p.length === 8 && p.every(Number.isFinite)))));
  const fountain = data.scenes.find(s => s.id === 'fountain');
  check('Fountain really injects fluid particles', fountain.frames[239][0].length > fountain.frames[0][0].length + 100);
  const stirred = data.scenes.find(s => s.id === 'stirred');
  check('Motorized body really rotates', Math.abs(stirred.frames[90][1] - stirred.frames[0][1]) > 30);
  const water = data.scenes.find(s => s.id === 'water'), syrup = data.scenes.find(s => s.id === 'syrup');
  const difference = water.frames[90][0].reduce((sum, p, i) => sum + Math.hypot(p[1] - syrup.frames[90][0][i][1], p[2] - syrup.frames[90][0][i][2]), 0);
  check('Viscosity examples have different actual motion', difference > 200);
  const browser = await chromium.launch({ headless: true, ...(process.argv[5] ? { executablePath: path.resolve(process.argv[5]) } : {}) });
  try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } });
    page.on('pageerror', error => errors.push(String(error)));
    page.on('requestfailed', request => errors.push(`${request.url()}: ${request.failure()?.errorText}`));
    await page.addInitScript(() => {
      const nativeRaf = window.requestAnimationFrame;
      window.__galleryFrames = 0;
      window.requestAnimationFrame = callback => nativeRaf.call(window, time => {
        window.__galleryFrames++; callback(time);
      });
    });
    await page.goto(pathToFileURL(path.join(gallery, 'index.html')).href);
    await page.waitForFunction(() => document.querySelectorAll('article').length === 12 && document.querySelector('canvas')?.width > 0);
    check('All cards render without an error notice', await page.locator('article').count() === 12 && await page.locator('#error').isHidden());
    await page.locator('#play').click();
    await wait(150);
    const frame = await page.evaluate(() => window.__galleryFrames);
    await wait(180);
    check('Global pause stops animation callbacks', await page.evaluate(() => window.__galleryFrames) === frame);
    await page.locator('#seek').fill('1500');
    await page.setViewportSize({ width: 1440, height: 2400 });
    await wait(80);
    await page.screenshot({ path: path.join(output, 'gallery-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 1440, height: 1100 });
    check('Lucide icons use inline SVG and inherit text color', await page.locator('#restart .icon svg').count() === 1);
    await page.locator('[data-filter="fluid"]').click();
    check('Fluid filter shows four cases and a selected state', await page.locator('article:visible').count() === 4 && await page.locator('[data-filter="fluid"]').getAttribute('aria-pressed') === 'true');
    await page.locator('#dpi').selectOption('2');
    await wait(100);
    check('Actual-DPI canvas buffers resize to CSS size', await page.locator('article:visible canvas').evaluateAll(nodes => nodes.every(node => Math.abs(node.width - Math.round(node.getBoundingClientRect().width * 2)) <= 1)));
    await page.locator('article[data-id="stirred"] .expand').click();
    check('Expanded scene opens as an accessible dialog', await page.locator('#detail').isVisible() && await page.locator('#detailTitle').innerText() === '机械搅拌');
    check('Native poster uses selected DPI', (await page.locator('#native').getAttribute('href')).endsWith('stirred-200.png'));
    await page.screenshot({ path: path.join(output, 'gallery-detail.png') });
    await page.keyboard.press('Escape');
    check('Escape closes the detail dialog', await page.locator('#detail').isHidden());
    await page.locator('[data-filter="all"]').click();
    await page.locator('#dpi').selectOption('1');
    await page.locator('#play').click();
    await page.locator('article[data-id="sparks"] .toggle').click();
    await wait(90);
    const still = await page.locator('article[data-id="sparks"] canvas').evaluate(node => node.toDataURL());
    await wait(200);
    check('Individual pause holds its own canvas', still === await page.locator('article[data-id="sparks"] canvas').evaluate(node => node.toDataURL()));
    await page.locator('article[data-id="sparks"] .toggle').click();
    await wait(220);
    check('Individual resume advances again', still !== await page.locator('article[data-id="sparks"] canvas').evaluate(node => node.toDataURL()));
    await page.locator('#restart').click();
    check('Restart resets individual pause controls', await page.locator('.toggle[aria-pressed="true"]').count() === 0);
    await page.setViewportSize({ width: 390, height: 844 });
    await page.locator('#play').click();
    await page.locator('#seek').fill('1500');
    await page.evaluate(() => window.scrollTo(0, 0));
    await wait(100);
    check('Mobile layout has no horizontal overflow', await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
    await page.screenshot({ path: path.join(output, 'gallery-mobile.png') });
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await page.reload();
    await page.waitForSelector('article');
    check('Reduced-motion preference starts paused', await page.locator('#play').innerText() === '全部播放');
    check('No browser exceptions or failed resources', errors.length === 0);
  } finally {
    await browser.close();
  }
  fs.writeFileSync(path.join(output, 'web-review.json'), JSON.stringify({ passed: true, checks, errors }, null, 2));
  console.log(`PASS ${checks.length} gallery checks, native samples and browser controls`);
})().catch(error => {
  fs.writeFileSync(path.join(output, 'web-review.json'), JSON.stringify({ passed: false, checks, errors, error: String(error) }, null, 2));
  console.error(error); process.exitCode = 1;
});
