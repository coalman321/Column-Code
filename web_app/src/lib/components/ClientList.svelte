<script lang="ts">
  import { onMount } from 'svelte';
  import ColorPicker from './ColorPicker.svelte';
  import type { RGBWColor } from './ColorPicker.svelte';

  interface Client {
    name: string;  /* display_name or MAC */
    mac?: string;
    display_name?: string | null;
    last_seen?: string;
    connected: boolean;
    battery_percent?: number | null;
    battery_mv?: number | null;
    color?: RGBWColor;
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
  let otaPending      = $state<Record<string, boolean>>({});
  let blinkPending    = $state<Record<string, boolean>>({});
  let openAll         = $state(false);
  let allSwatchEl     = $state<HTMLElement | null>(null);
  let allPopoverStyle = $state('');
  let presetOpen      = $state<string | null>(null);
  let editingName     = $state<string | null>(null);
  let editingValue    = $state('');
  let renamePending   = $state<Record<string, boolean>>({});

  async function fetchClients() {
    loading = true;
    error = null;
    try {
      const res = await fetch('/clients/');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      clients = await res.json();
      // Fetch detailed info for each client
      for (const c of clients) {
        fetchDeviceInfo(c.name);
      }
    } catch (e) {
      error = e instanceof Error ? e.message : 'Fetch failed';
    } finally {
      loading = false;
    }
  }

  async function fetchDeviceInfo(name: string) {
    try {
      const res = await fetch(`/clients/${encodeURIComponent(name)}`);
      if (res.ok) {
        const data = await res.json();
        const client = clients.find(c => c.name === name);
        if (client) {
          client.mac = data.mac;
          client.display_name = data.display_name;
          client.last_seen = data.last_seen;
          client.battery_percent = data.battery_percent;
          client.battery_mv = data.battery_mv;
          if (data.color) {
            colors[data.mac] = data.color;
          }
          clients = [...clients];
        }
      }
    } catch {}
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

  async function requestSleep(name: string) {
    const client = clients.find(c => c.name === name);
    if (!client?.mac) return;
    sleepPending = { ...sleepPending, [name]: true };
    try {
      await fetch(`/clients/sleep/${client.mac}`, { method: 'POST' });
    } catch { /* ignore */ } finally {
      sleepPending = { ...sleepPending, [name]: false };
    }
  }

  async function deleteClient(name: string, connected: boolean) {
    const client = clients.find(c => c.name === name);
    if (!client?.mac) return;
    const label = connected ? `⚠ ${name} is still online. Remove anyway?` : `Remove ${name}?`;
    if (!confirm(label)) return;
    deletePending = { ...deletePending, [name]: true };
    try {
      const res = await fetch(`/clients/${client.mac}`, { method: 'DELETE' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      clients = clients.filter(c => c.name !== name);
      if (client.mac) {
        const { [client.mac]: _, ...rest } = colors;
        colors = rest;
      }
    } catch (e) {
      error = e instanceof Error ? e.message : 'Delete failed';
    } finally {
      deletePending = { ...deletePending, [name]: false };
    }
  }

  async function sleepAll() {
    if (!confirm(`Send sleep request to all ${online} online client${online === 1 ? '' : 's'}?`)) return;
    sleepAllPending = true;
    try {
      await Promise.allSettled(
        clients.filter(c => c.connected && c.mac).map(c => fetch(`/clients/sleep/${c.mac!}`, { method: 'POST' }))
      );
    } finally {
      sleepAllPending = false;
    }
  }

  async function triggerOTA(name: string) {
    const client = clients.find(c => c.name === name);
    if (!client?.mac) return;
    if (!confirm(`Trigger OTA re-flash on ${name}?`)) return;
    otaPending[name] = true;
    try {
      const res = await fetch(`/ota/${client.mac}`, { method: 'POST' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
    } catch (e) {
      console.error('OTA trigger failed:', e);
    } finally {
      otaPending[name] = false;
    }
  }

  async function triggerBlink(name: string) {
    const client = clients.find(c => c.name === name);
    if (!client?.mac) return;
    blinkPending[name] = true;
    try {
      const res = await fetch(`/clients/blink/${client.mac}`, { method: 'POST' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
    } catch (e) {
      console.error('Blink trigger failed:', e);
    } finally {
      blinkPending[name] = false;
    }
  }

  function startEditName(name: string, currentName?: string) {
    editingName = name;
    editingValue = currentName || '';
  }

  async function saveName(name: string) {
    if (editingName !== name) return;
    const client = clients.find(c => c.name === name);
    if (!client?.mac) return;
    renamePending = { ...renamePending, [name]: true };
    try {
      const res = await fetch(`/clients/name/${client.mac}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: editingValue || null }),
      });
      if (res.ok) {
        client.display_name = editingValue || null;
        // Update the name to reflect the new display name
        if (editingValue) {
          client.name = editingValue;
        }
        clients = [...clients];
        editingName = null;
      }
    } catch (e) {
      console.error('Rename failed:', e);
    } finally {
      renamePending = { ...renamePending, [name]: false };
    }
  }

  function cancelEditName() {
    editingName = null;
    editingValue = '';
  }

  function putAllColor(color: RGBWColor) {
    currentColor = color;
    for (const c of clients) {
      if (c.mac) {
        colors[c.mac] = color;
        putColor(c.mac, color);
      }
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
        {@const color = client.mac ? colors[client.mac] : undefined}
        <li class:online={client.connected} class:offline={!client.connected}>
          <div class="dot"></div>
          <div class="left-group">
            {#if editingName === client.name}
              <input
                type="text"
                class="name-input"
                bind:value={editingValue}
                onkeydown={(e) => {
                  if (e.key === 'Enter') saveName(client.name);
                  if (e.key === 'Escape') cancelEditName();
                }}
              />
              <button
                class="name-save-btn"
                onclick={() => saveName(client.name)}
                disabled={renamePending[client.name]}
              >
                {renamePending[client.name] ? '…' : '✓'}
              </button>
              <button class="name-cancel-btn" onclick={cancelEditName}>✕</button>
            {:else}
              <button
                class="name-btn"
                title="Click to rename"
                onclick={() => startEditName(client.name, client.display_name || '')}
              >
                <div class="name-display">
                  {client.display_name || client.name}
                </div>
                {#if client.display_name}
                  <div class="name-mac">{client.mac || client.name}</div>
                {/if}
              </button>
            {/if}
            <span class="battery" title={client.battery_percent != null ? `${client.battery_mv}mV` : 'Battery data unavailable'}>
              {client.battery_percent != null ? `${client.battery_percent}%` : '—'}
            </span>
            <span class="time">{client.last_seen ? timeAgo(client.last_seen) : 'unknown'}</span>
          </div>
          <button
            class="delete-btn"
            onclick={() => deleteClient(client.name, client.connected)}
            disabled={deletePending[client.name]}
            aria-label="Remove {client.name}"
            title="Remove client"
          >{deletePending[client.name] ? '…' : '✕'}</button>
          <button
            class="ota-btn"
            onclick={() => triggerOTA(client.name)}
            disabled={otaPending[client.name]}
            aria-label="OTA {client.name}"
            title="Trigger OTA re-flash"
          >{otaPending[client.name] ? '…' : 'OTA'}</button>
          <button
            class="blink-btn"
            onclick={() => triggerBlink(client.name)}
            disabled={blinkPending[client.name] || !client.connected}
            aria-label="Blink {client.name}"
            title="Flash green for 5 seconds"
          >{blinkPending[client.name] ? '…' : '💡'}</button>
          <button
            class="sleep-btn"
            onclick={() => requestSleep(client.name)}
            disabled={sleepPending[client.name] || !client.connected}
            aria-label="Sleep {client.name}"
            title="Send sleep request"
          >{sleepPending[client.name] ? '…' : 'Sleep'}</button>
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

  .left-group {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex: 1;
  }

  .name-btn {
    display: flex;
    flex-direction: column;
    flex-shrink: 0;
    background: none;
    border: none;
    color: inherit;
    cursor: pointer;
    padding: 0;
    text-align: left;
    gap: 0;
  }

  .name-display {
    font-family: monospace;
    font-size: 0.9rem;
  }

  .name-mac {
    font-family: monospace;
    font-size: 0.65rem;
    color: #444;
  }

  .name-btn:hover .name-display {
    text-decoration: underline;
  }

  li.offline .name-btn { color: #666; }
  li.offline .name-mac { color: #333; }

  .name-input {
    font-family: monospace;
    font-size: 0.9rem;
    padding: 0.3rem 0.4rem;
    border: 1px solid #4caf82;
    border-radius: 3px;
    background: #1a1a1a;
    color: inherit;
    flex-shrink: 0;
  }

  .name-save-btn, .name-cancel-btn {
    padding: 0.2rem 0.4rem;
    font-size: 0.75rem;
    border: 1px solid #4caf82;
    border-radius: 3px;
    background: #1e2410;
    color: #4caf82;
    cursor: pointer;
    flex-shrink: 0;
  }

  .name-save-btn:hover:not(:disabled) {
    background: #2a3418;
  }

  .name-cancel-btn:hover {
    background: #2a1818;
    border-color: #8844aa;
    color: #cc88ff;
  }

  .time {
    font-size: 0.8rem;
    color: #555;
    white-space: nowrap;
    flex-shrink: 0;
  }

  .battery {
    font-size: 0.75rem;
    color: #4caf82;
    white-space: nowrap;
    padding: 0.2rem 0.5rem;
    background: rgba(76, 175, 130, 0.1);
    border-radius: 3px;
    flex-shrink: 0;
  }

  /* ── sleep button ── */
  /* ── OTA button ── */
  .ota-btn {
    padding: 0.2rem 0.55rem;
    font-size: 0.75rem;
    border: 1px solid #3a3a2a;
    border-radius: 4px;
    background: #1e2410;
    color: #ccaa66;
    cursor: pointer;
    flex-shrink: 0;
  }

  .ota-btn:hover:not(:disabled) {
    background: #2a3418;
    border-color: #aa8844;
    color: #eecc88;
  }

  .ota-btn:disabled { opacity: 0.35; cursor: default; }

  /* ── sleep button ── */
  .blink-btn {
    padding: 0.2rem 0.55rem;
    font-size: 0.75rem;
    border: 1px solid #3a3a2a;
    border-radius: 4px;
    background: #1e2410;
    color: #88cc44;
    cursor: pointer;
    flex-shrink: 0;
  }

  .blink-btn:hover:not(:disabled) {
    background: #2a3418;
    border-color: #66aa44;
    color: #aaee66;
  }

  .blink-btn:disabled { opacity: 0.35; cursor: default; }

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
    padding: 0.2rem 0.55rem;
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
