#!/bin/bash

gendef FTD2XX64.dll
x86_64-w64-mingw32-dlltool -d FTD2XX64.def -l libftd2xx.a -D FTD2XX64.dll