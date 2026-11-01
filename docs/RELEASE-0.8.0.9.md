# Apotheosis 0.8.0.9 ARM — existing build backup

This release preserves the existing Windows 10 Mobile ARM32 package built on 2026-10-23. It has not been rebuilt on 2026-11-01.

- Optional protected D3D manager / RGB32 main-reader playback path and software fallback; normal playback keeps the existing software path.
- Full current WebKit source patch and local port changes have been preserved on `fix/scheduled-task-checkedptr`.
- The existing package was deployed/launched in the earlier device session; continuous playback performance and MSE remain unverified. See the handoff document for the exact evidence and limits.

The APPX is version **0.8.0.9**, ARM, signed by **CN=EdgeHTMLReborn**, 56,644,996 bytes. SHA-256: `b2c21496087e72c39977f6fe8821ad4cafc0985e554242b866d1ab8208a5c5a1`. Install the included public certificate and ARM dependencies before sideloading. No private signing key is included.

Source provenance: the stored package verification identifies the 2026-10-23 build based on bb56a46 with local changes. The media patch hash still matches the recorded build, while the current PortMediaAudio.cpp hash differs from the recorded input hash. Therefore this existing binary is not asserted to be a reproducible build of the new source-backup commit.

This is a development prerelease backup.
