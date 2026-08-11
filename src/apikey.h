#ifndef APIKEY_H
#define APIKEY_H

#include <stdbool.h>
#include <stddef.h>

/* Stockage local de la cle d'API DeepSeek.
 *
 * Ce que ce module protege, et ce qu'il ne protege pas.
 *
 * Le jeu doit dechiffrer la cle tout seul, sans rien demander a chaque
 * lancement : la cle de dechiffrement doit donc etre calculable par le
 * programme lui-meme. Quelqu'un qui possede a la fois ce binaire et ce
 * fichier, sur cette machine et sous ce compte, peut donc retrouver la cle.
 *
 * Ce fichier chiffre evite en revanche qu'elle apparaisse en clair dans une
 * sauvegarde, une capture d'ecran, un depot git ou un fichier de
 * configuration lu par erreur, et le chiffrement est authentifie : le
 * fichier est inutilisable s'il est copie sur une autre machine ou sous un
 * autre compte, et toute modification est detectee.
 *
 * Pour une vraie confidentialite il faudrait une phrase de passe saisie a
 * chaque lancement ; ce n'est pas ce qui est demande ici. */

#define APIKEY_MAX 256

/* Chemin du fichier (sous $XDG_CONFIG_HOME, sinon ~/.config). */
const char *apikey_path(void);

/* true si une cle est deja enregistree. */
bool apikey_exists(void);

/* Lit et dechiffre la cle. Renvoie false si le fichier est absent,
 * illisible, corrompu, ou chiffre pour une autre machine / un autre
 * compte. */
bool apikey_load(char *out, size_t out_size);

/* Chiffre et enregistre la cle (dossier 0700, fichier 0600, ecriture par
 * fichier temporaire puis rename). */
bool apikey_store(const char *key);

/* Supprime la cle enregistree, pour pouvoir en saisir une autre. */
bool apikey_forget(void);

#endif
