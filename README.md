# PanicOff

**One hotkey. Instant power-off.**

PanicOff is a tiny (24 KB) Windows background app. Press **Ctrl+Alt+Shift+A** and your PC switches off immediately: no "Closing apps…" screen, no "This app is preventing shutdown", no waiting.

> [!WARNING]
> **It really doesn't wait.** Open apps get no chance to save, so anything unsaved is gone. That's the whole point, but don't say you weren't warned.

## Features

-  Instant power-off through the Windows kernel's own shutdown path
-  Runs invisibly: no window, no tray icon, no taskbar entry
-  Starts automatically at login (optional)
-  A single 24 KB exe: no installer, no dependencies, no admin rights needed
-  Install and uninstall by double-clicking

## Install

1. Download `PanicOff.exe` from the [Releases](../../releases) page.
2. Double-click it and choose **Yes**.

That's it. It copies itself to `%LOCALAPPDATA%\PanicOff`, adds itself to autostart, and starts right away. Choose **No** instead to run it only until the next reboot.

> [!NOTE]
> Windows SmartScreen may say *"Windows protected your PC"* because the exe isn't code-signed. Click **More info → Run anyway**, or [build it yourself](#build-from-source).

## Uninstall

Double-click `PanicOff.exe` again. While it's running, it offers to stop and remove itself (autostart entry and installed copy).

## Command-line options

| Option | What it does |
|---|---|
| *(none)* | Asks whether to install or run once. If it's already running, offers to uninstall. |
| `--install` | Copies itself to `%LOCALAPPDATA%\PanicOff`, adds itself to autostart, starts |
| `--uninstall` | Stops, removes the autostart entry, deletes the installed copy |
| `--stop` | Stops the running instance (stays in autostart) |
| `--test` | Arms the hotkey, but only shows a message box. **No shutdown.** |
| `--run` | Listens silently (this is what autostart uses) |

Want to try the hotkey without losing your session? Run `PanicOff.exe --test` before installing. (It can't grab the hotkey while the real PanicOff is already running.)

## How it works

PanicOff registers a global hotkey with `RegisterHotKey` on a hidden message-only window, then just waits.

When you press the hotkey, it calls `NtShutdownSystem(ShutdownPowerOff)` from `ntdll.dll`. A normal shutdown spends most of its time asking every app and service to close politely. This skips all of that and goes straight to the kernel's power-off path, which **still flushes file-system caches and the registry to disk** before cutting power. So it's much faster than `shutdown /s /f /t 0`, but it's not the same as yanking the power cord.

All the setup work (enabling `SeShutdownPrivilege`, looking up the function) happens at startup, so pressing the hotkey triggers a single call. If that call ever fails, PanicOff falls back to `ExitWindowsEx(EWX_POWEROFF | EWX_FORCE)`.

## Good to know

- **Hotkey conflicts:** global hotkeys are first come, first served. If another program already owns the combo, PanicOff shows an error and exits. Pick a different combo ([see below](#change-the-hotkey)).
- **Lock screen and UAC prompts:** the hotkey doesn't fire on the lock screen or while a UAC prompt is open (that's Windows' secure desktop).
- **Fast Startup:** the next boot after a PanicOff shutdown is a full cold boot, so it may take a little longer than usual.
- **Event Viewer** may log the shutdown as unexpected. That's harmless.
- **Services** (databases, game servers, etc.) aren't stopped gracefully. Most recover fine, but don't make this your everyday shutdown button on a machine running something important.

## Change the hotkey

Edit these three lines near the top of `panicoff.c` and rebuild:

```c
#define HOTKEY_MODS (MOD_CONTROL | MOD_ALT | MOD_SHIFT) /* MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN */
#define HOTKEY_KEY  'A'                                 /* uppercase letter/digit, or a VK_* code  */
#define HOTKEY_TEXT L"Ctrl+Alt+Shift+A"                  /* only used in the message boxes          */
```

If PanicOff is already installed, run the new build with `PanicOff.exe --install` to replace the old one.

## Build from source

It's a single C file that uses only built-in Windows DLLs (`kernel32`, `user32`, `advapi32`).

**On Windows ([MSYS2](https://www.msys2.org/)).** Run this in the *MSYS2 UCRT64* shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc
gcc -O2 -s -municode -mwindows panicoff.c -o PanicOff.exe -luser32 -ladvapi32
```

**Cross-compiling from Linux:**

```sh
sudo apt install gcc-mingw-w64-x86-64
x86_64-w64-mingw32-gcc -O2 -s -municode -mwindows panicoff.c -o PanicOff.exe -luser32 -ladvapi32
```

## Requirements

Windows 10 or 11, 64-bit.
