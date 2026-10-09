# rapidyaml (vendored, amalgamated single header)

Small YAML parser vendored for the VDI `--config file.yaml` work (task **P2.2**,
consumer is **P2.1**). Desktop only — the VDI role is desktop-only, so Android is
not wired up.

| | |
|---|---|
| **Library** | Rapid YAML (rapidyaml / ryml) |
| **Version** | `0.7.2` (release tag `v0.7.2`, commit `9b8df62d9a45c050804305462b243add936c9292`) |
| **Upstream** | <https://github.com/biojppm/rapidyaml> · release <https://github.com/biojppm/rapidyaml/releases/tag/v0.7.2> |
| **Asset** | `https://github.com/biojppm/rapidyaml/releases/download/v0.7.2/rapidyaml-0.7.2.hpp` |
| **SHA-256 of the downloaded asset** | `00aca709dbd24115874a3ee97da4f615ea15cae60bb1155bef1e7369c7c2f6d8` (1 523 783 bytes, 42 634 lines) |
| **Licence** | MIT — Copyright (c) 2018 João Paulo Magalhães `<dev@jpmag.me>`. The full MIT text is reproduced verbatim at the top of `ryml_all.hpp` (lines 36–61) and is `LICENSE.txt` upstream. One bundled third-party file inside the amalgamation, `c4/ext/debugbreak/debugbreak.h`, is BSD-2-Clause; it has its own banner inline and is not compiled into the Sonic path used here. |
| **Obtained** | 2026-10-09, by `curl` from the GitHub release above (that exact URL, HTTP 200). Recorded here so the asset can be re-fetched and hash-matched. |
| **Build-time network** | none — the file is committed in-tree. No system `libyaml`, no extra package on macOS or Linux. |

## Why rapidyaml

* **MIT**, permissive, no notice obligations beyond keeping the banner (which is
  inside the file anyway).
* **Single file, no build-system surface.** The whole library is one header the
  project already owns a copy of; there is no nested `add_subdirectory`, no
  `FetchContent` (which would need network at configure time — the task forbids
  that), no CMake option zoo, and nothing to keep in sync with JUCE's build.
* **C++17, header-friendly**, same standard the app uses.
* Exposes the *parse tree*, which P2.1 needs: it must know each key's source
  location so `Config::save()` can rewrite `audio.input_device` /
  `audio.output_device` **in place**, preserving comments and key order, rather
  than re-serialising. ryml keeps byte offsets for every node, so this is
  possible; a DOM-only API would make it much harder.

## Files added by us

```
sonobus/deps/rapidyaml/ryml_all.hpp     upstream asset, byte-identical (verified by SHA-256)
sonobus/deps/rapidyaml/ryml_impl.cpp    ours: the single implementation TU (see below)
sonobus/deps/rapidyaml/README.md        this file
```

`ryml_all.hpp` is **unmodified**. The upstream asset filename
`rapidyaml-0.7.2.hpp` was renamed to `ryml_all.hpp`, which is the name upstream's
own documentation and `#include <ryml_all.hpp>` examples use; the contents are
untouched, so re-downloading and re-hashing still works.

`ryml_impl.cpp` exists because upstream's amalgamation instructions require
exactly one translation unit in the program to `#define
RYML_SINGLE_HDR_DEFINE_NOW` before including the header — a single header alone
would not link. It borrows nothing: it is three lines of upstream-mandated macro
plus one deliberate deviation documented in the file
(`RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS`, so malformed input throws instead of
calling `abort()`). Losing that would mean a bad YAML file kills the audio app.

## How it is wired in

`sonobus/CMakeLists.txt`, single additive block (search for `P2.2`):

* `yaml_parser` — an **INTERFACE** library carrying `deps/rapidyaml` as an
  include directory. Consumers link it and write `#include <ryml_all.hpp>`;
  the include directory is deliberately this one directory, so ryml's `ryml::`
  / `c4::` symbols are the only names introduced and cannot collide with
  JUCE/AOO.
* `sono_yaml` — a `STATIC` library that compiles `ryml_impl.cpp` once and links
  `yaml_parser` `PUBLIC`, so the implementation is shared rather than duplicated
  per consumer.
* `SonoBus` links `sono_yaml` privately; `yaml_smoke` links it too. Unused code
  is dead-stripped in release, so app behaviour is unchanged.

`sono_yaml` is built with `POSITION_INDEPENDENT_CODE ON`. This is **required**,
not cosmetic: the default Linux format set is VST3 + Standalone + LV2, so this
archive ends up linked into shared modules. Without the property the Linux
VST3/LV2 link fails with ~32 `recompile with -fPIC` / `dangerous relocation`
errors as soon as anything actually calls the parser — it passes only while the
archive is unreferenced and the linker discards it, which is why the bug hides
until P2.1 lands. The property is portable, so it is correct on macOS too (where
PIC is already the default for x86-64/arm64).

Because the amalgamated header defines its implementation inline (a large body of
functions, and `Callbacks::Callbacks` etc. are *not* `inline`), the implementation
lives in exactly one object file. `sono_yaml` is the only place
`RYML_SINGLE_HDR_DEFINE_NOW` may appear.

## NOT part of the runtime path

`sonobus/tests/yaml_smoke.cpp` — a standalone executable target (`yaml_smoke`)
that parses a small document with the P2.1 key shape, prints the values, checks
them, asserts the parser can be constructed, and exits non-zero on any failure.
It is a build proof, not shipped code; nothing in `SonoBus` depends on it.

```
cmake --build build/desktop-release --target yaml_smoke -j5
./build/desktop-release/yaml_smoke
```

## Updating

Replace the header with the newer release asset, re-run `shasum -a 256`, update
the table above, and rebuild. The hash must match before committing.
