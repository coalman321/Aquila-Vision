#!/usr/bin/env bash
# Bazel credential helper for the workshop GitLab: supplies an access token to the private
# module registry (served by GitLab Pages) and to the package downloads it points at.
# .bazelrc registers it for robotics.pages.tuckers-workshop.xyz and gitlab.tuckers-workshop.xyz only.
#
# The token comes from $BAZEL_GITLAB_TOKEN (set in CI) or ~/.config/gitlab/token: a personal
# access token with the read_api scope. Without one, Pages redirects to the GitLab sign-in page
# and Bazel tries to parse that HTML as a MODULE.bazel ("invalid character: '!'"). Bazel only
# logs a helper failure as a warning and carries on, so the message below shows up as
# "WARNING: Error retrieving auth headers" just before those parse errors.
#
# Protocol: https://github.com/EngFlow/credential-helper-spec. Bazel runs "<helper> get" with the
# request JSON ({"uri": ...}) on stdin and reads {"headers": {...}} from stdout.
set -euo pipefail

if [[ "${1:-}" != "get" ]]; then
  echo "usage: $0 get" >&2
  exit 1
fi
cat > /dev/null

token="${BAZEL_GITLAB_TOKEN:-}"
token_file="${XDG_CONFIG_HOME:-$HOME/.config}/gitlab/token"
if [[ -z "$token" && -r "$token_file" ]]; then
  token="$(tr -d '[:space:]' < "$token_file")"
fi

if [[ -z "$token" ]]; then
  echo "gitlab_cred_helper: no GitLab token. Create a personal access token with the read_api" \
    "scope and save it to $token_file (see README.md, \"Private module registry\")." >&2
  exit 1
fi
# GitLab tokens are [A-Za-z0-9._-]; anything else would break the JSON below.
if [[ ! "$token" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "gitlab_cred_helper: the GitLab token contains unexpected characters" >&2
  exit 1
fi

printf '{"headers":{"Authorization":["Bearer %s"]}}\n' "$token"
