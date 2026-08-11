#ifndef MAP_H
#define MAP_H

#include <stddef.h>
#include <stdint.h>

#include "story.h"

struct s_game;

#define MAP_MAX_ROOMS 32
#define MAP_MAX_DOORS 64
#define ROOM_NONE (-1)

/* Une piece est une zone fermee de la carte. L'auteur ne la decrit pas par un
 * rectangle mais par un point a l'interieur : le moteur remplit la zone depuis
 * ce point, ce qui accepte aussi bien un rectangle qu'une piece en L. */
typedef struct {
	char id[48];
	char name[64];
	int  seed_x, seed_y;
	int  nb_tiles;
	int  min_x, min_y, max_x, max_y;
} Room;

/* Une porte est une case franchissable percee dans un mur. Elle n'appartient
 * a aucune piece, mais relie les deux qu'elle separe : c'est ce qui permet de
 * calculer un trajet d'une piece a l'autre. */
typedef struct {
	int x, y;
	int vertical;
	int room_a, room_b;
} Door;

typedef struct {
	char   *map;          /* w*h caracteres, sans separateur de ligne */
	size_t	map_len;
	int 	w, h;
	char	map_c_empty;

	/* Tout le monde avance de deux colonnes a la fois : personnages et joueur
	 * partagent donc une parite de x, calee sur la case de depart. Une place
	 * calculee qui l'ignorerait serait a jamais hors de portee. */
	int		x_parity;

	Room	rooms[MAP_MAX_ROOMS];
	int		nb_rooms;
	Door	doors[MAP_MAX_DOORS];
	int		nb_doors;

	int	   *room_of;      /* w*h : indice de piece, ou ROOM_NONE */
} Map;

/* Construit la carte de l'histoire chargee. Une histoire sans bloc "map"
 * retombe sur la carte integree, pour que les fichiers ecrits avant cette
 * version continuent de fonctionner. */
int  map_init(struct s_game *game, const Story *story);
void map_free(Map *m);

/* Lecture bornee : renvoie '\0' hors de la carte, ce qui evite d'avoir a
 * verifier les limites sur chaque acces (le rendu isometrique en fait
 * beaucoup, et lisait au-dela de la carte). */
char map_tile(const Map *m, int x, int y);
int  map_is_door_char(char c);
int  map_is_wall(const Map *m, int x, int y);

/* Regles de deplacement, communes au joueur et aux PNJ : un pas vertical fait
 * une ligne, un pas horizontal fait deux colonnes (la carte est dessinee en
 * isometrique, voir inputs.c) et la colonne traversee doit etre libre. */
int  map_can_stand(const Map *m, int x, int y);
int  map_can_step(const Map *m, int x, int y, int dx, int dy);

/* Piece d'une position. Une case de porte n'appartient a aucune piece : on
 * rend alors celle d'a cote, pour qu'un personnage sur le seuil soit toujours
 * quelque part. */
int  map_room_at(const Map *m, int x, int y);
const char *map_room_name(const Map *m, int room);
int  map_room_by_id(const Map *m, const char *id);

/* Place libre dans une piece, en s'ecartant des positions deja occupees.
 * Renvoie 0 et remplit out_x et out_y, ou -1 si la piece est introuvable. */
int  map_spot_in_room(const Map *m, int room,
                      const int *busy_x, const int *busy_y, int nb_busy,
                      int *out_x, int *out_y);

/* Une case au hasard dans une piece : `pick` est un entier quelconque, ramene
 * au nombre de cases disponibles. Sert aux deplacements d'oisivete. */
int  map_random_spot_in_room(const Map *m, int room, int pick, int *out_x, int *out_y);

/* Premier pas d'un trajet de (x,y) vers (tx,ty), en respectant les regles de
 * deplacement. Renvoie 0 et remplit nx et ny, ou -1 s'il n'y a pas de chemin. */
int  map_next_step(const Map *m, int x, int y, int tx, int ty, int *nx, int *ny);

/* Idem, mais en contournant des cases occupees (les autres personnages). Les
 * gens ne sont pas des murs : sans contournement, quelqu'un a qui l'on demande
 * de sortir reste fige des que le joueur se tient sur son premier pas — c'est
 * le cas le plus frequent, puisqu'on vient de lui parler face a face. */
int  map_next_step_avoid(const Map *m, int x, int y, int tx, int ty,
                         const int *busy_x, const int *busy_y, int nb_busy,
                         int *nx, int *ny);

/* Nombre de pas du trajet, ou -1 s'il n'y en a pas. */
int  map_path_len(const Map *m, int x, int y, int tx, int ty);

#endif
