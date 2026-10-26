# Foxglove extensions

Custom [Foxglove Studio extensions](https://docs.foxglove.dev/docs/visualization/extensions/introduction)
(panels, message converters, ...) for this workspace. Each subdirectory is one extension: an npm
project built with `foxglove-extension`, **outside Bazel**. Studio reaches the robot through the
`foxglove_bridge` that the robot app launches (`ws://<host>:8765`), so an extension only needs the
topics and services the bridge exposes.

| Extension | What it does |
|-----------|--------------|
| [basin](basin/) | Panels for the basin runtime: **Launch Control** (monitor and control the processes `launch_core` runs), **Fault Monitor** (live faults from `/state/fault_report`), the TUIs' jobs in Studio, and **Continuous Logger** (liveness, snapshots and events of the logger) |

## Build and install

The dev container ships Node.js and npm (the version is `NODE_VERSION` in the
[Dockerfile](../.devcontainer/Dockerfile)); elsewhere, install Node 22.

```bash
cd foxglove/<extension>
npm ci                  # or `npm install` when changing dependencies
npm run package         # writes <publisher>.<name>-<version>.foxe; needs a README.md and CHANGELOG.md
npm run local-install   # build and copy into ~/.foxglove-studio/extensions
npm run typecheck       # tsc only
```

`local-install` puts the extension in the home directory of whoever runs it, so **in the dev
container it never reaches a Studio desktop app running on your host**. There, use `package` and
open the `.foxe` (the workspace is bind-mounted, so it is already on the host): drag it into
Studio, or use Extensions > Install local extension. Run `local-install` from a host shell instead
if you'd rather have it reinstalled in one step. Restart Studio after installing to pick up changes.

## Adding an extension

1. Scaffold it here: `cd foxglove && npm init foxglove-extension@latest <name>`.
2. Add `foxglove/<name>/node_modules` to [.bazelignore](../.bazelignore), so `bazel build //...`
   doesn't crawl it looking for BUILD files.
3. Add a row to the table above.

Related panels belong in one extension, not one each: a new panel for the runtime goes in
[basin](basin/) (see its README, "Layout"). Start a new extension only for a different audience or
a different set of dependencies.

`node_modules/`, `dist/` and `*.foxe` are already ignored by [.gitignore](.gitignore).
