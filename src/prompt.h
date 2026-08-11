#ifndef PROMPT_H
#define PROMPT_H

#include "memory.h"
#include "story.h"

/* Builds the system prompt for one character, in the order laid out by
 * Memory_instructions.md §18: personality, world knowledge, relationship,
 * relevant memories, then the reply format.
 *
 * The static half (personality + knowledge + rules) is emitted first and the
 * volatile half (relationship + memories) last, so the long prefix stays
 * byte-identical from one turn to the next and the provider can cache it.
 *
 * Marks the selected memories as recalled, so it is not a read-only call.
 * Returns a malloc'd string. */
/* Ce que le personnage a physiquement sous les yeux au moment de repondre :
 * ou il est, qui est la, et ou il aurait le droit d'aller. Le moteur remplit
 * cette structure depuis la carte, donc le modele ne peut proposer que des
 * deplacements qui existent vraiment. */
#define PROMPT_MAX_SCENE 16

typedef struct {
	bool  can_move, can_change_room;
	const char *room_here;

	const char *room_ids[PROMPT_MAX_SCENE];
	const char *room_names[PROMPT_MAX_SCENE];
	int   nb_rooms;

	const char *other_ids[PROMPT_MAX_SCENE];
	const char *other_names[PROMPT_MAX_SCENE];
	int   nb_others;

	/* Les autres personnes presentes dans la piece, telles que l'enqueteur
	 * les connait (« un inconnu » tant qu'elles ne se sont pas nommees). */
	const char *present_names[PROMPT_MAX_SCENE];
	int   nb_present;

	/* Premiere prise de parole avec l'enqueteur : on n'accueille pas un inconnu
	 * comme on reprend une conversation entamee. Et tant que le joueur n'a pas
	 * son nom, le personnage ne doit pas parler comme s'il l'avait. */
	bool  first_meeting;
	bool  name_known_by_player;

	/* Intervention : le personnage n'est pas interroge, il assiste a un
	 * echange entre l'enqueteur et quelqu'un d'autre. Il a le droit de se
	 * taire, ce qui n'est jamais le cas quand on s'adresse a lui. */
	bool        overheard;
	const char *overheard_speaker;

	/* Cas particulier d'intervention : il est venu expres parler a quelqu'un,
	 * il a donc bien quelque chose a dire. Celui qui surprend une conversation
	 * n'a, lui, rien a livrer. */
	bool        came_to_speak;
} PromptScene;

char *prompt_build_dialogue(const Story *story, SaveState *save,
                            const StoryCharacter *ch, const char *player_message,
                            const PromptScene *ctx);

/* Prompt for the background analysis pass (§11): asks the model what, if
 * anything, the character should remember from the exchange, how the
 * relationship moved, and which authored facts the player just learned.
 * `known_fact_ids` are offered as the only ids it may return. */
char *prompt_build_analysis(const Story *story, const StoryCharacter *ch,
                            const char *player_message, const char *npc_reply);

/* Parsed result of that analysis call. Fact ids are validated against the
 * story before being kept, so the model can never invent one. */
#define ANALYSIS_MAX_TAGS  8
#define ANALYSIS_MAX_FACTS 8

typedef struct {
	/* Faux quand l'echange contient une personne, un document ou un fait que
	 * rien dans la fiche n'autorise. Un tel echange a pu etre affiche, mais il
	 * ne doit jamais devenir une nouvelle verite durable dans la sauvegarde. */
	bool  grounded;
	bool  remember;
	char *type;
	char *summary;
	int   importance;
	int   emotion;
	char *tags[ANALYSIS_MAX_TAGS];
	int   nb_tags;
	int   trust_d, affection_d, fear_d, suspicion_d;
	char *learned_fact_ids[ANALYSIS_MAX_FACTS];
	int   nb_learned_facts;
	char *revealed_secret_id;

	/* Piece a conviction que le personnage vient de produire (logs, mail,
	 * rapport...). Les indices de l'histoire etaient du contenu mort : rien
	 * dans le moteur ne les distribuait, donc les faits qu'ils revelent
	 * etaient hors d'atteinte, et un personnage qui disait « je vous sors les
	 * logs » ne produisait jamais rien. */
	char *produced_clue_id;
} AnalysisResult;

/* Vrai si ce fait peut etre obtenu autrement que par la bouche du coupable :
 * un autre personnage le connait, ou un indice le revele. Sert a garantir que
 * l'enquete est resoluble. */
bool story_fact_obtainable_without(const Story *s, const char *fact_id,
                                   const char *culprit_id);

/* Returns true when the JSON could be read at all. Unknown fact ids and
 * secret ids are dropped rather than trusted. */
bool analysis_parse(const char *json_text, const Story *story,
                    const StoryCharacter *ch, AnalysisResult *out);
void analysis_free(AnalysisResult *a);

/* ---- Etat des preuves contre le coupable ---- */

/* Combien des faits qui incriminent le coupable le joueur a reellement
 * etablis, et combien il en faut pour que le coupable finisse par craquer.
 * Le moteur garde la main : le modele ne recoit qu'une consigne, jamais le
 * compte, et ne peut donc pas decider seul que l'enquete est finie. */
int  culprit_evidence_count(const SaveState *save);
int  culprit_evidence_needed(const SaveState *save);
bool culprit_is_cornered(const SaveState *save);

/* ---- Resolution de l'enquete ---- */

/* Once the culprit has been drawn, the whole case has to actually add up:
 * the authored facts are deliberately ambiguous and name nobody. This asks
 * the model to derive, from the facts/timeline/clues alone, how and why that
 * character did it — so the mystery is coherent and solvable, and the culprit
 * can lie consistently instead of improvising. Run once per new game. */
char *prompt_build_solution(const Story *story, const char *culprit_id);

typedef struct {
	char  *solution;                          /* la verite, pour la revelation */
	char  *culprit_brief;                     /* ce que le coupable doit cacher */
	char  *fact_ids[ANALYSIS_MAX_FACTS];      /* faits qui l'incriminent */
	int    nb_facts;
} SolutionResult;

bool solution_parse(const char *json_text, const Story *story, SolutionResult *out);
void solution_free(SolutionResult *s);

/* ---- Combler les trous de solubilite, au lancement ---- */

/* Le moteur sait exactement ce qui manque : un fait qui accuse le coupable et
 * que personne d'autre ne peut donner rend l'enquete injouable (il faut la
 * preuve pour obtenir l'aveu, et l'aveu pour obtenir la preuve). Il n'y a rien a
 * deviner la-dessus, donc le modele ne sert qu'a ECRIRE : pour chaque fait
 * verrouille qu'on lui donne, une piece a conviction dans le ton de l'histoire,
 * et le second personnage qui la detient — choisi parmi ceux que le moteur
 * propose, jamais le coupable.
 * `missing` : les faits verrouilles. Renvoie NULL s'il n'y en a aucun (le cas
 * d'une histoire bien ecrite : aucun appel n'est alors emis). */
char *prompt_build_clue_fill(const Story *story, const char *culprit_id,
                             const char **missing, int nb_missing);

#define CLUE_FILL_MAX 8

typedef struct {
	char *clue_id;
	char *name;
	char *description;
	char *fact_id;      /* le fait verrouille que cette piece etablit */
	char *holder_id;    /* le personnage qui la detient (jamais le coupable) */
} ClueFill;

typedef struct {
	ClueFill items[CLUE_FILL_MAX];
	int      nb_items;
} ClueFillResult;

/* Ne garde que ce qui est utilisable : un fait qui existe et faisait bien partie
 * des trous demandes, un detenteur qui existe, est place sur la carte et n'est
 * pas le coupable. Le reste est jete sans bruit — le moteur reverifie de toute
 * facon la solubilite apres application. */
bool clue_fill_parse(const char *json_text, const Story *story, const char *culprit_id,
                     const char **missing, int nb_missing, ClueFillResult *out);
void clue_fill_free(ClueFillResult *r);

#endif
