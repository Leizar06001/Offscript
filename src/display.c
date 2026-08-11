#include "includes.h"

static wchar_t *c_wall_diag_r 	= L"╱";
// static wchar_t *c_wall_diag_l 	= L"╲";
// static wchar_t *c_horiz_one 	= L"─";
static wchar_t *c_horiz_two 	= L"═";
static wchar_t *c_vert_one 	= L"│";
// static wchar_t *c_vert_two 	= L"║";

static int check_wall_type(Game *game, int x, int y){
	Map *mp = &game->map;

	/* map_tile borne la lecture et rend '\0' hors carte : la carte fait
	 * exactement w*h caracteres, donc tout acces direct deborde des qu'on
	 * regarde la case d'a cote au bord. */
	char mL = map_tile(mp, x - 1, y);
	char mR = map_tile(mp, x + 1, y);
	char mU = map_tile(mp, x, y - 1);
	char mD = map_tile(mp, x, y + 1);

	char mdirs[4] = {mL, mR, mU, mD};

	int nb_connections = 0;
	for(int i = 0; i < 4; i++){
		if (mdirs[i] == '1' || mdirs[i] == '2') nb_connections++;
	}

	if (nb_connections > 2) return 0;

	if ((mL == '1' || mL == '2') && (mR == '1' || mR == '2')) return 1;
	if ((mU == '1' || mU == '2') && (mD == '1' || mD == '2')) return 2;

	return 0;
}

// void clear_emoji_if_needed(WINDOW *win, int y, int x) {
//     cchar_t cval;
//     wchar_t wc;

//     // Check (y, x - 1)
//     if (x > 0 && mvwin_wch(win, y, x - 1, &cval) != ERR) {
//         wc = cval.chars[0];
//         if (wcwidth(wc) == 2) {
//             mvwaddch(win, y, x - 1, ' ');
//             mvwaddch(win, y, x, ' ');
//         }
//     }

//     // Else check (y, x)
//     else if (mvwin_wch(win, y, x, &cval) != ERR) {
//         wc = cval.chars[0];
//         if (wcwidth(wc) == 2) {
//             mvwaddch(win, y, x, ' ');
//             mvwaddch(win, y, x + 1, ' ');
//         }
//     }
// }

static const int wall_h = 3;
static const int small_wall_h = 2;

void set_map_color(Game *game, int pair){
	// if (game->connected_to_server){
		wattron(game->display.main_win, COLOR_PAIR(pair));
	// } else {
	// 	wattron(game->display.main_win, COLOR_PAIR(9));
	// }
}

/* Un personnage a dessiner, avec sa position a l'ecran et sur la carte. */
typedef struct {
	int screen_x, screen_y;
	int map_x, map_y;
	int face, body, legs;
} DrawEntity;

/* Le mur qui masque un personnage, et sa hauteur. Le corps disparait derriere
 * un mur haut ('1') mais pas derriere un muret ('2') : c'est la meme regle
 * pour tout le monde, alors que les PNJ ne regardaient que les murs hauts. */
static void entity_occlusion(Game *game, int mx, int my, int *dist_wall, int *cur_wall_h){
	*dist_wall  = wall_h;
	*cur_wall_h = wall_h;

	for (int i = 0; i < wall_h; i++){
		char t = map_tile(&game->map, mx + i + 1, my + i);
		if (t == '1'){ *dist_wall = i; return; }
		if (t == '2'){ *cur_wall_h = small_wall_h; *dist_wall = i; return; }
	}
}

/* Un personnage occupe trois lignes (visage, buste, jambes) et ses emojis font
 * DEUX colonnes. Deux personnages decales d'une seule colonne se chevauchent
 * donc a moitie : se contenter de dessiner celui de devant par-dessus laisse
 * la moitie de l'emoji de derriere a l'ecran. On ne dessine pas du tout la
 * partie cachee. */
static bool hidden_by_front(const DrawEntity *ents, int n, int self, int row){
	const DrawEntity *e = &ents[self];
	for (int j = self + 1; j < n; j++){        /* dessines apres = devant */
		const DrawEntity *f = &ents[j];
		if (abs(f->screen_x - e->screen_x) >= 2) continue;
		if (row <= f->screen_y && row >= f->screen_y - 2) return true;
	}
	return false;
}

static void draw_one_entity(Game *game, const DrawEntity *ents, int n, int self){
	const DrawEntity *e = &ents[self];
	int dist_wall, cur_wall_h;
	entity_occlusion(game, e->map_x, e->map_y, &dist_wall, &cur_wall_h);

	if (e->screen_y - 2 > 0 && !hidden_by_front(ents, n, self, e->screen_y - 2))
		mvwaddwstr(game->display.main_win, e->screen_y - 2, e->screen_x, faces[e->face]);

	if (e->screen_y - 1 > 0 && dist_wall > cur_wall_h - 2 &&
	    !hidden_by_front(ents, n, self, e->screen_y - 1))
		mvwaddwstr(game->display.main_win, e->screen_y - 1, e->screen_x, bodies[e->body]);

	if (e->screen_y > 0 && dist_wall > cur_wall_h - 1 &&
	    !hidden_by_front(ents, n, self, e->screen_y))
		mvwaddwstr(game->display.main_win, e->screen_y, e->screen_x, legs[e->legs]);
}

/* Rassemble tout le monde, trie du fond vers l'avant, puis dessine. Plus une
 * case est basse a l'ecran, plus elle est proche : le dernier dessine
 * recouvre donc naturellement ceux qui sont derriere lui. */
static void draw_entities(Game *game, int center_x, int center_y){
	DrawEntity ents[NPC_MAX + 1];
	int n = 0;

	ents[n].screen_x = center_x;
	ents[n].screen_y = center_y;
	ents[n].map_x    = game->player.x;
	ents[n].map_y    = game->player.y;
	ents[n].face     = game->player.face_id;
	ents[n].body     = game->player.body_id;
	ents[n].legs     = game->player.legs_id;
	n++;

	for (int i = 0; i < game->nb_npcs && n < NPC_MAX + 1; i++){
		const NPC *npc = &game->npcs[i];
		if (!npc->present) continue;

		int sx = npc->x - game->player.x + center_x + game->player.y - npc->y;
		int sy = npc->y - game->player.y + center_y;
		/* Un emoji occupe DEUX colonnes : il faut donc la place pour sx ET
		 * sx+1 a l'interieur du cadre. Accepter sx = largeur-2 revenait a
		 * ecrire la moitie droite du personnage sur le trait du cadre, qui
		 * n'est redessine qu'au demarrage : le bord droit y perdait des
		 * morceaux definitivement. */
		if (sx < 1 || sx + 1 >= (int)game->display.width - 1) continue;
		if (sy < 1 || sy >= (int)game->display.height - 1) continue;

		ents[n].screen_x = sx;
		ents[n].screen_y = sy;
		ents[n].map_x    = npc->x;
		ents[n].map_y    = npc->y;
		ents[n].face     = npc->face_id;
		ents[n].body     = npc->body_id;
		ents[n].legs     = npc->legs_id;
		n++;
	}

	/* Tri par insertion : au plus une trentaine de personnages, et la liste
	 * est presque toujours deja triee d'une image a l'autre. */
	for (int i = 1; i < n; i++){
		DrawEntity tmp = ents[i];
		int k = i - 1;
		while (k >= 0 && (ents[k].screen_y > tmp.screen_y ||
		                  (ents[k].screen_y == tmp.screen_y &&
		                   ents[k].screen_x > tmp.screen_x))){
			ents[k + 1] = ents[k];
			k--;
		}
		ents[k + 1] = tmp;
	}

	for (int i = 0; i < n; i++) draw_one_entity(game, ents, n, i);
}

static void draw_map_iso(Game *game){
	int center_x = game->display.width / 2;
	int center_y = game->display.height / 2;

	wattron(game->display.main_win, A_BOLD);
	set_map_color(game, 8);

	for (size_t y = 1; y < game->display.height - 1; y++) {
		for (size_t x = 1; x < game->display.width - 1; x++) {

			int map_x = game->player.x - game->player.y + (x - center_x);
			int map_y = game->player.y + (y - center_y);

			if (map_x + map_y >= 0 && map_x + map_y < (int)game->map.w && map_y >= 0 && map_y < (int)game->map.h) {
				char ch = 0;
				int wtype;
				char cur_tile = map_tile(&game->map, map_x + map_y, map_y);
				int cur_wall_h = wall_h;
				switch (cur_tile){
					case ' ':
						// ch = ' ';
						// mvwaddch(game->display.main_win, y, x, ch);
						set_map_color(game, 22);
						mvwaddwstr(game->display.main_win, y, x, L"╳");
						set_map_color(game, 8);
						break;

					case '1':
					case '2':
						if (cur_tile == '2') cur_wall_h = small_wall_h;
						wtype = check_wall_type(game, (map_x + map_y), map_y);

						switch (wtype){
							case 0:
								mvwaddwstr(game->display.main_win, y, x, c_vert_one);
								for(int w = 0; w < cur_wall_h; w++){
									if (y - w > 0) mvwaddwstr(game->display.main_win, y - w, x, c_vert_one);
								}
								if (y - cur_wall_h > 0 && map_y - 1 > 0) {
									if (map_tile(&game->map, map_x + map_y, map_y - 1) == '1'){
										mvwaddwstr(game->display.main_win, y - cur_wall_h, x, L"╒");
										continue;
									}
								}
								if (y - cur_wall_h > 0)
									mvwaddch(game->display.main_win, y - cur_wall_h, x, ',');
								break;

							case 1:
								mvwaddwstr(game->display.main_win, y, x, L"_");
								
								for(int w = 1; w < cur_wall_h; w++){
									if (y - w > 0) mvwaddch(game->display.main_win, y - w, x, ' ');
								}
								// rch = (mvwinch(game->display.main_win, y - cur_wall_h, x) & A_CHARTEXT);
								// if (y - cur_wall_h > 0 && rch != '/') mvwaddch(game->display.main_win, y - cur_wall_h, x, '_');

								cchar_t wcval;
								wchar_t wch;
								mvwin_wch(game->display.main_win, y - cur_wall_h, x, &wcval);

								// Le tableau `wcval.chars` contient les caractères, terminé par L'\0'
								wch = wcval.chars[0];

								if (y - cur_wall_h > 0 && wch != L'╲') {
									mvwaddwstr(game->display.main_win, y - cur_wall_h, x, c_horiz_two);
								}

								break;

							case 2:
								mvwaddwstr(game->display.main_win, y, x, c_wall_diag_r);
								for(int w = 1; w < cur_wall_h; w++){
									if (y - w > 0) mvwaddch(game->display.main_win, y - w, x, ' ');
								}
								if (y - cur_wall_h > 0) mvwaddwstr(game->display.main_win, y - cur_wall_h, x, c_wall_diag_r); //c_wall_diag_r);
								break;

						}
						break;

					// Vertical doors opened
					case 'v':
						set_map_color(game, 20);
						for(int w = 0; w < cur_wall_h - 1; w++){
							if (y - w > 0) mvwaddwstr(game->display.main_win, y - w, x, L"/");
						}
						set_map_color(game, 8);
						break;

					// // Vertical doors closed
					case 'V':
						set_map_color(game, 25);
						for(int w = 0; w < cur_wall_h + 1; w++){
							if (y - w > 0) mvwaddwstr(game->display.main_win, y - w, x, L"/");
						}
						set_map_color(game, 8);
						break;

					// // Horizontal doors opened
					case 'h':
						set_map_color(game, 20);
						if (y > 0) {
							mvwaddwstr(game->display.main_win, y, x, L"├");
							if (x - 1 > 0) mvwaddwstr(game->display.main_win, y, x - 1, L"┤");
						}
						
						if (y - 1 > 0){
							mvwaddwstr(game->display.main_win, y - 1, x, L"┌");
							if (x - 1 > 0) mvwaddwstr(game->display.main_win, y - 1, x - 1, L"┐");
						}
						set_map_color(game, 8);
						break;

					// // Horizontal doors closed
					case 'H':
						set_map_color(game, 25);
						for(int w = 0; w < cur_wall_h; w++){
							if (y - w > 0) {
								mvwaddwstr(game->display.main_win, y - w, x, L"├");
								if (x - 1 > 0) mvwaddwstr(game->display.main_win, y - w, x - 1, L"┤");
							}
						}
						if (y - cur_wall_h > 0){
							mvwaddwstr(game->display.main_win, y - cur_wall_h, x, L"_");
							if (x - 1 > 0) mvwaddwstr(game->display.main_win, y - cur_wall_h, x - 1, L"_");
						}
						set_map_color(game, 8);
						break;
					
					default:
						ch = ' ';
						mvwaddch(game->display.main_win, y, x, ch);
						break;
				}
				// if (ch) mvwaddch(game->display.main_win, y, x, ch); // Dessiner le caractère
			} else {
				mvwaddch(game->display.main_win, y, x, ' '); // Hors des limites de la carte
			}
		}
	}
	

	/* Joueur et PNJ sont dessines ensemble, du fond vers l'avant. Avant, les
	 * PNJ sortaient dans l'ordre du tableau et le joueur toujours en dernier :
	 * quelqu'un place derriere un mur pouvait recouvrir quelqu'un place devant,
	 * et il fallait des cas particuliers pour que le joueur ne s'efface pas
	 * lui-meme. Trier par profondeur supprime tout cela. */
	draw_entities(game, center_x, center_y);

	/* Ceinture et bretelles : la colonne du bord droit est reecrite a chaque
	 * image. Le cadre n'est trace qu'a la creation des fenetres, donc n'importe
	 * quel debordement d'un caractere large y laisserait un trou permanent —
	 * y compris celui du joueur si le terminal devient tres etroit. */
	wattroff(game->display.main_win, A_BOLD);
	/* Meme couleur que le reste du cadre, qui suit le mode en cours (voir
	 * draw_window_frames) : sinon la colonne de droite resterait grise sur un
	 * cadre allume. */
	int frame_pair = game->discussion_mode ? PAIR_BORDER : PAIR_GOOD;
	wattron(game->display.main_win, COLOR_PAIR(frame_pair));
	for (size_t y = 1; y < game->display.height - 1; y++)
		mvwaddch(game->display.main_win, y, game->display.width - 1, ACS_VLINE);
	wattroff(game->display.main_win, COLOR_PAIR(frame_pair));

	wattroff(game->display.main_win, COLOR_PAIR(8));
	wrefresh(game->display.main_win);
}









char *empty = " ";
wchar_t *wall 			= L"█";

wchar_t *desk 			= L"▒";

wchar_t *vOpenDoor 		= L"░";

wchar_t *vClosedDoor 	= L"▓";

wchar_t *lowLcorner 	= L"▙";
wchar_t *lowRcorner 	= L"▟";
wchar_t *topLcorner 	= L"▛";
wchar_t *topRcorner 	= L"▜";

wchar_t *topBlock		= L"▀";
wchar_t *botBlock		= L"▄";


int update_display(Game *game){
	if (!game) return -1;

	curs_set(0);

	draw_map_iso(game);
	print_talk_hint(game);

	// // The player will always be at the center of the window
	// int center_x = game->display.width / 2;
	// int center_y = game->display.height / 2;
	// // Draw the map around the player
	// wattron(game->display.main_win, COLOR_PAIR(8));
	// for (size_t y = 1; y < game->display.height - 1; y++) {
	// 	for (size_t x = 1; x < game->display.width - 1; x++) {
	// 		int map_x = game->player.x + (x - center_x);
	// 		int map_y = game->player.y + (y - center_y);

	// 		if (map_x >= 0 && map_x < (int)game->map.w && map_y >= 0 && map_y <= (int)game->map.h) {
	// 			// if (map_y * game->map.w + map_x > game->map.map_len) continue;

	// 			wchar_t *ch = NULL;
	// 			switch (game->map.map[map_y * game->map.w + map_x]){
	// 				case ' ':
	// 					if (game->map.map[(map_y + 1) * game->map.w + map_x] == 'c' || 
	// 						(game->map.map[(map_y + 1) * game->map.w + map_x] == '1' && game->map.map[(map_y - 1) * game->map.w + map_x] == '2' && (game->map.map[map_y * game->map.w + map_x + 1] == 'v' || game->map.map[map_y * game->map.w + map_x - 1] == 'v'))){
	// 						ch = topBlock;
	// 						wattron(game->display.main_win, COLOR_PAIR(22));
	// 					} else {
	// 						mvwaddch(game->display.main_win, y, x, ' ');
	// 					}
						
	// 					break;
	// 				case '1':
	// 					wattron(game->display.main_win, COLOR_PAIR(8));
	// 					ch = wall;
	// 					break;
	// 				case '2':
	// 					wattron(game->display.main_win, COLOR_PAIR(22));
	// 					ch = wall;
	// 					break;
	// 				case 'v':
	// 					wattron(game->display.main_win, COLOR_PAIR(20));
	// 					mvwaddwstr(game->display.main_win, y, x, vOpenDoor);
	// 					wattron(game->display.main_win, COLOR_PAIR(8));
	// 					break;
	// 				case 'V':
	// 					wattron(game->display.main_win, COLOR_PAIR(21));
	// 					mvwaddwstr(game->display.main_win, y, x, vClosedDoor);
	// 					wattron(game->display.main_win, COLOR_PAIR(8));
	// 					break;
	// 				case 'c':
	// 					wattron(game->display.main_win, COLOR_PAIR(22));
	// 					ch = lowLcorner;
	// 					// ch = botBlock;
	// 					break;
	// 				case 'o':
	// 					wattron(game->display.main_win, COLOR_PAIR(23));
	// 					ch = topLcorner;
	// 					break;
	// 				case 'p':
	// 					wattron(game->display.main_win, COLOR_PAIR(23));
	// 					ch = topRcorner;
	// 					break;
	// 				case 'k':
	// 					wattron(game->display.main_win, COLOR_PAIR(23));
	// 					ch = lowLcorner;
	// 					break;
	// 				case 'l':
	// 					wattron(game->display.main_win, COLOR_PAIR(23));
	// 					ch = lowRcorner;
	// 					break;
	// 				case 'i':
	// 					wattron(game->display.main_win, COLOR_PAIR(23));
	// 					ch = topBlock;
	// 					break;
	// 				case 'j':
	// 					wattron(game->display.main_win, COLOR_PAIR(23));
	// 					ch = botBlock;
	// 					break;
	// 				case 'u':
	// 					wattron(game->display.main_win, COLOR_PAIR(23));
	// 					ch = vClosedDoor;
	// 					break;
	// 			}
	// 			if (ch) mvwaddwstr(game->display.main_win, y, x, ch); // Dessiner le caractère
	// 		} else {
	// 			mvwaddch(game->display.main_win, y, x, ' '); // Hors des limites de la carte
	// 		}
	// 	}
	// }
	// wattroff(game->display.main_win, COLOR_PAIR(8));

	// // Dessiner le joueur au centre de la fenêtre en rouge
	// wattron(game->display.main_win, COLOR_PAIR(game->player.color)); // Activer la couleur du joueur
	// mvwaddch(game->display.main_win, center_y - 1, center_x, '@'); // Dessiner le joueur au centre
	// mvwaddwstr(game->display.main_win, center_y, center_x, L"|");
	// wattroff(game->display.main_win, COLOR_PAIR(game->player.color)); // Désactiver la couleur

	// // Draw clients
	// uint64_t t_now = millis();
	// for (int i = 0; i < MAX_CLIENTS; i++) {
	// 	if (game->clients[i].connected) {
	// 		// wprintw(game->display.info, "Update client %d\n", i);
	// 		int client_x = game->clients[i].x - game->player.x + center_x;
	// 		int client_y = game->clients[i].y - game->player.y + center_y;
	// 		if (client_x >= 1 && client_x < (int)game->display.width - 1 &&
	// 			client_y >= 1 && client_y < (int)game->display.height - 1) {
				
	// 			int color = 0;
	// 			if (game->clients[i].color >= MIN_COLOR && game->clients[i].color <= MAX_COLOR){
	// 				color = game->clients[i].color;
	// 			}
	// 			wattron(game->display.main_win, COLOR_PAIR(color)); // Activer la couleur du client
	// 			if (client_y > 1){
	// 				mvwaddch(game->display.main_win, client_y - 1, client_x, '@'); // Dessiner le client
	// 			}
	// 			mvwaddwstr(game->display.main_win, client_y, client_x, L"|");

	// 			if (t_now - game->clients[i].last_msg < DURATION_MSG_NOTIF){
	// 				mvwaddch(game->display.main_win, client_y - 1, client_x, '!');
	// 			}

	// 			wattroff(game->display.main_win, COLOR_PAIR(color)); // Désactiver la couleur
	// 		}
	// 	}
	// }

	// refresh();
	// wrefresh(game->display.main_win); // Rafraîchir la fenêtre principale
	// wrefresh(game->display.self_text); // Rafraîchir la fenêtre de texte
	move_cursor_back(game);

	curs_set(1);

	return 0;
}

/* Ecrit « <prefixe><[Touche]><libelle> » sur stdscr et renvoie la colonne
 * suivante. Le nom de la touche porte la meme couleur que dans la barre du
 * haut, pour qu'une touche se reconnaisse partout de la meme facon. */
int print_key_hint(int y, int x, const char *prefix, const char *key, const char *label){
	if (prefix && *prefix){
		attron(COLOR_PAIR(9));
		mvprintw(y, x, "%s", prefix);
		attroff(COLOR_PAIR(9));
		x += text_display_cols(prefix);
	}
	attron(COLOR_PAIR(PAIR_WARN));
	mvprintw(y, x, "%s", key);
	attroff(COLOR_PAIR(PAIR_WARN));
	x += text_display_cols(key);

	attron(COLOR_PAIR(9));
	mvprintw(y, x, "%s", label);
	attroff(COLOR_PAIR(9));
	return x + text_display_cols(label);
}

/* Ligne 1 de l'ecran, entre l'en-tete et la carte : ou l'on se trouve et qui
 * est la. On parle a qui partage la piece, donc c'est cette ligne qui dit a
 * qui la prochaine question s'adressera. */
/* Un morceau de la ligne, avec ses attributs. La ligne est d'abord assemblee,
 * puis mesuree, puis dessinee : c'est le seul moyen de la centrer sans decrire
 * son contenu deux fois — une mesure ecrite a part finirait par mentir des que
 * le texte change. */
typedef struct {
	const char *text;
	int         attrs;
} HintSeg;

#define HINT_MAX_SEGS (4 + NPC_MAX * 3 + 4)

/* Ligne 0 : la barre des touches. Ligne 1 : laissee vide, pour que la barre et
 * cette ligne ne se lisent pas comme un seul bloc. Les fenetres commencent a la
 * ligne 3 (create_windows), donc la 2 est libre. */
#define TALK_HINT_ROW 2

void print_talk_hint(Game *game){
	int list[NPC_MAX];
	int n = npc_in_player_room(game, list, NPC_MAX);
	/* On lit la designation figee par la boucle de jeu : la recalculer ici la
	 * ferait dependre du nombre de redessins. */
	int idx = game->talk_target;

	HintSeg seg[HINT_MAX_SEGS];
	int nb = 0;

	/* Les textes composes ont besoin de vivre jusqu'au dessin. */
	char room_chip[96];
	char names[NPC_MAX][96];

	const char *room = map_room_name(&game->map, map_room_at(&game->map,
	                                 game->player.x, game->player.y));
	if (room && *room){
		/* La piece en pastille inversee : cette ligne se perdait entre la barre
		 * du haut et le cadre de la carte, alors qu'elle dit l'essentiel — ou on
		 * est, et a qui on va parler. */
		snprintf(room_chip, sizeof(room_chip), " %s ", room);
		seg[nb].text  = room_chip;
		seg[nb].attrs = COLOR_PAIR(PAIR_HEADING) | A_REVERSE | A_BOLD;
		nb++;
	}

	if (n == 0){
		seg[nb].text  = "   personne ici";
		seg[nb].attrs = COLOR_PAIR(9);
		nb++;
	} else {
		seg[nb].text  = "   avec ";
		seg[nb].attrs = COLOR_PAIR(9);
		nb++;

		/* Celui a qui la question s'adressera porte un chevron et son nom en
		 * inverse : souligne et gras ne suffisaient pas a le distinguer des
		 * autres noms, qui portent deja leur propre couleur. */
		for (int i = 0; i < n && nb + 3 < HINT_MAX_SEGS; i++){
			const char *name = npc_display_name(&game->npcs[list[i]]);
			bool aimed = (list[i] == idx);
			int  pair  = COLOR_PAIR(npc_color(list[i]));

			if (aimed){
				seg[nb].text  = "▸";           /* ▸ */
				seg[nb].attrs = pair | A_BOLD;
				nb++;
			}
			snprintf(names[i], sizeof(names[i]), aimed ? " %s " : "%s", name);
			seg[nb].text  = names[i];
			seg[nb].attrs = pair | (aimed ? (A_BOLD | A_REVERSE) : A_DIM);
			nb++;

			if (i + 1 < n){
				seg[nb].text  = ", ";
				seg[nb].attrs = COLOR_PAIR(9);
				nb++;
			}
		}

		/* Une touche se reconnait a sa couleur, la meme que dans la barre du
		 * haut : le nom de la touche ressort, son libelle reste discret. */
		const char *key = (n > 1) ? "[Tab]" : "[Entree]";
		seg[nb].text = "   ";     seg[nb].attrs = COLOR_PAIR(9);         nb++;
		seg[nb].text = key;       seg[nb].attrs = COLOR_PAIR(PAIR_WARN); nb++;

		/* Le libelle s'abrege plutot que de deborder : avec deux personnes dans
		 * la piece, la version longue depasse les 80 colonnes et se faisait
		 * couper par la droite — donc la touche restait, mais amputee. On garde
		 * le plus long qui tient. */
		int used = 0;
		for (int i = 0; i < nb; i++) used += text_display_cols(seg[i].text);

		const char *labels[4];
		if (n > 1) {
			labels[0] = " changer d'interlocuteur, ou nommez-le";
			labels[1] = " changer d'interlocuteur";
			labels[2] = " changer";
			labels[3] = "";
		} else if (game->discussion_mode) {
			labels[0] = " envoyer"; labels[1] = " envoyer";
			labels[2] = " envoyer"; labels[3] = "";
		} else {
			labels[0] = " pour lui parler"; labels[1] = " lui parler";
			labels[2] = " parler";         labels[3] = "";
		}
		const char *label = labels[3];
		for (int i = 0; i < 4; i++) {
			if (used + text_display_cols(labels[i]) <= COLS) { label = labels[i]; break; }
		}
		seg[nb].text = label;     seg[nb].attrs = COLOR_PAIR(9);         nb++;
	}

	int total = 0;
	for (int i = 0; i < nb; i++) total += text_display_cols(seg[i].text);

	/* Centree dans le terminal. Une ligne plus large que l'ecran repart de la
	 * colonne 0 : la tronquer par la gauche cacherait la piece. */
	int col = (COLS - total) / 2;
	if (col < 0) col = 0;

	/* Une ligne vide la separe de la barre des touches, juste au-dessus : les
	 * deux se lisaient comme un seul bloc. La ligne 2 etait deja libre (les
	 * fenetres commencent a la 3, voir create_windows), donc rien ne bouge en
	 * dessous. */
	move(1, 0);
	clrtoeol();
	// clrtoeol: sinon un nom plus court laisse la fin du precedent affichee
	move(TALK_HINT_ROW, 0);
	clrtoeol();

	for (int i = 0; i < nb; i++){
		attron(seg[i].attrs);
		mvprintw(TALK_HINT_ROW, col, "%s", seg[i].text);
		attroff(seg[i].attrs);
		col += text_display_cols(seg[i].text);
	}

	refresh();
}

/* ------------------------------------------------------------------ */
/* Zone de saisie                                                      */
/* ------------------------------------------------------------------ */

/* Decoupe la saisie en lignes d'ecran. Coupure a la colonne, sans chercher
 * les espaces : le point d'insertion doit rester calculable exactement, et
 * une coupure aux mots deplacerait le texte sous le curseur pendant la
 * frappe. Renvoie le nombre de lignes et remplit starts[] avec le decalage
 * en octets du debut de chacune. */
static int input_line_starts(const char *s, int width, int *starts, int max){
	int n = 0;
	if (max <= 0) return 0;
	starts[n++] = 0;
	if (width < 1) width = 1;

	int cols = 0;
	for (int i = 0; s[i]; i++){
		if (((unsigned char)s[i] & 0xC0) == 0x80) continue;  /* octet de suite */
		if (cols == width){
			if (n >= max) return n;
			starts[n++] = i;
			cols = 0;
		}
		cols++;
	}
	return n;
}

/* Ligne affichee et colonne du point d'insertion. */
static void input_cursor_at(const char *s, int width, size_t cursor,
                            int *out_line, int *out_col){
	int starts[CHAT_INPUT_MAX / 2 + 2];
	int n = input_line_starts(s, width, starts, (int)(sizeof(starts)/sizeof(starts[0])));

	int line = 0;
	for (int i = 0; i < n; i++){
		if ((size_t)starts[i] <= cursor) line = i;
		else break;
	}
	char saved[CHAT_INPUT_MAX];
	size_t len = cursor - (size_t)starts[line];
	if (len >= sizeof(saved)) len = sizeof(saved) - 1;
	memcpy(saved, s + starts[line], len);
	saved[len] = '\0';

	*out_line = line;
	*out_col  = text_display_cols(saved);
}

/* Dessine la zone de saisie : le cadre, le nom, puis le texte replie sur
 * CHAT_INPUT_LINES lignes. Quand la question depasse, on fait defiler pour
 * garder la ligne du curseur visible. */
void draw_input(Game *game){
	WINDOW *win = game->display.self_text;
	if (!win) return;

	int h, w;
	getmaxyx(win, h, w);
	int width = w - 4;
	if (width < 8) width = 8;

	int starts[CHAT_INPUT_MAX / 2 + 2];
	int nb = input_line_starts(game->chat.text_buffer, width, starts,
	                           (int)(sizeof(starts)/sizeof(starts[0])));

	int cline, ccol;
	input_cursor_at(game->chat.text_buffer, width, game->chat.cursor, &cline, &ccol);

	int visible = h - 3;                 /* cadre haut, ligne du nom, cadre bas */
	if (visible > CHAT_INPUT_LINES) visible = CHAT_INPUT_LINES;
	if (visible < 1) visible = 1;

	int top = 0;
	if (cline >= visible) top = cline - visible + 1;

	/* Le cadre s'allume en mode discussion : on voit d'un coup d'oeil si ce
	 * qu'on tape ira dans la question ou sera ignore. */
	int frame = game->discussion_mode ? PAIR_GOOD : 24;
	werase(win);
	wattron(win, COLOR_PAIR(frame));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(frame));
	wattron(win, COLOR_PAIR(game->discussion_mode ? PAIR_GOOD : 9) | A_BOLD);
	mvwprintw(win, 0, 3, game->discussion_mode ? " VOUS PARLEZ " : " VOUS ");
	wattroff(win, COLOR_PAIR(game->discussion_mode ? PAIR_GOOD : 9) | A_BOLD);

	wattron(win, COLOR_PAIR(game->discussion_mode ? PAIR_HEADING : 9) | A_BOLD);
	mvwprintw(win, 1, 2, "%s :", game->player.name);
	wattroff(win, COLOR_PAIR(game->discussion_mode ? PAIR_HEADING : 9) | A_BOLD);

	/* Hors mode discussion la zone est inerte : on le dit, plutot que de
	 * laisser un cadre vide qui invite a taper pour rien. */
	if (!game->discussion_mode && game->chat.text_size == 0) {
		wattron(win, COLOR_PAIR(9) | A_DIM);
		mvwprintw(win, 2, 2, "[Entree] pour prendre la parole");
		wattroff(win, COLOR_PAIR(9) | A_DIM);
		game->display.input_cursor_y = 2;
		game->display.input_cursor_x = 2;
		wrefresh(win);
		return;
	}

	/* Un rappel discret quand la saisie deborde vers le haut. */
	if (top > 0){
		wattron(win, COLOR_PAIR(9));
		mvwprintw(win, 1, w - 6, "...");
		wattroff(win, COLOR_PAIR(9));
	}

	wattron(win, COLOR_PAIR(PAIR_TEXT));
	for (int i = 0; i < visible && top + i < nb; i++){
		int s = starts[top + i];
		int e = (top + i + 1 < nb) ? starts[top + i + 1] : (int)game->chat.text_size;
		mvwprintw(win, 2 + i, 2, "%.*s", e - s, game->chat.text_buffer + s);
	}
	wattroff(win, COLOR_PAIR(PAIR_TEXT));

	game->display.input_cursor_y = 2 + (cline - top);
	game->display.input_cursor_x = 2 + ccol;

	wrefresh(win);
	move_cursor_back(game);
}

/* Couleur attribuee a un personnage. Les paires MIN_COLOR..MAX_COLOR sont
 * initialisees dans start_game ; on boucle dessus si l'histoire compte plus
 * de personnages que de couleurs. Un personnage garde ainsi la meme couleur
 * dans le chat, la ligne d'aide et le carnet. */
int npc_color(int npc_index){
	return MIN_COLOR + (npc_index % (MAX_COLOR - MIN_COLOR + 1));
}

/* Une relation elevee n'est pas toujours une bonne nouvelle : beaucoup de
 * confiance est favorable, beaucoup de peur ne l'est pas. La couleur suit donc
 * le sens de l'axe, pas seulement l'amplitude. */
int relation_color(int value, int positive_axis){
	int strong = positive_axis ? value >= 60  : value >= 60;
	int mild   = positive_axis ? value >= 20  : value >= 20;
	int bad    = positive_axis ? value <= -20 : 0;

	if (strong) return positive_axis ? PAIR_GOOD : PAIR_DANGER;
	if (mild)   return positive_axis ? PAIR_GOOD : PAIR_WARN;
	if (bad)    return PAIR_DANGER;
	return 9;   /* neutre : gris */
}

/* Largeur d'affichage d'une chaine UTF-8, en colonnes du terminal. Les textes
 * des histoires sont accentues : compter les octets ferait deborder ou rogner
 * les lignes de travers. */
int text_display_cols(const char *s){
	if (!s) return 0;
	int cols = 0;
	for (const char *p = s; *p; p++) {
		/* on ne compte que les octets de tete d'un caractere */
		if (((unsigned char)*p & 0xC0) != 0x80) cols++;
	}
	return cols;
}

/* Nombre d'octets couvrant au plus `cols` colonnes, sans jamais couper au
 * milieu d'un caractere multi-octets. */
int text_bytes_for_cols(const char *s, int cols){
	int used = 0, i = 0;
	while (s[i]) {
		if (((unsigned char)s[i] & 0xC0) != 0x80) {
			if (used == cols) break;
			used++;
		}
		i++;
	}
	return i;
}

/* Ecrit un paragraphe en coupant aux espaces et renvoie la ligne suivante
 * libre. Avec max_lines > 0, s'arrete apres ce nombre de lignes et signale la
 * suite par des points de suspension. */
int wrap_print_max(WINDOW *win, int y, int x, int width, const char *text, int max_lines){
	if (!text || width <= 1) return y;

	int line = y;
	const char *p = text;
	while (*p) {
		while (*p == ' ') p++;
		if (!*p) break;

		if (max_lines > 0 && line - y >= max_lines) {
			mvwprintw(win, line - 1, x + width - 3, "...");
			break;
		}

		int take = text_bytes_for_cols(p, width);
		if (p[take] != '\0' && p[take] != ' ') {
			int cut = take;
			while (cut > 0 && p[cut] != ' ') cut--;
			if (cut > 0) take = cut;
		}
		mvwprintw(win, line++, x, "%.*s", take, p);
		p += take;
	}
	return line;
}

int wrap_print(WINDOW *win, int y, int x, int width, const char *text){
	return wrap_print_max(win, y, x, width, text, 0);
}

/* Une fenetre superposee (carnet, menu personnage) a recouvert l'interface.
 * Les fenetres ncurses conservent leur contenu en memoire : il suffit de les
 * declarer entierement sales puis de les rafraichir pour tout retrouver, y
 * compris l'historique deja defile du chat, qu'aucun code ne pourrait
 * reconstruire autrement. */
void restore_game_screen(Game *game){
	if (!game->display.main_win) return;

	clear();
	refresh();

	print_header(game);
	print_talk_hint(game);

	WINDOW *wins[] = {
		game->display.main_win,
		game->display.self_text,
		game->display.chat_box,
		game->display.chat,
		game->display.info,
	};
	for (size_t i = 0; i < sizeof(wins) / sizeof(wins[0]); i++) {
		if (!wins[i]) continue;
		redrawwin(wins[i]);
		wrefresh(wins[i]);
	}

	move_cursor_back(game);
}

/* Remet le curseur du terminal dans la zone de saisie. La position exacte a
 * ete calculee par draw_input : text_size compte des octets, le curseur se
 * place en colonnes, et la saisie tient sur plusieurs lignes. */
void move_cursor_back(Game *game){
	if (!game->display.self_text) return;
	wmove(game->display.self_text,
	      game->display.input_cursor_y, game->display.input_cursor_x);
	wrefresh(game->display.self_text);
}

int ask_for_display_update(Game *game){
	if (game == NULL) return -1; // Invalid game pointer

	pthread_mutex_lock(&game->display.m_display_update);
	game->display.need_main_update = 1; // Set the flag to update the main display
	pthread_mutex_unlock(&game->display.m_display_update);

	return 0;
}