/*
 * ui_ctrl: the controller mapper and editor screen (logic: ctrl_editor.c). Opened from the File screen when A is pressed on a controller
 * in the top row of the card grid (uif_pad_picked()). It talks the DC API to the adapter on that port (iBlueControlMod, Pico2Maple, ...) on
 * the port's controller unit, never a memory card: one "get hello" when A is pressed, nothing while the File screen is only open. For an
 * iBlue it lists the port's type, accessory, layout and buttons, and for a port set to Maracas the Joy-Con calibration page with the little
 * man whose arms follow the Joy-Cons (pose_man.c); for a Pico2Maple its display, memory card and pad settings.
 */
#pragma once

#include <bios_menu.h>

#include "input.h"

/* Open the screen for port 0..3 (replaces the scene of `menu`). Returns 0 when no adapter on that port speaks the language: the caller says so
 * (uic_why() gives the words) and stays where it is. */
int uic_open(bmenu* menu, int port, int pal);
const char* uic_why(void);

/* One button. Returns 1 when the screen has been left. */
int uic_handle(button_t b);
/* One 60 Hz step (after bmenu_update()) */
void uic_step(void);
/* Once per frame after the logic steps */
void uic_sync(void);
/* Everything of the screen (the caller has drawn the background) */
void uic_draw(void);

/* What the adapter opened last calls itself, short ("iBlue", "Pico2Maple"): the File screen's pill for that controller. */
const char* uic_adapter_name(void);
