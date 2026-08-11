#ifndef DIALOGUE_H
#define DIALOGUE_H

#include <stdbool.h>

/* Structured reply from a character. All fields are malloc'd; free with
 * dialogue_reply_free. */
typedef struct {
    char *line;    /* what the character says out loud */
    char *emotion; /* a single emoji, from the emotion_faces[] palette */
    char *action;  /* what the character physically does */

    /* Le personnage declare avouer le meurtre. Le modele ne peut poser ce
     * drapeau que si le prompt le lui a ouvert, et le moteur ne l'accepte que
     * du coupable, une fois les preuves reunies : le mot du modele ne suffit
     * jamais a terminer la partie (voir npc.c). */
    int   confession;

    /* Deplacement demande par le personnage ("recule", "piece:labo_b"...).
     * NULL quand il n'en propose aucun. Le moteur valide avant d'appliquer. */
    char *move;
} DialogueReply;

/* Appended to every character's system prompt so the model answers with a
 * parseable object instead of free prose. Built once, on first call, from
 * emotion_faces[] so the list of emoji the model is offered can never drift
 * from the list the game is able to draw. Not thread-safe: call it from the
 * game loop only (it is, when a character's session is created). */
/* `with_move` ajoute la cle "move" a la forme demandee. Sans cela, cette
 * consigne — qui ferme le prompt — contredisait la section des deplacements
 * en exigeant EXACTEMENT trois cles : le modele n'emettait jamais "move", et
 * aucun ordre de deplacement n'arrivait au moteur. */
const char *dialogue_json_instructions(bool with_move);

/* Maps the emoji in a reply's "emotion" field to a faces[] index, ignoring
 * any Unicode variation selector the model may append. Returns -1 when the
 * model answered with something outside the palette, in which case the
 * caller should leave the character's current face alone. */
int dialogue_face_from_emoji(const char *emoji);

/* Strips optional model chatter (```json fences, a short lead-in) from
 * raw_text and parses it as {"emotion","line","action"}. Returns 1 and
 * fills *out on success, 0 on failure (out is left untouched). */
int dialogue_reply_from_text(const char *raw_text, DialogueReply *out);

/* Last-resort reading of a reply that is not the requested object at all: a
 * model that has drifted answers in plain prose. Rather than losing the turn,
 * the whole text becomes the spoken line (and a leading emoji, if the model
 * put one there, becomes the emotion). Returns 1 when there was any text. */
int dialogue_reply_from_prose(const char *raw_text, DialogueReply *out);

void dialogue_reply_free(DialogueReply *reply);

#endif
