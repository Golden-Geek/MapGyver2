// SPDX-License-Identifier: GPL-3.0-or-later
import { test, expect } from '@playwright/test';
import { spawn, type ChildProcess } from 'node:child_process';
import { once } from 'node:events';
import { resolve } from 'node:path';
import { mkdirSync, writeFileSync } from 'node:fs';
import { createServer, type ViteDevServer } from 'vite';
let child: ChildProcess;
let development: ViteDevServer;
let developmentOrigin = '';
let url = '', capability = '', stderr = '';
const fixture = process.env.MAPGYVER_TEST_FIXTURE;
test.beforeAll(async () => {
  const root = resolve('..');
  const assets = process.env.MAPGYVER_TEST_ASSETS || resolve('dist');
  development = await createServer({server: {host: '127.0.0.1', port: 0, strictPort: true}});
  await development.listen();
  const address = development.httpServer!.address();
  if (!address || typeof address === 'string') throw new Error('Development server has no TCP endpoint');
  developmentOrigin = 'http://127.0.0.1:'+address.port;
  child = fixture ? spawn(fixture, [assets, '--browser-mode', developmentOrigin], {cwd: root, windowsHide: true}) :
    spawn(process.env.MAPGYVER_TEST_ENGINE || resolve(root, 'out/build/windows-local/Debug/mapgyver-engine.exe'), ['--serve', '--pipe-name', 'browser-'+Date.now(), '--oscquery', '--editor-dir', assets, '--dev-origin', developmentOrigin], {cwd: root, windowsHide: true});
  child.stderr!.on('data', data => {stderr = (stderr+String(data)).slice(-65536);});
  const ready = await new Promise<{sourceRevision: string; sourceDirty: boolean; editor: {url: string; capability: string}}>((accept, reject) => {
    let data = '';
    const timeout = setTimeout(() => reject(new Error('Engine ready timeout: '+stderr)), 10000);
    child.once('error', error => {clearTimeout(timeout); reject(error);});
    child.once('exit', code => {clearTimeout(timeout); reject(new Error('Engine exited '+code+': '+stderr));});
    child.stdout!.on('data', bytes => {data += String(bytes); if (data.includes('\n')) {clearTimeout(timeout); accept(JSON.parse(data.split('\n')[0]));}});
  });
  url = ready.editor.url; capability = ready.editor.capability;
  const report = process.env.MAPGYVER_UI_REPORT || '../out/reports/ui-manual';
  mkdirSync(report, {recursive: true});
  writeFileSync(report+'/identity.json', JSON.stringify({sourceRevision: ready.sourceRevision, sourceDirty: ready.sourceDirty, processMode: fixture ? 'portable-native-fixture' : 'native-Windows-engine', assets, capability: 'omitted'}, null, 2));
});
test.afterAll(async () => {
  await development?.close();
  if (!child) return;
  const unexpected = child.exitCode;
  const exited = once(child, 'exit');
  child.stdin!.end();
  if (!fixture) child.kill();
  if (unexpected === null) {
    const code = await Promise.race([exited, new Promise<never>((_, reject) => setTimeout(() => {child.kill(); reject(new Error('Owned engine did not exit'));}, 10000))]);
    if (fixture) expect(code[0], stderr).toBe(0);
  }
  expect(unexpected, stderr).toBeNull();
});
test('packaged editor shows real state, edits, history and ownership', async ({page, context}) => {
  const errors: string[] = [];
  let target = '', dropped = false, replayed = false;
  const traffic: string[] = [];
  await page.routeWebSocket('**/control', route => {
    const server = route.connectToServer();
    route.onMessage(data => {
      const message = JSON.parse(String(data));
      traffic.push('client '+message.operation+' '+message.requestId);
      if (!dropped && message.operation === 'parameters') target = message.requestId;
      server.send(data);
    });
    server.onMessage(data => {
      const message = JSON.parse(String(data));
      traffic.push('server '+message.type+' '+message.requestId+' '+message.status+' r'+message.authoredRevision+' replay='+message.replayed);
      if (!dropped && message.type === 'reply' && message.requestId === target && message.status === 'accepted') {
        dropped = true; void server.close({code: 4000, reason: 'Injected lost acknowledgement'}); void route.close({code: 4000, reason: 'Injected lost acknowledgement'}); return;
      }
      if (message.replayed) replayed = true;
      route.send(data);
    });
  });
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => {if (message.type() === 'error') errors.push(message.text());});
  await page.goto(url);
  expect(errors).toEqual([]);
  await expect(page.getByTestId('connection')).toHaveText('Connected');
  await expect(page.locator('.role')).toHaveText('EDITOR');
  await expect(page.getByLabel('Name', {exact: true})).toBeEnabled();
  expect(page.url()).not.toContain(capability);
  const initial = await page.getByLabel('Name', {exact: true}).inputValue();
  await page.getByLabel('Name', {exact: true}).fill('Browser surface café');
  await page.getByRole('button', {name: 'Rename surface', exact: true}).click();
  try {await expect(page.locator('h1')).toHaveText('Browser surface café');}
  catch (error) {
    await test.info().attach('fault-diagnostics', {body: JSON.stringify({traffic, errors, status: await page.getByTestId('connection').textContent(), error: await page.getByRole('alert').allTextContents()}), contentType: 'application/json'});
    throw error;
  }
  await expect(page.getByTestId('revision')).toHaveText('1');
  await expect.poll(() => replayed).toBe(true);
  await page.getByRole('button', {name: 'Undo', exact: true}).click();
  await expect(page.locator('h1')).toHaveText(initial);
  await page.getByRole('button', {name: 'Redo', exact: true}).click();
  await expect(page.locator('h1')).toHaveText('Browser surface café');
  const viewer = await context.newPage();
  await viewer.goto(url);
  await expect(viewer.getByTestId('connection')).toHaveText('Connected');
  await expect(viewer.locator('.role')).toHaveText('OBSERVER');
  await expect(viewer.getByLabel('Name', {exact: true})).toBeDisabled();
  await page.getByLabel('Name', {exact: true}).fill('Shared snapshot');
  await page.getByRole('button', {name: 'Rename surface', exact: true}).click();
  await expect(viewer.locator('h1')).toHaveText('Shared snapshot');
  await viewer.close();
  await page.getByRole('button', {name: /Place /}).first().click();
  await expect(page.getByTestId('source-count')).toHaveText('2');
  await page.getByRole('button', {name: 'Reconnect', exact: true}).click();
  await expect(page.locator('.statusbar')).toContainText('Reconnects 2');
  await expect(page.getByTestId('connection')).toHaveText('Connected');
  await expect(page.locator('h1')).toHaveText('Shared snapshot');
  await expect(page.getByLabel('Name', {exact: true})).toBeEnabled();
  await page.reload();
  await expect(page.getByTestId('connection')).toHaveText('Connected');
  await expect(page.locator('h1')).toHaveText('Shared snapshot');
  await expect(page.getByTestId('source-count')).toHaveText('2');
  await page.screenshot({path: (process.env.MAPGYVER_UI_REPORT || '../out/reports/ui-manual')+'/editor.png', fullPage: true});
  expect(errors).toEqual([]);
});
test('launch capability stays out of public metadata and static content', async ({request, page}) => {
  const base = new URL(url).origin;
  for (const route of ['/?HOST_INFO', '/editor/', '/editor/../LICENSE', '/editor/%2e%2e/LICENSE', '/preview']) {
    const response = await request.get(base+route);
    expect(await response.text()).not.toContain(capability);
    if (route === '/preview') {expect(response.status()).toBe(503); expect((await response.json()).frame).toBeNull();}
  }
  await page.goto(base+'/editor/');
  await expect(page.getByRole('alert')).toContainText('launch link');
});
test('separate Vite UI reload keeps native authored state', async ({page}) => {
  const engine = new URL(url);
  const fragment = new URLSearchParams({engine: 'ws://'+engine.host+'/control', token: capability});
  await page.goto(developmentOrigin+'/editor/#'+fragment);
  await expect(page.getByTestId('connection')).toHaveText('Connected');
  await expect(page.locator('.role')).toHaveText('EDITOR');
  const revision = await page.getByTestId('revision').textContent();
  const name = await page.locator('h1').textContent();
  const navigation = page.waitForEvent('load');
  development.ws.send({type: 'full-reload'});
  await navigation;
  await expect(page.getByTestId('connection')).toHaveText('Connected');
  await expect(page.getByTestId('revision')).toHaveText(revision!);
  await expect(page.locator('h1')).toHaveText(name!);
});
