#ifndef GLOBALS_H
#define GLOBALS_H

#include <ncurses.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NB_FACES 49
#define NB_BODYS 13
#define NB_LEGS 14

extern wchar_t *faces[];
extern wchar_t *bodies[];
extern wchar_t *legs[];

/* Emotion palette for the dialogue replies: the only emoji a character is
 * allowed to answer with, and the faces[] entry each one maps to. The model
 * is told to pick from this table (see dialogue_json_instructions), so an
 * emotion can always be shown on the character's face. */
typedef struct {
    const char  *emoji;   /* UTF-8, as the model returns it */
    int          face_id; /* index into faces[] */
} EmotionFace;

extern const EmotionFace emotion_faces[];
extern const int nb_emotion_faces;

#endif
