#include "includes.h"
#include "diag.h"
#include "prompt.h"
#include "textutil.h"

#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Construction et proximite                                           */
/* ------------------------------------------------------------------ */

int npc_build_from_story(NPC *out, int max, const Story *story) {
	int n = 0;
	for (int i = 0; i < story->nb_characters && n < max; i++) {
		const StoryCharacter *c = &story->characters[i];

		memset(&out[n], 0, sizeof(out[n]));
		out[n].def     = c;
		out[n].x       = c->x;
		out[n].y       = c->y;
		out[n].present = c->placed;
		out[n].face_id = c->face_id;
		out[n].body_id = c->body_id;
		out[n].legs_id = c->legs_id;
		out[n].room    = ROOM_NONE;
		out[n].goes_to_talk_to = -1;

		/* Les autorisations sont recopiees : le jeu doit pouvoir les changer
		 * (rallumer un androide) sans toucher a la fiche de l'auteur. */
		out[n].can_move        = c->can_move;
		out[n].can_change_room = c->can_change_room;
		n++;
	}
	return n;
}

void npc_refresh_room(Game *game, int idx) {
	NPC *npc = &game->npcs[idx];
	npc->room = map_room_at(&game->map, npc->x, npc->y);
}

void npc_place_all(Game *game) {
	int busy_x[NPC_MAX + 1], busy_y[NPC_MAX + 1];
	int nb_busy = 0;

	/* Le joueur occupe deja une case : personne ne doit lui apparaitre
	 * dessus. */
	busy_x[nb_busy] = game->player.x;
	busy_y[nb_busy] = game->player.y;
	nb_busy++;

	for (int i = 0; i < game->nb_npcs; i++) {
		NPC *npc = &game->npcs[i];
		if (!npc->present) continue;

		/* Des coordonnees explicites l'emportent, a condition qu'elles soient
		 * jouables : sinon le personnage serait coince dans un mur. */
		bool placed = npc->def->has_xy && map_can_stand(&game->map, npc->x, npc->y);

		if (!placed && npc->def->room_id) {
			int room = map_room_by_id(&game->map, npc->def->room_id);
			int x, y;
			if (map_spot_in_room(&game->map, room, busy_x, busy_y, nb_busy, &x, &y) == 0) {
				npc->x = x;
				npc->y = y;
				placed = true;
			}
		}

		/* Ni position jouable ni piece utilisable : le laisser sur la carte
		 * le rendrait invisible ou intraversable, on le retire. */
		npc->present = placed;
		if (!placed) continue;

		npc_refresh_room(game, i);
		busy_x[nb_busy] = npc->x;
		busy_y[nb_busy] = npc->y;
		nb_busy++;
	}
}

/* Reprend l'etat du monde conserve dans la sauvegarde. A appeler apres
 * npc_place_all : le placement de l'histoire sert de valeur par defaut, la
 * sauvegarde ne fait que le corriger quand elle a quelque chose a dire. */
void npc_restore_from_save(Game *game) {
	if (!game->save) return;

	/* La carte d'une histoire peut avoir ete redessinee depuis la derniere
	 * partie. Une position enregistree qui tombe dans un mur, ou sur une
	 * colonne de la mauvaise parite, est abandonnee au profit du point de
	 * depart : tout le monde avance de deux colonnes a la fois, donc un
	 * joueur pose sur la mauvaise parite ne pourrait plus rejoindre
	 * personne. */
	if (game->save->player_x >= 0 &&
	    (game->save->player_x & 1) == game->map.x_parity &&
	    map_can_stand(&game->map, game->save->player_x, game->save->player_y)) {
		game->player.x = game->save->player_x;
		game->player.y = game->save->player_y;
	}

	for (int i = 0; i < game->nb_npcs; i++) {
		NPC *npc = &game->npcs[i];
		NpcState *ns = memory_get_npc(game->save, npc->def->id);
		if (!ns || !ns->has_world) continue;

		if (ns->x >= 0 && (ns->x & 1) == game->map.x_parity &&
		    map_can_stand(&game->map, ns->x, ns->y)) {
			npc->x = ns->x;
			npc->y = ns->y;
		}
		if (ns->face_id >= 0 && ns->face_id < NB_FACES) npc->face_id = ns->face_id;
		npc->met             = ns->met;
		npc->name_known      = ns->name_known;
		npc->can_move        = ns->can_move;
		npc->can_change_room = ns->can_change_room;
		npc_refresh_room(game, i);
	}
}

/* Recopie l'etat courant dans la sauvegarde, juste avant de l'ecrire. */
void npc_sync_to_save(Game *game) {
	if (!game->save) return;

	game->save->player_x = game->player.x;
	game->save->player_y = game->player.y;

	for (int i = 0; i < game->nb_npcs; i++) {
		NPC *npc = &game->npcs[i];
		NpcState *ns = memory_get_npc(game->save, npc->def->id);
		if (!ns) continue;

		ns->has_world       = true;
		ns->x               = npc->x;
		ns->y               = npc->y;
		ns->face_id         = npc->face_id;
		ns->met             = npc->met;
		ns->name_known      = npc->name_known;
		ns->can_move        = npc->can_move;
		ns->can_change_room = npc->can_change_room;
	}
}

int npc_in_player_room(const Game *game, int *out, int max) {
	int room = map_room_at(&game->map, game->player.x, game->player.y);
	if (room == ROOM_NONE) return 0;

	int n = 0;
	for (int i = 0; i < game->nb_npcs && n < max; i++) {
		if (!game->npcs[i].present) continue;
		if (game->npcs[i].room != room) continue;
		out[n++] = i;
	}

	/* Les plus proches d'abord : quand plusieurs personnes sont la, la
	 * question s'adresse d'abord a celle devant qui on se tient. */
	for (int i = 1; i < n; i++) {
		int v = out[i];
		double dv = distance(game->npcs[v].x, game->npcs[v].y, game->player.x, game->player.y);
		int k = i - 1;
		while (k >= 0 &&
		       distance(game->npcs[out[k]].x, game->npcs[out[k]].y,
		                game->player.x, game->player.y) > dv) {
			out[k + 1] = out[k];
			k--;
		}
		out[k + 1] = v;
	}
	return n;
}

int npc_find_target(const Game *game) {
	int list[NPC_MAX];
	int n = npc_in_player_room(game, list, NPC_MAX);
	if (n == 0) return -1;

	/* Un interlocuteur choisi explicitement ([Tab] ou en le nommant) reste
	 * l'interlocuteur tant qu'il est la. Sans cela, seule la personne la plus
	 * proche pouvait jamais repondre, et la seconde etait injoignable. */
	for (int i = 0; i < n; i++) {
		if (list[i] == game->talk_choice) return game->talk_choice;
	}

	/* Faute de choix explicite, on garde celui retenu au dernier arret du
	 * joueur : la selection automatique ne suit que ses deplacements, pas ceux
	 * des personnages. */
	for (int i = 0; i < n; i++) {
		if (list[i] == game->talk_auto) return game->talk_auto;
	}
	return list[0];
}

/* Fige l'interlocuteur du moment. Appele une fois par tour de boucle, apres le
 * deplacement des personnages : c'est ce qui rend la designation stable, alors
 * que la recalculer a chaque affichage la ferait suivre le plus proche. */
int npc_refresh_target(Game *game) {
	/* Un choix explicite qui sort de la piece est perdu : sinon, en revenant,
	 * il reprendrait la parole a celui a qui le joueur s'adressait depuis. */
	if (game->talk_choice >= 0) {
		int list[NPC_MAX];
		int n = npc_in_player_room(game, list, NPC_MAX);
		int here = 0;
		for (int i = 0; i < n; i++) if (list[i] == game->talk_choice) here = 1;
		if (!here) game->talk_choice = -1;
	}

	int idx = npc_find_target(game);
	game->talk_auto   = idx;
	game->talk_target = idx;
	return idx;
}

/* Passe a la personne suivante de la piece. */
void npc_cycle_target(Game *game) {
	int list[NPC_MAX];
	int n = npc_in_player_room(game, list, NPC_MAX);
	if (n <= 1) return;

	int current = npc_find_target(game);
	int pos = 0;
	for (int i = 0; i < n; i++) if (list[i] == current) pos = i;

	game->talk_choice = list[(pos + 1) % n];
	npc_refresh_target(game);   /* la ligne d'en-tete doit le dire tout de suite */
}

/* On s'adresse a quelqu'un en le nommant : « Naomi, ou etiez-vous ? ». Le nom
 * doit deja etre connu du joueur, sinon il devinerait a qui il parle. */
static void retarget_from_text(Game *game, const char *text) {
	int list[NPC_MAX];
	int n = npc_in_player_room(game, list, NPC_MAX);
	if (n <= 1 || !text) return;

	for (int i = 0; i < n; i++) {
		NPC *npc = &game->npcs[list[i]];
		if (!npc->name_known) continue;
		if (npc_name_in_text(npc->def->name, text)) {
			game->talk_choice = list[i];
			return;
		}
	}
}

const char *npc_display_name(const NPC *npc) {
	if (!npc || !npc->def) return "un inconnu";
	return npc->name_known ? npc->def->name : "un inconnu";
}

/* Une civilite ne designe personne : chercher « Dr. Karim Idrissi » sur son
 * premier mot revenait a guetter « dr », qui n'apparait jamais quand il se
 * presente (« Karim Idrissi, neuroscientifique »). */
static bool is_title_word(const char *w) {
	static const char *titles[] = {
		"dr", "m", "mr", "mme", "mlle", "pr", "prof", "me",
		"monsieur", "madame", "mademoiselle", "sir", "miss", NULL
	};
	for (int i = 0; titles[i]; i++) {
		if (strcmp(w, titles[i]) == 0) return true;
	}
	return false;
}

/* Recherche d'un mot entier : sans cela « Me » se trouverait dans « meme »
 * et n'importe quelle replique revelerait un nom. */
static bool word_in(const char *hay, const char *needle) {
	size_t n = strlen(needle);
	if (n == 0) return false;
	for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) {
		bool left_ok  = (p == hay) || !isalnum((unsigned char)p[-1]);
		bool right_ok = !isalnum((unsigned char)p[n]);
		if (left_ok && right_ok) return true;
	}
	return false;
}

bool npc_name_in_text(const char *name, const char *text) {
	if (!name || !text) return false;

	char folded_name[128], folded_text[4096];
	text_fold_ascii(folded_name, sizeof(folded_name), name);
	text_fold_ascii(folded_text, sizeof(folded_text), text);

	/* N'importe quelle partie du nom suffit : on se presente aussi bien par
	 * son prenom que par son patronyme. */
	const char *p = folded_name;
	while (*p) {
		while (*p && !isalnum((unsigned char)*p)) p++;
		size_t n = 0;
		while (p[n] && isalnum((unsigned char)p[n])) n++;
		if (n == 0) break;

		if (n >= 3 && n < 64) {
			char word[64];
			memcpy(word, p, n);
			word[n] = '\0';
			if (!is_title_word(word) && word_in(folded_text, word)) return true;
		}
		p += n;
	}
	return false;
}

bool npc_text_reveals_name(const NPC *npc, const char *text) {
	if (!npc || !npc->def) return false;
	return npc_name_in_text(npc->def->name, text);
}

/* ------------------------------------------------------------------ */
/* Deplacements                                                        */
/* ------------------------------------------------------------------ */

/* Un pas toutes les NPC_STEP_MS : les personnages traversent la piece a vue,
 * au lieu d'y apparaitre ailleurs entre deux rafraichissements. */
#define NPC_STEP_MS      320
#define NPC_WANDER_MIN_MS 14000
#define NPC_WANDER_MAX_MS 40000

static unsigned npc_rand(void) {
	static unsigned s = 0;
	if (!s) s = (unsigned)(millis() ^ 0x9E3779B9u) | 1u;
	s ^= s << 13; s ^= s >> 17; s ^= s << 5;
	return s;
}

/* Une case est libre si personne ne s'y tient deja : deux personnages
 * superposes n'en laissent qu'un seul visible a l'ecran. */
static bool tile_free(const Game *game, int idx, int x, int y) {
	if (game->player.x == x && game->player.y == y) return false;
	for (int i = 0; i < game->nb_npcs; i++) {
		if (i == idx || !game->npcs[i].present) continue;
		if (game->npcs[i].x == x && game->npcs[i].y == y) return false;
	}
	return true;
}

/* Pendant une conversation, la scene doit tenir en place : personne ne s'en
 * va au milieu d'une phrase. Cela ne concerne que les personnes concernees
 * par l'echange — a l'autre bout du batiment, la vie continue. */
static bool npc_frozen(const Game *game, int idx) {
	if (game->pending_req) return true;
	if (!game->discussion_mode) return false;
	return game->npcs[idx].room ==
	       map_room_at(&game->map, game->player.x, game->player.y);
}

/* Fixe une destination. Le trajet est recalcule a chaque pas, donc il suffit
 * de retenir l'arrivee. */
static bool npc_goto(Game *game, int idx, int tx, int ty) {
	NPC *npc = &game->npcs[idx];
	if (!npc->can_move) return false;
	if (!map_can_stand(&game->map, tx, ty)) return false;

	/* Changer de piece est une autorisation distincte : un personnage assigne
	 * a son poste peut circuler chez lui sans pouvoir en sortir. */
	int dest_room = map_room_at(&game->map, tx, ty);
	if (dest_room != npc->room && !npc->can_change_room) return false;
	if (map_path_len(&game->map, npc->x, npc->y, tx, ty) < 0) return false;

	npc->dest_x = tx;
	npc->dest_y = ty;
	npc->moving = true;
	npc->goes_to_talk_to = -1;
	return true;
}

/* Un pas vers la destination. Renvoie true si le personnage a bouge. */
static bool npc_step(Game *game, int idx) {
	NPC *npc = &game->npcs[idx];
	if (!npc->moving) return false;

	if (npc->x == npc->dest_x && npc->y == npc->dest_y) {
		npc->moving = false;
		return false;
	}

	int busy_x[NPC_MAX + 1], busy_y[NPC_MAX + 1], nb_busy = 0;
	busy_x[nb_busy] = game->player.x;
	busy_y[nb_busy++] = game->player.y;
	for (int i = 0; i < game->nb_npcs; i++) {
		if (i == idx || !game->npcs[i].present) continue;
		busy_x[nb_busy] = game->npcs[i].x;
		busy_y[nb_busy++] = game->npcs[i].y;
	}

	int nx, ny;
	if (map_next_step_avoid(&game->map, npc->x, npc->y,
	                        npc->dest_x, npc->dest_y,
	                        busy_x, busy_y, nb_busy, &nx, &ny) != 0) {
		/* L'architecture de la carte a deja ete validee par npc_goto. Un echec
		 * ici vient donc d'un obstacle temporaire : conserver la destination et
		 * reessayer au prochain pas. */
		return false;
	}
	/* Quelqu'un occupe la case : on attend le pas suivant plutot que de lui
	 * marcher dessus, le passage se libere generalement tout seul. */
	if (!tile_free(game, idx, nx, ny)) return false;

	npc->x = nx;
	npc->y = ny;
	npc_refresh_room(game, idx);
	if (npc->x == npc->dest_x && npc->y == npc->dest_y) npc->moving = false;
	return true;
}

/* Deplacement d'oisivete : de temps en temps, sans raison, dans sa piece. */
static void npc_maybe_wander(Game *game, int idx, uint64_t now) {
	NPC *npc = &game->npcs[idx];
	if (!npc->can_move || npc->moving) return;
	if (npc->room == ROOM_NONE) return;

	if (npc->t_next_wander == 0) {
		npc->t_next_wander = now + NPC_WANDER_MIN_MS +
		                     npc_rand() % (NPC_WANDER_MAX_MS - NPC_WANDER_MIN_MS);
		return;
	}
	if (now < npc->t_next_wander) return;

	npc->t_next_wander = now + NPC_WANDER_MIN_MS +
	                     npc_rand() % (NPC_WANDER_MAX_MS - NPC_WANDER_MIN_MS);

	int x, y;
	if (map_random_spot_in_room(&game->map, npc->room, (int)(npc_rand() & 0x7FFF), &x, &y) != 0)
		return;
	if (!tile_free(game, idx, x, y)) return;
	npc_goto(game, idx, x, y);
}

void npc_movement_update(Game *game) {
	uint64_t now = millis();
	int moved = 0;

	for (int i = 0; i < game->nb_npcs; i++) {
		NPC *npc = &game->npcs[i];
		if (!npc->present) continue;

		/* Les ordres recus du modele continuent de s'executer pendant la
		 * conversation : c'est l'errance spontanee qui est suspendue. */
		if (!npc_frozen(game, i)) npc_maybe_wander(game, i, now);

		if (!npc->moving) continue;
		if (now < npc->t_next_step) continue;
		npc->t_next_step = now + NPC_STEP_MS;
		if (npc_step(game, i)) moved = 1;
	}

	/* Si l'interlocuteur a quitte la piece, on en designe un autre. Tant qu'il
	 * est la, il le reste : un autre qui vient se placer plus pres ne prend pas
	 * sa place. */
	npc_refresh_target(game);

	if (moved) {
		ask_for_display_update(game);
		print_talk_hint(game);   /* quelqu'un a pu entrer ou sortir de la piece */
		/* print_talk_hint ecrit en haut de l'ecran : sans cela le curseur
		 * resterait la, au milieu du texte qu'on est en train de taper. */
		move_cursor_back(game);
	}
}

/* Applique une instruction de deplacement proposee par le modele. Le moteur
 * valide tout : un identifiant inconnu, une piece interdite ou un personnage
 * absent sont simplement ignores, comme partout ailleurs. */
const char *npc_move_result_name(NpcMoveResult result) {
	switch (result) {
		case NPC_MOVE_NO_OP:         return "no_op";
		case NPC_MOVE_ACCEPTED:      return "accepted";
		case NPC_MOVE_REJECTED:      return "rejected";
		case NPC_MOVE_NOT_REQUESTED: return "not_requested";
	}
	return "rejected";
}

NpcMoveResult npc_apply_move_order(Game *game, int idx, const char *order) {
	if (!order || !*order) return NPC_MOVE_NOT_REQUESTED;

	NPC *npc = &game->npcs[idx];

	if (strcmp(order, "reste") == 0) {
		bool was_moving = npc->moving;
		npc->moving = false;
		return was_moving ? NPC_MOVE_ACCEPTED : NPC_MOVE_NO_OP;
	}
	if (!npc->can_move) return NPC_MOVE_REJECTED;

	/* Un pas vers le joueur ou un pas en arriere : c'est le geste le plus
	 * courant en interrogatoire, il doit rester immediat. */
	if (strcmp(order, "approche") == 0 || strcmp(order, "recule") == 0) {
		bool towards = (strcmp(order, "approche") == 0);
		const int dx[4] = { 0, 0, -2, 2 };
		const int dy[4] = { -1, 1, 0, 0 };

		double best = distance(npc->x, npc->y, game->player.x, game->player.y);
		int bx = npc->x, by = npc->y;
		for (int i = 0; i < 4; i++) {
			if (!map_can_step(&game->map, npc->x, npc->y, dx[i], dy[i])) continue;
			int nx = npc->x + dx[i], ny = npc->y + dy[i];
			if (!tile_free(game, idx, nx, ny)) continue;
			/* Rester dans sa piece si les changements sont interdits. */
			if (!npc->can_change_room && map_room_at(&game->map, nx, ny) != npc->room) continue;

			double d = distance(nx, ny, game->player.x, game->player.y);
			if (towards ? (d < best) : (d > best)) { best = d; bx = nx; by = ny; }
		}
		if (bx != npc->x || by != npc->y) {
			npc->x = bx; npc->y = by;
			npc_refresh_room(game, idx);
			ask_for_display_update(game);
			return NPC_MOVE_ACCEPTED;
		}
		return NPC_MOVE_REJECTED;
	}

	if (strncmp(order, "piece:", 6) == 0) {
		int room = map_room_by_id(&game->map, order + 6);
		if (getenv("OFFSCRIPT_DEBUG_MOVE"))
			chat_addf(game, CHAT_SYSTEM, -1, "[debug] piece '%s' -> room=%d (npc room=%d, change=%d)",
			          order + 6, room, npc->room, npc->can_change_room);
		if (room == ROOM_NONE) return NPC_MOVE_REJECTED;
		int x, y;
		if (map_random_spot_in_room(&game->map, room, (int)(npc_rand() & 0x7FFF), &x, &y) == 0) {
			bool ok = npc_goto(game, idx, x, y);
			if (getenv("OFFSCRIPT_DEBUG_MOVE"))
				chat_addf(game, CHAT_SYSTEM, -1, "[debug] goto (%d,%d) -> %s, chemin=%d",
				          x, y, ok ? "ok" : "REFUSE",
				          map_path_len(&game->map, npc->x, npc->y, x, y));
			return ok ? NPC_MOVE_ACCEPTED : NPC_MOVE_REJECTED;
		} else if (getenv("OFFSCRIPT_DEBUG_MOVE")) {
			chat_addf(game, CHAT_SYSTEM, -1, "[debug] aucune place libre dans la piece");
		}
		return NPC_MOVE_REJECTED;
	}

	if (strncmp(order, "rejoint:", 8) == 0) {
		for (int i = 0; i < game->nb_npcs; i++) {
			if (i == idx || !game->npcs[i].present) continue;
			if (!game->npcs[i].def->id) continue;
			if (strcmp(game->npcs[i].def->id, order + 8) != 0) continue;

			/* On s'arrete a cote, pas sur lui. */
			const int dx[4] = { -2, 2, 0, 0 };
			const int dy[4] = { 0, 0, -1, 1 };
			for (int k = 0; k < 4; k++) {
				int tx = game->npcs[i].x + dx[k], ty = game->npcs[i].y + dy[k];
				if (!map_can_stand(&game->map, tx, ty)) continue;
				if (!tile_free(game, idx, tx, ty)) continue;
				if (npc_goto(game, idx, tx, ty)) {
					/* On retient l'intention : a l'arrivee, il lui adressera
					 * la parole. Sinon il faisait le trajet pour rien. */
					npc->goes_to_talk_to = i;
					return NPC_MOVE_ACCEPTED;
				}
			}
			return NPC_MOVE_REJECTED;
		}
	}
	return NPC_MOVE_REJECTED;
}

/* ------------------------------------------------------------------ */
/* Dialogue : envoi, suivi du streaming, analyse memoire               */
/* ------------------------------------------------------------------ */

/* Decrit au personnage la scene ou il se trouve : sa piece, qui l'entoure, et
 * les pieces qu'il pourrait rejoindre. Tout vient de la carte, donc le modele
 * ne peut proposer que des deplacements realisables. */
static void build_scene(Game *game, int idx, PromptScene *sc) {
	memset(sc, 0, sizeof(*sc));

	NPC *npc = &game->npcs[idx];
	sc->can_move        = npc->can_move;
	sc->can_change_room = npc->can_change_room;
	sc->room_here       = map_room_name(&game->map, npc->room);

	/* met est pose apres la reponse (voir npc_talk_update) : pendant le premier
	 * echange, il est donc encore faux, ce qui est exactement ce qu'il faut. */
	sc->first_meeting         = !npc->met;
	sc->name_known_by_player  = npc->name_known;

	for (int i = 0; i < game->map.nb_rooms && sc->nb_rooms < PROMPT_MAX_SCENE; i++) {
		if (i == npc->room) continue;      /* il y est deja */
		sc->room_ids[sc->nb_rooms]   = game->map.rooms[i].id;
		sc->room_names[sc->nb_rooms] = game->map.rooms[i].name;
		sc->nb_rooms++;
	}

	for (int i = 0; i < game->nb_npcs; i++) {
		if (i == idx || !game->npcs[i].present) continue;

		if (sc->nb_others < PROMPT_MAX_SCENE) {
			sc->other_ids[sc->nb_others]   = game->npcs[i].def->id;
			sc->other_names[sc->nb_others] = game->npcs[i].def->name;
			sc->nb_others++;
		}
		/* Presents = dans la meme piece, sous leur vrai nom. Un personnage
		 * connait ses collegues : lui cacher leur nom parce que l'enqueteur
		 * ne le connait pas encore le rendait absurde (« je ne sais pas qui
		 * est cette personne a cote de moi »). C'est au contraire une facon
		 * naturelle d'apprendre les noms : il suffit de demander a l'autre. */
		if (game->npcs[i].room == npc->room && sc->nb_present < PROMPT_MAX_SCENE) {
			sc->present_names[sc->nb_present++] = game->npcs[i].def->name;
		}
	}
}

/* Envoi commun a la question du joueur et a l'intervention spontanee : meme
 * personnage, meme memoire, meme format de reponse. Seule change la scene
 * decrite dans le prompt (voir PromptScene.overheard). */
static int npc_send_to(Game *game, int idx, const char *text, bool overheard,
                       const char *overheard_speaker) {
	NPC *npc = &game->npcs[idx];
	NpcState *ns = memory_get_npc(game->save, npc->def->id);
	if (!ns) return NPC_TALK_NOBODY;

	/* Le prompt est reconstruit a chaque tour : la relation et les souvenirs
	 * pertinents changent. La partie stable est en tete pour rester
	 * identique d'un appel a l'autre (voir prompt.c). */
	PromptScene scene;
	build_scene(game, idx, &scene);
	scene.overheard         = overheard;
	scene.overheard_speaker = overheard_speaker;
	/* Il a traverse le batiment pour parler : lui interdire de rien dire (ce
	 * qu'on impose a celui qui surprend une conversation) le laisserait plante
	 * la sans un mot, exactement le bug que maybe_queue_arrival_talk corrige. */
	scene.came_to_speak     = overheard && game->interject_is_arrival;
	char *system_prompt = prompt_build_dialogue(game->story, game->save, npc->def, text, &scene);

	DeepseekMsg hist[64];
	int nh = 0;
	for (int i = 0; i < ns->nb_recent && nh < 64; i++) {
		hist[nh].role    = ns->recent[i].role;
		hist[nh].content = ns->recent[i].content;
		hist[nh].emotion = ns->recent[i].emotion;
		hist[nh].action  = ns->recent[i].action;
		hist[nh].move    = ns->recent[i].move;
		/* Les anciennes sauvegardes n'ont pas de champ move. Pour un PNJ qui
		 * peut bouger, rejouer ces tours avec "reste" maintient tout de meme le
		 * format a quatre cles que le modele doit continuer d'imiter. */
		if (scene.can_move && hist[nh].role &&
		    strcmp(hist[nh].role, "assistant") == 0 && !hist[nh].move)
			hist[nh].move = "reste";
		nh++;
	}

	game->pending_req = deepseek_ask_dialogue(system_prompt, hist, nh, text);
	free(system_prompt);
	if (!game->pending_req) {
		pdiag(game, PAIR_DANGER, "Impossible de lancer la requete de dialogue (%s).",
		      npc->def->id ? npc->def->id : "npc inconnu");
		return NPC_TALK_ERROR;
	}

	/* La question n'est pas encore ecrite dans l'historique : les deux tours
	 * ne sont valides qu'ensemble, apres succes, pour ne jamais laisser un
	 * tour "user" orphelin dans la sauvegarde. */
	snprintf(game->pending_question, sizeof(game->pending_question), "%s", text);
	game->pending_npc    = idx;
	game->pending_since  = millis();
	game->stream_printed = 0;
	game->reply_is_interjection = overheard;

	/* La question n'entre dans le fil qu'ici, une fois l'envoi accepte : une
	 * question affichee mais jamais posee laissait croire qu'on avait parle
	 * dans le vide. Elle doit precede la ligne du personnage, sinon la
	 * reponse s'afficherait au-dessus de la question. */
	if (!overheard) chat_add(game, CHAT_PLAYER, -1, text);

	/* La ligne du personnage est ouverte dans la foulee, avec des points
	 * d'attente : on voit qui reflechit, et la replique vient prendre la
	 * place des points au lieu d'apparaitre de nulle part. */
	chat_stream_begin(game, idx);
	game->stream_started = 1;
	return NPC_TALK_SENT;
}

int npc_talk_send(Game *game, const char *text) {
	/* Une seule replique a la fois : une deuxieme reponse qui arriverait en
	 * parallele s'insererait au milieu de la premiere dans le fil. */
	if (game->pending_req) return NPC_TALK_BUSY;

	/* Nommer quelqu'un, c'est lui parler. */
	retarget_from_text(game, text);

	int idx = npc_refresh_target(game);
	if (idx < 0) return NPC_TALK_NOBODY;

	/* Le joueur reprend la parole : la chaine d'interventions repart de zero
	 * et celle qui attendait son tour est annulee, elle n'a plus lieu d'etre. */
	game->interject_chain = 0;
	game->interject_npc   = -1;

	return npc_send_to(game, idx, text, false, NULL);
}

/* ------------------------------------------------------------------ */
/* Interventions entre personnages                                     */
/* ------------------------------------------------------------------ */

/* Au-dela, deux personnages se renverraient la balle sans fin et le joueur
 * n'aurait plus qu'a regarder. */
#define INTERJECT_CHAIN_MAX 2
#define INTERJECT_CHANCE    45   /* pour cent */

/* Choisit, apres un echange, qui pourrait bien reagir. Ne fait que designer :
 * la requete part plus tard, quand la replique en cours est terminee. */
/* Quelqu'un vient d'arriver aupres de la personne qu'il voulait voir : il lui
 * adresse la parole. Sans cela, un personnage traversait le batiment sur
 * instruction du modele et, une fois sur place, restait muet. */
static void maybe_queue_arrival_talk(Game *game) {
	if (game->interject_npc >= 0 || game->pending_req) return;
	if (game->interject_chain >= INTERJECT_CHAIN_MAX) return;

	for (int i = 0; i < game->nb_npcs; i++) {
		NPC *npc = &game->npcs[i];
		if (!npc->present || npc->moving) continue;

		int target = npc->goes_to_talk_to;
		if (target < 0 || target >= game->nb_npcs) continue;
		npc->goes_to_talk_to = -1;              /* l'intention est consommee */

		if (!game->npcs[target].present) continue;
		if (game->npcs[target].room != npc->room) continue;

		snprintf(game->interject_context, sizeof(game->interject_context),
		         "Tu viens de traverser le batiment pour rejoindre %s, dans %s. "
		         "Tu es maintenant devant %s : adresse-lui la parole, dis-lui ce que "
		         "tu avais a lui dire.",
		         game->npcs[target].def->name,
		         map_room_name(&game->map, npc->room) ? map_room_name(&game->map, npc->room) : "cette piece",
		         game->npcs[target].def->name);

		game->interject_npc        = i;
		game->interject_speaker    = target;
		game->interject_is_arrival = 1;
		return;
	}
}

static void maybe_queue_interjection(Game *game, int speaker,
                                     const char *heard_question, const char *heard_line) {
	if (game->interject_chain >= INTERJECT_CHAIN_MAX) return;
	if (!heard_line || !*heard_line) return;

	/* Uniquement entre gens qui partagent la piece du joueur : une reaction
	 * venue d'une autre piece n'aurait aucun sens. */
	int here = map_room_at(&game->map, game->player.x, game->player.y);
	if (here == ROOM_NONE) return;

	int candidates[NPC_MAX], n = 0;
	for (int i = 0; i < game->nb_npcs; i++) {
		if (i == speaker || !game->npcs[i].present) continue;
		if (game->npcs[i].room != here) continue;
		candidates[n++] = i;
	}
	if (n == 0) return;
	if ((int)(npc_rand() % 100) >= INTERJECT_CHANCE) return;

	int pick = candidates[npc_rand() % (unsigned)n];

	const char *speaker_name = npc_display_name(&game->npcs[speaker]);
	if (heard_question && *heard_question) {
		snprintf(game->interject_context, sizeof(game->interject_context),
		         "%s vient de demander a %s : \"%s\"\nEt %s a repondu : \"%s\"",
		         game->save->player_name ? game->save->player_name : "L'enqueteur",
		         speaker_name, heard_question, speaker_name, heard_line);
	} else {
		/* Reaction a une reaction : personne n'a rien demande, c'est une
		 * remarque lancee dans la piece. */
		snprintf(game->interject_context, sizeof(game->interject_context),
		         "%s vient de dire, devant tout le monde : \"%s\"",
		         speaker_name, heard_line);
	}

	game->interject_npc        = pick;
	game->interject_speaker    = speaker;
	game->interject_is_arrival = 0;
}

/* Lance l'intervention en attente, une fois la voie libre. */
static void start_queued_interjection(Game *game) {
	if (game->interject_npc < 0 || game->pending_req) return;

	int idx = game->interject_npc;
	game->interject_npc = -1;

	/* Il a pu quitter la piece entre-temps. */
	int here = map_room_at(&game->map, game->player.x, game->player.y);
	if (!game->npcs[idx].present || game->npcs[idx].room != here) return;

	const char *heard_from = npc_display_name(&game->npcs[game->interject_speaker]);
	if (npc_send_to(game, idx, game->interject_context, true, heard_from) == NPC_TALK_SENT)
		game->interject_chain++;
}

/* Garde-fou. Une requete qui ne revient jamais (reseau qui avale la
 * connexion sans la fermer) bloquait toute la partie : le jeu repondait
 * indefiniment « Laissez-le finir de repondre » sans qu'aucune touche ne
 * puisse en sortir. Passe ce delai, on abandonne et on rend la parole.
 *
 * Le DeepseekRequest n'est PAS libere : son fil de discussion en est encore
 * proprietaire et ecrira dedans quand il finira par se terminer. Perdre
 * quelques centaines d'octets une fois de temps en temps vaut mieux qu'une
 * ecriture dans de la memoire liberee. */
#define DIALOGUE_WATCHDOG_MS 180000

static int dialogue_watchdog(Game *game) {
	if (!game->pending_req) return 0;
	if (millis() - game->pending_since < DIALOGUE_WATCHDOG_MS) return 0;

	int idx = game->pending_npc;
	if (game->stream_started) chat_stream_end(game);

	game->pending_req = NULL;
	game->pending_npc = -1;
	game->stream_started = 0;
	game->stream_printed = 0;

	chat_addf(game, CHAT_SYSTEM, -1,
	          "%s n'a jamais repondu (delai depasse). Vous pouvez reposer la question.",
	          idx >= 0 ? npc_display_name(&game->npcs[idx]) : "Votre interlocuteur");
	pdiag(game, PAIR_DANGER, "Reponse abandonnee: delai de 180 s depasse.");
	return 1;
}

/* Les salutations et demandes d'identite ne peuvent legitimement produire ni
 * preuve, ni souvenir important, ni changement de relation. Les envoyer au
 * modele consommait pourtant un appel complet — parfois plusieurs milliers de
 * jetons de raisonnement — au moment de quitter chaque personnage. */
static bool analysis_question_is_trivial(const char *question) {
	char q[512];
	text_fold_ascii(q, sizeof(q), question ? question : "");
	if (strlen(q) > 96) return false;

	if (strstr(q, "qui etes vous") || strstr(q, "qui es tu") ||
	    strstr(q, "votre nom") || strstr(q, "comment vous appelez") ||
	    strstr(q, "comment t'appelles") || strstr(q, "vous meme") ||
	    strcmp(q, "et vous") == 0 || strcmp(q, "et vous ?") == 0)
		return true;

	return strcmp(q, "bonjour") == 0 || strcmp(q, "salut") == 0 ||
	       strcmp(q, "bonsoir") == 0;
}

/* Lance l'analyse memoire (2e appel, en tache de fond) si l'intervalle
 * configure est atteint. Elle a son propre emplacement : elle ne doit jamais
 * empecher le joueur de poser la question suivante. */
static void maybe_start_analysis(Game *game, int npc_idx, const char *question,
                                 const char *reply_line) {
	if (game->analysis_req) return;

	NPC *npc = &game->npcs[npc_idx];
	NpcState *ns = memory_get_npc(game->save, npc->def->id);
	if (!ns) return;

	int interval = game->story->memory.memory_analysis_interval;
	if (interval < 1) interval = 1;
	if (ns->exchanges_since_analysis < interval) return;
	if (analysis_question_is_trivial(question)) {
		ns->exchanges_since_analysis = 0;
		return;
	}

	char *p = prompt_build_analysis(game->story, npc->def, question, reply_line);
	game->analysis_req = deepseek_ask_raw(p, "Analyse cet echange.");
	free(p);
	if (game->analysis_req) {
		game->analysis_npc = npc_idx;
		ns->exchanges_since_analysis = 0;
	}
}

/* L'analyse n'est lancee que tous les `memory_analysis_interval` echanges,
 * pour ne pas payer un appel par phrase. Consequence genante : tant qu'elle
 * n'a pas tourne, le carnet ne montre ni les faits qui viennent d'etre
 * obtenus, ni le changement d'attitude — le joueur croit que rien n'a compte.
 *
 * On solde donc l'ardoise des qu'un personnage cesse d'etre en face de nous :
 * quitter la piece met le carnet a jour, ce qui est aussi le moment ou l'on
 * va le consulter. */
static void flush_stale_analysis(Game *game) {
	if (game->analysis_req || game->pending_req) return;

	int here = map_room_at(&game->map, game->player.x, game->player.y);

	for (int i = 0; i < game->nb_npcs; i++) {
		NPC *npc = &game->npcs[i];
		if (!npc->present || !npc->met) continue;
		if (npc->room == here) continue;          /* encore en face de nous */

		NpcState *ns = memory_get_npc(game->save, npc->def->id);
		if (!ns || ns->exchanges_since_analysis <= 0) continue;
		if (ns->nb_recent < 2) continue;

		/* Le dernier tour complet de la conversation. */
		const char *question = NULL, *answer = NULL;
		for (int m = ns->nb_recent - 1; m >= 0; m--) {
			if (!answer && strcmp(ns->recent[m].role, "assistant") == 0) answer = ns->recent[m].content;
			else if (answer && strcmp(ns->recent[m].role, "user") == 0) { question = ns->recent[m].content; break; }
		}
		if (!answer) continue;
		if (analysis_question_is_trivial(question)) {
			ns->exchanges_since_analysis = 0;
			continue;
		}

		char *p = prompt_build_analysis(game->story, npc->def, question, answer);
		game->analysis_req = deepseek_ask_raw(p, "Analyse cet echange.");
		free(p);
		if (game->analysis_req) {
			game->analysis_npc = i;
			ns->exchanges_since_analysis = 0;
		}
		return;   /* une seule a la fois : l'emplacement d'analyse est unique */
	}
}

/* Applique le resultat de l'analyse. N'ecrit que dans les souvenirs, la
 * relation et les decouvertes du joueur : jamais dans recent_messages, qui a
 * pu avancer entre-temps. */
static void apply_analysis(Game *game, int npc_idx, const char *raw) {
	NPC *npc = &game->npcs[npc_idx];
	AnalysisResult a;
	if (!analysis_parse(raw, game->story, npc->def, &a)) {
		diag_analysis_rejected(game, npc_idx, raw);
		pdiag(game, PAIR_DANGER, "Analyse rejetee (%s): format ou identifiants invalides.",
		      npc->def->id ? npc->def->id : "npc inconnu");
		return;
	}
	NpcState *state_before = memory_get_npc(game->save, npc->def->id);
	Relation relation_before = state_before ? state_before->rel : (Relation){0, 0, 0, 0};
	int facts_before = game->save->nb_known_facts;
	int clues_before = game->save->nb_discovered_clues;
	if (!a.grounded) {
		diag_analysis(game, npc_idx, &a, facts_before, clues_before, relation_before);
		pdiag(game, PAIR_WARN, "Analyse ignoree (%s): reponse non fondee sur le scenario.",
		      npc->def->id ? npc->def->id : "npc inconnu");
		analysis_free(&a);
		return;
	}

	int min_imp = game->story->memory.minimum_importance_to_store;
	if (a.remember && a.summary && a.importance >= min_imp) {
		memory_add(game->save, npc->def->id, a.type, a.summary,
		           a.importance, a.emotion,
		           (const char **)a.tags, a.nb_tags, game->save->game_time);
		memory_cleanup(game->save, npc->def->id,
		               game->story->memory.long_term_memory_limit);
	}

	memory_update_relationship(game->save, npc->def->id,
	                           a.trust_d, a.affection_d, a.fear_d, a.suspicion_d);

	for (int i = 0; i < a.nb_learned_facts; i++) {
		if (memory_player_learn_fact(game->save, a.learned_fact_ids[i])) {
			const Fact *f = story_fact(game->story, a.learned_fact_ids[i]);
			if (f && f->text) pinfo_c(game, PAIR_GOOD, "Note au journal : %s\n", f->text);
		}
	}
	if (a.revealed_secret_id)
		memory_reveal_secret(game->save, npc->def->id, a.revealed_secret_id);

	/* Une piece a conviction produite vaut tous les faits qu'elle etablit :
	 * c'est le seul chemin vers les faits que personne ne connait de vive
	 * voix, et donc, souvent, vers les elements a charge contre le coupable. */
	if (a.produced_clue_id && memory_player_discover_clue(game->save, a.produced_clue_id)) {
		for (int i = 0; i < game->story->nb_clues; i++) {
			const Clue *cl = &game->story->clues[i];
			if (!cl->id || strcmp(cl->id, a.produced_clue_id) != 0) continue;

			chat_addf(game, CHAT_SYSTEM, -1, "Piece a conviction : %s",
			          cl->name ? cl->name : cl->id);
			for (int k = 0; k < cl->nb_reveals; k++) {
				if (!memory_player_learn_fact(game->save, cl->reveals_fact_ids[k])) continue;
				const Fact *f = story_fact(game->story, cl->reveals_fact_ids[k]);
				if (f && f->text)
					pinfo_c(game, PAIR_GOOD, "Note au journal : %s\n", f->text);
			}
			break;
		}
	}

	diag_analysis(game, npc_idx, &a, facts_before, clues_before, relation_before);
	analysis_free(&a);
}

void npc_talk_update(Game *game) {
	/* L'analyse tourne en parallele du dialogue : on la releve d'abord, elle
	 * n'empeche rien. */
	if (game->analysis_req) {
		char *raw = NULL;
		int st = deepseek_poll_raw(game->analysis_req, &raw);
		if (st != 0) {
			game->analysis_req = NULL;
			if (st == 1 && raw) {
				apply_analysis(game, game->analysis_npc, raw);
				game_autosave(game);
			} else if (st < 0)
				pdiag(game, PAIR_DANGER, "Analyse modele echouee (%s).",
				      game->analysis_npc >= 0 ? game->npcs[game->analysis_npc].def->id : "npc inconnu");
			free(raw);
			game->analysis_npc = -1;
		}
	}

	if (dialogue_watchdog(game)) return;

	/* La voie est libre : si quelqu'un attend pour reagir, c'est le moment. */
	if (!game->pending_req) {
		/* Une arrivee "je viens te parler" declenche l'echange, au meme titre
		 * qu'une reaction a ce qui vient d'etre dit. */
		maybe_queue_arrival_talk(game);
		start_queued_interjection(game);
		if (!game->pending_req) {
			flush_stale_analysis(game);
			return;
		}
	}

	NPC *npc = &game->npcs[game->pending_npc];

	/* L'emoji se referme avant la replique : l'expression arrive sur le visage
	 * du personnage pendant qu'il parle, et n'est jamais ecrite dans le chat. */
	char *emoji = deepseek_poll_emotion(game->pending_req);
	if (emoji) {
		/* fixed_face : l'emotion continue d'etre demandee (elle sert au
		 * journal et garde le format de reponse uniforme) mais le visage du
		 * personnage reste celui de sa fiche. */
		int face = npc->def->fixed_face ? -1 : dialogue_face_from_emoji(emoji);
		if (face >= 0 && face < NB_FACES) {
			npc->face_id = face;
			ask_for_display_update(game);
		}
		free(emoji);
	}

	char *chunk = deepseek_poll_stream(game->pending_req);
	if (chunk) {
		chat_stream_chunk(game, chunk);
		game->stream_printed += strlen(chunk);
		free(chunk);
	}

	DialogueReply reply;
	bool structured = true;
	int status = deepseek_poll(game->pending_req, &reply, &structured);
	if (status == 0) return;

	game->pending_req = NULL;
	int idx = game->pending_npc;
	game->pending_npc = -1;
	int was_interjection = game->reply_is_interjection;
	game->reply_is_interjection = 0;

	if (status != 1) {
		if (game->stream_started) chat_stream_end(game);
		if (game->options.diagnostic_ingame_logs)
			pdiag(game, PAIR_DANGER, "Dialogue rejete (%s): reseau, HTTP ou contenu inutilisable.",
			      npc->def->id ? npc->def->id : "npc inconnu");
		else
			pinfo(game, "%s ne repond pas (erreur de requete).\n", npc_display_name(npc));
		return;
	}
	if (!structured)
		pdiag(game, PAIR_WARN, "Format dialogue incorrect (%s): prose recuperee.",
		      npc->def->id ? npc->def->id : "npc inconnu");

	/* Une intervention peut se solder par un silence : c'est le cas le plus
	 * frequent, et il ne doit laisser aucune trace, ni dans le fil ni dans la
	 * memoire du personnage. */
	if (was_interjection && (!reply.line || !*reply.line)) {
		chat_stream_end(game);   /* retire la ligne d'attente restee vide */
		dialogue_reply_free(&reply);
		return;
	}

	/* Ce que le flux n'avait pas encore montre : une reponse courte peut
	 * arriver entiere entre deux relevés, et le texte diffuse est toujours un
	 * prefixe de reply.line. */
	if (reply.line && strlen(reply.line) > game->stream_printed) {
		chat_stream_chunk(game, reply.line + game->stream_printed);
	}
	chat_stream_end(game);

	/* La reaction physique se lit avec la replique, pas dans la bande de
	 * notes du moteur : elle fait partie de la scene. */
	if (reply.action && reply.action[0])
		chat_add(game, CHAT_ACTION, idx, reply.action);

	/* Le personnage a peut-etre demande a bouger : le moteur verifie que le
	 * deplacement est autorise et realisable avant de l'appliquer. */
	if (getenv("OFFSCRIPT_DEBUG_MOVE"))
		chat_addf(game, CHAT_SYSTEM, -1, "[debug] move=%s",
		          reply.move ? reply.move : "(absent)");
	int x_before = npc->x, y_before = npc->y, room_before = npc->room;
	int dest_x_before = npc->dest_x, dest_y_before = npc->dest_y;
	bool moving_before = npc->moving;
	NpcMoveResult move_result = npc_apply_move_order(game, idx, reply.move);
	diag_dialogue(game, idx, game->pending_question, reply.line, reply.emotion,
	              reply.action, reply.move, move_result, was_interjection,
	              x_before, y_before, room_before, moving_before,
	              dest_x_before, dest_y_before);
	if (move_result == NPC_MOVE_REJECTED)
		pdiag(game, PAIR_WARN, "Deplacement refuse (%s): %s.",
		      npc->def->id ? npc->def->id : "npc inconnu",
		      reply.move ? reply.move : "ordre absent");

	/* Les deux tours sont valides ensemble, maintenant qu'on a une reponse. */
	NpcState *ns = memory_get_npc(game->save, npc->def->id);
	int limit = game->story->memory.recent_message_limit;
	memory_add_recent_message(game->save, npc->def->id, "user",
	                          game->pending_question, NULL, NULL, NULL, limit);
	memory_add_recent_message(game->save, npc->def->id, "assistant",
	                          reply.line, reply.emotion, reply.action, reply.move, limit);
	if (ns) ns->exchanges_since_analysis++;

	npc->met = true;
	game->save->game_time += 10;

	/* Un nom prononce est un nom appris. On regarde celui qui parle, mais
	 * aussi les gens presents : quand quelqu'un dit « demandez a Naomi », le
	 * joueur sait desormais qui est Naomi, et tout le fil se corrige. */
	if (reply.line && *reply.line) {
		for (int i = 0; i < game->nb_npcs; i++) {
			NPC *other = &game->npcs[i];
			if (other->name_known || !other->present) continue;
			if (i != idx && other->room != npc->room) continue;
			if (!npc_name_in_text(other->def->name, reply.line)) continue;

			other->name_known = true;
			chat_touch(game);
			ask_for_display_update(game);
		}
	}

	/* Sur une intervention, la question en attente etait adressee a QUELQU'UN
	 * D'AUTRE : la donner a l'analyse lui presentait un echange qui n'a jamais
	 * eu lieu, et elle creditait le joueur de faits qu'il n'avait pas demandes. */
	maybe_start_analysis(game, idx,
	                     was_interjection ? "(rien : personne ne lui a rien demande, "
	                                        "il est intervenu de lui-meme)"
	                                      : game->pending_question,
	                     reply.line);

	/* Quelqu'un d'autre dans la piece peut vouloir reagir a ce qui vient
	 * d'etre dit. On se contente de le designer : la requete partira au tour
	 * suivant, une fois la replique en cours entierement affichee. */
	maybe_queue_interjection(game, idx,
	                         was_interjection ? NULL : game->pending_question,
	                         reply.line);

	/* Aveu du coupable : c'est la fin de l'enquete, mais le moteur verifie
	 * tout avant d'y croire. Le drapeau doit venir du coupable tire au sort,
	 * et les preuves doivent reellement etre reunies : un personnage qui
	 * s'accuserait par complaisance, ou un modele qui poserait la cle sans
	 * raison, ne peut pas terminer la partie. */
	if (reply.confession &&
	    game->save->culprit_id && npc->def->id &&
	    strcmp(npc->def->id, game->save->culprit_id) == 0 &&
	    culprit_is_cornered(game->save)) {
		game->save->solved = true;
		game->confession_npc = idx;
	}

	dialogue_reply_free(&reply);

	game_autosave(game);
}
