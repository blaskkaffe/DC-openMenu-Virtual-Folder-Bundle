# DC API client in the openMenu fork

The Dreamcast side of the control language `dc1` shared by the DC- projects (design and decisions: [DC-UnifiedEcosystem](https://github.com/blaskkaffe/DC-UnifiedEcosystem), `docs/04-control-channel.md`).

**Status: written. The text side (`vm2/dcapi_text.c`) is host-tested (`sh openMenu/tests/host/run.sh`, 25 checks). The Maple side (`vm2/dcapi.c`) and the hook in `vm2_rescan()` were only syntax-checked against a stub of the KallistiOS headers: never built with KallistiOS, never run on a Dreamcast.**

## What it does now

- **Discovery** (in `vm2_rescan()`, main.c): the ALLINFO answer openMenu already asks for carries, behind the 16 byte device marker, `dc1 ` and the capability bits as 8 hex digits (`dc1 00000023` from the iBlue). `get_last_allinfo_dcapi_caps()` reads them; `vm2_dc_caps[]` keeps them next to `vm2_devices[]`; `vm2_get_dc_caps(dev)` answers for one device. VM2, VMU Pro, USB4Maple and Pico2Maple say nothing there and are not asked anything.
- **Hello:** for a device whose bits include `api`, one request `get hello` (`dcapi_get_hello()`) fills `vm2_dc_hello[]` (name, version) and a line goes to the debug log: `DC API: iBlueControlMod <version> on A1 (caps 00000023)`.
- **Requests:** `dcapi_request(dev, text, answer, size)` sends Maple command `0x22` to the memory card unit with the request text after the memory card function code (text in memory order, NUL padded, like the game id), waits 200 ms like `vm2_set_id()`, retries on "again", and returns the answer text. `dcapi_set()` is the `set <key> <value>` shortcut. Return codes say whether the device timed out, only acknowledged (it does not speak the language) or answered something else.
- **`vm2_dc_set_network("dcnow"|"dcnet")`** writes `net.selected` to every device with the `net` capability. **Not called yet**: openMenu does not read the DreamPi's `NET` line yet (OM-P11). It is the direct route of decision D-12; with a DreamPi the Pi can pass the network on over Bluetooth instead.

## Not done

- No screen shows any of it. The natural places are the controllers/VMU screen (the type name "iBlue" is shown there) and a per-device page listing only what the device's capabilities offer.
- No client for `POST /dcapi` on the DreamPi (the DreamPi hosts the communication when there is one).
- The Maple frame layout (request word count, the answer in a data frame) is the one written in the iBlue firmware's `docs/dcapi.md`; both ends were written from the same description and neither has met the other on a bus.
