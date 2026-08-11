#ifndef MEMORY_H
#define MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#include "story.h"

/* Everything that evolves during a game, persisted to saves/<story_id>/
 * save.json. Implements Memory_instructions.md: the story file holds the
 * author's truth, this holds only what changed.
 *
 * The engine owns the truth (facts, culprit, discovered clues); the model
 * only ever proposes a reply, a memory and a relationship nudge. */

typedef struct {
	int trust;      /* -100..100 */
	int affection;
	int fear;
	int suspicion;
} Relation;

/* Un tour de conversation. Pour un tour "assistant", emotion, action et move sont
 * conserves en plus de la replique : ils servent au journal, mais surtout a
 * rejouer l'historique vers le modele sous la forme JSON exacte qu'on lui
 * demande. Sans cela il voit des tours en prose et cesse de respecter le
 * format au bout de quelques echanges. */
typedef struct {
	char *role;     /* "user" | "assistant" */
	char *content;  /* la replique dite a voix haute */
	char *emotion;  /* NULL pour un tour user */
	char *action;   /* NULL pour un tour user */
	char *move;     /* NULL pour un tour user ou un personnage immobile */
} ChatMsg;

typedef struct {
	char  *id;      /* mem_001... */
	char  *type;    /* interaction | event | information | relationship */
	char  *summary;
	int    importance;          /* 1..5 */
	int    emotion;             /* -5..5 */
	char **tags;
	int    nb_tags;
	long   created_at;
	long   last_recalled_at;    /* -1 = jamais rappele */
} Memory;

typedef struct {
	char    *npc_id;
	Relation rel;

	ChatMsg *recent;
	int      nb_recent;

	Memory  *mems;
	int      nb_mems;

	char   **revealed_secret_ids;
	int      nb_revealed;

	/* Compte les echanges depuis la derniere analyse memoire, pour ne pas
	 * appeler le LLM d'analyse a chaque phrase (§12). */
	int exchanges_since_analysis;
	int next_mem_seq;

	/* Etat du personnage dans le monde. Sans lui, un personnage qui avait
	 * change de piece, donne son nom ou recu le droit de bouger revenait a sa
	 * position d'origine au rechargement, alors que ses souvenirs de la
	 * conversation, eux, etaient bien conserves.
	 *
	 * x vaut -1 quand la sauvegarde ne contient pas encore cette information
	 * (partie commencee avant) : le moteur garde alors le placement de
	 * l'histoire. */
	int  x, y;
	int  face_id;
	bool met;
	bool name_known;
	bool can_move, can_change_room;
	bool has_world;
} NpcState;

typedef struct {
	int   save_version;
	char *story_id;
	long  game_time;

	char  *player_name;
	/* Ou le joueur s'est arrete. -1 = absent de la sauvegarde, on repart du
	 * point de depart de la carte. */
	int    player_x, player_y;

	/* Le mystere a ete resolu : la partie est terminee, on ne la reprend pas
	 * en plein interrogatoire du coupable. */
	bool   solved;

	/* Consommation cumulee de l'API sur cette partie, toutes sessions
	 * confondues : le compteur affiche doit repartir d'ou il s'etait arrete. */
	long   api_prompt_tokens;
	long   api_completion_tokens;
	long   api_calls;

	char **known_fact_ids;      /* ce que le JOUEUR a appris */
	int    nb_known_facts;
	char **discovered_clue_ids;
	int    nb_discovered_clues;

	/* Tire au sort a la premiere partie quand l'histoire ne le fixe pas.
	 * Le moteur seul en decide : le LLM ne peut jamais le changer. */
	char *culprit_id;

	/* Le tirage seul ne suffit pas : les faits de l'histoire sont ecrits pour
	 * rester ambigus, donc une resolution est generee une fois au debut de la
	 * partie, coherente avec ces faits, puis figee dans la sauvegarde.
	 *
	 * culprit_solution : la verite, pour la revelation finale.
	 * culprit_brief    : ce que le coupable sait devoir cacher, injecte dans
	 *                    son prompt pour qu'il mente de facon coherente au
	 *                    lieu d'improviser.
	 * culprit_fact_ids : les faits de l'histoire qui l'incriminent. */
	char  *culprit_solution;
	char  *culprit_brief;
	char **culprit_fact_ids;
	int    nb_culprit_facts;

	NpcState *npcs;
	int       nb_npcs;
} SaveState;

/* Chemin d'une sauvegarde (saves/<story_id>/<slug>.json) et creation du
 * dossier au besoin. `slug` vient du nom du joueur : plusieurs parties de la
 * meme histoire coexistent, une par enqueteur. */
void save_path_for(const char *story_id, const char *slug, char *out, size_t out_size);
bool save_exists(const char *story_id);

/* Nom de fichier sur pour un nom de joueur : minuscules, sans accent, sans
 * separateur de chemin. Un nom vide donne "partie". */
void save_slug_from_name(const char *name, char *out, size_t out_size);

/* Une partie trouvee sur le disque, pour le menu de reprise. */
typedef struct {
	char *path;
	char *player_name;
	long  game_time;
	bool  solved;
} SaveInfo;

/* Toutes les parties enregistrees pour une histoire, les plus avancees en
 * premier. Renvoie leur nombre. */
int  save_list(const char *story_id, SaveInfo **out);
void save_list_free(SaveInfo *list, int count);

/* Nouvelle partie: un etat vierge pour chaque personnage. Tire le coupable
 * parmi mystery.suspect_ids si l'histoire laisse culprit_id a null. */
SaveState *save_new(const Story *story, const char *player_name);

/* Charge une sauvegarde. Les personnages absents du fichier sont ajoutes
 * vierges, ce qui permet d'enrichir une histoire sans casser les parties
 * en cours. Renvoie NULL si le fichier est illisible. */
SaveState *save_load(const Story *story, const char *path);
bool       save_write(const SaveState *st, const char *path);
void       save_free(SaveState *st);

NpcState *memory_get_npc(SaveState *st, const char *npc_id);

/* emotion et action ne concernent que les tours "assistant" ; passer NULL
 * pour un tour "user". Les tours sont retires par paires quand la limite est
 * depassee, pour ne jamais casser l'alternance user/assistant. */
void memory_add_recent_message(SaveState *st, const char *npc_id,
                               const char *role, const char *content,
                               const char *emotion, const char *action, const char *move,
                               int recent_limit);

void memory_add(SaveState *st, const char *npc_id, const char *type,
                const char *summary, int importance, int emotion,
                const char **tags, int nb_tags, long game_time);

void memory_update_relationship(SaveState *st, const char *npc_id,
                                int trust_d, int affection_d,
                                int fear_d, int suspicion_d);

/* Selectionne les souvenirs les plus pertinents (§9) : importance, tags
 * correspondant au message, emotion, recence. Ecrit au plus max_results
 * pointeurs dans out et renvoie leur nombre. Marque les souvenirs
 * retenus comme rappeles (§15). */
int memory_find_relevant(SaveState *st, const char *npc_id,
                         const char *player_message,
                         const Memory **out, int max_results, long game_time);

/* Garde le nombre de souvenirs sous la limite (§14) : jamais un souvenir
 * d'importance >= 4 tant qu'un moins precieux existe. */
void memory_cleanup(SaveState *st, const char *npc_id, int limit);

/* Secrets deja laches par le personnage. */
bool memory_secret_revealed(SaveState *st, const char *npc_id, const char *secret_id);
void memory_reveal_secret(SaveState *st, const char *npc_id, const char *secret_id);

/* Faits/indices decouverts par le joueur (alimentent le journal). Renvoie
 * true si c'est une nouveaute. */
bool memory_player_learn_fact(SaveState *st, const char *fact_id);
bool memory_player_knows_fact(const SaveState *st, const char *fact_id);
bool memory_player_discover_clue(SaveState *st, const char *clue_id);

/* Traduit une valeur -100..100 en mot ("elevee", "faible"...) : le spec
 * (§2) demande de ne jamais envoyer les chiffres bruts au modele. */
const char *memory_relation_word(int value);

#endif
