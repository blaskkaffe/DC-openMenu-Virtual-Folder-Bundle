#!/bin/sh
# Host tests of the portable openMenu code (no KallistiOS). Usage: sh openMenu/tests/host/run.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I"$d/../../src/openmenu/src" -o "$out/dcapi_text" "$d/test_dcapi_text.c" "$d/../../src/openmenu/src/vm2/dcapi_text.c"
"$out/dcapi_text"
rm -rf "$out"
