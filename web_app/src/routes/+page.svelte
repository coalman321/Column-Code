<script lang="ts">
  import { onMount } from 'svelte';
  import LogViewer from '$lib/components/LogViewer.svelte';
  import ClientList from '$lib/components/ClientList.svelte';
  import PresetManager from '$lib/components/PresetManager.svelte';
  import type { RGBWColor } from '$lib/components/ColorPicker.svelte';

  let presets = $state<any[]>([]);
  let presetManager = $state<any>(null);
  let clientList = $state<any>(null);
  let currentColor = $state<RGBWColor>({ r: 0, g: 0, b: 0, w: 0 });

  function applyPresetToAll(preset: any) {
    const color = { r: preset.r, g: preset.g, b: preset.b, w: preset.w, flicker: preset.flicker };
    currentColor = color;
    clientList?.applyPresetToAll(color);
  }

  async function fetchGlobalColor() {
    try {
      const res = await fetch('/colors/global/current');
      if (res.ok) {
        currentColor = await res.json();
      }
    } catch (e) {
      console.error('Failed to fetch global color:', e);
    }
  }

  async function saveGlobalColor(color: RGBWColor) {
    try {
      await fetch('/colors/global/current', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(color),
      });
    } catch (e) {
      console.error('Failed to save global color:', e);
    }
  }

  onMount(() => {
    fetchGlobalColor();
  });

  $effect(() => {
    saveGlobalColor(currentColor);
  });
</script>

<main>
  <section class="presets-section">
    <PresetManager
      bind:this={presetManager}
      bind:presets
      {currentColor}
      onPresetSelected={applyPresetToAll}
    />
  </section>

  <section>
    <ClientList bind:this={clientList} bind:currentColor bind:presets />
  </section>

  <section>
    <LogViewer />
  </section>
</main>

<style>
  main {
    padding: 1.5rem;
    max-width: 1200px;
    margin: 0 auto;
    display: flex;
    flex-direction: column;
    gap: 2rem;
  }

  section {
    background: #111;
    border: 1px solid #222;
    border-radius: 8px;
    padding: 1.25rem 1.5rem;
  }

  section.presets-section {
    background: #0a1a1a;
    border: 2px solid #2a5a5a;
    padding-top: 1rem;
  }
</style>
