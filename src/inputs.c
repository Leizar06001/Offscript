#include "includes.h"

#include <limits.h>
#include <wchar.h>

#define KEY_F1 	265	// F1 key for changing color
#define KEY_F2 	266	// F2
#define KEY_F3 	267	// F3
#define KEY_F4	268
#define KEY_F5	269
#define KEY_F6	270
#define KEY_QUIT 		27 	// ESC key

/* Les regles de deplacement vivent desormais dans map.c : joueur et PNJ
 * doivent avancer exactement pareil, sinon un PNJ pourrait rejoindre une case
 * ou le joueur ne peut pas aller (ou l'inverse). */
static int try_move(Game *game, int dx, int dy){
	int nx = game->player.x + dx;
	int ny = game->player.y + dy;

	if (!map_can_step(&game->map, game->player.x, game->player.y, dx, dy)) return 0;

	/* Deux personnages sur la meme case ne laissent voir que le dernier
	 * dessine : on ne traverse personne. */
	for (int i = 0; i < game->nb_npcs; i++){
		if (!game->npcs[i].present) continue;
		if (game->npcs[i].x == nx && game->npcs[i].y == ny) return 0;
	}

	game->player.x = nx;
	game->player.y = ny;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Edition de la question                                              */
/* ------------------------------------------------------------------ */

/* Debut du caractere qui precede l'octet i. On remonte par-dessus les octets
 * de continuation (10xxxxxx) : reculer d'un seul octet couperait un « é » en
 * deux et laisserait une chaine UTF-8 invalide. */
static size_t utf8_prev(const char *s, size_t i){
	if (i == 0) return 0;
	i--;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
	return i;
}

static size_t utf8_next(const char *s, size_t i){
	if (!s[i]) return i;
	i++;
	while (s[i] && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
	return i;
}

/* Insere au point d'insertion, qui n'est pas forcement la fin. */
static void input_insert(Game *game, const char *bytes, size_t n){
	Chat *c = &game->chat;
	if (c->text_size + n >= sizeof(c->text_buffer) - 1) return;

	memmove(c->text_buffer + c->cursor + n, c->text_buffer + c->cursor,
	        c->text_size - c->cursor + 1);
	memcpy(c->text_buffer + c->cursor, bytes, n);
	c->text_size += n;
	c->cursor    += n;
	c->text_buffer[c->text_size] = '\0';
}

static void input_backspace(Game *game){
	Chat *c = &game->chat;
	if (c->cursor == 0) return;

	size_t start = utf8_prev(c->text_buffer, c->cursor);
	size_t n = c->cursor - start;
	memmove(c->text_buffer + start, c->text_buffer + c->cursor,
	        c->text_size - c->cursor + 1);
	c->text_size -= n;
	c->cursor     = start;
}

static void input_delete(Game *game){
	Chat *c = &game->chat;
	if (c->cursor >= c->text_size) return;

	size_t end = utf8_next(c->text_buffer, c->cursor);
	size_t n = end - c->cursor;
	memmove(c->text_buffer + c->cursor, c->text_buffer + end,
	        c->text_size - end + 1);
	c->text_size -= n;
}

static void input_clear(Game *game){
	game->chat.text_buffer[0] = '\0';
	game->chat.text_size = 0;
	game->chat.cursor = 0;
}

int menu_character(Game *game){
	// Report mouse events
	mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
	mouseinterval(0);

	// Crée une fenêtre
    int height = 18, width = 30, starty = 4, startx = 3;
	const int valid_x = 3;
	const int cancel_x = valid_x + 10;
	const int btns_y = height - 2;
    WINDOW *win = newwin(height, width, starty, startx);
    box(win, 0, 0);
    mvwprintw(win, 1, 7, "Menu personnage");
	mvwprintw(win, btns_y, valid_x, "Valider");
	mvwprintw(win, btns_y, cancel_x, "Annuler");

	const int icons_per_row = (width - 4) / 2;
	int y = 0, x = 0;
	// Draw faces
	const int f_start_y = 3, f_start_x = 2;
	const int nb_faces_rows = (NB_FACES + icons_per_row - 1) / icons_per_row;
	for(int i = 0; i < NB_FACES; i++){
		mvwaddwstr(win, f_start_y + y, f_start_x + x * 2, faces[i]);
		x++;
		if (x >= icons_per_row){
			x = 0; y++;
		}
	}

	// Draw bodies
	y = 0, x = 0;
	const int b_start_y = f_start_y + nb_faces_rows + 1, b_start_x = 2;
	const int nb_bodies_rows = (NB_BODYS + icons_per_row - 1) / icons_per_row;
	for(int i = 0; i < NB_BODYS; i++){
		mvwaddwstr(win, b_start_y + y, b_start_x + x * 2, bodies[i]);
		x++;
		if (x >= icons_per_row){
			x = 0; y++;
		}
	}

	// Draw legs
	y = 0, x = 0;
	const int l_start_y = b_start_y + nb_bodies_rows + 1, l_start_x = 2;
	const int nb_legs_rows = (NB_LEGS + icons_per_row - 1) / icons_per_row;
	for(int i = 0; i < NB_LEGS; i++){
		mvwaddwstr(win, l_start_y + y, l_start_x + x * 2, legs[i]);
		x++;
		if (x >= icons_per_row){
			x = 0; y++;
		}
	}

	int choosen_face = game->player.face_id;
	int choosen_body = game->player.body_id;
	int choosen_legs = game->player.legs_id;

	mvwaddwstr(win, height - 4, width - 4, faces[choosen_face]);
	mvwaddwstr(win, height - 3, width - 4, bodies[choosen_body]);
	mvwaddwstr(win, height - 2, width - 4, legs[choosen_legs]);

    wrefresh(win);

	int ret = 0;

    MEVENT event;
    int ch;
    while ((ch = wgetch(stdscr)) != KEY_F2) {
        if (ch == KEY_MOUSE) {
            if (getmouse(&event) == OK) {
                // Vérifie si le clic est dans la fenêtre
                if (wenclose(win, event.y, event.x)) {
                    int local_y = event.y - starty;
                    int local_x = event.x - startx;

					// Boutons
					if (local_y == btns_y){
						if (local_x >= valid_x && local_x < valid_x + 7){
							game->player.face_id = choosen_face;
							game->player.body_id = choosen_body;
							game->player.legs_id = choosen_legs;
							// write_config_int("face", choosen_face);
							// write_config_int("body", choosen_body);
							// write_config_int("legs", choosen_legs);
							ret = 1;
							break;
						} else if (local_x >= cancel_x && local_x < cancel_x + 7){
							break;
						}
					}

					// Faces
					if (local_y >= f_start_y && local_y < f_start_y + nb_faces_rows){
						if (local_x >= f_start_x && local_x <= f_start_x + icons_per_row * 2){
							int face_row = local_y - f_start_y;
							int face_row_ind = (local_x - f_start_x) / 2;
							int new_face = face_row * icons_per_row + face_row_ind;
							if (new_face >= 0 && new_face < NB_FACES) choosen_face = new_face;
						}
					}
					// Bodies
					if (local_y >= b_start_y && local_y < b_start_y + nb_bodies_rows){
						if (local_x >= b_start_x && local_x <= b_start_x + icons_per_row * 2){
							int body_row = local_y - b_start_y;
							int body_row_ind = (local_x - b_start_x) / 2;
							int new_body = body_row * icons_per_row + body_row_ind;
							if (new_body >= 0 && new_body < NB_BODYS) choosen_body = new_body;
						}
					}
					// Legs
					if (local_y >= l_start_y && local_y < l_start_y + nb_legs_rows){
						if (local_x >= l_start_x && local_x <= l_start_x + icons_per_row * 2){
							int leg_row = local_y - l_start_y;
							int leg_row_ind = (local_x - l_start_x) / 2;
							int new_legs = leg_row * icons_per_row + leg_row_ind;
							if (new_legs >= 0 && new_legs < NB_LEGS) choosen_legs = new_legs;
						}
					}

					mvwaddwstr(win, height - 4, width - 4, faces[choosen_face]);
					mvwaddwstr(win, height - 3, width - 4, bodies[choosen_body]);
					mvwaddwstr(win, height - 2, width - 4, legs[choosen_legs]);

                    wrefresh(win);
                }
            }
        }
		usleep(10000);
    }

    delwin(win);
	mousemask(0, NULL);
	restore_game_screen(game);

	return ret;
}

// void show_chat_menu(Game *game){
// 	mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
// 	mouseinterval(0);

// 	// Crée une fenêtre
//     int height = game->display.chat_h - 2;
// 	int width = game->display.chat_w - 2;
// 	int starty = game->display.chat_y + 1;
// 	int startx = game->display.chat_x + 1;

// 	const int cancel_x = width - 10;
// 	const int btns_y = height - 2;
//     WINDOW *win = newwin(height, width, starty, startx);
//     box(win, 0, 0);
//     mvwprintw(win, 1, 4, "Utilisateurs");
// 	mvwprintw(win, btns_y, cancel_x, "Fermer");
// 	wrefresh(win);

// 	int offset = 0;
//     int max_visible = list_height - 2;

// 	MEVENT event;
// 	int ch;
//     while ((ch = wgetch(stdscr)) != KEY_F3) {
// 		if (ch == KEY_MOUSE) {
//             if (getmouse(&event) == OK) {
// 				// Vérifie si le clic est dans la fenêtre
//                 if (wenclose(win, event.y, event.x)) {
// 					if (event.bstate & BUTTON4_PRESSED) {
// 						ch = KEY_UP;   // Scroll up
// 					} else if (event.bstate & BUTTON5_PRESSED) {
// 						ch = KEY_DOWN; // Scroll down
// 					} else if (event.bstate & BUTTON1_PRESSED) {
// 						// Left-click detected
// 						int local_y = event.y - starty;
// 						int local_x = event.x - startx;
	
// 						if (local_y == btns_y && (local_x > cancel_x)){
// 							break;
// 						}
// 					} else if (event.bstate & BUTTON3_PRESSED) {
// 						// Right-click detected
// 					}
// 				}
// 			}
// 		}

// 		switch (ch) {
//             case KEY_UP:
//                 if (offset > 0) {
//                     offset--;
//                     draw_user_list(offset);
//                 }
//                 break;
//             case KEY_DOWN:
//                 if (offset + max_visible < user_count) {
//                     offset++;
//                     draw_user_list(offset);
//                 }
//                 break;
//         }
		
// 		usleep(10000);
// 	}

// 	mousemask(0, NULL);
// 	delwin(win);
// 	werase(game->display.chat);   // Clear its contents
// 	wrefresh(game->display.chat);
// 	refresh();
// }

int get_user_input(Game *game) {
	if (!game) return -1; // Vérification de la validité du pointeur
	/* get_wch et non getch : getch ne rend que des octets, donc un « é »
	 * (2 octets en UTF-8) arrivait en morceaux inutilisables. Ici on recoit
	 * le caractere complet. */
	wint_t wch = 0;
	int rc = get_wch(&wch);
	if (rc == ERR) {
		return 0; // Aucune touche pressée, on ne fait rien
	}

	int ch = (int)wch;
	bool is_function_key = (rc == KEY_CODE_YES);

	/* Molette : on remonte dans le fil, jamais le personnage. Trois lignes par
	 * cran, comme un terminal. */
	if (is_function_key && ch == KEY_MOUSE) {
		MEVENT ev;
		if (getmouse(&ev) == OK) {
			if (ev.bstate & BUTTON4_PRESSED)      chat_scroll(game, 3);
			else if (ev.bstate & OFFSCRIPT_BUTTON5_PRESSED) chat_scroll(game, -3);
		}
		return IN_KEY_UNKN;
	}

	/* Caractere imprimable (accents compris). Il n'est saisi QUE dans le mode
	 * discussion : hors de ce mode les lettres servent aux actions du jeu
	 * (ZQSD, WASD...), pas a la question. */
	if (!is_function_key && wch >= 32 && wch != 127 && game->discussion_mode) {
		char mb[MB_LEN_MAX + 1];
		mbstate_t st;
		memset(&st, 0, sizeof(st));

		size_t n = wcrtomb(mb, (wchar_t)wch, &st);
		if (n == (size_t)-1) return IN_KEY_UNKN;   // hors du jeu de caracteres local

		/* On ne coupe jamais un caractere en deux : soit il tient en entier,
		 * soit on l'ignore. */
		input_insert(game, mb, n);
		return IN_KEY_TEXT;
	}

	/* Touches d'edition : elles ne se reglent pas, et n'ont de sens que dans
	 * la zone de saisie. */
	if (game->discussion_mode) {
		switch (ch) {
			case KEY_BACKSPACE:
			case 127:
				input_backspace(game);
				return IN_KEY_TEXT;
			case KEY_DC:
				input_delete(game);
				return IN_KEY_TEXT;
			case KEY_HOME:
				game->chat.cursor = 0;
				return IN_KEY_TEXT;
			case KEY_END:
				game->chat.cursor = game->chat.text_size;
				return IN_KEY_TEXT;
		}
	}

	/* Le reste passe par la table des reglages : le joueur choisit ses
	 * touches (voir options.c). */
	int moved = 0; // Variable pour vérifier si le joueur a bougé
	switch (options_action_for_key(&game->options, ch, is_function_key)) {
		/* En mode discussion les fleches deplacent le point d'insertion au
		 * lieu du joueur : on corrige une question sans quitter la piece. */
		case ACT_UP:
			if (game->discussion_mode) { chat_scroll(game, 1); return IN_KEY_UNKN; }
			moved = try_move(game, 0, -1);
			break;
		case ACT_DOWN:
			if (game->discussion_mode) { chat_scroll(game, -1); return IN_KEY_UNKN; }
			moved = try_move(game, 0, 1);
			break;
		case ACT_LEFT:
			if (game->discussion_mode) {
				game->chat.cursor = utf8_prev(game->chat.text_buffer, game->chat.cursor);
				return IN_KEY_TEXT;
			}
			moved = try_move(game, -2, 0);
			break;
		case ACT_RIGHT:
			if (game->discussion_mode) {
				game->chat.cursor = utf8_next(game->chat.text_buffer, game->chat.cursor);
				return IN_KEY_TEXT;
			}
			moved = try_move(game, 2, 0);
			break;

		/* Remonter dans le fil marche dans les deux modes : on relit ce qui
		 * a ete dit sans avoir a interrompre ce qu'on ecrit. */
		case ACT_SCROLL_UP:
			chat_scroll(game, 5);
			return IN_KEY_UNKN;
		case ACT_SCROLL_DOWN:
			chat_scroll(game, -5);
			return IN_KEY_UNKN;

		case ACT_JOURNAL:	// CARNET D'ENQUETE
			journal_show(game);
			return IN_KEY_UNKN;

		case ACT_NEXT_TARGET:	// CHANGER D'INTERLOCUTEUR dans la piece
			npc_cycle_target(game);
			return IN_KEY_MODE;

		case ACT_PERSO:	// MENU PERSO
			if (menu_character(game)) {
				pthread_mutex_lock(&game->chat.m_send_text);
				game->chat.new_perso = 1;
				pthread_mutex_unlock(&game->chat.m_send_text);
			}
			return IN_KEY_PERSO;

		case ACT_MENU: // Ouvre le menu : options, solution, quitter
			return IN_KEY_MENU;

		case ACT_TALK: // Prendre la parole / envoyer
			/* Entree ouvre le mode discussion, et le referme quand la saisie
			 * est vide. C'est la seule facon d'ecrire : hors de ce mode le
			 * clavier reste libre pour les actions. */
			if (!game->discussion_mode || game->chat.text_size == 0) {
				game->discussion_mode = !game->discussion_mode;
				return IN_KEY_MODE;
			}

			/* La question n'est PAS ecrite dans le fil ici : tant qu'on ne
			 * sait pas si quelqu'un peut l'entendre, l'afficher ferait croire
			 * qu'elle a ete posee. C'est la boucle de jeu qui l'ajoute, une
			 * fois l'envoi accepte. */
			pthread_mutex_lock(&game->chat.m_send_text); // Obtenir le mutex pour envoyer le texte
			if (game->chat.ready_to_send == 0){
				game->chat.text_buffer[game->chat.text_size] = '\0'; // Assurer la terminaison de la chaîne
				snprintf(game->chat.text_to_send, sizeof(game->chat.text_to_send),
				         "%s", game->chat.text_buffer);
				game->chat.ready_to_send = 1; // Indiquer que le texte est prêt à être envoyé
				input_clear(game);

				pthread_mutex_unlock(&game->chat.m_send_text); // Libérer le mutex
				return IN_KEY_SEND; // Indiquer que le texte a été envoyé
			}
			pthread_mutex_unlock(&game->chat.m_send_text); // Libérer le mutex
			break;

		default:
			// Touche non reglee : rien a faire.
			return IN_KEY_UNKN;
	}

	if (moved){
		/* On quitte la piece : l'interlocuteur choisi n'a plus de sens, on
		 * repart sur le plus proche. */
		if (map_room_at(&game->map, game->player.x, game->player.y) !=
		    (game->talk_choice >= 0 ? game->npcs[game->talk_choice].room : ROOM_NONE))
			game->talk_choice = -1;

		/* Le joueur bouge : c'est le seul moment ou la designation automatique
		 * se refait. Se placer devant quelqu'un, c'est s'adresser a lui. */
		game->talk_auto = -1;
		npc_refresh_target(game);

		pthread_mutex_lock(&game->chat.m_send_text);
		game->player.lastx = game->player.x; // Sauvegarder la position précédente
		game->player.lasty = game->player.y; // Sauvegarder la position précédente
		game->chat.new_pos = 1; // Indiquer que la position du joueur a changé
		pthread_mutex_unlock(&game->chat.m_send_text);
		return IN_KEY_MOVE;
	}
	return IN_KEY_UNKN;
}
