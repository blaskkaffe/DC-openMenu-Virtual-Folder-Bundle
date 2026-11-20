/* dc99_chat_parse_test.   gcc -Wall -Isrc -o t test/dc99_chat_parse_test.c src/backend/dc99_chat_parse.c && ./t   (from openmenu/) */
#include <stdio.h>
#include <string.h>
#include "backend/dc99_chat_parse.h"
static int bad;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); bad++; } } while (0)
int main(void) {
    /* KosNet feed, the shape the live site returned (plus unicode, an escaped quote, a } and a fake "body": inside a message) */
    const char* feed = "{\"ok\":true,\"next_cursor\":\"707\",\"messages\":["
        "{\"id\":701,\"kind\":\"message\",\"body\":\"Hello World!\",\"display_name\":\"enfilade\",\"game_name\":null,\"created_at\":1794165870},"
        "{\"id\":703,\"kind\":\"join\",\"body\":\"'Quinsided' joined the chat. Say Hi!\",\"display_name\":\"Quinsided\",\"game_name\":null,\"created_at\":1794265008},"
        "{\"id\":704,\"kind\":\"message\",\"body\":\"caf\\u00e9 \\ud83d\\ude00 \\\"hi\\\" a\\/b } \\\"body\\\": x\",\"display_name\":\"An\\u00e9\",\"game_name\":\"Sonic Adventure 2\",\"created_at\":1}"
        "]}";
    dc99_kn_msg_t m[DC99_KN_MAX];
    char cur[32];
    int n = dc99_kn_parse(feed, m, DC99_KN_MAX, cur, sizeof(cur));
    CHECK(n == 3 && !strcmp(cur, "707"));
    CHECK(!strcmp(m[0].id, "701") && !m[0].join && !strcmp(m[0].name, "enfilade") && !strcmp(m[0].body, "Hello World!") && m[0].game[0] == 0);
    CHECK(m[1].join && !strcmp(m[1].name, "Quinsided"));
    CHECK(!strcmp(m[2].body, "caf? ? \"hi\" a/b } \"body\": x") && !strcmp(m[2].name, "An?") && !strcmp(m[2].game, "Sonic Adventure 2"));
    CHECK(dc99_kn_parse("{\"ok\":false}", m, DC99_KN_MAX, cur, sizeof(cur)) == -2);
    CHECK(dc99_kn_parse("<html>", m, DC99_KN_MAX, cur, sizeof(cur)) == -1);
    /* cut off in the middle of a message: the whole ones stay */
    char cut[400];
    snprintf(cut, sizeof(cut), "%.*s", 300, feed);
    { int c = dc99_kn_parse(cut, m, DC99_KN_MAX, cur, sizeof(cur)); CHECK(c >= 1 && c < 3); }
    CHECK(dc99_kn_parse(feed, m, 2, cur, sizeof(cur)) == 2);
    char path[80];
    dc99_kn_path("707", path, sizeof(path));
    CHECK(!strcmp(path, "/online/kosnet_chat_status.php?cursor=707"));
    dc99_kn_path("7x", path, sizeof(path));
    CHECK(!strcmp(path, "/online/kosnet_chat_status.php"));
    dc99_kn_path("", path, sizeof(path));
    CHECK(!strcmp(path, "/online/kosnet_chat_status.php"));

    /* IRC */
    dc99_irc_ev_t ev;
    char l1[] = "PING :abc123";
    CHECK(dc99_irc_parse(l1, &ev) == 1 && ev.type == DC99_IRCE_NONE && !strcmp(ev.reply, "PONG :abc123"));
    char l2[] = ":irc.dc99.net 001 Nick :Welcome";
    CHECK(dc99_irc_parse(l2, &ev) == 1 && ev.type == DC99_IRCE_READY && !strcmp(ev.reply, "JOIN #dc99"));
    char l3[] = ":Bob!b@host PRIVMSG #dc99 :\x03" "04red\x02 bold\x0f hello \xc3\xa9";
    CHECK(dc99_irc_parse(l3, &ev) == 1 && ev.type == DC99_IRCE_MSG && !strcmp(ev.nick, "Bob") && !strcmp(ev.text, "red bold hello ?"));
    char l4[] = ":Bob!b@host PRIVMSG #dc99 :\x01" "ACTION waves\x01";
    CHECK(dc99_irc_parse(l4, &ev) == 1 && ev.type == DC99_IRCE_ACTION && !strcmp(ev.text, "waves"));
    char l5[] = ":Bob!b@host PRIVMSG #dc99 :\x01VERSION\x01";
    CHECK(dc99_irc_parse(l5, &ev) == 0);
    char l6[] = ":dc99status!b@h PRIVMSG #dc99 :count";
    CHECK(dc99_irc_parse(l6, &ev) == 0);
    char l7[] = ":Bob!b@host PRIVMSG Nick :psst";
    CHECK(dc99_irc_parse(l7, &ev) == 0);
    char l8[] = ":s 464 * :Password incorrect";
    CHECK(dc99_irc_parse(l8, &ev) == 1 && ev.type == DC99_IRCE_ERROR && strstr(ev.text, "Wrong password"));
    char l9[] = ":Bob!b@h JOIN #dc99";
    CHECK(dc99_irc_parse(l9, &ev) == 1 && ev.type == DC99_IRCE_JOIN && !strcmp(ev.nick, "Bob"));
    char l10[] = ":s NOTICE * :\x01" "DC99COLOR 1\x01";
    CHECK(dc99_irc_parse(l10, &ev) == 0);
    char l11[] = "ERROR :Closing link";
    CHECK(dc99_irc_parse(l11, &ev) == 1 && ev.type == DC99_IRCE_ERROR && !strcmp(ev.text, "Closing link"));
    char out[480];
    CHECK(dc99_irc_say_line("hi\r\nthere  ", out, sizeof(out)) == 0 && !strcmp(out, "PRIVMSG #dc99 :hi  there"));
    CHECK(dc99_irc_say_line("   ", out, sizeof(out)) == -1);

    /* wrap */
    char lines[6][48];
    int k = dc99_chat_wrap("Bob: ", "the quick brown fox jumps over the lazy dog and keeps running far away", 24, &lines[0][0], 6, 48);
    CHECK(k == 4 && !strcmp(lines[0], "Bob: the quick brown fox") && lines[1][0] == ' ' && lines[1][1] == ' ');
    for (int i = 0; i < k; i++) CHECK(strlen(lines[i]) <= 24);
    k = dc99_chat_wrap("", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 20, &lines[0][0], 6, 48);
    CHECK(k == 3 && strlen(lines[0]) == 20 && strlen(lines[1]) == 20);
    k = dc99_chat_wrap("x: ", "ok", 40, &lines[0][0], 6, 48);
    CHECK(k == 1 && !strcmp(lines[0], "x: ok"));
    printf(bad ? "dc99_chat_parse FAILED\n" : "dc99_chat_parse ok\n");
    return bad != 0;
}
