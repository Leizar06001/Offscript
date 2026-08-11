#ifndef STORY_H
#define STORY_H

#include <stdbool.h>
#include <stddef.h>

/* Loaded, read-only representation of one story file (ressources/<id>/
 * <id>_story.json). This is the author's truth: nothing here changes during
 * play. Everything that evolves lives in the save (see memory.h). */

#define STORY_DIR "ressources"
#define SAVE_DIR  "saves"

typedef struct {
	char *id;        /* key in the story's "facts" object */
	char *text;
	char *category;
	bool  truth;
} Fact;

typedef struct {
	char  *id;
	char  *fact_id;
	bool   can_hide;
	char **reveal_conditions;
	int    nb_reveal_conditions;
} Secret;

typedef struct {
	char *from, *to, *type, *description;
} StoryRelationship;

typedef struct {
	char  *time, *event;
	char **fact_ids;
	int    nb_fact_ids;
} TimelineEntry;

typedef struct {
	char  *id, *name, *description;
	char **reveals_fact_ids;
	int    nb_reveals;
	bool   discoverable;
} Clue;

/* Une piece de la carte. `x`/`y` ne sont pas un coin mais un point quelconque
 * a l'interieur : le moteur remplit la zone depuis la (voir map.c), donc une
 * piece en L se decrit aussi simplement qu'un rectangle. */
typedef struct {
	char *id;
	char *name;
	int   x, y;
} StoryRoom;

/* La carte appartient a l'histoire : chaque enquete a son batiment. Les
 * caracteres sont ceux qu'attend le rendu ('1' et '2' pour deux hauteurs de
 * mur, 'v'/'h' pour une porte, l'espace pour le sol). */
typedef struct {
	int    w, h;
	char  *tiles;          /* w*h, sans separateur de ligne */
	int    start_x, start_y;

	StoryRoom *rooms;
	int        nb_rooms;
} StoryMap;

typedef struct {
	char  *id, *name, *role;

	/* "identity" is polymorphic: humans carry a numeric "age", Echo carries
	 * "age_or_existence" and a "model". Both are normalised to a display
	 * string here so the prompt builder needs no special case. */
	char  *identity_type;
	char  *identity_age;
	char  *identity_model;

	char **personality;
	int    nb_personality;

	char  *public_description;
	char  *position_in_case;

	char **motives;
	int    nb_motives;

	/* NULL when the character has no alibi at all ("alibi": null). */
	char  *alibi_claim, *alibi_strength, *alibi_weakness;

	Secret *secrets;
	int     nb_secrets;

	char **known_fact_ids;
	int    nb_known_facts;
	char **unknown_fact_ids;
	int    nb_unknown_facts;

	char  *dialogue_tone;
	char **speech_rules;
	int    nb_speech_rules;

	/* Optional "game" block: where the character stands and what it looks
	 * like. Absent means the character is not placed on the map. */
	bool  placed;
	int   x, y;
	int   face_id, body_id, legs_id;

	/* Placement par piece plutot que par coordonnees : l'auteur ecrit ou se
	 * trouve le personnage, le moteur lui trouve une place libre. `x`/`y`
	 * restent acceptes et l'emportent quand ils sont donnes. */
	char *room_id;
	bool  has_xy;

	/* Un personnage n'a pas forcement le droit de bouger (un androide
	 * desactive, quelqu'un assis a son poste). Le jeu peut lever ces
	 * autorisations en cours de partie. */
	bool  can_move;
	bool  can_change_room;

	/* Certains personnages n'ont pas de palette d'expressions credible (un
	 * androide n'a qu'un seul emoji possible) : leur visage ne doit alors
	 * jamais suivre l'emotion de la replique. */
	bool  fixed_face;
} StoryCharacter;

/* Tunables from the story's "memory_config" (Memory_instructions.md §21),
 * with the defaults below when the block is absent. */
typedef struct {
	int recent_message_limit;        /* 10 */
	int long_term_memory_limit;      /* 30 */
	int memories_in_prompt;          /* 5  */
	int minimum_importance_to_store; /* 3  */
	int memory_analysis_interval;    /* 4  */
} MemoryConfig;

typedef struct {
	char *schema_version;

	char  *id, *title, *language, *premise;
	char **genre;
	int    nb_genre;
	int    year;
	char  *location, *organization;
	char **central_questions;   /* used as the player's objectives */
	int    nb_central_questions;

	/* Comment on s'adresse au joueur dans cette histoire. Il ne saisit que
	 * son nom : l'histoire fournit le titre, et il devient « Detective
	 * Poireau », « Commissaire Poireau »... Defaut : "Detective". */
	char  *player_title;

	char **ai_global;           /* ai_directives.global */
	int    nb_ai_global;
	int    max_words;
	bool   stream;
	bool   allow_character_lies;

	Fact *facts;
	int   nb_facts;

	StoryCharacter *characters;
	int             nb_characters;

	StoryRelationship *relationships;
	int                nb_relationships;

	TimelineEntry *timeline;
	int            nb_timeline;

	Clue *clues;
	int   nb_clues;

	char *victim_id, *victim_name, *victim_role;
	int   victim_age;
	char *crime_type, *crime_location, *crime_cause;
	char *crime_time_start, *crime_time_end;
	char **suspect_ids;
	int    nb_suspects;

	char *solution_mode;
	char *culprit_id;        /* may be NULL: then drawn at new game */
	char *solution_explanation;
	bool  locked_until_end;

	MemoryConfig memory;

	/* Le batiment de l'enquete. has_map est faux quand le fichier n'a pas de
	 * bloc "map" : le moteur retombe alors sur la carte integree. */
	bool     has_map;
	StoryMap map;

	char *dir;   /* ressources/<id> */
	char *path;  /* the story file itself */
} Story;

/* One entry per story found on disk, for the launch menu. Cheap: only the
 * fields the menu shows are kept. */
typedef struct {
	char *id;
	char *title;
	char *premise;
	char *genre_line;   /* "enquête, science-fiction, thriller" */
	char *location;
	int   year;
	int   nb_characters;
	bool  has_save;
	char *path;
} StoryInfo;

/* Scans STORY_DIR for <dir>/<something>_story.json. Returns the number
 * found and fills *out with a malloc'd array (free with story_list_free). */
int  story_list(StoryInfo **out);
void story_list_free(StoryInfo *list, int count);

/* Loads a story file in full. Returns NULL on failure and, when `err` is
 * given, writes a human-readable reason into it (missing file, malformed
 * JSON, missing required field, dangling fact id...). */
Story *story_load(const char *path, char *err, size_t err_size);
void   story_free(Story *story);

const Fact           *story_fact(const Story *s, const char *fact_id);
const StoryCharacter *story_character(const Story *s, const char *char_id);
int                   story_character_index(const Story *s, const char *char_id);

#endif
