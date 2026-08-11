/* Verification hors ligne du chemin « piece a conviction produite -> creditee »
 * et du filtre de solubilite des elements a charge. Aucun appel reseau, aucune
 * fenetre : on rejoue a la main ce que fait apply_analysis (npc.c:900).
 *
 * Hors du Makefile (il ne sert pas au jeu). Depuis la racine du projet :
 *   make && gcc -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -Isrc \
 *     tests/test_clue.c obj/story.o obj/memory.o obj/prompt.o obj/json_min.o \
 *     obj/textutil.o obj/dialogue.o obj/globals.o -o /tmp/test_clue && /tmp/test_clue
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "story.h"
#include "memory.h"
#include "prompt.h"

static int fails = 0;

static void check(const char *what, bool ok) {
	printf("  %-62s %s\n", what, ok ? "OK" : "ECHEC");
	if (!ok) fails++;
}

int main(void) {
	char err[512];
	Story *s = story_load("ressources/projet_echo/projet_echo_story.json", err, sizeof(err));
	if (!s) { printf("story_load: %s\n", err); return 1; }
	printf("Histoire : %s (%d personnages, %d indices)\n\n",
	       s->title, s->nb_characters, s->nb_clues);

	/* ---- 1. La passe d'analyse peut signaler une piece produite ---- */
	printf("1. analysis_parse : produced_clue_id\n");
	const StoryCharacter *anton = story_character(s, "anton_kowalski");
	AnalysisResult a;
	const char *json_ok =
		"{\"remember\": true, \"type\": \"information\", \"summary\": \"J'ai montre "
		"l'architecture reseau a l'enqueteur.\", \"importance\": 4, \"emotion\": -1, "
		"\"tags\": [\"reseau\"], \"relationship_changes\": {\"trust\": 1}, "
		"\"player_learned_fact_ids\": [], \"revealed_secret_id\": null, "
		"\"produced_clue_id\": \"analyse_code\"}";
	check("un identifiant d'indice reel est retenu",
	      analysis_parse(json_ok, s, anton, &a) &&
	      a.produced_clue_id && strcmp(a.produced_clue_id, "analyse_code") == 0);
	analysis_free(&a);

	const char *json_bogus =
		"{\"remember\": false, \"produced_clue_id\": \"indice_qui_n_existe_pas\"}";
	check("un identifiant invente est ignore",
	      analysis_parse(json_bogus, s, anton, &a) && a.produced_clue_id == NULL);
	analysis_free(&a);

	/* ---- 2. Creditee : l'indice et TOUS les faits qu'il etablit ---- */
	printf("\n2. Creditement (ce que fait npc.c:900)\n");
	SaveState *st = save_new(s, "Test");
	if (!st) { printf("save_new a echoue\n"); return 1; }

	const Clue *cl = NULL;
	for (int i = 0; i < s->nb_clues; i++)
		if (strcmp(s->clues[i].id, "analyse_code") == 0) cl = &s->clues[i];
	check("l'indice existe et etablit au moins 2 faits", cl && cl->nb_reveals >= 2);
	if (!cl) return 1;

	int already = 0;
	for (int k = 0; k < cl->nb_reveals; k++)
		if (memory_player_knows_fact(st, cl->reveals_fact_ids[k])) already++;
	check("aucun de ses faits n'est connu au depart", already == 0);

	check("l'indice est marque decouvert", memory_player_discover_clue(st, "analyse_code"));
	check("un deuxieme creditement ne compte pas deux fois",
	      !memory_player_discover_clue(st, "analyse_code"));

	int learned = 0, visible = 0;
	for (int k = 0; k < cl->nb_reveals; k++) {
		if (memory_player_learn_fact(st, cl->reveals_fact_ids[k])) learned++;
		if (memory_player_knows_fact(st, cl->reveals_fact_ids[k])) visible++;
	}
	check("tous ses faits sont accordes au joueur", learned == cl->nb_reveals);
	check("et le carnet les voit", visible == cl->nb_reveals);

	/* ---- 3. Le filtre de solubilite, coupable par coupable ---- */
	printf("\n3. story_fact_obtainable_without, pour chaque coupable possible\n");
	for (int i = 0; i < s->nb_characters; i++) {
		const StoryCharacter *c = &s->characters[i];
		int reachable = 0, only_culprit = 0;

		for (int k = 0; k < c->nb_known_facts; k++) {
			if (story_fact_obtainable_without(s, c->known_fact_ids[k], c->id)) reachable++;
			else only_culprit++;
		}
		printf("  %-18s %2d faits connus : %2d atteignables sans lui, %2d verrouilles\n",
		       c->id, c->nb_known_facts, reachable, only_culprit);
	}

	/* Le vrai critere de solubilite : quel que soit le coupable tire au sort,
	 * chacun de ses faits doit avoir une autre source. Sinon le filtre de
	 * generate_solution() n'a rien a garder et l'enquete est verrouillee. */
	printf("\n4. Solubilite : un fait sans autre source, coupable par coupable\n");
	for (int i = 0; i < s->nb_characters; i++) {
		const StoryCharacter *c = &s->characters[i];
		char detail[512] = "";
		int locked = 0;

		for (int k = 0; k < c->nb_known_facts; k++) {
			if (story_fact_obtainable_without(s, c->known_fact_ids[k], c->id)) continue;
			locked++;
			snprintf(detail + strlen(detail), sizeof(detail) - strlen(detail),
			         "%s%s", locked > 1 ? ", " : "", c->known_fact_ids[k]);
		}
		char label[128];
		snprintf(label, sizeof(label), "%s coupable : tous ses faits ont une autre source", c->id);
		check(label, locked == 0);
		if (locked) printf("      verrouilles : %s\n", detail);
	}

	/* Le cas concret du diagnostic : les faits de la backdoor d'Anton. */
	printf("\n5. Le cas de l'interblocage (diagnostic du TODO)\n");
	check("fact_backdoor_echo est atteignable meme si Anton est coupable",
	      story_fact_obtainable_without(s, "fact_backdoor_echo", "anton_kowalski"));
	check("et aussi si quelqu'un d'autre est coupable",
	      story_fact_obtainable_without(s, "fact_backdoor_echo", "lea_sartori"));
	check("un fait que plusieurs personnes connaissent reste atteignable",
	      story_fact_obtainable_without(s, "fact_echo_fear_message", "anton_kowalski"));

	/* Coherence des donnees : une fiche qui se contredit passe le chargement
	 * sans bruit, et c'est le joueur qui le decouvre en jeu. */
	printf("\n6. Coherence de l'histoire\n");

	int contradictions = 0, secrets_hors_known = 0, orphelins = 0;
	for (int i = 0; i < s->nb_characters; i++) {
		const StoryCharacter *c = &s->characters[i];
		for (int k = 0; k < c->nb_known_facts; k++)
			for (int u = 0; u < c->nb_unknown_facts; u++)
				if (strcmp(c->known_fact_ids[k], c->unknown_fact_ids[u]) == 0) contradictions++;

		for (int x = 0; x < c->nb_secrets; x++) {
			if (!c->secrets[x].fact_id) continue;
			bool in_known = false;
			for (int k = 0; k < c->nb_known_facts; k++)
				if (strcmp(c->known_fact_ids[k], c->secrets[x].fact_id) == 0) in_known = true;
			if (!in_known) secrets_hors_known++;
		}
	}
	check("aucun fait a la fois connu et inconnu", contradictions == 0);
	check("chaque secret figure dans les faits connus de son personnage", secrets_hors_known == 0);

	/* Un fait que personne ne connait et qu'aucun indice n'etablit est hors
	 * d'atteinte definitive : c'etait le defaut n°1 du diagnostic. */
	for (int f = 0; f < s->nb_facts; f++) {
		const char *fid = s->facts[f].id;
		bool reachable = false;
		for (int i = 0; i < s->nb_characters && !reachable; i++)
			for (int k = 0; k < s->characters[i].nb_known_facts; k++)
				if (strcmp(s->characters[i].known_fact_ids[k], fid) == 0) reachable = true;
		for (int i = 0; i < s->nb_clues && !reachable; i++)
			for (int k = 0; k < s->clues[i].nb_reveals; k++)
				if (strcmp(s->clues[i].reveals_fact_ids[k], fid) == 0) reachable = true;
		if (!reachable) { printf("      orphelin : %s\n", fid); orphelins++; }
	}
	check("aucun fait orphelin (ni detenteur ni indice)", orphelins == 0);

	save_free(st);
	story_free(s);
	printf("\n%s\n", fails ? "DES VERIFICATIONS ONT ECHOUE" : "Tout est passe.");
	return fails ? 1 : 0;
}
