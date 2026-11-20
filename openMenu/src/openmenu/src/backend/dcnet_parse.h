/*
 * File: dcnet_parse.h
 * Project: openmenu
 * The DCNet status endpoints (dcnet.flyca.st/status/api/...): pure text parsing, host-tested (test/dcnet_parse_test.c).
 * Documented by their maintainer: /users = [{"userName","dcnetIp","date","accessPoint","geoloc":{"continent","country","timeZone"}}] (who is
 * connected), /players = the ones in a game (empty when nobody plays; the entry fields are not known to this code: it reads userName / username /
 * name / player and gameId / game / gameName / title tolerantly), /games = [{"gameId","name","thumbnail","timestamp","online","playerCount",
 * "gameCount"}] (playerCount and gameCount may be missing).
 */

#pragma once

#include "backend/dcnow_fetch.h"

#define DCNET_GAME_MAX 64

typedef struct dcnet_game {
    char id[16];
    char name[DCNOW_TITLE_LEN];
    int players;
    int games;
} dcnet_game_t;

/* Appends the connected users to out[*n] (network "DCNET", title empty) up to max, counts every one in *total. Returns 0, or -1 when the text
 * is not a JSON array. */
int dcnet_parse_users(const char* json, dcnow_player_t* out, int* n, int max, int* total);

/* Marks the entries of out[0..n) whose name is in the /players text as playing: title and code from the entry. A player that is not in out is
 * appended (they are connected, whatever /users says). Returns the new count, or -1 when the text is not a JSON array. */
int dcnet_apply_players(const char* json, dcnow_player_t* out, int n, int max, int* total);

/* The games list. Returns how many were stored, -1 when not a JSON array. Offline games are skipped. */
int dcnet_parse_games(const char* json, dcnet_game_t* out, int max);
