// SPDX-License-Identifier: GPL-3.0-or-later
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
export default defineConfig({base: '/editor/', plugins: [svelte()], server: {host: '127.0.0.1', strictPort: true}});
