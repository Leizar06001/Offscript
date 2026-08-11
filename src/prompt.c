#include "prompt.h"
#include "dialogue.h"
#include "json_min.h"
#include "textutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_list(StrBuf *sb, const char *label, char **items, int n) {
	if (n <= 0) return;
	sb_addf(sb, "%s :", label);
	for (int i = 0; i < n; i++) sb_addf(sb, "\n- %s", items[i]);
	sb_add(sb, "\n\n");
}

/* ------------------------------------------------------------------ */
/* Ce que le joueur peut reellement obtenir                            */
/* ------------------------------------------------------------------ */

/* Detenteur d'une piece a conviction : celui qui connait l'un des faits qu'elle
 * etablit. C'est lui qui a les rapports, les journaux ou le message, donc lui
 * seul peut la sortir. Sert au prompt de dialogue comme a celui d'analyse : les
 * deux doivent voir exactement la meme liste. */
static bool character_holds_clue(const StoryCharacter *ch, const Clue *cl) {
	for (int k = 0; k < cl->nb_reveals; k++) {
		for (int f = 0; f < ch->nb_known_facts; f++) {
			if (strcmp(ch->known_fact_ids[f], cl->reveals_fact_ids[k]) == 0) return true;
		}
	}
	return false;
}

/* Un personnage absent de la carte ne peut rien dire a personne : le compter
 * comme source rendait « atteignable » un fait que le joueur ne pouvait pas
 * obtenir. */
static bool character_can_tell(const StoryCharacter *c, const char *fact_id,
                              const char *culprit_id) {
	if (!c->placed) return false;
	if (culprit_id && c->id && strcmp(c->id, culprit_id) == 0) return false;
	for (int k = 0; k < c->nb_known_facts; k++) {
		if (strcmp(c->known_fact_ids[k], fact_id) == 0) return true;
	}
	return false;
}

/* Une piece a conviction n'apparait pas toute seule : c'est un personnage qui
 * la sort, et le prompt ne la lui propose que s'il connait l'un des faits
 * qu'elle etablit (voir « CE QUE TU PEUX MONTRER A L'ENQUETEUR »). Une piece
 * que seul le coupable detient n'est donc pas un chemin de secours. */
static bool clue_producible_without(const Story *s, const Clue *cl,
                                    const char *culprit_id) {
	if (!cl->discoverable) return false;

	for (int i = 0; i < s->nb_characters; i++) {
		const StoryCharacter *c = &s->characters[i];
		if (!c->placed) continue;
		if (culprit_id && c->id && strcmp(c->id, culprit_id) == 0) continue;

		for (int k = 0; k < cl->nb_reveals; k++) {
			for (int f = 0; f < c->nb_known_facts; f++)
				if (strcmp(c->known_fact_ids[f], cl->reveals_fact_ids[k]) == 0) return true;
			for (int x = 0; x < c->nb_secrets; x++)
				if (c->secrets[x].fact_id &&
				    strcmp(c->secrets[x].fact_id, cl->reveals_fact_ids[k]) == 0) return true;
		}
	}
	return false;
}

bool story_fact_obtainable_without(const Story *s, const char *fact_id,
                                   const char *culprit_id) {
	if (!s || !fact_id) return false;

	/* Quelqu'un d'autre le sait et peut le dire. */
	for (int i = 0; i < s->nb_characters; i++) {
		if (character_can_tell(&s->characters[i], fact_id, culprit_id)) return true;
	}

	/* Ou une piece a conviction l'etablit, et quelqu'un d'autre peut la sortir. */
	for (int i = 0; i < s->nb_clues; i++) {
		const Clue *cl = &s->clues[i];
		for (int k = 0; k < cl->nb_reveals; k++) {
			if (strcmp(cl->reveals_fact_ids[k], fact_id) != 0) continue;
			if (clue_producible_without(s, cl, culprit_id)) return true;
		}
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* Etat des preuves contre le coupable                                 */
/* ------------------------------------------------------------------ */

/* Le moteur reste seul maitre de la fin de l'enquete : il compte, parmi les
 * faits qui incriminent le coupable, ceux que le joueur a reellement etablis.
 * Le modele ne voit jamais ce compte, il ne recoit qu'une consigne. */
int culprit_evidence_count(const SaveState *save) {
	if (!save || !save->culprit_id) return 0;
	int n = 0;
	for (int i = 0; i < save->nb_culprit_facts; i++) {
		if (memory_player_knows_fact(save, save->culprit_fact_ids[i])) n++;
	}
	return n;
}

int culprit_evidence_needed(const SaveState *save) {
	if (!save || save->nb_culprit_facts <= 0) return 0;
	/* La moitie des elements a charge, et jamais moins de deux : un seul fait
	 * peut etre une coincidence, l'aveu doit se meriter. */
	int need = (save->nb_culprit_facts + 1) / 2;
	if (need < 2) need = 2;
	if (need > save->nb_culprit_facts) need = save->nb_culprit_facts;
	return need;
}

bool culprit_is_cornered(const SaveState *save) {
	int need = culprit_evidence_needed(save);
	return need > 0 && culprit_evidence_count(save) >= need;
}

/* ------------------------------------------------------------------ */
/* Prompt de dialogue                                                  */
/* ------------------------------------------------------------------ */

char *prompt_build_dialogue(const Story *story, SaveState *save,
                            const StoryCharacter *ch, const char *player_message,
                            const PromptScene *ctx) {
	StrBuf sb;
	sb_init(&sb);

	/* ---- Partie stable : identique d'un tour a l'autre ---- */

	sb_addf(&sb, "Tu es %s", ch->name);
	if (ch->role && *ch->role) sb_addf(&sb, ", %s", ch->role);
	if (ch->identity_age && *ch->identity_age) sb_addf(&sb, " (%s)", ch->identity_age);
	sb_add(&sb, ".\n");
	if (ch->identity_model && *ch->identity_model) sb_addf(&sb, "Modele : %s.\n", ch->identity_model);
	if (ch->public_description && *ch->public_description)
		sb_addf(&sb, "%s\n", ch->public_description);
	sb_add(&sb, "\n");

	/* Le decor et l'affaire, tels que l'auteur les a ecrits. */
	sb_add(&sb, "CONTEXTE\n");
	if (story->location) sb_addf(&sb, "Lieu : %s", story->location);
	if (story->year) sb_addf(&sb, " (%d)", story->year);
	sb_add(&sb, "\n");
	if (story->premise) sb_addf(&sb, "%s\n", story->premise);
	if (story->victim_name) {
		sb_addf(&sb, "Victime : %s", story->victim_name);
		if (story->victim_role) sb_addf(&sb, ", %s", story->victim_role);
		sb_add(&sb, ".\n");
	}
	if (story->crime_type && story->crime_location) {
		sb_addf(&sb, "Fait : %s dans %s", story->crime_type, story->crime_location);
		if (story->crime_time_start && story->crime_time_end)
			sb_addf(&sb, " entre %s et %s", story->crime_time_start, story->crime_time_end);
		sb_add(&sb, ".\n");
	}
	sb_addf(&sb, "%s mene l'enquete dans le batiment.\n",
	        save->player_name ? save->player_name : "L'enqueteur");
	/* Qui l'interroge, et devant qui, est ecrit plus bas avec le reste de la
	 * scene : ces deux lignes changeaient a chaque fois que quelqu'un entrait
	 * dans la piece, au beau milieu de la partie stable du prompt. Le fournisseur
	 * ne peut mettre en cache qu'un prefixe : il ne restait plus que 11% de
	 * commun d'un tour a l'autre, et tout le reste etait refacture. */
	sb_add(&sb, "\n");

	if (ch->personality)      add_list(&sb, "TON CARACTERE", ch->personality, ch->nb_personality);
	if (ch->position_in_case && *ch->position_in_case)
		sb_addf(&sb, "TA POSITION DANS L'AFFAIRE :\n%s\n\n", ch->position_in_case);
	if (ch->motives)          add_list(&sb, "CE QUI POURRAIT TE MOTIVER", ch->motives, ch->nb_motives);

	/* "alibi": null est legitime (Echo n'en a pas): on n'ecrit rien. */
	if (ch->alibi_claim && *ch->alibi_claim) {
		sb_addf(&sb, "TON ALIBI (ce que tu affirmes) :\n%s\n", ch->alibi_claim);
		if (ch->alibi_weakness && *ch->alibi_weakness)
			sb_addf(&sb, "Sa faille, que tu ne mentionnes jamais de toi-meme : %s\n", ch->alibi_weakness);
		sb_add(&sb, "\n");
	}

	/* Uniquement ce que le personnage sait. Les unknown_fact_ids ne sont
	 * jamais envoyes : les nommer, meme pour les interdire, reviendrait a
	 * apprendre au modele qu'ils existent. */
	if (ch->nb_known_facts > 0) {
		sb_add(&sb, "CE QUE TU SAIS :");
		for (int i = 0; i < ch->nb_known_facts; i++) {
			const Fact *f = story_fact(story, ch->known_fact_ids[i]);
			if (f && f->text) sb_addf(&sb, "\n- %s", f->text);
		}
		sb_add(&sb, "\nTu ignores tout le reste de l'enquete. N'invente aucune autre information.\n");

		/* Rien n'invitait le personnage a PARTAGER ce qu'il sait : il n'avait
		 * que des consignes de retenue, et repondait donc a cote pendant des
		 * dizaines d'echanges. Un temoin qui n'a rien a cacher sur un point
		 * precis doit le dire, sinon l'enquete ne peut pas avancer. */
		sb_add(&sb, "Sauf pour ce que tu caches (plus bas), ces elements ne sont pas des "
		            "secrets : quand l'enqueteur pose une question qui touche l'un d'eux, tu "
		            "reponds CLAIREMENT et tu donnes l'information, avec tes mots et ton "
		            "caractere. Tu es un temoin, pas un adversaire. Ne repond pas par des "
		            "generalites, ne renvoie pas indefiniment vers quelqu'un d'autre ou vers "
		            "un document a consulter plus tard : ce que tu sais, tu le dis.\n");

		/* Contrepoids indispensable au paragraphe ci-dessus : sans lui, les
		 * personnages deballaient tout ce qu'ils savaient a la premiere phrase,
		 * heures et identifiants compris, et l'enquete se resolvait sans qu'on
		 * ait rien demande. La regle est la pertinence, pas un quota : une
		 * question qui touche trois elements en obtient trois. */
		sb_add(&sb, "MAIS tu ne reponds QU'A CE QUI EST DEMANDE. Tu ne livres jamais de "
		            "toi-meme un element que l'enqueteur n'a pas cherche : tu ne changes pas "
		            "de sujet pour le placer, tu n'ajoutes pas de « et aussi... », tu ne fais "
		            "l'inventaire de rien. S'il te parle d'autre chose, ou de rien, tu n'as "
		            "aucune information a donner. Une heure, un chiffre, un identifiant, un "
		            "nom de fichier ou un nom de personne ne sortent de ta bouche que si la "
		            "question porte dessus. C'est a l'enqueteur de trouver quoi te demander : "
		            "tu ne fais pas son travail a sa place.\n\n");
	}

	/* Secrets : ce qu'il cache, et ce qu'il a deja lache. */
	if (ch->nb_secrets > 0) {
		sb_add(&sb, "CE QUE TU CACHES :\n");
		for (int i = 0; i < ch->nb_secrets; i++) {
			const Secret *sec = &ch->secrets[i];
			const Fact *f = story_fact(story, sec->fact_id);
			if (!f || !f->text) continue;

			bool already = memory_secret_revealed(save, ch->id, sec->id);
			sb_addf(&sb, "- %s\n", f->text);
			if (already) {
				sb_add(&sb, "  (tu l'as deja avoue a l'enqueteur : inutile de le renier)\n");
			} else if (sec->can_hide) {
				sb_add(&sb, "  (tu le nies ou tu detournes, sauf si on te met devant une preuve precise)\n");
			}
		}
		sb_add(&sb, "\n");
	}

	/* Les pieces a conviction que ce personnage detient. Un personnage est
	 * considere comme detenteur d'une piece des lors qu'il connait l'un des
	 * faits qu'elle etablit : c'est lui qui a les rapports, les journaux ou le
	 * message. Sans cette section, tout le monde refusait poliment de sortir
	 * quoi que ce soit (« documents sous embargo »), et les faits que ces
	 * pieces revelent restaient inaccessibles toute la partie. */
	if (story->nb_clues > 0) {
		bool any = false;
		for (int i = 0; i < story->nb_clues; i++) {
			const Clue *cl = &story->clues[i];
			if (!cl->discoverable) continue;

			if (!character_holds_clue(ch, cl)) continue;

			bool is_secret = false;
			for (int k = 0; k < cl->nb_reveals; k++) {
				for (int s = 0; s < ch->nb_secrets; s++)
					if (ch->secrets[s].fact_id &&
					    strcmp(ch->secrets[s].fact_id, cl->reveals_fact_ids[k]) == 0) is_secret = true;
			}

			if (!any) {
				sb_add(&sb, "CE QUE TU PEUX MONTRER A L'ENQUETEUR :\n");
				any = true;
			}
			sb_addf(&sb, "- %s : %s%s\n", cl->name,
			        cl->description ? cl->description : "",
			        is_secret ? "  (cela t'accuse : tu ne le sors que contraint et force)" : "");
		}
		if (any) {
			sb_add(&sb, "Tu ne les mentionnes jamais de toi-meme : c'est a l'enqueteur de "
			            "penser a les demander. ");
			sb_add(&sb, "Tu as reellement acces a ces elements, ici et maintenant. Si "
			            "l'enqueteur te les demande, tu les SORS et tu dis ce qu'ils "
			            "contiennent, dans la meme replique — sauf pour ceux qui "
			            "t'accusent. Ne repond pas que c'est confidentiel, qu'il faut une "
			            "autorisation, ou que tu le feras plus tard : une enquete pour "
			            "meurtre est en cours et tu cooperes.\n\n");
		}
	}

	if (ch->dialogue_tone && *ch->dialogue_tone)
		sb_addf(&sb, "TON DE VOIX : %s\n", ch->dialogue_tone);
	if (ch->speech_rules) {
		add_list(&sb, "REGLES DE PAROLE", ch->speech_rules, ch->nb_speech_rules);
		/* Ces regles disent COMMENT parler. Prises pour des ordres de tout dire
		 * (« Donne les heures, noms de fichiers et traces avec precision »),
		 * elles faisaient deballer horaires et identifiants a des personnages a
		 * qui on n'avait rien demande. */
		sb_add(&sb, "Ces regles decrivent ta facon de parler quand tu reponds : elles ne "
		            "t'autorisent jamais a livrer de toi-meme une information qu'on ne t'a "
		            "pas demandee.\n\n");
	}

	if (story->ai_global) add_list(&sb, "REGLES ABSOLUES", story->ai_global, story->nb_ai_global);

	/* Le coupable tire au sort sait qu'il l'est, sinon les suspects se
	 * comporteraient tous exactement de la meme facon. Il recoit le brief
	 * genere au debut de la partie, ce qui lui permet de mentir de facon
	 * coherente sans avoir a inventer un deroulement. */
	if (save->culprit_id && ch->id && strcmp(save->culprit_id, ch->id) == 0) {
		sb_add(&sb, "VERITE PRIVEE (jamais avouee spontanement) :\n");
		if (save->culprit_brief && *save->culprit_brief) {
			sb_addf(&sb, "%s\n", save->culprit_brief);
		} else {
			sb_add(&sb, "C'est toi le coupable.\n");
		}
		sb_add(&sb, "Tu ne l'admets sous aucun pretexte tant que l'enqueteur ne t'oppose "
		            "pas une preuve materielle irrefutable. Tu t'en tiens strictement a ce "
		            "qui est ecrit ci-dessus : tu n'inventes aucun autre detail. Tu nies, tu "
		            "minimises, tu renvoies l'attention ailleurs ; a bout d'arguments, tu "
		            "preferes te taire.\n\n");

		/* L'aveu ne devient possible que lorsque le joueur a reuni assez
		 * d'elements a charge. Tant que ce n'est pas le cas, la consigne
		 * ci-dessus tient et le coupable nie : c'est ce qui empeche de gagner
		 * en accusant au hasard. */
		if (culprit_is_cornered(save)) {
			sb_add(&sb, "CE QUE L'ENQUETEUR A DEJA ETABLI CONTRE TOI :\n");
			for (int i = 0; i < save->nb_culprit_facts; i++) {
				if (!memory_player_knows_fact(save, save->culprit_fact_ids[i])) continue;
				const Fact *f = story_fact(story, save->culprit_fact_ids[i]);
				if (f && f->text) sb_addf(&sb, "- %s\n", f->text);
			}
			sb_add(&sb,
				"Ces elements sont entre ses mains et tu le sais. Tu tiens encore, mais tu "
				"es a bout. S'il te les oppose directement, ou s'il t'accuse en s'appuyant "
				"dessus, tu craques : tu avoues le meurtre, tu dis pourquoi tu l'as fait, "
				"sans te justifier longuement.\n"
				"UNIQUEMENT dans ce cas, ajoute la cle \"confession\": true a ton objet "
				"JSON. Tant que tu n'avoues pas, ne mets jamais cette cle.\n\n");
		}
	} else {
		sb_add(&sb, "VERITE PRIVEE : tu n'as pas tue la victime. Tu peux malgre tout "
		            "mentir pour proteger ton secret personnel.\n\n");
	}

	/* Premiere fois qu'il parle a l'enqueteur, ou reprise d'un echange deja
	 * entame. Sans cette distinction, un personnage se presentait a chaque
	 * replique, ou accueillait comme un habitue quelqu'un qu'il n'avait jamais
	 * vu. Bascule une seule fois par partie : reste donc dans la partie stable,
	 * ou il ne coute le prix plein qu'une fois. */
	if (ctx && ctx->first_meeting) {
		sb_add(&sb,
			"PREMIER CONTACT : tu n'as encore jamais parle a cet enqueteur. Tu ne sais "
			"de lui que ce que tu vois. Tu reagis comme on reagit a un inconnu qui vient "
			"vous interroger — surprise, mefiance, politesse de facade, agacement, "
			"empressement — selon TON CARACTERE.\n"
			"Te presenter ou non est ton choix, et il decoule de ton caractere : "
			"quelqu'un d'ouvert, de poli ou de cooperatif donne son nom des la premiere "
			"phrase ; quelqu'un de ferme, de presse, de mefiant ou d'hostile repond sans "
			"se nommer. Si tu ne te presentes pas, ne donne ton nom sous aucune forme "
			"dans cette replique.\n\n");
	} else {
		sb_add(&sb,
			"VOUS VOUS ETES DEJA PARLE : tu ne te presentes pas, tu ne redis pas bonjour "
			"comme si vous vous rencontriez, tu ne reexpliques pas qui tu es. Tu reprends "
			"la conversation la ou elle s'est arretee.\n\n");
	}
	if (ctx && !ctx->name_known_by_player) {
		sb_add(&sb,
			"L'enqueteur ne connait pas encore ton nom : ne fais pas comme s'il le "
			"savait, et n'attends pas qu'il t'appelle par ton nom.\n\n");
	}

	/* ---- Partie volatile : evolue a chaque tour, donc placee en dernier ---- */

	NpcState *ns = memory_get_npc(save, ch->id);
	if (ns) {
		sb_add(&sb, "TA RELATION ACTUELLE AVEC L'ENQUETEUR :\n");
		sb_addf(&sb, "- confiance : %s\n", memory_relation_word(ns->rel.trust));
		sb_addf(&sb, "- affection : %s\n", memory_relation_word(ns->rel.affection));
		sb_addf(&sb, "- peur : %s\n",      memory_relation_word(ns->rel.fear));
		sb_addf(&sb, "- suspicion : %s\n\n", memory_relation_word(ns->rel.suspicion));

		const Memory *rel[16];
		int max = story->memory.memories_in_prompt;
		if (max > 16) max = 16;
		int n = memory_find_relevant(save, ch->id, player_message, rel, max, save->game_time);
		if (n > 0) {
			sb_add(&sb, "TES SOUVENIRS PERTINENTS :");
			for (int i = 0; i < n; i++) sb_addf(&sb, "\n- %s", rel[i]->summary);
			sb_add(&sb, "\n\n");
		}
	}

	/* Ce que le personnage peut faire de son corps. La liste est construite a
	 * partir de ses propres autorisations et de la carte reelle : il ne peut
	 * pas proposer d'aller dans une piece qui n'existe pas, ni sortir s'il n'a
	 * pas le droit de changer de piece. */
	sb_addf(&sb, "TU TE TROUVES DANS : %s.\n", (ctx && ctx->room_here) ? ctx->room_here : "?");

	/* Deplacees ici depuis le CONTEXTE : meme information, mais dans la partie
	 * volatile, la seule qui a le droit de changer d'un tour a l'autre. */
	if (!ctx || !ctx->overheard) {
		sb_addf(&sb, "C'est %s qui t'interroge, en personne, maintenant.\n",
		        save->player_name ? save->player_name : "l'enqueteur");
	}
	/* Qui assiste a la scene : ce n'est pas un detail, on ne dit pas la meme
	 * chose devant temoin que seul a seul. */
	if (ctx && ctx->nb_present > 0) {
		sb_add(&sb, "Sont aussi presents, et vous entendent : ");
		for (int i = 0; i < ctx->nb_present; i++)
			sb_addf(&sb, "%s%s", i ? ", " : "", ctx->present_names[i]);
		sb_add(&sb, ".\n");
	}

	if (ctx && ctx->can_move) {
		sb_add(&sb, "TES DEPLACEMENTS POSSIBLES (cle \"move\") :\n");
		sb_add(&sb, "- \"reste\" : tu ne bouges pas (le cas normal)\n");
		sb_add(&sb, "- \"approche\" : tu fais un pas vers l'enqueteur\n");
		sb_add(&sb, "- \"recule\" : tu fais un pas en arriere\n");

		if (ctx->can_change_room && ctx->nb_rooms > 0) {
			sb_add(&sb, "- \"piece:<identifiant>\" : tu quittes la piece pour une autre, parmi :\n");
			for (int i = 0; i < ctx->nb_rooms; i++)
				sb_addf(&sb, "    %s (%s)\n", ctx->room_ids[i], ctx->room_names[i]);
			if (ctx->nb_others > 0) {
				sb_add(&sb, "- \"rejoint:<identifiant>\" : tu vas rejoindre quelqu'un, parmi :\n");
				for (int i = 0; i < ctx->nb_others; i++)
					sb_addf(&sb, "    %s (%s)\n", ctx->other_ids[i], ctx->other_names[i]);
			}
			sb_add(&sb, "Tu ne te deplaces que si la scene le justifie : la valeur normale "
			            "est \"reste\". Ne quitte la piece que pour une vraie raison.\n");
		} else {
			/* Sans cette phrase, le personnage promettait d'aller ailleurs — le
			 * moteur refusait le deplacement, et il ne bougeait jamais. Il doit
			 * savoir ce qu'il ne peut pas faire pour ne pas s'y engager. */
			sb_add(&sb, "Tu ne peux PAS quitter cette piece : les seules valeurs "
			            "autorisees sont \"reste\", \"approche\" et \"recule\".\n"
			            "Si on te demande d'aller ailleurs ou de rejoindre quelqu'un, tu "
			            "refuses, tu temporises ou tu expliques pourquoi tu restes la. "
			            "Ne promets JAMAIS de te deplacer ailleurs : tu n'en as pas la "
			            "possibilite.\n");
		}
		sb_add(&sb, "\n");
	} else {
		sb_add(&sb, "Tu ne peux pas te deplacer du tout. Si on te demande de bouger, tu "
		            "expliques que tu ne le peux pas ; ne promets jamais le contraire.\n\n");
	}

	/* Intervention : personne ne lui a rien demande. C'est ce qui distingue
	 * une reaction credible d'un personnage qui parlerait par obligation. */
	if (ctx && ctx->overheard && ctx->came_to_speak) {
		/* Il vient d'arriver expres : la consigne de silence ci-dessous ne
		 * s'applique pas, elle le laisserait plante devant l'autre sans un mot.
		 * Ce qu'il a a dire est decrit dans le message joint a ce prompt. */
		sb_add(&sb,
			"SITUATION : l'enqueteur ne s'adresse pas a toi. Tu viens, de ton propre chef, "
			"parler a quelqu'un d'autre, devant lui. Tu dis ce que tu avais a dire, a cette "
			"personne et non a l'enqueteur. Deux phrases au maximum.\n\n");
	} else if (ctx && ctx->overheard) {
		sb_addf(&sb,
			"SITUATION : l'enqueteur ne s'adresse pas a toi. Tu assistes a son echange "
			"avec %s, dans la meme piece. Tu n'interviens que si tu as une vraie raison : "
			"ce qui vient d'etre dit te met en cause, te surprend, contredit ce que tu sais, "
			"ou te fait sortir de tes gonds.\n"
			"Si tu n'as rien a dire, laisse \"line\" vide (\"\") : le silence est la reponse "
			"la plus frequente. Ne repete pas ce qui vient d'etre dit, ne resume pas, "
			"n'approuve pas poliment. Une phrase, deux au maximum.\n"
			/* C'etait la plus grosse fuite : un temoin qui n'avait rien a repondre
			 * profitait de l'intervention pour livrer ce qu'il savait, et le joueur
			 * recoltait des faits sans avoir pose une seule question. */
			"Ton intervention n'apporte AUCUN element de l'enquete : tu reagis, tu "
			"contredis, tu t'agaces, tu ironises, mais tu ne reveles rien de ce que tu "
			"sais. Pas d'heure, pas de nom, pas de detail technique, aucune deduction "
			"nouvelle : personne ne t'a rien demande. Si tu tiens a parler, on doit "
			"t'interroger ensuite pour en savoir plus.\n\n",
			ctx->overheard_speaker ? ctx->overheard_speaker : "quelqu'un");
	}

	sb_addf(&sb, "Reponds en %d mots maximum.", story->max_words > 0 ? story->max_words : 90);
	sb_add(&sb, dialogue_json_instructions(ctx && ctx->can_move));

	return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* Prompt d'analyse memoire                                            */
/* ------------------------------------------------------------------ */

char *prompt_build_analysis(const Story *story, const StoryCharacter *ch,
                            const char *player_message, const char *npc_reply) {
	StrBuf sb;
	sb_init(&sb);

	/* ---- Partie stable : identique pour tous les echanges de ce personnage ----
	 *
	 * L'echange a analyser vient EN DERNIER, alors qu'il etait en tete : il
	 * change a chaque appel, donc il ne restait que 4% de prefixe commun d'une
	 * analyse a l'autre et les 3 700 octets de listes et de consignes etaient
	 * refactures chaque fois. Le modele lit exactement la meme chose. */
	sb_addf(&sb,
		"Tu analyses un echange d'interrogatoire pour un moteur de jeu. "
		"Le personnage est %s (%s).\n\n",
		ch->name, ch->role ? ch->role : "");

	/* Le modele ne peut choisir que parmi les faits que ce personnage connait :
	 * il ne peut donc pas faire "apprendre" au joueur un fait hors-champ. */
	if (ch->nb_known_facts > 0) {
		sb_add(&sb, "Faits que ce personnage pouvait reveler (identifiants autorises) :\n");
		for (int i = 0; i < ch->nb_known_facts; i++) {
			const Fact *f = story_fact(story, ch->known_fact_ids[i]);
			if (f && f->text) sb_addf(&sb, "- %s : %s\n", f->id, f->text);
		}
		sb_add(&sb, "\n");
	}
	if (ch->nb_secrets > 0) {
		sb_add(&sb, "Secrets de ce personnage (identifiants autorises) :\n");
		for (int i = 0; i < ch->nb_secrets; i++) {
			const Fact *f = story_fact(story, ch->secrets[i].fact_id);
			sb_addf(&sb, "- %s : %s\n", ch->secrets[i].id, f && f->text ? f->text : "");
		}
		sb_add(&sb, "\n");
	}

	/* Les pieces a conviction. Sans cela, un personnage qui sort un document,
	 * ouvre des journaux ou montre un enregistrement ne produisait rien : le
	 * joueur voyait la scene mais n'obtenait aucune information, et les faits
	 * que ces pieces revelent restaient hors d'atteinte pour toute la partie. */
	/* Uniquement les pieces que CE personnage peut sortir, comme dans le prompt
	 * de dialogue : lui presenter tout le catalogue de l'enquete l'invitait a
	 * attribuer a l'un ce qu'un autre detient, et coutait le catalogue entier a
	 * chaque analyse. */
	if (story->nb_clues > 0) {
		bool any = false;
		for (int i = 0; i < story->nb_clues; i++) {
			const Clue *cl = &story->clues[i];
			if (!cl->discoverable) continue;
			if (!character_holds_clue(ch, cl)) continue;

			if (!any) {
				sb_add(&sb, "Pieces a conviction que ce personnage peut sortir "
				            "(identifiants autorises) :\n");
				any = true;
			}
			sb_addf(&sb, "- %s : %s (%s)\n", cl->id, cl->name,
			        cl->description ? cl->description : "");
		}
		if (any) sb_add(&sb, "\n");
	}

	sb_addf(&sb,
		"Reponds UNIQUEMENT avec un objet JSON brut, sans markdown ni texte autour.\n"
		"Si rien ne merite d'etre retenu a long terme :\n"
		"{\"remember\": false}\n"
		"Sinon :\n"
		"{\"remember\": true, \"type\": \"interaction|event|information|relationship\", "
		"\"summary\": \"une phrase, du point de vue du personnage\", "
		"\"importance\": 1-5, \"emotion\": -5 a 5, \"tags\": [\"...\"], "
		"\"relationship_changes\": {\"trust\": 0, \"affection\": 0, \"fear\": 0, \"suspicion\": 0}, "
		"\"player_learned_fact_ids\": [], \"revealed_secret_id\": null, "
		"\"produced_clue_id\": null}\n\n"
		"Regles : n'invente aucun identifiant, n'utilise que ceux listes ci-dessus. "
		"player_learned_fact_ids ne contient que les faits que le personnage vient "
		"reellement de reveler a l'enqueteur dans cet echange : il faut qu'il les ait "
		"ENONCES, assez clairement pour que l'enqueteur puisse s'en servir. Une allusion, "
		"un sous-entendu, une plaisanterie sur le sujet ou le simple fait d'en parler ne "
		"comptent pas, et un fait dont il n'a rien dit ne compte jamais, meme si la "
		"question le concernait. "
		"revealed_secret_id n'est renseigne que si le personnage vient d'avouer ce secret. "
		"produced_clue_id est renseigne quand le personnage vient de MONTRER ou de SORTIR "
		"cette piece a conviction a l'enqueteur (il ouvre les journaux, affiche le fichier, "
		"tend le document, lit le message a voix haute...). Une simple promesse de le faire "
		"plus tard ne compte pas ; en revanche, s'il dit l'avoir fait ou decrit ce qu'elle "
		"contient, elle compte. "
		"Les variations de relation restent entre -10 et 10. "
		"Ne cree un souvenir que si importance >= %d.\n\n",
		story->memory.minimum_importance_to_store);

	/* ---- Partie volatile : l'echange a analyser, donc en dernier ----
	 *
	 * Note : le prompt de DIALOGUE fait l'inverse, il garde ses consignes de
	 * format en toute fin. La difference est assumee et tient aux consequences
	 * d'un format rate : une replique mal formee se voit a l'ecran et coute le
	 * tour au joueur, alors qu'une analyse mal formee est simplement ignoree par
	 * analysis_parse et retentee a l'echange suivant. Ici on prend donc le cache
	 * (98% de prefixe commun au lieu de ~55%), la-bas on prend la fiabilite. */
	sb_addf(&sb,
		"ECHANGE A ANALYSER :\n"
		"ENQUETEUR : %s\n"
		"%s : %s\n",
		player_message ? player_message : "",
		ch->name, npc_reply ? npc_reply : "");

	return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* Lecture du resultat d'analyse                                       */
/* ------------------------------------------------------------------ */

static int clamp_delta(int v) {
	if (v >  10) return  10;
	if (v < -10) return -10;
	return v;
}

/* Le modele repond parfois avec un preambule ou des balises : on cherche le
 * premier '{' comme pour les repliques de dialogue. */
bool analysis_parse(const char *json_text, const Story *story,
                    const StoryCharacter *ch, AnalysisResult *out) {
	memset(out, 0, sizeof(*out));
	if (!json_text) return false;

	const char *brace = strchr(json_text, '{');
	JsonValue *root = json_parse(brace ? brace : json_text);
	if (!root) return false;

	out->remember = json_bool_or(json_object_get(root, "remember"), 0);

	out->type       = json_strdup(json_object_get(root, "type"));
	out->summary    = json_strdup(json_object_get(root, "summary"));
	out->importance = json_int_or(json_object_get(root, "importance"), 0);
	out->emotion    = json_int_or(json_object_get(root, "emotion"), 0);

	JsonValue *tags = json_object_get(root, "tags");
	int nb = json_array_count(tags);
	for (int i = 0; i < nb && out->nb_tags < ANALYSIS_MAX_TAGS; i++) {
		const char *t = json_string(json_array_get(tags, i));
		if (t && *t) out->tags[out->nb_tags++] = strdup(t);
	}

	JsonValue *rc = json_object_get(root, "relationship_changes");
	out->trust_d     = clamp_delta(json_int_or(json_object_get(rc, "trust"), 0));
	out->affection_d = clamp_delta(json_int_or(json_object_get(rc, "affection"), 0));
	out->fear_d      = clamp_delta(json_int_or(json_object_get(rc, "fear"), 0));
	out->suspicion_d = clamp_delta(json_int_or(json_object_get(rc, "suspicion"), 0));

	/* Un identifiant de fait n'est retenu que s'il existe vraiment ET si ce
	 * personnage etait bien en position de le reveler. Le moteur garde la
	 * main sur la verite du monde. */
	JsonValue *facts = json_object_get(root, "player_learned_fact_ids");
	nb = json_array_count(facts);
	for (int i = 0; i < nb && out->nb_learned_facts < ANALYSIS_MAX_FACTS; i++) {
		const char *fid = json_string(json_array_get(facts, i));
		if (!fid || !story_fact(story, fid)) continue;

		bool knows = false;
		for (int k = 0; k < ch->nb_known_facts; k++) {
			if (strcmp(ch->known_fact_ids[k], fid) == 0) { knows = true; break; }
		}
		if (knows) out->learned_fact_ids[out->nb_learned_facts++] = strdup(fid);
	}

	const char *sec = json_string(json_object_get(root, "revealed_secret_id"));
	if (sec) {
		for (int i = 0; i < ch->nb_secrets; i++) {
			if (ch->secrets[i].id && strcmp(ch->secrets[i].id, sec) == 0) {
				out->revealed_secret_id = strdup(sec);
				break;
			}
		}
	}

	/* Une piece a conviction inventee est ignoree, comme tout identifiant :
	 * le moteur reste maitre de ce qui existe dans l'affaire. */
	const char *clue = json_string(json_object_get(root, "produced_clue_id"));
	if (clue) {
		for (int i = 0; i < story->nb_clues; i++) {
			if (story->clues[i].id && story->clues[i].discoverable &&
			    strcmp(story->clues[i].id, clue) == 0) {
				out->produced_clue_id = strdup(clue);
				break;
			}
		}
	}

	json_free(root);
	return true;
}

/* ------------------------------------------------------------------ */
/* Resolution de l'enquete                                             */
/* ------------------------------------------------------------------ */

char *prompt_build_solution(const Story *story, const char *culprit_id) {
	const StoryCharacter *cul = story_character(story, culprit_id);
	if (!cul) return NULL;

	StrBuf sb;
	sb_init(&sb);

	sb_addf(&sb,
		"Tu es le concepteur d'une enquete. L'affaire suivante a ete ecrite sans "
		"coupable designe : les faits sont volontairement ambigus. On vient de "
		"decider que le coupable est %s (%s). Ta tache est d'ecrire la "
		"resolution qui rend toute l'affaire coherente.\n\n",
		cul->name, cul->role ? cul->role : "");

	sb_addf(&sb, "AFFAIRE : %s\n", story->premise ? story->premise : "");
	if (story->victim_name)
		sb_addf(&sb, "Victime : %s (%s)\n", story->victim_name,
		        story->victim_role ? story->victim_role : "");
	if (story->crime_type)
		sb_addf(&sb, "Crime : %s dans %s entre %s et %s\n",
		        story->crime_type, story->crime_location ? story->crime_location : "",
		        story->crime_time_start ? story->crime_time_start : "",
		        story->crime_time_end ? story->crime_time_end : "");
	sb_add(&sb, "\n");

	/* Tous les faits, avec leur identifiant : la resolution doit s'appuyer
	 * sur eux et sur rien d'autre. */
	sb_add(&sb, "FAITS ETABLIS (la resolution doit tous les respecter) :\n");
	for (int i = 0; i < story->nb_facts; i++) {
		sb_addf(&sb, "- %s : %s%s\n", story->facts[i].id, story->facts[i].text,
		        story->facts[i].truth ? "" : " (FAUX)");
	}
	sb_add(&sb, "\n");

	if (story->nb_timeline > 0) {
		sb_add(&sb, "CHRONOLOGIE :\n");
		for (int i = 0; i < story->nb_timeline; i++)
			sb_addf(&sb, "- %s : %s\n", story->timeline[i].time, story->timeline[i].event);
		sb_add(&sb, "\n");
	}

	if (story->nb_clues > 0) {
		sb_add(&sb, "INDICES QUE L'ENQUETEUR PEUT TROUVER :\n");
		for (int i = 0; i < story->nb_clues; i++)
			sb_addf(&sb, "- %s : %s\n", story->clues[i].name, story->clues[i].description);
		sb_add(&sb, "\n");
	}

	/* Les autres suspects et leurs alibis, pour que la resolution explique
	 * pourquoi ce sont eux qui sont innocents. */
	sb_add(&sb, "LES AUTRES PERSONNES PRESENTES :\n");
	for (int i = 0; i < story->nb_characters; i++) {
		const StoryCharacter *c = &story->characters[i];
		if (c->id && strcmp(c->id, culprit_id) == 0) continue;
		sb_addf(&sb, "- %s (%s)", c->name, c->role ? c->role : "");
		if (c->alibi_claim) sb_addf(&sb, " : dit %s", c->alibi_claim);
		sb_add(&sb, "\n");
	}
	sb_add(&sb, "\n");

	sb_addf(&sb, "LE COUPABLE : %s\n", cul->name);
	if (cul->nb_motives > 0) {
		sb_add(&sb, "Ses mobiles possibles :\n");
		for (int i = 0; i < cul->nb_motives; i++) sb_addf(&sb, "- %s\n", cul->motives[i]);
	}
	if (cul->alibi_claim)
		sb_addf(&sb, "Son alibi : %s\nSa faille : %s\n", cul->alibi_claim,
		        cul->alibi_weakness ? cul->alibi_weakness : "");
	if (cul->position_in_case)
		sb_addf(&sb, "Sa position : %s\n", cul->position_in_case);
	sb_add(&sb, "\n");

	sb_add(&sb,
		"Reponds UNIQUEMENT avec un objet JSON brut, sans markdown ni texte autour :\n"
		"{\n"
		"  \"solution\": \"comment et pourquoi le coupable a agi, en 4 a 6 phrases, "
		"en s'appuyant uniquement sur les faits ci-dessus, en expliquant le mobile, "
		"le deroulement pendant la fenetre du crime, et pourquoi les autres sont innocents\",\n"
		"  \"culprit_brief\": \"la meme verite mais adressee au coupable a la deuxieme "
		"personne, en 2 a 4 phrases : ce qu'il a fait, ce qu'il doit absolument cacher, "
		"et ce qui le trahirait si l'enqueteur le trouvait\",\n"
		"  \"incriminating_fact_ids\": [\"identifiants des faits qui l'incriminent\"]\n"
		"}\n\n"
		"Contraintes : n'invente aucun fait qui contredirait la liste, n'invente aucun "
		"personnage, n'utilise que les identifiants de faits fournis. La resolution doit "
		"etre deductible a partir des faits et des indices.\n"
		/* Le moteur ecarte de lui-meme les faits que seul le coupable connait
		 * (voir generate_solution), mais autant que le modele n'en propose pas :
		 * un fait ecarte est un element a charge de moins pour l'enqueteur. */
		"Pour incriminating_fact_ids, choisis en priorite des faits qu'un AUTRE personnage "
		"connait ou qu'une piece a conviction etablit : l'enqueteur doit pouvoir les obtenir "
		"sans passer par le coupable, qui n'avoue justement qu'une fois confronte a eux.");

	return sb_take(&sb);
}

bool solution_parse(const char *json_text, const Story *story, SolutionResult *out) {
	memset(out, 0, sizeof(*out));
	if (!json_text) return false;

	const char *brace = strchr(json_text, '{');
	JsonValue *root = json_parse(brace ? brace : json_text);
	if (!root) return false;

	out->solution      = json_strdup(json_object_get(root, "solution"));
	out->culprit_brief = json_strdup(json_object_get(root, "culprit_brief"));

	JsonValue *facts = json_object_get(root, "incriminating_fact_ids");
	int nb = json_array_count(facts);
	for (int i = 0; i < nb && out->nb_facts < ANALYSIS_MAX_FACTS; i++) {
		const char *fid = json_string(json_array_get(facts, i));
		/* Un identifiant invente est ignore, comme partout ailleurs. */
		if (fid && story_fact(story, fid)) out->fact_ids[out->nb_facts++] = strdup(fid);
	}

	json_free(root);
	return out->solution != NULL;
}

void solution_free(SolutionResult *s) {
	if (!s) return;
	free(s->solution);
	free(s->culprit_brief);
	for (int i = 0; i < s->nb_facts; i++) free(s->fact_ids[i]);
	memset(s, 0, sizeof(*s));
}

/* ------------------------------------------------------------------ */
/* Combler les trous de solubilite                                     */
/* ------------------------------------------------------------------ */

char *prompt_build_clue_fill(const Story *story, const char *culprit_id,
                             const char **missing, int nb_missing) {
	if (!story || !culprit_id || nb_missing <= 0) return NULL;

	const StoryCharacter *cul = story_character(story, culprit_id);
	if (!cul) return NULL;

	StrBuf sb;
	sb_init(&sb);

	sb_addf(&sb,
		"Tu completes le dossier d'une enquete policiere pour un moteur de jeu, dans "
		"l'univers suivant.\n\n"
		"AFFAIRE : %s\n", story->title ? story->title : "");
	if (story->location) sb_addf(&sb, "Lieu : %s", story->location);
	if (story->year) sb_addf(&sb, " (%d)", story->year);
	sb_add(&sb, "\n");
	if (story->premise) sb_addf(&sb, "%s\n", story->premise);
	sb_add(&sb, "\n");

	sb_addf(&sb,
		"PROBLEME A RESOUDRE : le coupable de cette partie est %s. Les faits ci-dessous "
		"l'accusent, mais AUCUNE autre personne que lui ne peut les rapporter, et il "
		"n'avoue que confronte a des preuves. L'enquete est donc actuellement impossible "
		"a resoudre.\n\n"
		"IL FAUT DONC, pour chaque fait listé, une piece a conviction materielle (des "
		"journaux, un mail, un rapport, un enregistrement, une trace, un objet) que "
		"l'enqueteur puisse obtenir SANS passer par %s, et une personne credible qui la "
		"detient.\n\n",
		cul->name, cul->name);

	sb_add(&sb, "FAITS A COUVRIR (un par piece, reprends l'identifiant tel quel) :\n");
	for (int i = 0; i < nb_missing; i++) {
		const Fact *f = story_fact(story, missing[i]);
		sb_addf(&sb, "- %s : %s\n", missing[i], f && f->text ? f->text : "");
	}
	sb_add(&sb, "\n");

	/* Les detenteurs possibles, calcules par le moteur : places sur la carte, et
	 * jamais le coupable. Le modele choisit dans cette liste, il ne l'invente
	 * pas. */
	sb_add(&sb, "DETENTEURS POSSIBLES (choisis dans cette liste, et dans elle seule) :\n");
	for (int i = 0; i < story->nb_characters; i++) {
		const StoryCharacter *c = &story->characters[i];
		if (!c->placed) continue;
		if (c->id && strcmp(c->id, culprit_id) == 0) continue;
		sb_addf(&sb, "- %s : %s", c->id, c->name);
		if (c->role) sb_addf(&sb, ", %s", c->role);
		sb_add(&sb, "\n");
	}
	sb_add(&sb, "\n");

	/* Ce qui existe deja, pour ne pas ecrire deux fois la meme piece. */
	if (story->nb_clues > 0) {
		sb_add(&sb, "PIECES QUI EXISTENT DEJA (n'en refais pas une equivalente) :\n");
		for (int i = 0; i < story->nb_clues; i++)
			sb_addf(&sb, "- %s : %s\n", story->clues[i].name,
			        story->clues[i].description ? story->clues[i].description : "");
		sb_add(&sb, "\n");
	}

	sb_add(&sb,
		"Reponds UNIQUEMENT avec un objet JSON brut, sans markdown ni texte autour :\n"
		"{\n"
		"  \"clues\": [\n"
		"    { \"fact_id\": \"l'identifiant du fait couvert, copie tel quel\",\n"
		"      \"id\": \"identifiant court en minuscules, sans espace ni accent\",\n"
		"      \"name\": \"nom court, tel qu'il s'affichera au joueur\",\n"
		"      \"description\": \"une phrase : ce que la piece montre concretement\",\n"
		"      \"holder_id\": \"l'identifiant d'un detenteur de la liste ci-dessus\" }\n"
		"  ]\n"
		"}\n\n"
		"Contraintes : une entree par fait a couvrir, pas plus. N'invente aucun fait, "
		"aucun personnage, aucun identifiant de fait : recopie ceux fournis. holder_id "
		"doit venir de la liste des detenteurs possibles. La piece doit exister "
		"materiellement dans ce decor et a cette epoque, et sa description doit rester "
		"compatible avec le fait qu'elle etablit — le joueur la lira comme une preuve.\n"
		"Ecris dans le ton de l'affaire, en francais.");

	return sb_take(&sb);
}

bool clue_fill_parse(const char *json_text, const Story *story, const char *culprit_id,
                     const char **missing, int nb_missing, ClueFillResult *out) {
	memset(out, 0, sizeof(*out));
	if (!json_text || !story) return false;

	const char *brace = strchr(json_text, '{');
	JsonValue *root = json_parse(brace ? brace : json_text);
	if (!root) return false;

	JsonValue *arr = json_object_get(root, "clues");
	int nb = json_array_count(arr);

	for (int i = 0; i < nb && out->nb_items < CLUE_FILL_MAX; i++) {
		JsonValue *c = json_array_get(arr, i);
		const char *fid    = json_string(json_object_get(c, "fact_id"));
		const char *cid    = json_string(json_object_get(c, "id"));
		const char *name   = json_string(json_object_get(c, "name"));
		const char *desc   = json_string(json_object_get(c, "description"));
		const char *holder = json_string(json_object_get(c, "holder_id"));
		if (!fid || !cid || !holder) continue;

		/* Le fait doit exister ET faire partie des trous demandes : sinon le
		 * modele s'offrirait le droit d'ajouter des preuves ailleurs. */
		if (!story_fact(story, fid)) continue;
		bool asked = false;
		for (int k = 0; k < nb_missing; k++)
			if (strcmp(missing[k], fid) == 0) asked = true;
		if (!asked) continue;

		/* Le detenteur doit exister, etre sur la carte, et ne pas etre le
		 * coupable : sinon on n'a rien resolu. */
		const StoryCharacter *h = story_character(story, holder);
		if (!h || !h->placed) continue;
		if (culprit_id && h->id && strcmp(h->id, culprit_id) == 0) continue;

		/* Un identifiant qui existe deja serait fusionne avec un indice d'auteur
		 * et changerait ce que l'auteur a ecrit. */
		bool clash = false;
		for (int k = 0; k < story->nb_clues; k++)
			if (story->clues[k].id && strcmp(story->clues[k].id, cid) == 0) clash = true;
		for (int k = 0; k < out->nb_items; k++)
			if (strcmp(out->items[k].clue_id, cid) == 0) clash = true;
		if (clash) continue;

		ClueFill *it = &out->items[out->nb_items++];
		it->clue_id     = strdup(cid);
		it->name        = strdup(name && *name ? name : cid);
		it->description = strdup(desc ? desc : "");
		it->fact_id     = strdup(fid);
		it->holder_id   = strdup(h->id);
	}

	json_free(root);
	return out->nb_items > 0;
}

void clue_fill_free(ClueFillResult *r) {
	if (!r) return;
	for (int i = 0; i < r->nb_items; i++) {
		free(r->items[i].clue_id);
		free(r->items[i].name);
		free(r->items[i].description);
		free(r->items[i].fact_id);
		free(r->items[i].holder_id);
	}
	memset(r, 0, sizeof(*r));
}

void analysis_free(AnalysisResult *a) {
	if (!a) return;
	free(a->type);
	free(a->summary);
	for (int i = 0; i < a->nb_tags; i++) free(a->tags[i]);
	for (int i = 0; i < a->nb_learned_facts; i++) free(a->learned_fact_ids[i]);
	free(a->revealed_secret_id);
	free(a->produced_clue_id);
	memset(a, 0, sizeof(*a));
}
