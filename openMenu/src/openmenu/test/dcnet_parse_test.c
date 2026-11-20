/* dcnet_parse_test.   gcc -Wall -Isrc -o dcnet_parse_test test/dcnet_parse_test.c src/backend/dcnet_parse.c && ./dcnet_parse_test   (from openmenu/) */
#include <stdio.h>
#include <string.h>
#include "backend/dcnet_parse.h"
static int bad;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); bad++; } } while (0)
int main(void) {
    /* the shapes the maintainer's endpoints returned */
    const char* users = "[{\"userName\":\"flycast1\",\"dcnetIp\":\"172.20.0.10\",\"date\":1789870637984,\"accessPoint\":\"USE\","
                        "\"geoloc\":{\"continent\":\"NA\",\"country\":\"US\",\"timeZone\":\"America/Chicago\"}},"
                        "{\"userName\":\"Anna\\u00e9\",\"geoloc\":{\"country\":\"SE\"}}]";
    const char* games = "[{\"gameId\":\"aerof\",\"name\":\"Aero Dancing F\",\"thumbnail\":\"https://x/a.jpg\",\"timestamp\":1795173640,"
                        "\"online\":true,\"playerCount\":2,\"gameCount\":1},{\"gameId\":\"vf4\",\"name\":\"Virtua Fighter 4\",\"online\":true},"
                        "{\"gameId\":\"off\",\"name\":\"Off\",\"online\":false,\"playerCount\":9}]";
    dcnow_player_t p[8];
    dcnet_game_t g[8];
    int n = 0, total = 0;

    CHECK(dcnet_parse_users(users, p, &n, 8, &total) == 0 && n == 2 && total == 2);
    CHECK(!strcmp(p[0].name, "flycast1") && !strcmp(p[0].country, "US") && !strcmp(p[0].network, "DCNET"));
    CHECK(!strcmp(p[1].name, "Anna?") && !strcmp(p[1].country, "SE")); /* é is not ASCII */
    /* the cap: counted but not stored */
    n = 0; total = 0;
    CHECK(dcnet_parse_users(users, p, &n, 1, &total) == 0 && n == 1 && total == 2);
    n = 0; total = 0;
    CHECK(dcnet_parse_users("[]", p, &n, 8, &total) == 0 && n == 0 && total == 0);
    CHECK(dcnet_parse_users("{\"a\":1}", p, &n, 8, &total) == -1);
    CHECK(dcnet_parse_users("", p, &n, 8, &total) == -1);
    /* players: marks the connected one as playing, appends one /users did not list */
    n = 0; total = 0;
    dcnet_parse_users(users, p, &n, 8, &total);
    n = dcnet_apply_players("[{\"userName\":\"flycast1\",\"gameId\":\"aerof\",\"game\":\"Aero Dancing F\"},{\"userName\":\"new\",\"game\":\"X\"}]", p, n, 8, &total);
    CHECK(n == 3 && total == 3 && !strcmp(p[0].title, "Aero Dancing F") && !strcmp(p[0].code, "aerof") && !strcmp(p[2].name, "new") && !strcmp(p[2].title, "X"));
    CHECK(dcnet_apply_players("[]", p, 3, 8, &total) == 3);
    CHECK(dcnet_apply_players("nope", p, 3, 8, &total) == -1);
    /* games */
    CHECK(dcnet_parse_games(games, g, 8) == 2);
    CHECK(!strcmp(g[0].id, "aerof") && !strcmp(g[0].name, "Aero Dancing F") && g[0].players == 2 && g[0].games == 1);
    CHECK(!strcmp(g[1].id, "vf4") && g[1].players == 0); /* the fields can be missing */
    /* a body cut in the middle keeps what was read */
    n = 0; total = 0;
    CHECK(dcnet_parse_users("[{\"userName\":\"a\"},{\"userName\":\"b\",\"geo", p, &n, 8, &total) == 0 && n == 1);
    printf(bad ? "%d failed\n" : "dcnet_parse ok\n", bad);
    return bad != 0;
}
