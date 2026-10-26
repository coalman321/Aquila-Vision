import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

export default defineConfig({
	plugins: [sveltekit()],
	server: {
		// The $framework alias (svelte.config.js) points outside this project.
		fs: { allow: ['../../basin/web/svelte_framework'] }
	}
});
