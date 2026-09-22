#!/bin/bash

CLANG_FORMAT="clang-format"

find . \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -exec "$CLANG_FORMAT" -i {} +