<script lang="ts">
	import favicon from '$lib/assets/favicon.svg';
	import logoFull from '$lib/assets/logo-full.svg';
	import { page } from '$app/stores';
	import { base } from '$app/paths';

	let { children } = $props();

	const tabs = [
		{ name: 'Pipelines', href: `${base}/`, routeId: '/' },
		{ name: 'Sources', href: `${base}/sources`, routeId: '/sources' },
		{ name: 'Field Layout', href: `${base}/field-layout`, routeId: '/field-layout' },
		{ name: 'Calibration', href: `${base}/calibration`, routeId: '/calibration' },
		{ name: 'Models', href: `${base}/models`, routeId: '/models' },
		{ name: 'Logger', href: `${base}/logger`, routeId: '/logger' },
		{ name: 'Status', href: `${base}/status`, routeId: '/status' },
	];

	function isActive(routeId: string): boolean {
		return $page.route.id === routeId;
	}
</script>

<svelte:head>
	<title>Aquila</title>
	<link rel="icon" href={favicon} />
</svelte:head>

<div class="tab-layout">
	<nav class="tab-nav" aria-label="Main navigation">
		<div class="tab-logo">
			<img src={logoFull} alt="Aquila" />
		</div>
		<div class="tab-top">
			<ul>
				{#each tabs as tab}
					<li>
						<a
							href={tab.href}
							class:active={isActive(tab.routeId)}
							aria-current={isActive(tab.routeId) ? 'page' : undefined}
						>{tab.name}</a>
					</li>
				{/each}
			</ul>
		</div>
	</nav>
	<main class="tab-content">
		{@render children()}
	</main>
</div>

<style>
	:global(:root) {
		/* svelte_framework component theme overrides */
		--fw-bg: #1a1a1a;
		--fw-bg-subtle: #2a2a2a;
		--fw-bg-hover: #2a2a2a;
		--fw-border: #333333;
		--fw-border-subtle: #2a2a2a;
		--fw-text: #f9fafb;
		--fw-text-muted: #9ca3af;
		--fw-primary: #dc2626;
		--fw-primary-hover: #b91c1c;
		--fw-secondary: #374151;
		--fw-secondary-hover: #4b5563;
		--fw-badge-bg: #3b0a0a;
		--fw-badge-text: #f87171;
		--fw-link: #f87171;
		--fw-danger: #ef4444;
		--fw-progress: #10b981;
	}

	:global(html),
	:global(body) {
		margin: 0;
		padding: 0;
		height: 100%;
		overflow: hidden;
		background: #0d0d0d;
		color: #f9fafb;
	}

	.tab-layout {
		display: flex;
		width: 100%;
		height: 100vh;
		overflow: hidden;
	}

	.tab-nav {
		flex: 0 0 160px;
		padding: 12px;
		position: relative;
		box-sizing: border-box;
		height: 100vh;
		overflow-y: auto;
		overflow-x: hidden;
		background: #111111;
		border-right: 1px solid #2a2a2a;
	}

	.tab-nav ul {
		list-style: none;
		padding: 0;
		margin: 0;
		display: flex;
		flex-direction: column;
		gap: 8px;
	}

	.tab-nav a {
		display: block;
		width: 100%;
		text-align: left;
		padding: 10px 14px;
		background: transparent;
		border: 0;
		border-left: 4px solid transparent;
		cursor: pointer;
		text-decoration: none;
		color: #9ca3af;
		box-sizing: border-box;
		font-weight: 500;
		letter-spacing: 0.02em;
		border-radius: 4px;
		transition: background 0.15s, color 0.15s;
	}

	.tab-nav a.active {
		background: #1a1a1a;
		color: #f9fafb;
		border-left-color: #dc2626;
		padding-left: 12px;
	}

	.tab-nav a:hover {
		background: #1a1a1a;
		color: #f9fafb;
	}

	.tab-nav a:focus {
		outline: 3px solid rgba(220, 38, 38, 0.6);
		outline-offset: 2px;
	}

	.tab-nav .tab-top {
		margin-bottom: auto;
	}

	.tab-logo {
		margin-bottom: 12px;
	}

	.tab-logo img {
		display: block;
		width: 100%;
		height: auto;
	}

	.tab-content {
		flex: 1;
		padding: 16px;
		overflow-y: auto;
		overflow-x: hidden;
		box-sizing: border-box;
		height: 100vh;
		background: #0d0d0d;
	}

	@media (max-width: 640px) {
		.tab-layout {
			flex-direction: column;
		}

		.tab-nav {
			flex: 0 0 auto;
			height: auto;
			max-height: 60px;
			overflow-y: hidden;
			overflow-x: auto;
		}

		.tab-logo {
			display: none;
		}

		.tab-nav ul {
			flex-direction: row;
			gap: 6px;
			overflow-x: auto;
		}

		.tab-content {
			height: calc(100vh - 84px);
		}
	}
</style>
