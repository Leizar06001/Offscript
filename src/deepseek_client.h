#ifndef DEEPSEEK_CLIENT_H
#define DEEPSEEK_CLIENT_H

#include "dialogue.h"

/* Stateless HTTP client. It owns no conversation: the caller passes the
 * system prompt and the recent history on every call, because both are
 * rebuilt from the save (see memory.h / prompt.h). */

/* One turn of history, as sent in messages[]. For an assistant turn, giving
 * emotion and action lets the client replay it as the exact JSON object the
 * model is asked to produce. That matters: replaying assistant turns as bare
 * prose makes the model imitate the prose and abandon the format after a few
 * exchanges. Leave them NULL for a user turn. */
typedef struct {
	const char *role;      /* "user" | "assistant" */
	const char *content;
	const char *emotion;
	const char *action;
	/* Deplacement rejoue avec le tour. Meme raison que emotion/action : un
	 * historique d'objets a trois cles apprend au modele a n'en produire que
	 * trois, et la cle "move" cesse d'arriver — le personnage dit alors qu'il
	 * se deplace sans jamais bouger. NULL quand il ne peut pas se deplacer. */
	const char *move;
} DeepseekMsg;

/* Opaque handle for a request in flight. */
typedef struct DeepseekRequest DeepseekRequest;

/* `model` sert a tout ce qui est frequent (repliques, analyse memoire).
 * `story_model` n'est utilise que pour les rares appels de conception, ou la
 * qualite compte plus que le prix : la resolution de l'enquete est ecrite une
 * fois par partie et doit tenir debout. NULL pour reprendre `model`. */
void deepseek_configure(const char *api_key, const char *model, const char *story_model);

/* Verifie une cle par un appel minimal, de facon synchrone (uniquement au
 * lancement, pour ne pas laisser le joueur decouvrir une cle invalide au
 * milieu d'un interrogatoire).
 *   1  = cle acceptee
 *   0  = cle refusee (401/403)
 *  -1  = injoignable : on ne peut pas conclure */
int deepseek_verify_key(const char *api_key, const char *model);

/* Streaming dialogue call. history is replayed before user_text so the
 * character remembers the current conversation. Returns NULL if the request
 * could not be started. */
DeepseekRequest *deepseek_ask_dialogue(const char *system_prompt,
                                        const DeepseekMsg *history, int nb_history,
                                        const char *user_text);

/* Non-streaming call used for the memory-analysis pass: the answer is only
 * read by the engine, so there is nothing to show progressively. */
DeepseekRequest *deepseek_ask_raw(const char *system_prompt, const char *user_text);

/* Idem, mais sur le modele de conception (voir deepseek_configure). */
DeepseekRequest *deepseek_ask_story(const char *system_prompt, const char *user_text);

/* Consommation cumulee depuis le lancement, telle que l'API la rapporte.
 * Sert au compteur affiche a l'ecran. */
typedef struct {
	long prompt_tokens;
	long completion_tokens;
	long calls;
} DeepseekUsage;

void deepseek_usage_get(DeepseekUsage *out);
/* Repart d'un total connu (repris de la sauvegarde au chargement). */
void deepseek_usage_set(long prompt_tokens, long completion_tokens, long calls);

/* Cout estime, en dollars, pour une consommation donnee. Les tarifs sont
 * dans deepseek_client.c et se corrigent a un seul endroit. */
double deepseek_usage_cost(const DeepseekUsage *u);

/* Tarifs employes par deepseek_usage_cost, en dollars par million de jetons.
 * Ils viennent des reglages du joueur : la grille du fournisseur bouge. */
void deepseek_set_prices(double in_per_m, double out_per_m);

/* Effort de raisonnement ("low", "high", "max"). NULL ou vide : le champ
 * n'est pas envoye et le modele garde son comportement par defaut. */
void deepseek_set_reasoning(const char *level);

/* 0 = still pending, 1 = done (fills *out, frees req), -1 = failed (frees
 * req). Never blocks. */
int deepseek_poll(DeepseekRequest *req, DialogueReply *out);

/* Same contract, for a deepseek_ask_raw request: on success *out_text is a
 * malloc'd copy of the model's answer, which the caller frees. */
int deepseek_poll_raw(DeepseekRequest *req, char **out_text);

/* Between polls, to get a typewriter effect: the new characters of the
 * spoken line since the last call, or NULL. The emoji is never included. */
char *deepseek_poll_stream(DeepseekRequest *req);

/* The reply's emoji, once, as soon as it is known — which is before the
 * line finishes streaming, since "emotion" is requested first. */
char *deepseek_poll_emotion(DeepseekRequest *req);

#endif
