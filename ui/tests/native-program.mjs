// SPDX-License-Identifier: GPL-3.0-or-later
import { chromium } from 'playwright';
import readline from 'node:readline';
let server, browser, page;
const send = value => process.stdout.write(JSON.stringify(value)+'\n');
try {
  for await (const line of readline.createInterface({input: process.stdin})) {
    const command = JSON.parse(line);
    if (command.operation === 'open') {
      server = await chromium.launchServer({headless: true});
      browser = await chromium.connect(server.wsEndpoint());
      page = await browser.newPage();
      await page.goto(command.url);
      await page.getByRole('link', {name: 'MapGyver home', exact: true}).waitFor();
      await page.getByTestId('connection').filter({hasText: 'Connected'}).waitFor();
      send({event: 'opened', ownedBrowserPid: server.process().pid});
    } else if (command.operation === 'reload') {
      await page.reload();
      await page.getByTestId('connection').filter({hasText: 'Connected'}).waitFor();
      send({event: 'reloaded'});
    } else if (command.operation === 'set_control') {
      await page.getByTestId(`output-${command.output}-${command.parameter}`).setChecked(command.value);
      await page.waitForFunction(revision => document.querySelector('[data-testid="revision"]')?.textContent === revision, command.revision);
      if (command.screenshot) await page.screenshot({path: command.screenshot});
      send({event: 'ui_control_applied'});
    } else if (command.operation === 'release_edit') {
      await page.getByRole('button', {name: 'Release editing', exact: true}).click();
      await page.locator('.role').filter({hasText: 'OBSERVER'}).waitFor();
      send({event: 'editing_released'});
    } else if (command.operation === 'crash') {
      const child = server.process();
      const terminated = new Promise((resolve, reject) => {
        const deadline = setTimeout(() => reject(new Error('Owned browser did not terminate')), 5000);
        child.once('exit', (code, signal) => {clearTimeout(deadline); resolve({code, signal});});
      });
      if (!child.kill()) throw new Error('Could not terminate owned browser');
      send({event: 'crashed', ownedBrowserPid: child.pid, termination: await terminated});
      break;
    } else throw new Error('Unknown qualification command');
  }
} catch (error) {
  process.stderr.write('Native program browser qualification failed: '+error.name+'\n');
  process.exitCode = 1;
} finally {
  if (server) await server.close();
}
