#!/bin/bash

gendef ftd2xx.dll
x86_64-w64-mingw32-dlltool -d ftd2xx.def -l libftd2xx.a -D ftd2xx.dll