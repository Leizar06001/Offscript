#include "includes.h"

/* Carnet de l'enqueteur (F3). Une page de recapitulatif et d'objectifs, puis
 * une page par personne rencontree. Tout ce qui s'y trouve vient de l'etat du
 * moteur : rien n'y est ecrit par le modele sans avoir ete valide. */


/* ------------------------------------------------------------------ */
/* Page 0 : l'affaire                                                  */
/* ------------------------------------------------------------------ */

static void draw_case_page(Game *game, WINDOW *win, int h, int w) {
	const Story *s = game->story;
	SaveState *st = game->save;
	int y = 2;

	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "L'AFFAIRE");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	y = wrap_print(win, y, 3, w - 6, s->premise);
	y++;

	if (s->victim_name)
		mvwprintw(win, y++, 3, "Victime : %s, %s", s->victim_name,
		          s->victim_role ? s->victim_role : "");
	if (s->crime_type)
		mvwprintw(win, y++, 3, "Fait : %s dans %s, entre %s et %s",
		          s->crime_type, s->crime_location ? s->crime_location : "",
		          s->crime_time_start ? s->crime_time_start : "",
		          s->crime_time_end ? s->crime_time_end : "");
	y++;

	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "OBJECTIFS");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	for (int i = 0; i < s->nb_central_questions && y < h - 6; i++) {
		mvwprintw(win, y, 3, "-");
		y = wrap_print(win, y, 5, w - 8, s->central_questions[i]);
	}
	y++;

	/* Ce que le joueur a effectivement etabli, et rien de plus. */
	int pair = st->nb_known_facts == 0 ? 9
	         : (st->nb_known_facts * 3 >= s->nb_facts ? PAIR_GOOD : PAIR_WARN);
	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y, 3, "CE QUE VOUS AVEZ ETABLI ");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	wattron(win, COLOR_PAIR(pair) | A_BOLD);
	wprintw(win, "(%d/%d)", st->nb_known_facts, s->nb_facts);
	wattroff(win, COLOR_PAIR(pair) | A_BOLD);
	y++;

	if (st->nb_known_facts == 0) {
		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, y++, 5, "Rien encore. Interrogez les personnes presentes.");
		wattroff(win, COLOR_PAIR(9));
	}
	for (int i = 0; i < st->nb_known_facts && y < h - 3; i++) {
		const Fact *f = story_fact(s, st->known_fact_ids[i]);
		if (!f || !f->text) continue;
		wattron(win, COLOR_PAIR(PAIR_GOOD));
		mvwprintw(win, y, 3, "+");
		wattroff(win, COLOR_PAIR(PAIR_GOOD));
		y = wrap_print(win, y, 5, w - 8, f->text);
	}
}

/* ------------------------------------------------------------------ */
/* Page par personnage                                                 */
/* ------------------------------------------------------------------ */

static void draw_npc_page(Game *game, WINDOW *win, int h, int w, int npc_idx) {
	NPC *npc = &game->npcs[npc_idx];
	const StoryCharacter *c = npc->def;
	NpcState *ns = memory_get_npc(game->save, c->id);
	int y = 2;

	int ncol = npc->name_known ? npc_color(npc_idx) : 9;
	wattron(win, COLOR_PAIR(ncol) | A_BOLD);
	mvwprintw(win, y++, 3, "%s", npc->name_known ? c->name : "PERSONNE NON IDENTIFIEE");
	wattroff(win, COLOR_PAIR(ncol) | A_BOLD);

	/* Tant qu'on ne l'a pas identifie, le carnet ne peut rien contenir sur lui :
	 * l'enqueteur ne sait ni son metier, ni son age. Les fiches de l'histoire
	 * disaient tout des la premiere seconde de la partie. */
	wattron(win, COLOR_PAIR(9));
	if (npc->name_known)
		mvwprintw(win, y++, 3, "%s%s%s", c->role ? c->role : "",
		          c->identity_age ? "  -  " : "", c->identity_age ? c->identity_age : "");
	else
		mvwprintw(win, y++, 3, "?  -  ?");

	/* Ou on l'a laisse : avec des personnages qui se deplacent, le carnet
	 * doit dire ou les retrouver. */
	const char *room = map_room_name(&game->map, npc->room);
	if (npc->present && room)
		mvwprintw(win, y++, 3, "Vu dans : %s", room);
	wattroff(win, COLOR_PAIR(9));
	y++;

	/* La description n'arrive qu'avec le nom : c'est le nom qui fait passer
	 * quelqu'un de silhouette a personne. */
	if (npc->name_known) {
		y = wrap_print(win, y, 3, w - 6, c->public_description);
	} else {
		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, y++, 3, "?");
		wattroff(win, COLOR_PAIR(9));
	}
	y++;

	if (!npc->met) {
		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, y++, 3, "Vous ne lui avez pas encore parle.");
		wattroff(win, COLOR_PAIR(9));
		return;
	}

	/* La relation en mots, jamais en chiffres : c'est ce que l'enqueteur
	 * ressent, pas une statistique. */
	if (ns) {
		wattron(win, A_BOLD);
		mvwprintw(win, y++, 3, "ATTITUDE ENVERS VOUS");
		wattroff(win, A_BOLD);
		struct { const char *label; int value; int positive; } axes[] = {
			{ "confiance", ns->rel.trust,     1 },
			{ "affection", ns->rel.affection, 1 },
			{ "peur     ", ns->rel.fear,      0 },
			{ "suspicion", ns->rel.suspicion, 0 },
		};
		for (int a = 0; a < 4; a++) {
			int col = a % 2 ? 30 : 5;
			int row = y + a / 2;
			mvwprintw(win, row, col, "%s ", axes[a].label);
			int pc = relation_color(axes[a].value, axes[a].positive);
			wattron(win, COLOR_PAIR(pc));
			wprintw(win, "%s", memory_relation_word(axes[a].value));
			wattroff(win, COLOR_PAIR(pc));
		}
		y += 2;
		y++;
	}

	/* Son alibi, tel qu'il l'a affirme (les personnages sans alibi n'en ont
	 * pas de ligne). */
	if (c->alibi_claim) {
		wattron(win, A_BOLD);
		mvwprintw(win, y++, 3, "CE QU'IL AFFIRME");
		wattroff(win, A_BOLD);
		y = wrap_print(win, y, 5, w - 8, c->alibi_claim);
		y++;
	}

	/* Les faits que ce personnage connait ET que le joueur a etablis : c'est
	 * la trace de ce qu'on a tire de lui. */
	int shown = 0;
	for (int i = 0; i < c->nb_known_facts; i++) {
		if (!memory_player_knows_fact(game->save, c->known_fact_ids[i])) continue;
		if (!shown) {
			wattron(win, A_BOLD);
			mvwprintw(win, y++, 3, "CE QUE VOUS AVEZ APPRIS DE LUI");
			wattroff(win, A_BOLD);
			shown = 1;
		}
		const Fact *f = story_fact(game->story, c->known_fact_ids[i]);
		if (f && f->text && y < h - 4) {
			mvwprintw(win, y, 3, "-");
			y = wrap_print(win, y, 5, w - 8, f->text);
		}
	}
	if (shown) y++;

	if (ns && ns->nb_revealed > 0) {
		wattron(win, COLOR_PAIR(PAIR_DANGER) | A_BOLD);
		mvwprintw(win, y++, 3, "AVEUX");
		wattroff(win, COLOR_PAIR(PAIR_DANGER) | A_BOLD);
		for (int i = 0; i < ns->nb_revealed && y < h - 4; i++) {
			for (int k = 0; k < c->nb_secrets; k++) {
				if (!c->secrets[k].id) continue;
				if (strcmp(c->secrets[k].id, ns->revealed_secret_ids[i]) != 0) continue;
				const Fact *f = story_fact(game->story, c->secrets[k].fact_id);
				if (f && f->text) {
					wattron(win, COLOR_PAIR(PAIR_DANGER));
					mvwprintw(win, y, 3, "!");
					wattroff(win, COLOR_PAIR(PAIR_DANGER));
					y = wrap_print(win, y, 5, w - 8, f->text);
				}
			}
		}
		y++;
	}

	if (ns && ns->nb_mems > 0) {
		wattron(win, A_BOLD);
		mvwprintw(win, y++, 3, "NOTES");
		wattroff(win, A_BOLD);
		for (int i = 0; i < ns->nb_mems && y < h - 3; i++) {
			mvwprintw(win, y, 3, "-");
			y = wrap_print(win, y, 5, w - 8, ns->mems[i].summary);
		}
	}
}

/* ------------------------------------------------------------------ */

void journal_show(Game *game) {
	int h, w;
	getmaxyx(stdscr, h, w);
	int win_w = w - 2;
	int win_h = h - 2;
	WINDOW *win = newwin(win_h, win_w, 1, 1);
	keypad(win, TRUE);
	/* lecture bloquante sur CETTE fenetre: sinon ncurses rend ESC seul
	 * au lieu d'assembler les sequences des touches flechees. */
	nodelay(win, FALSE);

	int page = 0;                     /* 0 = l'affaire, 1..n = personnages */
	int nb_pages = 1 + game->nb_npcs;

	nodelay(stdscr, FALSE);
	int ch = 0;
	do {
		/* Le jeu reclame les evenements de molette (voir main.c), donc le
		 * terminal ne les traduit plus en fleches : on refait la traduction
		 * ici, sinon la molette ne tournerait plus les pages du carnet. */
		if (ch == KEY_MOUSE) {
			MEVENT ev;
			ch = 0;
			if (getmouse(&ev) == OK) {
				if (ev.bstate & BUTTON4_PRESSED)      ch = KEY_UP;
				else if (ev.bstate & BUTTON5_PRESSED) ch = KEY_DOWN;
			}
		}

		if (ch == KEY_RIGHT || ch == KEY_DOWN) page = (page + 1) % nb_pages;
		else if (ch == KEY_LEFT || ch == KEY_UP) page = (page + nb_pages - 1) % nb_pages;

		werase(win);
		wattron(win, COLOR_PAIR(24));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(24));

		wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	mvwprintw(win, 0, 3, " CARNET  -  %s ", game->story->title);
	wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
		if (page == 0) draw_case_page(game, win, win_h, win_w);
		else           draw_npc_page(game, win, win_h, win_w, page - 1);

		/* Onglets: on n'annonce que les personnes rencontrees par leur nom. */
		int tx = 3;
		mvwprintw(win, win_h - 3, tx, "Pages:");
		tx += 7;
		for (int i = 0; i < nb_pages && tx < win_w - 12; i++) {
			const char *label = (i == 0) ? "Affaire" : npc_display_name(&game->npcs[i - 1]);
			int tcol = (i == 0) ? PAIR_HEADING
			         : (game->npcs[i - 1].name_known ? npc_color(i - 1) : 9);
			wattron(win, COLOR_PAIR(tcol));
			if (i == page) wattron(win, A_REVERSE);
			mvwprintw(win, win_h - 3, tx, " %.14s ", label);
			if (i == page) wattroff(win, A_REVERSE);
			wattroff(win, COLOR_PAIR(tcol));
			tx += (int)strlen(label) + 3;
			if (tx > win_w - 16) break;
		}

		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, win_h - 2, 3, "[Gauche/Droite] page   [F3] ou [ESC] fermer");
		wattroff(win, COLOR_PAIR(9));
		wrefresh(win);
	} while ((ch = wgetch(win)) != 267 && ch != 27);   /* 267 = F3 */
	wtimeout(stdscr, INPUT_TIMEOUT_MS);

	delwin(win);

	/* Le carnet a recouvert la carte et le chat : tout est restaure a
	 * l'identique, historique du chat compris. */
	restore_game_screen(game);
}
