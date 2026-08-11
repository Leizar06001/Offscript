#include "includes.h"

#include <stdarg.h>

/* Le fil de discussion.
 *
 * Il est conserve en memoire puis redessine entierement des qu'il change,
 * plutot qu'ecrit au fil de l'eau dans la fenetre ncurses. Trois choses en
 * dependent : on peut remonter dans l'historique, on peut y afficher les
 * reactions des personnages a leur place, et surtout le nom de celui qui
 * parle est resolu au moment du rendu, donc « un inconnu » devient son vrai
 * nom dans tout le passe des qu'on l'apprend. */

#define CHAT_LINES_MAX 1200
#define CHAT_LINE_MAX  256

typedef struct {
	char text[CHAT_LINE_MAX];
	int  pair;
	int  attrs;
} ChatLine;

static ChatLine *g_lines;
static int      g_nb_lines;
static int      g_cap_lines;

/* ------------------------------------------------------------------ */
/* Cycle de vie                                                        */
/* ------------------------------------------------------------------ */

void chat_init(Game *game) {
	game->chat.log    = NULL;
	game->chat.nb_log = 0;
	game->chat.cap_log = 0;
	game->chat.scroll = 0;
	game->chat.live   = -1;
	game->chat.dirty  = 1;
}

void chat_free(Game *game) {
	if (!game) return;
	for (int i = 0; i < game->chat.nb_log; i++) {
		free(game->chat.log[i].text);
		game->chat.log[i].text = NULL;
	}
	game->chat.nb_log = 0;
	free(game->chat.log);
	game->chat.log = NULL;
	game->chat.cap_log = 0;
	game->chat.live = -1;
}

void chat_touch(Game *game) {
	game->chat.dirty = 1;
}

static bool chat_reserve(Chat *c) {
	if (c->nb_log < c->cap_log) return true;
	int cap = c->cap_log > 0 ? c->cap_log * 2 : 128;
	ChatEntry *grown = realloc(c->log, sizeof(ChatEntry) * (size_t)cap);
	if (!grown) return false;
	c->log = grown;
	c->cap_log = cap;
	return true;
}

static int chat_push(Game *game, int kind, int npc_index, const char *text) {
	Chat *c = &game->chat;
	if (!chat_reserve(c)) return -1;

	ChatEntry *e = &c->log[c->nb_log];
	e->kind      = kind;
	e->npc_index = npc_index;
	e->text      = strdup(text ? text : "");
	c->nb_log++;

	/* Un message qui arrive ramene en bas : sinon il s'ecrirait hors champ
	 * pendant qu'on relit l'historique, sans qu'on le sache. */
	c->scroll = 0;
	c->dirty  = 1;
	return c->nb_log - 1;
}

void chat_add(Game *game, int kind, int npc_index, const char *text) {
	pthread_mutex_lock(&game->chat.m_chat_box);
	chat_push(game, kind, npc_index, text);
	pthread_mutex_unlock(&game->chat.m_chat_box);
}

void chat_addf(Game *game, int kind, int npc_index, const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	chat_add(game, kind, npc_index, buf);
}

/* Le fil complet appartient a la sauvegarde, mais jamais au prompt. On en
 * prend une copie coherente au moment de l'autosave ; une replique encore en
 * streaming est omise jusqu'a sa prochaine sauvegarde, car elle n'est pas un
 * tour termine. */
void chat_sync_to_save(Game *game) {
	if (!game || !game->save) return;
	SaveState *save = game->save;
	pthread_mutex_lock(&game->chat.m_chat_box);

	/* Le chargement prepare et autosauvegarde parfois la resolution avant que
	 * la carte — et donc le fil runtime — soit restauree. Une fenetre encore
	 * vide ne doit jamais effacer un chat_log deja lu depuis le disque. */
	if (game->chat.nb_log == 0 && save->nb_chat_log > 0) {
		pthread_mutex_unlock(&game->chat.m_chat_box);
		return;
	}

	for (int i = 0; i < save->nb_chat_log; i++) {
		free(save->chat_log[i].npc_id);
		free(save->chat_log[i].text);
	}
	free(save->chat_log);
	save->chat_log = NULL;
	save->nb_chat_log = 0;

	int count = game->chat.nb_log - (game->chat.live >= 0 ? 1 : 0);
	if (count > 0) save->chat_log = calloc((size_t)count, sizeof(SavedChatEntry));
	for (int i = 0; i < game->chat.nb_log; i++) {
		if (i == game->chat.live) continue;
		const ChatEntry *src = &game->chat.log[i];
		SavedChatEntry *dst = &save->chat_log[save->nb_chat_log++];
		dst->kind = src->kind;
		dst->text = strdup(src->text ? src->text : "");
		if ((src->kind == CHAT_NPC || src->kind == CHAT_ACTION) &&
		    src->npc_index >= 0 && src->npc_index < game->nb_npcs &&
		    game->npcs[src->npc_index].def && game->npcs[src->npc_index].def->id)
			dst->npc_id = strdup(game->npcs[src->npc_index].def->id);
	}
	pthread_mutex_unlock(&game->chat.m_chat_box);
}

static int chat_npc_index(const Game *game, const char *npc_id) {
	if (!npc_id) return -1;
	for (int i = 0; i < game->nb_npcs; i++)
		if (game->npcs[i].def && game->npcs[i].def->id &&
		    strcmp(game->npcs[i].def->id, npc_id) == 0) return i;
	return -1;
}

void chat_restore_from_save(Game *game) {
	if (!game || !game->save) return;
	chat_free(game);
	chat_init(game);

	if (game->save->nb_chat_log > 0) {
		for (int i = 0; i < game->save->nb_chat_log; i++) {
			const SavedChatEntry *e = &game->save->chat_log[i];
			int npc = chat_npc_index(game, e->npc_id);
			chat_push(game, e->kind, npc, e->text ? e->text : "");
		}
		return;
	}

	/* Migration douce des anciennes sauvegardes : elles ne possedent que la
	 * fenetre recente de chaque PNJ et aucune chronologie globale. On restaure
	 * ce qui existe, groupe par personnage ; les nouveaux tours seront ensuite
	 * conserves integralement dans chat_log. */
	for (int i = 0; i < game->nb_npcs; i++) {
		NpcState *ns = memory_get_npc(game->save, game->npcs[i].def->id);
		if (!ns) continue;
		for (int m = 0; m < ns->nb_recent; m++) {
			const ChatMsg *msg = &ns->recent[m];
			if (msg->role && strcmp(msg->role, "user") == 0)
				chat_push(game, CHAT_PLAYER, -1, msg->content);
			else if (msg->role && strcmp(msg->role, "assistant") == 0) {
				chat_push(game, CHAT_NPC, i, msg->content);
				if (msg->action && *msg->action)
					chat_push(game, CHAT_ACTION, i, msg->action);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Reception d'une replique morceau par morceau                        */
/* ------------------------------------------------------------------ */

void chat_stream_begin(Game *game, int npc_index) {
	pthread_mutex_lock(&game->chat.m_chat_box);
	game->chat.live = chat_push(game, CHAT_NPC, npc_index, "");
	pthread_mutex_unlock(&game->chat.m_chat_box);
}

void chat_stream_chunk(Game *game, const char *text) {
	if (!text || !*text) return;

	pthread_mutex_lock(&game->chat.m_chat_box);
	int live = game->chat.live;
	if (live >= 0 && live < game->chat.nb_log) {
		ChatEntry *e = &game->chat.log[live];
		size_t old = e->text ? strlen(e->text) : 0;
		size_t add = strlen(text);
		char *grown = realloc(e->text, old + add + 1);
		if (grown) {
			memcpy(grown + old, text, add + 1);
			e->text = grown;
			game->chat.dirty = 1;
		}
	}
	pthread_mutex_unlock(&game->chat.m_chat_box);
}

void chat_stream_end(Game *game) {
	pthread_mutex_lock(&game->chat.m_chat_box);

	/* Rien n'est venu : la ligne d'attente n'a plus lieu d'etre. C'est le cas
	 * d'une requete en echec, et surtout d'une intervention ou le personnage
	 * a choisi de se taire. */
	int live = game->chat.live;
	if (live >= 0 && live == game->chat.nb_log - 1 &&
	    (!game->chat.log[live].text || !game->chat.log[live].text[0])) {
		free(game->chat.log[live].text);
		game->chat.log[live].text = NULL;
		game->chat.nb_log--;
	}

	game->chat.live = -1;
	game->chat.dirty = 1;
	pthread_mutex_unlock(&game->chat.m_chat_box);
}

/* Anime les points de suspension tant que la replique n'a pas commence a
 * arriver. Appelee a chaque tour de boucle : c'est le seul endroit qui sait
 * que le temps passe. */
void chat_tick(Game *game) {
	pthread_mutex_lock(&game->chat.m_chat_box);
	int live = game->chat.live;
	bool waiting = (live >= 0 && live < game->chat.nb_log &&
	                (!game->chat.log[live].text || !game->chat.log[live].text[0]));
	if (waiting && millis() >= game->chat.t_next_spin) {
		game->chat.spinner++;
		game->chat.t_next_spin = millis() + 400;
		game->chat.dirty = 1;
	}
	pthread_mutex_unlock(&game->chat.m_chat_box);
}

/* ------------------------------------------------------------------ */
/* Mise en lignes                                                      */
/* ------------------------------------------------------------------ */

static void line_push(const char *text, int pair, int attrs) {
	if (g_nb_lines >= g_cap_lines) {
		int cap = g_cap_lines > 0 ? g_cap_lines * 2 : CHAT_LINES_MAX;
		ChatLine *grown = realloc(g_lines, sizeof(ChatLine) * (size_t)cap);
		if (!grown) return;
		g_lines = grown;
		g_cap_lines = cap;
	}
	ChatLine *l = &g_lines[g_nb_lines++];
	snprintf(l->text, sizeof(l->text), "%s", text);
	l->pair  = pair;
	l->attrs = attrs;
}

/* Replie un paragraphe sur `width` colonnes, en coupant aux espaces et en
 * decalant les lignes suivantes sous le texte plutot que sous le nom. */
static void wrap_into_lines(const char *prefix, const char *body,
                            int width, int indent, int pair, int attrs) {
	char buf[CHAT_LINE_MAX];
	const char *p = body ? body : "";
	int first = 1;

	/* Une entree vide (replique qui commence a peine d'arriver) doit quand
	 * meme afficher le nom, sinon la ligne apparait d'un coup a la fin. */
	if (!*p) {
		snprintf(buf, sizeof(buf), "%s", prefix);
		line_push(buf, pair, attrs);
		return;
	}

	while (*p) {
		while (*p == ' ') p++;
		if (!*p) break;

		int avail = width - (first ? text_display_cols(prefix) : indent);
		if (avail < 8) avail = 8;

		int take = text_bytes_for_cols(p, avail);
		if (p[take] != '\0' && p[take] != ' ') {
			int cut = take;
			while (cut > 0 && p[cut] != ' ') cut--;
			if (cut > 0) take = cut;
		}

		if (first) {
			snprintf(buf, sizeof(buf), "%s%.*s", prefix, take, p);
		} else {
			snprintf(buf, sizeof(buf), "%*s%.*s", indent, "", take, p);
		}
		line_push(buf, pair, attrs);

		p += take;
		first = 0;
	}
}

/* Construit toutes les lignes du fil. Le nom du personnage est lu ICI, donc
 * un nom appris entre-temps s'applique retroactivement a tout le fil. */
static void build_lines(Game *game, int width) {
	g_nb_lines = 0;

	for (int i = 0; i < game->chat.nb_log; i++) {
		const ChatEntry *e = &game->chat.log[i];
		char prefix[96];
		int pair = PAIR_TEXT, attrs = 0, indent = 3;

		switch (e->kind) {
			case CHAT_PLAYER:
				snprintf(prefix, sizeof(prefix), " %s : ", game->player.name);
				pair = PAIR_TEXT;
				attrs = A_BOLD;
				indent = 3;
				break;

			case CHAT_NPC: {
				const NPC *npc = (e->npc_index >= 0 && e->npc_index < game->nb_npcs)
				               ? &game->npcs[e->npc_index] : NULL;
				snprintf(prefix, sizeof(prefix), " %s : ", npc_display_name(npc));
				pair = npc ? npc_color(e->npc_index) : 9;
				indent = 3;
				break;
			}

			case CHAT_ACTION:
				/* La reaction physique est mise en retrait et en gris : elle se
				 * distingue de la parole sans couper la lecture. */
				snprintf(prefix, sizeof(prefix), "   ");
				pair = 9;
				attrs = A_DIM;
				indent = 3;
				break;

			default:
				snprintf(prefix, sizeof(prefix), " ");
				pair = 9;
				indent = 1;
				break;
		}

		/* Le personnage reflechit : on montre son nom tout de suite, suivi de
		 * points qui avancent. La replique viendra remplacer les points a la
		 * meme place, dans le fil. */
		if (i == game->chat.live && (!e->text || !e->text[0])) {
			static const char *dots[] = { ".", "..", "...", ".." };
			wrap_into_lines(prefix, dots[game->chat.spinner & 3], width, indent, 9, A_DIM);
			continue;
		}

		wrap_into_lines(prefix, e->text, width, indent, pair, attrs);
	}
}

/* ------------------------------------------------------------------ */
/* Rendu                                                               */
/* ------------------------------------------------------------------ */

void chat_scroll(Game *game, int lines) {
	pthread_mutex_lock(&game->chat.m_chat_box);
	game->chat.scroll += lines;
	if (game->chat.scroll < 0) game->chat.scroll = 0;
	game->chat.dirty = 1;
	pthread_mutex_unlock(&game->chat.m_chat_box);
}

int chat_render(Game *game) {
	if (!game || !game->display.chat) return 0;

	pthread_mutex_lock(&game->chat.m_chat_box);
	if (!game->chat.dirty) {
		pthread_mutex_unlock(&game->chat.m_chat_box);
		return 0;
	}
	game->chat.dirty = 0;

	int h, w;
	getmaxyx(game->display.chat, h, w);
	if (h <= 0 || w <= 2) {
		pthread_mutex_unlock(&game->chat.m_chat_box);
		return 0;
	}

	build_lines(game, w - 1);

	/* On ne peut pas remonter plus haut que le debut du fil. */
	int max_scroll = g_nb_lines - h;
	if (max_scroll < 0) max_scroll = 0;
	if (game->chat.scroll > max_scroll) game->chat.scroll = max_scroll;

	int last  = g_nb_lines - game->chat.scroll;   /* exclu */
	int first = last - h;
	if (first < 0) first = 0;

	pthread_mutex_lock(&game->display.m_display_update);
	werase(game->display.chat);
	for (int i = first; i < last; i++) {
		ChatLine *l = &g_lines[i];
		wattron(game->display.chat, COLOR_PAIR(l->pair) | l->attrs);
		mvwprintw(game->display.chat, i - first, 0, "%s", l->text);
		wattroff(game->display.chat, COLOR_PAIR(l->pair) | l->attrs);
	}

	/* Un repere quand on relit l'historique : sans lui, rien ne dit que des
	 * messages plus recents attendent en bas. */
	if (game->chat.scroll > 0) {
		wmove(game->display.chat, h - 1, 0);
		wclrtoeol(game->display.chat);   /* sinon le texte dessous depasse */
		wattron(game->display.chat, COLOR_PAIR(PAIR_WARN) | A_BOLD);
		mvwprintw(game->display.chat, h - 1, 0, " v %d ligne%s plus bas - [PgDn]",
		          game->chat.scroll, game->chat.scroll > 1 ? "s" : "");
		wattroff(game->display.chat, COLOR_PAIR(PAIR_WARN) | A_BOLD);
	}
	wrefresh(game->display.chat);
	pthread_mutex_unlock(&game->display.m_display_update);

	pthread_mutex_unlock(&game->chat.m_chat_box);

	move_cursor_back(game);
	return 1;
}
