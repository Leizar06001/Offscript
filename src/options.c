#include "options.h"
#include "json_min.h"
#include "textutil.h"

#include <ncurses.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Emplacement                                                         */
/* ------------------------------------------------------------------ */

static char g_path[512];

const char *options_path(void) {
	if (g_path[0]) return g_path;

	const char *base = getenv("XDG_CONFIG_HOME");
	char root[400];

	if (base && *base) {
		snprintf(root, sizeof(root), "%s", base);
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home) {
			struct passwd *pw = getpwuid(getuid());
			home = (pw && pw->pw_dir) ? pw->pw_dir : ".";
		}
		snprintf(root, sizeof(root), "%s/.config", home);
	}

	snprintf(g_path, sizeof(g_path), "%s/enquete/options.json", root);
	return g_path;
}

static bool ensure_parent_dir(void) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", options_path());

	char *slash = strrchr(dir, '/');
	if (!slash) return false;
	*slash = '\0';

	for (char *p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0700);
		*p = '/';
	}
	if (mkdir(dir, 0700) != 0) {
		struct stat sb;
		if (stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) return false;
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* Touches                                                             */
/* ------------------------------------------------------------------ */

/* Les touches speciales de ncurses (KEY_UP, KEY_F(3)...) partagent leur plage
 * de valeurs avec des caracteres imprimables selon les terminaux. On les range
 * donc au-dessus de OPT_FUNCTION_BASE : une touche enregistree ne peut jamais
 * etre prise pour une autre. */
int options_encode_key(int ch, bool is_function_key) {
	if (ch <= 0) return OPT_KEY_NONE;
	return is_function_key ? (OPT_FUNCTION_BASE + ch) : ch;
}

/* Les lettres sont comparees sans casse : personne ne veut que la touche
 * cesse de fonctionner parce que Verr.Maj est actif. */
static int fold_key(int key) {
	if (key >= 'A' && key <= 'Z') return key - 'A' + 'a';
	return key;
}

void options_defaults(Options *o) {
	memset(o, 0, sizeof(*o));

	/* Fleches en principal, ZQSD en secondaire : la disposition francaise par
	 * defaut, puisque les histoires livrees sont en francais. Le menu permet
	 * de passer en WASD ou a n'importe quoi d'autre. */
	o->keys[ACT_UP][0]    = options_encode_key(KEY_UP, true);
	o->keys[ACT_UP][1]    = 'z';
	o->keys[ACT_DOWN][0]  = options_encode_key(KEY_DOWN, true);
	o->keys[ACT_DOWN][1]  = 's';
	o->keys[ACT_LEFT][0]  = options_encode_key(KEY_LEFT, true);
	o->keys[ACT_LEFT][1]  = 'q';
	o->keys[ACT_RIGHT][0] = options_encode_key(KEY_RIGHT, true);
	o->keys[ACT_RIGHT][1] = 'd';

	o->keys[ACT_SCROLL_UP][0]   = options_encode_key(KEY_PPAGE, true);
	o->keys[ACT_SCROLL_UP][1]   = 'a';
	o->keys[ACT_SCROLL_DOWN][0] = options_encode_key(KEY_NPAGE, true);
	o->keys[ACT_SCROLL_DOWN][1] = 'e';

	o->keys[ACT_TALK][0]        = '\n';
	o->keys[ACT_NEXT_TARGET][0] = '\t';

	o->keys[ACT_JOURNAL][0] = options_encode_key(KEY_F(3), true);
	o->keys[ACT_PERSO][0]   = options_encode_key(KEY_F(2), true);
	o->keys[ACT_MENU][0]    = 27;

	snprintf(o->reasoning, sizeof(o->reasoning), "high");
	o->price_in_per_m  = 0.28;
	o->price_out_per_m = 0.42;
}

int options_action_for_key(const Options *o, int ch, bool is_function_key) {
	int key = fold_key(options_encode_key(ch, is_function_key));
	if (key == OPT_KEY_NONE) return -1;

	for (int a = 0; a < ACT_COUNT; a++) {
		for (int k = 0; k < 2; k++) {
			if (o->keys[a][k] == OPT_KEY_NONE) continue;
			if (fold_key(o->keys[a][k]) == key) return a;
		}
	}
	return -1;
}

const char *options_action_label(Action a) {
	switch (a) {
		case ACT_UP:          return "Avancer";
		case ACT_DOWN:        return "Reculer";
		case ACT_LEFT:        return "Aller a gauche";
		case ACT_RIGHT:       return "Aller a droite";
		case ACT_SCROLL_UP:   return "Remonter le fil";
		case ACT_SCROLL_DOWN: return "Descendre le fil";
		case ACT_TALK:        return "Prendre la parole";
		case ACT_NEXT_TARGET: return "Changer d'interlocuteur";
		case ACT_JOURNAL:     return "Carnet";
		case ACT_PERSO:       return "Apparence";
		case ACT_MENU:        return "Menu (options, solution)";
		default:              return "?";
	}
}

void options_key_name(int key, char *out, size_t out_size) {
	if (key == OPT_KEY_NONE) { snprintf(out, out_size, "-"); return; }

	if (key >= OPT_FUNCTION_BASE) {
		int k = key - OPT_FUNCTION_BASE;
		switch (k) {
			case KEY_UP:    snprintf(out, out_size, "Fleche haut");   return;
			case KEY_DOWN:  snprintf(out, out_size, "Fleche bas");    return;
			case KEY_LEFT:  snprintf(out, out_size, "Fleche gauche"); return;
			case KEY_RIGHT: snprintf(out, out_size, "Fleche droite"); return;
			case KEY_PPAGE: snprintf(out, out_size, "Page haut");     return;
			case KEY_NPAGE: snprintf(out, out_size, "Page bas");      return;
			case KEY_HOME:  snprintf(out, out_size, "Debut");         return;
			case KEY_END:   snprintf(out, out_size, "Fin");           return;
			case KEY_DC:    snprintf(out, out_size, "Suppr");         return;
			default: break;
		}
		for (int f = 1; f <= 12; f++) {
			if (k == KEY_F(f)) { snprintf(out, out_size, "F%d", f); return; }
		}
		snprintf(out, out_size, "touche %d", k);
		return;
	}

	switch (key) {
		case '\n': snprintf(out, out_size, "Entree");  return;
		case '\t': snprintf(out, out_size, "Tab");     return;
		case 27:   snprintf(out, out_size, "Echap");   return;
		case ' ':  snprintf(out, out_size, "Espace");  return;
		default: break;
	}
	if (key >= 33 && key < 127) {
		char c = (char)(key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key);
		snprintf(out, out_size, "%c", c);
		return;
	}
	snprintf(out, out_size, "code %d", key);
}

/* ------------------------------------------------------------------ */
/* Lecture / ecriture                                                  */
/* ------------------------------------------------------------------ */

/* Nom stable pour le fichier : l'ordre de l'enumeration peut changer d'une
 * version a l'autre, pas ces chaines. */
static const char *action_key_name(Action a) {
	switch (a) {
		case ACT_UP:          return "up";
		case ACT_DOWN:        return "down";
		case ACT_LEFT:        return "left";
		case ACT_RIGHT:       return "right";
		case ACT_SCROLL_UP:   return "scroll_up";
		case ACT_SCROLL_DOWN: return "scroll_down";
		case ACT_TALK:        return "talk";
		case ACT_NEXT_TARGET: return "next_target";
		case ACT_JOURNAL:     return "journal";
		case ACT_PERSO:       return "perso";
		case ACT_MENU:        return "menu";
		default:              return "?";
	}
}

void options_load(Options *o) {
	options_defaults(o);

	JsonValue *root = json_parse_file(options_path());
	if (!root) return;   /* pas de fichier : on garde les valeurs d'usine */

	JsonValue *keys = json_object_get(root, "keys");
	for (int a = 0; a < ACT_COUNT; a++) {
		JsonValue *pair = json_object_get(keys, action_key_name(a));
		if (!pair) continue;   /* action absente : reglage d'usine conserve */
		for (int k = 0; k < 2; k++) {
			o->keys[a][k] = json_int_or(json_array_get(pair, k), OPT_KEY_NONE);
		}
	}

	/* Une valeur inconnue est ignoree : mieux vaut le reglage d'usine qu'un
	 * champ que l'API refuserait. */
	const char *r = json_string(json_object_get(root, "reasoning"));
	if (r && (strcmp(r, "low") == 0 || strcmp(r, "high") == 0 || strcmp(r, "max") == 0))
		snprintf(o->reasoning, sizeof(o->reasoning), "%s", r);

	JsonValue *prices = json_object_get(root, "prices_per_million");
	o->price_in_per_m  = json_number_or(json_object_get(prices, "input"),  o->price_in_per_m);
	o->price_out_per_m = json_number_or(json_object_get(prices, "output"), o->price_out_per_m);

	json_free(root);
}

bool options_save(const Options *o) {
	if (!ensure_parent_dir()) return false;

	StrBuf sb;
	sb_init(&sb);
	sb_add(&sb, "{\n  \"keys\": {\n");
	for (int a = 0; a < ACT_COUNT; a++) {
		sb_addf(&sb, "    \"%s\": [%d, %d]%s\n", action_key_name(a),
		        o->keys[a][0], o->keys[a][1], (a + 1 < ACT_COUNT) ? "," : "");
	}
	sb_add(&sb, "  },\n");
	sb_addf(&sb, "  \"reasoning\": \"%s\",\n", o->reasoning);
	sb_addf(&sb, "  \"prices_per_million\": { \"input\": %.6f, \"output\": %.6f }\n}\n",
	        o->price_in_per_m, o->price_out_per_m);

	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", options_path());

	FILE *f = fopen(tmp, "wb");
	if (!f) { sb_free(&sb); return false; }
	size_t written = fwrite(sb.data, 1, sb.len, f);
	bool ok = (written == sb.len);
	if (fclose(f) != 0) ok = false;
	sb_free(&sb);

	if (!ok) { remove(tmp); return false; }
	chmod(tmp, 0600);
	return rename(tmp, options_path()) == 0;
}
