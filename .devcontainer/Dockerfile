# Development environment for the Aquila Vision monorepo.
#
# Bazel's LLVM/Rust/Python toolchains are hermetic, so the image only needs
# bazelisk plus the host tools the README lists (git, python3, zip/unzip),
# along with the editor helpers referenced by .vscode/settings.json. Node.js
# is the exception: it is for the non-Bazel Foxglove extensions in foxglove/.
#
# "ubuntu:26" is not a published tag; 26.04 is the LTS release (resolute).
FROM ubuntu:26.04

# The account below is only a placeholder. devcontainer.json sets
# "updateRemoteUserUID", so at build time the devcontainer tooling rewrites
# this user's UID/GID to match the host user's, which is what keeps files
# created in the bind-mounted workspace owned by the right person on the host.
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000

RUN export DEBIAN_FRONTEND=noninteractive \
    && apt-get update -qq \
    && apt-get install -y -qq --no-install-recommends \
        ca-certificates \
        curl \
        git \
        openssh-client \
        python3 \
        sudo \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

# dpkg's arch names (amd64/arm64) match the bazelisk and buildifier release
# asset names, so the same image definition works on either architecture.
RUN arch="$(dpkg --print-architecture)" \
    && curl -fsSL -o /usr/local/bin/bazel \
        "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${arch}" \
    && curl -fsSL -o /usr/local/bin/buildifier \
        "https://github.com/bazelbuild/buildtools/releases/latest/download/buildifier-linux-${arch}" \
    && chmod +x /usr/local/bin/bazel /usr/local/bin/buildifier

# Node.js (LTS) and npm, for the Foxglove Studio extensions under foxglove/. They
# are plain npm projects built outside Bazel. Node's release tarballs use x64/arm64
# where dpkg says amd64/arm64, and the .tar.gz form avoids needing xz-utils. The
# tarball is checked against the release's SHASUMS256.txt before it is unpacked.
ARG NODE_VERSION=22.23.2
RUN case "$(dpkg --print-architecture)" in \
        amd64) arch=x64 ;; \
        arm64) arch=arm64 ;; \
        *) echo "unsupported architecture for Node.js" >&2; exit 1 ;; \
    esac \
    && tarball="node-v${NODE_VERSION}-linux-${arch}.tar.gz" \
    && cd /tmp \
    && curl -fsSLO "https://nodejs.org/dist/v${NODE_VERSION}/${tarball}" \
    && curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt" \
        | grep " ${tarball}\$" | sha256sum -c - \
    && tar -xzf "${tarball}" -C /usr/local --strip-components=1 --no-same-owner \
        --exclude=CHANGELOG.md --exclude=README.md --exclude=LICENSE \
    && rm "${tarball}" \
    && node --version && npm --version

# Ubuntu images since 23.10 ship an "ubuntu" user at UID/GID 1000. Drop it so
# that UID doesn't collide with the user created here (or with the host user's
# UID when updateRemoteUserUID remaps it).
RUN if id -u ubuntu >/dev/null 2>&1; then userdel --remove ubuntu; fi \
    && if getent group ubuntu >/dev/null; then groupdel ubuntu; fi \
    && groupadd --gid "${USER_GID}" "${USERNAME}" \
    && useradd --uid "${USER_UID}" --gid "${USER_GID}" --create-home --shell /bin/bash "${USERNAME}" \
    && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}" \
    && chmod 0440 "/etc/sudoers.d/${USERNAME}"

USER ${USERNAME}

# Pre-create the mount points for the named volumes in devcontainer.json (the
# Bazel output root, Claude Code's config dir, the shell history dir and the
# GitLab token dir) so they're initialised with the user's ownership rather
# than root's. The token dir is private to the user, like the token in it.
RUN mkdir -p "${HOME}/.cache/bazel" "${HOME}/.claude" "${HOME}/.shell-history" \
    && mkdir -p -m 700 "${HOME}/.config/gitlab"

# Persist bash history in the shell-history volume. "history -a" appends each
# command as it runs, so history isn't lost when a terminal is killed rather
# than exited cleanly (which is how VS Code usually closes them).
RUN printf '%s\n' \
        '' \
        '# Persist shell history across container rebuilds.' \
        'export HISTFILE="$HOME/.shell-history/.bash_history"' \
        'export PROMPT_COMMAND="history -a${PROMPT_COMMAND:+; $PROMPT_COMMAND}"' \
        >> "${HOME}/.bashrc"

WORKDIR /home/${USERNAME}
