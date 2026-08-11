#include "globals.h"

wchar_t *faces[] = {
    L"😀",  // 0 - Grinning face
    L"😄",  // 2 - Grinning face with smiling eyes
    L"😁",  // 3 - Beaming face with smiling eyes
    L"😅",  // 5 - Grinning face with sweat
    L"😂",  // 6 - Face with tears of joy
    L"😊",  // 8 - Smiling face with smiling eyes
    L"😇",  // 9 - Smiling face with halo
    L"🙂",  //10 - Slightly smiling face
    L"🙃",  //11 - Upside-down face
    L"😉",  //12 - Winking face
    L"😌",  //13 - Relieved face
    L"😍",  //14 - Smiling face with heart-eyes
    L"😘",  //15 - Face blowing a kiss
    L"😙",  //17 - Kissing face with smiling eyes
    L"😋",   //19 - Face savoring food
	L"😭",  // pleure fort
    L"😱",  // visage choqué / cri
    L"🤮",  // vomi
	L"👼", 
    L"🤯",  // 1 - Grinning face with big eyes
    L"🤬",  // 4 - Grinning squinting face
    L"🥸",  // 7 - Rolling on the floor laughing
    L"🎧",  //16 - Kissing face
    L"🤿",  //18 - Kissing face with closed eyes
    L"😈",  // diable souriant
    L"💀",  // tête de mort
    L"🤡",  // clown
    L"👻",  // fantôme
    L"👽",  // alien
    L"🐱",  // tête de chat
    L"🐵",  // singe
    L"🦊",  // renard
    L"🐸",  // grenouille
    L"🍕",  // pizza
    L"🍩",  // donut
    L"🧠",  // cerveau
    L"🗿",  // citrouille
    L"💩",  // caca souriant
    L"⚽",  // ballon de foot

    // 39+ : expressions utilisees par les PNJ pendant les dialogues
    L"🤔",  //39 - reflexion
    L"🤨",  //40 - soupcon / scepticisme
    L"😐",  //41 - neutre / impassible
    L"😠",  //42 - colere
    L"😢",  //43 - tristesse
    L"😨",  //44 - peur
    L"😳",  //45 - trouble / gene
    L"😏",  //46 - ironie / mepris
    L"😰",  //47 - angoisse / nervosite

    L"🤖"   //48 - androide : visage fixe, aucun equivalent expressif
};

/* Must stay consistent with faces[] above: face_id indexes into it. The
 * prompt sent to the model is generated from this table, so adding an entry
 * here is all it takes to widen the palette. */
const EmotionFace emotion_faces[] = {
    { "😐", 41 },  // neutre / impassible
    { "🙂", 7  },  // aimable
    { "😊", 5  },  // chaleureux
    { "😂", 4  },  // rire
    { "😉", 9  },  // complice
    { "😌", 10 },  // soulage
    { "🤔", 39 },  // reflexion
    { "🤨", 40 },  // soupcon / scepticisme
    { "😏", 46 },  // ironie / mepris
    { "😠", 42 },  // colere
    { "🤬", 20 },  // fureur
    { "😢", 43 },  // tristesse
    { "😭", 15 },  // chagrin
    { "😨", 44 },  // peur
    { "😱", 16 },  // terreur
    { "😰", 47 },  // angoisse / nervosite
    { "😳", 45 },  // trouble / gene
    { "🤯", 19 },  // sideration
    { "😈", 24 },  // malveillance
};

const int nb_emotion_faces = (int)(sizeof(emotion_faces) / sizeof(emotion_faces[0]));

wchar_t *bodies[] = {
	L"🥼",
	L"🦺",
	L"👔",
	L"👕",
	L"🧥",
	L"👘",
	L"👗",
	L"🥻",
	L"🩱",
	L"👚",
	L"🎽",
	L"🥋",
	L"🫁"
};

wchar_t *legs[] = {
	L"👖",
	L"🩳",
	L"🦵",
	L"👢",
	L"🥾",
	L"🧦",
	L"🦶",
	L"🐑",
	L"🦄",
	L"🐖",
	L"🛵",
	L"🛴",
	L"🛶",
	L"🦿"      //13 - prothese : jambes d'androide
};