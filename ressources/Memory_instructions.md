# Système de mémoire des PNJ

## Objectif

Chaque PNJ possède trois types de mémoire :

1. **Mémoire récente** : les derniers échanges avec le joueur.
2. **Mémoire long terme** : uniquement les événements jugés importants.
3. **Relation** : quelques valeurs numériques qui évoluent selon les interactions.

Le fichier d'histoire définit la personnalité et les connaissances initiales du PNJ.

Le fichier de sauvegarde contient uniquement ce qui évolue pendant la partie.

---

# 1. Structure JSON d'un PNJ dans la sauvegarde

```json
{
  "npc_id": "erik",

  "relationship": {
    "trust": 10,
    "affection": 0,
    "fear": 0,
    "suspicion": 5
  },

  "recent_messages": [
    {
      "role": "user",
      "content": "Tu sais quelque chose sur le meurtre ?"
    },
    {
      "role": "assistant",
      "content": "Pas grand-chose. Et je préfère que ça reste ainsi."
    }
  ],

  "memories": [
    {
      "id": "mem_001",
      "type": "interaction",
      "summary": "Le joueur a aidé Erik à retrouver son fils.",
      "importance": 5,
      "emotion": 4,
      "tags": [
        "player",
        "family",
        "help"
      ],
      "created_at": 1240,
      "last_recalled_at": 1402
    }
  ]
}
```

---

# 2. Relation avec le joueur

Chaque PNJ possède quatre valeurs comprises entre `-100` et `100`.

```json
"relationship": {
  "trust": 20,
  "affection": 10,
  "fear": 0,
  "suspicion": 15
}
```

### trust

Indique si le PNJ pense que le joueur est fiable.

Exemples :

```text
-100 = considère le joueur comme un ennemi
0    = neutre
+100 = confiance absolue
```

### affection

Indique si le PNJ apprécie personnellement le joueur.

Un PNJ peut donc avoir :

```text
trust = 80
affection = -20
```

Il sait que le joueur est fiable mais ne l'aime pas.

### fear

Indique à quel point le PNJ a peur du joueur.

### suspicion

Indique si le PNJ pense que le joueur cache quelque chose ou représente une menace.

Ces valeurs ne doivent pas déterminer directement le dialogue.

Elles sont données à l'IA comme contexte.

Par exemple :

```text
Relation avec le joueur :

Confiance : élevée
Affection : faible
Peur : aucune
Suspicion : moyenne
```

Il vaut mieux convertir les chiffres en descriptions avant de les envoyer au LLM.

---

# 3. Mémoire récente

Chaque PNJ conserve uniquement les derniers messages.

Par exemple :

```json
"recent_messages": [
  {
    "role": "user",
    "content": "Où étais-tu hier soir ?"
  },
  {
    "role": "assistant",
    "content": "Dans mon bureau."
  },
  {
    "role": "user",
    "content": "Tu mens."
  },
  {
    "role": "assistant",
    "content": "Fais attention à tes accusations."
  }
]
```

Limiter cette mémoire à environ :

```text
8 à 12 messages
```

Quand elle dépasse la limite, supprimer les messages les plus anciens.

Ces messages sont envoyés directement dans `messages[]` lors de l'appel API.

---

# 4. Mémoire long terme

Les conversations anciennes ne sont pas toutes sauvegardées.

Seuls les événements intéressants deviennent des souvenirs.

Exemple :

```json
{
  "id": "mem_014",
  "type": "interaction",
  "summary": "Le joueur a accusé Erik d'être impliqué dans le meurtre.",
  "importance": 3,
  "emotion": -2,
  "tags": [
    "player",
    "accusation",
    "murder"
  ],
  "created_at": 3500,
  "last_recalled_at": null
}
```

Un souvenir possède :

```text
id
type
summary
importance
emotion
tags
created_at
last_recalled_at
```

---

# 5. Importance

`importance` va de 1 à 5.

```text
1 = détail banal
2 = information légèrement intéressante
3 = événement notable
4 = événement important
5 = événement majeur pour le personnage
```

Exemples :

```text
Le joueur dit bonjour
importance = 1

Le joueur insulte le PNJ
importance = 2 ou 3

Le joueur découvre son secret
importance = 4

Le joueur sauve son fils
importance = 5

Le joueur tue son meilleur ami
importance = 5
```

En pratique, je conseille de ne créer des souvenirs long terme que pour :

```text
importance >= 3
```

---

# 6. Émotion du souvenir

`emotion` va de `-5` à `+5`.

```text
-5 = extrêmement négatif
0  = neutre
+5 = extrêmement positif
```

Exemple :

```json
{
  "summary": "Le joueur a sauvé la vie d'Erik.",
  "importance": 5,
  "emotion": 5
}
```

ou :

```json
{
  "summary": "Le joueur a menacé la fille d'Erik.",
  "importance": 5,
  "emotion": -5
}
```

Cela permet au système de sélectionner les souvenirs non seulement selon leur pertinence, mais également selon leur impact émotionnel.

---

# 7. Types de souvenirs

Garder seulement quelques types.

```text
interaction
event
information
relationship
```

### interaction

Quelque chose que le joueur a fait directement au PNJ.

```json
{
  "type": "interaction",
  "summary": "Le joueur a insulté Erik."
}
```

### event

Un événement du monde auquel le PNJ a assisté ou dont il a été informé.

```json
{
  "type": "event",
  "summary": "Le laboratoire a été placé en quarantaine."
}
```

### information

Une information apprise pendant une conversation.

```json
{
  "type": "information",
  "summary": "Le joueur affirme avoir vu Anton près du laboratoire."
}
```

Important : une information mémorisée n'est pas forcément vraie.

Elle représente ce que **le PNJ croit avoir appris**.

### relationship

Un changement important dans la relation.

```json
{
  "type": "relationship",
  "summary": "Erik considère désormais le joueur comme un allié."
}
```

---

# 8. Tags

Les tags permettent de retrouver facilement les souvenirs pertinents.

Exemple :

```json
"tags": [
  "anton",
  "murder",
  "laboratory"
]
```

Si le joueur demande :

```text
Qu'est-ce que tu sais sur Anton ?
```

le moteur peut chercher les souvenirs contenant :

```text
anton
```

Il n'est pas nécessaire de faire des embeddings ou une base vectorielle au début.

Une simple recherche par tags est suffisante.

---

# 9. Sélection des souvenirs avant un appel DeepSeek

Il ne faut pas envoyer les 50 souvenirs du PNJ.

Avant chaque appel API, sélectionner environ :

```text
3 à 6 souvenirs
```

Priorité :

```text
importance
+
pertinence avec la conversation
+
émotion
+
récence
```

Exemple très simple :

```text
score =
importance * 3
+ correspondance_tags * 5
+ abs(emotion)
```

Puis prendre les cinq meilleurs souvenirs.

On peut ajouter un petit bonus si le souvenir est récent.

---

# 10. Exemple

Le joueur demande :

```text
Tu fais confiance à Anton ?
```

Erik possède :

```json
[
  {
    "summary": "Anton a réparé gratuitement le système de sécurité d'Erik.",
    "importance": 2,
    "emotion": 2,
    "tags": ["anton", "security"]
  },

  {
    "summary": "Le joueur affirme avoir vu Anton près du laboratoire pendant le meurtre.",
    "importance": 4,
    "emotion": -2,
    "tags": ["anton", "murder"]
  },

  {
    "summary": "Naomi aime beaucoup le café.",
    "importance": 1,
    "emotion": 0,
    "tags": ["naomi"]
  }
]
```

Le moteur sélectionne automatiquement les deux souvenirs concernant Anton.

DeepSeek reçoit :

```text
Souvenirs pertinents :

- Anton a réparé gratuitement le système de sécurité d'Erik.
- Le joueur affirme avoir vu Anton près du laboratoire pendant le meurtre.
```

L'IA peut alors répondre naturellement :

```text
Anton m'a déjà rendu service, donc je n'ai jamais eu de raison
de me méfier de lui.

Mais ce que tu m'as raconté sur la nuit du meurtre...
ça change un peu les choses.
```

---

# 11. Création automatique d'un souvenir

Toutes les quelques interactions, le jeu peut demander à un LLM d'analyser ce qui vient de se passer.

Par exemple après 4 échanges.

Requête interne :

```text
Analyse cette conversation.

Détermine uniquement s'il existe quelque chose que ce personnage
devrait retenir à long terme.

Répond uniquement en JSON.

Si rien n'est important :

{
  "remember": false
}

Sinon :

{
  "remember": true,
  "type": "interaction",
  "summary": "...",
  "importance": 1-5,
  "emotion": -5 à 5,
  "tags": ["...", "..."],
  "relationship_changes": {
    "trust": 0,
    "affection": 0,
    "fear": 0,
    "suspicion": 0
  }
}
```

Exemple de réponse :

```json
{
  "remember": true,
  "type": "interaction",
  "summary": "Le joueur a révélé à Erik qu'Anton possédait une backdoor permettant de contrôler Écho.",
  "importance": 4,
  "emotion": -2,
  "tags": [
    "anton",
    "echo",
    "backdoor"
  ],
  "relationship_changes": {
    "trust": 2,
    "affection": 0,
    "fear": 0,
    "suspicion": -1
  }
}
```

Le moteur ajoute alors automatiquement cette mémoire.

---

# 12. Ne pas analyser chaque phrase

Pour éviter trop d'appels API :

```text
joueur
↓
conversation normale
↓
4 à 6 messages
↓
analyse mémoire
↓
création éventuelle d'un souvenir
```

Les événements importants du jeu peuvent, eux, créer directement une mémoire sans passer par le LLM.

Par exemple :

```c
npc_add_memory(
    erik,
    "Le joueur a sauvé le fils d'Erik.",
    5,
    5
);
```

---

# 13. Événements importants générés par le jeu

Certaines mémoires doivent être déterministes.

Par exemple :

```text
quête terminée
personnage tué
cadeau important
trahison
combat contre le PNJ
secret découvert
PNJ sauvé
PNJ menacé
```

Le moteur ajoute directement la mémoire.

Le LLM ne doit pas décider si l'événement s'est réellement produit.

Exemple :

```json
{
  "id": "mem_052",
  "type": "event",
  "summary": "Le joueur a sauvé Naomi pendant l'incendie du laboratoire.",
  "importance": 5,
  "emotion": 5,
  "tags": [
    "player",
    "naomi",
    "fire",
    "rescue"
  ]
}
```

---

# 14. Oubli

Limiter par exemple chaque PNJ à :

```text
30 souvenirs
```

Lorsque cette limite est dépassée :

ne jamais supprimer immédiatement les souvenirs avec :

```text
importance >= 4
```

Parmi les autres, supprimer celui ayant le score le plus faible.

Par exemple :

```text
memory_score =
importance * 10
+ abs(emotion) * 2
+ recent_bonus
```

Cela donne une forme d'oubli sans système compliqué.

Un PNJ conservera naturellement :

```text
Le joueur a sauvé mon fils.
```

mais oubliera progressivement :

```text
Le joueur m'a demandé où étaient les toilettes il y a trois semaines.
```

---

# 15. Souvenirs rappelés

Lorsqu'un souvenir est utilisé dans le prompt, mettre à jour :

```json
"last_recalled_at": 15320
```

Cela permet éventuellement de donner un petit bonus aux souvenirs régulièrement rappelés.

Un souvenir souvent utilisé devient ainsi plus durable.

---

# 16. Séparation connaissance / mémoire

Il est important de ne pas confondre :

```text
story facts
```

et :

```text
NPC memories
```

Les facts de `story.json` représentent la vérité définie par l'auteur.

Exemple :

```json
"fact_anton_phone": {
  "text": "Le téléphone d'Anton a borné près du laboratoire à 23:04.",
  "truth": true
}
```

Les memories représentent uniquement ce que le PNJ croit ou a vécu.

Exemple :

```json
{
  "summary": "Le joueur affirme qu'Anton était au laboratoire.",
  "type": "information"
}
```

Le PNJ peut donc croire quelque chose de faux sans modifier la vérité du monde.

---

# 17. Format complet recommandé pour save.json

```json
{
  "save_version": 1,
  "story_id": "projet_echo",

  "game_time": 4520,

  "player": {
    "name": "Alex",
    "known_fact_ids": [
      "fact_anton_phone"
    ],

    "discovered_clue_ids": [
      "analyse_code"
    ]
  },

  "npc_states": {
    "echo": {
      "relationship": {
        "trust": 20,
        "affection": 5,
        "fear": 15,
        "suspicion": 0
      },

      "recent_messages": [],

      "memories": []
    },

    "lea_sartori": {
      "relationship": {
        "trust": -10,
        "affection": -5,
        "fear": 0,
        "suspicion": 20
      },

      "recent_messages": [],

      "memories": []
    },

    "karim_idrissi": {
      "relationship": {
        "trust": 0,
        "affection": 0,
        "fear": 0,
        "suspicion": 10
      },

      "recent_messages": [],

      "memories": []
    }
  }
}
```

---

# 18. Construction du prompt d'un PNJ

Lorsqu'un joueur parle à un PNJ, construire le contexte dans cet ordre :

```text
PERSONNALITÉ DU PERSONNAGE

↓  

INFORMATIONS DU MONDE QU'IL CONNAÎT

↓

RELATION ACTUELLE AVEC LE JOUEUR

↓

3 À 6 SOUVENIRS PERTINENTS

↓

8 À 12 MESSAGES RÉCENTS

↓

NOUVEAU MESSAGE DU JOUEUR
```

Exemple :

```text
Tu es Erik, aubergiste de 54 ans.

Tu es méfiant, sarcastique et très protecteur envers ta famille.

Tu ne connais que les informations données ci-dessous.
N'invente jamais une connaissance concernant l'enquête.

Relation avec le joueur :
- confiance élevée
- affection moyenne
- aucune peur
- faible suspicion

Souvenirs pertinents :
- Le joueur a sauvé ton fils il y a plusieurs jours.
- Le joueur t'a prévenu qu'Anton pourrait avoir manipulé Écho.
- Tu considères maintenant le joueur comme quelqu'un de fiable.

Informations connues :
- Marc Vernon est mort.
- Anton possédait un accès privilégié aux systèmes du laboratoire.

Conversation récente :
...

Joueur :
Est-ce que tu me crois concernant Anton ?
```

---

# 19. Principe important

Le système doit respecter cette séparation :

```text
LLM
=
interprétation
dialogue
personnalité
souvenirs subjectifs

MOTEUR DU JEU
=
vérité
état du monde
quêtes
inventaire
personnages morts/vivants
preuves découvertes
relations numériques
```

Le LLM ne doit jamais pouvoir modifier directement la vérité du jeu.

Il peut seulement proposer :

```text
un dialogue
une nouvelle mémoire
un changement de relation
```

Le moteur valide ensuite les modifications.

---

# 20. Fonctions à implémenter

L'implémentation devrait exposer approximativement :

```c
NPCMemoryState *memory_get_npc(const char *npc_id);

void memory_add(
    const char *npc_id,
    const char *type,
    const char *summary,
    int importance,
    int emotion,
    const char **tags,
    int tag_count
);

void memory_add_recent_message(
    const char *npc_id,
    const char *role,
    const char *content
);

void memory_update_relationship(
    const char *npc_id,
    int trust_delta,
    int affection_delta,
    int fear_delta,
    int suspicion_delta
);

MemoryList memory_find_relevant(
    const char *npc_id,
    const char *player_message,
    int max_results
);

void memory_cleanup(const char *npc_id);

char *memory_build_context(
    const char *npc_id,
    const char *player_message
);

bool memory_save(const char *filename);

bool memory_load(const char *filename);
```

`memory_find_relevant()` doit sélectionner environ cinq souvenirs selon :

```text
importance
tags
émotion
récence
```

`memory_cleanup()` garantit que le nombre de souvenirs reste sous la limite configurée.

`memory_build_context()` construit le texte qui sera injecté dans le prompt DeepSeek.

---

# 21. Configuration

Prévoir quelques paramètres facilement modifiables :

```json
{
  "memory_config": {
    "recent_message_limit": 10,
    "long_term_memory_limit": 30,
    "memories_in_prompt": 5,
    "minimum_importance_to_store": 3,
    "memory_analysis_interval": 4
  }
}
```

Ainsi le système peut être réglé sans recompilation.

---

# Résumé de la mécanique

Le fonctionnement complet devient :

```text
Le joueur parle à Erik
        ↓
chercher 5 souvenirs pertinents
        ↓
charger personnalité + connaissances
        ↓
charger relation avec le joueur
        ↓
charger conversation récente
        ↓
appel DeepSeek en streaming
        ↓
afficher la réponse
        ↓
ajouter l'échange à recent_messages
        ↓
toutes les quelques interactions :
analyser la conversation
        ↓
éventuellement créer un souvenir
        ↓
modifier légèrement la relation
        ↓
sauvegarder dans save.json
```

L'objectif est qu'un PNJ puisse naturellement dire plusieurs heures plus tard :

> Attends… c'est toi qui m'avais parlé de la backdoor d'Anton, non ?

ou :

> Après ce que tu as fait pour ma famille, oui. Je te crois.

sans avoir besoin de renvoyer toute l'histoire des conversations au modèle.
