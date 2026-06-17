<script lang="ts">
  import LogViewer from '$lib/components/LogViewer.svelte';
  import ClientList from '$lib/components/ClientList.svelte';
  import PresetManager from '$lib/components/PresetManager.svelte';
  import type { RGBWColor } from '$lib/components/ColorPicker.svelte';

  let presets = $state<any[]>([]);
  let presetManager = $state<any>(null);
  let currentColor = $state<RGBWColor>({ r: 0, g: 0, b: 0, w: 0 });
</script>

<main>
  <section class="presets-section">
    <PresetManager
      bind:this={presetManager}
      bind:presets
      currentColor={currentColor}
    />
  </section>

  <section>
    <ClientList bind:currentColor bind:presets />
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

  :global(.presets-section) {
    background: #0a1a1a;
    border: 1px solid #1a3a3a;
  }
</style>
