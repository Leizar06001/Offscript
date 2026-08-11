# Prochaines modifications

# Remarques gameplay

On dirait que l'enquete n'avance pas, malgré les questions les npcs ne sont pas si coopératifs et ne donnent quasiment aucune information qui aide a avancer. Le joueur n'obtient pas de pistes intéressantes, les quelques piste offertes par un npc sont contredites par un autre.. On dirait qu'il n'y a pas de solution pour résoudre l'histoire.

    DIAGNOSTIC (mesure sur save_echo_for_debug.json : 125 echanges, 1 fait etabli sur 17)
    Le joueur avait raison : cette partie etait litteralement insoluble.
    1. Les indices etaient du contenu mort. `memory_player_discover_clue()` n'etait appele de nulle part.
       Les 5 pieces a conviction de Projet Echo ne pouvaient donc jamais etre obtenues, et 3 faits
       n'etaient dans le known_fact_ids d'aucun personnage : ils etaient hors d'atteinte definitivement.
    2. Interblocage de la condition de victoire. Le coupable tire au sort etait Anton, et ses 3 elements
       a charge (fact_anton_phone, fact_backdoor_echo, fact_backdoor_activated) n'etaient connus que
       de lui — alors que son propre prompt lui dit de ne rien admettre « tant que l'enqueteur ne
       t'oppose pas une preuve materielle irrefutable ». Il fallait la preuve pour obtenir l'aveu,
       et l'aveu pour obtenir la preuve.
    3. Rien n'invitait un personnage a PARTAGER ce qu'il sait. Le prompt ne contenait que des consignes
       de retenue (« tu nies », « tu detournes », « n'invente rien »), aucune de cooperation. D'ou les
       renvois en boucle vers des actions que le jeu ne sait pas faire : « verifiez les logs »,
       « je vous le sors tout de suite », « allez interroger le systeme ».

    CORRECTIONS APPORTEES
    - Les indices existent enfin : la passe d'analyse peut signaler `produced_clue_id`, le moteur valide
      l'identifiant contre l'histoire, marque l'indice decouvert et accorde tous les faits qu'il revele.
    - Chaque personnage sait ce qu'il peut montrer : section « CE QUE TU PEUX MONTRER A L'ENQUETEUR »,
      construite a partir des indices dont il connait au moins un fait. Consigne explicite de sortir la
      piece et d'en dire le contenu au lieu de repondre que c'est confidentiel ou pour plus tard.
      Ceux qui l'accusent restent soumis aux regles de secret.
    - Consigne de cooperation sur les faits non secrets : « tu es un temoin, pas un adversaire ; ce que
      tu sais, tu le dis », et interdiction de renvoyer indefiniment vers un document a consulter.
    - `story_fact_obtainable_without()` permet de savoir si un fait est atteignable autrement que par la
      bouche du coupable — de quoi garantir la solubilite lors du tirage des elements a charge.

    RESTE A FAIRE (non verifie en jeu, cf. rapport)
    - Verifier en partie reelle qu'une piece est bien produite puis creditee.
    - Le tirage des elements a charge ne privilegie pas encore les faits atteignables sans le coupable.

# To do

- Ajouter un parametre au lancement de l'histoire pour choisir la difficulté
- Il est très difficile d'obtenir des informations
- Il faudrait peut etre ajouter des appels a l'IA sans que ce soit un npc, un peu comme un narateur, ou un mecanisme du jeu qui orchestre le tout 

# Plus tard

- Un inventaire
- Ajouter des objets
- Des objets interactifs (ex: pc, digicode, ..)
- Avoir des portes ouvrables par certains NPC ou avec clé / code / a distance
- Voir si on peut réduire les tokens 

# Done

- Pouvoir demander des indices (depuis le menu de pause)
    Entree « Demander une piste » dans le menu [Echap]. La piste est calculee a partir de l'etat reel
    du moteur, sans appel au modele : elle est instantanee, gratuite, et ne peut pas envoyer le joueur
    vers quelque chose qui n'existe pas — precisement le reproche fait aux personnages.
    Quatre niveaux, du plus utile au plus general : assez de preuves pour faire craquer quelqu'un /
    une personne jamais interrogee / une piece a conviction que quelqu'un detient sans l'avoir sortie
    (avec son nom et sa piece) / un temoin qui sait encore N choses non notees.
    Elle oriente sans resoudre : elle ne nomme jamais le coupable, et ne cite jamais le contenu d'un
    fait non etabli. Elle reste aussi dans le fil pour etre relue.
    Verifie sur la sauvegarde bloquee (1 fait sur 17) : elle pointe la piece a conviction « Message de
    peur d'Écho », son detenteur et sa piece.

- Résoudre les problemes de déplacements des npcs par le modele
    Le modele emettait bien l'ordre (verifie : `move=piece:salle_serveurs`), mais le personnage ne bougeait pas.
    Deux causes : la cle `move` etait contredite par la consigne de format, qui fermait le prompt en exigeant EXACTEMENT trois cles — le modele obeissait a la derniere et n'emettait jamais `move` ; et un personnage dont le premier pas etait occupe attendait indefiniment, or apres une conversation le joueur se tient justement la.
    Les personnages contournent maintenant les autres (`map_next_step_avoid`), et la consigne de format inclut `move` quand le personnage peut bouger.
    Verifie hors ligne sur les cinq formes d'ordre : `reste`, `approche`, `recule`, `piece:<id>`, `rejoint:<id>`, plus un identifiant invente (ignore), avec le joueur colle au personnage.
- Un npc qui se deplace pour parler a un autre npc : il ne se passait rien
    L'intention etait perdue en route. Le personnage retient desormais qui il va voir (`goes_to_talk_to`), et l'arrivee declenche l'echange.
    Verifie : Karim traverse le batiment du Laboratoire B jusqu'a la Salle de reunion et adresse la parole a Naomi en arrivant.

- Ajouter le concept de pièces, lors du passage des portes (v sur la map), revoir comment la map est définie, comment ajouter des noms aux pieces
    La carte appartient maintenant à l'histoire : bloc `map` dans le JSON (`width`, `rows`, `player_start`, `rooms`).
    Une pièce est décrite par un point intérieur (`at`), pas par un rectangle : le moteur remplit la zone, donc les pièces en L marchent (Laboratoire B).
    Les portes (`v`/`h`) sont détectées et reliées aux deux pièces qu'elles séparent, ce qui donne les trajets d'une pièce à l'autre.
    Projet Écho a ses 8 pièces nommées ; `ressources/story_template.json` montre le format.
- Le placement des npcs se fera par pièces plutot que par position absolue
    `game.room` dans la fiche du personnage, le moteur lui trouve une place libre. `x`/`y` restent acceptés en surcharge.
    Ajout au passage de `can_move` / `can_change_room`, modifiables en cours de partie.
- Revoir la methode d'affichage des chats pour permettre de remplacer le "un inconnu" par le nom du npc une fois decouvert
    Le fil est conservé en mémoire et redessiné, au lieu d'être écrit au fil de l'eau dans la fenêtre ncurses.
    Le nom n'est plus stocké dans le message : il est résolu au moment du rendu, donc tout l'historique se corrige d'un coup quand le personnage se présente. Vérifié en jeu avec Anton.
- Déplacer le texte qui indique les réaction des npcs dans la fenetre de chat
    Les actions arrivent dans le fil, en retrait et en gris. La bande du bas ne garde que les notes du moteur (« Note au journal : … »).
- Rendre le chat scrollable
    [PgUp] / [PgDn], avec un repère « v N lignes plus bas » quand on n'est pas en bas.
- Augmenter la limite de characteres prompt utilisateur, autoriser 3 lignes
    Tampon de 1024 octets, saisie repliée sur 3 lignes qui défilent, curseur déplaçable (fleches, Début/Fin, Suppr) et insertion au milieu du texte, le tout en UTF-8.
- Etendre l'UI a la hauteur de fenetre -3 lignes pour plus tard afficher des infos
    La mise en page se calcule à partir de la hauteur réelle du terminal au lieu de tailles fixes, et se réajuste au redimensionnement.
- A la place que le joueur doivent etre collé a un npc pour lui parler, le joueur peut engager la conversation avec le ou les npcs présents dans la pièce
    La cible est choisie dans la pièce (le plus proche d'abord). La ligne du haut affiche la pièce et qui s'y trouve.
- Ajouter mode discution, appui sur entrée (desactive les mouvements, permet de bouger le curseur). Sortie du mode discution en appuyant sur entrée avec texte vide
    [Entree] entre dans le mode, [Entree] à vide en sort. Hors de ce mode les touches n'écrivent rien : elles restent libres pour les actions à venir.
- Il y a eu un bug ou on ne pouvait plus avoir de reponse, c'etait toujours ecrit "Laissez-le finir de repondre"
    Trois causes possibles corrigées, voir le rapport : verrous jamais initialisés (donc inopérants sur macOS), aucune limite de temps sur une requête en vol, et question consommée puis perdue quand l'envoi était refusé.
- Mettre a jour plus frequemment le fichier save
    La sauvegarde ne suivait que les échanges. Elle passe maintenant toutes les 20 s et systématiquement en quittant.
    Elle contient aussi l'état du monde (`player.x/y`, bloc `world` par personnage : position, visage, `met`, `name_known`, autorisations) : on reprend exactement où on s'était arrêté. Vérifié en relançant une partie.
- Detection de game over (mystere résolu)
    Le moteur compte les faits à charge que le joueur a réellement établis. Au-delà de la moitié (minimum 2), le prompt du coupable l'autorise à craquer et à poser `"confession": true`.
    Le moteur ne l'accepte que du coupable tiré au sort ET seuil atteint : accuser au hasard ne peut pas gagner. Vérifié dans les deux sens — avec preuves Léa avoue et l'écran de résolution s'ouvre, sans preuves la même accusation est niée et la partie continue.
- Ajouter option pour connaitre la solution et explication + reset l'histoire
    `[F5]` ouvre l'écran de résolution : coupable, explication figée au début de partie, et la liste des éléments à charge en marquant ceux que le joueur avait trouvés.
    `[R]` relance l'enquête : nouveau coupable, nouvelle résolution, mémoire des personnages et faits connus remis à zéro. Vérifié.
- Faire en sorte que les npc puissent se deplacer
    Moteur de trajectoire (parcours en largeur) qui respecte exactement les règles de déplacement du joueur, portes comprises.
    Errance : chaque personnage change de place dans sa pièce toutes les 15-40 s. Suspendue pendant une conversation, mais seulement pour les gens de la pièce concernée — ailleurs la vie continue.
    Sur instruction du modèle : `"move"` valant `reste`, `approche`, `recule`, `piece:<id>`, `rejoint:<id>`. Le moteur valide tout (autorisations, pièce existante, trajet possible) avant d'appliquer.
    `can_move` / `can_change_room` dans le JSON, recopiés dans l'état de jeu donc modifiables en cours de partie, et sauvegardés.
    Vérifié sur 84 s : les cinq personnages bougent et aucun ne quitte sa pièce.
- Empecher 2 npc ou joueur/npc d'être sur la meme case
    Le joueur ne traverse plus personne, et un PNJ qui trouve sa case occupée attend au lieu de se superposer.
- Si plusieurs npc dans une piece, les npcs peuvent intervenir entre eux
    Après un échange, quelqu'un d'autre de la pièce peut réagir (45 % de chances). Il a le droit de se taire, et le silence ne laisse aucune trace.
    Chaîne bornée à 2 interventions : le joueur récupère toujours la parole. Reprendre la parole remet le compteur à zéro et annule une intervention en attente.
    Vérifié en jeu : Naomi répond, Karim la coupe, Naomi lui répond, puis la chaîne s'arrête.
- Vérifier que les infos du journal sont bien mises a jour, les attitudes envers le joueur aussi
    Tout ce que montre le carnet vient de la passe d'analyse, qui ne tournait qu'un échange sur quatre : le carnet restait donc en retard de trois échanges.
    L'analyse en attente est maintenant soldée dès qu'on quitte la pièce de la personne — c'est-à-dire juste avant d'aller consulter le carnet.
    Le carnet indique aussi où chaque personne a été vue, utile maintenant qu'elles se déplacent.
- Terminer les couleurs du texte, améliorer l'UI
    Intitulés dans les cadres (PLAN, ENTRETIEN, VOUS). Le cadre de saisie s'allume en vert en mode discussion et affiche « [Entree] pour prendre la parole » quand il est inerte.
    La ligne du haut indique la pièce, qui s'y trouve, et souligne la personne à qui la question s'adressera. Les noms de touches y ont la même couleur que dans la barre du haut.
- Le nom du Dr Karim Idrissi n'était jamais reconnu quand il se présentait
    On ne cherchait que le PREMIER mot du nom, donc « Dr. » — un mot qu'il ne prononce jamais.
    Maintenant n'importe quelle partie du nom suffit (« Karim » ou « Idrissi »), les civilités sont ignorées, et la recherche se fait sur des mots entiers pour que « Me » ne se trouve pas dans « même ». Couvert par un test hors ligne (13 cas).
- Avec deux personnes dans une pièce, une seule pouvait répondre
    La cible était toujours la plus proche, sans moyen d'en changer : la seconde personne était injoignable.
    [Tab] fait passer d'un interlocuteur à l'autre, et la nommer dans la question suffit aussi. La personne visée est soulignée dans la ligne du haut.
    Au passage : les personnages se désignent entre eux par leur vrai nom (ils se connaissent), et entendre un nom l'apprend au joueur — demander « qui est avec vous ? » révèle les deux. Vérifié en jeu.
- Utiliser deepseek-v4-pro pour la generation de l'histoire, passer sur le deepseek-v4-flash pour tout le reste
    Deux modèles : le rapide pour chaque réplique et chaque analyse mémoire, le fort pour le seul appel qui écrit la résolution.
    `DEEPSEEK_MODEL` et `DEEPSEEK_MODEL_STORY` permettent d'en changer sans recompiler. Vérifié : une nouvelle partie génère bien sa résolution avec le modèle fort.
- Ajouter un compteur d'usage de l'API (tokens / prix si possible)
    Les jetons sont lus dans les réponses de l'API, y compris en diffusion (`stream_options.include_usage`, sans quoi les dialogues ne comptaient pour rien).
    Affiché sur la dernière ligne de l'écran : appels, jetons entrée/sortie, estimation de prix, et le modèle en cours.
    Le total est conservé dans la sauvegarde, donc il continue d'une session à l'autre. Vérifié (4 appels puis 6 après relance).
    Les tarifs sont deux constantes en tête de `src/deepseek_client.c` : c'est une ESTIMATION, pas une facture.
- Le joueur doit pouvoir rentrer son nom au lancement, plusieurs sauvegardes par histoire
    Après le choix de l'enquête, un menu liste les parties enregistrées (nom, nombre d'échanges, [resolue]) ou propose une nouvelle enquête.
    Le joueur ne saisit que son nom ; le titre vient de l'histoire (`story.player_title`), il devient « Detective Poireau ».
    La sauvegarde porte son nom : `saves/<histoire>/poireau.json`. Le nom est nettoyé avant de servir de nom de fichier (pas de séparateur de chemin possible).
    Les anciens `save.json` restent lisibles et apparaissent dans la liste. Vérifié de bout en bout.
- Avoir une indication que le NPC réflechi (est replacé par sa réponse dans le chat)
    La ligne du personnage s'ouvre dès l'envoi, avec des points qui s'animent, et la réplique vient les remplacer au même endroit.
    La ligne d'attente disparaît si rien ne vient (erreur, ou intervention où le personnage choisit de se taire).
    A révélé au passage un défaut d'ordre : la question du joueur s'écrivait après la réponse. Corrigé.
- [ESC] au choix de la sauvegarde fermait le jeu au lieu de revenir en arriere
    [ESC] remonte maintenant d'un cran : de la liste des parties vers la liste des enquêtes, de la saisie du nom vers la liste des parties.
- Pouvoir bouger avec ZQSD ou WASD hors conversation, menu d'options pour changer les touches
    Toutes les actions sont reglables, chacune avec deux touches : fleches en principal, ZQSD en secondaire par defaut.
    Le menu propose aussi les dispositions ZQSD et WASD toutes faites, et « tout remettre par defaut ». Une touche deja prise ailleurs est liberee au lieu de creer un doublon silencieux.
    Verifie en jeu : passage en WASD, puis deplacement avec W/A/S/D d'une piece a l'autre et retour.
- Dans les options ajouter la possibilité de changer le prix des tokens
    Les deux tarifs (jetons envoyes / recus) se saisissent dans le menu et alimentent l'estimation de la barre du bas.
- Ajouter un fichier de sauvegarde pour les options, indépendant des parties
    `~/.config/enquete/options.json`, a cote de la cle d'API. Rien a voir avec les sauvegardes : changer de partie ne touche pas aux reglages.
- Menu d'options accessible depuis la liste des enquetes ([O]) et en jeu ([Echap])
    [Echap] ouvre un menu : Reprendre, Carnet, Options, Solution, Quitter. Le carnet garde son raccourci direct [F3].
    « Solution » demande confirmation avant de reveler le coupable. Les touches F4 et F5 ont disparu.
- Les fichiers JSON ecrits par le jeu etaient invalides en locale francaise
    `setlocale(LC_ALL, "")` fait ecrire « 0,42 » a printf et fait s'arreter strtod au point : le fichier d'options produit etait illisible, et n'importe quel nombre decimal d'une histoire aurait ete mal lu.
    Le jeu ne prend plus la locale que pour l'UTF-8 (`setlocale(LC_NUMERIC, "C")`).
- Dans les options avoir le choix du raisonnement du modele (low, high, max. Defaut is high)
    Ligne « Raisonnement » dans les options, qui tourne entre low / high / max. Envoye en `reasoning_effort` dans la requete, et conserve dans le fichier d'options.
    Une valeur inconnue lue dans le fichier est ignoree plutot que transmise a l'API.
- Ajouter des instruction dans le template pour expliquer comment bien le remplir
    Le JSON n'accepte pas de commentaires : les explications sont dans des cles `_lisez_moi` / `_aide`, que le moteur ignore.
    Attention signalee en tete : jamais de cle `_aide` dans `facts`, dont chaque cle est lue comme un identifiant de fait.
    Le modele se charge maintenant sans erreur tel quel (2 personnages, 2 pieces, 2 faits) : on le copie, on lance, ca marche, puis on remplace les textes.
- Pouvoir changer son API KEY
    Entree « Changer la cle d'API » dans les options, avec confirmation : la cle enregistree est effacee puis redemandee.
- Petits bugs d'affichage : ordre des npc, murs qui ne masquent pas correctement
    Joueur et PNJ sont desormais dessines ensemble, tries du fond vers l'avant, au lieu des PNJ dans l'ordre du tableau puis le joueur toujours en dernier.
    `dist_wall` n'etait pas reinitialise entre deux PNJ : des qu'un personnage etait derriere un mur, tous les suivants heritaient de son masquage.
    Les PNJ ne testaient que les murs hauts ('1') alors que le joueur testait aussi les murets ('2') ; la regle est maintenant la meme pour tout le monde.
    Les emojis font deux colonnes : la partie d'un personnage recouverte par quelqu'un de plus proche n'est plus dessinee du tout, sinon il en restait une moitie a l'ecran.
- Une sauvegarde non nommee "save" n'etait pas detectee dans la liste des enquetes
    `story_list()` cherchait encore le seul fichier `save.json` en dur, alors que les parties portent maintenant le nom du joueur. Il passe par `save_exists()`, qui parcourt le dossier.
    Verifie : les enquetes dont la partie s'appelle `sarah.json` / `coco.json` affichent bien [sauvegarde].
- Le champ "type" (genres) de l'histoire etait trop court
    Il etait coupe par un `%.30s`, qui compte des OCTETS : un genre accentue etait donc tronque encore plus tot que 30 caracteres.
    La largeur se calcule maintenant sur la fenetre, et la coupe se fait en colonnes sans casser un caractere. Meme correction sur la liste des parties.
- Cartes de "La Fausse Princesse", "Le Sabotage d'Orion" et "Projet Chimere" a refaire (que des murs '2', pas de murs accoles)
    Les trois cartes sont redessinees : murs uniquement en '2', d'une seule case d'epaisseur (aucun bloc 2x2), et coordonnees des pieces corrigees.
    Elles cachaient un defaut bien plus grave : le joueur avance de DEUX colonnes a la fois, donc une porte horizontale posee sur une colonne de la mauvaise parite est infranchissable.
    Resultat, 5 personnages etaient litteralement injoignables (Lyra Sen, Dorian Pike, Elian Varga, Ines Morel, Nora Bensaid) et 8 pieces inaccessibles. Toutes les portes 'h' sont maintenant sur la bonne parite.
    Verifie avec le moteur lui-meme (meme parcours en largeur que le jeu) : chaque piece et chaque personnage sont joignables depuis le depart, et depuis la sauvegarde existante d'Orion.
    Au passage : une position enregistree qui ne correspond plus a la carte (mur, ou mauvaise parite apres redessin) est ignoree au profit du point de depart, sinon on reprenait une partie sans pouvoir rejoindre personne.
