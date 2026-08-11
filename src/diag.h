#ifndef DIAG_H
#define DIAG_H

#include <stdbool.h>

#include "npc.h"
#include "prompt.h"

struct s_game;

/* Journal de diagnostic volontaire, desactive par defaut. Les fichiers sont
 * prives (0600) car ils contiennent les prompts, la verite de l'enquete et les
 * conversations du joueur. */
int         diag_init(bool enabled);
bool        diag_enabled(void);
const char *diag_path(void);
void        diag_close(void);

long diag_api_request(const char *purpose, const char *model, const char *body);
void diag_api_response(long request_id, const char *purpose, const char *model,
                       long http_status, int curl_code, long duration_ms,
                       int attempts, const char *curl_error,
                       const char *provider_response, const char *model_text,
                       bool usable, bool requested_output_parsed);

void diag_game_state(const struct s_game *game, const char *reason);
void diag_dialogue(const struct s_game *game, int npc_idx, const char *question,
                   const char *line, const char *emotion, const char *action,
                   const char *move, NpcMoveResult move_result, bool interjection,
                   int x_before, int y_before, int room_before,
                   bool moving_before, int dest_x_before, int dest_y_before);
void diag_analysis(const struct s_game *game, int npc_idx,
                   const AnalysisResult *analysis,
                   int facts_before, int clues_before, Relation relation_before);
void diag_analysis_rejected(const struct s_game *game, int npc_idx,
                            const char *model_text);

#endif
