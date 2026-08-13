# Ferry (Switch)

A native Nintendo Switch homebrew app, styled to match the macOS Ferry client (same dark palette, same teal accent, same ferry icon). It's a drop-in replacement for DBI's built-in ftpd — it runs its own minimal FTP server on port 5000 with a live status screen (who's connected, what's currently being sent/received), plus a local SD card file browser.

Built with [devkitPro](https://devkitpro.org/) (devkitA64 + libnx) and SDL2. QR code rendering uses [Nayuki's QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library) (MIT license, vendored under `source/vendor/`).

## Screens

Launches into the **Server** screen: shows the console's IP/port (with a QR code while nothing's connected — scan it instead of typing the address), connection status, and a live log of transfers as Ferry (or any FTP client) connects. Switch to the **Browse** screen (local SD card file management) with **L** or **R**. Press **-** (Minus) anytime for an in-app help overlay.

### Server screen

- **L / R** — switch to Browse
- **ZL** — set or clear a PIN (clients must send it as their FTP password)
- **-** — help
- **+** — exit

### Browse screen

- **D-Pad or Left Stick Up/Down** — move selection
- **A** — open folder
- **B** — go up one level
- **Y** — delete selected file/folder (with confirmation)
- **X** — create a new folder (opens the on-screen keyboard)
- **L / R** — switch to Server
- **-** — help
- **+** — exit

## Protocol support

Minimal FTP server tailored to what the Ferry macOS client sends: `USER`/`PASS`/`TYPE`/`PWD`/`CWD`/`CDUP`/`PASV`/`LIST`/`RETR`/`STOR`/`MKD`/`RMD`/`DELE`/`RNFR`/`RNTO`/`SIZE`/`QUIT`. Up to 4 clients at once (non-blocking, single-threaded — each client's command or transfer advances one step per frame), so the Mac app's parallel-transfer mode actually runs in parallel against a Switch target. Anonymous by default; set a PIN (ZL) to require it as the FTP password — a wrong one gets the client disconnected immediately.

## Other notes

- The console won't auto-sleep while Ferry is running, so a connection left idle for a while doesn't silently drop.
- Every connect/disconnect/transfer/delete is appended to `sdmc:/switch/Ferry/log.txt`, so history survives an app restart even though the on-screen log only keeps the last few entries.

## Building

Requires `DEVKITPRO` set up with `switch-dev`, `switch-sdl2`, `switch-sdl2_ttf`, and `switch-sdl2_gfx`:

```bash
export DEVKITPRO=/opt/devkitpro
export PATH=$DEVKITPRO/pacman/bin:$PATH
sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_gfx
```

Then, from this directory:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITA64/bin:/opt/devkitpro/tools/bin:/opt/devkitpro/portlibs/switch/bin:$PATH
make
```

This produces `Ferry.nro`.

## Installing

Copy `Ferry.nro` to `/switch/Ferry/Ferry.nro` on your SD card, then launch it from the Homebrew Menu (hbmenu) instead of DBI.
