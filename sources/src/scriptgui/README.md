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
│        ├── script.main
│        ├── uartscript1.comm
│        ├── uartscript2.comm
│        ├── uartscript.comm
│        └── config.ini
│
│
├── qt.conf
├── launch.sh
├── ScriptFrontend
└── uscript

# File types

Each kind of file has its own extension; the GUI and the interpreter enforce it the same way
(one shared rule set, `config/inc/uSharedScriptExt.hpp`).

| Extension | Used for | Where it can be opened |
|---|---|---|
| `.main` | scripts run directly by the interpreter (`uscript -s script.main`); the **only** kind an `INCLUDE "file.main"` may reference | main editor tabs; the only kind RUN accepts |
| `.comm` | communication scripts run by plugins (`UART.SCRIPT uart.comm`, `PLUGIN.CMD script x.comm`) | comm view only |
| `.ini` | interpreter configuration (`config.ini`) | main editor tabs (not runnable) |
| `.sh` (Linux/macOS only) | helper shell scripts, e.g. `setup.sh` | main editor tabs (not runnable as a µScript) |
| `.bat` `.cmd` `.ps1` (Windows only) | helper scripts: batch / command / PowerShell, e.g. `setup.cmd` | main editor tabs (not runnable as a µScript) |

Shell scripts are platform specific: a `.sh` is not opened on Windows, and `.bat` / `.cmd` / `.ps1` are
not opened on Linux/macOS. Anything else (`.txt`, `.scr`, `.script`, no extension, ...) is refused on every
platform, with the reason written to the log:

* **Open / Browse, drag-and-drop, the SCRIPT path field, session restore** – the open dialog lists only
  `*.main *.ini *.sh` (Linux) or `*.main *.ini *.bat *.cmd *.ps1` (Windows), with no "All files"; other
  dropped files are skipped and reported.
* **Save As** – the dialog filter matches the viewer (main editor as above, comm view `*.comm`); a name
  without extension gets `.main` / `.comm` appended; a wrong extension is refused.
* **Duplicate script** – the copy must also end in `.main`.
* **Clicking a `PLUGIN.SCRIPT file` line** previews the file in the comm view only if it is a `.comm` file;
  **clicking an `INCLUDE "file"` line** opens it only if it is a `.main` file.
* **Interpreter** – `uscript` refuses a main script that is not `.main`; every plugin's comm-script loader
  refuses a file that is not `.comm`; an `INCLUDE` of anything but `.main` is an error
  (`INCLUDE supports only .main files`). Note: this also means a `.comm` script cannot `INCLUDE` a
  `.comm` file. The default script name is now `script.main`.

## Migrating older projects

Older versions accepted any extension (typically `.txt`). To convert a project folder:

```
python3 tools/migrate_script_extensions.py  <folder> [<folder> ...]           # dry run: shows the plan
python3 tools/migrate_script_extensions.py  --apply [--git]  <folder> ...     # renames + rewrites references
```

Files are classified by how they are used: the target of `INCLUDE "..."` becomes `.main`, the target of
`PLUGIN.SCRIPT <file>` / `PLUGIN.CMD script <file>` becomes `.comm`, a file with `LOAD_PLUGIN` (or one that
references other scripts) becomes `.main`. References inside scripts are rewritten, existing files are never
overwritten, and `setup.bat` becomes `setup.cmd` (a `setup.bat` still works if you keep it). Files whose role cannot be determined (not referenced, no
`LOAD_PLUGIN`) and files used both ways are listed for manual handling instead of being guessed.
Saved GUI sessions that still list `.txt` files simply skip them (the log says which).

# Config and Setup buttons (config.ini, setup.sh / setup.cmd)

The setup script is run by `bash` (Linux) or `cmd.exe` (Windows), so `setup.ps1` can be edited like any
other `.ps1` but is not run by the Setup **Run** button.

The toolbar shows `Config: [Open|Create]   Setup: [Open|Create] [Run]`. They always refer to
the folder of the active script (the one RUN executes; ini / setup tabs do not change it) and are
re-evaluated on every tab switch, load, save and when the window regains focus.

* **Config** – `Open` when `config.ini` exists next to the script (opens it in a tab), otherwise
  `Create` (creates an empty `config.ini` and opens it). If the file exists it is passed to the
  interpreter as `-c`; otherwise no `-c` is passed.
* **Setup Open/Create** – `Open` when `setup.sh` (Linux) / `setup.cmd` (Windows; an existing `setup.bat`
  is found too) exists next to the
  script, otherwise `Create` (creates it and opens it for editing). It is shown as plain text and
  **RUN** refuses to feed it to the interpreter.
* **Setup Run** – enabled when the setup script exists. A script just created with `Create` keeps
  Run disabled until it has been saved. Working directory = the script's folder; output and exit
  code appear in the log panel (stderr in red). If the script is open with unsaved edits you are
  asked whether to save first.

## Elevated rights

| | Linux (`setup.sh`) | Windows (`setup.cmd`) |
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
