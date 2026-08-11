#ifndef INCLUDES_H
#define INCLUDES_H

#define _XOPEN_SOURCE_EXTENDED 1

#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdbool.h"
#include "unistd.h"
#include <ncurses.h>
#include <termios.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>

#include "globals.h"
#include "deepseek_client.h"
#include "dialogue.h"
#include "story.h"
#include "memory.h"
#include "npc.h"
#include "apikey.h"
#include "options.h"

#define IN_KEY_EXIT -1
#define IN_KEY_UNKN	0
#define IN_KEY_MOVE	1
#define IN_KEY_TEXT	2
#define IN_KEY_SEND 3
#define IN_KEY_COLOR 4
#define IN_KEY_DOOR	5
#define IN_KEY_NOTIF 6
#define IN_KEY_PERSO 7
#define IN_KEY_MODE	 8
#define IN_KEY_MENU  9

/* Attente maximale d'une touche, en millisecondes. Ce n'est pas de la lecture
 * non bloquante : une touche flechee arrive en trois octets (ESC [ A) et,
 * sans le droit d'attendre la suite, ncurses rend l'ESC tout seul, que le jeu
 * comprend comme « quitter ». Il suffit d'une liaison lente ou d'un terminal
 * qui decoupe la sequence pour que les fleches ferment la partie. Avec ce
 * delai, la sequence est reassemblee, et la boucle tourne toujours a ~60 Hz
 * pour afficher les repliques au fil de leur arrivee. */
#define INPUT_TIMEOUT_MS 15

/* Periode de sauvegarde automatique. Ecrire est peu couteux (quelques dizaines
 * de kilo-octets) et une partie perdue l'est definitivement. */
#define AUTOSAVE_PERIOD_MS 20000

#define MIN_COLOR 10
#define MAX_COLOR 19

#define PAIR_CYAN 		1
#define PAIR_RED		2
#define PAIR_GREEN		3
#define PAIR_YELLOW		4
#define PAIR_MAGENTA 	5
#define PAIR_BLUE		6
#define PAIR_WHITE		7

/* Paires semantiques (voir start_game). */
#define PAIR_TITLE		26
#define PAIR_HEADING	27
#define PAIR_GOOD		28
#define PAIR_WARN		29
#define PAIR_DANGER		30
#define PAIR_TEXT		31

typedef struct {
	int 	x, y; // Position du joueur
	int 	color;
	int		face_id;
	int		body_id;
	int		legs_id;
	char 	name[64]; // Nom du joueur, titre compris ("Detective Poireau")
	int		connected;
	int		lastx, lasty;
	uint64_t last_msg;
} Player;

typedef struct {
	WINDOW *main_win; // Fenêtre ncurses pour l'affichage
	WINDOW *self_text;
	WINDOW *chat; // Fenêtre pour le chat
	WINDOW *chat_box;
	WINDOW *info;
	size_t height;  // Hauteur de l'affichage
	size_t width;   // Largeur de l'affichage
	int	term_h;
	int	term_w;
	int 			need_main_update;
	pthread_mutex_t m_display_update;
	uint64_t	t_next_update;
	int chat_x, chat_y, chat_w, chat_h;
	/* Position du point d'insertion dans la zone de saisie, calculee par
	 * draw_input : la question tient sur plusieurs lignes, donc elle ne se
	 * deduit plus de la longueur du texte. */
	int input_cursor_y, input_cursor_x;
} Display;

#include "map.h"

/* Taille d'une question. Trois lignes d'affichage, donc de quoi ecrire une
 * vraie question plutot qu'une phrase tronquee. */
#define CHAT_INPUT_MAX	 1024
#define CHAT_INPUT_LINES 3

/* Une entree du fil de discussion. Le nom de celui qui parle n'est PAS
 * conserve ici : seul son indice l'est, et le nom est resolu au moment du
 * rendu. C'est ce qui permet de remplacer « un inconnu » par le vrai nom dans
 * tout l'historique des qu'on l'apprend. */
#define CHAT_PLAYER	0
#define CHAT_NPC	1
#define CHAT_ACTION	2	/* ce que le personnage fait, pas ce qu'il dit */
#define CHAT_SYSTEM	3	/* note du moteur */

typedef struct {
	int   kind;
	int   npc_index;   /* -1 pour le joueur et le moteur */
	char *text;
} ChatEntry;

#define CHAT_LOG_MAX 200

typedef struct {
	char 		text_buffer[CHAT_INPUT_MAX]; // Tampon pour les messages
	size_t 		text_size; // Taille du tampon de texte
	size_t		cursor;    // Point d'insertion, en octets, dans text_buffer
	char		text_to_send[CHAT_INPUT_MAX]; // Tampon pour le texte à envoyer
	uint8_t 	ready_to_send;
	uint8_t		new_pos;
	uint8_t		new_color;
	uint8_t		new_door;
	uint8_t		new_perso;
	int			door_change_id;

	/* Le fil est conserve puis redessine entierement a chaque changement :
	 * une fenetre ncurses ou l'on ecrit au fil de l'eau ne peut ni defiler
	 * vers le haut ni voir son passe corrige. */
	ChatEntry	log[CHAT_LOG_MAX];
	int			nb_log;
	int			scroll;    // nb de lignes remontees depuis le bas (0 = en bas)
	int			live;      // entree en cours de reception, -1 sinon
	int			dirty;
	int			spinner;   // animation des points d'attente
	uint64_t	t_next_spin;

	pthread_mutex_t m_send_text;
	pthread_mutex_t m_chat_box;
} Chat;

typedef struct s_game {
	Display display; // Affichage du jeu
	Player 	player; // Le joueur
	NPC 	npcs[NPC_MAX]; // Les PNJ presents, construits depuis l'histoire
	int		nb_npcs;
	// Player	clients[100];
	Map 	map;     // Carte du jeu
	Chat 	chat;
	// ComThread com; // Thread de communication

	// Histoire chargee et etat de la partie
	const char *model_name;         // modele des repliques et de l'analyse
	const char *story_model_name;   // modele de la resolution, appele une fois
	Story		*story;
	SaveState	*save;
	char		save_path[512];
	uint64_t	t_next_autosave;

	// Dialogues avec les PNJ
	DeepseekRequest *pending_req;				// replique en cours, NULL si aucune
	int 	pending_npc;						// PNJ qui repond, -1 si aucun
	uint64_t pending_since;						// date d'envoi, pour le garde-fou
	char	pending_question[256];				// question en attente de reponse
	int		stream_started;						// le prefixe "Nom :" est deja affiche
	size_t	stream_printed;						// octets de la reponse deja affiches
	int		talk_target;						// PNJ vise dans la piece, -1 si aucun
	/* Interlocuteur choisi explicitement ([Tab], ou en le nommant). -1 = le
	 * plus proche. Sans cela, quand deux personnes partagent une piece, la
	 * seconde ne pouvait jamais repondre. */
	int		talk_choice;
	/* Interlocuteur retenu faute de choix explicite : le plus proche au moment
	 * ou le joueur s'est arrete. Fige, car un personnage qui vient se placer
	 * devant lui ne doit pas voler la parole a celui a qui il s'adressait.
	 * -1 = a redesigner (le joueur a bouge, ou l'interlocuteur a quitte la
	 * piece). */
	int		talk_auto;

	/* Mode discussion : les fleches editent la question au lieu de deplacer
	 * le joueur. On y entre et on en sort avec Entree sur une saisie vide. */
	int		discussion_mode;

	/* Le coupable vient d'avouer : la boucle de jeu montre la resolution des
	 * que sa replique a fini de s'afficher. -1 tant que rien n'est avoue. */
	int		confession_npc;

	/* Interventions entre personnages. Quand plusieurs personnes partagent la
	 * piece, l'une d'elles peut reagir a ce qui vient d'etre dit. La chaine
	 * est bornee : sans cela deux personnages se repondraient indefiniment
	 * sans que le joueur puisse reprendre la parole. */
	int		interject_npc;					// PNJ qui doit intervenir, -1 si aucun
	char	interject_context[768];			// ce qu'il vient d'entendre
	int		interject_speaker;				// de qui il l'a entendu
	int		interject_chain;				// interventions depuis la derniere question
	int		reply_is_interjection;			// la reponse en vol est une intervention

	// L'analyse memoire a son propre emplacement : elle tourne en tache de
	// fond et ne doit jamais empecher de poser la question suivante.
	DeepseekRequest *analysis_req;
	int		analysis_npc;

	int server_mode;
	char server_ip[16]; // Adresse IP du serveur
	uint16_t server_port;

	Options options;   // reglages du joueur, communs a toutes les parties

	char exit_error[256];
	int print_error;
	int notif_enabled;
	int connected_to_server;
} Game;

// utils.c
uint64_t millis();
double distance(int x1, int y1, int x2, int y2);
int min(int a, int b);
void pinfo(Game *game, const char *fmt, ...);
void pinfo_c(Game *game, int pair, const char *fmt, ...);
/* Couleur d'un personnage, la meme dans le chat, l'aide et le carnet. */
int  npc_color(int npc_index);
/* Couleur d'une valeur de relation : positive_axis = 1 pour confiance et
 * affection (haut = bon), 0 pour peur et suspicion (haut = mauvais). */
int  relation_color(int value, int positive_axis);
int shortest_distance(const char *map, int map_w, int map_h,
                      int x_start, int y_start, int x_end, int y_end);

// display.c
int update_display(Game *game);
int ask_for_display_update(Game *game);
void move_cursor_back(Game *game);
void print_talk_hint(Game *game);
void draw_input(Game *game);
/* Ecrit "<prefixe>[Touche]<libelle>" avec la couleur de touche habituelle et
 * renvoie la colonne suivante. */
int  print_key_hint(int y, int x, const char *prefix, const char *key, const char *label);
void restore_game_screen(Game *game);
int  text_display_cols(const char *s);
/* Nombre d'octets couvrant au plus `cols` colonnes, sans couper un caractere
 * multi-octets en deux. */
int  text_bytes_for_cols(const char *s, int cols);
int  wrap_print(WINDOW *win, int y, int x, int width, const char *text);
int  wrap_print_max(WINDOW *win, int y, int x, int width, const char *text, int max_lines);

// inputs.c
int get_user_input(Game *game);

// listes.c
void chat_init(Game *game);
void chat_free(Game *game);

/* Ajout d'une entree au fil. Le texte est copie. */
void chat_add(Game *game, int kind, int npc_index, const char *text);
void chat_addf(Game *game, int kind, int npc_index, const char *fmt, ...);

/* Redessine le fil si quelque chose a change. Renvoie 1 s'il a redessine. */
int  chat_render(Game *game);
void chat_touch(Game *game);          /* a redessiner (un nom vient d'etre appris) */
void chat_scroll(Game *game, int lines); /* >0 remonte dans l'historique */
void chat_tick(Game *game);              /* anime l'attente d'une replique */

/* Reponses des PNJ : elles arrivent morceau par morceau. L'entree est creee
 * vide puis completee, et le fil est redessine a chaque morceau. */
void chat_stream_begin(Game *game, int npc_index);
void chat_stream_chunk(Game *game, const char *text);
void chat_stream_end(Game *game);

// npc.c
#define NPC_TALK_SENT	1
#define NPC_TALK_NOBODY	0
#define NPC_TALK_BUSY	-1
int npc_talk_send(Game *game, const char *text);
void npc_talk_update(Game *game);

// main.c
void game_autosave(Game *game);
void print_header(Game *game);
void print_usage_bar(Game *game);

// menu.c
int  menu_ensure_api_key(Game *game); // 0 = cle utilisable, -1 = abandon
int  menu_choose_story(Game *game);   // 0 = une histoire est chargee, -1 = quitter
/* 0 = une partie est prete, -1 = quitter, MENU_BACK = revenir au choix de
 * l'enquete. [ESC] ne doit jamais fermer le jeu depuis un sous-menu. */
#define MENU_BACK 1
int  menu_choose_save(Game *game);
void menu_show_briefing(Game *game);
/* Menu des reglages : touches et tarifs. Enregistre en sortant. */
void menu_options(Game *game);
/* Menu ouvert en cours de partie : Options, Solution, Quitter.
 * Renvoie true si le joueur veut fermer le jeu. */
bool menu_pause(Game *game);
/* Ecran de resolution : aveu du coupable (won) ou abandon. Renvoie true si la
 * partie doit s'arreter, false si le joueur a relance l'enquete. */
bool menu_show_solution(Game *game, bool won);

// journal.c
void journal_show(Game *game);

#endif