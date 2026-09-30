# Provenance

This repository builds a WCLAP from pinned upstream sources.

## Pins

- OB-Xd: https://github.com/reales/OB-Xd.git @ `f1fcdd51eabcb7855cdd90818611cd7e357c359c` ("2.10 release").
  The upstream `main` branch has since removed the sources (commit `73cea60`, "Clean up");
  the README states that source for versions up to 2.11 is available under GPL v3.
- CLAP headers: https://github.com/free-audio/clap.git @ `a47f6badb49d948fd009998f28309cdab78979c9`
  (includes the `clap.webview/3` draft, `include/clap/ext/draft/webview.h`)
- wasi-sdk: `wasi-sdk-34`
- wasi-sdk Linux x86_64 archive: `wasi-sdk-34.0-x86_64-linux.tar.gz`
- SHA-256: `b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4`

## License provenance

- OB-Xd: GPL-3.0. The pinned repository's `LICENSE` contains GNU GPL version 3
  (source headers in the engine say GPL version 2 or later). Source:
  https://github.com/reales/OB-Xd/blob/f1fcdd51eabcb7855cdd90818611cd7e357c359c/LICENSE
- CLAP headers: MIT. Source: https://github.com/free-audio/clap/blob/a47f6badb49d948fd009998f28309cdab78979c9/LICENSE
- wasi-sdk: Apache-2.0. Source: https://github.com/WebAssembly/wasi-sdk/blob/wasi-sdk-34/LICENSE

This repository's own code is distributed under GPL-3.0 (`LICENSE`), and the
packaged bundle carries that license text next to `module.wasm`.

The web editor (`webui/`) is original work for this port. Its layout follows
the OB-Xd 2 panel; it contains no OB-Xd skin images or fonts.

## Local patches

1. `patches/obxd/0001-engine-juce-free-include.patch`
   - SHA-256: `03e2a2593990a0165f9210913dc3061cadc8c96c1e35066228c8e91306590982`
   - `Source/Engine/SynthEngine.h` includes `../PluginProcessor.h`, the JUCE
     audio processor, only to obtain JUCE utilities. The patch includes
     `<ObxdJuceShim.h>` (this repository's `src/`) instead. No DSP changes.

## Engine substitutions (no patch)

- `src/ObxdJuceShim.h` implements the JUCE utilities the engine uses. `Random`
  is JUCE's 48-bit LCG with the same `nextInt()`/`nextFloat()` mapping; the
  system generator is seeded from a fixed value rather than the wall clock, so
  renders are reproducible.
- `src/MtsEspUnavailable.cpp` implements the ODDSound MTS-ESP client API
  declared by the pinned `Source/MTS/libMTSClient.h` as a client that never
  finds a master. MTS-ESP loads a shared library from the host process, which
  a WCLAP sandbox cannot do; the engine's `Tuning` stays in 12-TET.
