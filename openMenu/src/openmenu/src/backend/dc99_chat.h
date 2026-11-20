/*
 * File: dc99_chat.h
 * Project: openmenu
 * The two DC99 chats on worker threads: the IRC channel #dc99 (irc.dc99.net:6667, plain text, the nick registers with its password on first
 * use) and the KosNet global chat, read through DC99's plain-HTTP feed (dc99.net/online/kosnet_chat_status.php, no login, read only: posting
 * to KosNet needs the user's own KosNet account over TLS). The text handling is dc99_chat_parse.
 */

#pragma once

#include <stdint.h>

enum { DC99_CHAT_IRC = 0, DC99_CHAT_KOSNET = 1, DC99_CHATS };
enum { DC99_CHAT_OFF = 0, DC99_CHAT_CONNECTING, DC99_CHAT_ON, DC99_CHAT_FAILED };
enum { DC99_ROW_MSG = 0, DC99_ROW_SYS, DC99_ROW_SELF, DC99_ROW_ERR };

#define DC99_ROW_CHARS 44 /* what a row of the Online page shows */
#define DC99_ROWS 120

typedef struct dc99_row {
    char text[DC99_ROW_CHARS + 4];
    uint8_t kind;
} dc99_row_t;

typedef struct dc99_chat_status {
    int state;
    int generation; /* grows with every row that arrives */
    int rows;
    char note[64]; /* what is wrong, or "" */
} dc99_chat_status_t;

/* KosNet: starts the polling (every few seconds); does nothing when it runs. */
void dc99_kosnet_start(void);
/* IRC: connects and joins #dc99 with the login (nick 1-20 characters, a password). Does nothing while connected or connecting. */
void dc99_irc_start(const char* nick, const char* pass);
/* One chat line to the channel. Returns 0 when queued, -1 when not connected or one is still waiting to go out. */
int dc99_irc_send(const char* text);
/* Hangs up and stops both, waits for the threads. The rows are kept. */
void dc99_chat_stop(void);
/* Stops one chat only (IRC: leaves the channel). */
void dc99_irc_stop(void);

void dc99_chat_status(int which, dc99_chat_status_t* out);
/* Copies the newest rows, oldest first. Returns how many. */
int dc99_chat_copy(int which, dc99_row_t* out, int max);
