<script lang="ts">
  import { onMount } from 'svelte';
  import type { Snippet } from 'svelte';
  import type { RGBWColor } from './ColorPicker.svelte';

  interface Preset {
    id: number;
    name: string;
    r: number;
    g: number;
    b: number;
    w: number;
  }

  interface Props {
    presets?: Preset[];
    currentColor?: RGBWColor;
    onPresetSelected?: (preset: Preset) => void;
    onPresetsChanged?: () => void;
    children?: Snippet;
  }

  let { presets = $bindable([]), currentColor, onPresetSelected, onPresetsChanged, children }: Props = $props();

  let loading = $state(false);
  let error = $state<string | null>(null);
  let showForm = $state(false);
  let presetName = $state('');
  let deleteConfirm = $state<number | null>(null);

  async function fetchPresets() {
    loading = true;
    error = null;
    try {
      const res = await fetch('/presets');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      presets = await res.json();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to fetch presets';
    } finally {
      loading = false;
    }
  }

  export async function saveCurrentPreset(color: RGBWColor) {
    const name = presetName.trim();
    if (!name) {
      error = 'Preset name cannot be empty';
      return false;
    }
    loading = true;
    error = null;
    try {
      const res = await fetch('/presets', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, r: color.r, g: color.g, b: color.b, w: color.w }),
      });
      if (!res.ok) {
        const data = await res.json();
        throw new Error(data.detail || `HTTP ${res.status}`);
      }
      presetName = '';
      showForm = false;
      await fetchPresets();
      onPresetsChanged?.();
      return true;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to save preset';
      return false;
    } finally {
      loading = false;
    }
  }

  async function deletePreset(id: number) {
    if (deleteConfirm !== id) {
      deleteConfirm = id;
      return;
    }
    loading = true;
    error = null;
    try {
      const res = await fetch(`/presets/${id}`, { method: 'DELETE' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      await fetchPresets();
      deleteConfirm = null;
      onPresetsChanged?.();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to delete preset';
    } finally {
      loading = false;
    }
  }

  function colorBg(p: Preset): string {
    const a = p.w / 255;
    const blend = (ch: number) => Math.round(ch + (255 - ch) * a);
    return `rgb(${blend(p.r)}, ${blend(p.g)}, ${blend(p.b)})`;
  }

  onMount(() => {
    fetchPresets();
  });
</script>

<div class="preset-manager">
  <div class="header">
    <h3>Color Presets</h3>
    <button
      class="add-btn"
      onclick={() => (showForm = !showForm)}
      disabled={loading}
      aria-label="Toggle preset form"
    >
      {showForm ? '−' : '+'}
    </button>
  </div>

  {#if error}
    <p class="error">{error}</p>
  {/if}

  {#if showForm}
    <div class="form">
      <input
        type="text"
        placeholder="Preset name"
        bind:value={presetName}
        maxlength="50"
        disabled={loading}
        aria-label="Preset name"
      />
      {#if currentColor}
        <button
          class="save-btn"
          onclick={async () => {
            await saveCurrentPreset(currentColor);
          }}
          disabled={loading}
        >
          Save Color
        </button>
      {/if}
      {#if children}
        {@render children()}
      {/if}
    </div>
  {/if}

  {#if presets.length === 0 && !loading && !showForm}
    <p class="empty">No presets saved yet</p>
  {:else}
    <ul class="preset-list">
      {#each presets as preset}
        <li class="preset-item">
          <button
            class="swatch"
            style={`--bg: ${colorBg(preset)}`}
            onclick={() => onPresetSelected?.(preset)}
            aria-label={preset.name}
            title="Apply preset"
          ></button>
          <span class="name">{preset.name}</span>
          <button
            class="delete-btn"
            onclick={() => deletePreset(preset.id)}
            disabled={loading}
            aria-label="Delete {preset.name}"
          >
            {deleteConfirm === preset.id ? 'Confirm?' : '✕'}
          </button>
        </li>
      {/each}
    </ul>
  {/if}
</div>

<style>
  .preset-manager {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
  }

  .header h3 {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 600;
  }

  .add-btn {
    width: 24px;
    height: 24px;
    padding: 0;
    border: 1px solid #444;
    border-radius: 4px;
    background: #2a2a2a;
    color: #aaa;
    cursor: pointer;
    font-size: 0.9rem;
    flex-shrink: 0;
  }

  .add-btn:hover:not(:disabled) {
    background: #3a3a3a;
    color: #ccc;
  }

  .add-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }

  .form {
    display: flex;
    gap: 0.5rem;
  }

  .form input {
    flex: 1;
    padding: 0.35rem 0.5rem;
    border: 1px solid #2a2a2a;
    border-radius: 4px;
    background: #111;
    color: #fff;
    font-size: 0.85rem;
  }

  .form input:focus {
    outline: none;
    border-color: #444;
  }

  .form input:disabled {
    opacity: 0.5;
  }

  .save-btn {
    padding: 0.35rem 0.8rem;
    border: 1px solid #2a5a3a;
    border-radius: 4px;
    background: #0d2d1a;
    color: #4caf82;
    cursor: pointer;
    font-size: 0.85rem;
    white-space: nowrap;
  }

  .save-btn:hover:not(:disabled) {
    background: #1a3a28;
    border-color: #4caf82;
  }

  .save-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }

  .preset-list {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }

  .preset-item {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.5rem;
    border-radius: 4px;
    background: #0a0a0a;
    border: 1px solid #1a1a1a;
  }

  .preset-item:hover {
    background: #151515;
  }

  .swatch {
    width: 20px;
    height: 20px;
    padding: 0;
    border-radius: 3px;
    border: 1px solid #333;
    background: var(--bg, #1a1a1a);
    cursor: pointer;
    flex-shrink: 0;
  }

  .swatch:hover {
    border-color: #555;
  }

  .name {
    flex: 1;
    font-size: 0.85rem;
    color: #ccc;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .delete-btn {
    padding: 0.15rem 0.35rem;
    border: 1px solid #3a2a2a;
    border-radius: 3px;
    background: transparent;
    color: #999;
    cursor: pointer;
    font-size: 0.75rem;
    flex-shrink: 0;
  }

  .delete-btn:hover:not(:disabled) {
    background: #2a1a1a;
    color: #ee8888;
    border-color: #aa4444;
  }

  .delete-btn:disabled {
    opacity: 0.5;
  }

  .empty {
    font-size: 0.8rem;
    color: #555;
    margin: 0;
    padding: 0.5rem;
  }

  .error {
    font-size: 0.8rem;
    color: #f55;
    margin: 0;
    padding: 0.5rem;
  }
</style>
