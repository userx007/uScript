# Create deployment 


## Copy Qt libraries

```bash
#!/bin/bash

# Copy all your non-system Qt libs into ./lib/
mkdir -p ./deploy/lib ./deploy/plugins/platforms

for lib in libQt6Widgets libQt6Gui libQt6Core libQt6DBus \
           libicui18n libicuuc libicudata libzstd \
           libdouble-conversion libpcre2-16 libpcre2-8 libb2; do
  cp -P /lib/x86_64-linux-gnu/${lib}.so* ./deploy/lib/
done

# Copy platform plugin
cp /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms/libqxcb.so ./deploy/plugins/platforms/
```

## Copy uscript, ScriptFrontend and plugins in deploy folder

├── iplugins
│        └── libtest_iplugin.so
├── splugins
│        ├── libbuspirate_plugin.so
│        ├── libcp2112_plugin.so
│        ├── libft2232_plugin.so
│        ├── libft232h_plugin.so
│        ├── libft245_plugin.so
│        ├── libft4232_plugin.so
│        ├── libhydrabus_plugin.so
│        ├── libshell_plugin.so
│        ├── libuartmon_plugin.so
│        └── libuart_plugin.so
├── ScriptFrontend
└── uscript


## Add `qt.conf`

```ini
[Paths]
Prefix = .
Plugins = plugins
Libraries = lib
```

## Launch script

```bash
#!/bin/bash
DIR="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
exec "$DIR/ScriptFrontend" "$@"
```

## Final structure
│
│ -------- Qt libraries needed by ScriptFrontend ------------
│
├── lib
│    ├── libb2.so.1 -> libb2.so.1.0.4
│    ├── libb2.so.1.0.4
│    ├── libdouble-conversion.so.3 -> libdouble-conversion.so.3.1
│    ├── libdouble-conversion.so.3.1
│    ├── libicudata.so -> libicudata.so.72.1
│    ├── libicudata.so.72 -> libicudata.so.72.1
│    ├── libicudata.so.72.1
│    ├── libicui18n.so -> libicui18n.so.72.1
│    ├── libicui18n.so.72 -> libicui18n.so.72.1
│    ├── libicui18n.so.72.1
│    ├── libicuuc.so -> libicuuc.so.72.1
│    ├── libicuuc.so.72 -> libicuuc.so.72.1
│    ├── libicuuc.so.72.1
│    ├── libpcre2-16.so -> libpcre2-16.so.0.11.2
│    ├── libpcre2-16.so.0 -> libpcre2-16.so.0.11.2
│    ├── libpcre2-16.so.0.11.2
│    ├── libpcre2-8.so -> libpcre2-8.so.0.11.2
│    ├── libpcre2-8.so.0 -> libpcre2-8.so.0.11.2
│    ├── libpcre2-8.so.0.11.2
│    ├── libQt6Core.so -> libQt6Core.so.6
│    ├── libQt6Core.so.6 -> libQt6Core.so.6.4.2
│    ├── libQt6Core.so.6.4.2
│    ├── libQt6DBus.so -> libQt6DBus.so.6
│    ├── libQt6DBus.so.6 -> libQt6DBus.so.6.4.2
│    ├── libQt6DBus.so.6.4.2
│    ├── libQt6Gui.so -> libQt6Gui.so.6
│    ├── libQt6Gui.so.6 -> libQt6Gui.so.6.4.2
│    ├── libQt6Gui.so.6.4.2
│    ├── libQt6Widgets.so -> libQt6Widgets.so.6
│    ├── libQt6Widgets.so.6 -> libQt6Widgets.so.6.4.2
│    ├── libQt6Widgets.so.6.4.2
│    ├── libzstd.so -> libzstd.so.1.5.4
│    ├── libzstd.so.1 -> libzstd.so.1.5.4
│    └── libzstd.so.1.5.4
├── plugins
│    └── platforms
│        └── libqxcb.so
│
│ ----------- uscript ----------------
│
├── iplugins
│    └── libtest_iplugin.so
├── splugins
│    ├── libbuspirate_plugin.so
│    ├── libcp2112_plugin.so
│    ├── libft2232_plugin.so
│    ├── libft232h_plugin.so
│    ├── libft245_plugin.so
│    ├── libft4232_plugin.so
│    ├── libhydrabus_plugin.so
│    ├── libshell_plugin.so
│    ├── libuartmon_plugin.so
│    └── libuart_plugin.so
│
│ --------- scripts ------------------
│
├── scripts
│    └── uart
│        ├── script.txt
│        ├── uartscript1.txt
│        ├── uartscript2.txt
│        ├── uartscript.txt
│        └── uscript.ini
│
│
├── qt.conf
├── launch.sh
├── ScriptFrontend
└── uscript

# Config and Setup buttons (uscript.ini, setup.sh / setup.bat)

The toolbar shows `Config: [Open|Create]   Setup: [Open|Create] [Run]`. They always refer to
the folder of the active script (the one RUN executes; ini / setup tabs do not change it) and are
re-evaluated on every tab switch, load, save and when the window regains focus.

* **Config** – `Open` when `uscript.ini` exists next to the script (opens it in a tab), otherwise
  `Create` (creates an empty `uscript.ini` and opens it). If the file exists it is passed to the
  interpreter as `-c`; otherwise no `-c` is passed.
* **Setup Open/Create** – `Open` when `setup.sh` (Linux) / `setup.bat` (Windows) exists next to the
  script, otherwise `Create` (creates it and opens it for editing). It is shown as plain text and
  **RUN** refuses to feed it to the interpreter.
* **Setup Run** – enabled when the setup script exists. A script just created with `Create` keeps
  Run disabled until it has been saved. Working directory = the script's folder; output and exit
  code appear in the log panel (stderr in red). If the script is open with unsaved edits you are
  asked whether to save first.

## Elevated rights

| | Linux (`setup.sh`) | Windows (`setup.bat`) |
|---|---|---|
| Detection | a `sudo` command outside comment lines | well-known admin-only commands (`net start`, `sc config`, `netsh`, `reg add HKLM`, `schtasks`, `choco`, ...) |
| Not needed / already root-admin / passwordless sudo | runs directly, **no dialog** | runs directly, **no dialog** |
| Needed | a password dialog appears; the password is checked first (`sudo -S -k -v`) and re-requested if wrong | a dialog asks for an administrator account + password |
| Execution | `bash setup.sh`; `sudo` inside the script is routed through a private `SUDO_ASKPASS` helper (works in pipes and repeated calls, no terminal needed) | PowerShell `Start-Process -Credential`, output tailed from redirect files |

The dialog also has **Run without password** (for when the detection is wrong) and **Cancel**.
The password is only kept in memory for the duration of the run; the askpass helper lives in a
private temp directory that is removed when the script ends.

Notes: the script runs without a terminal and with stdin closed, so it must be non-interactive
(`apt-get -y`, no `read` prompts). Closing the app while the setup runs asks for confirmation.
