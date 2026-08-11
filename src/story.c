#include "story.h"
#include "json_min.h"
#include "memory.h"   /* save_exists : une histoire a-t-elle une partie en cours */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Chargement                                                          */
/* ------------------------------------------------------------------ */

static void load_secrets(StoryCharacter *c, JsonValue *arr) {
	c->nb_secrets = json_array_count(arr);
	if (c->nb_secrets <= 0) { c->nb_secrets = 0; return; }

	c->secrets = calloc((size_t)c->nb_secrets, sizeof(Secret));
	for (int i = 0; i < c->nb_secrets; i++) {
		JsonValue *s = json_array_get(arr, i);
		c->secrets[i].id       = json_strdup(json_object_get(s, "id"));
		c->secrets[i].fact_id  = json_strdup(json_object_get(s, "fact_id"));
		c->secrets[i].can_hide = json_bool_or(json_object_get(s, "can_hide"), 1);
		c->secrets[i].reveal_conditions =
			json_strdup_array(json_object_get(s, "reveal_conditions"),
			                  &c->secrets[i].nb_reveal_conditions);
	}
}

/* "identity" carries either a numeric age (humans) or a free-form
 * age_or_existence (Echo: "3 ans d'existence"). Both end up as one string. */
static void load_identity(StoryCharacter *c, JsonValue *id_obj) {
	c->identity_type  = json_strdup(json_object_get(id_obj, "type"));
	c->identity_model = json_strdup(json_object_get(id_obj, "model"));

	JsonValue *age_str = json_object_get(id_obj, "age_or_existence");
	if (json_string(age_str)) {
		c->identity_age = json_strdup(age_str);
		return;
	}

	JsonValue *age_num = json_object_get(id_obj, "age");
	int age = json_int_or(age_num, -1);
	if (age > 0) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%d ans", age);
		c->identity_age = strdup(buf);
	}
}

static void load_characters(Story *st, JsonValue *arr) {
	st->nb_characters = json_array_count(arr);
	if (st->nb_characters <= 0) { st->nb_characters = 0; return; }

	st->characters = calloc((size_t)st->nb_characters, sizeof(StoryCharacter));
	for (int i = 0; i < st->nb_characters; i++) {
		JsonValue *c = json_array_get(arr, i);
		StoryCharacter *out = &st->characters[i];

		out->id   = json_strdup(json_object_get(c, "id"));
		out->name = json_strdup(json_object_get(c, "name"));
		out->role = json_strdup(json_object_get(c, "role"));

		load_identity(out, json_object_get(c, "identity"));

		out->personality = json_strdup_array(json_object_get(c, "personality"),
		                                     &out->nb_personality);
		out->public_description = json_strdup(json_object_get(c, "public_description"));
		out->position_in_case   = json_strdup(json_object_get(c, "position_in_case"));
		out->motives = json_strdup_array(json_object_get(c, "motives"), &out->nb_motives);

		/* "alibi": null is legitimate (Echo has none): leave the fields NULL
		 * so the prompt builder can skip the whole section. */
		JsonValue *alibi = json_object_get(c, "alibi");
		out->alibi_claim    = json_strdup(json_object_get(alibi, "claim"));
		out->alibi_strength = json_strdup(json_object_get(alibi, "strength"));
		out->alibi_weakness = json_strdup(json_object_get(alibi, "weakness"));

		load_secrets(out, json_object_get(c, "secrets"));

		JsonValue *know = json_object_get(c, "knowledge");
		out->known_fact_ids = json_strdup_array(json_object_get(know, "known_fact_ids"),
		                                        &out->nb_known_facts);
		out->unknown_fact_ids = json_strdup_array(json_object_get(know, "unknown_fact_ids"),
		                                          &out->nb_unknown_facts);

		JsonValue *dlg = json_object_get(c, "dialogue");
		out->dialogue_tone = json_strdup(json_object_get(dlg, "tone"));
		out->speech_rules  = json_strdup_array(json_object_get(dlg, "speech_rules"),
		                                       &out->nb_speech_rules);

		/* Bloc "game": position et apparence sur la carte. */
		JsonValue *g = json_object_get(c, "game");
		if (g) {
			out->placed  = true;
			out->room_id = json_strdup(json_object_get(g, "room"));

			/* "x"/"y" restent acceptes et l'emportent sur la piece : c'est ce
			 * qui permet de poser quelqu'un a un endroit precis (devant une
			 * porte, a cote du corps) sans decrire une piece pour lui seul. */
			JsonValue *jx = json_object_get(g, "x");
			JsonValue *jy = json_object_get(g, "y");
			out->has_xy  = (jx != NULL && jy != NULL);
			out->x       = json_int_or(jx, 1);
			out->y       = json_int_or(jy, 1);

			out->face_id = json_int_or(json_object_get(g, "face"), 0);
			out->body_id = json_int_or(json_object_get(g, "body"), 0);
			out->legs_id = json_int_or(json_object_get(g, "legs"), 0);
			out->fixed_face = json_bool_or(json_object_get(g, "fixed_face"), 0);

			/* Par defaut un personnage se deplace dans sa piece mais n'en
			 * change pas : c'est le comportement le moins surprenant pour une
			 * histoire ecrite avant l'arrivee des deplacements. */
			out->can_move        = json_bool_or(json_object_get(g, "can_move"), 1);
			out->can_change_room = json_bool_or(json_object_get(g, "can_change_room"), 0);
		}
	}
}

static void load_facts(Story *st, JsonValue *obj) {
	st->nb_facts = json_object_count(obj);
	if (st->nb_facts <= 0) { st->nb_facts = 0; return; }

	st->facts = calloc((size_t)st->nb_facts, sizeof(Fact));
	for (int i = 0; i < st->nb_facts; i++) {
		JsonValue *f = json_object_value_at(obj, i);
		const char *key = json_object_key_at(obj, i);
		st->facts[i].id       = key ? strdup(key) : NULL;
		st->facts[i].text     = json_strdup(json_object_get(f, "text"));
		st->facts[i].category = json_strdup(json_object_get(f, "category"));
		st->facts[i].truth    = json_bool_or(json_object_get(f, "truth"), 1);
	}
}

static void load_timeline(Story *st, JsonValue *arr) {
	st->nb_timeline = json_array_count(arr);
	if (st->nb_timeline <= 0) { st->nb_timeline = 0; return; }

	st->timeline = calloc((size_t)st->nb_timeline, sizeof(TimelineEntry));
	for (int i = 0; i < st->nb_timeline; i++) {
		JsonValue *t = json_array_get(arr, i);
		st->timeline[i].time  = json_strdup(json_object_get(t, "time"));
		st->timeline[i].event = json_strdup(json_object_get(t, "event"));
		st->timeline[i].fact_ids = json_strdup_array(json_object_get(t, "fact_ids"),
		                                             &st->timeline[i].nb_fact_ids);
	}
}

static void load_clues(Story *st, JsonValue *arr) {
	st->nb_clues = json_array_count(arr);
	if (st->nb_clues <= 0) { st->nb_clues = 0; return; }

	st->clues = calloc((size_t)st->nb_clues, sizeof(Clue));
	for (int i = 0; i < st->nb_clues; i++) {
		JsonValue *c = json_array_get(arr, i);
		st->clues[i].id          = json_strdup(json_object_get(c, "id"));
		st->clues[i].name        = json_strdup(json_object_get(c, "name"));
		st->clues[i].description = json_strdup(json_object_get(c, "description"));
		st->clues[i].discoverable = json_bool_or(json_object_get(c, "discoverable"), 1);
		st->clues[i].reveals_fact_ids =
			json_strdup_array(json_object_get(c, "reveals_fact_ids"),
			                  &st->clues[i].nb_reveals);
	}
}

static void load_relationships(Story *st, JsonValue *arr) {
	st->nb_relationships = json_array_count(arr);
	if (st->nb_relationships <= 0) { st->nb_relationships = 0; return; }

	st->relationships = calloc((size_t)st->nb_relationships, sizeof(StoryRelationship));
	for (int i = 0; i < st->nb_relationships; i++) {
		JsonValue *r = json_array_get(arr, i);
		st->relationships[i].from        = json_strdup(json_object_get(r, "from"));
		st->relationships[i].to          = json_strdup(json_object_get(r, "to"));
		st->relationships[i].type        = json_strdup(json_object_get(r, "type"));
		st->relationships[i].description = json_strdup(json_object_get(r, "description"));
	}
}

/* Bloc "map". Les lignes sont ecrites telles qu'elles apparaissent a l'ecran,
 * mais rien n'oblige l'auteur a les completer jusqu'au bord : on prend la plus
 * longue comme largeur et on complete les autres avec du sol. Le rendu lit la
 * carte comme un rectangle plein, donc elle doit en etre un en memoire. */
static void load_map(Story *st, JsonValue *obj) {
	JsonValue *rows = json_object_get(obj, "rows");
	int nb_rows = json_array_count(rows);
	if (nb_rows <= 0) return;

	int w = json_int_or(json_object_get(obj, "width"), 0);
	for (int i = 0; i < nb_rows; i++) {
		const char *line = json_string(json_array_get(rows, i));
		int len = line ? (int)strlen(line) : 0;
		if (len > w) w = len;
	}
	if (w <= 0) return;

	st->map.w = w;
	st->map.h = nb_rows;
	st->map.tiles = malloc((size_t)w * (size_t)nb_rows + 1);
	for (int y = 0; y < nb_rows; y++) {
		const char *line = json_string(json_array_get(rows, y));
		int len = line ? (int)strlen(line) : 0;
		for (int x = 0; x < w; x++)
			st->map.tiles[y * w + x] = (x < len) ? line[x] : ' ';
	}
	st->map.tiles[(size_t)w * (size_t)nb_rows] = '\0';

	JsonValue *start = json_object_get(obj, "player_start");
	st->map.start_x = json_int_or(json_object_get(start, "x"), 1);
	st->map.start_y = json_int_or(json_object_get(start, "y"), 1);

	JsonValue *rooms = json_object_get(obj, "rooms");
	st->map.nb_rooms = json_array_count(rooms);
	if (st->map.nb_rooms > 0) {
		st->map.rooms = calloc((size_t)st->map.nb_rooms, sizeof(StoryRoom));
		for (int i = 0; i < st->map.nb_rooms; i++) {
			JsonValue *r = json_array_get(rooms, i);
			st->map.rooms[i].id   = json_strdup(json_object_get(r, "id"));
			st->map.rooms[i].name = json_strdup(json_object_get(r, "name"));
			JsonValue *at = json_object_get(r, "at");
			st->map.rooms[i].x = json_int_or(json_object_get(at, "x"), 1);
			st->map.rooms[i].y = json_int_or(json_object_get(at, "y"), 1);
		}
	} else {
		st->map.nb_rooms = 0;
	}

	st->has_map = true;
}

static void load_memory_config(Story *st, JsonValue *obj) {
	/* Defaults from Memory_instructions.md §21. */
	st->memory.recent_message_limit        = json_int_or(json_object_get(obj, "recent_message_limit"), 10);
	st->memory.long_term_memory_limit      = json_int_or(json_object_get(obj, "long_term_memory_limit"), 30);
	st->memory.memories_in_prompt          = json_int_or(json_object_get(obj, "memories_in_prompt"), 5);
	st->memory.minimum_importance_to_store = json_int_or(json_object_get(obj, "minimum_importance_to_store"), 3);
	st->memory.memory_analysis_interval    = json_int_or(json_object_get(obj, "memory_analysis_interval"), 4);
}

/* Story files are hand-written, so a silent half-loaded Story would surface
 * much later as blank prompts. Everything required is checked once here. */
static bool validate(const Story *st, char *err, size_t err_size) {
	if (!st->id || !*st->id)         { snprintf(err, err_size, "story.id manquant"); return false; }
	if (!st->title || !*st->title)   { snprintf(err, err_size, "story.title manquant"); return false; }
	if (!st->premise || !*st->premise){ snprintf(err, err_size, "story.premise manquant"); return false; }
	if (st->nb_characters <= 0)      { snprintf(err, err_size, "aucun personnage dans characters[]"); return false; }

	for (int i = 0; i < st->nb_characters; i++) {
		const StoryCharacter *c = &st->characters[i];
		if (!c->id || !*c->id) {
			snprintf(err, err_size, "characters[%d].id manquant", i);
			return false;
		}
		if (!c->name || !*c->name) {
			snprintf(err, err_size, "characters[%d] (%s): name manquant", i, c->id);
			return false;
		}
		/* A dangling fact id would silently drop knowledge from the prompt. */
		for (int k = 0; k < c->nb_known_facts; k++) {
			if (!story_fact(st, c->known_fact_ids[k])) {
				snprintf(err, err_size, "%s: known_fact_ids[%d] = \"%s\" introuvable dans facts",
				         c->id, k, c->known_fact_ids[k]);
				return false;
			}
		}
		for (int s = 0; s < c->nb_secrets; s++) {
			if (c->secrets[s].fact_id && !story_fact(st, c->secrets[s].fact_id)) {
				snprintf(err, err_size, "%s: secret \"%s\" pointe le fait introuvable \"%s\"",
				         c->id, c->secrets[s].id ? c->secrets[s].id : "?", c->secrets[s].fact_id);
				return false;
			}
		}
		/* Une piece inconnue laisserait le personnage sans place sur la carte,
		 * donc invisible et injoignable, sans rien signaler. */
		if (c->room_id && *c->room_id) {
			bool found = false;
			for (int r = 0; r < st->map.nb_rooms; r++) {
				if (st->map.rooms[r].id && strcmp(st->map.rooms[r].id, c->room_id) == 0) {
					found = true;
					break;
				}
			}
			if (!found) {
				snprintf(err, err_size, "%s: game.room = \"%s\" n'est pas une piece de map.rooms",
				         c->id, c->room_id);
				return false;
			}
		}
	}

	/* Deux pieces de meme identifiant rendraient game.room ambigu. */
	for (int i = 0; i < st->map.nb_rooms; i++) {
		if (!st->map.rooms[i].id || !*st->map.rooms[i].id) {
			snprintf(err, err_size, "map.rooms[%d].id manquant", i);
			return false;
		}
		for (int k = i + 1; k < st->map.nb_rooms; k++) {
			if (st->map.rooms[k].id && strcmp(st->map.rooms[i].id, st->map.rooms[k].id) == 0) {
				snprintf(err, err_size, "map.rooms: identifiant \"%s\" en double",
				         st->map.rooms[i].id);
				return false;
			}
		}
	}

	for (int i = 0; i < st->nb_suspects; i++) {
		if (!story_character(st, st->suspect_ids[i])) {
			snprintf(err, err_size, "mystery.suspect_ids[%d] = \"%s\" n'est pas un personnage",
			         i, st->suspect_ids[i]);
			return false;
		}
	}
	return true;
}

Story *story_load(const char *path, char *err, size_t err_size) {
	if (err && err_size) err[0] = '\0';

	JsonValue *root = json_parse_file(path);
	if (!root) {
		if (err) snprintf(err, err_size, "impossible de lire ou d'analyser %s", path);
		return NULL;
	}

	Story *st = calloc(1, sizeof(Story));
	st->path = strdup(path);

	/* dir = tout avant le dernier '/' */
	const char *slash = strrchr(path, '/');
	if (slash) {
		size_t n = (size_t)(slash - path);
		st->dir = malloc(n + 1);
		memcpy(st->dir, path, n);
		st->dir[n] = '\0';
	} else {
		st->dir = strdup(".");
	}

	st->schema_version = json_strdup(json_object_get(root, "schema_version"));

	JsonValue *s = json_object_get(root, "story");
	st->id       = json_strdup(json_object_get(s, "id"));
	st->title    = json_strdup(json_object_get(s, "title"));
	st->language = json_strdup(json_object_get(s, "language"));
	st->premise  = json_strdup(json_object_get(s, "premise"));
	st->genre    = json_strdup_array(json_object_get(s, "genre"), &st->nb_genre);
	st->central_questions = json_strdup_array(json_object_get(s, "central_questions"),
	                                          &st->nb_central_questions);
	st->player_title = json_strdup(json_object_get(s, "player_title"));
	if (!st->player_title || !*st->player_title) {
		free(st->player_title);
		st->player_title = strdup("Detective");
	}

	JsonValue *set = json_object_get(s, "setting");
	st->year         = json_int_or(json_object_get(set, "year"), 0);
	st->location     = json_strdup(json_object_get(set, "location"));
	st->organization = json_strdup(json_object_get(set, "organization"));

	JsonValue *ai = json_object_get(root, "ai_directives");
	st->ai_global = json_strdup_array(json_object_get(ai, "global"), &st->nb_ai_global);
	JsonValue *dd = json_object_get(ai, "default_dialogue");
	st->max_words            = json_int_or(json_object_get(dd, "max_words"), 90);
	st->stream               = json_bool_or(json_object_get(dd, "stream"), 1);
	st->allow_character_lies = json_bool_or(json_object_get(dd, "allow_character_lies"), 1);

	load_facts(st, json_object_get(root, "facts"));
	load_characters(st, json_object_get(root, "characters"));
	load_relationships(st, json_object_get(root, "relationships"));
	load_timeline(st, json_object_get(root, "timeline"));
	load_clues(st, json_object_get(root, "clues"));
	load_map(st, json_object_get(root, "map"));
	load_memory_config(st, json_object_get(root, "memory_config"));

	JsonValue *m = json_object_get(root, "mystery");
	JsonValue *v = json_object_get(m, "victim");
	st->victim_id   = json_strdup(json_object_get(v, "id"));
	st->victim_name = json_strdup(json_object_get(v, "name"));
	st->victim_role = json_strdup(json_object_get(v, "role"));
	st->victim_age  = json_int_or(json_object_get(v, "age"), 0);

	JsonValue *cr = json_object_get(m, "crime");
	st->crime_type     = json_strdup(json_object_get(cr, "type"));
	st->crime_location = json_strdup(json_object_get(cr, "location"));
	st->crime_cause    = json_strdup(json_object_get(cr, "cause"));
	JsonValue *tw = json_object_get(cr, "time_window");
	st->crime_time_start = json_strdup(json_object_get(tw, "start"));
	st->crime_time_end   = json_strdup(json_object_get(tw, "end"));

	st->suspect_ids = json_strdup_array(json_object_get(m, "suspect_ids"), &st->nb_suspects);

	JsonValue *sol = json_object_get(m, "solution");
	st->solution_mode         = json_strdup(json_object_get(sol, "mode"));
	st->culprit_id            = json_strdup(json_object_get(sol, "culprit_id"));
	st->solution_explanation  = json_strdup(json_object_get(sol, "explanation"));
	st->locked_until_end      = json_bool_or(json_object_get(sol, "locked_until_end"), 1);

	json_free(root);

	char local_err[256];
	if (!validate(st, local_err, sizeof(local_err))) {
		if (err) snprintf(err, err_size, "%s: %s", path, local_err);
		story_free(st);
		return NULL;
	}
	return st;
}

/* ------------------------------------------------------------------ */
/* Recherches                                                          */
/* ------------------------------------------------------------------ */

const Fact *story_fact(const Story *s, const char *fact_id) {
	if (!s || !fact_id) return NULL;
	for (int i = 0; i < s->nb_facts; i++) {
		if (s->facts[i].id && strcmp(s->facts[i].id, fact_id) == 0) return &s->facts[i];
	}
	return NULL;
}

int story_character_index(const Story *s, const char *char_id) {
	if (!s || !char_id) return -1;
	for (int i = 0; i < s->nb_characters; i++) {
		if (s->characters[i].id && strcmp(s->characters[i].id, char_id) == 0) return i;
	}
	return -1;
}

const StoryCharacter *story_character(const Story *s, const char *char_id) {
	int i = story_character_index(s, char_id);
	return i < 0 ? NULL : &s->characters[i];
}

/* ------------------------------------------------------------------ */
/* Liberation                                                          */
/* ------------------------------------------------------------------ */

void story_free(Story *st) {
	if (!st) return;

	free(st->schema_version);
	free(st->id); free(st->title); free(st->language); free(st->premise);
	json_free_string_array(st->genre, st->nb_genre);
	json_free_string_array(st->central_questions, st->nb_central_questions);
	free(st->player_title);
	free(st->location); free(st->organization);
	json_free_string_array(st->ai_global, st->nb_ai_global);

	for (int i = 0; i < st->nb_facts; i++) {
		free(st->facts[i].id); free(st->facts[i].text); free(st->facts[i].category);
	}
	free(st->facts);

	for (int i = 0; i < st->nb_characters; i++) {
		StoryCharacter *c = &st->characters[i];
		free(c->id); free(c->name); free(c->role);
		free(c->identity_type); free(c->identity_age); free(c->identity_model);
		json_free_string_array(c->personality, c->nb_personality);
		free(c->public_description); free(c->position_in_case);
		json_free_string_array(c->motives, c->nb_motives);
		free(c->alibi_claim); free(c->alibi_strength); free(c->alibi_weakness);
		for (int s = 0; s < c->nb_secrets; s++) {
			free(c->secrets[s].id); free(c->secrets[s].fact_id);
			json_free_string_array(c->secrets[s].reveal_conditions,
			                       c->secrets[s].nb_reveal_conditions);
		}
		free(c->secrets);
		json_free_string_array(c->known_fact_ids, c->nb_known_facts);
		json_free_string_array(c->unknown_fact_ids, c->nb_unknown_facts);
		free(c->dialogue_tone);
		free(c->room_id);
		json_free_string_array(c->speech_rules, c->nb_speech_rules);
	}
	free(st->characters);

	free(st->map.tiles);
	for (int i = 0; i < st->map.nb_rooms; i++) {
		free(st->map.rooms[i].id);
		free(st->map.rooms[i].name);
	}
	free(st->map.rooms);

	for (int i = 0; i < st->nb_relationships; i++) {
		free(st->relationships[i].from); free(st->relationships[i].to);
		free(st->relationships[i].type); free(st->relationships[i].description);
	}
	free(st->relationships);

	for (int i = 0; i < st->nb_timeline; i++) {
		free(st->timeline[i].time); free(st->timeline[i].event);
		json_free_string_array(st->timeline[i].fact_ids, st->timeline[i].nb_fact_ids);
	}
	free(st->timeline);

	for (int i = 0; i < st->nb_clues; i++) {
		free(st->clues[i].id); free(st->clues[i].name); free(st->clues[i].description);
		json_free_string_array(st->clues[i].reveals_fact_ids, st->clues[i].nb_reveals);
	}
	free(st->clues);

	free(st->victim_id); free(st->victim_name); free(st->victim_role);
	free(st->crime_type); free(st->crime_location); free(st->crime_cause);
	free(st->crime_time_start); free(st->crime_time_end);
	json_free_string_array(st->suspect_ids, st->nb_suspects);
	free(st->solution_mode); free(st->culprit_id); free(st->solution_explanation);

	free(st->dir); free(st->path);
	free(st);
}

/* ------------------------------------------------------------------ */
/* Liste des histoires disponibles                                     */
/* ------------------------------------------------------------------ */

static char *join_genres(char **genre, int n) {
	if (n <= 0) return strdup("");
	size_t len = 1;
	for (int i = 0; i < n; i++) len += strlen(genre[i]) + 2;

	char *out = malloc(len);
	out[0] = '\0';
	for (int i = 0; i < n; i++) {
		if (i) strcat(out, ", ");
		strcat(out, genre[i]);
	}
	return out;
}

/* Trouve le fichier se terminant par "_story.json" dans un dossier, sans
 * imposer que son nom corresponde a l'identifiant du dossier. */
static char *find_story_file(const char *dir_path) {
	DIR *d = opendir(dir_path);
	if (!d) return NULL;

	char *found = NULL;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		const char *suffix = "_story.json";
		size_t nl = strlen(e->d_name), sl = strlen(suffix);
		if (nl > sl && strcmp(e->d_name + nl - sl, suffix) == 0) {
			size_t need = strlen(dir_path) + 1 + nl + 1;
			found = malloc(need);
			snprintf(found, need, "%s/%s", dir_path, e->d_name);
			break;
		}
	}
	closedir(d);
	return found;
}

int story_list(StoryInfo **out) {
	*out = NULL;

	DIR *d = opendir(STORY_DIR);
	if (!d) return 0;

	StoryInfo *list = NULL;
	int count = 0;

	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;

		char dir_path[512];
		snprintf(dir_path, sizeof(dir_path), "%s/%s", STORY_DIR, e->d_name);

		struct stat sb;
		if (stat(dir_path, &sb) != 0 || !S_ISDIR(sb.st_mode)) continue;

		char *file = find_story_file(dir_path);
		if (!file) continue;

		/* Le menu n'a besoin que de l'entete: on charge, on copie, on libere. */
		char err[256];
		Story *st = story_load(file, err, sizeof(err));
		if (!st) { free(file); continue; }

		list = realloc(list, sizeof(StoryInfo) * (size_t)(count + 1));
		StoryInfo *info = &list[count++];
		info->id            = strdup(st->id);
		info->title         = strdup(st->title);
		info->premise       = strdup(st->premise);
		info->genre_line    = join_genres(st->genre, st->nb_genre);
		info->location      = st->location ? strdup(st->location) : strdup("");
		info->year          = st->year;
		info->nb_characters = st->nb_characters;
		info->path          = file;
		/* Un culprit_id ecrit dans l'histoire fige la reponse ; NULL veut dire
		 * qu'elle est tiree au sort a chaque nouvelle partie. */
		info->fixed_culprit = (st->culprit_id && *st->culprit_id);

		/* N'importe quelle partie compte, pas seulement un ancien "save.json" :
		 * les sauvegardes portent maintenant le nom du joueur. */
		info->has_save = save_exists(st->id);

		story_free(st);
	}
	closedir(d);

	*out = list;
	return count;
}

void story_list_free(StoryInfo *list, int count) {
	if (!list) return;
	for (int i = 0; i < count; i++) {
		free(list[i].id); free(list[i].title); free(list[i].premise);
		free(list[i].genre_line); free(list[i].location); free(list[i].path);
	}
	free(list);
}
