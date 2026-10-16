// SPDX-License-Identifier: GPL-3.0-or-later
import { defineConfig } from '@playwright/test';
const report = process.env.MAPGYVER_UI_REPORT || '../out/reports/ui-manual';
export default defineConfig({testDir: './tests', workers: 1, timeout: 30000, use: {viewport: {width: 1440, height: 940}, headless: true}, reporter: [['list'], ['json', {outputFile: report+'/playwright.json'}]], outputDir: report+'/browser'});
