#!/bin/bash
# verify-environment.sh -- is this machine still able to build and trust the
# tools that talk to the mouse?
#
# WHY THIS EXISTS. Written 2026-09-05, before the owner upgraded macOS an earlier macOS
# to a newer macOS  . The worry was that a major OS jump would invalidate the
# project. It should not: the whole OS surface is three dylibs, two of them
# Apple's most ABI-stable, and nothing in the derivation, the captures, the
# vendor binaries or the mouse's firmware depends on the host OS at all.
#
# But "should not" is a prediction, and this project's rule is that predictions
# get written down and then checked. So: run it BEFORE the upgrade to record a
# baseline, run it AFTER, and diff. Anything that moved is the answer, and
# anything that did not is not worth worrying about.
#
#   Tools/verify-environment.sh --save baseline-pre-upgrade.txt
#   ...upgrade...
#   Tools/verify-environment.sh --save baseline-post-upgrade.txt
#   diff baseline-pre-upgrade.txt baseline-post-upgrade.txt
#
# It sends NOTHING to the device. The one device check is enumeration only,
# which is a read of what the OS already knows.
set -uo pipefail
cd "$(dirname "$0")/.."

# --save FILE: re-run ourselves with output teed to FILE. Doing it by re-exec
# means every line below is captured without threading a file handle through
# each one -- and, more to the point, without the "(saved to ...)" line being
# able to lie. The first version of this printed that line and wrote nothing.
if [ "${1:-}" = "--save" ]; then
  dest="${2:?--save needs a filename}"
  "$0" | tee "$dest"
  rc=${PIPESTATUS[0]}
  printf '\n(saved to %s)\n' "$dest"
  exit "$rc"
fi

fail=0
say() { printf '%s\n' "$*"; }
ck()  { if [ "$1" = 0 ]; then say "  ok    $2"; else say "  FAIL  $2"; fail=$((fail+1)); fi; }

say "=== host ==="
say "  os        $(sw_vers -productName) $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
say "  arch      $(uname -m)"
say "  clang     $(cc --version | head -1)"
say "  cmake     $(cmake --version | head -1)"
say "  hidapi    $(brew list --versions hidapi 2>/dev/null || echo 'NOT INSTALLED')"

say ""
say "=== build ==="
cmake -S . -B build >/dev/null 2>&1
out=$(cmake --build build -j8 2>&1); rc=$?
ck $rc "the tree builds"
if printf '%s' "$out" | grep -qE "warning:"; then
  say "  note  build emitted warnings:"
  printf '%s' "$out" | grep -E "warning:" | head -5 | sed 's/^/        /'
fi

say ""
say "=== the things that must not change, whatever the OS does ==="

# 1. The firmware image we would flash.
EXE="Endgame Gear OP1 8k v2 Firmware Updater 1.10.exe"
if [ -f "$EXE" ]; then
  got=$(python3 -c "
import sys,hashlib; sys.path.insert(0,'Tools/pe')
import fwfile
blob=open(sys.argv[1],'rb').read()
for rid,_l,off,size,_d in fwfile.fwfiles(sys.argv[1]):
    if rid==140: print(hashlib.sha256(blob[off:off+size]).hexdigest())
" "$EXE" 2>/dev/null)
  want=8148ebe9f8d2848abe483aee98df6e42bab341c6a17523bfef0f85f1f66754d0
  [ "$got" = "$want" ]; ck $? "FWFILE 140 sha256 is the pinned value"
  say "        $got"
else
  say "  skip  updater .exe not present (it is gitignored)"
fi

# 2. The approval token. This is the strongest invariant the project has: it is
#    the SHA-256 of Endgame's own captured traffic. If the OS could change it,
#    something is very wrong indeed.
if [ -f "$EXE" ] && [ -x ./build/egg-flash ]; then
  tok=$(./build/egg-flash flash "$EXE" --backup /nonexistent 2>&1 |
        grep -oE '\-\-confirm [0-9a-f]{8}' | head -1 | awk '{print $2}')
  [ "$tok" = "ecfc8f88" ]; ck $? "approval token is still ecfc8f88 (got '${tok:-none}')"
fi

say ""
say "=== tests ==="
out=$(ctest --test-dir build -E mutants 2>&1); rc=$?
ck $rc "ctest -E mutants passes"
printf '%s' "$out" | grep -E "tests passed|tests failed" | sed 's/^/        /'

say ""
say "=== device (enumeration only -- nothing is sent) ==="
if [ -x ./build/egg-config ]; then
  dev=$(./build/egg-config devices 2>&1)
  n=$(printf '%s' "$dev" | grep -c "^0x19")
  if [ "$n" -gt 0 ]; then
    say "  ok    $n vendor interface(s) visible"
    printf '%s' "$dev" | head -3 | sed 's/^/        /'
    say "        If this drops to 0 with the mouse plugged in, the likely cause"
    say "        is Input Monitoring permission, which a macOS upgrade can reset."
    say "        System Settings > Privacy & Security > Input Monitoring."
  else
    say "  note  no vendor interfaces visible. Either the mouse is unplugged,"
    say "        or macOS has not granted Input Monitoring to your terminal."
    say "        This is NOT a code failure and NOT a reason to reflash anything."
  fi
fi

say ""
if [ $fail -eq 0 ]; then
  say "ALL CHECKS PASSED. The toolchain and every pinned invariant are intact."
else
  say "$fail CHECK(S) FAILED -- see above."
  say ""
  say "Almost every plausible post-upgrade failure is a REBUILD problem, not a"
  say "protocol problem. In order of likelihood:"
  say "  1. brew reinstall hidapi cmake     # Homebrew after a major OS bump"
  say "  2. xcode-select --install          # new SDK"
  say "  3. rm -rf build && cmake -S . -B build && cmake --build build"
  say "  4. re-grant Input Monitoring to your terminal"
  say ""
  say "NOTHING here can have changed the mouse. The firmware on the device, the"
  say "captures, the vendor binaries and every note are files and hardware; they"
  say "do not know what OS you are running."
fi

exit $fail
