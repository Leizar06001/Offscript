#include "deepseek_client.h"
#include "json_min.h"
#include "textutil.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEEPSEEK_URL "https://api.deepseek.com/chat/completions"
#define DEEPSEEK_DEFAULT_MODEL "deepseek-chat"

static char g_api_key[128]     = "";
static char g_model[64]        = DEEPSEEK_DEFAULT_MODEL;
static char g_model_story[64]  = DEEPSEEK_DEFAULT_MODEL;

/* Tarifs, en dollars par million de jetons. Ce sont des reperes, modifiables
 * dans les options du jeu : le compteur affiche une ESTIMATION, jamais une
 * facture. */
static double g_price_in_per_m  = 0.28;
static double g_price_out_per_m = 0.42;

void deepseek_set_prices(double in_per_m, double out_per_m) {
	if (in_per_m  >= 0.0) g_price_in_per_m  = in_per_m;
	if (out_per_m >= 0.0) g_price_out_per_m = out_per_m;
}

static char g_reasoning[8] = "";

void deepseek_set_reasoning(const char *level) {
	snprintf(g_reasoning, sizeof(g_reasoning), "%s", level ? level : "");
}

static DeepseekUsage   g_usage;
static pthread_mutex_t g_usage_lock = PTHREAD_MUTEX_INITIALIZER;

void deepseek_configure(const char *api_key, const char *model, const char *story_model) {
	if (api_key) snprintf(g_api_key, sizeof(g_api_key), "%s", api_key);
	if (model && *model) snprintf(g_model, sizeof(g_model), "%s", model);
	snprintf(g_model_story, sizeof(g_model_story), "%s",
	         (story_model && *story_model) ? story_model : g_model);
}

void deepseek_usage_get(DeepseekUsage *out) {
	pthread_mutex_lock(&g_usage_lock);
	*out = g_usage;
	pthread_mutex_unlock(&g_usage_lock);
}

void deepseek_usage_set(long prompt_tokens, long completion_tokens, long calls) {
	pthread_mutex_lock(&g_usage_lock);
	g_usage.prompt_tokens     = prompt_tokens;
	g_usage.completion_tokens = completion_tokens;
	g_usage.calls             = calls;
	/* Les compteurs de cache ne viennent pas de la sauvegarde : ils repartent de
	 * zero avec la session. Les garder ferait comparer les succes de la partie
	 * precedente aux echecs de la nouvelle apres un [R]. */
	g_usage.cache_hit_tokens  = 0;
	g_usage.cache_miss_tokens = 0;
	pthread_mutex_unlock(&g_usage_lock);
}

double deepseek_usage_cost(const DeepseekUsage *u) {
	if (!u) return 0.0;
	return (double)u->prompt_tokens     / 1e6 * g_price_in_per_m
	     + (double)u->completion_tokens / 1e6 * g_price_out_per_m;
}

/* Le compte est tenu par les fils de requete : il lui faut son propre verrou.
 * `usage` n'est pas toujours present (une reponse en erreur n'en a pas). */
static void usage_add(JsonValue *usage, const char *raw_json) {
	if (!usage) return;

	/* Les noms des compteurs de cache dependent du fournisseur, et un nom qui ne
	 * correspond a rien est indiscernable d'un fournisseur qui ne cache pas :
	 * dans les deux cas la barre du bas n'affiche rien. OFFSCRIPT_DEBUG_USAGE
	 * ecrit la reponse brute pour trancher sur un vrai appel plutot que de
	 * deviner. L'ecran appartient a ncurses : on passe par un fichier. */
	if (raw_json && getenv("OFFSCRIPT_DEBUG_USAGE")) {
		FILE *f = fopen("/tmp/offscript_usage.log", "a");
		if (f) { fprintf(f, "%s\n", raw_json); fclose(f); }
	}

	long in  = (long)json_number_or(json_object_get(usage, "prompt_tokens"), 0);
	long out = (long)json_number_or(json_object_get(usage, "completion_tokens"), 0);
	if (in == 0 && out == 0) return;

	/* Le fournisseur met en cache le prefixe des requetes et facture ces jetons
	 * une fraction du prix. Absent des vieilles reponses ou d'un autre
	 * fournisseur : on laisse alors les compteurs a zero, et l'affichage
	 * n'annonce rien plutot que d'annoncer 0%. */
	long hit  = (long)json_number_or(json_object_get(usage, "prompt_cache_hit_tokens"), 0);
	long miss = (long)json_number_or(json_object_get(usage, "prompt_cache_miss_tokens"), 0);

	pthread_mutex_lock(&g_usage_lock);
	g_usage.prompt_tokens     += in;
	g_usage.completion_tokens += out;
	g_usage.cache_hit_tokens  += hit;
	g_usage.cache_miss_tokens += miss;
	g_usage.calls++;
	pthread_mutex_unlock(&g_usage_lock);
}

/* Ecarte la reponse : on ne s'interesse qu'au code HTTP. */
static size_t discard_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
	(void)ptr; (void)userdata;
	return size * nmemb;
}

int deepseek_verify_key(const char *api_key, const char *model) {
	if (!api_key || !*api_key) return 0;

	char auth[192];
	snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);

	char body[256];
	snprintf(body, sizeof(body),
	         "{\"model\":\"%s\",\"max_tokens\":1,"
	         "\"messages\":[{\"role\":\"user\",\"content\":\"ping\"}]}",
	         (model && *model) ? model : DEEPSEEK_DEFAULT_MODEL);

	CURL *curl = curl_easy_init();
	if (!curl) return -1;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, auth);

	curl_easy_setopt(curl, CURLOPT_URL, DEEPSEEK_URL);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_cb);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);

	CURLcode res = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) return -1;
	if (status == 401 || status == 403) return 0;
	if (status >= 200 && status < 300) return 1;
	/* 429, 5xx... : la cle n'est pas en cause. */
	return -1;
}

typedef enum { DS_DIALOGUE, DS_RAW } DeepseekMode;

struct DeepseekRequest {
	pthread_t       thread;
	pthread_mutex_t mutex;
	DeepseekMode    mode;
	int             ready;      /* 0 pending, 1 ok, -1 erreur */

	DialogueReply reply;        /* DS_DIALOGUE */
	char         *raw_text;     /* DS_RAW: reponse brute du modele */

	/* Apercu progressif de "line", et l'emoji des qu'il est connu. */
	char  *partial_line;
	size_t stream_consumed;
	char  *emotion_pub;
	int    emotion_consumed;
};

typedef struct {
	DeepseekRequest *req;
	char            *body;   /* possede par le thread */
} AskThreadArgs;

/* ------------------------------------------------------------------ */
/* Construction du corps de requete                                    */
/* ------------------------------------------------------------------ */

static void add_message(StrBuf *sb, const char *role, const char *content, bool first) {
	char *r = json_escape(role);
	char *c = json_escape(content);
	sb_addf(sb, "%s{\"role\":\"%s\",\"content\":\"%s\"}", first ? "" : ",", r, c);
	free(r);
	free(c);
}

static char *build_body(const char *system_prompt,
                        const DeepseekMsg *history, int nb_history,
                        const char *user_text, bool stream, const char *model_name) {
	StrBuf sb;
	sb_init(&sb);

	char *model = json_escape(model_name && *model_name ? model_name : g_model);
	sb_addf(&sb, "{\"model\":\"%s\",\"stream\":%s,", model, stream ? "true" : "false");
	free(model);

	/* En diffusion, la consommation n'arrive que si on la demande : sans
	 * cette option le dernier fragment ne porte aucun compte de jetons et le
	 * compteur resterait a zero pour tous les dialogues. */
	if (stream) sb_add(&sb, "\"stream_options\":{\"include_usage\":true},");
	if (g_reasoning[0]) sb_addf(&sb, "\"reasoning_effort\":\"%s\",", g_reasoning);
	sb_add(&sb, "\"messages\":[");

	add_message(&sb, "system", system_prompt ? system_prompt : "", true);

	/* L'historique doit rester une alternance user/assistant valide: il vient
	 * de la sauvegarde, qui ne stocke que des paires completes.
	 *
	 * Les tours de l'assistant sont rejoues sous la forme JSON exacte qu'on
	 * lui demande. Rejoues en prose, ils lui apprenaient l'inverse de la
	 * consigne et il cessait de repondre en JSON au bout de 2 ou 3 echanges. */
	for (int i = 0; i < nb_history; i++) {
		if (!history[i].role || !history[i].content) continue;

		bool is_assistant = strcmp(history[i].role, "assistant") == 0;
		if (is_assistant && history[i].emotion && history[i].action) {
			char *emo = json_escape(history[i].emotion);
			char *lin = json_escape(history[i].content);
			char *act = json_escape(history[i].action);
			char *mv  = history[i].move ? json_escape(history[i].move) : NULL;

			StrBuf inner;
			sb_init(&inner);
			sb_addf(&inner, "{\"emotion\":\"%s\",\"line\":\"%s\",\"action\":\"%s\"", emo, lin, act);
			if (mv) sb_addf(&inner, ",\"move\":\"%s\"", mv);
			sb_add(&inner, "}");
			add_message(&sb, "assistant", inner.data, false);
			sb_free(&inner);

			free(emo); free(lin); free(act); free(mv);
		} else {
			add_message(&sb, history[i].role, history[i].content, false);
		}
	}
	add_message(&sb, "user", user_text ? user_text : "", false);

	sb_add(&sb, "]}");
	return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* Lecture du flux (Server-Sent Events)                                */
/* ------------------------------------------------------------------ */

typedef struct {
	DeepseekRequest *req;

	char  *sse_buf;   /* octets pas encore resolus en ligne complete */
	size_t sse_len, sse_cap;

	char  *content;   /* morceaux de delta.content concatenes */
	size_t content_len, content_cap;

	char *emotion;
	int   line_done;
} StreamCtx;

static void buf_append(char **buf, size_t *len, size_t *cap, const char *data, size_t n) {
	if (*len + n + 1 > *cap) {
		*cap = (*len + n + 1) * 2;
		*buf = realloc(*buf, *cap);
	}
	memcpy(*buf + *len, data, n);
	*len += n;
	(*buf)[*len] = '\0';
}

/* Extrait la valeur (possiblement encore en cours) du champ JSON `key`.
 * Les echappements sont decodes exactement comme dans json_min.c, pour que
 * ce qui est diffuse reste un prefixe octet pour octet de ce que produira
 * l'analyse finale: l'appelant compte les octets deja affiches. */
static char *extract_field_value(const char *text, const char *key, int *closed) {
	char pattern[32];
	snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
	const char *pos = strstr(text, pattern);
	if (!pos) return NULL;
	pos += strlen(pattern);

	char *out = malloc(strlen(pos) + 1);
	size_t oi = 0;
	const char *s = pos;
	*closed = 0;
	while (*s) {
		if (*s == '\\') {
			/* La sortie du modele s'accumule morceau par morceau: une valeur
			 * peut s'arreter au milieu d'un echappement. On s'arrete avant,
			 * plutot que d'emettre l'antislash nu. */
			char n = s[1];
			if (!n) break;
			if (n == 'u') {
				if (!s[2] || !s[3] || !s[4] || !s[5]) break;
				out[oi++] = '?';
				s += 4;
			}
			else if (n == 'n') out[oi++] = '\n';
			else if (n == 't') out[oi++] = '\t';
			else if (n == 'r') out[oi++] = '\r';
			else out[oi++] = n;
			s += 2;
		} else if (*s == '"') {
			*closed = 1;
			break;
		} else {
			out[oi++] = *s++;
		}
	}
	out[oi] = '\0';
	return out;
}

/* Publie ce qui est arrive: l'emoji des que sa valeur se referme (le modele
 * doit emettre "emotion" en premier, donc tres tot) et la replique au fil de
 * sa croissance. Les deux restent separes parce que l'emoji va sur le visage
 * du personnage et non dans le chat. */
static void update_stream_state(StreamCtx *ctx) {
	if (!ctx->emotion) {
		int closed = 0;
		char *emotion = extract_field_value(ctx->content, "emotion", &closed);
		if (emotion && closed) {
			ctx->emotion = emotion;
			pthread_mutex_lock(&ctx->req->mutex);
			free(ctx->req->emotion_pub);
			ctx->req->emotion_pub = strdup(emotion);
			pthread_mutex_unlock(&ctx->req->mutex);
		} else {
			free(emotion);
		}
	}

	if (ctx->line_done) return;

	int line_closed = 0;
	char *line_part = extract_field_value(ctx->content, "line", &line_closed);
	if (!line_part) return;
	ctx->line_done = line_closed;

	pthread_mutex_lock(&ctx->req->mutex);
	free(ctx->req->partial_line);
	ctx->req->partial_line = line_part;
	pthread_mutex_unlock(&ctx->req->mutex);
}

static void handle_sse_event(StreamCtx *ctx, const char *payload, size_t payload_len) {
	if (payload_len == 6 && strncmp(payload, "[DONE]", 6) == 0) return;

	char *line = malloc(payload_len + 1);
	memcpy(line, payload, payload_len);
	line[payload_len] = '\0';

	JsonValue *chunk = json_parse(line);
	/* Le dernier fragment d'un flux porte la consommation de tout l'appel. */
	usage_add(json_object_get(chunk, "usage"), line);
	JsonValue *choices = json_object_get(chunk, "choices");
	if (json_array_count(choices) > 0) {
		JsonValue *delta = json_object_get(json_array_get(choices, 0), "delta");
		const char *piece = json_string(json_object_get(delta, "content"));
		if (piece && *piece) {
			buf_append(&ctx->content, &ctx->content_len, &ctx->content_cap, piece, strlen(piece));
			update_stream_state(ctx);
		}
	}
	json_free(chunk);
	free(line);
}

static size_t sse_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
	StreamCtx *ctx = userdata;
	size_t n = size * nmemb;
	buf_append(&ctx->sse_buf, &ctx->sse_len, &ctx->sse_cap, ptr, n);

	char *start = ctx->sse_buf;
	char *end   = ctx->sse_buf + ctx->sse_len;
	char *nl;
	while ((nl = memchr(start, '\n', (size_t)(end - start))) != NULL) {
		size_t line_len = (size_t)(nl - start);
		if (line_len >= 6 && strncmp(start, "data: ", 6) == 0) {
			handle_sse_event(ctx, start + 6, line_len - 6);
		}
		start = nl + 1;
	}

	size_t remaining = (size_t)(end - start);
	memmove(ctx->sse_buf, start, remaining);
	ctx->sse_len = remaining;
	ctx->sse_buf[ctx->sse_len] = '\0';
	return n;
}

/* Reponse non diffusee: on accumule tout le corps tel quel. */
static size_t plain_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
	StreamCtx *ctx = userdata;
	size_t n = size * nmemb;
	buf_append(&ctx->content, &ctx->content_len, &ctx->content_cap, ptr, n);
	return n;
}

/* ------------------------------------------------------------------ */
/* Execution                                                           */
/* ------------------------------------------------------------------ */

/* Une tentative. Remet les tampons a zero d'abord, pour qu'un reessai ne
 * melange jamais les octets d'un essai precedent. */
static CURLcode perform_once(const char *body, StreamCtx *ctx, bool stream, long *status) {
	ctx->sse_len = 0;
	ctx->content_len = 0;
	if (ctx->sse_buf) ctx->sse_buf[0] = '\0';
	if (ctx->content) ctx->content[0] = '\0';
	free(ctx->emotion);
	ctx->emotion = NULL;
	ctx->line_done = 0;

	char auth_header[192];
	snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_api_key);

	CURL *curl = curl_easy_init();
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, auth_header);

	curl_easy_setopt(curl, CURLOPT_URL, DEEPSEEK_URL);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream ? sse_write_cb : plain_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 120000L);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return res;
}

static void *ask_thread_main(void *arg) {
	AskThreadArgs *a = arg;
	DeepseekRequest *req = a->req;
	bool stream = (req->mode == DS_DIALOGUE);

	StreamCtx ctx = { .req = req };
	ctx.sse_buf = malloc(1); ctx.sse_buf[0] = '\0'; ctx.sse_cap = 1;
	ctx.content = malloc(1); ctx.content[0] = '\0'; ctx.content_cap = 1;

	/* Ce reseau a montre de vraies coupures (delai de connexion sans aucune
	 * reponse): un seul reessai sur ce cas ameliore nettement la fiabilite.
	 * Pas de reessai sur une erreur HTTP, qui ne se corrigerait pas. */
	long status = 0;
	CURLcode res = perform_once(a->body, &ctx, stream, &status);
	if (res != CURLE_OK) res = perform_once(a->body, &ctx, stream, &status);

	free(a->body);
	free(a);

	DialogueReply reply = { NULL, NULL, NULL, 0, NULL };
	char *raw_text = NULL;
	int ok = 0;

	if (res == CURLE_OK && status >= 200 && status < 300 && ctx.content_len > 0) {
		if (req->mode == DS_DIALOGUE) {
			ok = dialogue_reply_from_text(ctx.content, &reply);
			/* Le modele a repondu en prose au lieu de l'objet demande : on
			 * garde quand meme la replique plutot que de perdre le tour. */
			if (!ok) ok = dialogue_reply_from_prose(ctx.content, &reply);
		} else {
			/* Reponse complete: on en extrait le texte du modele. */
			JsonValue *root = json_parse(ctx.content);
			usage_add(json_object_get(root, "usage"), ctx.content);
			JsonValue *choices = json_object_get(root, "choices");
			if (json_array_count(choices) > 0) {
				JsonValue *msg = json_object_get(json_array_get(choices, 0), "message");
				const char *c = json_string(json_object_get(msg, "content"));
				if (c && *c) { raw_text = strdup(c); ok = 1; }
			}
			json_free(root);
		}
	}

	free(ctx.sse_buf);
	free(ctx.content);
	free(ctx.emotion);

	pthread_mutex_lock(&req->mutex);
	req->reply    = reply;
	req->raw_text = raw_text;
	req->ready    = ok ? 1 : -1;
	pthread_mutex_unlock(&req->mutex);
	return NULL;
}

static DeepseekRequest *spawn(DeepseekMode mode, char *body) {
	DeepseekRequest *req = calloc(1, sizeof(*req));
	pthread_mutex_init(&req->mutex, NULL);
	req->mode  = mode;
	req->ready = 0;

	AskThreadArgs *args = malloc(sizeof(*args));
	args->req  = req;
	args->body = body;

	if (pthread_create(&req->thread, NULL, ask_thread_main, args) != 0) {
		free(args->body);
		free(args);
		pthread_mutex_destroy(&req->mutex);
		free(req);
		return NULL;
	}
	pthread_detach(req->thread);
	return req;
}

DeepseekRequest *deepseek_ask_dialogue(const char *system_prompt,
                                        const DeepseekMsg *history, int nb_history,
                                        const char *user_text) {
	return spawn(DS_DIALOGUE,
	             build_body(system_prompt, history, nb_history, user_text, true, g_model));
}

DeepseekRequest *deepseek_ask_raw(const char *system_prompt, const char *user_text) {
	return spawn(DS_RAW, build_body(system_prompt, NULL, 0, user_text, false, g_model));
}

DeepseekRequest *deepseek_ask_story(const char *system_prompt, const char *user_text) {
	return spawn(DS_RAW, build_body(system_prompt, NULL, 0, user_text, false, g_model_story));
}

/* ------------------------------------------------------------------ */
/* Interrogation                                                       */
/* ------------------------------------------------------------------ */

static void request_destroy(DeepseekRequest *req) {
	pthread_mutex_destroy(&req->mutex);
	free(req);
}

int deepseek_poll(DeepseekRequest *req, DialogueReply *out) {
	pthread_mutex_lock(&req->mutex);
	int status = req->ready;
	DialogueReply reply = req->reply;
	char *partial = req->partial_line;
	char *emotion = req->emotion_pub;
	char *raw     = req->raw_text;
	pthread_mutex_unlock(&req->mutex);

	if (status == 0) return 0;

	if (out) *out = reply;
	else dialogue_reply_free(&reply);

	free(partial);
	free(emotion);
	free(raw);
	request_destroy(req);
	return status;
}

int deepseek_poll_raw(DeepseekRequest *req, char **out_text) {
	pthread_mutex_lock(&req->mutex);
	int status = req->ready;
	char *raw = req->raw_text;
	char *partial = req->partial_line;
	char *emotion = req->emotion_pub;
	DialogueReply reply = req->reply;
	pthread_mutex_unlock(&req->mutex);

	if (status == 0) return 0;

	if (out_text) *out_text = raw;
	else free(raw);

	dialogue_reply_free(&reply);
	free(partial);
	free(emotion);
	request_destroy(req);
	return status;
}

char *deepseek_poll_stream(DeepseekRequest *req) {
	pthread_mutex_lock(&req->mutex);
	size_t full_len = req->partial_line ? strlen(req->partial_line) : 0;
	char *result = NULL;
	if (full_len > req->stream_consumed) {
		result = strdup(req->partial_line + req->stream_consumed);
		req->stream_consumed = full_len;
	}
	pthread_mutex_unlock(&req->mutex);
	return result;
}

char *deepseek_poll_emotion(DeepseekRequest *req) {
	pthread_mutex_lock(&req->mutex);
	char *result = NULL;
	if (req->emotion_pub && !req->emotion_consumed) {
		result = strdup(req->emotion_pub);
		req->emotion_consumed = 1;
	}
	pthread_mutex_unlock(&req->mutex);
	return result;
}
