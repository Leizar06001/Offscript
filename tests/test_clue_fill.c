/* Verification hors ligne du comblement des trous de solubilite : le moteur
 * detecte ce qui manque, le modele n'ecrit que ca, le moteur valide, applique et
 * conserve. Aucun appel reseau : la reponse du modele est ecrite a la main.
 *
 * Hors du Makefile. Depuis la racine du projet :
 *   make && gcc -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -Isrc \
 *     tests/test_clue_fill.c obj/story.o obj/memory.o obj/prompt.o obj/json_min.o \
 *     obj/textutil.o obj/dialogue.o obj/globals.o -o /tmp/test_fill && /tmp/test_fill
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "story.h"
#include "memory.h"
#include "prompt.h"

static int fails = 0;

static void check(const char *what, bool ok) {
	printf("  %-64s %s\n", what, ok ? "OK" : "ECHEC");
	if (!ok) fails++;
}

/* Une histoire volontairement verrouillee : le coupable est le seul a connaitre
 * ce qui l'accuse. C'est le cas que le comblement doit reparer. */
static const char *STORY_JSON =
"{\"schema_version\":\"1.0\","
" \"story\":{\"id\":\"test_bloque\",\"title\":\"Test bloque\",\"language\":\"fr\","
"   \"setting\":{\"year\":2030,\"location\":\"Un entrepot\"},"
"   \"premise\":\"Quelqu'un est mort.\",\"player_title\":\"Detective\"},"
" \"facts\":{"
"   \"f_public\":{\"text\":\"La victime est morte a 22h.\",\"truth\":true},"
"   \"f_secret\":{\"text\":\"Le badge du coupable a ouvert la porte a 21h58.\",\"truth\":true}},"
" \"characters\":["
"   {\"id\":\"cul\",\"name\":\"Coupable\",\"role\":\"Gardien\","
"    \"knowledge\":{\"known_fact_ids\":[\"f_public\",\"f_secret\"]},"
"    \"game\":{\"room\":\"hall\"}},"
"   {\"id\":\"temoin\",\"name\":\"Temoin\",\"role\":\"Technicien\","
"    \"knowledge\":{\"known_fact_ids\":[\"f_public\"]},"
"    \"game\":{\"room\":\"hall\"}}],"
" \"clues\":[],"
" \"map\":{\"width\":10,\"rows\":[\"2222222222\",\"2        2\",\"2        2\",\"2222222222\"],"
"   \"player_start\":{\"x\":3,\"y\":2},"
"   \"rooms\":[{\"id\":\"hall\",\"name\":\"Hall\",\"at\":{\"x\":3,\"y\":1}}]},"
" \"mystery\":{\"victim\":{\"id\":\"v\",\"name\":\"Victime\"},"
"   \"crime\":{\"type\":\"homicide\",\"location\":\"Hall\"},"
"   \"suspect_ids\":[\"cul\",\"temoin\"],"
"   \"solution\":{\"culprit_id\":\"cul\"}}}";

int main(void) {
	const char *path = "/tmp/test_bloque_story.json";
	FILE *f = fopen(path, "w");
	if (!f) { printf("ecriture impossible\n"); return 1; }
	fputs(STORY_JSON, f);
	fclose(f);

	char err[512];
	Story *s = story_load(path, err, sizeof(err));
	if (!s) { printf("story_load : %s\n", err); return 1; }

	/* ---- 1. Le moteur voit le trou ---- */
	printf("1. Detection du trou\n");
	check("f_secret est inatteignable sans le coupable",
	      !story_fact_obtainable_without(s, "f_secret", "cul"));
	check("f_public, lui, est atteignable",
	      story_fact_obtainable_without(s, "f_public", "cul"));

	/* ---- 2. Le prompt ne demande QUE ce qui manque ---- */
	printf("\n2. Prompt de comblement\n");
	const char *missing[] = { "f_secret" };
	char *p = prompt_build_clue_fill(s, "cul", missing, 1);
	check("un prompt est produit", p != NULL);
	check("il nomme le fait a couvrir", p && strstr(p, "f_secret") != NULL);
	check("il ne propose PAS le coupable comme detenteur",
	      p && strstr(p, "- cul :") == NULL);
	check("il propose le temoin", p && strstr(p, "- temoin :") != NULL);
	check("aucun appel n'est emis quand il n'y a rien a combler",
	      prompt_build_clue_fill(s, "cul", NULL, 0) == NULL);
	free(p);

	/* ---- 3. Le moteur ne gobe pas n'importe quoi ---- */
	printf("\n3. Validation de la reponse\n");
	ClueFillResult r;

	check("un fait invente est rejete",
	      !clue_fill_parse("{\"clues\":[{\"fact_id\":\"f_invente\",\"id\":\"x\","
	                       "\"name\":\"X\",\"description\":\"d\",\"holder_id\":\"temoin\"}]}",
	                       s, "cul", missing, 1, &r));
	clue_fill_free(&r);

	check("un fait hors du perimetre demande est rejete",
	      !clue_fill_parse("{\"clues\":[{\"fact_id\":\"f_public\",\"id\":\"x\","
	                       "\"name\":\"X\",\"description\":\"d\",\"holder_id\":\"temoin\"}]}",
	                       s, "cul", missing, 1, &r));
	clue_fill_free(&r);

	check("le coupable comme detenteur est rejete",
	      !clue_fill_parse("{\"clues\":[{\"fact_id\":\"f_secret\",\"id\":\"x\","
	                       "\"name\":\"X\",\"description\":\"d\",\"holder_id\":\"cul\"}]}",
	                       s, "cul", missing, 1, &r));
	clue_fill_free(&r);

	check("un detenteur inconnu est rejete",
	      !clue_fill_parse("{\"clues\":[{\"fact_id\":\"f_secret\",\"id\":\"x\","
	                       "\"name\":\"X\",\"description\":\"d\",\"holder_id\":\"fantome\"}]}",
	                       s, "cul", missing, 1, &r));
	clue_fill_free(&r);

	const char *good =
	    "{\"clues\":[{\"fact_id\":\"f_secret\",\"id\":\"journal_badges\","
	    "\"name\":\"Journal des badges\",\"description\":\"L'ouverture de 21h58.\","
	    "\"holder_id\":\"temoin\"}]}";
	check("une reponse correcte est acceptee",
	      clue_fill_parse(good, s, "cul", missing, 1, &r) && r.nb_items == 1);

	/* ---- 4. Application dans l'histoire en memoire ---- */
	printf("\n4. Application\n");
	SaveState *st = save_new(s, "Test");
	st->gen_clues = calloc(1, sizeof(GeneratedClue));
	st->gen_clues[0].id          = strdup(r.items[0].clue_id);
	st->gen_clues[0].name        = strdup(r.items[0].name);
	st->gen_clues[0].description = strdup(r.items[0].description);
	st->gen_clues[0].reveals_fact_ids = calloc(1, sizeof(char *));
	st->gen_clues[0].reveals_fact_ids[0] = strdup(r.items[0].fact_id);
	st->gen_clues[0].nb_reveals  = 1;
	st->nb_gen_clues = 1;
	st->extra_knowledge = calloc(1, sizeof(ExtraKnowledge));
	st->extra_knowledge[0].npc_id  = strdup(r.items[0].holder_id);
	st->extra_knowledge[0].fact_id = strdup(r.items[0].fact_id);
	st->nb_extra_knowledge = 1;
	clue_fill_free(&r);

	int clues_avant = s->nb_clues;
	story_apply_additions(s, st);
	check("l'indice rejoint ceux de l'histoire", s->nb_clues == clues_avant + 1);
	check("le second detenteur connait le fait",
	      story_character(s, "temoin")->nb_known_facts == 2);
	check("LE TROU EST COMBLE : f_secret est atteignable sans le coupable",
	      story_fact_obtainable_without(s, "f_secret", "cul"));

	story_apply_additions(s, st);   /* deux fois : le chargement + la generation */
	check("un second appel n'ajoute pas de doublon", s->nb_clues == clues_avant + 1);
	check("ni de fait en double", story_character(s, "temoin")->nb_known_facts == 2);

	/* ---- 5. Ca survit au rechargement ---- */
	printf("\n5. Persistance\n");
	const char *save_path = "/tmp/test_bloque_save.json";
	check("la sauvegarde s'ecrit", save_write(st, save_path));

	Story *s2 = story_load(path, err, sizeof(err));
	SaveState *st2 = save_load(s2, save_path);
	check("elle se relit", st2 != NULL);
	check("l'indice genere est conserve", st2 && st2->nb_gen_clues == 1);
	check("le fait donne au temoin aussi", st2 && st2->nb_extra_knowledge == 1);

	check("avant application, l'histoire rechargee est de nouveau verrouillee",
	      !story_fact_obtainable_without(s2, "f_secret", "cul"));
	story_apply_additions(s2, st2);
	check("apres application, elle est de nouveau soluble",
	      story_fact_obtainable_without(s2, "f_secret", "cul"));

	save_free(st); save_free(st2);
	story_free(s); story_free(s2);
	printf("\n%s\n", fails ? "DES VERIFICATIONS ONT ECHOUE" : "Tout est passe.");
	return fails ? 1 : 0;
}
