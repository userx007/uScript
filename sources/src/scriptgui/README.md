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

