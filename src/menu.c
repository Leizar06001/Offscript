#include "includes.h"
#include "prompt.h"

#include <openssl/crypto.h>
#include <sys/stat.h>


/* ------------------------------------------------------------------ */
/* Cle d'API                                                           */
/* ------------------------------------------------------------------ */

/* Saisie de la cle. Masquee a l'affichage, mais on accepte le collage
 * (chaque caractere colle arrive comme une frappe). Renvoie false si le
 * joueur abandonne avec ESC. */
static bool ask_api_key(char *out, size_t out_size, const char *message) {
	int h, w;
	getmaxyx(stdscr, h, w);
	int win_w = w - 8 < 90 ? w - 8 : 90;
	int win_h = 13;
	WINDOW *win = newwin(win_h, win_w, (h - win_h) / 2, (w - win_w) / 2);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	size_t len = 0;
	out[0] = '\0';

	for (;;) {
		werase(win);
		wattron(win, COLOR_PAIR(24));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(24));

		wattron(win, A_BOLD);
		mvwprintw(win, 1, 3, "CLE D'API DEEPSEEK");
		wattroff(win, A_BOLD);

		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, 3, 3, "%s", message);
		mvwprintw(win, 4, 3, "A creer sur https://platform.deepseek.com (commence par sk-).");
		mvwprintw(win, 5, 3, "Elle sera chiffree dans %.*s",
		          win_w - 30, apikey_path());
		mvwprintw(win, 6, 3, "et liee a cette machine et a ce compte.");
		wattroff(win, COLOR_PAIR(9));

		mvwprintw(win, 8, 3, "Cle : ");
		/* Masquee : la cle ne doit pas rester lisible a l'ecran ni dans une
		 * capture. On montre juste les 3 derniers caracteres pour permettre
		 * de verifier une faute de collage. */
		int shown = (int)len;
		if (shown > win_w - 20) shown = win_w - 20;
		for (int i = 0; i < shown; i++) {
			bool tail = (len >= 3 && (size_t)i >= len - 3);
			mvwaddch(win, 8, 9 + i, tail ? (unsigned char)out[i] : '*');
		}

		mvwprintw(win, win_h - 2, 3, "[Entree] valider   [ESC] quitter   (%zu caracteres)", len);
		wrefresh(win);

		int ch = wgetch(win);
		if (ch == 27) { delwin(win); return false; }
		if (ch == '\n' || ch == KEY_ENTER) {
			if (len > 0) break;
			continue;
		}
		if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (len > 0) out[--len] = '\0';
			continue;
		}
		if (ch >= 32 && ch <= 126 && len + 1 < out_size) {
			out[len++] = (char)ch;
			out[len] = '\0';
		}
	}

	delwin(win);
	clear();
	refresh();
	return true;
}

static void show_status(const char *text) {
	int h, w;
	getmaxyx(stdscr, h, w);
	mvprintw(h / 2, (w - (int)strlen(text)) / 2, "%s", text);
	clrtoeol();
	refresh();
}

int menu_ensure_api_key(Game *game) {
	char key[APIKEY_MAX];
	const char *model = game->model_name;

	/* Une cle deja enregistree est utilisee telle quelle : on ne verifie pas
	 * a chaque lancement, ce serait un appel reseau pour rien. Si elle a ete
	 * revoquee, le premier dialogue echouera et le joueur pourra la changer
	 * en supprimant le fichier. */
	if (apikey_load(key, sizeof(key))) {
		deepseek_configure(key, model, game->story_model_name);
		OPENSSL_cleanse(key, sizeof(key));
		return 0;
	}

	const char *message = apikey_exists()
		? "Cle enregistree illisible (machine ou compte different) : ressaisissez-la."
		: "Premier lancement : saisissez votre cle d'API.";

	while (ask_api_key(key, sizeof(key), message)) {
		show_status("Verification de la cle...");
		int ok = deepseek_verify_key(key, model);

		if (ok == 1) {
			bool stored = apikey_store(key);
			deepseek_configure(key, model, game->story_model_name);
			OPENSSL_cleanse(key, sizeof(key));
			clear();
			show_status(stored ? "Cle enregistree." : "Cle acceptee (enregistrement impossible).");
			napms(700);
			clear();
			refresh();
			return 0;
		}

		if (ok == -1) {
			/* Injoignable : refuser la cle serait arbitraire, on la garde
			 * sans l'enregistrer pour ne pas figer une cle douteuse. */
			deepseek_configure(key, model, game->story_model_name);
			OPENSSL_cleanse(key, sizeof(key));
			clear();
			show_status("Serveur injoignable : cle acceptee sans verification.");
			napms(1200);
			clear();
			refresh();
			return 0;
		}

		message = "Cle refusee par DeepSeek. Verifiez-la et ressaisissez-la.";
		clear();
		refresh();
	}

	OPENSSL_cleanse(key, sizeof(key));
	snprintf(game->exit_error, sizeof(game->exit_error),
	         "aucune cle d'API : impossible de faire parler les personnages");
	game->print_error = 1;
	return -1;
}

/* ------------------------------------------------------------------ */
/* Choix de l'histoire                                                 */
/* ------------------------------------------------------------------ */

/* Ecrit `text` sur au plus `cols` COLONNES, puis complete avec des espaces.
 * La precision de printf ("%.30s") compte des octets : un texte accentue est
 * alors coupe bien avant la largeur voulue, et peut meme se retrouver casse au
 * milieu d'un caractere. */
static void print_cols(WINDOW *win, const char *text, int cols) {
	if (!text) text = "";
	int bytes = text_bytes_for_cols(text, cols);
	wprintw(win, "%.*s", bytes, text);

	int used = text_display_cols(text);
	if (used > cols) used = cols;
	for (int i = used; i < cols; i++) waddch(win, ' ');
}

static void draw_story_list(WINDOW *win, int h, int w, StoryInfo *list, int count, int sel) {
	werase(win);
	wattron(win, COLOR_PAIR(24));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(24));

	wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	mvwprintw(win, 1, 3, "CHOISIR UNE ENQUETE");
	wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

	wattron(win, COLOR_PAIR(9));
	mvwprintw(win, h - 2, 3, "[Haut/Bas] choisir   [Entree] commencer   [O] options   [ESC] quitter");
	wattroff(win, COLOR_PAIR(9));

	/* La largeur des genres se deduit de la fenetre : « enquete,
	 * science-fiction, thriller » etait coupe a « ...thr » par un %.30s, qui
	 * compte des OCTETS et se trompe donc encore davantage avec des accents. */
	const int title_w = 28;
	const int mark_w  = 30;            /* " [coupable fixe]" + " [sauvegarde]" */
	int genre_w = w - 8 - title_w - mark_w;
	if (genre_w < 12) genre_w = 12;

	for (int i = 0; i < count; i++) {
		int y = 3 + i;
		if (i == sel) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_TEXT));
		mvwprintw(win, y, 3, " ");
		print_cols(win, list[i].title, title_w);
		wattroff(win, COLOR_PAIR(PAIR_TEXT));

		wprintw(win, "  ");
		wattron(win, COLOR_PAIR(9));
		print_cols(win, list[i].genre_line, genre_w);
		wattroff(win, COLOR_PAIR(9));

		/* Qui est le coupable : ecrit par l'auteur, ou tire a chaque nouvelle
		 * partie. Ce n'est pas un detail au moment de choisir — rejouer une
		 * enquete dont la reponse change n'a rien a voir avec rejouer la meme.
		 * Toujours affiche, donc a une colonne fixe : la sauvegarde, elle, est
		 * facultative et passe apres. */
		wattron(win, COLOR_PAIR(PAIR_WARN));
		wprintw(win, list[i].fixed_culprit ? " [coupable fixe]" : " [tire au sort]");
		wattroff(win, COLOR_PAIR(PAIR_WARN));

		if (list[i].has_save) {
			wattron(win, COLOR_PAIR(PAIR_GOOD));
			wprintw(win, " [sauvegarde]");
			wattroff(win, COLOR_PAIR(PAIR_GOOD));
		}
		if (i == sel) wattroff(win, A_REVERSE);
	}

	/* Details de l'histoire selectionnee. */
	int y = 4 + count;
	wattron(win, COLOR_PAIR(9));
	mvwprintw(win, y++, 3, "%s, %d  -  %d personnages",
	          list[sel].location, list[sel].year, list[sel].nb_characters);
	y++;
	wrap_print(win, y, 3, w - 6, list[sel].premise);
	wattroff(win, COLOR_PAIR(9));

	wrefresh(win);
}

int menu_choose_story(Game *game) {
	StoryInfo *list = NULL;
	int count = story_list(&list);
	if (count <= 0) {
		snprintf(game->exit_error, sizeof(game->exit_error),
		         "aucune histoire trouvee dans %s/<dossier>/<nom>_story.json", STORY_DIR);
		game->print_error = 1;
		return -1;
	}

	int h, w;
	getmaxyx(stdscr, h, w);
	int win_h = 14 + count;
	int win_w = w - 2;
	if (win_h > h - 2) win_h = h - 2;
	WINDOW *win = newwin(win_h, win_w, 1, 1);
	keypad(win, TRUE);
	/* lecture bloquante sur CETTE fenetre: sinon ncurses rend ESC seul
	 * au lieu d'assembler les sequences des touches flechees. */
	nodelay(win, FALSE);

	int sel = 0;
	int chosen = -1;
	draw_story_list(win, win_h, win_w, list, count, sel);

	/* Lecture bloquante : rien a animer tant qu'une histoire n'est pas
	 * choisie, et cela evite de bruler du CPU dans le menu. */
	nodelay(stdscr, FALSE);
	int ch;
	int dead_reads = 0;
	while ((ch = wgetch(win)) != 27) {          /* 27 = ESC */
		/* wgetch rend ERR en boucle si l'entree est fermee (stdin redirige):
		 * sans ce garde-fou le menu tournerait indefiniment. */
		if (ch == ERR) { if (++dead_reads > 100) break; continue; }
		dead_reads = 0;

		if (ch == KEY_UP && sel > 0) sel--;
		else if (ch == KEY_DOWN && sel < count - 1) sel++;
		else if (ch == '\n' || ch == KEY_ENTER) { chosen = sel; break; }
		else if (ch == 'o' || ch == 'O') {
			/* Les reglages sont accessibles avant meme de commencer : c'est la
			 * qu'on change ses touches sans avoir a lancer une partie. */
			delwin(win);
			menu_options(game);
			win = newwin(win_h, win_w, 1, 1);
			keypad(win, TRUE);
			nodelay(win, FALSE);
		}
		draw_story_list(win, win_h, win_w, list, count, sel);
	}
	wtimeout(stdscr, INPUT_TIMEOUT_MS);

	delwin(win);
	clear();
	refresh();

	if (chosen < 0) { story_list_free(list, count); return -1; }

	char err[200];   /* tient dans exit_error */
	game->story = story_load(list[chosen].path, err, sizeof(err));
	story_list_free(list, count);

	if (!game->story) {
		snprintf(game->exit_error, sizeof(game->exit_error), "%s", err);
		game->print_error = 1;
		return -1;
	}

	game->nb_npcs = npc_build_from_story(game->npcs, NPC_MAX, game->story);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Choix de la partie                                                  */
/* ------------------------------------------------------------------ */

/* Saisie d'une ligne de texte visible (le nom du joueur). Renvoie false si le
 * joueur abandonne. */
static bool ask_line(const char *title, const char *hint, char *out, size_t out_size) {
	int h, w;
	getmaxyx(stdscr, h, w);
	int win_w = w - 8 < 70 ? w - 8 : 70;
	int win_h = 9;
	WINDOW *win = newwin(win_h, win_w, (h - win_h) / 2, (w - win_w) / 2);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	size_t len = strlen(out);

	for (;;) {
		werase(win);
		wattron(win, COLOR_PAIR(24));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(24));

		wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
		mvwprintw(win, 1, 3, "%s", title);
		wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, 3, 3, "%s", hint);
		mvwprintw(win, win_h - 2, 3, "[Entree] valider   [ESC] annuler");
		wattroff(win, COLOR_PAIR(9));

		wattron(win, COLOR_PAIR(PAIR_TEXT) | A_BOLD);
		mvwprintw(win, 5, 3, "> %s", out);
		wattroff(win, COLOR_PAIR(PAIR_TEXT) | A_BOLD);
		wmove(win, 5, 5 + text_display_cols(out));
		wrefresh(win);

		int ch = wgetch(win);
		if (ch == 27) { delwin(win); return false; }
		if (ch == '\n' || ch == KEY_ENTER) {
			if (len > 0) break;
			continue;
		}
		if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			/* On recule d'un caractere entier, pas d'un octet. */
			while (len > 0 && ((unsigned char)out[len - 1] & 0xC0) == 0x80) len--;
			if (len > 0) len--;
			out[len] = '\0';
			continue;
		}
		if (ch >= 32 && ch != 127 && len + 1 < out_size) {
			out[len++] = (char)ch;
			out[len] = '\0';
		}
	}

	delwin(win);
	clear();
	refresh();
	return true;
}

static void draw_save_list(WINDOW *win, int h, int w, const Story *story,
                           SaveInfo *list, int count, int sel) {
	(void)w;
	werase(win);
	wattron(win, COLOR_PAIR(24));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(24));

	wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	mvwprintw(win, 1, 3, "%s", story->title);
	wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

	wattron(win, COLOR_PAIR(9));
	mvwprintw(win, 2, 3, "Reprendre une enquete, ou en commencer une nouvelle.");
	mvwprintw(win, h - 2, 3, "[Haut/Bas] choisir   [Entree] valider   [ESC] quitter");
	wattroff(win, COLOR_PAIR(9));

	for (int i = 0; i < count && 4 + i < h - 4; i++) {
		int y = 4 + i;
		if (i == sel) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_TEXT));
		mvwprintw(win, y, 3, " ");
		print_cols(win, list[i].player_name, 28);
		wprintw(win, " ");
		wattroff(win, COLOR_PAIR(PAIR_TEXT));

		wattron(win, COLOR_PAIR(9));
		wprintw(win, "%4ld echange%s ", list[i].game_time / 10,
		        list[i].game_time / 10 > 1 ? "s" : " ");
		wattroff(win, COLOR_PAIR(9));

		if (list[i].solved) {
			wattron(win, COLOR_PAIR(PAIR_GOOD));
			wprintw(win, " [resolue]");
			wattroff(win, COLOR_PAIR(PAIR_GOOD));
		}
		if (i == sel) wattroff(win, A_REVERSE);
	}

	int y = 5 + count;
	if (y < h - 3) {
		if (sel == count) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_GOOD) | A_BOLD);
		mvwprintw(win, y, 3, " + Nouvelle enquete ");
		wattroff(win, COLOR_PAIR(PAIR_GOOD) | A_BOLD);
		if (sel == count) wattroff(win, A_REVERSE);
	}

	wrefresh(win);
}

int menu_choose_save(Game *game) {
	const Story *story = game->story;

	SaveInfo *list = NULL;
	int count = save_list(story->id, &list);

	int h, w;
	getmaxyx(stdscr, h, w);
	int win_h = 10 + count;
	if (win_h > h - 2) win_h = h - 2;
	int win_w = w - 2;
	WINDOW *win = newwin(win_h, win_w, 1, 1);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	/* Boucle : [ESC] doit toujours ramener a l'ecran precedent, jamais fermer
	 * le jeu. Depuis la liste des parties on remonte aux enquetes ; depuis la
	 * saisie du nom on revient a la liste des parties. */
	int sel = 0;
	for (;;) {
		int chosen;

		if (count > 0) {
			draw_save_list(win, win_h, win_w, story, list, count, sel);
			nodelay(stdscr, FALSE);
			chosen = -1;
			int ch, dead_reads = 0;
			while ((ch = wgetch(win)) != 27) {
				if (ch == ERR) { if (++dead_reads > 100) break; continue; }
				dead_reads = 0;
				if (ch == KEY_UP && sel > 0) sel--;
				else if (ch == KEY_DOWN && sel < count) sel++;
				else if (ch == '\n' || ch == KEY_ENTER) { chosen = sel; break; }
				draw_save_list(win, win_h, win_w, story, list, count, sel);
			}
			wtimeout(stdscr, INPUT_TIMEOUT_MS);
			if (chosen < 0) {   /* ESC : retour au choix de l'enquete */
				delwin(win);
				save_list_free(list, count);
				clear();
				refresh();
				return MENU_BACK;
			}
		} else {
			chosen = 0;   /* rien a reprendre : on part sur une nouvelle enquete */
		}

		if (count > 0 && chosen < count) {
			/* Reprise : le nom du joueur vient de la sauvegarde. */
			snprintf(game->save_path, sizeof(game->save_path), "%s", list[chosen].path);
			game->save = save_load(story, game->save_path);
			break;
		}

		/* Nouvelle enquete : le joueur donne son nom, l'histoire fournit le
		 * titre. Il sera « Detective Poireau » pour tout le monde. */
		char name[48] = "";
		char hint[160];
		snprintf(hint, sizeof(hint), "Vous serez appele : %s <votre nom>", story->player_title);
		if (!ask_line("VOTRE NOM", hint, name, sizeof(name))) {
			/* Annulation : on revient a la liste s'il y en a une, sinon au
			 * choix de l'enquete. */
			if (count > 0) continue;
			delwin(win);
			save_list_free(list, count);
			clear();
			refresh();
			return MENU_BACK;
		}

		snprintf(game->player.name, sizeof(game->player.name), "%s %s",
		         story->player_title, name);

		char slug[64];
		save_slug_from_name(name, slug, sizeof(slug));
		save_path_for(story->id, slug, game->save_path, sizeof(game->save_path));

		/* Un nom deja pris reprendrait la partie de quelqu'un d'autre. */
		struct stat sb;
		if (stat(game->save_path, &sb) == 0) game->save = save_load(story, game->save_path);
		if (!game->save) game->save = save_new(story, game->player.name);
		break;
	}

	delwin(win);
	save_list_free(list, count);
	clear();
	refresh();

	if (!game->save) {
		snprintf(game->exit_error, sizeof(game->exit_error),
		         "sauvegarde illisible : %s", game->save_path);
		game->print_error = 1;
		return -1;
	}

	/* Le nom affiche et celui du prompt sont les memes : c'est la sauvegarde
	 * qui fait foi une fois la partie commencee. */
	if (game->save->player_name)
		snprintf(game->player.name, sizeof(game->player.name), "%s", game->save->player_name);

	/* Le compteur d'API repart du total deja consomme par cette partie. */
	deepseek_usage_set(game->save->api_prompt_tokens,
	                   game->save->api_completion_tokens,
	                   game->save->api_calls);

	/* Une partie reprise retrouve les complements de solubilite ecrits a son
	 * lancement : sans cela, les indices generes et les faits donnes a un second
	 * personnage disparaitraient au rechargement, et l'enquete redeviendrait
	 * verrouillee alors que le joueur a peut-etre deja trouve la piece. */
	story_apply_additions(game->story, game->save);

	/* Les noms deja appris sont retrouves depuis la sauvegarde: un personnage
	 * avec qui on a deja parle reste connu apres un rechargement. */
	for (int i = 0; i < game->nb_npcs; i++) {
		NpcState *ns = memory_get_npc(game->save, game->npcs[i].def->id);
		if (ns && ns->nb_recent > 0) {
			game->npcs[i].met = true;
			for (int m = 0; m < ns->nb_recent; m++) {
				if (strcmp(ns->recent[m].role, "assistant") == 0 &&
				    npc_text_reveals_name(&game->npcs[i], ns->recent[m].content)) {
					game->npcs[i].name_known = true;
					break;
				}
			}
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Briefing de depart                                                  */
/* ------------------------------------------------------------------ */

/* La resolution est generee une fois par partie, pendant que le briefing est
 * affiche: les faits de l'histoire sont volontairement ambigus et ne
 * designent personne, donc sans cela le coupable tire au sort devrait
 * improviser et l'enquete ne tiendrait pas debout. */
/* ------------------------------------------------------------------ */
/* Barre de progression du lancement                                   */
/* ------------------------------------------------------------------ */

/* Le lancement enchaine des appels au modele dont on ne connait pas la duree.
 * Le compteur montre l'etape reelle ; la barre, elle, se remplit doucement sur
 * une base visuelle de 90 secondes pour eviter deux longs plateaux. Le travail
 * local garde son reflet mobile et les appels au modele deviennent un flux
 * binaire. Au-dela des 90 secondes, elle reste verte avec un temoin mobile :
 * le jeu ne pretend donc jamais que le travail est fini avant qu'il le soit.
 *
 * `step` : etapes deja terminees. `frame` : compteur d'animation, incremente par
 * l'appelant a chaque tour de boucle. */
#define LAUNCH_STEPS 7
#define LAUNCH_FILL_MS 90000ULL
#define LAUNCH_LABEL_MS 5000ULL

static void draw_launch_progress(WINDOW *win, int y, int width,
                                 int step, const char *label, int frame,
                                 bool receiving_model, uint64_t elapsed_ms,
                                 bool complete) {
	int win_w = getmaxx(win);
	char count[16];
	snprintf(count, sizeof(count), "%d/%d",
	         step < LAUNCH_STEPS ? step + 1 : LAUNCH_STEPS, LAUNCH_STEPS);
	int count_w = (int)strlen(count);

	/* La barre et son compteur forment un seul bloc centre. Sur un terminal
	 * etroit, la barre se contracte mais ne touche jamais le cadre. */
	int max_width = win_w - count_w - 8;
	if (max_width < 10) max_width = 10;
	if (width > max_width) width = max_width;
	if (width < 10) width = 10;

	int inner = width - 2;                        /* sans les crochets */
	/* Le numero reste celui de l'etape REELLE, mais le remplissage est lisse
	 * sur 90 secondes : les petites etapes locales ne disparaissent plus entre
	 * deux rafraichissements. Une fin precoce remplit immediatement le reste. */
	uint64_t capped = elapsed_ms < LAUNCH_FILL_MS ? elapsed_ms : LAUNCH_FILL_MS;
	int filled = complete ? inner : (int)((uint64_t)inner * capped / LAUNCH_FILL_MS);
	bool overtime = !complete && elapsed_ms >= LAUNCH_FILL_MS;

	/* Effacer les deux anciennes lignes sans emporter les montants du cadre. */
	mvwhline(win, y,     1, ' ', win_w - 2);
	mvwhline(win, y + 1, 1, ' ', win_w - 2);

	/* Le texte de l'etape vit au-dessus de la barre et se centre independamment
	 * d'elle. Les libelles sont volontairement assez courts pour tenir dans les
	 * fenetres minimales du jeu. */
	int label_w = label ? text_display_cols(label) : 0;
	int label_x = (win_w - label_w) / 2;
	if (label_x < 2) label_x = 2;
	wattron(win, COLOR_PAIR(PAIR_TEXT));
	mvwprintw(win, y, label_x, "%s", label ? label : "");
	wattroff(win, COLOR_PAIR(PAIR_TEXT));

	int block_w = width + 1 + count_w;
	int x = (win_w - block_w) / 2;
	if (x < 2) x = 2;

	wattron(win, COLOR_PAIR(9));
	mvwaddch(win, y + 1, x, '[');
	wattroff(win, COLOR_PAIR(9));

	for (int i = 0; i < inner; i++) {
		if (complete) {
			wattron(win, COLOR_PAIR(PAIR_GOOD));
			mvwaddwstr(win, y + 1, x + 1 + i, L"█");
			wattroff(win, COLOR_PAIR(PAIR_GOOD));
		} else if (overtime) {
			/* Le budget visuel est ecoule, pas le vrai travail : un temoin vert
			 * large de deux cases rebondit sur la barre orange. Trois cases toutes
			 * les deux images le rendent un peu plus vif sans devenir saccade. */
			int travel = inner - 2;
			int cycle = 2 * travel;
			int pos = cycle > 0 ? ((frame * 3) / 2) % cycle : 0;
			if (pos > travel) pos = cycle - pos;
			int pair = (i == pos || i == pos + 1) ? PAIR_GOOD : PAIR_WARN;
			wattron(win, COLOR_PAIR(pair));
			mvwaddwstr(win, y + 1, x + 1 + i, L"█");
			wattroff(win, COLOR_PAIR(pair));
		} else if (receiving_model) {
			/* Pendant le reseau, les donnees traversent TOUTE la barre. La
			 * progression ne disparait pas pour autant : la partie ecoulee devient
			 * verte, le reste demeure discret. */
			static const char bits[] = "0100111011010010";
			int nbits = (int)sizeof(bits) - 1;
			int pair = i < filled ? PAIR_GOOD : 9;
			int attrs = COLOR_PAIR(pair) | (i < filled ? A_BOLD : 0);
			wattron(win, attrs);
			mvwaddch(win, y + 1, x + 1 + i, bits[(i + frame) % nbits]);
			wattroff(win, attrs);
		} else if (i < filled) {
			/* Travail local deja represente par le chronometre : plein. */
			wattron(win, COLOR_PAIR(PAIR_GOOD));
			mvwaddwstr(win, y + 1, x + 1 + i, L"█");
			wattroff(win, COLOR_PAIR(PAIR_GOOD));
		} else {
			/* Animation locale historique : un reflet orange se deplace dans la
			 * partie qui reste a remplir. */
			int remaining = inner - filled;
			int local = i - filled;
			int cycle = 2 * remaining - 2;
			int pos = cycle > 0 ? frame % cycle : 0;
			if (pos >= remaining) pos = cycle - pos;
			int pair = local == pos ? PAIR_WARN : 9;
			wattron(win, COLOR_PAIR(pair));
			mvwaddwstr(win, y + 1, x + 1 + i, local == pos ? L"█" : L"░");
			wattroff(win, COLOR_PAIR(pair));
		}
	}

	wattron(win, COLOR_PAIR(9));
	mvwaddch(win, y + 1, x + 1 + inner, ']');
	wprintw(win, " %s", count);
	wattroff(win, COLOR_PAIR(9));
}

static const char *resolution_wait_label(uint64_t elapsed_ms) {
	static const char *labels[] = {
		"Le modele remet les faits dans le bon ordre...",
		"Les alibis passent au peigne fin...",
		"Le coupable travaille deja son air innocent...",
		"On recolle les morceaux sans perdre les empreintes...",
		"La chronologie repasse une derniere fois au propre...",
		"Les temoignages comparent leurs versions en silence...",
		"On pese les mobiles, sans la balance du legiste...",
		"Les contradictions commencent a transpirer...",
		"Chaque indice cherche sa place dans le dossier...",
		"Les fausses pistes sont priees de patienter dehors...",
		"Notre inspecteur virtuel relit les petites lignes...",
		"La reconstitution ajuste ses derniers details...",
	};
	return labels[(elapsed_ms / LAUNCH_LABEL_MS) %
	              (uint64_t)(sizeof(labels) / sizeof(labels[0]))];
}

static const char *evidence_wait_label(uint64_t elapsed_ms) {
	static const char *labels[] = {
		"Le modele cherche une seconde source fiable...",
		"Une preuve reclame un temoin un peu moins compromis...",
		"On complete le dossier, trombones sous controle...",
		"On verifie qui peut reellement sortir cette preuve...",
		"L'indice change de main, mais pas de version...",
		"Un temoin secondaire fouille poliment ses archives...",
		"Les acces au dossier sont passes en revue...",
		"La preuve prepare son double exemplaire...",
		"Le dernier verrou cherche discretement sa cle...",
	};
	return labels[(elapsed_ms / LAUNCH_LABEL_MS) %
	              (uint64_t)(sizeof(labels) / sizeof(labels[0]))];
}

/* Un element a charge que SEUL le coupable peut rapporter rend l'enquete
 * injouable : il faut la preuve pour obtenir l'aveu, et l'aveu pour obtenir la
 * preuve. Le moteur sait exactement lesquels manquent — il n'y a rien a deviner
 * la-dessus — donc le modele ne sert qu'a ecrire : une piece a conviction dans
 * le ton de l'affaire, et le second personnage qui la detient. Le moteur valide,
 * applique, et reverifie.
 * Renvoie le nombre de trous combles. N'emet AUCUN appel quand il n'y en a pas,
 * ce qui est le cas d'une histoire correctement ecrite. */
static int fill_solubility_gaps(Game *game, WINDOW *win, int status_y, int bar_w,
                                const SolutionResult *sol, uint64_t launch_started) {
	const char *missing[ANALYSIS_MAX_FACTS];
	int nb_missing = 0;

	for (int i = 0; i < sol->nb_facts; i++) {
		if (!story_fact_obtainable_without(game->story, sol->fact_ids[i],
		                                   game->save->culprit_id))
			missing[nb_missing++] = sol->fact_ids[i];
	}
	if (nb_missing == 0) {
		draw_launch_progress(win, status_y, bar_w, 4,
		                     "Toutes les preuves ont une sortie de secours.", 0, false,
		                     millis() - launch_started, false);
		wrefresh(win);
		return 0;
	}

	char *p = prompt_build_clue_fill(game->story, game->save->culprit_id,
	                                 missing, nb_missing);
	if (!p) return 0;

	DeepseekRequest *req = deepseek_ask_story(p, "Complete le dossier.");
	free(p);
	if (!req) return 0;

	int frame = 0;
	uint64_t wait_started = millis();
	char *raw = NULL;
	int st;
	while ((st = deepseek_poll_raw(req, &raw)) == 0) {
		const char *label = evidence_wait_label(millis() - wait_started);
		draw_launch_progress(win, status_y, bar_w, 4,
		                     label, frame, true, millis() - launch_started, false);
		frame++;
		wrefresh(win);
		usleep(120000);
	}

	int applied = 0;
	ClueFillResult fill;
	if (st == 1 && raw &&
	    clue_fill_parse(raw, game->story, game->save->culprit_id, missing, nb_missing, &fill)) {

		SaveState *sv = game->save;
		sv->gen_clues = realloc(sv->gen_clues,
		                        sizeof(GeneratedClue) * (size_t)(sv->nb_gen_clues + fill.nb_items));
		sv->extra_knowledge = realloc(sv->extra_knowledge,
		                              sizeof(ExtraKnowledge) * (size_t)(sv->nb_extra_knowledge + fill.nb_items));

		for (int i = 0; i < fill.nb_items; i++) {
			const ClueFill *it = &fill.items[i];

			GeneratedClue *gc = &sv->gen_clues[sv->nb_gen_clues++];
			memset(gc, 0, sizeof(*gc));
			gc->id          = strdup(it->clue_id);
			gc->name        = strdup(it->name);
			gc->description = strdup(it->description);
			gc->reveals_fact_ids = calloc(1, sizeof(char *));
			gc->reveals_fact_ids[0] = strdup(it->fact_id);
			gc->nb_reveals  = 1;

			/* La piece ne suffit pas : personne ne peut la sortir sans connaitre
			 * l'un des faits qu'elle etablit (voir character_holds_clue). C'est
			 * cette ligne-la qui debloque reellement l'enquete. */
			ExtraKnowledge *ek = &sv->extra_knowledge[sv->nb_extra_knowledge++];
			ek->npc_id  = strdup(it->holder_id);
			ek->fact_id = strdup(it->fact_id);

			applied++;
		}
		clue_fill_free(&fill);

		/* Versees dans l'histoire en memoire : a partir d'ici, le moteur ne fait
		 * plus la difference avec un indice d'auteur. */
		story_apply_additions(game->story, sv);
	}

	free(raw);
	return applied;
}

static void generate_solution(Game *game, WINDOW *win, int status_y, int bar_w) {
	uint64_t launch_started = millis();
	const char *ready = "L'enquete peut commencer, appuyez sur [ENTREE]";
	if (!game->save->culprit_id || game->save->culprit_solution) {
		/* Partie reprise : rien a recalculer, mais l'invitation ne doit apparaitre
		 * qu'ici, au meme endroit que pour une nouvelle enquete terminee. */
		draw_launch_progress(win, status_y, bar_w, LAUNCH_STEPS, ready, 0, false,
		                     0, true);
		wrefresh(win);
		return;
	}

	draw_launch_progress(win, status_y, bar_w, 0,
	                     "On ouvre le dossier et on sort les crayons rouges...", 0, false,
	                     0, false);
	wrefresh(win);

	char *p = prompt_build_solution(game->story, game->save->culprit_id);
	if (!p) return;

	DeepseekRequest *req = deepseek_ask_story(p, "Ecris la resolution.");
	free(p);
	if (!req) return;

	int frame = 0;
	uint64_t wait_started = millis();
	char *raw = NULL;
	int st;
	bool dropped_all = false;   /* aucun element a charge n'etait atteignable */
	bool no_facts    = false;   /* le modele n'en a propose aucun */
	while ((st = deepseek_poll_raw(req, &raw)) == 0) {
		const char *label = resolution_wait_label(millis() - wait_started);
		draw_launch_progress(win, status_y, bar_w, 1,
		                     label, frame, true, millis() - launch_started, false);
		frame++;
		wrefresh(win);
		usleep(120000);
	}

	/* Reponse recue : les phases suivantes sont locales et retrouvent le reflet
	 * mobile de la barre. Rien a annoncer si l'appel a echoue : le message
	 * d'erreur suit immediatement. */
	if (st == 1) {
		draw_launch_progress(win, status_y, bar_w, 2,
		                     "La reconstitution est arrivee. On dechiffre les notes...",
		                     frame++, false, millis() - launch_started, false);
		wrefresh(win);
	}

	if (st == 1 && raw) {
		SolutionResult sol;
		if (solution_parse(raw, game->story, &sol)) {
			game->save->culprit_solution = sol.solution;   /* transfert */
			game->save->culprit_brief    = sol.culprit_brief;
			sol.solution = NULL;
			sol.culprit_brief = NULL;

			draw_launch_progress(win, status_y, bar_w, 3,
			                     "On verifie que chaque preuve est vraiment trouvable...",
			                     frame++, false, millis() - launch_started, false);
			wrefresh(win);

			/* Avant de filtrer, on essaie de REPARER : les elements a charge que
			 * seul le coupable pouvait rapporter recoivent une piece a conviction
			 * et un second detenteur. Ce qui reste inatteignable apres ca est
			 * ecarte juste en dessous. */
			int filled = fill_solubility_gaps(game, win, status_y, bar_w, &sol,
			                                    launch_started);
			(void)filled;

			draw_launch_progress(win, status_y, bar_w, 5,
			                     "Dernier recoupement : faits, suspects et contradictions...",
			                     frame++, false, millis() - launch_started, false);
			wrefresh(win);

			/* Les elements a charge doivent etre atteignables SANS le coupable,
			 * sinon l'enquete est verrouillee : il faut la preuve pour obtenir
			 * l'aveu, et l'aveu pour obtenir la preuve. Le modele choisit
			 * volontiers des faits que seul le coupable connait ; on ne garde
			 * que ceux qu'un autre peut dire ou qu'une piece a conviction
			 * etablit. Le seuil de victoire suit (`(n+1)/2`, borne a n), donc
			 * reduire la liste ne rend rien inatteignable.
			 * Cas limite : si AUCUN n'est atteignable, on garde la liste telle
			 * quelle — c'est l'histoire qui est insoluble, et le dire vaut mieux
			 * que livrer une partie qu'on ne peut pas gagner sans le savoir. */
			int keep[ANALYSIS_MAX_FACTS], nb_keep = 0;
			for (int i = 0; i < sol.nb_facts; i++) {
				if (story_fact_obtainable_without(game->story, sol.fact_ids[i],
				                                 game->save->culprit_id))
					keep[nb_keep++] = i;
			}
			/* Deux echecs distincts, deux messages : le modele n'a propose aucun
			 * element (bug de generation), ou aucun n'est atteignable (histoire
			 * verrouillee). Un seul message pour les deux enverrait chercher la
			 * mauvaise cause. */
			if (sol.nb_facts == 0) {
				no_facts = true;
			} else if (nb_keep == 0) {
				for (int i = 0; i < sol.nb_facts; i++) keep[nb_keep++] = i;
				dropped_all = true;
			}

			game->save->culprit_fact_ids = calloc((size_t)nb_keep, sizeof(char *));
			for (int i = 0; i < nb_keep; i++) {
				game->save->culprit_fact_ids[i] = sol.fact_ids[keep[i]];
				sol.fact_ids[keep[i]] = NULL;
			}
			game->save->nb_culprit_facts = nb_keep;
			solution_free(&sol);   /* libere les faits ecartes */
		}
	}
	free(raw);

	/* Le joueur doit savoir qu'il part avec une enquete verrouillee : sans ce
	 * mot, il chercherait des heures une preuve que personne ne peut lui
	 * donner. */
	const char *warning = NULL;
	if (st != 1)          warning = "Resolution indisponible pour cette enquete.";
	else if (no_facts)    warning = "Attention : la resolution ne designe aucun element a charge.";
	else if (dropped_all) warning = "Attention : aucune preuve n'est accessible sans l'aveu.";

	draw_launch_progress(win, status_y, bar_w, 6,
	                     "On classe le dossier sans egarer la piece maitresse...",
	                     frame++, false, millis() - launch_started, false);
	wrefresh(win);
	game_autosave(game);

	/* Barre pleine conservee a l'ecran : le message final remplace le libelle,
	 * centre lui aussi, et le joueur voit clairement que tout est termine. */
	draw_launch_progress(win, status_y, bar_w, LAUNCH_STEPS, ready, frame, false,
	                     millis() - launch_started, true);
	/* Un avertissement eventuel occupe la ligne libre sous la barre. Il
	 * n'apparait lui aussi qu'une fois tout traitement termine. */
	if (warning) {
		int win_w = getmaxx(win);
		int warning_w = text_display_cols(warning);
		int warning_x = (win_w - warning_w) / 2;
		if (warning_x < 2) warning_x = 2;
		mvwhline(win, status_y + 2, 1, ' ', win_w - 2);
		wattron(win, COLOR_PAIR(PAIR_WARN));
		mvwprintw(win, status_y + 2, warning_x, "%s", warning);
		wattroff(win, COLOR_PAIR(PAIR_WARN));
	}
	wrefresh(win);
}

/* Question fermee. `dangerous` met la reponse affirmative en rouge : on ne
 * renonce pas a une enquete par megarde. */
static bool confirm(const char *title, const char *line1, const char *line2,
                    const char *yes_label, bool dangerous) {
	int h, w;
	getmaxyx(stdscr, h, w);
	int win_w = w - 8 < 74 ? w - 8 : 74;
	int win_h = 10;
	WINDOW *win = newwin(win_h, win_w, (h - win_h) / 2, (w - win_w) / 2);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	int sel = 1;   /* « Non » par defaut */
	for (;;) {
		werase(win);
		wattron(win, COLOR_PAIR(dangerous ? PAIR_DANGER : 24));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(dangerous ? PAIR_DANGER : 24));

		wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
		mvwprintw(win, 1, 3, "%s", title);
		wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

		wattron(win, COLOR_PAIR(PAIR_TEXT));
		if (line1) mvwprintw(win, 3, 3, "%s", line1);
		wattroff(win, COLOR_PAIR(PAIR_TEXT));
		wattron(win, COLOR_PAIR(9));
		if (line2) mvwprintw(win, 4, 3, "%s", line2);
		wattroff(win, COLOR_PAIR(9));

		if (sel == 0) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(dangerous ? PAIR_DANGER : PAIR_GOOD) | A_BOLD);
		mvwprintw(win, 6, 3, " %s ", yes_label);
		wattroff(win, COLOR_PAIR(dangerous ? PAIR_DANGER : PAIR_GOOD) | A_BOLD);
		if (sel == 0) wattroff(win, A_REVERSE);

		if (sel == 1) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_TEXT));
		mvwprintw(win, 6, 3 + (int)strlen(yes_label) + 4, " Non, continuer ");
		wattroff(win, COLOR_PAIR(PAIR_TEXT));
		if (sel == 1) wattroff(win, A_REVERSE);

		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, win_h - 2, 3, "[Gauche/Droite] choisir   [Entree] valider   [ESC] annuler");
		wattroff(win, COLOR_PAIR(9));
		wrefresh(win);

		int ch = wgetch(win);
		if (ch == ERR) continue;
		if (ch == 27) { delwin(win); return false; }
		if (ch == KEY_LEFT || ch == KEY_RIGHT) { sel = 1 - sel; continue; }
		if (ch == '\n' || ch == KEY_ENTER) { delwin(win); return sel == 0; }
	}
}

/* ------------------------------------------------------------------ */
/* Reglages                                                            */
/* ------------------------------------------------------------------ */

/* Les lignes du menu : une par touche a regler, puis les deux tarifs, puis
 * les dispositions toutes faites. */
#define OPT_ROW_REASONING (ACT_COUNT)
#define OPT_ROW_PRICE_IN  (ACT_COUNT + 1)
#define OPT_ROW_PRICE_OUT (ACT_COUNT + 2)
#define OPT_ROW_ZQSD      (ACT_COUNT + 3)
#define OPT_ROW_WASD      (ACT_COUNT + 4)
#define OPT_ROW_DEFAULTS  (ACT_COUNT + 5)
#define OPT_ROW_APIKEY    (ACT_COUNT + 6)
#define OPT_ROW_COUNT     (ACT_COUNT + 7)

/* Applique une disposition de deplacement sur la touche secondaire, en
 * laissant les fleches en place. Les lettres reprises ailleurs sont liberees :
 * en WASD, le « A » du deplacement entrait sinon en concurrence avec celui du
 * defilement, et l'une des deux actions devenait inatteignable. */
static void options_set_layout(Options *o, const char *letters) {
	const Action moves[4] = { ACT_UP, ACT_LEFT, ACT_DOWN, ACT_RIGHT };

	for (int i = 0; i < 4; i++) {
		for (int a = 0; a < ACT_COUNT; a++) {
			for (int k = 0; k < 2; k++) {
				if (o->keys[a][k] == letters[i]) o->keys[a][k] = OPT_KEY_NONE;
			}
		}
	}
	for (int i = 0; i < 4; i++) o->keys[moves[i]][1] = letters[i];
}

static void draw_options(WINDOW *win, int h, int w, const Options *o, int sel,
                         int capturing, const char *message) {
	(void)w;
	werase(win);
	wattron(win, COLOR_PAIR(24));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(24));

	wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	mvwprintw(win, 1, 3, "OPTIONS");
	wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

	wattron(win, COLOR_PAIR(9));
	mvwprintw(win, 2, 3, "Reglages communs a toutes les parties : %.*s",
	          w - 45, options_path());
	wattroff(win, COLOR_PAIR(9));

	int y = 4;
	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "TOUCHES");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);

	for (int a = 0; a < ACT_COUNT && y < h - 4; a++, y++) {
		char k1[24], k2[24];
		options_key_name(o->keys[a][0], k1, sizeof(k1));
		options_key_name(o->keys[a][1], k2, sizeof(k2));

		if (sel == a) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_TEXT));
		mvwprintw(win, y, 3, " %-24s ", options_action_label(a));
		wattroff(win, COLOR_PAIR(PAIR_TEXT));
		wattron(win, COLOR_PAIR(PAIR_WARN));
		wprintw(win, "%-14s %-14s", k1, k2);
		wattroff(win, COLOR_PAIR(PAIR_WARN));
		if (sel == a) wattroff(win, A_REVERSE);
	}

	y++;
	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "MODELE");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);

	if (y < h - 3) {
		if (sel == OPT_ROW_REASONING) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_TEXT));
		mvwprintw(win, y, 3, " %-24s ", "Raisonnement");
		wattroff(win, COLOR_PAIR(PAIR_TEXT));
		wattron(win, COLOR_PAIR(PAIR_WARN));
		wprintw(win, "%-6s", o->reasoning);
		wattroff(win, COLOR_PAIR(PAIR_WARN));
		wattron(win, COLOR_PAIR(9));
		wprintw(win, " (low / high / max)");
		wattroff(win, COLOR_PAIR(9));
		if (sel == OPT_ROW_REASONING) wattroff(win, A_REVERSE);
		y++;
	}

	y++;
	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "TARIFS DU MODELE (dollars par million de jetons)");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);

	const char *plabel[2] = { "Jetons envoyes", "Jetons recus" };
	double pval[2] = { o->price_in_per_m, o->price_out_per_m };
	for (int i = 0; i < 2 && y < h - 3; i++, y++) {
		if (sel == OPT_ROW_PRICE_IN + i) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_TEXT));
		mvwprintw(win, y, 3, " %-24s ", plabel[i]);
		wattroff(win, COLOR_PAIR(PAIR_TEXT));
		wattron(win, COLOR_PAIR(PAIR_WARN));
		wprintw(win, "%.4f $", pval[i]);
		wattroff(win, COLOR_PAIR(PAIR_WARN));
		if (sel == OPT_ROW_PRICE_IN + i) wattroff(win, A_REVERSE);
	}

	y++;
	const char *actions[4] = { " Disposition ZQSD ", " Disposition WASD ",
	                           " Tout remettre par defaut ",
	                           " Changer la cle d'API " };
	for (int i = 0; i < 4 && y < h - 2; i++, y++) {
		if (sel == OPT_ROW_ZQSD + i) wattron(win, A_REVERSE);
		wattron(win, COLOR_PAIR(PAIR_GOOD));
		mvwprintw(win, y, 3, "%s", actions[i]);
		wattroff(win, COLOR_PAIR(PAIR_GOOD));
		if (sel == OPT_ROW_ZQSD + i) wattroff(win, A_REVERSE);
	}

	wattron(win, COLOR_PAIR(9));
	if (capturing) {
		wattroff(win, COLOR_PAIR(9));
		wattron(win, COLOR_PAIR(PAIR_GOOD) | A_BOLD);
		mvwprintw(win, h - 2, 3, "Appuyez sur la touche a associer   ([Suppr] aucune, [ESC] annuler)");
		wattroff(win, COLOR_PAIR(PAIR_GOOD) | A_BOLD);
	} else if (message && *message) {
		wattroff(win, COLOR_PAIR(9));
		wattron(win, COLOR_PAIR(PAIR_WARN));
		mvwprintw(win, h - 2, 3, "%s", message);
		wattroff(win, COLOR_PAIR(PAIR_WARN));
	} else {
		mvwprintw(win, h - 2, 3,
		          "[Haut/Bas] choisir   [Entree] touche principale   [S] secondaire   [ESC] fermer");
		wattroff(win, COLOR_PAIR(9));
	}
	wrefresh(win);
}

/* Saisie d'un nombre pour un tarif. Renvoie false si le joueur annule. */
static bool ask_price(const char *label, double *value) {
	char text[32];
	snprintf(text, sizeof(text), "%.4f", *value);
	char hint[128];
	snprintf(hint, sizeof(hint), "%s, en dollars par million de jetons", label);

	if (!ask_line("TARIF", hint, text, sizeof(text))) return false;

	/* Virgule ou point : les deux se saisissent selon l'habitude. */
	for (char *p = text; *p; p++) if (*p == ',') *p = '.';

	char *end = NULL;
	double v = strtod(text, &end);
	if (end == text || v < 0.0) return false;   /* saisie inutilisable */
	*value = v;
	return true;
}

void menu_options(Game *game) {
	Options *o = &game->options;

	int h, w;
	getmaxyx(stdscr, h, w);
	int win_h = h - 2, win_w = w - 2;
	WINDOW *win = newwin(win_h, win_w, 1, 1);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	int sel = 0;
	char message[128] = "";
	nodelay(stdscr, FALSE);

	for (;;) {
		draw_options(win, win_h, win_w, o, sel, 0, message);
		message[0] = '\0';

		int ch = wgetch(win);
		if (ch == ERR) continue;
		if (ch == 27) break;

		if (ch == KEY_UP)   { sel = (sel + OPT_ROW_COUNT - 1) % OPT_ROW_COUNT; continue; }
		if (ch == KEY_DOWN) { sel = (sel + 1) % OPT_ROW_COUNT; continue; }

		bool secondary = (ch == 's' || ch == 'S');
		bool validate  = (ch == '\n' || ch == KEY_ENTER);
		if (!validate && !secondary) continue;

		if (sel < ACT_COUNT) {
			/* Capture d'une touche. La suivante pressee devient le raccourci,
			 * quelle qu'elle soit : c'est le seul moyen fiable de saisir une
			 * fleche ou une touche de fonction. */
			draw_options(win, win_h, win_w, o, sel, 1, NULL);
			int key = wgetch(win);
			if (key == ERR || key == 27) continue;

			int slot = secondary ? 1 : 0;
			if (key == KEY_DC || key == KEY_BACKSPACE || key == 127) {
				o->keys[sel][slot] = OPT_KEY_NONE;
				continue;
			}

			bool is_fn = (key >= KEY_MIN);
			int encoded = options_encode_key(key, is_fn);

			/* Une touche deja prise ailleurs rendrait l'une des deux actions
			 * inatteignable : on libere l'ancienne au lieu de creer un
			 * doublon silencieux. */
			for (int a = 0; a < ACT_COUNT; a++) {
				for (int k = 0; k < 2; k++) {
					if (a == sel && k == slot) continue;
					if (o->keys[a][k] == encoded) {
						o->keys[a][k] = OPT_KEY_NONE;
						char name[24];
						options_key_name(encoded, name, sizeof(name));
						snprintf(message, sizeof(message),
						         "%s etait deja sur \"%s\" : retiree de la.",
						         name, options_action_label(a));
					}
				}
			}
			o->keys[sel][slot] = encoded;
			continue;
		}

		if (sel == OPT_ROW_REASONING) {
			/* Trois crans, on tourne : pas besoin d'un sous-menu pour ca. */
			if (strcmp(o->reasoning, "low") == 0)       snprintf(o->reasoning, sizeof(o->reasoning), "high");
			else if (strcmp(o->reasoning, "high") == 0) snprintf(o->reasoning, sizeof(o->reasoning), "max");
			else                                        snprintf(o->reasoning, sizeof(o->reasoning), "low");
			continue;
		}
		if (sel == OPT_ROW_PRICE_IN)  { ask_price("Jetons envoyes", &o->price_in_per_m);  continue; }
		if (sel == OPT_ROW_PRICE_OUT) { ask_price("Jetons recus",  &o->price_out_per_m); continue; }
		if (sel == OPT_ROW_ZQSD)      { options_set_layout(o, "zqsd"); continue; }
		if (sel == OPT_ROW_WASD)      { options_set_layout(o, "wasd"); continue; }
		if (sel == OPT_ROW_DEFAULTS)  { options_defaults(o); continue; }

		if (sel == OPT_ROW_APIKEY) {
			/* Oublier la cle avant de la redemander : si le joueur renonce, il
			 * n'y en a plus, donc on previent clairement. */
			if (!confirm("CHANGER LA CLE D'API",
			             "La cle enregistree va etre effacee et redemandee.",
			             "Sans cle, les personnages ne peuvent plus repondre.",
			             "Oui, changer de cle", true))
				continue;

			delwin(win);
			apikey_forget();
			menu_ensure_api_key(game);
			clear();
			refresh();
			win = newwin(win_h, win_w, 1, 1);
			keypad(win, TRUE);
			nodelay(win, FALSE);
			nodelay(stdscr, FALSE);
			snprintf(message, sizeof(message), "Cle d'API mise a jour.");
			continue;
		}
	}

	wtimeout(stdscr, INPUT_TIMEOUT_MS);
	delwin(win);

	/* Les tarifs servent au compteur, et le fichier doit survivre a la
	 * fermeture : on applique et on enregistre en sortant. */
	deepseek_set_prices(o->price_in_per_m, o->price_out_per_m);
	deepseek_set_reasoning(o->reasoning);
	if (!options_save(o))
		snprintf(message, sizeof(message), "Reglages non enregistres.");

	clear();
	refresh();
	restore_game_screen(game);
}

/* ------------------------------------------------------------------ */
/* Fin de partie et abandon                                            */
/* ------------------------------------------------------------------ */

/* Ecran de resolution. Il sert dans les deux cas : le coupable a avoue
 * (`won`), ou le joueur demande la solution. Le texte affiche est celui qui a
 * ete genere au debut de la partie et fige dans la sauvegarde, jamais une
 * explication improvisee a la fin. */
static void draw_solution(Game *game, WINDOW *win, int h, int w, bool won) {
	const Story *s = game->story;
	SaveState *st = game->save;
	const StoryCharacter *cul = st->culprit_id ? story_character(s, st->culprit_id) : NULL;

	werase(win);
	wattron(win, COLOR_PAIR(won ? PAIR_GOOD : PAIR_DANGER));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(won ? PAIR_GOOD : PAIR_DANGER));

	int y = 2;
	wattron(win, COLOR_PAIR(won ? PAIR_GOOD : PAIR_DANGER) | A_BOLD);
	mvwprintw(win, y++, 3, won ? "AFFAIRE RESOLUE" : "ENQUETE ABANDONNEE");
	wattroff(win, COLOR_PAIR(won ? PAIR_GOOD : PAIR_DANGER) | A_BOLD);
	y++;

	if (cul) {
		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, y, 3, won ? "Il a avoue : " : "Le coupable etait : ");
		wattroff(win, COLOR_PAIR(9));
		wattron(win, COLOR_PAIR(PAIR_DANGER) | A_BOLD);
		wprintw(win, "%s", cul->name);
		wattroff(win, COLOR_PAIR(PAIR_DANGER) | A_BOLD);
		if (cul->role) {
			wattron(win, COLOR_PAIR(9));
			wprintw(win, ", %s", cul->role);
			wattroff(win, COLOR_PAIR(9));
		}
		y += 2;
	}

	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "CE QUI S'EST PASSE");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	wattron(win, COLOR_PAIR(PAIR_TEXT));
	y = wrap_print(win, y, 3, w - 6,
	               st->culprit_solution ? st->culprit_solution
	                                    : "La resolution n'a pas pu etre generee pour cette partie.");
	wattroff(win, COLOR_PAIR(PAIR_TEXT));
	y++;

	/* Ce que le joueur avait trouve tout seul : c'est la mesure de sa partie. */
	int found = culprit_evidence_count(st);
	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "ELEMENTS A CHARGE QUE VOUS AVIEZ ETABLIS (%d/%d)",
	          found, st->nb_culprit_facts);
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	for (int i = 0; i < st->nb_culprit_facts && y < h - 5; i++) {
		const Fact *f = story_fact(s, st->culprit_fact_ids[i]);
		if (!f || !f->text) continue;
		bool known = memory_player_knows_fact(st, st->culprit_fact_ids[i]);
		wattron(win, COLOR_PAIR(known ? PAIR_GOOD : 9));
		mvwprintw(win, y, 3, "%s", known ? "+" : "-");
		y = wrap_print(win, y, 5, w - 8, f->text);
		wattroff(win, COLOR_PAIR(known ? PAIR_GOOD : 9));
	}

	wattron(win, COLOR_PAIR(9));
	mvwprintw(win, h - 2, 3, "[R] recommencer cette enquete   -   [Entree] ou [ESC] quitter");
	wattroff(win, COLOR_PAIR(9));
	wrefresh(win);
}

/* Efface la partie en cours et en prepare une neuve : nouveau coupable tire
 * au sort, nouvelle resolution. L'histoire, elle, ne bouge pas. */
static void restart_story(Game *game) {
	SaveState *fresh = save_new(game->story, game->player.name);
	if (!fresh) return;

	/* Une replique encore en vol appartient a la partie qu'on abandonne : on
	 * la laisse tomber, sinon elle s'ecrirait dans la nouvelle. Comme pour le
	 * garde-fou, la requete n'est pas liberee, son fil la possede encore. */
	game->pending_req    = NULL;
	game->pending_npc    = -1;
	game->analysis_req   = NULL;
	game->analysis_npc   = -1;
	game->stream_started = 0;
	game->stream_printed = 0;
	game->interject_npc  = -1;
	game->interject_chain = 0;
	game->confession_npc = -1;

	save_free(game->save);
	game->save = fresh;

	/* L'histoire est rechargee depuis le disque : la partie qu'on abandonne a
	 * pu y verser ses complements de solubilite, qui appartenaient a SON
	 * coupable. Les garder ferait traîner des indices d'une enquete a l'autre.
	 * Tout ce qui pointe dans la Story est reconstruit juste apres. */
	char story_path[512];
	snprintf(story_path, sizeof(story_path), "%s", game->story->path);
	Story *reloaded = story_load(story_path, NULL, 0);
	if (reloaded) {
		story_free(game->story);
		game->story = reloaded;
	}
	/* Echec du rechargement : le fichier vient d'etre lu sans probleme, donc on
	 * n'arrive ici que s'il a change sur le disque entre-temps. On garde
	 * l'histoire en memoire, complements de la partie precedente compris, et on
	 * le dit dans le fil (une fois celui-ci recree, plus bas) : une enquete qui
	 * traine les indices de la precedente sans que personne ne le sache serait
	 * pire que le message. */
	bool stale_story = (reloaded == NULL);

	/* Le monde repart de la fiche de l'auteur, pas de l'etat en cours. */
	game->nb_npcs = npc_build_from_story(game->npcs, NPC_MAX, game->story);
	map_init(game, game->story);
	npc_place_all(game);

	chat_free(game);
	chat_init(game);
	game->discussion_mode = 0;

	if (stale_story)
		chat_addf(game, CHAT_SYSTEM, -1,
		          "Histoire non rechargee (%s) : cette enquete peut contenir des pieces "
		          "a conviction de la partie precedente.", story_path);

	int h, w;
	getmaxyx(stdscr, h, w);
	int win_h = h - 2, win_w = w - 2;
	WINDOW *win = newwin(win_h, win_w, 1, 1);
	werase(win);
	wattron(win, COLOR_PAIR(24));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(24));
	wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	mvwprintw(win, 2, 3, "NOUVELLE ENQUETE - %s", game->story->title);
	wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	wrefresh(win);

	/* draw_launch_progress la centre et la contracte si la fenetre est etroite. */
	int bar_w = 46;
	generate_solution(game, win, 4, bar_w);
	napms(600);
	delwin(win);

	game_autosave(game);
}

/* Renvoie true si la partie doit s'arreter. */
bool menu_show_solution(Game *game, bool won) {
	int h, w;
	getmaxyx(stdscr, h, w);
	int win_h = h - 2, win_w = w - 2;
	WINDOW *win = newwin(win_h, win_w, 1, 1);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	draw_solution(game, win, win_h, win_w, won);

	nodelay(stdscr, FALSE);
	bool quit = true;
	int ch, dead_reads = 0;
	for (;;) {
		ch = wgetch(win);
		if (ch == ERR) { if (++dead_reads > 100) break; continue; }
		dead_reads = 0;
		if (ch == 'r' || ch == 'R') { quit = false; break; }
		if (ch == '\n' || ch == KEY_ENTER || ch == 27) break;
	}
	wtimeout(stdscr, INPUT_TIMEOUT_MS);
	delwin(win);

	if (!quit) restart_story(game);

	clear();
	refresh();
	restore_game_screen(game);
	return quit;
}

/* ------------------------------------------------------------------ */
/* Pistes                                                              */
/* ------------------------------------------------------------------ */

/* La piste est calculee a partir de l'etat reel du moteur, sans appeler le
 * modele : elle ne peut donc pas envoyer le joueur vers quelque chose qui
 * n'existe pas — precisement le reproche fait aux personnages, qui renvoyaient
 * vers des journaux et des systemes que le jeu ne sait pas ouvrir.
 *
 * Elle oriente sans resoudre : elle ne nomme jamais le coupable. */
static void build_hint(Game *game, char *out, size_t out_size) {
	const Story *s = game->story;
	SaveState *st = game->save;

	/* 1. Assez d'elements a charge : c'est la fin, il ne manque que de les
	 *    opposer a la bonne personne. */
	if (culprit_is_cornered(st)) {
		snprintf(out, out_size,
		         "Vous en savez assez pour faire craquer quelqu'un.\n\n"
		         "Relisez les elements etablis dans votre carnet [F3] : pris ensemble, "
		         "ils designent une seule personne. Allez la trouver et opposez-lui ces "
		         "elements un par un, sans la laisser detourner la conversation.");
		return;
	}

	/* 2. Quelqu'un n'a jamais ete interroge : c'est le plus simple a corriger. */
	for (int i = 0; i < game->nb_npcs; i++) {
		const NPC *npc = &game->npcs[i];
		if (!npc->present || npc->met) continue;
		const char *room = map_room_name(&game->map, npc->room);
		snprintf(out, out_size,
		         "Vous n'avez encore parle a personne dans %s.\n\n"
		         "%s s'y trouve. Commencez par lui demander ou il etait, et ce qu'il a vu.",
		         room ? room : "l'une des pieces",
		         npc->name_known ? npc->def->name : "Quelqu'un");
		return;
	}

	/* 3. Une piece a conviction que quelqu'un detient et n'a pas encore sortie.
	 *    C'est le levier le plus productif : elle etablit plusieurs faits d'un
	 *    coup, et il suffit de la demander explicitement. */
	for (int c = 0; c < s->nb_clues; c++) {
		const Clue *cl = &s->clues[c];
		if (!cl->discoverable || !cl->id) continue;

		bool already = false;
		for (int k = 0; k < st->nb_discovered_clues; k++)
			if (strcmp(st->discovered_clue_ids[k], cl->id) == 0) already = true;
		if (already) continue;

		for (int i = 0; i < game->nb_npcs; i++) {
			const NPC *npc = &game->npcs[i];
			if (!npc->present) continue;

			bool holds = false;
			for (int r = 0; r < cl->nb_reveals; r++)
				for (int f = 0; f < npc->def->nb_known_facts; f++)
					if (strcmp(npc->def->known_fact_ids[f], cl->reveals_fact_ids[r]) == 0)
						holds = true;
			if (!holds) continue;

			const char *room = map_room_name(&game->map, npc->room);
			snprintf(out, out_size,
			         "Il existe une piece a conviction que vous n'avez pas obtenue :\n"
			         "  %s\n\n"
			         "%s y a acces, dans %s. Demandez-la lui explicitement, et insistez "
			         "s'il se retranche derriere la confidentialite.",
			         cl->name ? cl->name : cl->id,
			         npc->name_known ? npc->def->name : "Quelqu'un",
			         room ? room : "le batiment");
			return;
		}
	}

	/* 4. Un fait qu'un temoin peut encore donner. On nomme la personne, jamais
	 *    le fait : sinon la piste repondrait a la place du joueur. */
	for (int i = 0; i < game->nb_npcs; i++) {
		const NPC *npc = &game->npcs[i];
		if (!npc->present) continue;

		int left = 0;
		for (int f = 0; f < npc->def->nb_known_facts; f++)
			if (!memory_player_knows_fact(st, npc->def->known_fact_ids[f])) left++;
		if (left == 0) continue;

		const char *room = map_room_name(&game->map, npc->room);
		snprintf(out, out_size,
		         "%s sait encore %d chose%s que vous n'avez pas notee%s.\n\n"
		         "Il est dans %s. Posez des questions precises plutot que generales : "
		         "sur l'heure, sur ce qu'il a vu, sur ce qu'il reproche a la victime.",
		         npc->name_known ? npc->def->name : "Quelqu'un que vous avez deja vu",
		         left, left > 1 ? "s" : "", left > 1 ? "s" : "",
		         room ? room : "le batiment");
		return;
	}

	snprintf(out, out_size,
	         "Vous avez tire de chacun tout ce qu'il acceptait de dire.\n\n"
	         "Ce qui reste ne s'obtient qu'en mettant quelqu'un devant ses "
	         "contradictions. Reprenez votre carnet [F3] et cherchez qui est "
	         "contredit par les faits des autres.");
}

/* Fenetre d'information simple, refermee par n'importe quelle touche. */
static void show_message(Game *game, const char *title, const char *body) {
	int h, w;
	getmaxyx(stdscr, h, w);
	int win_w = w - 8 < 76 ? w - 8 : 76;
	int win_h = 14;
	WINDOW *win = newwin(win_h, win_w, (h - win_h) / 2, (w - win_w) / 2);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	werase(win);
	wattron(win, COLOR_PAIR(PAIR_WARN));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(PAIR_WARN));

	wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	mvwprintw(win, 1, 3, "%s", title);
	wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

	wattron(win, COLOR_PAIR(PAIR_TEXT));
	int y = 3;
	/* Le texte contient des sauts de ligne volontaires : on replie chaque
	 * paragraphe separement. */
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s", body);
	char *line = buf;
	while (line && y < win_h - 2) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		if (*line) y = wrap_print(win, y, 3, win_w - 6, line);
		else y++;
		line = nl ? nl + 1 : NULL;
	}
	wattroff(win, COLOR_PAIR(PAIR_TEXT));

	wattron(win, COLOR_PAIR(9));
	mvwprintw(win, win_h - 2, 3, "[Entree] fermer");
	wattroff(win, COLOR_PAIR(9));
	wrefresh(win);

	int ch, dead = 0;
	while ((ch = wgetch(win)) == ERR && ++dead < 100) { }
	delwin(win);
	(void)game;
}

/* ------------------------------------------------------------------ */
/* Menu de pause                                                       */
/* ------------------------------------------------------------------ */

/* Ouvert avec [Echap] en cours de partie. Les actions rares vivent ici plutot
 * que sur une touche chacune ; le carnet y figure aussi, tout en gardant son
 * raccourci direct, parce qu'on l'ouvre souvent. */
/* Retour d'un sous-menu vers le menu de pause. delwin ne nettoie pas l'ecran :
 * la fenetre du sous-menu (le carnet, les options, plus larges que ce menu)
 * restait visible autour de lui. On restaure le fond du jeu avant de rouvrir. */
static WINDOW *pause_window_reopen(Game *game, int win_h, int win_w) {
	int h, w;
	getmaxyx(stdscr, h, w);

	restore_game_screen(game);

	WINDOW *win = newwin(win_h, win_w, (h - win_h) / 2, (w - win_w) / 2);
	keypad(win, TRUE);
	nodelay(win, FALSE);
	return win;
}

bool menu_pause(Game *game) {
	static const char *entries[] = {
		"Reprendre l'enquete",
		"Carnet",
		"Demander une piste",
		"Options",
		"Solution",
		"Quitter",
	};
	static const char *hints[] = {
		"Retourner au jeu.",
		"L'affaire, vos objectifs, et une page par personne.",
		"Ou chercher maintenant, sans vous donner la reponse.",
		"Touches et tarifs, communs a toutes les parties.",
		"Renoncer et voir qui etait le coupable.",
		"La partie est enregistree avant de fermer.",
	};
	const int nb = (int)(sizeof(entries) / sizeof(entries[0]));

	int h, w;
	getmaxyx(stdscr, h, w);
	int win_w = w - 8 < 62 ? w - 8 : 62;
	int win_h = nb + 8;
	WINDOW *win = newwin(win_h, win_w, (h - win_h) / 2, (w - win_w) / 2);
	keypad(win, TRUE);
	nodelay(win, FALSE);

	int sel = 0;
	bool quit = false;

	for (;;) {
		werase(win);
		wattron(win, COLOR_PAIR(24));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(24));

		wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
		mvwprintw(win, 1, 3, "MENU");
		wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

		for (int i = 0; i < nb; i++) {
			if (i == sel) wattron(win, A_REVERSE);
			wattron(win, COLOR_PAIR(i == nb - 1 ? PAIR_DANGER : PAIR_TEXT));
			mvwprintw(win, 3 + i, 3, " %-*s ", win_w - 8, entries[i]);
			wattroff(win, COLOR_PAIR(i == nb - 1 ? PAIR_DANGER : PAIR_TEXT));
			if (i == sel) wattroff(win, A_REVERSE);
		}

		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, win_h - 3, 3, "%.*s", win_w - 6, hints[sel]);
		mvwprintw(win, win_h - 2, 3, "[Haut/Bas] choisir   [Entree] valider   [ESC] reprendre");
		wattroff(win, COLOR_PAIR(9));
		wrefresh(win);

		int ch = wgetch(win);
		if (ch == ERR) continue;
		if (ch == 27) break;
		if (ch == KEY_UP)   { sel = (sel + nb - 1) % nb; continue; }
		if (ch == KEY_DOWN) { sel = (sel + 1) % nb; continue; }
		if (ch != '\n' && ch != KEY_ENTER) continue;

		if (sel == 0) break;                       /* reprendre */

		if (sel == 1) {                            /* carnet */
			delwin(win);
			journal_show(game);
			win = pause_window_reopen(game, win_h, win_w);
			continue;
		}

		if (sel == 2) {                            /* piste */
			char hint[1024];
			build_hint(game, hint, sizeof(hint));
			delwin(win);
			show_message(game, "UNE PISTE", hint);
			/* Elle reste dans le fil : on peut la relire plus tard sans
			 * rouvrir le menu. */
			chat_addf(game, CHAT_SYSTEM, -1, "Piste : %s", hint);
			win = pause_window_reopen(game, win_h, win_w);
			continue;
		}

		if (sel == 3) {                            /* options */
			delwin(win);
			menu_options(game);
			win = pause_window_reopen(game, win_h, win_w);
			continue;
		}

		if (sel == 4) {                            /* solution : on renonce */
			/* La fenetre de confirmation est plus large que ce menu : refusee,
			 * elle laissait ses bords visibles de part et d'autre. */
			if (!confirm("ABANDONNER L'ENQUETE",
			             "Vous allez apprendre qui est le coupable.",
			             "L'enquete sera terminee.",
			             "Oui, montrer la solution", true)) {
				delwin(win);
				win = pause_window_reopen(game, win_h, win_w);
				continue;
			}
			delwin(win);
			quit = menu_show_solution(game, false);
			clear();
			refresh();
			restore_game_screen(game);
			return quit;
		}

		if (sel == 5) {                            /* quitter */
			if (!confirm("QUITTER", "La partie est enregistree avant de fermer.",
			             NULL, "Oui, quitter", false)) {
				delwin(win);
				win = pause_window_reopen(game, win_h, win_w);
				continue;
			}
			quit = true;
			break;
		}
	}

	delwin(win);
	clear();
	refresh();
	restore_game_screen(game);
	return quit;
}

void menu_show_briefing(Game *game) {
	const Story *s = game->story;

	int h, w;
	getmaxyx(stdscr, h, w);
	int win_w = w - 2;
	int win_h = h - 2;
	WINDOW *win = newwin(win_h, win_w, 1, 1);
	keypad(win, TRUE);
	/* lecture bloquante sur CETTE fenetre: sinon ncurses rend ESC seul
	 * au lieu d'assembler les sequences des touches flechees. */
	nodelay(win, FALSE);

	werase(win);
	wattron(win, COLOR_PAIR(24));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(24));

	int y = 1;
	wattron(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	mvwprintw(win, y++, 3, "%s", s->title);
	wattroff(win, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
	wattron(win, COLOR_PAIR(9));
	mvwprintw(win, y, 3, "%s, %d", s->location ? s->location : "", s->year);
	int gx = 3;
	for (int i = 0; i < s->nb_genre; i++) gx += (int)strlen(s->genre[i]) + 2;
	if (gx < win_w - 6) {
		gx = win_w - 4 - gx;
		for (int i = 0; i < s->nb_genre; i++) {
			mvwprintw(win, y, gx, "%s%s", s->genre[i], i + 1 < s->nb_genre ? ", " : "");
			gx += (int)strlen(s->genre[i]) + (i + 1 < s->nb_genre ? 2 : 0);
		}
	}
	y++;
	wattroff(win, COLOR_PAIR(9));
	y++;

	wattron(win, COLOR_PAIR(PAIR_TEXT));
	y = wrap_print(win, y, 3, win_w - 6, s->premise);
	wattroff(win, COLOR_PAIR(PAIR_TEXT));
	y++;

	if (s->victim_name) {
		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, y, 3, "Victime : ");
		wattroff(win, COLOR_PAIR(9));
		wattron(win, COLOR_PAIR(PAIR_DANGER));
		wprintw(win, "%s", s->victim_name);
		wattroff(win, COLOR_PAIR(PAIR_DANGER));
		if (s->victim_role) {
			wattron(win, COLOR_PAIR(9));
			wprintw(win, ", %s", s->victim_role);
			wattroff(win, COLOR_PAIR(9));
		}
		y++;
	}
	if (s->crime_type) {
		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, y++, 3, "Fait : %s dans %s, entre %s et %s",
		          s->crime_type, s->crime_location ? s->crime_location : "",
		          s->crime_time_start ? s->crime_time_start : "",
		          s->crime_time_end ? s->crime_time_end : "");
		wattroff(win, COLOR_PAIR(9));
	}
	y++;

	if (s->nb_central_questions > 0) {
		wattron(win, A_BOLD);
		mvwprintw(win, y++, 3, "CE QU'IL FAUT ETABLIR");
		wattroff(win, A_BOLD);
		wattron(win, COLOR_PAIR(PAIR_WARN));
		for (int i = 0; i < s->nb_central_questions && y < win_h - 8; i++)
			y = wrap_print(win, y, 5, win_w - 8, s->central_questions[i]);
		wattroff(win, COLOR_PAIR(PAIR_WARN));
		y++;
	}

	wattron(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);
	mvwprintw(win, y++, 3, "PERSONNES PRESENTES");
	wattroff(win, COLOR_PAIR(PAIR_HEADING) | A_BOLD);

	/* Nom et role sur une ligne, description repliee en dessous. Aucune
	 * troncature a la largeur d'une colonne : les roles ("Ethicienne,
	 * Responsable IA") et les descriptions sont des phrases entieres, et
	 * couper avec une precision printf compterait des OCTETS, ce qui rogne
	 * le texte accentue au mauvais endroit et peut casser un caractere. */
	/* On parcourt les personnages du jeu et non ceux de l'histoire : c'est le
	 * seul endroit qui sait ce que le joueur a deja appris (une partie reprise
	 * garde les noms connus). */
	for (int i = 0; i < game->nb_npcs; i++) {
		const NPC *npc = &game->npcs[i];
		const StoryCharacter *c = npc->def;
		int left = (win_h - 4) - y;

		/* Terminal trop court : plutot que de s'arreter sans rien dire, on
		 * annonce combien de personnes ne sont pas affichees. */
		if (left < 1 || (left < 2 && i + 1 < game->nb_npcs)) {
			int rest = game->nb_npcs - i;
			if (left >= 1 && rest > 0) {
				wattron(win, COLOR_PAIR(9));
				mvwprintw(win, y++, 5, "et %d autre%s (voir le carnet, [F3])",
				          rest, rest > 1 ? "s" : "");
				wattroff(win, COLOR_PAIR(9));
			}
			break;
		}

		/* Le nom et le metier sont sur la porte du bureau : le briefing les
		 * donne, comme n'importe quel dossier de depart. */
		wattron(win, COLOR_PAIR(npc_color(i)) | A_BOLD);
		mvwprintw(win, y, 5, "%s", c->name);
		wattroff(win, COLOR_PAIR(npc_color(i)) | A_BOLD);
		if (c->role && *c->role) {
			/* La largeur d'affichage n'est pas la taille en octets : on se cale
			 * sur le nombre de colonnes reellement occupees. */
			int used = 5 + text_display_cols(c->name);
			wattron(win, COLOR_PAIR(9));
			mvwprintw(win, y, used + 1, "- %s", c->role);
			wattroff(win, COLOR_PAIR(9));
		}
		y++;

		/* La description ne vient qu'avec le nom appris de sa bouche : le
		 * briefing ne la donne pas, sinon le joueur saurait tout de chacun avant
		 * meme d'entrer dans le batiment. */
		if (npc->name_known && c->public_description && *c->public_description && left > 1) {
			int max_lines = left - 1 < 2 ? left - 1 : 2;
			wattron(win, COLOR_PAIR(PAIR_TEXT));
			y = wrap_print_max(win, y, 7, win_w - 10, c->public_description, max_lines);
			wattroff(win, COLOR_PAIR(PAIR_TEXT));
		}
	}

	/* Deux lignes reservees : libelle centre, puis barre centree. */
	int status_y = win_h - 4;

	int bar_w = 46;
	generate_solution(game, win, status_y, bar_w);

	nodelay(stdscr, FALSE);
	int ch, dead_reads = 0;
	while ((ch = wgetch(win)) != '\n' && ch != KEY_ENTER && ch != 27) {
		if (ch == ERR && ++dead_reads > 100) break;
	}
	wtimeout(stdscr, INPUT_TIMEOUT_MS);

	delwin(win);
	clear();
	refresh();
}
