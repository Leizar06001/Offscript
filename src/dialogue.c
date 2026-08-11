#include "dialogue.h"
#include "globals.h"
#include "json_min.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Asked for via plain instructions in the system prompt rather than a
 * provider-specific "structured output" / JSON-schema request field: not
 * every backend/model honors that reliably, while a direct instruction to
 * emit raw JSON works consistently across all of them.
 *
 * The keys are requested in the order emotion, line, action so that the
 * emoji is fully streamed before the spoken line is, which lets the game
 * put the expression on the character's face while the line is still
 * arriving (see deepseek_poll_emotion). */
const char *dialogue_json_instructions(bool with_move) {
    /* Deux variantes mises en cache : avec et sans la cle "move". */
    static char buf[2][2048];
    static int  built[2] = { 0, 0 };
    int v = with_move ? 1 : 0;

    if (built[v]) return buf[v];

    int n = snprintf(buf[v], sizeof(buf[v]),
        "\n\nReponds TOUJOURS avec un unique objet JSON brut, sans markdown, "
        "sans balises de code, sans aucun texte avant ou apres, exactement "
        "dans cette forme, avec les cles DANS CET ORDRE : "
        "%s. "
        "\"line\" est ce que tu dis a voix haute. \"action\" est ce que tu "
        "fais physiquement, en une courte proposition. Les deux doivent etre "
        "en francais. %s"
        "\"emotion\" doit etre EXACTEMENT UN emoji, choisi "
        "OBLIGATOIREMENT dans cette liste :",
        with_move
            ? "{\"emotion\":\"...\",\"line\":\"...\",\"action\":\"...\",\"move\":\"...\"}"
            : "{\"emotion\":\"...\",\"line\":\"...\",\"action\":\"...\"}",
        with_move
            ? "\"move\" est OBLIGATOIRE et vaut l'une des valeurs listees dans "
              "TES DEPLACEMENTS POSSIBLES ; c'est la seule facon de bouger, "
              "dire que tu te deplaces dans \"line\" ne te deplace pas. "
            : "");

    for (int i = 0; i < nb_emotion_faces && n > 0 && n < (int)sizeof(buf[v]); i++) {
        n += snprintf(buf[v] + n, sizeof(buf[v]) - n, " %s", emotion_faces[i].emoji);
    }
    if (n > 0 && n < (int)sizeof(buf[v])) {
        snprintf(buf[v] + n, sizeof(buf[v]) - n,
                 ". N'utilise jamais un autre emoji, et n'en mets aucun dans "
                 "\"line\" ni dans \"action\".");
    }

    built[v] = 1;
    return buf[v];
}

/* Byte length of the leading UTF-8 codepoint of s (1..4). */
static size_t utf8_cp_len(const char *s) {
    unsigned char c = (unsigned char)s[0];
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; /* malformed: advance one byte so callers can't spin */
}

int dialogue_face_from_emoji(const char *emoji) {
    if (!emoji) return -1;

    while (*emoji == ' ' || *emoji == '\n' || *emoji == '\t') emoji++;
    if (!*emoji) return -1;

    /* Only the first codepoint is compared: the same emoji reaches us either
     * bare or followed by a variation selector (U+FE0F), and a plain strcmp
     * would miss the second form. */
    size_t n = utf8_cp_len(emoji);

    for (int i = 0; i < nb_emotion_faces; i++) {
        const char *cand = emotion_faces[i].emoji;
        if (utf8_cp_len(cand) == n && strncmp(cand, emoji, n) == 0) {
            return emotion_faces[i].face_id;
        }
    }
    return -1;
}

/* Strips optional ```json ... ``` / ``` ... ``` fences some models wrap
 * their JSON output in, and surrounding whitespace. Returns a pointer
 * into `text` (no copy); does not allocate. */
static const char *strip_code_fence(const char *text, size_t *out_len) {
    while (*text == ' ' || *text == '\n' || *text == '\r' || *text == '\t') text++;
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\n' ||
                        text[len - 1] == '\r' || text[len - 1] == '\t')) {
        len--;
    }
    if (len >= 6 && strncmp(text, "```", 3) == 0 && strncmp(text + len - 3, "```", 3) == 0) {
        text += 3;
        len -= 6;
        while (len > 0 && *text != '\n' && *text != '{') { text++; len--; } /* skip "json" tag */
        if (*text == '\n') { text++; len--; }
        while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\n' || text[len - 1] == '\r')) len--;
    }
    *out_len = len;
    return text;
}

int dialogue_reply_from_text(const char *raw_text, DialogueReply *out) {
    if (!raw_text) return 0;

    size_t clean_len;
    const char *clean_start = strip_code_fence(raw_text, &clean_len);
    char *clean = malloc(clean_len + 1);
    memcpy(clean, clean_start, clean_len);
    clean[clean_len] = '\0';

    /* Despite instructions, models occasionally prepend a short lead-in
     * before the JSON object (e.g. "Voici ma reponse : {...}"). Our parser
     * requires the object to start at position 0, so hunt for the first
     * '{' instead of failing outright on any preamble; the parser already
     * stops cleanly at the matching '}' and ignores anything after. */
    char *brace = strchr(clean, '{');

    JsonValue *dialogue = json_parse(brace ? brace : clean);
    free(clean);

    const char *line = json_string(json_object_get(dialogue, "line"));
    const char *emotion = json_string(json_object_get(dialogue, "emotion"));
    const char *action = json_string(json_object_get(dialogue, "action"));

    int ok = 0;
    if (line) {
        out->line = strdup(line);
        out->emotion = strdup(emotion ? emotion : "");
        out->action = strdup(action ? action : "");
        out->confession = json_bool_or(json_object_get(dialogue, "confession"), 0);
        const char *mv = json_string(json_object_get(dialogue, "move"));
        out->move = (mv && *mv) ? strdup(mv) : NULL;
        ok = 1;
    }
    json_free(dialogue);
    return ok;
}

int dialogue_reply_from_prose(const char *raw_text, DialogueReply *out) {
	if (!raw_text) return 0;

	size_t clean_len;
	const char *start = strip_code_fence(raw_text, &clean_len);
	while (clean_len > 0 && (*start == ' ' || *start == '\n')) { start++; clean_len--; }
	if (clean_len == 0) return 0;

	/* Le modele derive parfois vers "😐 Sa replique..." : on recupere l'emoji
	 * de tete s'il fait partie de la palette, sinon l'emotion reste vide et le
	 * visage du personnage garde son expression. */
	char emoji[8] = "";
	size_t cp = utf8_cp_len(start);
	if (cp > 1 && cp <= clean_len) {
		char candidate[8] = "";
		memcpy(candidate, start, cp);
		if (dialogue_face_from_emoji(candidate) >= 0) {
			memcpy(emoji, candidate, cp);
			start += cp;
			clean_len -= cp;
			while (clean_len > 0 && *start == ' ') { start++; clean_len--; }
		}
	}
	if (clean_len == 0) return 0;

	out->line = malloc(clean_len + 1);
	memcpy(out->line, start, clean_len);
	out->line[clean_len] = '\0';
	out->emotion = strdup(emoji);
	out->action  = strdup("");
	/* Une reponse partie en prose ne peut pas conclure l'enquete ni deplacer
	 * qui que ce soit : ces effets n'existent que dans la forme demandee. */
	out->confession = 0;
	out->move = NULL;
	return 1;
}

void dialogue_reply_free(DialogueReply *reply) {
    if (!reply) return;
    free(reply->line);
    free(reply->emotion);
    free(reply->action);
    free(reply->move);
    reply->line = reply->emotion = reply->action = reply->move = NULL;
}
