<script lang="ts">
  import LogViewer from '$lib/components/LogViewer.svelte';
  import ColorPicker from '$lib/components/ColorPicker.svelte';
  import ClientList from '$lib/components/ClientList.svelte';
  import type { RGBWColor } from '$lib/components/ColorPicker.svelte';

  let color = $state<RGBWColor>({ r: 255, g: 0, b: 0, w: 0 });
</script>

<main>
  <section>
    <ColorPicker bind:value={color} />
    <p class="values">
      R {color.r} · G {color.g} · B {color.b} · W {color.w}
    </p>
  </section>

  <section>
    <ClientList />
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

  .values {
    margin: 0.5rem 0 0;
    font-family: monospace;
    font-size: 0.85rem;
    color: #888;
  }
</style>
