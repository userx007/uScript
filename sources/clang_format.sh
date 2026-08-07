#!/bin/bash

# sudo apt install clang-format

find . \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -exec clang-format -i {} +

# alternative
# find . -regex '.*\.\(cpp\|cc\|cxx\|h\|hpp\)' -exec clang-format -i {} +

# Format only Git-tracked files
# git ls-files '*.cpp' '*.cc' '*.cxx' '*.h' '*.hpp' | xargs clang-format -i

# disable formating inside some files
# // clang-format off
# ...
#// clang-format on