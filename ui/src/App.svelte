<script lang="ts">
  // SPDX-License-Identifier: GPL-3.0-or-later
  import { onMount } from 'svelte';
  import { ControlClient, type View } from './control';
  import type { Asset } from './generated/model';
  let view = $state<View>({status: 'Waiting for engine', snapshot: null, error: '', busy: false, reconnects: 0});
  let client = $state<ControlClient>();
  let selected = $state('');
  let name = $state('');
  const project = $derived(view.snapshot?.data.project);
  const surfaces = $derived(Object.values(project?.stage.surfaces || {}));
  const surface = $derived(surfaces.find(item => item.id === selected) || surfaces[0]);
  const compositions = $derived(Object.values(project?.definitions.compositions || {}));
  const assets = $derived(Object.values(project?.definitions.assets || {}));
  const editable = $derived(Boolean(view.snapshot?.isEditor && view.status === 'Connected' && !view.busy));
  const geometry = $derived.by(() => {
    if (!surface) return {points: [], triangles: []};
    const vertices = surface.geometry.vertices;
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
    for (const vertex of vertices) {minX = Math.min(minX, vertex.local.x); maxX = Math.max(maxX, vertex.local.x); minY = Math.min(minY, vertex.local.y); maxY = Math.max(maxY, vertex.local.y);}
    const points = vertices.map(vertex => ({id: vertex.id, x: 160+(vertex.local.x-minX)/Math.max(.001,maxX-minX)*480, y: 110+(vertex.local.y-minY)/Math.max(.001,maxY-minY)*280}));
    const lookup = new Map(points.map(point => [point.id, point]));
    return {points, triangles: surface.geometry.triangles.map(triangle => triangle.vertices.map(id => {const point = lookup.get(id)!; return `${point.x},${point.y}`;}).join(' '))};
  });
  let previousSurface: string | undefined, previousName = '';
  $effect(() => {
    const authoredName = surface?.name || '';
    if (previousSurface !== surface?.id || name === previousName) name = authoredName;
    previousSurface = surface?.id; previousName = authoredName;
  });
  onMount(() => {
    const fragment = new URLSearchParams(location.hash.slice(1));
    let url = fragment.get('engine') || `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/control`;
    if (!fragment.has('engine')) {try {url = sessionStorage.getItem('mapgyver.engine') || url;} catch { /* optional storage */ }}
    const key = 'mapgyver.capability.'+url;
    let capability = fragment.get('token');
    try {capability ||= sessionStorage.getItem(key);} catch { /* optional browser storage */ }
    if (fragment.has('token')) {
      try {sessionStorage.setItem(key, capability!); sessionStorage.setItem('mapgyver.engine', url);} catch { /* in-memory connection remains available */ }
      history.replaceState(null, '', location.pathname+location.search);
    }
    if (!capability || !/^[a-f0-9]{64}$/.test(capability)) {
      view = {...view, error: 'Open the editor launch link printed by MapGyver to connect.'}; return;
    }
    const target = new URL(url);
    if (!['ws:', 'wss:'].includes(target.protocol) || target.hostname !== '127.0.0.1' || target.pathname !== '/control' || target.username || target.password || target.search || target.hash) {
      view = {...view, error: 'This build connects to a local MapGyver engine.'}; return;
    }
    client = new ControlClient(url, capability, state => {view = state;});
    return () => client?.stop();
  });
  async function action(task: () => Promise<unknown>) {
    try {await task();} catch (error) {client?.report(error);}
  }
  async function rename() {
    if (client && surface) await action(() => client!.write({address: `/stage/surfaces/${surface.id}/name`, value: {type: 'text', value: name}}));
  }
  async function historyAction(operation: 'undo' | 'redo') {
    if (!client || !view.snapshot) return;
    await action(async () => {await client!.call(operation, {}, view.snapshot!.authoredRevision); await client!.call('snapshot', {});});
  }
  async function place(asset: Asset) {
    if (!client || !project || !view.snapshot) return;
    const outputs = asset.source.type === 'ExternalSource' ? asset.source.value.outputs : project.definitions.compositions[asset.source.value.composition].outputs;
    if (!outputs[0]) return;
    await action(async () => {
      await client!.call('transaction', {commands: [{schemaVersion: 1, command: {type: 'PlaceSource', value: {source: {asset: asset.id, port: outputs[0].id}, keepAlive: false}}}]}, view.snapshot!.authoredRevision);
      await client!.call('snapshot', {});
    });
  }
</script>

<svelte:head><title>MapGyver · Show control</title></svelte:head>
<div class="app">
  <header class="topbar">
    <a class="brand" href="/editor/" aria-label="MapGyver home"><span class="brand-mark">M<span>G</span></span><strong>MapGyver</strong><small>WORKSPACE</small></a>
    <div class="show-title"><span class="show-dot"></span> Untitled show <span class="muted">/</span> <span class="muted">Local engine</span></div>
    <div class="connection"><span class:online={view.status === 'Connected'} class="status-dot"></span><span data-testid="connection">{view.status}</span><span class="role">{view.snapshot?.isEditor ? 'EDITOR' : 'OBSERVER'}</span></div>
  </header>
  <div class="toolbar">
    <div class="section-tabs"><span class="active">Mapping</span></div>
    <div class="toolbar-actions">
      <button disabled={!editable} onclick={() => historyAction('undo')} aria-label="Undo">↶ <span>Undo</span></button>
      <button disabled={!editable} onclick={() => historyAction('redo')} aria-label="Redo">↷ <span>Redo</span></button>
      <span class="divider"></span><button onclick={() => client?.reconnect()} disabled={!client} aria-label="Reconnect">↻ <span>Reconnect</span></button>
    </div>
  </div>
  {#if view.error}<div role="alert" class="error-bar">{view.error}</div>{/if}
  <main>
    <aside class="library">
      <div class="panel-heading"><h2>Show library</h2><span class="count">{assets.length}</span></div>
      <div class="library-section"><h3>COMPOSITIONS</h3>
        {#each compositions as composition (composition.id)}
          <div class="composition-row"><span class="item-icon">▧</span><div><strong>{composition.name}</strong><small>{composition.layers.length} layers · {composition.outputs.length} outputs</small></div></div>
        {/each}
      </div>
      <div class="library-section"><h3>ASSETS</h3>
        {#each assets as asset (asset.id)}
          <div class="asset-row"><span class="item-icon">◇</span><div><strong>{asset.name}</strong><small>{asset.source.type === 'CompositionSource' ? 'Composition' : asset.source.value.adapter}</small></div><button class="icon-button" disabled={!editable} onclick={() => place(asset)} aria-label={`Place ${asset.name}`} title="Place an independent source">+</button></div>
        {/each}
      </div>
      <div class="library-footer"><span class="muted">SOURCE INSTANCES</span><strong data-testid="source-count">{Object.keys(project?.sources || {}).length}</strong><p>Independent playback state for every placement.</p></div>
    </aside>
    <section class="workspace" aria-label="Mapping workspace">
      <div class="workspace-heading"><div><span class="eyebrow">SURFACE SHAPE</span><h1>{surface?.name || 'Connect to your show'}</h1></div><span class="view-label">Authored geometry <span class="muted">· Fit</span></span></div>
      <div class="viewport">
        <div class="viewport-grid"></div>
        {#if surface}
          <svg viewBox="0 0 800 500" role="img" aria-label={`${surface.name} authored surface shape`}>
            {#each geometry.triangles as triangle}<polygon points={triangle} class="surface-fill"/>{/each}
            {#each geometry.points as point (point.id)}<rect x={point.x-4} y={point.y-4} width="8" height="8" class="control-point"/>{/each}
            <text x="400" y="245" text-anchor="middle" class="surface-label">{surface.name}</text>
            <text x="400" y="270" text-anchor="middle" class="surface-caption">{surface.geometry.vertices.length} control points</text>
          </svg>
        {:else}<div class="empty-stage"><span class="brand-mark large">MG</span><h2>Your show starts here</h2><p>Connect to MapGyver to inspect surfaces and compositions.</p></div>{/if}
        <div class="viewport-bottom"><span><span class="axis">X</span> <span class="axis">Y</span> Surface coordinates</span><span>{surface?.geometry.triangles.length || 0} triangles</span></div>
      </div>
      <div class="surface-strip"><h3>SURFACES <span class="count">{surfaces.length}</span></h3><div class="surface-cards">
        {#each surfaces as item (item.id)}<button class:selected={surface?.id === item.id} onclick={() => {selected = item.id;}}><span class="surface-thumbnail">▱</span><span>{item.name}</span><small>{item.contentPlacement ? 'Content assigned' : 'No content'}</small></button>{/each}
      </div></div>
      <div class="preview-status"><span class="preview-icon">▣</span><div><strong>Engine preview</strong><p>{view.snapshot?.renderer.available ? 'Preview transport is waiting for an encoded frame.' : 'Preview will appear when the native renderer is connected.'}</p></div><span class="badge">UNAVAILABLE</span></div>
    </section>
    <aside class="inspector">
      <div class="panel-heading"><h2>Inspector</h2><span class="inspector-symbol">☷</span></div>
      {#if surface}<div class="inspector-content">
        <div class="object-kind">SURFACE</div><h2>{surface.name}</h2>
        <form onsubmit={event => {event.preventDefault(); void rename();}}>
          <label for="surface-name">Name</label><input id="surface-name" bind:value={name} disabled={!editable} maxlength="256" autocomplete="off"/>
          <button class="primary" disabled={!editable || name === surface.name || !name.trim()}>Rename surface</button>
        </form>
        <div class="property-group"><h3>GEOMETRY</h3><dl><dt>Control points</dt><dd>{surface.geometry.vertices.length}</dd><dt>Triangles</dt><dd>{surface.geometry.triangles.length}</dd><dt>Content</dt><dd>{surface.contentPlacement ? 'Assigned' : 'Unassigned'}</dd></dl></div>
        <div class="property-group"><h3>PROJECTION</h3><dl><dt>Patches</dt><dd>{Object.values(project?.stage.patches || {}).filter(p => p.surface === surface.id).length}</dd><dt>Output targets</dt><dd>{Object.keys(project?.stage.outputs || {}).length}</dd></dl><p class="hint">Physical calibration belongs to projection patches and stays independent of content changes.</p></div>
        <div class="property-group"><h3>CONTROL</h3><dl><dt>Access</dt><dd>{view.snapshot?.isEditor ? 'Editing owner' : 'Read only'}</dd><dt>Revision</dt><dd data-testid="revision">{view.snapshot?.authoredRevision || '—'}</dd><dt>Saved revision</dt><dd>{view.snapshot?.persistence.savedRevision || 'Unavailable'}</dd></dl>
          {#if view.snapshot && !view.snapshot.isEditor && !view.snapshot.editOwner}<button onclick={() => action(async () => {await client!.call('claimEdit', {expectedOwnershipGeneration: view.snapshot!.ownershipGeneration}); await client!.call('snapshot', {});})}>Claim editing</button>{/if}
        </div>
      </div>{:else}<p class="inspector-empty">Select a surface to inspect its authored state.</p>{/if}
      <div class="inspector-footer"><span class="status-dot"></span> Native output not connected</div>
    </aside>
  </main>
  <footer class="statusbar"><span>MapGyver <span class="muted">0.1 · Development</span></span><span>{surfaces.length} surfaces <span class="muted">·</span> {Object.keys(project?.stage.outputs || {}).length} outputs</span><span>Authored r{view.snapshot?.authoredRevision || '—'} <span class="muted">·</span> Applied {view.snapshot?.renderer.appliedRevision || 'unavailable'} <span class="muted">·</span> Reconnects {view.reconnects}</span></footer>
</div>
