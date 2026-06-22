# ADB.webOS
<img width="100" height="100" alt="android mascot" src="https://github.com/user-attachments/assets/046bf530-0ce8-4804-9561-ec9860cd9313" /> 

Say 'Hello' to this green guy visiting your rooted LG TV:

ADB.webOS offers low-latency screencasts and ADB access to your android devices.

## Features
- High-performance Android video and audio screencasting over USB (in the near future wireless adb is planned)
- Native `adb` binary included for debugging, shell access, and device management
- Supports Android 11 through Android 16
- Low-latency USB connection with no network setup required
- Works with Android devices that have Developer Options and USB debugging enabled

<img width="1280" height="720" alt="screenshot-adb-webos" src="https://github.com/user-attachments/assets/527f5613-a7b9-43be-9162-e26da49e8447" />

## Requirements
### Android Device
- Android 11–16
- Developer Options enabled
- USB debugging enabled
- USB connection to the TV

### webOS Device
- webOS TV (> webOS 4, LG C9) with Developer Mode enabled

## Download

Download the latest IPK package from the **Releases** page.

## Getting Started

1. Enable **Developer Options** on your Android device.
2. Enable **USB debugging**.
3. Connect your Android device to your webOS TV using a USB cable.
4. Launch **ADB.webOS**.
5. Accept the USB debugging authorization prompt on your Android device if prompted.

Your Android device should now be screencasting to the TV.

## Using ADB from the Terminal

The bundled `adb` binary is available at:

```bash
/media/developer/apps/usr/palm/applications/com.adb.webos/global-syspath/adb
```

Example commands:

```bash
adb devices
adb shell
adb logcat
```

### Add ADB to Your PATH

To add `adb` to the BusyBox `PATH`, run:

```bash
sh /media/developer/apps/usr/palm/applications/com.adb.webos/scripts/add-busybox-path.sh
```

### Remove ADB from Your PATH

To remove `adb` from the BusyBox `PATH`, run:

```bash
sh /media/developer/apps/usr/palm/applications/com.adb.webos/scripts/remove-busybox-path.sh
```

## Known Issues

- Device rotation may require disconnecting and reconnecting the USB cable.
- Occasional audio stuttering or chopping may occur.
- Some Android devices may require reconnecting after changing USB modes.

## Documentation

Documentation is currently a work in progress and will be expanded in future releases.

## Building

Building is currently supported on **Linux only**.

If you are using Windows, you can build the project through **WSL (Windows Subsystem for Linux)**.

A typical build takes less than a minute to complete.

From the project root directory, run:

```bash
./scripts/webos/easy_build.sh
```

The build script will automatically:

1. Download the appropriate webOS SDK (if not already installed)
2. Compile the project
3. Generate the final `.ipk` package

## License Notice

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.

If you distribute modified versions of this project, you must make the corresponding source code for those modifications publicly available in accordance with the GPLv3 license terms.

See the `LICENSE` file for details.

## Support Development

If you find this project useful and would like to support its continued development, donations are greatly appreciated.

👉 https://ko-fi.com/eddy70696

Feedback, bug reports, feature requests, and pull requests are always welcome.

## Credits

- Moonlight webOS — Foundation and inspiration for this project.
- ADB — Android Debug Bridge.
- Scrcpy — Android screencasting technology.
- Moonlight Embedded — `libgamestream` and decoder components.
- Android Brand Guidelines — Android robot artwork attribution.

### Attribution

> "The Android robot is reproduced or modified from work created and shared by Google and used according to terms described in the Creative Commons 3.0 Attribution License."

### Upstream Projects

- https://github.com/mariotaku/moonlight-tv
- https://developer.android.com/tools/adb
- https://github.com/genymobile/scrcpy
- https://github.com/irtimmer/moonlight-embedded
- https://developer.android.com/distribute/marketing-tools/brand-guidelines

If any attribution is missing or incorrect, please open an issue.
