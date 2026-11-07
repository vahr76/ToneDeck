#!/usr/bin/env bash
# Downloads the exact dependency versions ToneDeck was built and tested with.
# Works on Linux, macOS and Windows (Git Bash).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p libs

fetch () {  # fetch <dir> <url> <commit>
  local dir="libs/$1" url="$2" commit="$3"
  if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ]; then
    echo "ok   $1"; return
  fi
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" remote add origin "$url"
  git -C "$dir" fetch -q --depth 1 origin "$commit"
  git -C "$dir" checkout -q FETCH_HEAD
  echo "got  $1 @ ${commit:0:10}"
}

fetch JUCE                 https://github.com/juce-framework/JUCE.git                51d11a2be6d5c97ccf12b4e5e827006e19f0555a  # 8.0.4
fetch NeuralAmpModelerCore https://github.com/sdatkinson/NeuralAmpModelerCore.git    0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842  # 0.6.0 (A2 / slimmable)
fetch eigen                https://github.com/eigen-mirror/eigen.git                 bc3b39870ecb690a623a3f49149a358b95c5781d  # commit pinned by NAM core
fetch RTNeural             https://github.com/jatinchowdhury18/RTNeural.git          95c3c0f987a6fe903e7eec71e797405dbed7caf7
fetch clap-juce-extensions https://github.com/free-audio/clap-juce-extensions.git    6bde108329ff9fc24404b79b8b397261a75cf6a8
git -C libs/clap-juce-extensions submodule update -q --init --recursive --depth 1
echo "Dependencies ready."
