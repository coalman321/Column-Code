<script lang="ts">
  import { onMount } from 'svelte';
  import ColorPicker from './ColorPicker.svelte';
  import type { RGBWColor } from './ColorPicker.svelte';

  interface Client {
    mac: string;
    last_seen: string;
    connected: boolean;
  }

  let { presets = $bindable<any[]>([]), currentColor = $bindable<RGBWColor>({ r: 0, g: 0, b: 0, w: 0 }) } = $props();

  let clients     = $state<Client[]>([]);
  let loading     = $state(false);
  let error       = $state<string | null>(null);
  let autoRefresh = $state(true);
  let colors      = $state<Record<string, RGBWColor>>({});
  let openMac      = $state<string | null>(null);
  let popoverStyle = $state('');
  let listEl       = $state<HTMLElement | null>(null);

  const debounceTimers: Record<string, ReturnType<typeof setTimeout>> = {};
  let sleepPending    = $state<Record<string, boolean>>({});
  let deletePending   = $state<Record<string, boolean>>({});
  let sleepAllPending = $state(false);
  let openAll         = $state(false);
  let allSwatchEl     = $state<HTMLElement | null>(null);
  let allPopoverStyle = $state('');
  let presetOpen      = $state<string | null>(null);

  async function fetchClients() {
    loading = true;
    error = null;
    try {
      const res = await fetch('/clients/');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      clients = await res.json();
      for (const c of clients) {
        if (!colors[c.mac]) fetchColor(c.mac);
      }
    } catch (e) {
      error = e instanceof Error ? e.message : 'Fetch failed';
    } finally {
      loading = false;
    }
  }

  async function fetchColor(mac: string) {
    try {
      const res = await fetch(`/colors/${mac}`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      colors[mac] = await res.json();
    } catch {
      colors[mac] = { r: 0, g: 0, b: 0, w: 0 };
    }
  }

  function putColor(mac: string, color: RGBWColor) {
    clearTimeout(debounceTimers[mac]);
    debounceTimers[mac] = setTimeout(async () => {
      try {
        await fetch(`/colors/${mac}`, {
          method: 'PUT',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(color),
        });
      } catch { /* device retains last color */ }
    }, 150);
  }

  async function requestSleep(mac: string) {
    sleepPending = { ...sleepPending, [mac]: true };
    try {
      await fetch(`/clients/sleep/${mac}`, { method: 'POST' });
    } catch { /* ignore */ } finally {
      sleepPending = { ...sleepPending, [mac]: false };
    }
  }

  async function deleteClient(mac: string, connected: boolean) {
    const label = connected ? `⚠ ${mac} is still online. Remove anyway?` : `Remove ${mac}?`;
    if (!confirm(label)) return;
    deletePending = { ...deletePending, [mac]: true };
    try {
      const res = await fetch(`/clients/${mac}`, { method: 'DELETE' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      clients = clients.filter(c => c.mac !== mac);
      const { [mac]: _, ...rest } = colors;
      colors = rest;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Delete failed';
    } finally {
      deletePending = { ...deletePending, [mac]: false };
    }
  }

  async function sleepAll() {
    if (!confirm(`Send sleep request to all ${online} online client${online === 1 ? '' : 's'}?`)) return;
    sleepAllPending = true;
    try {
      await Promise.allSettled(
        clients.filter(c => c.connected).map(c => fetch(`/clients/sleep/${c.mac}`, { method: 'POST' }))
      );
    } finally {
      sleepAllPending = false;
    }
  }

  function putAllColor(color: RGBWColor) {
    currentColor = color;
    for (const c of clients) {
      colors[c.mac] = color;
      putColor(c.mac, color);
    }
  }

  export function applyPresetToAll(color: RGBWColor) {
    putAllColor(color);
  }

  function openAllPicker(e: MouseEvent) {
    e.stopPropagation();
    if (openAll) { openAll = false; return; }
    if (!listEl || !allSwatchEl) return;
    const swatchRect = allSwatchEl.getBoundingClientRect();
    const listRect   = listEl.getBoundingClientRect();
    const top   = swatchRect.bottom - listRect.top + 8;
    const right = listRect.right - swatchRect.right;
    allPopoverStyle = `top:${top}px;right:${right}px`;
    openAll = true;
  }

  function swatchBg(c: RGBWColor): string {
    const a = c.w / 255;
    const blend = (ch: number) => Math.round(ch + (255 - ch) * a);
    return `rgb(${blend(c.r)}, ${blend(c.g)}, ${blend(c.b)})`;
  }

  function openPicker(mac: string, e: MouseEvent) {
    e.stopPropagation();
    if (openMac === mac) { openMac = null; return; }
    if (!listEl) return;
    const swatchRect = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const listRect   = listEl.getBoundingClientRect();
    const top   = swatchRect.bottom - listRect.top + 8;
    const right = listRect.right - swatchRect.right;
    popoverStyle = `top:${top}px;right:${right}px`;
    openMac = mac;
  }

  onMount(() => {
    fetchClients();
    const id = setInterval(() => { if (autoRefresh) fetchClients(); }, 5000);
    return () => clearInterval(id);
  });

  let online  = $derived(clients.filter(c => c.connected).length);
  let offline = $derived(clients.filter(c => !c.connected).length);

  function timeAgo(iso: string) {
    const diff = Math.floor((Date.now() - new Date(iso).getTime()) / 1000);
    if (diff < 60)   return `${diff}s ago`;
    if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
    return `${Math.floor(diff / 3600)}h ago`;
  }

</script>

<svelte:window onclick={() => { openMac = null; openAll = false; }} />

<div class="client-list" bind:this={listEl}>
  <div class="toolbar">
    <h2>Connected Clients</h2>

    <div class="summary">
      <span class="pill online">{online} online</span>
      {#if offline > 0}
        <span class="pill offline">{offline} offline</span>
      {/if}
    </div>

    <div class="actions">
      <label class="toggle">
        <input type="checkbox" bind:checked={autoRefresh} />
        Auto-refresh
      </label>
      <button onclick={() => fetchClients()} disabled={loading}>
        {loading ? 'Loading…' : 'Refresh'}
      </button>
      <button
        class="sleep-btn"
        onclick={sleepAll}
        disabled={sleepAllPending || online === 0}
        title="Send sleep request to all online clients"
      >{sleepAllPending ? '…' : 'Sleep All'}</button>
      <button
        bind:this={allSwatchEl}
        class="swatch"
        class:active={openAll}
        style={`--bg: ${swatchBg(currentColor as RGBWColor)}`}
        onclick={openAllPicker}
        title="Set color on all clients"
        aria-label="Set color on all clients"
      ></button>
    </div>
  </div>

  {#if error}
    <p class="error">{error}</p>
  {/if}

  {#if clients.length === 0 && !loading}
    <p class="empty">No clients have connected yet.</p>
  {:else}
    <ul>
      {#each clients as client}
        {@const color = colors[client.mac]}
        <li class:online={client.connected} class:offline={!client.connected}>
          <div class="dot"></div>
          <span class="mac">{client.mac}</span>
          <span class="time">{timeAgo(client.last_seen)}</span>
          <button
            class="sleep-btn"
            onclick={() => requestSleep(client.mac)}
            disabled={sleepPending[client.mac] || !client.connected}
            aria-label="Sleep {client.mac}"
            title="Send sleep request"
          >{sleepPending[client.mac] ? '…' : 'Sleep'}</button>
          <button
            class="delete-btn"
            onclick={() => deleteClient(client.mac, client.connected)}
            disabled={deletePending[client.mac]}
            aria-label="Remove {client.mac}"
            title="Remove client"
          >{deletePending[client.mac] ? '…' : '✕'}</button>
          <button
            class="swatch"
            class:active={openMac === client.mac}
            style={color ? `--bg: ${swatchBg(color)}` : undefined}
            onclick={(e) => openPicker(client.mac, e)}
            aria-label="Set color for {client.mac}"
          ></button>
        </li>
      {/each}
    </ul>
  {/if}
  {#if openAll}
    <div
      class="popover"
      style={allPopoverStyle}
      onclick={(e) => e.stopPropagation()}
      role="dialog"
      aria-label="Set color on all clients"
    >
      <ColorPicker
        bind:value={currentColor as RGBWColor}
        onchange={(c) => putAllColor(c)}
      />
    </div>
  {/if}
  {#if openMac}
    {@const mac = openMac}
    <div
      class="popover"
      style={popoverStyle}
      onclick={(e) => e.stopPropagation()}
      role="dialog"
      aria-label="Color picker"
    >
      {#if colors[mac]}
        <ColorPicker
          bind:value={colors[mac]}
          onchange={(c) => putColor(mac, c)}
        />
      {:else}
        <p class="popover-loading">Loading…</p>
      {/if}
    </div>
  {/if}
</div>

<style>
  .client-list {
    position: relative;
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    font-family: system-ui, sans-serif;
    font-size: 0.9rem;
  }

  /* ── toolbar ── */
  .toolbar {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.75rem;
  }

  .toolbar h2 {
    margin: 0;
    font-size: 1.1rem;
    font-weight: 600;
    flex-shrink: 0;
  }

  .summary {
    display: flex;
    gap: 0.5rem;
  }

  .pill {
    padding: 0.15rem 0.6rem;
    border-radius: 999px;
    font-size: 0.75rem;
    font-weight: 600;
  }

  .pill.online  { background: #0d2d1a; color: #4caf82; }
  .pill.offline { background: #2a1a1a; color: #e05555; }

  .actions {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    margin-left: auto;
  }

  .toggle {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    cursor: pointer;
    color: #aaa;
    font-size: 0.85rem;
  }

  button {
    padding: 0.3rem 0.8rem;
    border: 1px solid #444;
    border-radius: 4px;
    background: #2a2a2a;
    color: inherit;
    cursor: pointer;
    font-size: 0.85rem;
  }

  button:hover:not(:disabled) { background: #3a3a3a; }
  button:disabled { opacity: 0.5; cursor: default; }

  /* ── list ── */
  ul {
    list-style: none;
    margin: 0;
    padding: 0;
    border: 1px solid #2a2a2a;
    border-radius: 6px;
    overflow: hidden;
  }

  li {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.6rem 0.875rem;
    border-bottom: 1px solid #1e1e1e;
  }

  li:last-child { border-bottom: none; }
  li:hover { background: #161616; }

  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
  }

  li.online  .dot { background: #4caf82; box-shadow: 0 0 6px #4caf8288; }
  li.offline .dot { background: #555; }

  .mac {
    font-family: monospace;
    font-size: 0.9rem;
    flex: 1;
  }

  li.offline .mac { color: #666; }

  .time {
    font-size: 0.8rem;
    color: #555;
    white-space: nowrap;
  }

  /* ── sleep button ── */
  .sleep-btn {
    padding: 0.2rem 0.55rem;
    font-size: 0.75rem;
    border: 1px solid #3a3a3a;
    border-radius: 4px;
    background: #1e1e2e;
    color: #8888cc;
    cursor: pointer;
    flex-shrink: 0;
  }

  .sleep-btn:hover:not(:disabled) {
    background: #2a2a44;
    border-color: #6666aa;
    color: #aaaaee;
  }

  .sleep-btn:disabled { opacity: 0.35; cursor: default; }

  /* ── delete button ── */
  .delete-btn {
    padding: 0.2rem 0.45rem;
    font-size: 0.75rem;
    border: 1px solid #3a2a2a;
    border-radius: 4px;
    background: #1e1010;
    color: #cc6666;
    cursor: pointer;
    flex-shrink: 0;
  }

  .delete-btn:hover:not(:disabled) {
    background: #3a1a1a;
    border-color: #aa4444;
    color: #ee8888;
  }

  .delete-btn:disabled { opacity: 0.35; cursor: default; }

  /* ── color swatch button ── */
  .swatch {
    width: 28px;
    height: 28px;
    padding: 0;
    border-radius: 5px;
    border: 2px solid #333;
    background: var(--bg, #1a1a1a);
    cursor: pointer;
    flex-shrink: 0;
    transition: border-color 0.15s, box-shadow 0.15s;
  }

  .swatch:hover {
    border-color: #666;
    background: var(--bg, #1a1a1a);
  }

  .swatch.active {
    border-color: #888;
    box-shadow: 0 0 0 2px rgba(255,255,255,0.1);
    background: var(--bg, #1a1a1a);
  }

  /* ── popover ── */
  .popover {
    position: absolute;
    z-index: 200;
    background: #1a1a1a;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 0.75rem;
    box-shadow: 0 8px 32px rgba(0,0,0,0.6);
  }

  .popover-loading {
    color: #555;
    font-size: 0.85rem;
    margin: 0;
    padding: 0.5rem;
    font-family: system-ui, sans-serif;
  }

  /* ── misc ── */
  .empty {
    color: #555;
    font-size: 0.875rem;
    margin: 0;
    padding: 1.5rem;
    text-align: center;
    border: 1px solid #2a2a2a;
    border-radius: 6px;
  }

  .error {
    color: #f55;
    font-size: 0.85rem;
    margin: 0;
  }

</style>
