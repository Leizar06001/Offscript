#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

/* Reglages du joueur, communs a toutes les parties.
 *
 * Ils vivent a cote de la cle d'API, sous $XDG_CONFIG_HOME (sinon ~/.config),
 * et non dans une sauvegarde : changer de partie ou en commencer une nouvelle
 * ne doit pas reinitialiser ses touches. */

/* Une action du jeu. Chacune a deux touches : une principale et une
 * secondaire, ce qui permet de garder les fleches tout en jouant en ZQSD. */
typedef enum {
	ACT_UP = 0,
	ACT_DOWN,
	ACT_LEFT,
	ACT_RIGHT,
	ACT_SCROLL_UP,
	ACT_SCROLL_DOWN,
	ACT_TALK,
	ACT_NEXT_TARGET,
	ACT_JOURNAL,
	ACT_PERSO,
	/* Options, Solution et Quitter passent par le menu ouvert avec cette
	 * touche : ce sont des actions rares, elles n'ont pas a occuper un
	 * raccourci chacune. */
	ACT_MENU,
	ACT_COUNT
} Action;

#define OPT_KEY_NONE 0

typedef struct {
	int    keys[ACT_COUNT][2];

	/* Effort de raisonnement demande au modele : "low", "high" ou "max".
	 * Plus il est eleve, plus la replique est reflechie, plus elle coute et
	 * plus elle se fait attendre. */
	char   reasoning[8];

	/* Tarifs du modele, en dollars par million de jetons. Ils ne servent qu'a
	 * l'estimation affichee en bas de l'ecran : la grille du fournisseur
	 * bouge, le joueur doit pouvoir la corriger sans recompiler. */
	double price_in_per_m;
	double price_out_per_m;
} Options;

/* Chemin du fichier de reglages. */
const char *options_path(void);

/* Remet les valeurs d'usine : fleches + ZQSD, tarifs par defaut. */
void options_defaults(Options *o);

/* Charge le fichier s'il existe, sinon remplit avec les valeurs d'usine.
 * Ne peut pas echouer du point de vue de l'appelant : sans fichier lisible,
 * on joue avec les reglages par defaut. */
void options_load(Options *o);
bool options_save(const Options *o);

/* Action declenchee par une touche, ou -1. `is_function_key` distingue une
 * touche speciale de ncurses (fleches, F3...) d'un caractere tape : sans
 * cela, KEY_DOWN et un caractere de meme valeur seraient confondus. */
int  options_action_for_key(const Options *o, int ch, bool is_function_key);

/* Libelle d'une action, pour le menu. */
const char *options_action_label(Action a);

/* Nom lisible d'une touche ("Fleche haut", "Z", "[Tab]"...). */
void options_key_name(int key, char *out, size_t out_size);

/* Encodage d'une touche saisie : les touches speciales sont rangees au-dela
 * des caracteres pour ne jamais entrer en collision avec eux. */
#define OPT_FUNCTION_BASE 0x100000
int  options_encode_key(int ch, bool is_function_key);

#endif
