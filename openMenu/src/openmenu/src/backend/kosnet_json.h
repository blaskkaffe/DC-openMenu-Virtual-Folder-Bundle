/*
 * File: kosnet_json.h
 * Project: openmenu
 * The little JSON the KosNet login and post calls need (no KOS, host-testable): escaping a text into a JSON string, reading a string
 * value out of an answer, and telling the error codes apart.
 */

#pragma once

#include <stddef.h>

/* `in` as the inside of a JSON string (quotes, backslashes, control characters; non-ASCII bytes pass). Returns the length, or -1 when out is too small. */
int kn_json_escape(char* out, size_t size, const char* in);
/* The string value of "key" (the first one anywhere in json, unescaped for \" \\ \/ \n \t and \uXXXX as '?'). Returns its length, -1 when absent. */
int kn_json_str(const char* json, const char* key, char* out, size_t size);
enum { KN_ERR_NONE = 0, KN_ERR_CREDENTIALS, KN_ERR_EMAIL, KN_ERR_RATE, KN_ERR_OTHER };
/* The kind of error in an answer body (its code strings), KN_ERR_OTHER when it has none of the known ones. */
int kn_error_kind(const char* body);
/* The HTTP status line and the start of the body of a whole response: the status, and *body at the text after the blank line (NULL: none). -1: no status line. */
int kn_http_status(const char* resp, const char** body);
