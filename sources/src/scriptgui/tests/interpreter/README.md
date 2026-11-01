# Interpreter-side extension tests

Standalone programs (no full project build needed) that exercise the real interpreter headers.
Run from this folder; `SRC` is the repository's `src` directory, `ROOT` its parent.

```
SRC=../../..; ROOT=$SRC/..
INC=$(find $SRC $ROOT/include -type d \( -name inc -o -name include \) -not -path '*/third_party/*' | sed 's/^/-I/')

# helper + INCLUDE rule (.main only)
g++ -std=c++20 $INC extension_rules_test.cpp -o ext_test -lpthread && ./ext_test

# ScriptClient refuses anything but .main  /  CommScriptClient refuses anything but .comm
SRCS="$(find $SRC/scripts/core $SRC/scripts/shared $SRC/scripts/comm $SRC/libs/utils -name '*.cpp' -not -path '*test*')"
g++ -std=c++20 $INC script_client_guard_test.cpp $SRCS -o sc_test -lpthread -ldl && ./sc_test
g++ -std=c++20 $INC comm_client_guard_test.cpp  $SRCS -o cc_test -lpthread -ldl && ./cc_test
```
