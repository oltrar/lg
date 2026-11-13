#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
lgSourceFingerprint() {
  local root=$1
  (
    cd -- "$root" || exit 1
    find overlay -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
    sha256sum build.sh versions.env
  ) | sha256sum | cut -d ' ' -f 1
}
lgBuildValue() {
  local root=$1 key=$2
  [[ -f $root/_build/build-info.txt ]] || return 0
  sed -n "s/^${key}=//p" "$root/_build/build-info.txt"
}
lgRecordBuild() {
  local root=$1 binary=$2 upstream=$3 temporary commit branch
  commit=$(git -C "$root" rev-parse --verify HEAD 2>/dev/null || printf 'archive')
  branch=$(git -C "$root" symbolic-ref --short -q HEAD 2>/dev/null || printf 'archive-or-detached')
  temporary=$(mktemp "$root/_build/build-info.XXXXXX")
  {
    printf 'project_commit=%s\nproject_branch=%s\nupstream_commit=%s\n' "$commit" "$branch" "$upstream"
    printf 'built_utc=%s\n' "$(date -u +%FT%TZ)"
    printf 'source_sha256=%s\n' "$(lgSourceFingerprint "$root")"
    printf 'binary_sha256=%s\n' "$(sha256sum < "$binary" | cut -d ' ' -f 1)"
  } > "$temporary"
  mv -f -- "$temporary" "$root/_build/build-info.txt"
}
