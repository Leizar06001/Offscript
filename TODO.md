# Prochaines modifications

# To do

- Ajouter un parametre au lancement de l'histoire pour choisir la difficulté
- (facultatif) Ecarter au tirage un coupable dont rien n'est atteignable sans lui
    Plus necessaire sur Projet Echo, ou les 5 suspects passent le test. Ce serait un garde-fou pour les
    histoires mal ecrites, en plus du message d'avertissement.

# Plus tard

- Un inventaire
- Ajouter des objets
- Des objets interactifs (ex: pc, digicode, ..)
- Avoir des portes ouvrables par certains NPC ou avec clé / code / a distance

# Done

- Le mode en cours se lit sur les cadres, et la ligne « piece + interlocuteur » ressort
    Seule la zone de saisie changeait d'aspect avec le mode : rien ne disait, en un coup d'oeil, si les
    fleches allaient deplacer le personnage ou le curseur.
    Le cadre ALLUME est celui sur lequel le clavier agit, la meme regle que la saisie qui verdit :
    hors mode discussion le PLAN est vert (les fleches deplacent le personnage) et l'ENTRETIEN gris ;
    en mode discussion c'est l'inverse. Les intitules suivent la couleur de leur cadre.
    `draw_window_frames()` (main.c) est appelee au demarrage et a chaque changement de mode — le rendu de
    la carte ne redessine pas le cadre, donc il fallait le rappeler explicitement. La colonne de droite
    repeinte a chaque image par draw_map_iso suit la meme couleur, sinon elle restait grise sur un cadre
    allume.
    La ligne 1 (piece, personnes presentes, interlocuteur vise) se perdait entre la barre du haut et le
    cadre : la piece est maintenant une pastille en video inverse, et l'interlocuteur vise porte un
    chevron « ▸ » et son nom en video inverse, au lieu d'un simple souligne qui se confondait avec les
    autres noms deja colores.
    Verifie hors ligne en rendant le meme code dans un ncurses hors du jeu (les fonctions sont static) :
    couleurs des deux cadres dans les deux modes, pastille de piece, chevron et inversion du nom vise,
    noms non vises en attenue. Pas encore vu dans une vraie partie.
    Cette ligne est aussi CENTREE dans le terminal, et separee de la barre des touches par une ligne
    vide (elle passe de la ligne 1 a la ligne 2, qui etait deja libre : les fenetres commencent a la 3,
    donc rien ne bouge en dessous). Pour la centrer il faut connaitre sa largeur avant de l'ecrire : elle
    est desormais assemblee en morceaux (texte + attributs), mesuree, puis dessinee — une mesure ecrite
    a part aurait fini par mentir. Effet de bord utile : le libelle de la touche s'abrege au lieu de se
    faire couper par la droite (a deux personnes dans la piece, la version longue depassait les 80
    colonnes). Geometrie verifiee sur 40, 60, 80 et 120 colonnes.

- Barre de progression au lancement de l'enquete
    Le briefing n'avait qu'un tourniquet (`Preparation de l'enquete... |`), et le lancement enchaine
    maintenant deux appels au modele : on ne voyait pas ou on en etait.
    La barre avance par ETAPES terminees, jamais en pourcentage : la duree d'un appel est inconnue, donc
    un pourcentage serait invente. Deux etapes fixes — « Reconstitution de l'affaire » (la resolution)
    puis « Verification / Constitution du dossier de preuves » (le comblement des trous, qui n'emet un
    appel que s'il y a des trous). L'etape en cours est traversee par un reflet qui va et vient, pour
    qu'on voie que ca travaille sans pretendre savoir combien il reste ; les etapes finies sont pleines
    et vertes. Le message final (dont les avertissements de solubilite) remplace la barre : il peut etre
    long et doit rester lisible sur un terminal etroit.
    La largeur s'adapte a la fenetre (40 colonnes au plus, moins si besoin) et se clampe a 20.
    Verifie hors ligne : geometrie simulee sur plusieurs largeurs (dont une largeur impaire, qui laissait
    un creux a la fin — corrige) et rendu d'une COPIE de la fonction dans un ncurses hors du jeu (elle
    est static dans menu.c, donc non liable depuis un test), pour confirmer les glyphes et les couleurs.
    Pas encore vu dans une vraie partie.

- Le modele complete lui-meme l'histoire au lancement quand elle n'est pas soluble
    Il generait deja la resolution, le brief du coupable et la liste des elements a charge ; il ne
    fabriquait pas ce qui manquait pour les OBTENIR. Une histoire ou le coupable tire au sort est le
    seul a connaitre ce qui l'accuse restait injouable, et le moteur se contentait de l'annoncer.
    Repartition des roles, dans la logique du reste du moteur : le moteur DECIDE, le modele ECRIT.
    1. Apres la resolution, `fill_solubility_gaps()` (menu.c) calcule les faits a charge que personne
       d'autre que le coupable ne peut donner — `story_fact_obtainable_without()`, rien a deviner.
       Zero trou = ZERO appel : une histoire bien ecrite ne coute rien de plus.
    2. `prompt_build_clue_fill()` ne demande que ces faits-la, avec la liste des detenteurs possibles
       calculee par le moteur (personnages places sur la carte, jamais le coupable) et les pieces qui
       existent deja, pour ne pas en refaire une equivalente. Le modele ne fournit que la piece
       (identifiant, nom, description dans le ton de l'affaire) et le detenteur choisi dans la liste.
    3. `clue_fill_parse()` jette tout ce qui n'est pas utilisable : fait invente, fait hors du perimetre
       demande, detenteur inconnu, detenteur absent de la carte, detenteur = le coupable, identifiant
       qui collerait avec un indice d'auteur.
    4. Chaque piece est doublee d'un `extra_knowledge` : le detenteur apprend le fait. C'EST CA qui
       debloque l'enquete — dans ce moteur, une piece n'est sortable que par quelqu'un qui connait
       deja l'un des faits qu'elle etablit. Une piece seule serait restee decorative.
    5. Le filtre des elements a charge tourne APRES la reparation : il n'ecarte plus que ce qui reste
       vraiment inatteignable, et le message « aucune preuve n'est accessible sans l'aveu » devient un
       dernier recours au lieu du cas courant.
    Ou ca vit : dans la SAUVEGARDE (`generated_clues`, `extra_knowledge`), jamais dans le fichier
    d'histoire. Les indices utiles dependent du coupable tire, donc de la partie ; l'histoire reste la
    verite de l'auteur, partagee par toutes les sauvegardes, et le jeu ne reecrit pas un fichier que le
    joueur n'a pas demande a modifier. `story_apply_additions()` les verse dans la Story chargee en
    memoire — a la reprise d'une partie et apres chaque generation — et le reste du moteur ne fait plus
    la difference avec un indice d'auteur. Idempotent, et il ne reloue jamais le tableau `characters`
    pour que les PNJ gardent leur pointeur `def`.
    `[R]` recharge l'histoire depuis le disque : sans ca, les indices de la partie precedente auraient
    traine dans la nouvelle, avec un autre coupable.
    Les vieilles sauvegardes n'ont pas ces deux cles : les tableaux restent vides et rien ne change.
    Verifie par `tests/test_clue_fill.c` (23 verifications, toutes passees) sur une histoire ecrite
    expres pour etre verrouillee : detection du trou, prompt qui ne demande que lui et ne propose pas le
    coupable, rejet des quatre formes de reponse invalide, application (le fait devient atteignable),
    idempotence, ecriture/relecture de la sauvegarde, et re-application au rechargement.
    Pas encore vu en partie reelle : il faut une histoire mono-source pour declencher un appel. Projet
    Echo, desormais doublement sourcee, n'en declenche aucun ; n'importe laquelle des autres (toutes en
    `clues: []` avec un seul detenteur par fait) le fera au premier lancement.

- Voir si on peut réduire les tokens 
- Reduire la consommation de jetons sans toucher a la qualite
    L'entree represente ~74% de la facture (mesure sur `guinet.json` : 2043 jetons d'entree contre
    481 de sortie par appel), et cette entree est presque toujours le MEME texte. Le fournisseur la
    facture une fraction du prix quand il peut la servir depuis son cache de prefixe — mais on cassait
    ce prefixe. Rien n'a ete retire du prompt : tout est du deplacement de blocs.
    - Prompt de dialogue : « Sont aussi presents, et vous entendent : ... » et « Tu es interroge par X »
      etaient a l'octet 755 sur 7000, en pleine partie stable. Ils descendent avec le reste de la scene,
      a cote de « TU TE TROUVES DANS ». Le bloc premier contact / deja parle, qui ne bascule qu'une fois
      par partie, remonte au contraire dans la partie stable.
      Prefixe commun d'un tour a l'autre : 11% -> 80% quand quelqu'un entre dans la piece,
      9% -> 79% pour une intervention. Meme scene, autre question : 100% avant comme apres.
    - Prompt d'analyse : l'echange a analyser etait ecrit EN TETE, donc les 3700 octets de listes et de
      consignes qui suivaient etaient refactures a chaque fois. Il passe en dernier : 4% -> 98%.
    - La liste d'indices de l'analyse ne contient plus que les pieces que CE personnage peut sortir,
      comme le prompt de dialogue (helper `character_holds_clue`, partage par les deux). L'analyse perd
      500 octets et ne peut plus attribuer a l'un ce qu'un autre detient.
    - `deepseek_client` lit enfin `prompt_cache_hit_tokens` / `prompt_cache_miss_tokens` et la barre du
      bas affiche « cache NN% » quand l'API les rapporte. Sans ca, l'effet des points ci-dessus n'etait
      pas mesurable en jeu. Compteur de session, pas conserve dans la sauvegarde.
    Non fait volontairement : le niveau de raisonnement reste a `high`. C'est le plus gros levier de
    sortie (481 jetons/appel pour une replique de ~180) mais c'est justement lui qui paye l'esquive et
    la confrontation.
    Asymetrie assumee : l'analyse met ses consignes de format AVANT l'echange (c'est ce qui donne 98%
    au lieu de ~55%), le dialogue les garde en toute fin. Une replique mal formee se voit a l'ecran et
    coute le tour au joueur ; une analyse mal formee est ignoree par `analysis_parse` et retentee a
    l'echange suivant. On prend donc le cache d'un cote, la fiabilite de l'autre. Les remonter aussi
    dans le dialogue vaudrait ~10 points de cache de plus, au risque du format.
    Les pourcentages ci-dessus sont des plafonds (part de prefixe partagee), pas une baisse de facture :
    le premier appel de chaque personnage est toujours un echec de cache, et le tarif d'un jeton mis en
    cache reste a verifier. C'est le « cache NN% » en jeu qui donnera le vrai chiffre.
    `OFFSCRIPT_DEBUG_USAGE=1` ecrit les reponses brutes de l'API dans /tmp/offscript_usage.log : c'est
    ce qui permettra de confirmer les noms exacts des compteurs de cache sur un vrai appel.
    Verifie hors ligne : les prompts avant/apres ont ete compares ligne a ligne (aucune information
    perdue, seul l'ordre change) et les prefixes communs mesures sur Projet Echo.

- Documenter les cles vides du modele d'histoire (`ressources/story_template.json`)
    `relationships`, `timeline`, `clues`, `memory_config` et `runtime_template` etaient des tableaux
    vides sans explication : impossible de deviner quoi y mettre, ni lesquelles servent vraiment.
    Chacune a maintenant son `_aide` et un exemple rempli qui charge tel quel, en disant la verite sur
    ce que le moteur en fait : `timeline` ne part qu'a la generation de la resolution, `relationships`
    est lu mais pas encore envoye au modele, `runtime_template` n'est pas lu du tout (c'est la forme de
    la sauvegarde), `memory_config: null` = les cinq valeurs par defaut, documentees une par une avec
    leur effet sur la consommation.
    `_aide_clues` explique la mecanique reelle : un personnage ne peut sortir une piece que s'il connait
    l'un des faits qu'elle etablit, il ne la mentionne jamais de lui-meme, et la produire credite tous
    ses faits d'un coup. Avec la regle des deux sources en tete de fichier.
    Le modele se conforme desormais a ce qu'il enseigne : ses deux personnages connaissent les deux
    faits, donc aucun fait n'est verrouille quel que soit le coupable (verifie avec le meme controle que
    Projet Echo). Au passage, le secret de personnage_a figure enfin dans ses `known_fact_ids`, comme
    le moteur l'attend.

- L'enquete semblait insoluble : malgre les questions, les npcs ne donnaient quasiment aucune information
  utile, les pistes de l'un etaient contredites par un autre, et rien ne permettait de conclure
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
    Les deux points laisses en suspens ont ete traites le 11/08/2026, voir les deux entrees suivantes.

- Projet Echo n'etait pas soluble : chaque fait n'avait qu'une source, et c'etait souvent le coupable
    Le moteur avait ete corrige, pas l'histoire. Mesure d'origine : 3 faits (`fact_murder_window`,
    `fact_cameras_off`, `fact_echo_move`) n'etaient connus de PERSONNE et reveles par aucun indice ;
    13 faits sur 17 n'avaient qu'un seul detenteur ; et chaque indice n'etait produisible que par la
    seule personne qui connaissait deja ce qu'il revele. Resultat, quel que soit le coupable tire, ses
    propres faits n'etaient accessibles que par sa bouche.
    L'intrigue est inchangee (meme victime, meme fenetre, memes mobiles, memes secrets, aucun coupable
    predefini). Ce qui change, c'est qui SAIT quoi :
    - Recoupements plausibles : Anton a la copie serveur du mail de Marc a Lea et voit les logs du
      terminal de Karim ; Karim a la telemetrie (coupure des cameras, canal moteur active, deplacement
      d'Echo, fenetre de la mort) ; Lea, co-fondatrice, connait le brevet et la mise en pause voulue par
      Marc, et a recu le releve telephonique d'Anton ; Naomi, qui evalue Echo, a vu son message de peur ;
      Echo a entendu la dispute de 22:30 qui le concernait.
    - Trois indices ajoutes, dont deux que l'histoire citait deja dans ses `reveal_conditions` sans
      qu'ils existent : `fouille_stockage_prive` (les cerveaux v1-6 de Karim), `enregistrement_audio`
      (la dispute de 22:30) et `rapport_autopsie` (mort + fenetre horaire). `analyse_terminal_karim`
      revele aussi l'heure de la coupure, `analyse_code` le deplacement commande a Echo.
    - Chaque secret reste un secret : il est juste atteignable autrement, par un indice que quelqu'un
      d'autre peut sortir. C'est ce que le moteur exige pour que l'aveu soit atteignable.
    Verifie par `tests/test_clue.c` (19 verifications) : aucun fait orphelin, aucune contradiction
    known/unknown, et pour CHACUN des 5 suspects pris comme coupable, tous ses faits ont une autre
    source. Avant : 3 suspects sur 5 n'en avaient aucune.

- Verifier qu'une piece a conviction est bien produite puis creditee
    Verifie hors ligne sur Projet Echo (`tests/test_clue.c`) : la passe d'analyse retient
    `produced_clue_id` quand l'identifiant existe et l'ignore quand il est invente ; l'indice est marque
    decouvert une seule fois ; tous les faits que revele `analyse_code` sont accordes au joueur et vus
    par le carnet. Le test rejoue a la main ce que fait npc.c:900 (une boucle sur les faits de
    l'indice) ; ces quelques lignes de liaison, elles, ne sont pas couvertes.
    Toujours pas vu en partie reelle, et ce n'est pas un defaut du moteur : `discovered_clue_ids` est
    vide dans les 5 sauvegardes existantes parce que les histoires jouees n'avaient AUCUN indice ecrit
    (voir « Rendre les 7 autres histoires solubles » dans To do).

- Le tirage des elements a charge ignorait l'atteignabilite des faits
    `generate_solution()` (menu.c) ne garde desormais que les elements a charge atteignables sans le
    coupable : un autre personnage les connait, ou une piece a conviction les etablit. Le seuil de
    victoire etant `(n+1)/2` borne a n, reduire la liste ne rend rien inatteignable.
    Contrepartie a surveiller : quand la liste est raccourcie, le seuil baisse avec elle (avec un seul
    element atteignable, l'aveu peut tomber sur UN fait au lieu de 2). Sur Projet Echo ce cas a disparu
    depuis que l'histoire est doublement sourcee, mais il reviendra sur les histoires encore
    mono-source : c'est le prix de la solubilite, et il vaut mieux le payer la qu'ici — remonter le
    seuil reverrouillerait l'enquete.
    Si AUCUN n'est atteignable, la liste d'origine est conservee et le briefing l'annonce
    (« Attention : aucune preuve n'est accessible sans l'aveu ») : mieux vaut le dire que livrer une
    enquete qu'on ne peut pas gagner sans le savoir.
    `story_fact_obtainable_without()` servait enfin, mais elle etait fausse sur deux points, corriges :
    elle comptait les personnages absents de la carte (`placed`) et les indices non decouvrables, et
    elle ignorait qu'un indice n'est produit que par quelqu'un qui connait l'un de ses faits — une piece
    que seul le coupable detient n'est donc plus un chemin de secours.
    Le prompt de resolution demande aussi au modele de privilegier ces faits, pour qu'il y ait moins a
    ecarter. Verifie par `tests/test_clue.c` sur les 5 suspects de Projet Echo.

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
