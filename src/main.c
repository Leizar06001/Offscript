#include "includes.h"
#include "diag.h"

/* Le modele rapide sert a tout ce qui est frequent : chaque replique, chaque
 * analyse memoire. Le modele de conception n'est appele qu'une fois par
 * partie, pour ecrire la resolution — c'est le seul texte qui doit tenir
 * debout d'un bout a l'autre de l'enquete. */
#define DEEPSEEK_MODEL_DEFAULT       "deepseek-v4-flash"
#define DEEPSEEK_MODEL_STORY_DEFAULT "deepseek-v4-pro"

atomic_int server_running = 1;

int init_struct(Game *game){
	/* Ces trois verrous etaient laisses a zero. Un pthread_mutex_t nul n'est
	 * pas un verrou valide sur macOS : lock echouait silencieusement et ne
	 * protegeait donc rien, alors que le fil de reponse ecrit en parallele de
	 * la boucle de jeu. */
	pthread_mutex_init(&game->chat.m_send_text, NULL);
	pthread_mutex_init(&game->chat.m_chat_box, NULL);
	pthread_mutex_init(&game->display.m_display_update, NULL);

	game->chat.ready_to_send = 0;
	game->chat.text_size = 0;
	game->chat.cursor = 0;
	game->chat.text_buffer[0] = '\0'; // Initialiser le tampon de texte
	chat_init(game);

	game->display.need_main_update = 0;
	game->display.input_cursor_y = 2;
	game->display.input_cursor_x = 2;

	game->print_error = 0;
	game->exit_error[0] = '\0'; // Initialiser le tampon d'erreur

	game->server_mode = 0;

	game->map.map = NULL;

	game->notif_enabled = 1;

	/* Les reglages du joueur (touches, tarifs) vivent hors des sauvegardes :
	 * ils sont charges avant tout le reste et valent pour toutes les parties.
	 * Sans fichier, ce sont les valeurs d'usine. */
	options_load(&game->options);
	deepseek_set_prices(game->options.price_in_per_m, game->options.price_out_per_m);
	deepseek_set_reasoning(game->options.reasoning);

    /* La position de depart vient de la carte de l'histoire (map_init) : elle
     * n'est pas connue tant qu'aucune enquete n'est choisie. */

    // Le joueur est le commissaire appele par Gael la veille de sa mort.
    snprintf(game->player.name, sizeof(game->player.name), "Detective");
    game->player.color = PAIR_WHITE;

    /* La cle d'API n'est pas dans le code : elle est demandee au premier
     * lancement puis conservee chiffree (voir apikey.h). */
    game->model_name = getenv("DEEPSEEK_MODEL") ? getenv("DEEPSEEK_MODEL")
                                                : DEEPSEEK_MODEL_DEFAULT;
    game->story_model_name = getenv("DEEPSEEK_MODEL_STORY") ? getenv("DEEPSEEK_MODEL_STORY")
                                                            : DEEPSEEK_MODEL_STORY_DEFAULT;

    game->pending_req  = NULL;
    game->pending_npc  = -1;
    game->analysis_req = NULL;
    game->analysis_npc = -1;
    game->talk_target  = -1;
    game->confession_npc = -1;
    game->interject_npc  = -1;
    game->talk_choice    = -1;
    game->talk_auto      = -1;

	return 0;
}

/* Ecrit la sauvegarde. save_write passe par un fichier temporaire puis
 * rename, donc une coupure ne peut pas laisser un fichier tronque.
 *
 * L'etat du monde (positions, visages, noms appris) est recopie juste avant :
 * il vit dans les structures de jeu, pas dans la sauvegarde. */
void game_autosave(Game *game){
	if (!game->save || !game->save_path[0]) return;
	npc_sync_to_save(game);
	chat_sync_to_save(game);

	/* Le compteur d'API vit dans le client (les fils de requete l'alimentent) :
	 * on en prend une photo au moment d'ecrire. */
	DeepseekUsage u;
	deepseek_usage_get(&u);
	game->save->api_prompt_tokens     = u.prompt_tokens;
	game->save->api_completion_tokens = u.completion_tokens;
	game->save->api_calls             = u.calls;

	if (!save_write(game->save, game->save_path))
		pinfo_c(game, PAIR_DANGER, "Echec de la sauvegarde (%s)\n", game->save_path);
	game->t_next_autosave = millis() + AUTOSAVE_PERIOD_MS;
}

/* La sauvegarde ne suivait que les echanges : se deplacer pendant dix minutes
 * puis fermer la fenetre ne laissait aucune trace. On repasse donc a
 * intervalle regulier, et systematiquement en quittant. */
static void autosave_tick(Game *game){
	if (!game->save || millis() < game->t_next_autosave) return;
	game_autosave(game);
}

void del_windows(Game *game){
	delwin(game->display.main_win);
	delwin(game->display.self_text);
	delwin(game->display.chat_box);
	delwin(game->display.info);
}

/* Les hauteurs ne sont plus fixes : la zone de saisie occupe trois lignes de
 * texte, et l'ensemble doit continuer a tenir dans un terminal court. On part
 * du bas (saisie et notes, dont la taille est imposee) et on donne le reste a
 * la carte. */
void create_windows(Game *game){
	int w,h;
	getmaxyx(stdscr, h, w);

	game->display.term_h = h;
	game->display.term_w = w;

	int main_y, main_x, main_h, main_w;
	int txt_y, txt_x, txt_h, txt_w;

	main_y = 3; main_x = 2;

	/* cadre haut + ligne du nom + CHAT_INPUT_LINES lignes + cadre bas */
	txt_h = CHAT_INPUT_LINES + 3;
	int info_h = 4;

	main_h = h - main_y - txt_h - info_h - 1;
	if (main_h < 12) { info_h = 3; main_h = h - main_y - txt_h - info_h - 1; }
	if (main_h < 8)  { main_h = 8; }

	main_w = min(70, (w - 5) / 2);

	game->display.chat_y = main_y;
	game->display.chat_x = main_w + main_x + 1;
	game->display.chat_h = main_h;
	game->display.chat_w = w - main_w - main_x - 1;

	txt_y = main_y + main_h;
	txt_x = main_x;
	txt_w = main_w + game->display.chat_w + 1;

	game->display.height 	= main_h;
	game->display.width 	= main_w;

    // Créer une fenêtre
    game->display.main_win 	= newwin(main_h, main_w, main_y, main_x);
	game->display.self_text = newwin(txt_h, txt_w, txt_y, txt_x); // Fenêtre pour les messages
	game->display.chat_box 	= newwin(game->display.chat_h, game->display.chat_w, game->display.chat_y, game->display.chat_x);
	game->display.chat 		= derwin(game->display.chat_box, game->display.chat_h - 2, game->display.chat_w - 2, 1, 1);
	game->display.info 		= newwin(info_h, main_w + game->display.chat_w, txt_y + txt_h, main_x); // Fenêtre pour les informations
	scrollok(game->display.info, TRUE);
	wattron(game->display.info, COLOR_PAIR(9));

	/* Le fil est redessine a partir de son contenu conserve : rien n'est
	 * perdu quand la fenetre change de taille. */
	game->chat.dirty = 1;
}

/* Un intitule pose dans le trait du cadre : chaque zone se nomme, au lieu de
 * laisser deviner ce qu'on regarde. `pair` suit l'etat du cadre. */
static void box_title(WINDOW *win, const char *title, int pair){
	wattron(win, COLOR_PAIR(pair) | A_BOLD);
	mvwprintw(win, 0, 3, " %s ", title);
	wattroff(win, COLOR_PAIR(pair) | A_BOLD);
}

/* Le cadre allume est celui sur lequel les touches agissent — la meme regle que
 * la zone de saisie, qui verdit quand ce qu'on tape part dans la question. Hors
 * mode discussion, les fleches deplacent le personnage : c'est le PLAN qui
 * repond. En mode discussion, elles bougent le curseur et le texte part dans
 * l'ENTRETIEN. On voit donc l'etat du clavier sans avoir a essayer une touche.
 * A rappeler a chaque changement de mode : le cadre n'est pas redessine par le
 * rendu de la carte. */
void draw_window_frames(Game *game){
	if (!game->display.main_win || !game->display.chat_box) return;

	int map_pair  = game->discussion_mode ? PAIR_BORDER : PAIR_GOOD;
	int chat_pair = game->discussion_mode ? PAIR_GOOD   : PAIR_BORDER;

	wattron(game->display.main_win, COLOR_PAIR(map_pair));
	box(game->display.main_win, 0, 0);
	wattroff(game->display.main_win, COLOR_PAIR(map_pair));
	box_title(game->display.main_win, "PLAN",
	          game->discussion_mode ? 9 : PAIR_GOOD);

	wattron(game->display.chat_box, COLOR_PAIR(chat_pair));
	box(game->display.chat_box, 0, 0);
	wattroff(game->display.chat_box, COLOR_PAIR(chat_pair));
	box_title(game->display.chat_box, "ENTRETIEN",
	          game->discussion_mode ? PAIR_GOOD : 9);

	wrefresh(game->display.main_win);
	wrefresh(game->display.chat_box);
	/* Le fil est une sous-fenetre du cadre : rafraichir le cadre seul le
	 * laisserait recouvert. */
	if (game->display.chat) {
		redrawwin(game->display.chat);
		wrefresh(game->display.chat);
	}
}

void draw_windows(Game *game){
	draw_window_frames(game);
	draw_input(game);

	refresh();                 // Rafraîchir l'écran principal
    wrefresh(game->display.main_win);              // Afficher la fenêtre
	wrefresh(game->display.self_text);             // Afficher la fenêtre de texte
	wrefresh(game->display.chat_box);
	wrefresh(game->display.chat);                 // Afficher la fenêtre de chat
	wrefresh(game->display.info);                 // Afficher la fenêtre d'informations
}

/* Les touches ressortent, leur libelle reste discret : on lit la barre d'un
 * coup d'oeil sans qu'elle attire l'oeil pendant le jeu. */
static void header_entry(const char *key, const char *label, int last){
	attron(COLOR_PAIR(PAIR_WARN));
	printw("%s", key);
	attroff(COLOR_PAIR(PAIR_WARN));
	attron(COLOR_PAIR(9));
	printw(" %s", label);
	if (!last) printw("  |  ");
	attroff(COLOR_PAIR(9));
}

/* Les touches etant reglables, la barre les lit dans les reglages : elle ne
 * peut plus annoncer un raccourci que le joueur a change. */
static void header_action(Game *game, Action a, const char *label, int last){
	char key[24], shown[32];
	options_key_name(game->options.keys[a][0], key, sizeof(key));
	snprintf(shown, sizeof(shown), "[%s]", key);
	header_entry(shown, label, last);
}

void print_header(Game *game){
	move(0, 0);
	clrtoeol();
	header_action(game, ACT_MENU,    "Menu",    0);
	header_action(game, ACT_PERSO,   "Perso",   0);
	header_action(game, ACT_JOURNAL, "Journal", 0);

	/* Le clavier ne fait pas la meme chose selon le mode : la barre dit
	 * lequel des deux est actif plutot que de laisser le joueur deviner
	 * pourquoi il ne bouge plus, ou pourquoi ses touches n'ecrivent rien. */
	if (game->discussion_mode) {
		header_entry("[Fleches]", "Curseur", 0);
		attron(COLOR_PAIR(PAIR_GOOD) | A_BOLD);
		printw("DISCUSSION");
		attroff(COLOR_PAIR(PAIR_GOOD) | A_BOLD);
		attron(COLOR_PAIR(9));
		printw(" - [Entree] envoyer, a vide sortir");
		attroff(COLOR_PAIR(9));
	} else {
		/* Deplacement : on montre les deux jeux de touches, puisque les
		 * fleches cohabitent avec ZQSD ou WASD. */
		char up[24], left[24], down[24], right[24], both[64];
		options_key_name(game->options.keys[ACT_UP][1],    up,    sizeof(up));
		options_key_name(game->options.keys[ACT_LEFT][1],  left,  sizeof(left));
		options_key_name(game->options.keys[ACT_DOWN][1],  down,  sizeof(down));
		options_key_name(game->options.keys[ACT_RIGHT][1], right, sizeof(right));
		if (up[0] != '-' && strlen(up) == 1 && strlen(left) == 1 &&
		    strlen(down) == 1 && strlen(right) == 1) {
			snprintf(both, sizeof(both), "[Fleches/%s%s%s%s]", up, left, down, right);
		} else {
			snprintf(both, sizeof(both), "[Fleches]");
		}
		header_entry(both, "Se deplacer", 0);
		header_action(game, ACT_TALK, "Parler", 1);
	}

	/* L'enregistrement contient toute la verite privee de l'enquete : il ne
	 * doit jamais pouvoir rester actif a l'insu du joueur. En mode alertes, la
	 * position aide aussi a reproduire une anomalie ; elle reste collee a
	 * gauche de l'indicateur du journal quand les deux modes sont actifs. */
	const char *indicator = diag_enabled() ? "DIAGNOSTIC ENREGISTRE" : NULL;
	char position[64];
	int h, w;
	getmaxyx(stdscr, h, w);
	(void)h;
	int right_edge = w - 2;

	if (indicator) {
		int x = right_edge - (int)strlen(indicator);
		if (x >= 0) {
			attron(COLOR_PAIR(PAIR_DANGER) | A_BOLD);
			mvprintw(0, x, "%s", indicator);
			attroff(COLOR_PAIR(PAIR_DANGER) | A_BOLD);
			right_edge = x - 2;
		}
	}
	if (game->options.diagnostic_ingame_logs) {
		snprintf(position, sizeof(position), "POS (%d, %d)",
		         game->player.x, game->player.y);
		int x = right_edge - (int)strlen(position);
		if (x >= 0) {
			attron(COLOR_PAIR(PAIR_WARN) | A_BOLD);
			mvprintw(0, x, "%s", position);
			attroff(COLOR_PAIR(PAIR_WARN) | A_BOLD);
		}
	}
	refresh();
}

/* Barre du bas : ce que la partie a consomme en appels au modele. Le prix est
 * une ESTIMATION calculee a partir d'une grille tarifaire ecrite en dur (voir
 * deepseek_client.c) : il donne un ordre de grandeur, pas une facture. */
void print_usage_bar(Game *game){
	int h, w;
	getmaxyx(stdscr, h, w);
	int y = h - 1;

	DeepseekUsage u;
	deepseek_usage_get(&u);

	move(y, 0);
	clrtoeol();

	int x = 2;
	attron(COLOR_PAIR(9));
	mvprintw(y, x, "API");
	x += 4;
	attroff(COLOR_PAIR(9));

	attron(COLOR_PAIR(PAIR_TEXT));
	mvprintw(y, x, "%ld appel%s", u.calls, u.calls > 1 ? "s" : "");
	x += 8 + (u.calls > 9 ? 1 : 0) + (u.calls > 99 ? 1 : 0);
	attroff(COLOR_PAIR(PAIR_TEXT));

	attron(COLOR_PAIR(9));
	mvprintw(y, x, "   entree ");
	attroff(COLOR_PAIR(9));
	attron(COLOR_PAIR(PAIR_TEXT));
	printw("%.1fk", (double)u.prompt_tokens / 1000.0);
	attroff(COLOR_PAIR(PAIR_TEXT));

	attron(COLOR_PAIR(9));
	printw("   sortie ");
	attroff(COLOR_PAIR(9));
	attron(COLOR_PAIR(PAIR_TEXT));
	printw("%.1fk", (double)u.completion_tokens / 1000.0);
	attroff(COLOR_PAIR(PAIR_TEXT));

	/* Part de l'entree servie par le cache du fournisseur : c'est ce qui dit si
	 * l'ordre du prompt tient (partie stable en tete). Rien n'est affiche quand
	 * l'API ne rapporte pas ces compteurs — mieux vaut le silence qu'un 0%
	 * trompeur. Compte de la session, pas de la partie. */
	long cache_total = u.cache_hit_tokens + u.cache_miss_tokens;
	if (cache_total > 0) {
		attron(COLOR_PAIR(9));
		printw("   cache ");
		attroff(COLOR_PAIR(9));
		attron(COLOR_PAIR(PAIR_GOOD));
		printw("%.0f%%", 100.0 * (double)u.cache_hit_tokens / (double)cache_total);
		attroff(COLOR_PAIR(PAIR_GOOD));
	}

	attron(COLOR_PAIR(9));
	printw("   ~");
	attroff(COLOR_PAIR(9));
	attron(COLOR_PAIR(PAIR_WARN));
	printw("%.3f $", deepseek_usage_cost(&u));
	attroff(COLOR_PAIR(PAIR_WARN));

	/* Le modele employe, discretement : utile quand on en change par
	 * variable d'environnement et qu'on veut verifier lequel tourne. */
	int right = w - (int)strlen(game->model_name) - 3;
	if (right > x + 30) {
		attron(COLOR_PAIR(9));
		mvprintw(y, right, "%s", game->model_name);
		attroff(COLOR_PAIR(9));
	}
	refresh();
}

void check_terminal_resize(Game *game){
	int w,h;
	getmaxyx(stdscr, h, w);

	if (w != game->display.term_w || h != game->display.term_h){
		pthread_mutex_lock(&game->display.m_display_update);
		pthread_mutex_lock(&game->chat.m_chat_box);

		del_windows(game);
		clear();
		create_windows(game);
		draw_windows(game);
		print_header(game);
		refresh();

		pthread_mutex_unlock(&game->chat.m_chat_box);
		pthread_mutex_unlock(&game->display.m_display_update);
	}
}

int game_loop(Game *game) {
	if (!game) return -1; // Vérification de la validité du pointeur

	/* Premier affichage : personne n'est encore designe, et l'affichage ne
	 * designe plus lui-meme. */
	npc_refresh_target(game);

	pthread_mutex_lock(&game->display.m_display_update);
	update_display(game);
	print_header(game);
	pthread_mutex_unlock(&game->display.m_display_update);

	// Boucle de jeu principale
	int ret = 0;
	int talk = 0;
	while (server_running) {
		
		check_terminal_resize(game);

		ret = get_user_input(game); // Obtenir l'entrée de l'utilisateur

		switch (ret){
			case IN_KEY_EXIT:
				server_running = 0;
				break;

			case IN_KEY_MOVE:
				ask_for_display_update(game);
				if (game->options.diagnostic_ingame_logs) print_header(game);
				break;

			case IN_KEY_TEXT:
				/* La zone entiere est redessinee : un caractere accentue
				 * occupe plusieurs octets et la saisie tient sur trois
				 * lignes, donc reafficher "le dernier octet" ne marche pas. */
				draw_input(game);
				break;

			case IN_KEY_SEND:
				draw_input(game);

				// Adresser le texte au PNJ present dans la piece
				pthread_mutex_lock(&game->chat.m_send_text);
				if (game->chat.ready_to_send){
					game->chat.ready_to_send = 0;
					talk = npc_talk_send(game, game->chat.text_to_send);
				}
				pthread_mutex_unlock(&game->chat.m_send_text);

				/* La question est ecrite dans le fil par npc_talk_send, juste
				 * avant la ligne d'attente du personnage, pour que les deux
				 * arrivent dans l'ordre. */
				if (talk != 0 && talk != NPC_TALK_SENT){
					/* Refusee : on rend la question au joueur au lieu de la
					 * perdre, il n'a plus qu'a re-appuyer sur Entree. */
					snprintf(game->chat.text_buffer, sizeof(game->chat.text_buffer),
					         "%s", game->chat.text_to_send);
					game->chat.text_size = strlen(game->chat.text_buffer);
					game->chat.cursor    = game->chat.text_size;

					if (talk == NPC_TALK_NOBODY)
						pinfo_c(game, PAIR_WARN, "Personne ici pour vous entendre.\n");
					else if (talk == NPC_TALK_BUSY)
						pinfo_c(game, PAIR_WARN, "Laissez-le finir de repondre.\n");
					else if (!diag_enabled())
						pinfo_c(game, PAIR_DANGER, "Impossible de lancer la requete de dialogue.\n");
					draw_input(game);
				}
				talk = 0;
				break;

			case IN_KEY_COLOR:
			case IN_KEY_DOOR:
				ask_for_display_update(game);
				break;

			case IN_KEY_MENU:
				/* Options, carnet, solution et sortie sont regroupes ici :
				 * ce sont des actions rares, elles n'ont pas a occuper une
				 * touche chacune. */
				if (menu_pause(game)) server_running = 0;
				else print_header(game);
				break;

			case IN_KEY_MODE:
				/* Les trois cadres changent d'aspect avec le mode (le PLAN,
				 * l'ENTRETIEN et la saisie) : ils sont redessines en meme temps
				 * que la barre du haut. */
				pthread_mutex_lock(&game->display.m_display_update);
				print_header(game);
				draw_window_frames(game);
				pthread_mutex_unlock(&game->display.m_display_update);
				print_talk_hint(game);
				draw_input(game);
				move_cursor_back(game);
				break;

			case IN_KEY_NOTIF:
				pthread_mutex_lock(&game->display.m_display_update);
				print_header(game);
				pthread_mutex_unlock(&game->display.m_display_update);
				move_cursor_back(game);
				break;
		}

		// Hors du switch: aucune touche n'arrive pendant que le modele repond
		npc_talk_update(game);
		npc_movement_update(game);
		autosave_tick(game);

		/* L'aveu est traite ici, hors de npc_talk_update : la replique doit
		 * d'abord s'afficher en entier dans le fil, sinon l'ecran de
		 * resolution recouvrirait les mots memes de l'aveu. */
		if (game->confession_npc >= 0 && !game->pending_req) {
			game->confession_npc = -1;

			/* C'est au joueur de tourner la page : la resolution recouvrait le
			 * fil une seconde et demie apres la derniere phrase de l'aveu, donc
			 * la partie s'arretait avant qu'on ait fini de le lire. */
			chat_add(game, CHAT_SYSTEM, -1,
			         "L'enquete est terminee. [Entree] pour la resolution.");
			chat_render(game);
			move_cursor_back(game);

			nodelay(stdscr, FALSE);
			int ch, dead_reads = 0;
			while ((ch = wgetch(stdscr)) != '\n' && ch != KEY_ENTER) {
				/* Entree fermee (stdin redirige) : wgetch rend ERR en boucle et
				 * personne ne viendra appuyer sur une touche. */
				if (ch == ERR && ++dead_reads > 100) break;

				/* On attend pour laisser LIRE : il faut donc pouvoir remonter
				 * dans le fil, un aveu et les reactions qui l'entourent tiennent
				 * rarement dans la hauteur visible. */
				if (ch == KEY_PPAGE) chat_scroll(game, 5);
				else if (ch == KEY_NPAGE) chat_scroll(game, -5);
				else if (ch == KEY_MOUSE) {
					MEVENT ev;
					if (getmouse(&ev) == OK) {
						if (ev.bstate & BUTTON4_PRESSED)      chat_scroll(game, 3);
						else if (ev.bstate & OFFSCRIPT_BUTTON5_PRESSED) chat_scroll(game, -3);
					}
				}
				chat_render(game);
			}
			wtimeout(stdscr, INPUT_TIMEOUT_MS);

			if (menu_show_solution(game, true)) server_running = 0;
			else print_header(game);
		}

		chat_tick(game);   // Fait avancer les points d'attente
		chat_render(game); // Redessine le fil s'il a change

		pthread_mutex_lock(&game->display.m_display_update);
		if (game->display.need_main_update || millis() > game->display.t_next_update) {
			update_display(game); // Mettre à jour l'affichage principal
			print_usage_bar(game);
			move_cursor_back(game);

			game->display.t_next_update = millis() + 500;
			game->display.need_main_update = 0; // Réinitialiser le besoin de mise à jour
		}
		pthread_mutex_unlock(&game->display.m_display_update);

		// mvprintw(1, 0, "Position du joueur: (%d, %d) - C: %d", game->player.x, game->player.y, game->player.color);
		usleep(1000); // get_user_input attend deja jusqu'a INPUT_TIMEOUT_MS
	}

	return 0;
}

int start_game(Game *game){
    setlocale(LC_ALL, "");  // avant initscr: sinon ncurses ignore l'UTF-8
    /* ...mais PAS pour les nombres. En locale francaise, printf("%f") ecrit
     * "0,42" et strtod s'arrete au point : les fichiers JSON ecrits par le jeu
     * devenaient illisibles, et ceux des histoires auraient ete mal lus. Le
     * jeu n'a besoin de la locale que pour l'UTF-8. */
    setlocale(LC_NUMERIC, "C");
    initscr();              // Démarrer ncurses
    cbreak();               // Lecture caractère par caractère
    noecho();               // Ne pas afficher les touches
    keypad(stdscr, TRUE);   // Activer les touches spéciales
    /* Sans delai explicite, ncurses rend un ESC nu au lieu d'assembler les
     * sequences des touches flechees et de fonction: les fleches passaient
     * alors pour [ESC] et fermaient le jeu. */
    set_escdelay(100);
    /* Molette : sans cela le terminal la traduit en fleches et le joueur se
     * deplacait en voulant relire le fil. Les evenements arrivent maintenant
     * comme KEY_MOUSE et vont au fil (voir inputs.c).
     * Le masque ne retient que la molette, mais ncurses doit quand meme
     * demander au terminal de rapporter TOUS les boutons : le prix a payer est
     * que selectionner du texte a la souris demande desormais Maj+glisser. */
	    mousemask(OFFSCRIPT_MOUSE_WHEEL_MASK, NULL);
    mouseinterval(0);       // pas d'attente d'un double-clic qu'on n'ecoute pas
	start_color();          // Activer les couleurs
	wtimeout(stdscr, INPUT_TIMEOUT_MS);

    init_pair(1, COLOR_CYAN, COLOR_BLACK); // Définir une paire de couleurs
	init_pair(2, COLOR_RED, COLOR_BLACK);  // Définir une autre paire de couleurs
	init_pair(3, COLOR_GREEN, COLOR_BLACK); // Définir une paire de couleurs pour le joueur
	init_pair(4, COLOR_YELLOW, COLOR_BLACK); // Définir une paire de couleurs pour le score
	init_pair(5, COLOR_MAGENTA, COLOR_BLACK); // Définir une paire de couleurs pour le niveau
	init_pair(6, COLOR_BLUE, COLOR_BLACK); // Définir une paire de couleurs pour les vies
	init_pair(7, COLOR_WHITE, COLOR_BLACK); // Définir une paire de couleurs pour les messages

	init_color(8, 600, 200, 200);	// dark red
	init_color(9, 70, 70, 70);		// dark gray
	init_pair(8, 8, 9);				// Map

	init_color(20, 700, 700, 700);		// gris clair : texte secondaire lisible
	init_pair(9, 20, COLOR_BLACK);		// dark text

	/* Une couleur par personnage : c'est elle qui porte TOUTE sa replique dans
	 * le fil (voir chat_render), pas seulement son nom. Les teintes sourdes
	 * d'origine passaient sur la carte mais rendaient les dialogues penibles a
	 * lire sur fond noir : chaque teinte garde son identite, montee au niveau
	 * de luminosite d'un texte confortable. */
	short pale_colors[10] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    init_color(10, 500, 650, 1000);  // steel blue
    init_color(11, 900, 850, 400);   // mustard
    init_color(12, 700, 450, 1000);  // violet
    init_color(13, 450, 900, 900);   // aqua
    init_color(14, 950, 760, 450);   // clay amber
    init_color(15, 800, 850, 1000);  // lilac pale
    init_color(16, 1000, 620, 750);  // rose
    init_color(17, 760, 950, 550);   // lime
    init_color(18, 1000, 620, 400);  // brick orange
    init_color(19, 520, 950, 520);   // green
	for (short i = 0; i < 10; i++) {
        init_pair(pale_colors[i], pale_colors[i], COLOR_BLACK);
    }

	init_pair(20, 17, 9);			// Opened door
	init_pair(21, COLOR_RED, 9);	// Closed door

	init_color(22, 300, 100, 100);	// darker red, wall shades
	init_pair(22, 22, 9);	// dark walls

	init_color(23, 90*1000/255, 63*1000/255, 40*1000/255);  // Dark brown
	init_pair(23, 23, 9);	// mobilier

	init_color(24, 550, 550, 550);		// gris moyen : bordures
	init_pair(24, 24, COLOR_BLACK);		// dark light borders

	init_color(25, 800, 0, 0);
	init_pair(25, 25, 9); // Remplace le rouge qui marche plus

	/* Couleurs semantiques : une intention par paire, pour que le sens d'une
	 * couleur soit le meme dans le menu, le briefing, le carnet et le chat. */
	init_color(26, 950, 800, 400);	init_pair(PAIR_TITLE,   26, COLOR_BLACK); // titres
	init_color(27, 450, 800, 900);	init_pair(PAIR_HEADING, 27, COLOR_BLACK); // intertitres
	init_color(28, 400, 850, 450);	init_pair(PAIR_GOOD,    28, COLOR_BLACK); // acquis, favorable
	init_color(29, 950, 700, 300);	init_pair(PAIR_WARN,    29, COLOR_BLACK); // a surveiller
	init_color(30, 950, 350, 350);	init_pair(PAIR_DANGER,  30, COLOR_BLACK); // hostile, aveu
	init_color(31, 880, 880, 920);	init_pair(PAIR_TEXT,    31, COLOR_BLACK); // corps de texte



	/* Le menu et le briefing dessinent leurs propres fenetres: ils doivent
	 * venir apres les paires de couleurs, mais avant la mise en place de
	 * l'interface de jeu. */
	if (menu_ensure_api_key(game) != 0) goto exit_first;
	/* Choix de l'enquete, puis de la partie. On boucle pour que [ESC] depuis
	 * la liste des parties ramene aux enquetes au lieu de fermer le jeu. */
	for (;;) {
		if (menu_choose_story(game) != 0) goto exit_first;

		int r = menu_choose_save(game);
		if (r == 0) break;
		if (r != MENU_BACK) goto exit_first;

		story_free(game->story);
		game->story  = NULL;
		game->nb_npcs = 0;
	}

	/* La carte appartient a l'histoire : elle ne peut etre construite qu'une
	 * fois l'enquete choisie, et les PNJ ne peuvent etre places qu'ensuite,
	 * puisqu'ils sont poses par piece. */
	if (map_init(game, game->story) != 0) {
		snprintf(game->exit_error, sizeof(game->exit_error),
		         "carte inutilisable dans %s", game->story->path);
		game->print_error = 1;
		goto exit_first;
	}
	npc_place_all(game);
	npc_restore_from_save(game);
	chat_restore_from_save(game);
	diag_game_state(game, "session_started");

	menu_show_briefing(game);

    create_windows(game);
	pthread_mutex_lock(&game->display.m_display_update);
	draw_windows(game);
	pthread_mutex_unlock(&game->display.m_display_update);


    game_loop(game);

    /* Quitter ne doit rien couter : la derniere position et les derniers
     * echanges partent sur le disque avant de rendre le terminal. */
    game_autosave(game);
	diag_game_state(game, "session_ended");

    delwin(game->display.main_win);                // Supprimer la fenêtre

exit_first:
    clear();             // Clear all ncurses windows
	refresh();           // Force draw to terminal
	endwin();            // Restore terminal state
	fflush(stdout);

	// Libérer les ressources
	chat_free(game); // Libérer le fil de discussion
	/* Une requete detachee peut encore lire l'etat: on ne libere l'histoire
	 * et la sauvegarde que si rien n'est en vol. Le processus se termine de
	 * toute facon juste apres. */
	if (!game->pending_req && !game->analysis_req) {
		save_free(game->save);
		story_free(game->story);
		game->save = NULL;
		game->story = NULL;
	}
	map_free(&game->map);

	if (game->print_error) {
		printf("\nError: %s\n", game->exit_error);
		return 1; // Quitter avec une erreur
	}

	return 0;
}

int main(void) {
    Game game = {0};
    init_struct(&game);
	if (game.options.diagnostic_file_logs && diag_init(true) != 0) {
		fprintf(stderr, "Impossible de creer le journal de diagnostic dans diagnostics/.\n");
		game.options.diagnostic_file_logs = false;
		options_save(&game.options);
	}

    int ret = start_game(&game);
	char diagnostic_path[512] = "";
	if (diag_enabled()) snprintf(diagnostic_path, sizeof(diagnostic_path), "%s", diag_path());
	diag_close();
	if (diagnostic_path[0])
		printf("Diagnostic enregistre dans %s\n", diagnostic_path);

    if (ret != 0){
        printf("Erreur lors du lancement du jeu : %s\n", game.exit_error);
        return ret;
    }

    return EXIT_SUCCESS;
}
