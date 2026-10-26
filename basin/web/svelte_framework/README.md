# svelte_framework

Shared Svelte 5 components for the web UIs (`CameraViewer`, `CameraViewerBase`, `FileUploader`, `LaunchManager`)
plus the API interfaces in `apiInterfaces.ts`. `src/lib/index.ts` is the public entry.

This is **plain source, not an npm package**: there is no `package.json`, and apps don't list it as a dependency.
Each app compiles it in through a SvelteKit alias and imports from `$framework`:

```svelte
<script lang="ts">
  import { CameraViewer, FileUploader } from '$framework';
</script>
```

Because the app's toolchain builds these files, they can only import what every consuming app provides,
which today means `svelte` alone. `@sveltejs/vite-plugin-svelte` dedupes `svelte`, so these files use the app's copy.
Type errors in here show up in each app's `pnpm run check`.

`//basin/web/svelte_framework:srcs` is the Bazel target an app's Vite build lists in its `srcs`.

## Adding an app

Use [camera_app/camera-app](../../../camera_app/camera-app/) as the template (e.g. for `myapp/myapp-app`):

1. Copy its `package.json`, `pnpm-workspace.yaml`, `svelte.config.js`, `vite.config.ts`, `tsconfig.json`,
   `eslint.config.js`, `.gitignore` and `.npmrc`, and rename the package.
2. In `svelte.config.js`, point `kit.alias.$framework` at this directory's `src/lib`, relative to the app
   (`'../../basin/web/svelte_framework/src/lib'` for an app two levels deep). In `vite.config.ts`, add the same
   directory to `server.fs.allow`, or `vite dev` refuses to serve it.
3. Run `pnpm install` in the app directory. That writes the app's own `pnpm-lock.yaml`; there is no repo-wide workspace.
4. Add the app's `node_modules` to the repo-root `.bazelignore`.
5. In `MODULE.bazel`, add a `npm.npm_translate_lock(name = "npm_<app>", npmrc = "//<pkg>:.npmrc",
   pnpm_lock = "//<pkg>:pnpm-lock.yaml", verify_node_modules_ignored = "//:.bazelignore")` and the repo name to `use_repo(npm, ...)`.
6. Copy camera-app's `BUILD.bazel`, loading from `@npm_<app>//:defs.bzl` and `@npm_<app>//<pkg>:vite/package_json.bzl`,
   and keep `//basin/web/svelte_framework:srcs` in the `:build` srcs.
