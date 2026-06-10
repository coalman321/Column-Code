<script lang="ts">
  import { onMount } from 'svelte';

  interface LogEntry {
    mac: string;
    level: string;
    tag: string;
    message: string;
    received_at: string;
  }

  const LEVELS = ['DEBUG', 'INFO', 'WARN', 'ERROR'];

  let logs       = $state<LogEntry[]>([]);
  let loading    = $state(false);
  let error      = $state<string | null>(null);
  let macFilter  = $state('');
  let levelFilter = $state('');
  let autoRefresh = $state(true);

  /* Level filtering is client-side; MAC filtering is server-side. */
  let filtered = $derived(
    (levelFilter ? logs.filter(l => l.level === levelFilter) : logs).toReversed()
  );

  /* Populate the MAC dropdown from logs already in memory. */
  let knownMacs = $derived([...new Set(logs.map(l => l.mac))].sort());

  async function fetchLogs() {
    loading = true;
    error = null;
    try {
      const params = new URLSearchParams({ limit: '500' });
      if (macFilter) params.set('mac', macFilter);
      const res = await fetch(`/logs/?${params}`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      logs = await res.json();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Fetch failed';
    } finally {
      loading = false;
    }
  }

  /* Re-fetch when MAC filter changes. */
  $effect(() => {
    macFilter; // track
    fetchLogs();
  });

  onMount(() => {
    const id = setInterval(() => { if (autoRefresh) fetchLogs(); }, 5000);
    return () => clearInterval(id);
  });

  function fmt(iso: string) {
    return new Date(iso).toLocaleString();
  }
</script>

<div class="log-viewer">
  <div class="toolbar">
    <h2>Device Logs</h2>

    <div class="filters">
      <!-- MAC filter -->
      <label>
        <span>MAC</span>
        <select bind:value={macFilter}>
          <option value="">All devices</option>
          {#each knownMacs as mac}
            <option value={mac}>{mac}</option>
          {/each}
        </select>
      </label>

      <!-- Level filter -->
      <label>
        <span>Level</span>
        <select bind:value={levelFilter}>
          <option value="">All levels</option>
          {#each LEVELS as lvl}
            <option value={lvl}>{lvl}</option>
          {/each}
        </select>
      </label>
    </div>

    <div class="actions">
      <label class="toggle">
        <input type="checkbox" bind:checked={autoRefresh} />
        Auto-refresh
      </label>
      <button onclick={() => fetchLogs()} disabled={loading}>
        {loading ? 'Loading…' : 'Refresh'}
      </button>
    </div>
  </div>

  {#if error}
    <p class="error">{error}</p>
  {/if}

  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>Time</th>
          <th>MAC</th>
          <th>Level</th>
          <th>Tag</th>
          <th>Message</th>
        </tr>
      </thead>
      <tbody>
        {#if filtered.length === 0}
          <tr>
            <td colspan="5" class="empty">
              {loading ? 'Loading…' : 'No logs found.'}
            </td>
          </tr>
        {/if}
        {#each filtered as entry}
          <tr>
            <td class="mono nowrap">{fmt(entry.received_at)}</td>
            <td class="mono nowrap">{entry.mac}</td>
            <td><span class="badge {entry.level.toLowerCase()}">{entry.level}</span></td>
            <td class="mono">{entry.tag}</td>
            <td class="message">{entry.message}</td>
          </tr>
        {/each}
      </tbody>
    </table>
  </div>

  <p class="count">{filtered.length} entr{filtered.length === 1 ? 'y' : 'ies'}</p>
</div>

<style>
  .log-viewer {
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
    gap: 1rem;
  }

  .toolbar h2 {
    margin: 0;
    font-size: 1.1rem;
    font-weight: 600;
    flex-shrink: 0;
  }

  .filters {
    display: flex;
    gap: 0.75rem;
    flex-wrap: wrap;
  }

  .filters label {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }

  .filters span {
    color: #888;
    font-size: 0.8rem;
  }

  .filters select {
    padding: 0.3rem 0.5rem;
    border: 1px solid #333;
    border-radius: 4px;
    background: #1a1a1a;
    color: inherit;
    font-size: 0.85rem;
  }

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

  /* ── table ── */
  .table-wrap {
    overflow-x: auto;
    border: 1px solid #2a2a2a;
    border-radius: 6px;
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
  }

  th {
    text-align: left;
    padding: 0.5rem 0.75rem;
    background: #1a1a1a;
    color: #aaa;
    font-weight: 500;
    border-bottom: 1px solid #2a2a2a;
    white-space: nowrap;
  }

  td {
    padding: 0.45rem 0.75rem;
    border-bottom: 1px solid #1e1e1e;
    vertical-align: top;
  }

  tr:last-child td { border-bottom: none; }
  tr:hover td { background: #161616; }

  .mono { font-family: monospace; }
  .nowrap { white-space: nowrap; }
  .message { word-break: break-word; max-width: 40ch; }

  .empty {
    text-align: center;
    padding: 2rem;
    color: #666;
  }

  /* ── level badges ── */
  .badge {
    display: inline-block;
    padding: 0.15rem 0.45rem;
    border-radius: 3px;
    font-size: 0.75rem;
    font-weight: 600;
    font-family: monospace;
    letter-spacing: 0.03em;
  }

  .badge.debug   { background: #2a2a2a; color: #888; }
  .badge.info    { background: #0d2d4a; color: #5ab4f5; }
  .badge.warn    { background: #3a2a00; color: #f0b840; }
  .badge.warning { background: #3a2a00; color: #f0b840; }
  .badge.error   { background: #3a0d0d; color: #f55; }

  /* ── footer ── */
  .error {
    color: #f55;
    font-size: 0.85rem;
    margin: 0;
  }

  .count {
    color: #555;
    font-size: 0.8rem;
    margin: 0;
    text-align: right;
  }
</style>
