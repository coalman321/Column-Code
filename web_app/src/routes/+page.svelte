<script lang="ts">
  import LogViewer from '$lib/components/LogViewer.svelte';
  import ClientList from '$lib/components/ClientList.svelte';
</script>

<main>
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

  section {
    background: #111;
    border: 1px solid #222;
    border-radius: 8px;
    padding: 1.25rem 1.5rem;
  }
</style>
