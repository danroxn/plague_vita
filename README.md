# Plague Inc. Vita

This is a native wrapper/port of **Plague Inc. 1.15.3** for the PS Vita.

The port loads the original Android ARMv7 `libgame.so` into memory, resolves its imports and patches it to run on the Vita. Graphics are handled by vitaGL, while input, audio, keyboards and dialogs use native Vita services.

You will need your own legally obtained copy of the supported Android APK to prepare the package assets once and to run the game on your Vita.

## Notes

The game runs offline. Online services, cloud saves, social features and store purchases are unavailable, and external web links are removed.

## Setup Instructions (For End Users)

1. Set up a homebrew-enabled PS Vita with
   [kubridge](https://github.com/bythos14/kubridge) and
   [libshacccg](https://cimmerian.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx)
2. Build the VPK using the [build instructions below](#build-instructions).
3. Install `build/plague-vita.vpk` with VitaShell.
4. Copy the original Android APK (v. 1.15.3) to `ux0:/data/plague/package.apk` on the Vita.
5. Launch **Plague Inc. Vita** from the LiveArea.

Use the front touchscreen to control the game.

### Configuration and Troubleshooting

Retina rendering and 4× MSAA are enabled by default. To adjust these settings, create or edit `ux0:/data/plague/port.cfg` with the options you want to change:

```ini
retina=1
msaa=4
log_level=0
unlock_all=0
```

Set `retina` to `0` or `1` and `msaa` to `0`, `2` or `4`. Configuration changes take effect after restarting the game.

Logging is disabled by default. Set `log_level=1` for errors or `log_level=2` for debug output and performance measurements, then restart the game. The log is written to `ux0:/data/plague/port.log`. When reporting a problem, include this log and describe what happened on screen.

## Build Instructions

The recommended environment is VS Code with the **Dev Containers** extension and a running Docker runtime. The included devcontainer uses the [official VitaSDK image](https://github.com/vitasdk/docker).

Only the following APK is supported:

```text
Package:        com.miniclip.plagueinc
Version:        1.15.3
APK bytes:      54207234
APK SHA-256:    6ac3f16b37f3c8fd125d029e8883f3bef8d450675687606d29cb68b24ae94363
APK XXH3-128:   bf4c7699a451f8e550a3ff3edd21f541
```

1. Open the repository folder in VS Code.
2. Run **Dev Containers: Reopen in Container**.
3. Update the submodules:

   ```sh
   git submodule update --init
   ```
4. Prepare the package assets from your APK in the
   devcontainer terminal:

   ```sh
   make prepare APK=/path/to/plague.apk
   ```
5. Build the VPK:

   ```sh
   make build
   ```

## Additional Content

All additional content can be unlocked by setting `unlock_all=1` in `ux0:/data/plague/port.cfg`.
Please enable support for these contents solely if you possess these contents on your Android device. Sadly, there is no way to propose a license check on Vita against your purchased in-game contents on Android, so show respect and support the original game devs first before proceeding.  


```ini
unlock_all=1
```

## Trophies

The game's 167 achievements are also available as native PS Vita trophies, split into two sets of 128 and 39 bronze trophies.

Trophies require [NoTrpDrm](https://github.com/Rinnegatamante/NoTrpDrm).

## Development

### VitaCompanion

For development with [VitaCompanion](https://github.com/devnoname120/vitacompanion), create a local `.vita` file in the repository root. Use Make syntax without quotes:

```make
IP=192.168.1.100 # Vita IP
FTP_PORT=1337
COMMAND_PORT=1338
```

**Before using `make send` for the first time, install `build/plague-vita.vpk` with VitaShell and copy the original APK to `ux0:/data/plague/package.apk`.**  
`make send` only updates an existing installation

```sh
make send
make log
```

`make send` closes all running apps, uploads only `eboot.bin`, then launches the game. Reinstall the VPK with VitaShell to update the loading background, bubble icon or LiveArea artwork. 

`make log` downloads the current log to `work/vita-debug/port.log`.

## Credits

- TheFloW and Rinnegatamante for [so_util](https://github.com/Rinnegatamante/so_util).
- Rinnegatamante and contributors for [vitaGL](https://github.com/Rinnegatamante/vitaGL).
- The VitaSDK contributors for the [toolchain](https://github.com/vitasdk).
- Rinnegatamante for [NoTrpDrm](https://github.com/Rinnegatamante/NoTrpDrm).
- The [soloader-boilerplate contributors](third_party/bionic/README.md) for the
  Android compatibility helpers, and the Android Open Source Project and
  FalsoJNI for the [JNI interface header](third_party/jni/README.md).
