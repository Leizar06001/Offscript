#include "textutil.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sb_init(StrBuf *sb) {
	sb->cap  = 256;
	sb->len  = 0;
	sb->data = malloc(sb->cap);
	sb->data[0] = '\0';
}

static void sb_reserve(StrBuf *sb, size_t extra) {
	if (sb->len + extra + 1 <= sb->cap) return;
	while (sb->cap < sb->len + extra + 1) sb->cap *= 2;
	sb->data = realloc(sb->data, sb->cap);
}

void sb_add(StrBuf *sb, const char *s) {
	if (!s) return;
	size_t n = strlen(s);
	sb_reserve(sb, n);
	memcpy(sb->data + sb->len, s, n);
	sb->len += n;
	sb->data[sb->len] = '\0';
}

void sb_addf(StrBuf *sb, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	int needed = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (needed <= 0) return;

	sb_reserve(sb, (size_t)needed);
	va_start(ap, fmt);
	vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, ap);
	va_end(ap);
	sb->len += (size_t)needed;
}

void sb_free(StrBuf *sb) {
	free(sb->data);
	sb->data = NULL;
	sb->len = sb->cap = 0;
}

char *sb_take(StrBuf *sb) {
	char *out = sb->data;
	sb->data = NULL;
	sb->len = sb->cap = 0;
	return out;
}

void text_fold_ascii(char *dst, size_t dst_size, const char *src) {
	static const struct { const char *utf8; char ascii; } map[] = {
		{"à",'a'},{"â",'a'},{"ä",'a'},{"á",'a'},{"ã",'a'},
		{"é",'e'},{"è",'e'},{"ê",'e'},{"ë",'e'},
		{"î",'i'},{"ï",'i'},{"í",'i'},
		{"ô",'o'},{"ö",'o'},{"ó",'o'},{"õ",'o'},
		{"ù",'u'},{"û",'u'},{"ü",'u'},{"ú",'u'},
		{"ç",'c'},{"ñ",'n'},{"ÿ",'y'},
		{"À",'a'},{"Â",'a'},{"Ä",'a'},
		{"É",'e'},{"È",'e'},{"Ê",'e'},{"Ë",'e'},
		{"Î",'i'},{"Ï",'i'},{"Ô",'o'},{"Ö",'o'},
		{"Ù",'u'},{"Û",'u'},{"Ü",'u'},{"Ç",'c'},
	};
	const size_t nb_map = sizeof(map) / sizeof(map[0]);

	if (!dst || dst_size == 0) return;
	if (!src) { dst[0] = '\0'; return; }

	size_t di = 0;
	const char *s = src;
	while (*s && di + 1 < dst_size) {
		if ((unsigned char)*s < 0x80) {
			dst[di++] = (char)tolower((unsigned char)*s);
			s++;
			continue;
		}

		int matched = 0;
		for (size_t m = 0; m < nb_map; m++) {
			size_t l = strlen(map[m].utf8);
			if (strncmp(s, map[m].utf8, l) == 0) {
				dst[di++] = map[m].ascii;
				s += l;
				matched = 1;
				break;
			}
		}
		if (!matched) dst[di++] = *s++;
	}
	dst[di] = '\0';
}
