#!/bin/bash
# Writes make/wasm32-asyncify-only.txt, the functions the wasm32 build instruments with Asyncify (CMakeLists_app.txt).
# Instrumenting everything makes Safari's optimizing wasm compiler run out of memory (WebKit bug 304810), so the list
# has to follow the code: rerun this when code on a path to a suspend changes. Run it from the repository root with a
# configured and built wasm32 build directory, with emsdk's environment set:
#   make/wasm32-asyncify-only.sh build/wasm-wasm32
# It relinks once, into a temporary folder, with -sASYNCIFY_IGNORE_INDIRECT -sASYNCIFY_ADVISE instead of the list, keeps
# the functions that report says can change the state (those reaching a suspend through direct calls), and adds
# make/wasm32-asyncify-extra.txt (those reaching it through indirect calls). SDL_SYS_DelayNS is left out of the report:
# main.cpp sets SDL_HINT_EMSCRIPTEN_ASYNCIFY to 0, so SDL's waits never suspend, and most of SDL's callers (the in-game
# UI among them) don't need instrumenting.
set -euo pipefail
build=$(cd "$1" && pwd)
make_dir=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
link=$(<"$build/CMakeFiles/vpinball.dir/link.txt")
link=${link//-sASYNCIFY_ONLY=@$make_dir\/wasm32-asyncify-only.txt/}
link=${link/ -o vpinball.html / -o $tmp\/vpinball.html }
(cd "$build" && eval "$link -sASYNCIFY_IGNORE_INDIRECT=1 -sASYNCIFY_REMOVE=SDL_SYS_DelayNS -sASYNCIFY_ADVISE=1") \
   > "$tmp/advise.log" 2>&1
{
   grep '^\[asyncify\] ' "$tmp/advise.log" | grep -v ' is an import that can change the state' \
      | sed -E 's/^\[asyncify\] //; s/ can change the state due to .*$//' | sort -u
   grep -v -E '^(#|$)' "$make_dir/wasm32-asyncify-extra.txt"
} > "$make_dir/wasm32-asyncify-only.txt"
echo "$(wc -l < "$make_dir/wasm32-asyncify-only.txt") functions in make/wasm32-asyncify-only.txt"
rm -r "$tmp"
