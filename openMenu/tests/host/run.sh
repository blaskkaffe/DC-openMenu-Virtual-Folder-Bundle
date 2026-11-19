#!/bin/sh
# Host tests of the portable openMenu code (no KallistiOS). Usage: sh openMenu/tests/host/run.sh
# IBLUE_FW=<an iBlueControlMod checkout with the DC API motion keys> also runs the Joy-Con motion page against the firmware's own engine.
set -e
d=$(cd "$(dirname "$0")" && pwd)
src="$d/../../src/openmenu/src"
out=$(mktemp -d)
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I"$src" -o "$out/dcapi_text" "$d/test_dcapi_text.c" "$src/vm2/dcapi_text.c"
"$out/dcapi_text"
if [ -n "$IBLUE_FW" ]; then
    fw="$IBLUE_FW/main"
    cc -Wall -Wextra -Werror -fsanitize=address,undefined -DWITH_ENGINE -I"$src" -I"$fw/tests/host/stubs" -I"$fw" \
        -o "$out/ui_motion" "$d/test_ui_motion.c" "$src/ui_motion.c" "$src/vm2/dcapi_text.c" \
        "$fw/iblue/iblue_dcapi.c" "$fw/iblue/iblue_gameid.c" "$fw/iblue/iblue_mapcfg.c"
else
    cc -Wall -Wextra -Werror -fsanitize=address,undefined -I"$src" -o "$out/ui_motion" "$d/test_ui_motion.c" "$src/ui_motion.c" "$src/vm2/dcapi_text.c"
fi
"$out/ui_motion"
if [ -n "$IBLUE_FW" ]; then
    cc -Wall -Wextra -Werror -fsanitize=address,undefined -DWITH_ENGINE -I"$src" -I"$fw/tests/host/stubs" -I"$fw" \
        -o "$out/ctrl_editor" "$d/test_ctrl_editor.c" "$src/ctrl_editor.c" "$src/ui_motion.c" "$src/vm2/dcapi_text.c" \
        "$fw/iblue/iblue_dcapi.c" "$fw/iblue/iblue_gameid.c" "$fw/iblue/iblue_mapcfg.c"
else
    cc -Wall -Wextra -Werror -fsanitize=address,undefined -I"$src" -o "$out/ctrl_editor" "$d/test_ctrl_editor.c" "$src/ctrl_editor.c" "$src/ui_motion.c" "$src/vm2/dcapi_text.c"
fi
"$out/ctrl_editor"
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I"$src" -o "$out/pose_man" "$d/test_pose_man.c" "$src/pose_man.c" -lm
"$out/pose_man"
rm -rf "$out"
