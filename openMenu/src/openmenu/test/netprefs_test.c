/* netprefs_test: pack / unpack of the file tail, the per-game network, the stars, the limits.
 *   gcc -Wall -Iopenmenu/src -o netprefs_test openmenu/test/netprefs_test.c openmenu/src/netprefs.c && ./netprefs_test */
#include <stdio.h>
#include <string.h>

#include "netprefs.h"

static int bad, hooked;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); bad++; } } while (0)
static void hook(void) { hooked++; }

int main(void) {
    static uint8_t buf[NP_MAX_BYTES];
    np_reset();
    np_set_changed_hook(hook);
    CHECK(np_iface() == 0 && np_net() == NP_NET_DCNOW && np_game_net(5) == NP_NET_DCNOW);
    np_set_iface(3);
    np_set_net(NP_NET_DCNET);
    CHECK(hooked == 2 && np_iface() == 3 && np_game_net(5) == NP_NET_DCNET);
    np_set_iface(3);
    CHECK(hooked == 2); /* no change, no save */
    CHECK(np_set_game_net(5, NP_NET_DCNOW) == 0 && np_game_net(5) == NP_NET_DCNOW && np_game_net(6) == NP_NET_DCNET);
    CHECK(np_set_game_net(5, NP_NET_NONE) == 0 && np_game_net_saved(5) == NP_NET_NONE);
    for (uint32_t h = 100; h < 100 + NP_GAME_NETS; h++) CHECK(np_set_game_net(h, NP_NET_DCNOW) == 0);
    CHECK(np_set_game_net(999, NP_NET_DCNOW) == -1);
    np_isp_t isp; memset(&isp, 0, sizeof(isp));
    isp.mode = NP_ISP_STATIC; isp.ip[0] = 192; isp.ip[3] = 77; strcpy(isp.number, "5551234"); strcpy(isp.login, "me");
    np_set_isp(&isp);
    CHECK(np_player_toggle("Alice") == 1 && np_player_starred("Alice") && !np_player_starred("Bob"));
    CHECK(np_player_toggle("A very long player name here") == 1);
    CHECK(np_game_toggle(0xDEADBEEF) == 1 && np_game_starred(0xDEADBEEF));
    CHECK(np_event_toggle("DC99 Friday") == 1 && np_event_starred("DC99 Friday") && !np_event_starred("other"));
    np_set_chat("Dreamer", "throwaway");
    int n = np_pack(buf, NP_MAX_BYTES);
    CHECK(n > 0 && n <= NP_MAX_BYTES);
    CHECK(np_pack(buf, 10) == 0);
    np_reset();
    CHECK(np_iface() == 0 && !np_player_starred("Alice"));
    CHECK(np_unpack(buf, n) == 0);
    CHECK(np_iface() == 3 && np_net() == NP_NET_DCNET && np_game_net_saved(100 + 7) == NP_NET_DCNOW);
    CHECK(np_isp()->mode == NP_ISP_STATIC && np_isp()->ip[3] == 77 && strcmp(np_isp()->number, "5551234") == 0 && strcmp(np_isp()->login, "me") == 0);
    CHECK(np_player_starred("Alice") && np_star_player_count() == 2 && np_game_starred(0xDEADBEEF) && np_event_starred("DC99 Friday"));
    CHECK(!strcmp(np_chat_nick(), "Dreamer") && !strcmp(np_chat_pass(), "throwaway"));
    { /* a file from before the chat login: it still loads, the login is empty */
        CHECK(np_unpack(buf, n - 2 * NP_CHAT_LEN) == 0 && np_chat_nick()[0] == 0 && np_chat_pass()[0] == 0 && np_player_starred("Alice"));
        CHECK(np_unpack(buf, n) == 0 && !strcmp(np_chat_nick(), "Dreamer"));
        np_set_chat("A-nick-that-is-far-too-long-for-irc", "p");
        CHECK(strlen(np_chat_nick()) == NP_CHAT_LEN - 1);
        np_set_chat("Dreamer", "throwaway");
    }
    /* toggling off */
    CHECK(np_player_toggle("Alice") == 0 && !np_player_starred("Alice") && np_star_player_count() == 1);
    CHECK(np_game_toggle(0xDEADBEEF) == 0 && np_star_game_count() == 0);
    /* limits */
    for (int i = 0; i < NP_STAR_PLAYERS; i++) { char nm[8]; sprintf(nm, "p%d", i); np_player_toggle(nm); }
    CHECK(np_player_toggle("onemore") == -1);
    /* garbage and short files leave the defaults */
    CHECK(np_unpack(buf, 5) == -1 && np_iface() == 0);
    buf[0] = 'X'; CHECK(np_unpack(buf, n) == -1);
    buf[0] = 'N'; CHECK(np_unpack(buf, n - 2 * NP_CHAT_LEN - 3) == -1 && np_star_player_count() == 0); /* truncated tail */
    printf(bad ? "%d failed\n" : "netprefs ok\n", bad);
    return bad != 0;
}
