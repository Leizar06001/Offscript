#ifndef TEXTUTIL_H
#define TEXTUTIL_H

#include <stddef.h>

/* Growable string, used to build save files and LLM prompts without
 * counting bytes by hand. */
typedef struct {
	char  *data;
	size_t len;
	size_t cap;
} StrBuf;

void  sb_init(StrBuf *sb);
void  sb_add(StrBuf *sb, const char *s);
void  sb_addf(StrBuf *sb, const char *fmt, ...);
void  sb_free(StrBuf *sb);
/* Hands the buffer to the caller (who must free it) and resets sb. */
char *sb_take(StrBuf *sb);

/* Folds UTF-8 to lowercase unaccented ASCII, so "Solène"/"Écho" match
 * "solene"/"echo". Used for name detection and memory tag matching, where
 * the authored data is unaccented but the model answers in real French. */
void text_fold_ascii(char *dst, size_t dst_size, const char *src);

#endif
