#include "diag.h"

#include "includes.h"
#include "json_min.h"
#include "textutil.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>

static FILE            *g_diag_file;
static char             g_diag_path[512];
static pthread_mutex_t  g_diag_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long    g_diag_seq;
static long             g_diag_request_seq;
static unsigned int     g_diag_file_seq;

static void write_event(const char *event, const char *data_fmt, ...)
	__attribute__((format(printf, 2, 3)));
static void write_event(const char *event, const char *data_fmt, ...) {
	char timestamp[40];
	struct timespec ts;
	struct tm utc;
	clock_gettime(CLOCK_REALTIME, &ts);
	gmtime_r(&ts.tv_sec, &utc);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &utc);

	char *escaped_event = json_escape(event ? event : "unknown");
	pthread_mutex_lock(&g_diag_lock);
	if (g_diag_file) {
		fprintf(g_diag_file, "{\"time\":\"%s.%03ldZ\",\"seq\":%lu,\"event\":\"%s\",\"data\":",
		        timestamp, ts.tv_nsec / 1000000L, ++g_diag_seq, escaped_event);
		va_list ap;
		va_start(ap, data_fmt);
		vfprintf(g_diag_file, data_fmt, ap);
		va_end(ap);
		fputs("}\n", g_diag_file);
		fflush(g_diag_file);
	}
	pthread_mutex_unlock(&g_diag_lock);
	free(escaped_event);
}

static char *esc(const char *s) {
	return json_escape(s ? s : "");
}

int diag_init(bool enabled) {
	if (!enabled) return 0;
	if (diag_enabled()) return 0;
	if (mkdir("diagnostics", 0700) != 0 && errno != EEXIST) return -1;
	chmod("diagnostics", 0700);

	time_t now = time(NULL);
	struct tm local;
	char stamp[32];
	localtime_r(&now, &local);
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
	snprintf(g_diag_path, sizeof(g_diag_path),
	         "diagnostics/offscript-%s-%ld-%u.jsonl", stamp, (long)getpid(),
	         ++g_diag_file_seq);

	int fd = open(g_diag_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) return -1;
	g_diag_file = fdopen(fd, "w");
	if (!g_diag_file) { close(fd); return -1; }
	g_diag_seq = 0;
	g_diag_request_seq = 0;
	write_event("diagnostic_started",
	            "{\"format_version\":1,\"warning\":\"contains full prompts, conversations and story spoilers\"}");
	return 0;
}

bool diag_enabled(void) {
	pthread_mutex_lock(&g_diag_lock);
	bool enabled = g_diag_file != NULL;
	pthread_mutex_unlock(&g_diag_lock);
	return enabled;
}
const char *diag_path(void) { return g_diag_path; }

void diag_close(void) {
	if (!g_diag_file) return;
	write_event("diagnostic_stopped", "{}");
	pthread_mutex_lock(&g_diag_lock);
	if (g_diag_file) fclose(g_diag_file);
	g_diag_file = NULL;
	pthread_mutex_unlock(&g_diag_lock);
}

long diag_api_request(const char *purpose, const char *model, const char *body) {
	pthread_mutex_lock(&g_diag_lock);
	if (!g_diag_file) { pthread_mutex_unlock(&g_diag_lock); return 0; }
	long id = ++g_diag_request_seq;
	pthread_mutex_unlock(&g_diag_lock);
	char *p = esc(purpose), *m = esc(model), *b = esc(body);
	write_event("model_request", "{\"request_id\":%ld,\"purpose\":\"%s\",\"model\":\"%s\",\"body\":\"%s\"}",
	            id, p, m, b);
	free(p); free(m); free(b);
	return id;
}

void diag_api_response(long id, const char *purpose, const char *model,
		long http_status, int curl_code, long duration_ms,
		int attempts, const char *curl_error,
		const char *provider_response, const char *model_text,
		bool usable, bool requested_output_parsed) {
	if (id <= 0) return;
	char *p = esc(purpose), *m = esc(model);
	char *err = esc(curl_error);
	char *raw = esc(provider_response), *text = esc(model_text);
	write_event("model_response",
	            "{\"request_id\":%ld,\"purpose\":\"%s\",\"model\":\"%s\","
	            "\"http_status\":%ld,\"curl_code\":%d,\"curl_error\":\"%s\","
	            "\"duration_ms\":%ld,\"attempts\":%d,"
	            "\"usable\":%s,\"requested_output_parsed\":%s,"
	            "\"provider_response\":\"%s\",\"model_text\":\"%s\"}",
	            id, p, m, http_status, curl_code, err, duration_ms, attempts,
	            usable ? "true" : "false", requested_output_parsed ? "true" : "false",
	            raw, text);
	free(p); free(m); free(err); free(raw); free(text);
}

void diag_game_state(const struct s_game *game, const char *reason) {
	if (!game || !diag_enabled()) return;
	char *why = esc(reason);
	char *story = esc(game->story && game->story->id ? game->story->id : "");
	char *save = esc(game->save_path);
	char *player = esc(game->player.name);
	write_event("game_state",
	            "{\"reason\":\"%s\",\"story_id\":\"%s\",\"save_path\":\"%s\","
	            "\"player\":\"%s\",\"player_x\":%d,\"player_y\":%d,"
	            "\"game_time\":%ld,\"known_facts\":%d,\"discovered_clues\":%d,\"solved\":%s}",
	            why, story, save, player, game->player.x, game->player.y,
	            game->save ? game->save->game_time : 0,
	            game->save ? game->save->nb_known_facts : 0,
	            game->save ? game->save->nb_discovered_clues : 0,
	            game->save && game->save->solved ? "true" : "false");
	free(why); free(story); free(save); free(player);
}

void diag_dialogue(const struct s_game *game, int idx, const char *question,
		const char *line, const char *emotion, const char *action,
		const char *move, NpcMoveResult move_result, bool interjection, int xb, int yb, int rb,
		bool mb, int dxb, int dyb) {
	if (!game || idx < 0 || idx >= game->nb_npcs || !diag_enabled()) return;
	const NPC *n = &game->npcs[idx];
	char *id = esc(n->def ? n->def->id : ""), *name = esc(npc_display_name(n));
	char *q = esc(question), *l = esc(line), *e = esc(emotion);
	char *a = esc(action), *mv = esc(move);
	write_event("dialogue_applied",
	            "{\"npc_id\":\"%s\",\"npc_name\":\"%s\",\"interjection\":%s,"
	            "\"question\":\"%s\",\"line\":\"%s\",\"emotion\":\"%s\","
	            "\"action\":\"%s\",\"move_order\":\"%s\",\"move_result\":\"%s\","
	            "\"before\":{\"x\":%d,\"y\":%d,\"room\":%d,\"moving\":%s,\"dest_x\":%d,\"dest_y\":%d},"
	            "\"after\":{\"x\":%d,\"y\":%d,\"room\":%d,\"moving\":%s,\"dest_x\":%d,\"dest_y\":%d}}",
	            id, name, interjection ? "true" : "false", q, l, e, a, mv,
	            npc_move_result_name(move_result),
	            xb, yb, rb, mb ? "true" : "false", dxb, dyb,
	            n->x, n->y, n->room, n->moving ? "true" : "false", n->dest_x, n->dest_y);
	free(id); free(name); free(q); free(l); free(e); free(a); free(mv);
}

void diag_analysis(const struct s_game *game, int idx, const AnalysisResult *a,
		int facts_before, int clues_before, Relation before) {
	if (!game || !a || idx < 0 || idx >= game->nb_npcs || !diag_enabled()) return;
	NpcState *ns = memory_get_npc(game->save, game->npcs[idx].def->id);
	Relation after = ns ? ns->rel : before;
	char *id = esc(game->npcs[idx].def->id), *type = esc(a->type), *summary = esc(a->summary);
	char *secret = esc(a->revealed_secret_id), *clue = esc(a->produced_clue_id);
	StrBuf facts;
	sb_init(&facts);
	sb_add(&facts, "[");
	for (int i = 0; i < a->nb_learned_facts; i++) {
		char *fact = esc(a->learned_fact_ids[i]);
		sb_addf(&facts, "%s\"%s\"", i ? "," : "", fact);
		free(fact);
	}
	sb_add(&facts, "]");
	write_event(a->grounded ? "analysis_applied" : "analysis_discarded",
	            "{\"npc_id\":\"%s\",\"grounded\":%s,\"remember\":%s,\"type\":\"%s\",\"summary\":\"%s\","
	            "\"importance\":%d,\"emotion\":%d,\"learned_fact_ids\":%s,"
	            "\"revealed_secret_id\":\"%s\",\"produced_clue_id\":\"%s\","
	            "\"relationship_before\":{\"trust\":%d,\"affection\":%d,\"fear\":%d,\"suspicion\":%d},"
	            "\"relationship_after\":{\"trust\":%d,\"affection\":%d,\"fear\":%d,\"suspicion\":%d},"
	            "\"known_facts_before\":%d,\"known_facts_after\":%d,"
	            "\"clues_before\":%d,\"clues_after\":%d}",
	            id, a->grounded ? "true" : "false", a->remember ? "true" : "false",
	            type, summary, a->importance, a->emotion,
	            facts.data, secret, clue,
	            before.trust, before.affection, before.fear, before.suspicion,
	            after.trust, after.affection, after.fear, after.suspicion,
	            facts_before, game->save->nb_known_facts,
	            clues_before, game->save->nb_discovered_clues);
	sb_free(&facts);
	free(id); free(type); free(summary); free(secret); free(clue);
}

void diag_analysis_rejected(const struct s_game *game, int idx,
		const char *model_text) {
	if (!game || idx < 0 || idx >= game->nb_npcs || !diag_enabled()) return;
	char *id = esc(game->npcs[idx].def->id), *text = esc(model_text);
	write_event("analysis_rejected",
	            "{\"npc_id\":\"%s\",\"reason\":\"invalid analysis format or identifiers\",\"model_text\":\"%s\"}",
	            id, text);
	free(id); free(text);
}
