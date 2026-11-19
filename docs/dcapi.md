# DC API client in the openMenu fork

The Dreamcast side of the control language `dc1` shared by the DC- projects (design and decisions: [DC-UnifiedEcosystem](https://github.com/blaskkaffe/DC-UnifiedEcosystem), `docs/04-control-channel.md`).

**Status: written. The text side (`vm2/dcapi_text.c`) is host-tested (`sh openMenu/tests/host/run.sh`, 25 checks). The Maple side (`vm2/dcapi.c`) and the hook in `vm2_rescan()` were only syntax-checked against a stub of the KallistiOS headers: never built with KallistiOS, never run on a Dreamcast.**

## What it does now

- **Discovery** (in `vm2_rescan()`, main.c): the ALLINFO answer openMenu already asks for carries, behind the 16 byte device marker, `dc1 ` and the capability bits as 8 hex digits (`dc1 00000023` from the iBlue). `get_last_allinfo_dcapi_caps()` reads them; `vm2_dc_caps[]` keeps them next to `vm2_devices[]`; `vm2_get_dc_caps(dev)` answers for one device. VM2, VMU Pro, USB4Maple and Pico2Maple say nothing there and are not asked anything.
- **Hello:** for a device whose bits include `api`, one request `get hello` (`dcapi_get_hello()`) fills `vm2_dc_hello[]` (name, version) and a line goes to the debug log: `DC API: iBlueControlMod <version> on A1 (caps 00000023)`.
- **Requests:** `dcapi_request(dev, text, answer, size)` sends Maple command `0x22` to the memory card unit with the request text after the memory card function code (text in memory order, NUL padded, like the game id), waits 200 ms like `vm2_set_id()`, retries on "again", and returns the answer text. `dcapi_set()` is the `set <key> <value>` shortcut. Return codes say whether the device timed out, only acknowledged (it does not speak the language) or answered something else.
- **`vm2_dc_set_network("dcnow"|"dcnet")`** writes `net.selected` to every device with the `net` capability. **Not called yet**: openMenu does not read the DreamPi's `NET` line yet (OM-P11). It is the direct route of decision D-12; with a DreamPi the Pi can pass the network on over Bluetooth instead.

## The controller editor (`ctrl_editor.c`, `ui_ctrl.c`): what it asks for

File screen, A on a controller. The port page, in the owner's order of importance (2026-11-19):

| Row | Keys | Shown |
|---|---|---|
| Type | `port.<X>.mode`, `.modes` | always |
| Memory card | `port.<X>.acc` (the memory card half of `none,vmu,rumble,both`) | always |
| Memory card slot | `port.<X>.vmu.slot`, `port.<X>.vmu.slots` (**proposed**: the slot shown, the slots accepted, e.g. `1,2,...,10`) | only when the device answers both |
| Game id | `game.mode` (**proposed**: `on` / `off`, the device follows the game id or not) | only when the device answers it |
| Load game id | none: a game of the card is picked and its id goes to the port's memory card unit as Maple `0x21` (`vm2_set_id()`, as at a game's start) | always; for a DC API device the memory card has to be on |
| Rumble | `port.<X>.acc` (the rumble half) | always; a combination the device does not list in `.accs` says "Not on this device" |
| Layout, Buttons, Joy-Con motion, Save, Undo | as before | |

**Which device can use what** (checked 2026-11-19 against DC-iBlueControlMod `unified` 6c32142 and DC-Pico2Maple `unified` dc17edb / upstream 2026-04-05):

- **iBlueControlMod:** Type, Memory card, Rumble, Layout, Buttons, motion: yes (its engine; `sh openMenu/tests/host/run.sh` with `IBLUE_FW=<checkout>` runs the editor against it, 92 checks). Slot: no (one card, `docs/vmu.md`). Game id switch: it exists only as tap action 10, there is no `game.mode` key yet (firmware task). Load game id: yes, `0x21` loads that game's stored configuration (not a memory card) when the memory card accessory is on.
- **Pico2Maple:** no DC API in any firmware (no `dc1` marker, no `0x22`). Its layout, memory card slot, game id mode and rumble are button combos on the controller (Select + ...). The editor shows them as a page of hints. Load game id: yes, `0x21` loads that game's memory card while the adapter is in game id mode (slot past 10 / below 1, the display says GID).

## Not done

- The firmware side of the proposed keys: `game.mode` in iBlueControlMod (its tap action 10 already does it), slots and `game.mode` in Pico2Maple (no source: needs its author).
- No client for `POST /dcapi` on the DreamPi (the DreamPi hosts the communication when there is one).
- The Maple frame layout (request word count, the answer in a data frame) is the one written in the iBlue firmware's `docs/dcapi.md`; both ends were written from the same description and neither has met the other on a bus.
