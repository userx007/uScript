#!/bin/bash

CLANG_FORMAT="$HOME/.local/bin/clang-format"

find . \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -exec "$CLANG_FORMAT" -i {} +