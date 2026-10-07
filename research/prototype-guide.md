# dockctl — Dell WD22TB4 sur macOS

> Archive du prototype, antérieure à la bibliothèque. Les anciennes commandes
> `./dockctl` de ce document correspondent désormais à `./research/dockctl-probe`,
> à exécuter depuis la racine du projet. Pour la CLI actuelle, voir [README](../README.md).

Prototype natif en C qui interroge le contrôleur du dock via USB HID et son pont
HID-I2C. Aucun paquet Homebrew, pilote tiers ou service à installer.

```sh
make check
./dockctl list
./dockctl info
./dockctl fan-probe
```

La compilation nécessite les outils de ligne de commande Xcode. Le binaire est
déjà compilé pour ce Mac dans ce dossier.

`list` détecte l'interface Dell `413c:b06e`. `info` tente de lire le modèle, le
Service Tag, la puissance de l'alimentation, les états bruts des ports et les
versions des composants. Le Service Tag est un identifiant du matériel : vous
pouvez le masquer avant de partager le résultat.
Pour les EC 01.01.00.03 et 01.01.00.16 analysés, `info` décode aussi le bit
`0x0008` des états de ports : son absence empêche le traitement des commandes
thermiques par l'EC. Sa présence est nécessaire, sans garantir une lecture réussie.

Le protocole envoie un rapport HID OUTPUT pour **demander une lecture**, puis
récupère un rapport INPUT. Seules les commandes EC de lecture `0x05` (type),
`0x03` (identité) et `0x02` (versions) sont autorisées. L'utilitaire ne contient
aucune commande de flash, de reset ou de réglage du ventilateur. `fan-probe`
est expérimental : il tente uniquement la lecture des registres d'identification
AMC6821 `0x3D` et `0x3E` aux neuf adresses documentées par TI. Le pont utilise
son réglage documenté le plus lent (250 kHz) ; TI spécifie 100 kHz pour
l'AMC6821. Un échec ne permet donc pas de conclure que la puce est absente
ou inaccessible par toute autre méthode.

## État de validation sur ce Mac

- Compilation avec `-Wall -Wextra -Werror` réussie.
- `make check` valide les trames, le filtrage des commandes et les limites des réponses.
- `./dockctl list` détecte le dock.
- `./dockctl info` fonctionne depuis Terminal et depuis la session de l'agent
  après la levée de son confinement. Les échanges HID sont validés sur le dock.
- Le dock déclare une alimentation de 130 W, également confirmée sur le bloc
  secteur par l'utilisateur. Dell prévoit un bloc de 180 W pour le WD22TB4.
  Cela ne suffit pas à établir la cause du bruit.
- `fan-probe` échoue dès la première adresse avec `SET_REPORT: 0xe0005000`.
  Aucun identifiant de contrôleur de ventilateur ni aucune télémétrie n'a été obtenu
  par ce pont HID-I2C.
- Les lectures thermiques via Power Delivery sont validées sur EC 01.01.00.03 :
  mode automatique, classe de vitesse 1 (entre 0 et 2750 tr/min, bornes exclues),
  températures locale 34 °C, distante 41 °C et module 64 °C lors de l'essai.
- L'arrêt temporaire d'une minute est exécuté : départ 33/35/62 °C,
  maxima observés 34/36/64 °C, aucun seuil atteint, puis retour automatique
  et rotation confirmés. Cela caractérise cet essai, pas un régime permanent.

Depuis un Terminal macOS :

```sh
cd /Users/mowmow/Lab/dell-wd22tb04
./dockctl info
```

Le firmware EC analysé contient des commandes de lecture et de réglage thermique
transmises par Power Delivery, distinctes du protocole USB HID ci-dessus.
Voir [les résultats de rétro-ingénierie](research/thermal-protocol.md).
Leur lecture depuis macOS fonctionne avec `hpm-probe` après l'entrée en mode Dell ;
le réglage `thermal-auto` est aussi validé par un succès EC et une relecture du mode 0.

Un deuxième prototype lit le contrôleur Power Delivery natif du Mac :

```sh
make research/hpm-probe
sudo ./research/hpm-probe
```

L'interface AppleHPM nécessite les droits administrateur sur ce Mac. Les premières
lectures ont réussi en mode `APP` ; le dock est raccordé au contrôleur `RID 2`.
Sans argument, le prototype lit les registres et les tampons VDM ordinaire
et Attention via AppleHPMLib. Le registre `0x4e` et `ReceiveVDMAttention` sont
ajoutés pour chercher les notifications dans un canal distinct. La lecture
Attention a d'abord réussi avec un tampon vide, puis a livré les réponses thermiques
après l'entrée en mode Dell. Aucun VDM n'est envoyé
sans argument.
`sudo ./research/hpm-probe discover` envoie deux requêtes standard Discover Modes
(DisplayPort puis Dell), après vérification de la connexion et de l'identité
du dock. Il n'entre dans aucun mode et ne modifie pas le profil thermique.
Ce test a reçu un ACK Dell et deux VDO de modes (`1`, `2`) : la communication
PD macOS ↔ WD22TB4 est confirmée. La lecture thermique nécessite l'étape suivante.

Les anciens essais `thermal-probe` et `thermal-session` sont **désactivés**.
La session a obtenu un ACK Enter Mode 1, mais aucune donnée thermique ;
lors de la tentative de sortie, le tampon RX s'est vidé et les écrans ont
été coupés. Le dock a dû être réactivé avec son bouton. Le lien causal exact
reste à déterminer ; la sémantique du mode Dell 1 n'était pas établie.
Ces arguments quittent désormais avant tout accès matériel. Aucun réglage
thermique n'a été envoyé.

Un test distinct capture maintenant les deux canaux **pendant** les requêtes :

```sh
sudo ./research/hpm-probe thermal-read
```

Si le mode Dell est encore engagé après l'essai réussi, le test suivant ajoute
un accusé de réception expérimental `0x13` après chaque réponse thermique
Attention reconnue (au plus deux envois au total) :

```sh
sudo ./research/hpm-probe thermal-read-ack
```

Il vise à vérifier si cet accusé arrête les répétitions observées toutes les
125 ms environ. Il ne renvoie aucun Enter/Exit et ne change aucune consigne
thermique. L'essai utilisateur a encore lu 34/35/64 °C, mais les répétitions
ont continué malgré cet acquittement unique. Le compteur RX seul ne permet pas d'établir que chaque
notification répond à la dernière requête : une réponse répétée peut être ancienne.

Le test suivant acquitte aussi les nouvelles répétitions reconnues de profil
ou de température, avec un plafond de 16 accusés par phase de trois secondes :

```sh
sudo ./research/hpm-probe thermal-read-ack-loop
```

Il vérifie l'hypothèse d'une réponse conservée dans la file de réémission EC.
Il n'envoie ni Enter/Exit ni consigne. L'essai a réussi : deux accusés pour le
profil, un pour les températures 33/35/64 °C, puis aucune répétition observée
jusqu'à la fin de chaque fenêtre. `thermal-read` et les lectures après
`thermal-enter-hold` utilisent désormais ce même acquittement borné.

Il reprend les deux payloads de lecture expérimentaux, sans Enter/Exit Mode,
et surveille VDM et Attention pendant trois secondes après chaque envoi.
Les changements sont horodatés ; la connexion et l'identité sont revérifiées
toutes les 250 ms environ. Une erreur arrête le test sans commande de sortie.
Les réponses sont décodées d'après le firmware EC analysé. Le code de retour 3
signifie qu'une réponse thermique manque ou que le plafond de 16 accusés a été
atteint. Aucun argument ne permet une consigne arbitraire.
Le test a été compilé et vérifié hors matériel ; son exécution depuis l'agent
est bloquée par le mot de passe sudo, à saisir uniquement dans Terminal.
Lors des premiers essais sans mode Dell actif : deux ACK Dell vides, aucun changement du
tampon Attention, aucune signature thermique. Le premier ACK était marqué
SOP 1, le deuxième SOP 0 ; cette différence reste inexpliquée.

Pour valider une première écriture depuis macOS, une fois le mode Dell engagé :

```sh
sudo ./research/hpm-probe thermal-auto
```

Cette commande envoie une seule demande `02 10 00 01` (mode automatique,
classe valide 1), acquitte les réponses puis
relit le profil. Elle ne confirme le résultat qu'après un succès EC `01 80 01`
et un mode relu égal à zéro. Elle ne force aucune vitesse, n'envoie aucun
Enter/Exit et ne devrait pas réduire le bruit puisque le mode actuel est déjà
automatique. L'essai utilisateur a reçu le succès EC et confirmé le mode 0.
La commande n'exige plus de lecture de température préalable : elle peut aussi
servir à restaurer la régulation lorsqu'une lecture de capteur échoue.

Le premier essai de changement de mode est temporaire :

```sh
sudo ./research/hpm-probe thermal-low-test
```

Il exige un mode initial automatique, une classe de vitesse 0 ou 1, et des
températures positives ou nulles sous 62/70/73 °C (locale/distante/module).
Il demande ensuite le mode forcé à 1900 tr/min, vérifie le succès EC et le mode 1,
surveille les températures puis restaure et vérifie le mode automatique.
L'objectif est d'environ 12 secondes depuis l'envoi initial, plus les latences
de communication et la vérification du retour. Une température atteignant un
seuil, une lecture échouée ou Ctrl-C conduit à la restauration ; laisser le
processus terminer. Aucun Enter/Exit ni arrêt du ventilateur n'est envoyé.

Cette vitesse peut être identique à la vitesse actuelle. Ce n'est pas un profil
silencieux permanent : le mode forcé remplace les courbes thermiques pendant
l'essai. La restauration est tentée même si l'écriture initiale échoue, mais
elle dépend de la liaison et du processus : un arrêt brutal (`kill -9`) ne peut
pas être intercepté. En cas de retour non confirmé, utiliser `thermal-auto`
dès que le dock répond. L'essai utilisateur a confirmé la transition 0 → 1 → 0,
des températures stables à 33/35/64 °C et une classe mesurée 1 pendant l'essai.
Aucun changement audible notable n'a été rapporté. Cela reste compatible avec
une vitesse initiale déjà proche de 1900 RPM, sans mesure exacte permettant de
l'affirmer.

Pour vérifier un changement physique distinct du simple changement de mode :

```sh
sudo ./research/hpm-probe thermal-high-test
```

Ce test reprend les mêmes préconditions, la surveillance et la restauration,
avec une consigne de 3600 tr/min pendant environ 12 secondes. Il devrait être
plus audible et cherche une classe mesurée 2 (au moins 2750 tr/min) pendant le
mode forcé. Il revient ensuite en automatique. L'absence de classe 2 observée
est signalée explicitement. L'essai physique est validé : accélération audible,
passage de la classe mesurée 1 à 2, puis mode 0 relu après restauration.
La première mesure après restauration était encore en classe 2 ; la lecture
ultérieure `thermal-read` confirme le mode 0 et la classe 1, avec 33/35/64 °C.
Le ralentissement après retour automatique est donc aussi validé.
Ce n'est pas une solution au bruit.

Le handler EC de la commande identifiée ne propose que 0, 1900 et 3600 RPM ;
aucune vitesse arbitraire sous 1900 RPM n'a été trouvée dans ce chemin.

Un arrêt bref peut être essayé uniquement avec cette commande expérimentale :

```sh
sudo ./research/hpm-probe thermal-stop-test
```

Le test demande zéro RPM pendant environ neuf secondes avant restauration
automatique, avec vérification du mode puis jusqu'à trois lectures pour observer
le redémarrage du ventilateur. Il exige initialement le mode automatique, une
classe 0 ou 1, et des températures dans [0,40[, [0,45[, [0,66[ °C.
Ces limites sont des garde-fous expérimentaux, pas une validation de refroidissement
passif. Une nouvelle lecture des températures a lieu pendant l'essai ; atteindre
un seuil ou échouer entraîne la restauration, comme Ctrl-C. La durée dépend
des latences I/O : aucun minuteur autonome du dock n'a été établi.

Laisser le processus finir et conserver la liaison. Un kill brutal, un crash ou
une perte de communication peuvent empêcher la restauration. Il n'existe pas
de commande d'arrêt permanent dans l'outil. Une classe mesurée 0 peut aussi
signifier une mesure indisponible : l'observation acoustique est nécessaire.
L'essai physique de neuf secondes a réussi : arrêt constaté par l'utilisateur,
températures lues 33/35/64 °C, puis mode automatique et rotation confirmés.
La lecture précoce en mode forcé était encore de classe 1 ; aucun relevé de
classe 0 n'a été capturé pendant ce test court.

Pour mesurer l'évolution thermique sur une durée plus utile :

```sh
sudo ./research/hpm-probe thermal-silence-test
```

Ce test réutilise exactement l'arrêt surveillé, avec une durée cible de
60 secondes. Les mêmes seuils 40/45/66 °C, la restauration sur erreur ou
interruption et la vérification de rotation restent actifs. Les lignes
`Suivi thermique` et `Bilan thermique` montrent l'évolution et les maxima
observés. Les horodatages indiquent la fin de chaque lecture de trois secondes,
pas l'instant exact de mesure du capteur. Les seuils sont contrôlés après
chaque échange ; ce n'est pas une surveillance matérielle instantanée.
Les latences I/O peuvent retarder l'échéance, et les limites de restauration
décrites plus haut restent applicables. Ce test n'installe aucun service et
ne valide pas un usage silencieux permanent. L'essai physique est effectué :
60,331 secondes jusqu'à la fin de la dernière lecture, départ 33/35/62 °C,
maxima 34/36/64 °C, puis restauration et rotation confirmées. Aucun seuil
n'a été atteint. Ne pas extrapoler cette minute à une autre charge ou à une
durée indéfinie ; aucun régime thermique permanent n'a été établi.

Pour isoler l'entrée de mode de la sortie qui accompagnait l'incident :

```sh
sudo ./research/hpm-probe thermal-enter-hold
```

**Expérimental.** Les deux essais sans découverte préalable ont reçu un NAK ;
le dernier n'a coupé aucun écran. La version actuelle commence par Discover Modes
Dell et exige un ACK annonçant le VDO `1` en première position avant tout Enter.
Dans la ROM TI étudiée, cette découverte initialise les positions utilisées pour
retrouver le mode ; son absence est une cause possible des refus précédents.
Cette séquence corrigée a reçu un ACK Enter et les deux réponses thermiques sur
le dock. Le bit EC du port 0 est passé de `0x0060` à `0x0068`.
Le test envoie ensuite une seule entrée en mode Dell 1, exige son ACK sur SOP 0
dans la première seconde, attend trois
secondes depuis l'envoi en surveillant la connexion, puis exécute les lectures
ci-dessus. Il ne contient aucune sortie automatique : le mode peut rester
engagé jusqu'à une déconnexion normale, y compris si le test échoue ou est
interrompu. L'effet d'Enter seul sur les écrans n'est pas établi. La présence
du lien PD ne garantit pas celle des écrans ou des ports downstream.
Après l'essai, `./dockctl info` permet de vérifier si le bit thermique EC est
devenu présent. La lecture fournie par l'utilisateur après l'entrée réussie
donne `0x0068 / 0x0000`, avec le bit présent sur le port 0.

L'en-tête provient d'[AsahiLinux/macvdmtool](https://github.com/AsahiLinux/macvdmtool),
cloné dans `research/macvdmtool` ; son code et sa licence Apache-2.0 y sont conservés.

## Préparation de la mise à jour — 7 octobre 2026

`fwupd` 2.1.8 est installé via Homebrew, sans service en arrière-plan.
L'installation Homebrew a aussi installé ou mis à jour ses dépendances.
Le paquet Dell/LVFS est téléchargé dans
`firmware/DellDockFirmwareUpdateLinux_01.01.15.cab`. Son SHA-256 correspond au
catalogue LVFS :

```text
4bc576f1d44b4dfda66bca65847a463165e678a3e57a972c34ff7cf4248ad42f
```

`fwupdtool get-details` reconnaît les métadonnées et les payloads comme fiables.
Le paquet contient plusieurs familles de docks ; cette vérification ne remplace
pas la détection du matériel et la sélection des GUID par fwupd.

| Composant | Lu sur ce dock | Disponible dans ce paquet |
|---|---|---|
| EC | 01.01.00.03 | 01.01.00.16 |
| Hub USB Gen2 | 01.47 | 01.66 |
| Hub USB Gen1 | 01.21 | 01.25 |
| DisplayPort MST VMM5331 | 05.06.03 | 05.07.08 |

L'inventaire fwupd exécuté par l'utilisateur a confirmé le WD22TB4 et les versions
ci-dessus. Le numéro du package est `01.00.25.01` : l'affichage initial de
`dockctl` inversait ses octets et a été corrigé.
La note de version du paquet annonce une amélioration de stabilité ; elle ne
promet pas de correction du bruit du ventilateur.

Le backend USB de fwupd nécessite les droits administrateur. L'inventaire déjà
réalisé dans Terminal peut être reproduit ainsi, sans installation de firmware :

```sh
sudo /opt/homebrew/bin/fwupdtool --plugins dell_dock get-devices
```

Aucun firmware n'a été installé. À la demande de l'utilisateur, la mise à jour
est différée ; la priorité est le contrôle du dock depuis macOS.

## Références du protocole

Les commandes et la disposition des données suivent le projet fwupd :

- [Transport HID-I2C](https://github.com/fwupd/fwupd/blob/main/plugins/dell-dock/fu-dell-dock-hid.c)
- [Commandes du contrôleur EC](https://github.com/fwupd/fwupd/blob/main/plugins/dell-dock/fu-dell-dock-ec.c)
- [Structures des réponses](https://github.com/fwupd/fwupd/blob/main/plugins/dell-dock/fu-dell-dock.rs)
- [Registres et adresses AMC6821](https://www.ti.com/lit/ds/symlink/amc6821.pdf)
- [Catalogue LVFS consulté](https://cdn.fwupd.org/downloads/firmware.xml.gz)
- [Alimentation requise pour le WD22TB4](https://www.dell.com/support/manuals/en-za/wd22tb4-dock/dell_wd22tb4_userguide/upgrading-your-wd19-docks?guid=guid-d152d7a1-4d6e-4579-8e22-c9a9dfa2f31e&lang=en-us)

Le code Dell/Realtek de référence est proposé sous licence MIT/LGPL.
