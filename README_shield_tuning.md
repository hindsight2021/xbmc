# Kodi Shield Tuning

This document outlines modifications and recommended settings applied to this fork of Kodi to improve performance on Android devices, particularly the NVIDIA Shield.

## Added advancedsettings.xml
A new file `shield_advancedsettings.xml` has been added at the root of the repository. This XML file contains caching and network settings tuned for high‑bitrate streaming and network shares. The settings increase the buffer size, read factor, reduce decode buffer count and adjust network timeouts, as recommended by the research.

## How to use
Copy `shield_advancedsettings.xml` into Kodi's `userdata` folder on your Android/Nvidia Shield device (e.g., `/sdcard/Android/data/org.xbmc.kodi/files/.kodi/userdata/advancedsettings.xml`) and rename it to `advancedsettings.xml`.  Restart Kodi to apply the settings.

## Future work
The research report suggests additional code‑level improvements such as:

- Increasing queue sizes for high‑bitrate video in the VideoPlayer components.
- Improving audio sink stuck detection on Android (Amlogic devices).
- Updating to the bwdif deinterlacer.
- Adjusting HDR handling and theme packaging.

These improvements are not yet implemented in this fork, and contributions are welcome.
