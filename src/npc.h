#ifndef NPC_H
#define NPC_H

#include <stdbool.h>
#include <stdint.h>

#include "story.h"

struct s_game;

/* Runtime presence of a character on the map. Everything the author wrote
 * stays in `def` (the Story owns it) and everything that evolves lives in the
 * save (memory.h): this struct only holds what the renderer needs plus what
 * the player has discovered. */
typedef struct {
	const StoryCharacter *def;   /* fiche d'origine, jamais modifiee */

	int  x, y;
	bool present;
	int  face_id, body_id, legs_id;

	bool name_known;   /* le joueur a appris son nom */
	bool met;          /* le joueur lui a parle au moins une fois */

	/* Piece courante, tenue a jour a chaque deplacement : c'est elle qui
	 * decide a qui le joueur peut parler. */
	int  room;

	/* Autorisations de deplacement. Copiees de la fiche au demarrage, puis
	 * modifiables en cours de partie (un robot qu'on rallume). */
	bool can_move;
	bool can_change_room;

	/* Deplacement en cours : case visee et date du prochain pas. Un PNJ
	 * avance d'une case a la fois, pour qu'on le voie marcher. */
	int      dest_x, dest_y;
	bool     moving;

	/* Il s'est deplace POUR parler a quelqu'un ("rejoint:<id>"). Sans le
	 * retenir, il traversait le batiment et, une fois arrive, il ne se
	 * passait rien. -1 = il se deplace sans intention particuliere. */
	int      goes_to_talk_to;
	uint64_t t_next_step;
	uint64_t t_next_wander;
} NPC;

/* Le nombre de personnages vient de l'histoire chargee. Cette borne ne sert
 * qu'au dimensionnement des tableaux fixes (couleurs, pages du journal). */
#define NPC_MAX 32

/* Builds the runtime array from the story's characters, using each one's
 * optional "game" block for appearance and placement. Returns the count.
 * Positions are only resolved later, by npc_place_all, because placing by
 * room needs a map and the map belongs to the story. */
int npc_build_from_story(NPC *out, int max, const Story *story);

/* Pose chaque personnage sur la carte : a ses coordonnees si la fiche en
 * donne, sinon a une place libre de la piece nommee. A appeler une fois la
 * carte construite. */
void npc_place_all(struct s_game *game);

/* Personnages presents dans la piece du joueur. Ecrit au plus max indices
 * dans out (les plus proches d'abord) et renvoie leur nombre : on parle a qui
 * partage la piece, plus besoin d'etre colle a lui. */
int npc_in_player_room(const struct s_game *game, int *out, int max);

/* Celui a qui la prochaine question s'adresse : le choix explicite, sinon
 * celui fige au dernier arret du joueur, sinon le plus proche de la piece.
 * -1 si le joueur est seul. */
int npc_find_target(const struct s_game *game);

/* Fige l'interlocuteur du moment et renvoie son indice. A appeler une fois par
 * tour de boucle : la designation ne bouge alors que quand le joueur bouge ou
 * quand l'interlocuteur quitte la piece. */
int npc_refresh_target(struct s_game *game);

/* Met a jour npc->room apres un deplacement. */
void npc_refresh_room(struct s_game *game, int idx);

/* Etat du monde <-> sauvegarde : positions, visages, noms appris,
 * autorisations de deplacement. */
void npc_restore_from_save(struct s_game *game);
void npc_sync_to_save(struct s_game *game);

/* Fait avancer les personnages d'un pas quand c'est l'heure, et declenche de
 * loin en loin un deplacement d'oisivete. A appeler a chaque tour de boucle. */
void npc_movement_update(struct s_game *game);

/* Instruction de deplacement proposee par le modele dans sa reponse :
 * "reste", "approche", "recule", "piece:<id>", "rejoint:<id>". Le moteur
 * valide tout et ignore ce qui n'a pas de sens. */
typedef enum {
	NPC_MOVE_NOT_REQUESTED,
	NPC_MOVE_NO_OP,
	NPC_MOVE_ACCEPTED,
	NPC_MOVE_REJECTED
} NpcMoveResult;

NpcMoveResult npc_apply_move_order(struct s_game *game, int idx, const char *order);
const char   *npc_move_result_name(NpcMoveResult result);

/* What to call the character on screen: the real name once the player has
 * learned it, "un inconnu" until then. */
const char *npc_display_name(const NPC *npc);

/* True when `text` mentions any part of the name (title words like "Dr."
 * excluded, so "Dr. Karim Idrissi" is found on "Karim" or "Idrissi").
 * Accent-insensitive: the sheets are written without accents and the model
 * answers in real French. */
bool npc_name_in_text(const char *name, const char *text);
bool npc_text_reveals_name(const NPC *npc, const char *text);

/* Passe a la personne suivante presente dans la piece. */
void npc_cycle_target(struct s_game *game);

#endif
