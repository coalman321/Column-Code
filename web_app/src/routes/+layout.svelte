<script lang="ts">
	import favicon from '$lib/assets/favicon.svg';
	import '../app.css';
	import { onMount } from 'svelte';

	let { children } = $props();

	let connected = $state<boolean | null>(null);

	async function checkHealth() {
		try {
			const res = await fetch('/health');
			connected = res.ok;
		} catch {
			connected = false;
		}
	}

	onMount(() => {
		checkHealth();
		const id = setInterval(checkHealth, 5000);
		return () => clearInterval(id);
	});
</script>

<svelte:head>
	<link rel="icon" href={favicon} />
</svelte:head>

<div class="status-bar">
	<span class="indicator" class:ok={connected === true} class:err={connected === false}></span>
	<span class="label">
		{#if connected === null}Connecting…{:else if connected}Backend connected{:else}Backend unreachable{/if}
	</span>
</div>

{@render children()}

<style>
	.status-bar {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		padding: 0.35rem 1rem;
		background: #0a0a0a;
		border-bottom: 1px solid #1e1e1e;
		font-size: 0.75rem;
		color: #666;
	}

	.indicator {
		width: 7px;
		height: 7px;
		border-radius: 50%;
		background: #444;
		flex-shrink: 0;
	}

	.indicator.ok  { background: #4caf82; box-shadow: 0 0 5px #4caf8266; }
	.indicator.err { background: #e05555; box-shadow: 0 0 5px #e0555566; }

	.label { color: #888; }
</style>
