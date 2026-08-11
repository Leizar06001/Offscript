#ifndef JSON_MIN_H
#define JSON_MIN_H

#include <stddef.h>

/* Minimal, self-contained JSON: just enough to build small request bodies
 * and pull a few fields back out of API responses. Not a general-purpose
 * parser (e.g. no strict validation), but never spins on malformed input. */

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonType;

typedef struct JsonValue {
    JsonType type;
    union {
        int boolean;
        double number;
        char *string;
        struct { struct JsonValue **items; size_t count; } array;
        struct { char **keys; struct JsonValue **values; size_t count; } object;
    } as;
} JsonValue;

/* Parses `text` and returns the root value (NULL on empty/unparseable
 * input); free with json_free. */
JsonValue *json_parse(const char *text);
void json_free(JsonValue *v);

/* Reads a whole file and parses it. Returns NULL if unreadable or
 * unparseable. */
JsonValue *json_parse_file(const char *path);

/* NULL-safe: returns NULL if obj isn't a JSON_OBJECT or key is absent. */
JsonValue *json_object_get(JsonValue *obj, const char *key);
/* NULL-safe: returns NULL if v isn't a JSON_STRING. */
const char *json_string(JsonValue *v);

/* Every accessor below is NULL-safe and type-checked, returning the given
 * fallback when the value is absent or of another type. Story files are
 * hand-written, so a missing key has to be survivable everywhere. */
const char *json_string_or(JsonValue *v, const char *fallback);
double      json_number_or(JsonValue *v, double fallback);
int         json_int_or(JsonValue *v, int fallback);
int         json_bool_or(JsonValue *v, int fallback);

/* Arrays. json_array_count returns 0 for anything that is not an array,
 * so `for (i = 0; i < json_array_count(v); i++)` is always safe. */
int        json_array_count(JsonValue *v);
JsonValue *json_array_get(JsonValue *v, int index);

/* Objects used as maps (e.g. the story's "facts", keyed by fact id):
 * iterate with json_object_count + json_object_key_at/json_object_value_at. */
int         json_object_count(JsonValue *v);
const char *json_object_key_at(JsonValue *v, int index);
JsonValue  *json_object_value_at(JsonValue *v, int index);

/* Convenience: malloc'd copy of a string field, or NULL when absent.
 * json_strdup_array copies a whole array of strings and sets *count. */
char  *json_strdup(JsonValue *v);
char **json_strdup_array(JsonValue *v, int *count);
void   json_free_string_array(char **arr, int count);

/* Returns a malloc'd copy of `text` with JSON string-escaping applied
 * (no surrounding quotes). */
char *json_escape(const char *text);

#endif
