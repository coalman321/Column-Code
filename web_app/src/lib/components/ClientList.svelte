<script lang="ts">
  import { onMount } from 'svelte';

  interface Client {
    mac: string;
    last_seen: string;
    connected: boolean;
  }

  let clients    = $state<Client[]>([]);
  let loading    = $state(false);
  let error      = $state<string | null>(null);
  let autoRefresh = $state(true);

  async function fetchClients() {
    loading = true;
    error = null;
    try {
      const res = await fetch('/clients/');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      clients = await res.json();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Fetch failed';
    } finally {
      loading = false;
    }
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
    if (diff < 60)  return `${diff}s ago`;
    if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
    return `${Math.floor(diff / 3600)}h ago`;
  }
</script>

<div class="client-list">
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
        <li class:online={client.connected} class:offline={!client.connected}>
          <div class="dot"></div>
          <span class="mac">{client.mac}</span>
          <span class="time">{timeAgo(client.last_seen)}</span>
        </li>
      {/each}
    </ul>
  {/if}
</div>

<style>
  .client-list {
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
