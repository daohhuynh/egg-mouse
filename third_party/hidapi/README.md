# hidapi 0.15.0, vendored

Not our code. Upstream is <https://github.com/libusb/hidapi>, and these four
files are copied from its 0.15.0 release **byte for byte, unmodified**.

## Provenance, so this is checkable rather than trusted

| what | value |
| --- | --- |
| release tarball | `https://github.com/libusb/hidapi/archive/refs/tags/hidapi-0.15.0.tar.gz` |
| tarball SHA-256 | `5d84dec684c27b97b921d2f3b73218cb773cf4ea915caee317ac8fc73cef8136` |
| `hidapi/hidapi.h` | `c9cef28c53a13a9403e54d5a8a099d51afff119ab04094df89356df0833a822e` |
| `mac/hid.c` | `0770e9b42138f9a42e91c8ddb4ddbd0696ebaf6f6535c969b7b36b8caa90ae80` |
| `mac/hidapi_darwin.h` | `48854f6f6b612a90291c3de83a5755c2ed8642da173151a41377090821c39187` |
| `LICENSE-bsd.txt` | `30eb1bef29b46f8ba7ab8b416035dbd93cb034a45481dd97815b944284582cd2` |

`Tests/test_vendored_hidapi.py` recomputes every one of those and fails if a
byte moves, so a local edit cannot pass unnoticed. Re-verify from upstream with:

```sh
curl -sL https://github.com/libusb/hidapi/archive/refs/tags/hidapi-0.15.0.tar.gz | shasum -a 256
```

## Why it is here at all

Two reasons, both about the people downloading a release rather than about us.

1. **Homebrew's `libhidapi.0.15.0.dylib` is arm64 only.** A universal build
   cannot link it, so an Intel Mac could never run a released binary.
2. **The dylib lives at an absolute path.** `otool -L` on a Homebrew-linked
   `egg-flash` shows `/opt/homebrew/opt/hidapi/lib/libhidapi.0.dylib`, so it
   dies at launch on any Mac without Homebrew and hidapi installed.

Compiling this one file in fixes both, and the resulting binaries have no
non-system dependency at all.

**0.15.0 is not an arbitrary pin.** It is the exact version whose Homebrew
build was linked into the `egg-flash` that performed the real firmware update
on 2026-09-05. Same upstream source, same version; only the compiler flags and
the target architectures differ. Do not bump it casually: engineering-rules.md
§4.4 says each step should remove untested code from the next, and the HID
transport is the one layer the mock cannot exercise.

## Licence

hidapi is tri-licensed: `GPL-3.0-only OR BSD-3-Clause OR HIDAPI`. **This project
takes BSD-3-Clause**, whose full text is in `LICENSE-bsd.txt` beside this file,
with the contributor list in `AUTHORS.txt`. BSD-3-Clause imposes no copyleft on
the rest of this repository, which stays Apache-2.0; engineering-rules.md §1.1
explains why keeping copyleft obligations out matters here. Redistributing a
binary built from these files means keeping that notice with it, which
`NOTICE` does.

Only the macOS backend is vendored. The Linux, Windows, libusb, NetBSD and
Android backends are not, because this project is macOS-only.
