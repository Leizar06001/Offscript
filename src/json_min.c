#include "json_min.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_free_children(JsonValue *v);

static const char *json_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static JsonValue *json_new(JsonType type) {
    JsonValue *v = calloc(1, sizeof(*v));
    v->type = type;
    return v;
}

static char *json_parse_raw_string(const char **p) {
    /* *p points at the opening quote. */
    const char *s = *p + 1;
    size_t cap = 32, len = 0;
    char *out = malloc(cap);
    while (*s && *s != '"') {
        char c = *s;
        if (c == '\\') {
            s++;
            switch (*s) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'u': {
                    /* Skip \uXXXX; not decoded (not needed for our use). */
                    s += 4;
                    c = '?';
                    break;
                }
                default: c = *s; break;
            }
            s++;
        } else {
            s++;
        }
        if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[len++] = c;
    }
    out[len] = '\0';
    if (*s == '"') s++;
    *p = s;
    return out;
}

static JsonValue *json_parse_value(const char **p) {
    *p = json_skip_ws(*p);
    char c = **p;
    if (c == '\0') return NULL;
    if (c == '"') {
        JsonValue *v = json_new(JSON_STRING);
        v->as.string = json_parse_raw_string(p);
        return v;
    }
    if (c == '{') {
        JsonValue *v = json_new(JSON_OBJECT);
        (*p)++;
        *p = json_skip_ws(*p);
        while (**p && **p != '}') {
            *p = json_skip_ws(*p);
            if (**p != '"') break;
            char *key = json_parse_raw_string(p);
            *p = json_skip_ws(*p);
            if (**p == ':') (*p)++;
            JsonValue *val = json_parse_value(p);
            v->as.object.keys = realloc(v->as.object.keys, sizeof(char *) * (v->as.object.count + 1));
            v->as.object.values = realloc(v->as.object.values, sizeof(JsonValue *) * (v->as.object.count + 1));
            v->as.object.keys[v->as.object.count] = key;
            v->as.object.values[v->as.object.count] = val;
            v->as.object.count++;
            *p = json_skip_ws(*p);
            if (**p == ',') { (*p)++; }
        }
        if (**p == '}') (*p)++;
        return v;
    }
    if (c == '[') {
        JsonValue *v = json_new(JSON_ARRAY);
        (*p)++;
        *p = json_skip_ws(*p);
        while (**p && **p != ']') {
            JsonValue *item = json_parse_value(p);
            v->as.array.items = realloc(v->as.array.items, sizeof(JsonValue *) * (v->as.array.count + 1));
            v->as.array.items[v->as.array.count++] = item;
            *p = json_skip_ws(*p);
            if (**p == ',') { (*p)++; *p = json_skip_ws(*p); }
        }
        if (**p == ']') (*p)++;
        return v;
    }
    if (c == 't' && strncmp(*p, "true", 4) == 0) { *p += 4; JsonValue *v = json_new(JSON_BOOL); v->as.boolean = 1; return v; }
    if (c == 'f' && strncmp(*p, "false", 5) == 0) { *p += 5; JsonValue *v = json_new(JSON_BOOL); v->as.boolean = 0; return v; }
    if (c == 'n' && strncmp(*p, "null", 4) == 0) { *p += 4; return json_new(JSON_NULL); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *start = *p;
        while (**p == '-' || **p == '+' || **p == '.' || **p == 'e' || **p == 'E' || (**p >= '0' && **p <= '9')) (*p)++;
        JsonValue *v = json_new(JSON_NUMBER);
        v->as.number = strtod(start, NULL);
        return v;
    }
    /* Malformed/unexpected token (e.g. a model returned prose instead of
     * JSON): always advance so callers can never spin forever on this. */
    (*p)++;
    return NULL;
}

JsonValue *json_parse(const char *text) {
    if (!text) return NULL;
    const char *p = text;
    return json_parse_value(&p);
}

static void json_free_children(JsonValue *v) {
    switch (v->type) {
        case JSON_STRING:
            free(v->as.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) json_free(v->as.array.items[i]);
            free(v->as.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->as.object.count; i++) {
                free(v->as.object.keys[i]);
                json_free(v->as.object.values[i]);
            }
            free(v->as.object.keys);
            free(v->as.object.values);
            break;
        default:
            break;
    }
}

void json_free(JsonValue *v) {
    if (!v) return;
    json_free_children(v);
    free(v);
}

JsonValue *json_object_get(JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.keys[i], key) == 0) return obj->as.object.values[i];
    }
    return NULL;
}

const char *json_string(JsonValue *v) {
    return (v && v->type == JSON_STRING) ? v->as.string : NULL;
}

const char *json_string_or(JsonValue *v, const char *fallback) {
    const char *s = json_string(v);
    return s ? s : fallback;
}

double json_number_or(JsonValue *v, double fallback) {
    return (v && v->type == JSON_NUMBER) ? v->as.number : fallback;
}

int json_int_or(JsonValue *v, int fallback) {
    return (v && v->type == JSON_NUMBER) ? (int)v->as.number : fallback;
}

int json_bool_or(JsonValue *v, int fallback) {
    if (!v) return fallback;
    if (v->type == JSON_BOOL) return v->as.boolean;
    return fallback;
}

int json_array_count(JsonValue *v) {
    return (v && v->type == JSON_ARRAY) ? (int)v->as.array.count : 0;
}

JsonValue *json_array_get(JsonValue *v, int index) {
    if (!v || v->type != JSON_ARRAY) return NULL;
    if (index < 0 || index >= (int)v->as.array.count) return NULL;
    return v->as.array.items[index];
}

int json_object_count(JsonValue *v) {
    return (v && v->type == JSON_OBJECT) ? (int)v->as.object.count : 0;
}

const char *json_object_key_at(JsonValue *v, int index) {
    if (!v || v->type != JSON_OBJECT) return NULL;
    if (index < 0 || index >= (int)v->as.object.count) return NULL;
    return v->as.object.keys[index];
}

JsonValue *json_object_value_at(JsonValue *v, int index) {
    if (!v || v->type != JSON_OBJECT) return NULL;
    if (index < 0 || index >= (int)v->as.object.count) return NULL;
    return v->as.object.values[index];
}

char *json_strdup(JsonValue *v) {
    const char *s = json_string(v);
    return s ? strdup(s) : NULL;
}

char **json_strdup_array(JsonValue *v, int *count) {
    int n = json_array_count(v);
    if (count) *count = 0;
    if (n <= 0) return NULL;

    char **out = calloc((size_t)n, sizeof(char *));
    int kept = 0;
    for (int i = 0; i < n; i++) {
        const char *s = json_string(json_array_get(v, i));
        if (s) out[kept++] = strdup(s);
    }
    if (count) *count = kept;
    return out;
}

void json_free_string_array(char **arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) free(arr[i]);
    free(arr);
}

JsonValue *json_parse_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';

    JsonValue *root = json_parse(buf);
    free(buf);
    return root;
}

static void json_escape_append(char **buf, size_t *len, size_t *cap, const char *text) {
    for (const char *s = text; *s; s++) {
        const char *rep = NULL;
        char tmp[8];
        switch (*s) {
            case '"': rep = "\\\""; break;
            case '\\': rep = "\\\\"; break;
            case '\n': rep = "\\n"; break;
            case '\r': rep = "\\r"; break;
            case '\t': rep = "\\t"; break;
            default:
                if ((unsigned char)*s < 0x20) {
                    snprintf(tmp, sizeof(tmp), "\\u%04x", *s);
                    rep = tmp;
                }
                break;
        }
        size_t need = rep ? strlen(rep) : 1;
        if (*len + need + 1 >= *cap) { *cap = (*cap + need + 1) * 2; *buf = realloc(*buf, *cap); }
        if (rep) { memcpy(*buf + *len, rep, need); *len += need; }
        else (*buf)[(*len)++] = *s;
    }
    (*buf)[*len] = '\0';
}

char *json_escape(const char *text) {
    size_t len = 0, cap = 32;
    char *buf = malloc(cap);
    buf[0] = '\0';
    json_escape_append(&buf, &len, &cap, text ? text : "");
    return buf;
}
