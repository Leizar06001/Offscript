#include "memory.h"
#include "json_min.h"
#include "textutil.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Chemins                                                             */
/* ------------------------------------------------------------------ */

/* Le nom saisi devient un nom de fichier : il ne doit contenir ni separateur
 * de chemin ni « .. », sinon on ecrirait n'importe ou sur le disque. On ne
 * garde donc que des lettres, des chiffres et des tirets. */
void save_slug_from_name(const char *name, char *out, size_t out_size) {
	char folded[128];
	text_fold_ascii(folded, sizeof(folded), name ? name : "");

	size_t o = 0;
	bool last_sep = true;   /* evite un tiret en tete */
	for (size_t i = 0; folded[i] && o + 1 < out_size && o < 40; i++) {
		unsigned char c = (unsigned char)folded[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			out[o++] = (char)c;
			last_sep = false;
		} else if (!last_sep) {
			out[o++] = '-';
			last_sep = true;
		}
	}
	while (o > 0 && out[o - 1] == '-') o--;   /* ni tiret en queue */
	out[o] = '\0';
	if (o == 0) snprintf(out, out_size, "partie");
}

void save_path_for(const char *story_id, const char *slug, char *out, size_t out_size) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s/%s", SAVE_DIR, story_id);
	mkdir(SAVE_DIR, 0755);
	mkdir(dir, 0755);
	snprintf(out, out_size, "%s/%s.json", dir, (slug && *slug) ? slug : "save");
}

bool save_exists(const char *story_id) {
	SaveInfo *list = NULL;
	int n = save_list(story_id, &list);
	save_list_free(list, n);
	return n > 0;
}

/* Lit juste ce que le menu affiche, sans construire tout un SaveState. */
static bool save_peek(const char *path, SaveInfo *info) {
	JsonValue *root = json_parse_file(path);
	if (!root) return false;

	JsonValue *pl = json_object_get(root, "player");
	const char *name = json_string(json_object_get(pl, "name"));
	info->path        = strdup(path);
	info->player_name = strdup(name && *name ? name : "Sans nom");
	info->game_time   = (long)json_number_or(json_object_get(root, "game_time"), 0);
	info->solved      = json_bool_or(json_object_get(root, "solved"), 0);

	json_free(root);
	return true;
}

int save_list(const char *story_id, SaveInfo **out) {
	*out = NULL;

	char dir_path[512];
	snprintf(dir_path, sizeof(dir_path), "%s/%s", SAVE_DIR, story_id);

	DIR *d = opendir(dir_path);
	if (!d) return 0;

	SaveInfo *list = NULL;
	int count = 0;

	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		size_t nl = strlen(e->d_name);
		if (nl < 6 || strcmp(e->d_name + nl - 5, ".json") != 0) continue;
		if (e->d_name[0] == '.') continue;

		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", dir_path, e->d_name);

		SaveInfo info;
		memset(&info, 0, sizeof(info));
		if (!save_peek(path, &info)) continue;

		list = realloc(list, sizeof(SaveInfo) * (size_t)(count + 1));
		list[count++] = info;
	}
	closedir(d);

	/* La partie la plus avancee en premier : c'est celle qu'on veut reprendre. */
	for (int i = 1; i < count; i++) {
		SaveInfo tmp = list[i];
		int k = i - 1;
		while (k >= 0 && list[k].game_time < tmp.game_time) {
			list[k + 1] = list[k];
			k--;
		}
		list[k + 1] = tmp;
	}

	*out = list;
	return count;
}

void save_list_free(SaveInfo *list, int count) {
	if (!list) return;
	for (int i = 0; i < count; i++) {
		free(list[i].path);
		free(list[i].player_name);
	}
	free(list);
}

/* ------------------------------------------------------------------ */
/* Creation / liberation                                               */
/* ------------------------------------------------------------------ */

/* Un entier dans [0, n). La graine est posee une seule fois : re-seeder a
 * chaque appel redemarrerait la meme suite, et deux parties lancees dans la
 * meme seconde tireraient le meme coupable. /dev/urandom quand il est
 * disponible, sinon l'horloge melangee au pid. */
static int random_below(int n) {
	static bool seeded = false;
	if (!seeded) {
		unsigned seed = 0;
		FILE *ur = fopen("/dev/urandom", "rb");
		if (ur) {
			if (fread(&seed, sizeof(seed), 1, ur) != 1) seed = 0;
			fclose(ur);
		}
		if (seed == 0) seed = (unsigned)time(NULL) ^ ((unsigned)getpid() << 16);
		srand(seed);
		seeded = true;
	}
	if (n <= 1) return 0;
	return rand() % n;
}

static void npc_state_init(NpcState *n, const char *npc_id) {
	memset(n, 0, sizeof(*n));
	n->npc_id = strdup(npc_id);
	n->next_mem_seq = 1;
	/* -1 : rien d'enregistre, le moteur garde le placement de l'histoire. */
	n->x = n->y = -1;
	n->face_id = -1;
}

SaveState *save_new(const Story *story, const char *player_name) {
	SaveState *st = calloc(1, sizeof(SaveState));
	st->save_version = 1;
	st->story_id     = strdup(story->id);
	st->game_time    = 0;
	st->player_name  = strdup(player_name ? player_name : "Detective");
	st->player_x     = -1;
	st->player_y     = -1;

	st->nb_npcs = story->nb_characters;
	st->npcs    = calloc((size_t)st->nb_npcs, sizeof(NpcState));
	for (int i = 0; i < st->nb_npcs; i++) {
		npc_state_init(&st->npcs[i], story->characters[i].id);
	}

	/* Le coupable : fixe par l'auteur s'il l'a renseigne, sinon tire au sort
	 * une fois pour toute la partie et conserve dans la sauvegarde. */
	if (story->culprit_id && *story->culprit_id) {
		st->culprit_id = strdup(story->culprit_id);
	} else if (story->nb_suspects > 0) {
		st->culprit_id = strdup(story->suspect_ids[random_below(story->nb_suspects)]);
	}
	return st;
}

static void free_memory_entry(Memory *m) {
	free(m->id); free(m->type); free(m->summary);
	json_free_string_array(m->tags, m->nb_tags);
}

static void free_npc_state(NpcState *n) {
	free(n->npc_id);
	for (int i = 0; i < n->nb_recent; i++) {
		free(n->recent[i].role);
		free(n->recent[i].content);
		free(n->recent[i].emotion);
		free(n->recent[i].action);
		free(n->recent[i].move);
	}
	free(n->recent);
	for (int i = 0; i < n->nb_mems; i++) free_memory_entry(&n->mems[i]);
	free(n->mems);
	json_free_string_array(n->revealed_secret_ids, n->nb_revealed);
}

void save_free(SaveState *st) {
	if (!st) return;
	free(st->story_id);
	free(st->player_name);
	json_free_string_array(st->known_fact_ids, st->nb_known_facts);
	json_free_string_array(st->discovered_clue_ids, st->nb_discovered_clues);
	free(st->culprit_id);
	free(st->culprit_solution);
	free(st->culprit_brief);
	json_free_string_array(st->culprit_fact_ids, st->nb_culprit_facts);
	for (int i = 0; i < st->nb_gen_clues; i++) {
		free(st->gen_clues[i].id);
		free(st->gen_clues[i].name);
		free(st->gen_clues[i].description);
		json_free_string_array(st->gen_clues[i].reveals_fact_ids, st->gen_clues[i].nb_reveals);
	}
	free(st->gen_clues);
	for (int i = 0; i < st->nb_extra_knowledge; i++) {
		free(st->extra_knowledge[i].npc_id);
		free(st->extra_knowledge[i].fact_id);
	}
	free(st->extra_knowledge);
	for (int i = 0; i < st->nb_npcs; i++) free_npc_state(&st->npcs[i]);
	free(st->npcs);
	free(st);
}

/* ------------------------------------------------------------------ */
/* Acces                                                              */
/* ------------------------------------------------------------------ */

NpcState *memory_get_npc(SaveState *st, const char *npc_id) {
	if (!st || !npc_id) return NULL;
	for (int i = 0; i < st->nb_npcs; i++) {
		if (st->npcs[i].npc_id && strcmp(st->npcs[i].npc_id, npc_id) == 0) return &st->npcs[i];
	}
	return NULL;
}

static int clamp100(int v) {
	if (v >  100) return  100;
	if (v < -100) return -100;
	return v;
}

void memory_update_relationship(SaveState *st, const char *npc_id,
                                int trust_d, int affection_d,
                                int fear_d, int suspicion_d) {
	NpcState *n = memory_get_npc(st, npc_id);
	if (!n) return;
	n->rel.trust     = clamp100(n->rel.trust + trust_d);
	n->rel.affection = clamp100(n->rel.affection + affection_d);
	n->rel.fear      = clamp100(n->rel.fear + fear_d);
	n->rel.suspicion = clamp100(n->rel.suspicion + suspicion_d);
}

const char *memory_relation_word(int value) {
	if (value <= -60) return "hostile";
	if (value <= -20) return "negative";
	if (value <   20) return "neutre";
	if (value <   60) return "moyenne";
	return "elevee";
}

/* ------------------------------------------------------------------ */
/* Messages recents                                                    */
/* ------------------------------------------------------------------ */

void memory_add_recent_message(SaveState *st, const char *npc_id,
                               const char *role, const char *content,
                               const char *emotion, const char *action, const char *move,
                               int recent_limit) {
	NpcState *n = memory_get_npc(st, npc_id);
	if (!n || !role || !content) return;

	n->recent = realloc(n->recent, sizeof(ChatMsg) * (size_t)(n->nb_recent + 1));
	n->recent[n->nb_recent].role    = strdup(role);
	n->recent[n->nb_recent].content = strdup(content);
	n->recent[n->nb_recent].emotion = emotion ? strdup(emotion) : NULL;
	n->recent[n->nb_recent].action  = action  ? strdup(action)  : NULL;
	n->recent[n->nb_recent].move    = move    ? strdup(move)    : NULL;
	n->nb_recent++;

	/* Au-dela de la limite, on jette les plus anciens (§3). Par paires, pour
	 * ne jamais laisser un tour assistant sans le tour user qui l'a
	 * provoque : l'alternance user/assistant doit rester valide. */
	if (recent_limit < 2) recent_limit = 2;
	if (n->nb_recent > recent_limit) {
		int drop = n->nb_recent - recent_limit;
		if (drop % 2) drop++;                 /* toujours par paires */
		if (drop > n->nb_recent) drop = n->nb_recent;

		for (int i = 0; i < drop; i++) {
			free(n->recent[i].role);
			free(n->recent[i].content);
			free(n->recent[i].emotion);
			free(n->recent[i].action);
			free(n->recent[i].move);
		}
		memmove(n->recent, n->recent + drop, sizeof(ChatMsg) * (size_t)(n->nb_recent - drop));
		n->nb_recent -= drop;
	}
}

/* ------------------------------------------------------------------ */
/* Souvenirs long terme                                                */
/* ------------------------------------------------------------------ */

void memory_add(SaveState *st, const char *npc_id, const char *type,
                const char *summary, int importance, int emotion,
                const char **tags, int nb_tags, long game_time) {
	NpcState *n = memory_get_npc(st, npc_id);
	if (!n || !summary || !*summary) return;

	if (importance < 1) importance = 1;
	if (importance > 5) importance = 5;
	if (emotion < -5) emotion = -5;
	if (emotion >  5) emotion =  5;

	n->mems = realloc(n->mems, sizeof(Memory) * (size_t)(n->nb_mems + 1));
	Memory *m = &n->mems[n->nb_mems++];
	memset(m, 0, sizeof(*m));

	char id[32];
	snprintf(id, sizeof(id), "mem_%03d", n->next_mem_seq++);
	m->id         = strdup(id);
	m->type       = strdup(type && *type ? type : "interaction");
	m->summary    = strdup(summary);
	m->importance = importance;
	m->emotion    = emotion;
	m->created_at = game_time;
	m->last_recalled_at = -1;

	if (nb_tags > 0 && tags) {
		m->tags = calloc((size_t)nb_tags, sizeof(char *));
		for (int i = 0; i < nb_tags; i++) {
			if (tags[i]) m->tags[m->nb_tags++] = strdup(tags[i]);
		}
	}
}

/* Score de pertinence (§9): importance*3 + correspondance_tags*5
 * + |emotion| + petit bonus de recence. */
static int relevance_score(const Memory *m, const char *folded_msg, long game_time) {
	int tag_hits = 0;
	if (folded_msg && *folded_msg) {
		char folded_tag[128];
		for (int t = 0; t < m->nb_tags; t++) {
			text_fold_ascii(folded_tag, sizeof(folded_tag), m->tags[t]);
			if (*folded_tag && strstr(folded_msg, folded_tag)) tag_hits++;
		}
	}

	int score = m->importance * 3 + tag_hits * 5;
	score += m->emotion < 0 ? -m->emotion : m->emotion;

	/* Recence: un souvenir des 500 derniers ticks vaut un point de plus. */
	if (game_time - m->created_at < 500) score += 1;
	if (m->last_recalled_at >= 0) score += 1;   /* §15: souvent rappele = plus durable */
	return score;
}

int memory_find_relevant(SaveState *st, const char *npc_id,
                         const char *player_message,
                         const Memory **out, int max_results, long game_time) {
	NpcState *n = memory_get_npc(st, npc_id);
	if (!n || n->nb_mems == 0 || max_results <= 0) return 0;

	char folded_msg[2048];
	text_fold_ascii(folded_msg, sizeof(folded_msg), player_message);

	int *score = malloc(sizeof(int) * (size_t)n->nb_mems);
	for (int i = 0; i < n->nb_mems; i++) {
		score[i] = relevance_score(&n->mems[i], folded_msg, game_time);
	}

	int count = 0;
	bool *taken = calloc((size_t)n->nb_mems, sizeof(bool));
	while (count < max_results) {
		int best = -1;
		for (int i = 0; i < n->nb_mems; i++) {
			if (taken[i]) continue;
			if (best < 0 || score[i] > score[best]) best = i;
		}
		if (best < 0) break;
		taken[best] = true;
		out[count++] = &n->mems[best];
		n->mems[best].last_recalled_at = game_time;   /* §15 */
	}

	free(taken);
	free(score);
	return count;
}

/* Score de conservation (§14) : importance*10 + |emotion|*2 + bonus. */
static int keep_score(const Memory *m, long game_time) {
	int s = m->importance * 10 + (m->emotion < 0 ? -m->emotion : m->emotion) * 2;
	if (game_time - m->created_at < 1000) s += 3;
	if (m->last_recalled_at >= 0) s += 2;
	return s;
}

void memory_cleanup(SaveState *st, const char *npc_id, int limit) {
	NpcState *n = memory_get_npc(st, npc_id);
	if (!n || limit <= 0) return;

	while (n->nb_mems > limit) {
		/* Les souvenirs majeurs (importance >= 4) ne partent jamais tant
		 * qu'il reste un souvenir plus banal a sacrifier. */
		int victim = -1;
		for (int i = 0; i < n->nb_mems; i++) {
			if (n->mems[i].importance >= 4) continue;
			if (victim < 0 || keep_score(&n->mems[i], st->game_time) <
			                  keep_score(&n->mems[victim], st->game_time)) victim = i;
		}
		if (victim < 0) {
			/* Que des souvenirs majeurs: on sacrifie le plus faible d'entre eux. */
			for (int i = 0; i < n->nb_mems; i++) {
				if (victim < 0 || keep_score(&n->mems[i], st->game_time) <
				                  keep_score(&n->mems[victim], st->game_time)) victim = i;
			}
		}
		if (victim < 0) break;

		free_memory_entry(&n->mems[victim]);
		memmove(&n->mems[victim], &n->mems[victim + 1],
		        sizeof(Memory) * (size_t)(n->nb_mems - victim - 1));
		n->nb_mems--;
	}
}

/* ------------------------------------------------------------------ */
/* Secrets et decouvertes du joueur                                    */
/* ------------------------------------------------------------------ */

static bool str_array_has(char **arr, int n, const char *needle) {
	if (!needle) return false;
	for (int i = 0; i < n; i++) {
		if (arr[i] && strcmp(arr[i], needle) == 0) return true;
	}
	return false;
}

static bool str_array_add_unique(char ***arr, int *n, const char *value) {
	if (!value || !*value) return false;
	if (str_array_has(*arr, *n, value)) return false;
	*arr = realloc(*arr, sizeof(char *) * (size_t)(*n + 1));
	(*arr)[(*n)++] = strdup(value);
	return true;
}

/* ------------------------------------------------------------------ */
/* Complements de solubilite verses dans l'histoire en memoire          */
/* ------------------------------------------------------------------ */

void story_apply_additions(Story *story, const SaveState *save) {
	if (!story || !save) return;

	/* Les indices generes rejoignent ceux de l'auteur. Un identifiant deja
	 * present est ignore : la fonction est appelee au chargement ET apres la
	 * generation, et elle doit pouvoir l'etre sans compter deux fois. */
	for (int i = 0; i < save->nb_gen_clues; i++) {
		const GeneratedClue *gc = &save->gen_clues[i];
		if (!gc->id) continue;

		bool exists = false;
		for (int k = 0; k < story->nb_clues; k++)
			if (story->clues[k].id && strcmp(story->clues[k].id, gc->id) == 0) exists = true;
		if (exists) continue;

		story->clues = realloc(story->clues, sizeof(Clue) * (size_t)(story->nb_clues + 1));
		Clue *cl = &story->clues[story->nb_clues++];
		memset(cl, 0, sizeof(*cl));
		cl->id           = strdup(gc->id);
		cl->name         = strdup(gc->name ? gc->name : gc->id);
		cl->description  = strdup(gc->description ? gc->description : "");
		cl->discoverable = true;
		cl->reveals_fact_ids = calloc((size_t)gc->nb_reveals, sizeof(char *));
		for (int k = 0; k < gc->nb_reveals; k++)
			cl->reveals_fact_ids[cl->nb_reveals++] = strdup(gc->reveals_fact_ids[k]);
	}

	/* Un fait donne a un second personnage. C'est ce qui rend l'indice
	 * utilisable : dans ce moteur, seul quelqu'un qui connait l'un des faits
	 * d'une piece peut la sortir. */
	for (int i = 0; i < save->nb_extra_knowledge; i++) {
		const ExtraKnowledge *ek = &save->extra_knowledge[i];
		if (!ek->npc_id || !ek->fact_id) continue;
		if (!story_fact(story, ek->fact_id)) continue;    /* fait inconnu : ignore */

		for (int c = 0; c < story->nb_characters; c++) {
			StoryCharacter *ch = &story->characters[c];
			if (!ch->id || strcmp(ch->id, ek->npc_id) != 0) continue;
			/* On ne touche pas au tableau `characters`, seulement au tableau de
			 * chaines d'UN personnage : les NPC gardent leur pointeur `def`. */
			str_array_add_unique(&ch->known_fact_ids, &ch->nb_known_facts, ek->fact_id);
			break;
		}
	}
}

bool memory_secret_revealed(SaveState *st, const char *npc_id, const char *secret_id) {
	NpcState *n = memory_get_npc(st, npc_id);
	if (!n) return false;
	return str_array_has(n->revealed_secret_ids, n->nb_revealed, secret_id);
}

void memory_reveal_secret(SaveState *st, const char *npc_id, const char *secret_id) {
	NpcState *n = memory_get_npc(st, npc_id);
	if (!n) return;
	str_array_add_unique(&n->revealed_secret_ids, &n->nb_revealed, secret_id);
}

bool memory_player_learn_fact(SaveState *st, const char *fact_id) {
	return str_array_add_unique(&st->known_fact_ids, &st->nb_known_facts, fact_id);
}

bool memory_player_knows_fact(const SaveState *st, const char *fact_id) {
	return str_array_has(st->known_fact_ids, st->nb_known_facts, fact_id);
}

bool memory_player_discover_clue(SaveState *st, const char *clue_id) {
	return str_array_add_unique(&st->discovered_clue_ids, &st->nb_discovered_clues, clue_id);
}

/* ------------------------------------------------------------------ */
/* Serialisation                                                       */
/* ------------------------------------------------------------------ */

static void write_string_array(StrBuf *sb, char **arr, int n) {
	sb_add(sb, "[");
	for (int i = 0; i < n; i++) {
		char *esc = json_escape(arr[i]);
		sb_addf(sb, "%s\"%s\"", i ? ", " : "", esc);
		free(esc);
	}
	sb_add(sb, "]");
}

bool save_write(const SaveState *st, const char *path) {
	if (!st) return false;

	StrBuf sb;
	sb_init(&sb);

	char *story_esc  = json_escape(st->story_id);
	char *player_esc = json_escape(st->player_name);

	sb_addf(&sb, "{\n  \"save_version\": %d,\n", st->save_version);
	sb_addf(&sb, "  \"story_id\": \"%s\",\n", story_esc);
	sb_addf(&sb, "  \"game_time\": %ld,\n", st->game_time);
	if (st->culprit_id) {
		char *c = json_escape(st->culprit_id);
		sb_addf(&sb, "  \"culprit_id\": \"%s\",\n", c);
		free(c);
	} else {
		sb_add(&sb, "  \"culprit_id\": null,\n");
	}
	if (st->culprit_solution) {
		char *e = json_escape(st->culprit_solution);
		sb_addf(&sb, "  \"culprit_solution\": \"%s\",\n", e);
		free(e);
	}
	if (st->culprit_brief) {
		char *e = json_escape(st->culprit_brief);
		sb_addf(&sb, "  \"culprit_brief\": \"%s\",\n", e);
		free(e);
	}
	sb_add(&sb, "  \"culprit_fact_ids\": ");
	write_string_array(&sb, st->culprit_fact_ids, st->nb_culprit_facts);
	sb_add(&sb, ",\n");

	/* Complements de solubilite ecrits au lancement. Ils appartiennent a CETTE
	 * partie : le fichier d'histoire n'est jamais modifie. */
	sb_add(&sb, "  \"generated_clues\": [");
	for (int i = 0; i < st->nb_gen_clues; i++) {
		const GeneratedClue *gc = &st->gen_clues[i];
		char *id = json_escape(gc->id);
		char *nm = json_escape(gc->name ? gc->name : "");
		char *ds = json_escape(gc->description ? gc->description : "");
		sb_addf(&sb, "%s\n    { \"id\": \"%s\", \"name\": \"%s\", \"description\": \"%s\", "
		             "\"reveals_fact_ids\": ", i ? "," : "", id, nm, ds);
		write_string_array(&sb, gc->reveals_fact_ids, gc->nb_reveals);
		sb_add(&sb, " }");
		free(id); free(nm); free(ds);
	}
	sb_add(&sb, st->nb_gen_clues ? "\n  ],\n" : "],\n");

	sb_add(&sb, "  \"extra_knowledge\": [");
	for (int i = 0; i < st->nb_extra_knowledge; i++) {
		char *np = json_escape(st->extra_knowledge[i].npc_id);
		char *fa = json_escape(st->extra_knowledge[i].fact_id);
		sb_addf(&sb, "%s\n    { \"npc_id\": \"%s\", \"fact_id\": \"%s\" }", i ? "," : "", np, fa);
		free(np); free(fa);
	}
	sb_add(&sb, st->nb_extra_knowledge ? "\n  ],\n" : "],\n");

	sb_addf(&sb, "  \"solved\": %s,\n", st->solved ? "true" : "false");
	sb_addf(&sb, "  \"api_usage\": { \"prompt_tokens\": %ld, \"completion_tokens\": %ld, "
	             "\"calls\": %ld },\n",
	        st->api_prompt_tokens, st->api_completion_tokens, st->api_calls);

	sb_addf(&sb, "  \"player\": {\n    \"name\": \"%s\",\n", player_esc);
	sb_addf(&sb, "    \"x\": %d,\n    \"y\": %d,\n", st->player_x, st->player_y);
	sb_add(&sb, "    \"known_fact_ids\": ");
	write_string_array(&sb, st->known_fact_ids, st->nb_known_facts);
	sb_add(&sb, ",\n    \"discovered_clue_ids\": ");
	write_string_array(&sb, st->discovered_clue_ids, st->nb_discovered_clues);
	sb_add(&sb, "\n  },\n");

	sb_add(&sb, "  \"npc_states\": {\n");
	for (int i = 0; i < st->nb_npcs; i++) {
		const NpcState *n = &st->npcs[i];
		char *id_esc = json_escape(n->npc_id);
		sb_addf(&sb, "    \"%s\": {\n", id_esc);
		free(id_esc);

		sb_addf(&sb, "      \"relationship\": { \"trust\": %d, \"affection\": %d, "
		             "\"fear\": %d, \"suspicion\": %d },\n",
		        n->rel.trust, n->rel.affection, n->rel.fear, n->rel.suspicion);
		sb_addf(&sb, "      \"exchanges_since_analysis\": %d,\n", n->exchanges_since_analysis);
		sb_addf(&sb, "      \"next_mem_seq\": %d,\n", n->next_mem_seq);

		/* Ou il se trouve et ce que le joueur sait de lui : sans cela, un
		 * personnage retrouvait sa place de depart a chaque rechargement. */
		sb_addf(&sb, "      \"world\": { \"x\": %d, \"y\": %d, \"face\": %d, "
		             "\"met\": %s, \"name_known\": %s, "
		             "\"can_move\": %s, \"can_change_room\": %s },\n",
		        n->x, n->y, n->face_id,
		        n->met ? "true" : "false",
		        n->name_known ? "true" : "false",
		        n->can_move ? "true" : "false",
		        n->can_change_room ? "true" : "false");

		sb_add(&sb, "      \"recent_messages\": [\n");
		for (int m = 0; m < n->nb_recent; m++) {
			char *r = json_escape(n->recent[m].role);
			char *c = json_escape(n->recent[m].content);
			sb_addf(&sb, "        { \"role\": \"%s\", \"content\": \"%s\"", r, c);
			if (n->recent[m].emotion) {
				char *e = json_escape(n->recent[m].emotion);
				sb_addf(&sb, ", \"emotion\": \"%s\"", e);
				free(e);
			}
			if (n->recent[m].action) {
				char *a = json_escape(n->recent[m].action);
				sb_addf(&sb, ", \"action\": \"%s\"", a);
				free(a);
			}
			if (n->recent[m].move) {
				char *mv = json_escape(n->recent[m].move);
				sb_addf(&sb, ", \"move\": \"%s\"", mv);
				free(mv);
			}
			sb_addf(&sb, " }%s\n", (m + 1 < n->nb_recent) ? "," : "");
			free(r); free(c);
		}
		sb_add(&sb, "      ],\n");

		sb_add(&sb, "      \"memories\": [\n");
		for (int m = 0; m < n->nb_mems; m++) {
			const Memory *mem = &n->mems[m];
			char *mid = json_escape(mem->id);
			char *mty = json_escape(mem->type);
			char *msu = json_escape(mem->summary);
			sb_addf(&sb, "        { \"id\": \"%s\", \"type\": \"%s\", \"summary\": \"%s\", "
			             "\"importance\": %d, \"emotion\": %d, \"tags\": ",
			        mid, mty, msu, mem->importance, mem->emotion);
			write_string_array(&sb, mem->tags, mem->nb_tags);
			sb_addf(&sb, ", \"created_at\": %ld, \"last_recalled_at\": ", mem->created_at);
			if (mem->last_recalled_at < 0) sb_add(&sb, "null");
			else sb_addf(&sb, "%ld", mem->last_recalled_at);
			sb_addf(&sb, " }%s\n", (m + 1 < n->nb_mems) ? "," : "");
			free(mid); free(mty); free(msu);
		}
		sb_add(&sb, "      ],\n");

		sb_add(&sb, "      \"revealed_secret_ids\": ");
		write_string_array(&sb, n->revealed_secret_ids, n->nb_revealed);
		sb_addf(&sb, "\n    }%s\n", (i + 1 < st->nb_npcs) ? "," : "");
	}
	sb_add(&sb, "  }\n}\n");

	free(story_esc);
	free(player_esc);

	/* Ecriture via un fichier temporaire puis rename: une coupure pendant
	 * l'ecriture ne peut pas laisser une sauvegarde tronquee. */
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);

	FILE *f = fopen(tmp, "wb");
	if (!f) { sb_free(&sb); return false; }
	size_t written = fwrite(sb.data, 1, sb.len, f);
	bool ok = (written == sb.len);
	if (fclose(f) != 0) ok = false;
	sb_free(&sb);

	if (!ok) { remove(tmp); return false; }
	return rename(tmp, path) == 0;
}

SaveState *save_load(const Story *story, const char *path) {
	JsonValue *root = json_parse_file(path);
	if (!root) return NULL;

	SaveState *st = calloc(1, sizeof(SaveState));
	st->save_version = json_int_or(json_object_get(root, "save_version"), 1);
	st->story_id     = json_strdup(json_object_get(root, "story_id"));
	if (!st->story_id) st->story_id = strdup(story->id);
	st->game_time    = (long)json_number_or(json_object_get(root, "game_time"), 0);
	st->culprit_id   = json_strdup(json_object_get(root, "culprit_id"));
	st->culprit_solution = json_strdup(json_object_get(root, "culprit_solution"));
	st->culprit_brief    = json_strdup(json_object_get(root, "culprit_brief"));
	st->culprit_fact_ids = json_strdup_array(json_object_get(root, "culprit_fact_ids"),
	                                         &st->nb_culprit_facts);

	st->solved = json_bool_or(json_object_get(root, "solved"), 0);

	/* Complements de solubilite. Absents des sauvegardes anterieures : les
	 * tableaux restent alors vides et rien ne change. */
	JsonValue *gcs = json_object_get(root, "generated_clues");
	int nb_gcs = json_array_count(gcs);
	if (nb_gcs > 0) {
		st->gen_clues = calloc((size_t)nb_gcs, sizeof(GeneratedClue));
		for (int i = 0; i < nb_gcs; i++) {
			JsonValue *g = json_array_get(gcs, i);
			GeneratedClue *gc = &st->gen_clues[st->nb_gen_clues];
			gc->id          = json_strdup(json_object_get(g, "id"));
			gc->name        = json_strdup(json_object_get(g, "name"));
			gc->description = json_strdup(json_object_get(g, "description"));
			gc->reveals_fact_ids = json_strdup_array(json_object_get(g, "reveals_fact_ids"),
			                                         &gc->nb_reveals);
			/* Un indice sans identifiant ou qui n'etablit rien ne sert a rien et
			 * ferait planter le rapprochement plus loin. */
			if (gc->id && gc->nb_reveals > 0) st->nb_gen_clues++;
			else {
				free(gc->id); free(gc->name); free(gc->description);
				json_free_string_array(gc->reveals_fact_ids, gc->nb_reveals);
				memset(gc, 0, sizeof(*gc));
			}
		}
	}

	JsonValue *eks = json_object_get(root, "extra_knowledge");
	int nb_eks = json_array_count(eks);
	if (nb_eks > 0) {
		st->extra_knowledge = calloc((size_t)nb_eks, sizeof(ExtraKnowledge));
		for (int i = 0; i < nb_eks; i++) {
			JsonValue *k = json_array_get(eks, i);
			ExtraKnowledge *ek = &st->extra_knowledge[st->nb_extra_knowledge];
			ek->npc_id  = json_strdup(json_object_get(k, "npc_id"));
			ek->fact_id = json_strdup(json_object_get(k, "fact_id"));
			if (ek->npc_id && ek->fact_id) st->nb_extra_knowledge++;
			else { free(ek->npc_id); free(ek->fact_id); memset(ek, 0, sizeof(*ek)); }
		}
	}

	JsonValue *api = json_object_get(root, "api_usage");
	st->api_prompt_tokens     = (long)json_number_or(json_object_get(api, "prompt_tokens"), 0);
	st->api_completion_tokens = (long)json_number_or(json_object_get(api, "completion_tokens"), 0);
	st->api_calls             = (long)json_number_or(json_object_get(api, "calls"), 0);

	JsonValue *pl = json_object_get(root, "player");
	st->player_name = json_strdup(json_object_get(pl, "name"));
	if (!st->player_name) st->player_name = strdup("Detective");
	st->player_x = json_int_or(json_object_get(pl, "x"), -1);
	st->player_y = json_int_or(json_object_get(pl, "y"), -1);
	st->known_fact_ids = json_strdup_array(json_object_get(pl, "known_fact_ids"),
	                                       &st->nb_known_facts);
	st->discovered_clue_ids = json_strdup_array(json_object_get(pl, "discovered_clue_ids"),
	                                            &st->nb_discovered_clues);

	/* Un etat par personnage de l'histoire, dans l'ordre de l'histoire : une
	 * histoire enrichie d'un personnage reste compatible avec une vieille
	 * sauvegarde, le nouveau venu demarre simplement vierge. */
	st->nb_npcs = story->nb_characters;
	st->npcs    = calloc((size_t)st->nb_npcs, sizeof(NpcState));

	JsonValue *states = json_object_get(root, "npc_states");
	for (int i = 0; i < st->nb_npcs; i++) {
		NpcState *n = &st->npcs[i];
		npc_state_init(n, story->characters[i].id);

		JsonValue *s = json_object_get(states, n->npc_id);
		if (!s) continue;

		JsonValue *rel = json_object_get(s, "relationship");
		n->rel.trust     = json_int_or(json_object_get(rel, "trust"), 0);
		n->rel.affection = json_int_or(json_object_get(rel, "affection"), 0);
		n->rel.fear      = json_int_or(json_object_get(rel, "fear"), 0);
		n->rel.suspicion = json_int_or(json_object_get(rel, "suspicion"), 0);

		n->exchanges_since_analysis = json_int_or(json_object_get(s, "exchanges_since_analysis"), 0);
		n->next_mem_seq             = json_int_or(json_object_get(s, "next_mem_seq"), 1);

		/* Bloc absent d'une sauvegarde anterieure : has_world reste faux et le
		 * moteur replace le personnage comme le decrit l'histoire. */
		JsonValue *world = json_object_get(s, "world");
		if (world) {
			n->has_world       = true;
			n->x               = json_int_or(json_object_get(world, "x"), -1);
			n->y               = json_int_or(json_object_get(world, "y"), -1);
			n->face_id         = json_int_or(json_object_get(world, "face"), -1);
			n->met             = json_bool_or(json_object_get(world, "met"), 0);
			n->name_known      = json_bool_or(json_object_get(world, "name_known"), 0);
			n->can_move        = json_bool_or(json_object_get(world, "can_move"), 1);
			n->can_change_room = json_bool_or(json_object_get(world, "can_change_room"), 0);
		}

		JsonValue *msgs = json_object_get(s, "recent_messages");
		int nb = json_array_count(msgs);
		if (nb > 0) {
			n->recent = calloc((size_t)nb, sizeof(ChatMsg));
			for (int m = 0; m < nb; m++) {
				JsonValue *e = json_array_get(msgs, m);
				char *role    = json_strdup(json_object_get(e, "role"));
				char *content = json_strdup(json_object_get(e, "content"));
				if (role && content) {
					n->recent[n->nb_recent].role    = role;
					n->recent[n->nb_recent].content = content;
					n->recent[n->nb_recent].emotion = json_strdup(json_object_get(e, "emotion"));
					n->recent[n->nb_recent].action  = json_strdup(json_object_get(e, "action"));
					n->recent[n->nb_recent].move    = json_strdup(json_object_get(e, "move"));
					n->nb_recent++;
				} else { free(role); free(content); }
			}
		}

		JsonValue *mems = json_object_get(s, "memories");
		nb = json_array_count(mems);
		if (nb > 0) {
			n->mems = calloc((size_t)nb, sizeof(Memory));
			for (int m = 0; m < nb; m++) {
				JsonValue *e = json_array_get(mems, m);
				Memory *mem = &n->mems[n->nb_mems];
				memset(mem, 0, sizeof(*mem));
				mem->id      = json_strdup(json_object_get(e, "id"));
				mem->type    = json_strdup(json_object_get(e, "type"));
				mem->summary = json_strdup(json_object_get(e, "summary"));
				if (!mem->summary) { free(mem->id); free(mem->type); continue; }
				mem->importance = json_int_or(json_object_get(e, "importance"), 1);
				mem->emotion    = json_int_or(json_object_get(e, "emotion"), 0);
				mem->tags       = json_strdup_array(json_object_get(e, "tags"), &mem->nb_tags);
				mem->created_at = (long)json_number_or(json_object_get(e, "created_at"), 0);
				/* null (jamais rappele) retombe sur -1 via le defaut. */
				mem->last_recalled_at =
					(long)json_number_or(json_object_get(e, "last_recalled_at"), -1);
				if (!mem->type) mem->type = strdup("interaction");
				n->nb_mems++;
			}
		}

		n->revealed_secret_ids = json_strdup_array(json_object_get(s, "revealed_secret_ids"),
		                                           &n->nb_revealed);

		/* Repart apres le plus grand identifiant deja utilise. */
		if (n->next_mem_seq <= n->nb_mems) n->next_mem_seq = n->nb_mems + 1;
	}

	json_free(root);
	return st;
}
