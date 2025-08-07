# Shield Tuning Guide for Kodi Omega Fork

This document summarizes the enhancements implemented in this fork of Kodi 21.2 (Omega) for Android/Nvidia Shield and outlines additional optimizations that require code changes.

## Implemented Changes

* **Dynamic caching and network tuning** – `autoexec.py` writes an `advancedsettings.xml` on startup. It dynamically calculates the buffer size based on available RAM (20 % of total memory, capped at 512 MB) and adjusts NFS/SMB chunk sizes and readfactor depending on whether the Shield is on wired Ethernet or Wi‑Fi.

* **High-performance advanced settings** – A static `shield_advancedsettings.xml` is provided for manual use. It sets aggressive caching (256 MB buffer, readfactor 8), network timeouts, and chunk sizes.

* **Audio sink watchdog improvements** – `AESinkAUDIOTRACK.cpp` contains a watchdog that resets the Android `AudioTrack` when the playback head position stops increasing, fixing the Amlogic BSP bug that causes severe A/V desynchronisation【246519636831221†L140-L149】.

* **Build script for minimal Android build** – `build_android_shield.sh` demonstrates how to compile a lean Android APK using Ninja and disables unused modules such as AirTunes, UPnP, PVR, DVD audio and tests, enables link‑time optimisation and uses `-Oz` size optimisation flags.

## Pending and Recommended Work

The following features were outlined in the research report and should be implemented in the C++ source for a truly supercharged Shield build. These changes remain TODO:

* **Adaptive queue sizing** – Replace hard‑coded 8 second queue sizes in VideoPlayer with dynamic calculations based on bit‑rate and available memory. Expose these parameters in the settings GUI and provide an advanced option to disable the 8 second cap entirely【713809543717377†L208-L248】.

* **Asynchronous remote probing** – Modify the file and UPnP/NFS/SMB enumeration code to probe remote shares in background threads and skip unavailable sources to avoid startup stalls.

* **AudioTimestamp fallback & offload** – Enhance the audio sink watchdog by consulting `AudioTrack.getTimestamp()` when the playback head stagnates and use AudioTrack offload on Shield for AC3/E-AC3/DTS/TrueHD. Expose sink latency and passthrough mode in the UI.

* **HDR and refresh rate handling** – Audit use of deprecated HDR enums and migrate fully to `Display.getSupportedHdrTypes()`. Use `Surface.setFrameRate()` on API 30+ to request exact frame rates (e.g. 23.976 fps) and separate refresh‑rate switching from HDR switching.

* **Hardware deinterlacing and Dolby Vision** – Integrate Nvidia’s Tegra Video Decoder and proprietary `nvdeint` deinterlacer when available; add detection for Dolby Vision (`HDR_TYPE_DOLBY_VISION`) and fallback gracefully to HDR10 or SDR.

* **UI and cache optimisation** – Implement lazy loading and periodic purging of the texture cache and provide a lightweight Estuary‑Lite skin without heavy animations. Consider adding `<imageres>` and `<imagereslimit>` tags in `advancedsettings.xml` to cap thumbnail resolution.

* **Minimal build configuration** – Further reduce binary size by disabling unused features via CMake (CEC, PVR clients, AirPlay server, screensavers, etc.) and enabling link‑time optimisation. See `build_android_shield.sh` for an example.

* **MP4 multi‑part seek bug** – Investigate the seek logic for multi‑part MP4 files and port the fix referenced in [Kodi issue #23673](https://github.com/xbmc/xbmc/issues/23673). The issue describes how fast‑forwarding across chapter boundaries hangs playback【87175554174265†L182-L216】.

Developers interested in contributing to this fork can use this document as a roadmap. Refer to the original research report for detailed explanations and citations.
