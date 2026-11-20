/*
 * File: dc99_chat_parse.h
 * Project: openmenu
 * The two DC99 chats, pure text handling (host-tested with test/dc99_chat_parse_test.c): the KosNet global chat as DC99's plain-HTTP feed
 * publishes it (dc99.net/online/kosnet_chat_status.php) and the IRC lines of #dc99 (irc.dc99.net:6667). The scanners follow the maintainer's
 * reference clients (dc99_kosnet_chat.c, dc99_irc.c); the sockets are in dc99_chat.c.
 */

#pragma once

#include <stddef.h>

#define DC99_KN_MAX 40

typedef struct dc99_kn_msg {
    char id[16];
    int join;       /* a "so-and-so joined" line, not a person talking */
    char name[28];
    char game[28];  /* what they were playing, "" for nothing */
    char body[244];
} dc99_kn_msg_t;

/* Reads the feed's JSON (text after the HTTP header). Returns how many messages were stored (oldest first), -1 when the text is not the
 * feed, -2 when DC99 answers but KosNet itself is down ("ok":false). cursor gets the next_cursor ("" when none). Text comes out ASCII. */
int dc99_kn_parse(const char* json, dc99_kn_msg_t* out, int max, char* cursor, size_t cursor_cap);

/* The path to fetch: with the cursor when it is digits only, else the latest. */
void dc99_kn_path(const char* cursor, char* out, size_t cap);

typedef enum {
    DC99_IRCE_NONE = 0,
    DC99_IRCE_READY, /* welcome received: reply holds the JOIN to send */
    DC99_IRCE_MSG,
    DC99_IRCE_ACTION,
    DC99_IRCE_JOIN,
    DC99_IRCE_PART,
    DC99_IRCE_QUIT,
    DC99_IRCE_NOTICE,
    DC99_IRCE_ERROR /* text says what is wrong (wrong password, nick in use...) */
} dc99_irc_type_t;

typedef struct dc99_irc_ev {
    dc99_irc_type_t type;
    char nick[28];
    char text[240];
    char reply[80]; /* a line to send back (PONG, JOIN), "" for none */
} dc99_irc_ev_t;

/* One IRC line (changed in place, no line end). Returns 1 when ev says something to show or send (type, or reply), 0 to ignore. The
 * counting bot dc99status and CTCP other than /me are left out; text comes out ASCII without colour codes. */
int dc99_irc_parse(char* line, dc99_irc_ev_t* ev);

/* A line to send for the user's text: "PRIVMSG #dc99 :text" with line ends removed and long text cut. Returns 0, or -1 for empty text. */
int dc99_irc_say_line(const char* text, char* out, size_t cap);

/* IRC colour, bold and underline codes out, anything not ASCII (a UTF-8 sequence is one) becomes '?', control characters become blanks. */
void dc99_chat_clean(char* s);

/* Wraps "prefix text" to lines of at most width characters (words kept whole where they fit, continuation lines start with two blanks).
 * Returns how many lines were written to lines[max][cap]. */
int dc99_chat_wrap(const char* prefix, const char* text, int width, char* lines, int max, int cap);
