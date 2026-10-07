# WD22TB4 : recherche du contrôle thermique — 7 octobre 2026

## Résultat et limites

Les firmwares Dell EC **01.01.00.03 et 01.01.00.16** contiennent des handlers de lecture de profil,
de températures et de consigne du ventilateur. Leur entrée passe par les messages
propriétaires traités par les contrôleurs Power Delivery du dock.
La commande USB HID EC `0x03` utilisée par `dockctl info` ne fournit pas ces champs
sur le WD22TB4.

Le dock physique utilise **01.01.00.03**. Le firmware officiel de cette version
a également été téléchargé, sans installation ; ses handlers thermiques suivent
le même protocole interne. L'accès depuis macOS, les deux consignes non nulles,
l'accélération audible et mesurée, ainsi que le retour automatique puis le
ralentissement sont maintenant validés. Aucun flash n'a été effectué.
Le palier 1900 RPM n'a pas réduit le souffle. Un arrêt bref a été constaté
par l'utilisateur, suivi d'un retour automatique et d'une rotation confirmés.
Un essai thermique d'une minute est exécuté : maxima 34/36/64 °C, puis
restauration et rotation confirmées. Les sections suivantes retracent les étapes
de la recherche ; les résultats les plus récents sont ajoutés en fin de document.
Les noms Quiet/Optimized/Cool/Ultra Performance de Power Manager ne sont pas
encore associés aux octets reçus par le dock.

## Preuves et fichiers analysés

- [Témoignage Reddit](https://www.reddit.com/r/Dell/comments/y9rmaa/docking_station_wd22tb4_annoying_fan_noise/) :
  un utilisateur rapporte que passer d'Ultra Performance à Optimized dans Dell
  Power Manager a corrigé le bruit. C'est un témoignage, pas une capture du protocole.
- [Dell Power Manager 3.13](https://www.dell.com/support/home/en-us/drivers/driversdetails?driverid=pv7r1) :
  `research/windows/PowerManager-3.13.exe`, SHA-256
  `365ebe15827e9c44c3e925cf5065731d4e02a0fa8d3122447f06aafc3a96b04f`.
- [Dell Dock Firmware 01.01.13](https://www.dell.com/support/home/en-us/drivers/driversdetails?driverid=xvxn7) :
  `research/windows/DockFirmware-01.01.13.exe`, SHA-256
  `edf41e881f2f1008d1a9371b8341c0dad89e4619ce4c4a6e54d408e0a238c8ed`.
- Firmware EC extrait du CAB Dell/LVFS déjà téléchargé :
  `research/extracted/ec/ec.bin`, 131 008 octets, SHA-256
  `a3afe16d941816c4dfa8727206650cd1fa79881ce1555513e6a684d87f045736`.
  Le GUID de ses métadonnées correspond au dock physique :
  `cd357cf1-40b2-5d87-b8df-bb2dd82774aa`.
- Firmware EC **01.01.00.03**, extrait de `DellDockFirmwareUpdateLinux_01.00.09.cab` :
  `research/extracted/ec03/ec.bin`, SHA-256
  `3440ae6e00e92d9cf0ee4ef4585d41e089d7649db2acead74d801fcd69f408a1`.
  Le CAB téléchargé sur LVFS a pour SHA-256
  `03ff7f61284b6d3c6dcab2644f1cdf701c650ad4270b93590c5eb4f6d8571192`.

Les EXE Windows ont été extraits et inspectés statiquement sur macOS, sans
exécution. Les binaires propriétaires restent des fichiers locaux de recherche.

## Chemin Windows : profil du BIOS

Les DLL .NET de Power Manager contiennent le chemin réel suivant, hors simulation :

```text
ThermalManagementProvider.SetThermalMode
  -> SystemInterop.SetUsttInformation
  -> MakeBiosCall
  -> DAInterface.ExecuteDACommand
  -> WMI root/wmi, BDat / BFn, DoBFn
```

`SetUsttInformation` utilise la classe SMBIOS `0x11`, sélecteur `0x13`,
argument 1 = 1 pour modifier le profil, argument 2 contenant le mode thermique.
La lecture utilise argument 1 = 0. Les traces CIL sont conservées dans
`thermal-il.txt`, `ustt-il.txt` et `bios-il.txt`.

Ce chemin vise le BIOS du PC Dell. Il ne fournit pas une commande USB que l'on
pourrait simplement envoyer depuis macOS. Le transfert BIOS/EC du PC vers le
dock n'a pas été capturé ; le firmware du dock fournit cependant une entrée PD.

## Firmware : entrée par Power Delivery

Disassembly MIPS32 little endian : base de chargement **0x9d002000**.
Un offset fichier vaut donc `adresse virtuelle - 0x9d002000`.
Les tables de sauts internes confirment cette base.

```sh
/opt/homebrew/opt/binutils/bin/gobjdump -D -b binary -m mips:isa32 -EL \
  --adjust-vma=0x9d002000 --start-address=0x9d00c500 \
  --stop-address=0x9d01d000 research/extracted/ec/ec.bin \
  > research/ec-mips.txt
```

| Fonction | Adresse virtuelle | Interprétation issue du code |
|---|---|---|
| Événement PD | `0x9d00f334` | Lit `CMgd`, retire le préfixe de longueur, appelle le dispatcher |
| Dispatcher propriétaire | `0x9d011604` | Vérifie un état PD et sélectionne le handler par l'octet 1 |
| Réponse vers le PC | `0x9d00f5bc` | Transmet le payload par la commande PD `CMsd` |
| Lecture profil/vitesse | `0x9d01491c` | Renvoie cinq octets `01 81 0b profil classe_vitesse` |
| Consigne thermique | `0x9d014970` | Stocke le mode et une consigne discrète |
| Lecture températures | `0x9d0149f4` | Renvoie six octets `01 81 0c T_locale T_distante T_module` |
| Choix de la consigne | `0x9d01464c` | Courbes thermiques normales ou remplacement par consigne forcée |

Dans **01.01.00.03**, le dispatcher est à `0x9d010eec`, la lecture de profil
à `0x9d013778`, la modification à `0x9d0137cc`, les températures à `0x9d013850`
et l'envoi `CMsd` à `0x9d00f054`. La table de dispatch confirme les mêmes
commandes `0x0f`, `0x10`, `0x11`. Le choix de la consigne est à `0x9d0134a8`
(dont le test de mode à `0x9d013578`). Le désassemblage est conservé dans
`ec03-mips.txt`, produit avec la même commande et `-m mips:isa32r2`.
Les variables du mode et de la consigne sont déplacées de quatre octets en RAM
dans cette version ; les octets du payload sont identiques.

Le dispatcher exige le bit `0x08` du champ d'état du port, dérivé d'un registre
PD `0x5a`. La négociation qui établit cet état doit encore être reconstruite.

Le dispatcher des lectures EC via le pont HID-I2C a aussi été retrouvé dans
01.01.00.16 à `0x9d016698`. Sa table traite les commandes `0x02` à `0x0f` :
versions, identité, types et état de mise à jour ; aucune entrée n'appelle
les handlers thermiques identifiés. Cela confirme la séparation entre les deux
chemins dans cette image, plutôt qu'une simple omission de fwupd.

### Payloads internes identifiés

Ces octets sont les **payloads internes après traitement par le contrôleur PD**.
Ce ne sont ni des rapports HID prêts à envoyer, ni des VDM complets.

| Payload entrant | Handler |
|---|---|
| `02 0f ...` | Lire profil et classe de vitesse |
| `02 10 mode classe ...` | Modifier le mode et la consigne discrète |
| `02 11 ...` | Lire les trois températures |

La modification exige aussi que le message provienne du port upstream actif.
L'octet `mode` est stocké à `gp - 32628`. L'octet `classe` détermine la consigne
stockée à `gp - 32624` : 0 -> 0 tr/min, 1 -> 1 900 tr/min, 2 -> 3 600 tr/min.

**Correction essentielle : classe 0 ne signifie pas « automatique ».**
La fonction `0x9d01464c` remplace la consigne calculée par cette valeur uniquement
si `mode == 1`. Les autres valeurs de mode suivent les tables normales dans ce
chemin analysé. Aucune commande d'arrêt n'a été testée.

La classe de vitesse renvoyée par la lecture est calculée à partir de la vitesse
mesurée : 0 si RPM nul ou lecture `0xffff`, 1 si RPM < 2 750, sinon 2.
Elle ne suffit donc pas à connaître la consigne forcée exacte.

### Contrôleur du ventilateur

Le firmware initialise l'AMC6821 à l'adresse I2C 8 bits `0x30` (7 bits `0x18`).
Il lit la température locale en `0x0a`, la distante en `0x0b`, le tachymètre
en `0x08/0x09`, écrit le PWM en `0x22` et une cible tachymétrique en `0x1e/0x1f`.
La conversion utilisée est `RPM = 6 000 000 / tach`.

Ces accès passent par le bus interne de l'EC. L'échec du pont HID-I2C à lire
l'identifiant AMC6821 ne remet pas en cause leur existence.
Les courbes du module WD22 (type 8) commencent à 0 tr/min, puis proposent
1 900, 2 200, 2 900, 3 000 et 3 600 tr/min avec hystérésis.
Le firmware contient également un chemin de protection à haute température.

## Updater Windows : télémétrie d'une autre famille

L'EXE Dell récent contient les libellés `InternalTemperature` et `FanSpeed`.
La fonction native `0x14002d0f0` sélectionne toutefois ces champs uniquement
pour le **dock type 7**, avec une structure étendue de 192 octets.
Les types 4 à 6 suivent une structure de 103 octets sans ces deux champs.
Le WD22TB4 physique déclare le type de base 4, module 8.
Ces libellés ne démontrent donc pas une commande de télémétrie HID du WD22.

## Accès Power Delivery sur ce Mac

`research/hpm-probe.c` utilise AppleHPMLib via IOKit, avec l'en-tête du projet
[AsahiLinux/macvdmtool](https://github.com/AsahiLinux/macvdmtool).
Le user-client utilisé par la bibliothèque Apple est de type 42.
L'interface installée contient aussi les fonctions `sendVDM` et `receiveVDM`.
Leur vtable v3 et la signature de réception ont été recoupées dans la bibliothèque
native de ce Mac. Deux assertions de compilation contrôlent les offsets ABI.

L'utilisateur a validé `sudo ./research/hpm-probe` :

- Ouverture IOKit et interface AppleHPM réussies.
- Contrôleurs RID 0, 1, 5 sans connexion dans le registre lu.
- RID 2 connecté au WD22TB4, mode `APP`, registre `0x3f` = `3f 0f 00 ...`.
- Registre `0x4d` vide ; ce seul registre ne permet pas de conclure à l'absence
  de messages propriétaires.
- IORegistry identifie le partenaire Dell `413c:b070` et compte 56 mises à jour
  de messages SOP UVDM sur ce port au moment de l'inspection.

La capture passive étendue a aussi réussi : `ReceiveVDM` renvoie
`43 a0 01 ff 05 1c 00 00`, SOP=0, compteur=7, longueur=8.
C'est une réponse structurée DisplayPort Discover Modes (`ff01`), pas un payload
thermique Dell. Le registre `0x4f` contient la même réponse avec son préfixe.

Un premier test actif a transmis la requête structurée Dell Discover Modes
`03 a0 3c 41` par `sendVDM`. macOS a renvoyé succès, mais aucune réponse Dell
n'a été identifiée en une seconde. Ce succès signifie que l'appel Apple est
accepté, pas que le dock a reçu ou accepté la requête.
Le test suivant, avec contrôle DisplayPort, a confirmé les deux allers-retours :

```text
ff01 : 43 a0 01 ff 05 1c 00 00                      ACK
413c : 43 a0 3c 41 01 00 00 00 02 00 00 00         ACK
```

Le WD22TB4 annonce donc deux modes Dell, avec les VDO `0x00000001` et
`0x00000002`. **Ce ne sont pas deux profils thermiques.** Leur sémantique n'est
pas encore validée sur ce dock. Le test confirme cette fois une réponse physique
du partenaire, au-delà du simple succès de l'appel Apple.

### Enveloppe thermique expérimentale

L'overlay `[0]` de l'updater Windows contient aussi un firmware PD plus récent
(`TPS6699X M1.0 G0`, chaîne à l'offset `0x712211`). Son code Thumb construit
des VDM structurés Dell `413c`, commande `0x12`, et copie 24 octets de données :
constructeur à l'offset `0x70ceb9`, réception à `0x70cfe1`, copie à `0x70d051`.
La commande `0x13` apparaît également dans ce transport.
Ces offsets sont ceux de l'overlay extrait, pas des adresses CPU.

Cette piste vient d'une autre génération de contrôleur ; elle ne prouve pas
encore le format du WD22. L'ancien prototype `thermal-probe` a testé :

```text
12 a1 3c 41 02 0f 00 ... 00   # VDM mode position 1, lire profil
12 a1 3c 41 02 11 00 ... 00   # même enveloppe, lire températures
```

Chaque trame contient 28 octets (en-tête + 24 octets). Les opcodes internes de
lecture sont établis dans l'EC installé `01.01.00.03`. Le prototype vérifie
l'identité physique avant chaque envoi, journalise les nouveaux RX pendant
deux secondes et cherche les payloads `01 81 0b ...` / `01 81 0c ...`.
Il n'envoie ni `Enter Mode`, ni `Exit Mode`, ni l'opcode de réglage `0x10`.
Une absence de réponse ne permettra pas de départager enveloppe incorrecte,
mode non négocié ou filtrage par le contrôleur Apple.

Sur le dock physique, les deux requêtes ont reçu `52 a1 3c 41`, SOP=0,
longueur=4 : ACK de la commande structurée `0x12`, position de mode 1, sans
payload. Le transport reconnaît donc la requête ; aucune donnée thermique
n'a encore été obtenue. Le bit d'état requis par l'EC peut rester désactivé.

### Incident du test de session — 7 octobre 2026

Le test `thermal-session` a envoyé `04 a1 3c 41` (Enter Mode, position 1),
reçu `44 a1 3c 41` (ACK), puis obtenu les mêmes ACK vides aux deux lectures.
Après l'envoi de `05 a1 3c 41` (Exit Mode, position 1), la réception est passée
à compteur 0 / longueur 0, sans ACK de sortie. L'utilisateur a constaté une
extinction du dock et a dû appuyer sur son bouton pour rétablir les écrans.

Cet effet invalide l'hypothèse d'une session de gestion sans conséquence
sur le fonctionnement du dock. Le moment exact et le mécanisme de coupure
ne sont pas établis par ces logs ; ne pas attribuer définitivement l'incident
au seul Exit Mode. La signification des deux modes physiques reste inconnue.

Les anciens envois thermiques et les routines Enter/Exit ont été retirés du code.
Les deux anciens arguments échouent avant toute ouverture IOKit.
Les requêtes fixes Discover Modes ont été conservées. Un nouveau test de lecture
sans changement de mode a ensuite été ajouté ; voir « Capture pendant les
requêtes » ci-dessous. Un essai ultérieur `thermal-enter-hold` isole Enter
et ne réintroduit aucune commande Exit.

### Piste statique sur les changements d'état PD

Dans l'EC installé, `0x9d011464` compare les anciens et nouveaux états du port.
Un changement du bit `0x08` du champ `+20` appelle, via `0x9d0103a8`,
`0x9d00dbb4`, puis `0x9d00d978`. Cette dernière routine réexécute notamment
`0x9d00c97c`, `0x9d00ca64`, `0x9d00cb40` et `0x9d00d3d4` : accès GPIO,
calculs et reconfiguration des ports PD. Le même bit autorise le dispatcher
thermique : il n'est donc pas isolé dans un canal de télémétrie.

Cela justifie de reconstruire la négociation complète avant tout nouvel envoi,
mais ne prouve ni que notre Enter Mode a changé ce bit, ni quelle instruction
a causé l'extinction observée. Les adresses ci-dessus servent à poursuivre
l'analyse ; elles ne sont pas des commandes à envoyer au matériel.

### Réponse du réglage interne et protection

L'EC 01.01.00.03 répond aux setters via `0x9d010a50` : `01 80 résultat`,
avec résultat 1 pour succès et 4 pour erreur dans le handler thermique.
Le handler stocke le mode avant de vérifier la classe : une classe incorrecte
peut donc modifier un état malgré un ACK d'erreur. Une future interface devra
valider les deux champs avant tout envoi et relire le mode après modification.

Le choix forcé `mode == 1` remplace la courbe normale ; les trois températures
ne servent alors plus de plancher de vitesse dans `0x9d0134a8`. La protection
séparée `0x9d013600` surveille deux seuils à 100 °C et un à 105 °C et déclenche
une séquence de coupure après temporisation. Elle n'est pas une régulation de
confort remplaçant les courbes. Aucune consigne forcée n'a été exécutée.

La commande `nop` du macvdmtool original change quand même le mode du contrôleur
Apple ; elle n'a pas été exécutée. Le prototype local ne contient ni `LOCK`,
ni `DBMa`, ni reset, ni appel à `Write` ou `Command`. L'appel `sendVDM` est réservé
aux deux requêtes fixes Discover Modes et aux deux lectures expérimentales
du test `thermal-read`, ainsi qu'à l'entrée unique du nouveau test
`thermal-enter-hold`. Aucun chemin ne contient de commande de réglage du
ventilateur ni de sortie de mode.

Référence des registres et tâches TI :
[TPS65981/82/86 Host Interface, SLVUAN1A](https://www.ti.com/lit/ug/slvuan1a/slvuan1a.pdf).
Cette documentation sert de comparaison ; le contrôleur Apple possède des
extensions et certaines adresses diffèrent.

### Configuration PD correspondant à l'identité upstream

Le début de l'image EC 01.01.00.03 contient une configuration `CST1` (signature
à l'offset fichier `0x190`). Ses records `11 registre offset taille_moins_un`
permettent de lire notamment :

| Record (offset fichier) | Registre | Donnée |
|---|---|---|
| `0x1b4` | `0x06` | Version `12 00 00 00` |
| `0x2b0` | `0x47` | Identité Dell `413c:b070` |
| `0x337` | `0x73` | Identité fabricant / libellé WD19 |

La version `0x12` et l'identité sont cohérentes avec les lectures du dock
physique. Ce bloc cible donc davantage le port upstream que le bloc annonçant
`b050` et un seul mode Dell. Il ne contient pas à lui seul le code ROM qui
traite les commandes Dell ; cette correspondance ne valide aucun paquet thermique.

Un [projet TI public joint à une discussion sur un dock Dell K20A001](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1474212/tps65994ad-power-button-status-in-the-alert-message)
contient les définitions d'interruptions : arrivée d'un VDM CustomerD (bit 66)
et entrée dans un mode VDM (bit 67) sont deux événements distincts.
Le fichier PJT téléchargé est du code Python ; il a été lu statiquement, jamais
exécuté. Il correspond à une autre révision et ne remplace pas la preuve sur WD22.

### Lecture passive du tampon Attention

AppleHPMLib v3 expose `receiveVDMAttention` au slot `0x50`, distinct de
`receiveVDM` au slot `0x48`. Le désassemblage de la bibliothèque de ce Mac
confirme la signature et le sélecteur IOKit 5, utilisé uniquement pour recevoir.
Le prototype sans argument lit désormais aussi le registre `0x4e` et ce slot.
L'offset est vérifié à la compilation et l'appel a réussi sur le matériel
avec les droits administrateur. Cette branche n'appelle jamais `SendVDM`.

Cela permet de chercher une notification dans le second tampon sans refaire
les requêtes expérimentales. Les tampons contiennent des données récentes,
pas une archive de la session interrompue : une lecture vide ne pourra donc
pas expliquer rétrospectivement l'incident.

Validation physique de la lecture Attention : sur RID 2, registre `0x4e`
= 29 octets nuls ; `ReceiveVDMAttention` = succès, SOP 0, compteur 0,
longueur 0. Le tampon VDM ordinaire contient toujours la réponse DisplayPort
Discover Modes (`43 a0 01 ff 05 1c 00 00`, compteur 7).
Le dock reste identifié `413c:b070` et connecté (`0x3f = 3f 0f ...`).
Aucune notification Dell ni donnée thermique n'est disponible dans ces tampons
au moment de la lecture. Ce résultat ne valide pas l'enveloppe expérimentale.

### Vérification du pilote Apple ARM installé

Le kernelcache ARM de la partition Preboot a été lu puis décompressé avec
la bibliothèque native `libcompression`, sans modifier le système.
Le build correspond à `uname -v` : Darwin 24.6.0,
`xnu-11417.140.69.708.3~1/RELEASE_ARM64_T6000`.
SHA-256 de l'image décompressée :
`2451c2cbbbb056defe6dc3def8475fff16649d55dd503a8388aaee5efd6d3943`.
Les collections Boot/System présentes sous `/System/Library/KernelCollections`
étaient x86_64 ; elles ne servent pas à cette conclusion sur le Mac ARM.

Dans `com.apple.driver.AppleHPM`, les symboles et le code ARM confirment :

| Fonction | Adresse dans cette image | Comportement |
|---|---|---|
| `hpmIECSReceiveVDM` | `0xfffffe0009c2a0ec` | Lit `0x4f`, copie les octets suivant le préfixe |
| `hpmIECSReceiveVDMAttention` | `0xfffffe0009c2a23c` | Même traitement, registre `0x4e` |
| `hpmIECSSendVDM` | `0xfffffe0009c29f40` | Écrit le préfixe et le paquet dans `0x09`, puis exécute `VDMs` |

Les deux réceptions décodent longueur, SOP et compteur depuis le préfixe.
Elles ne filtrent ni le VID ni les commandes du paquet : l'absence de payload
Dell ne vient donc pas d'un filtrage dans ces deux routines. Cela ne permet
pas d'exclure un traitement antérieur dans le firmware du contrôleur Apple.

Le compteur `SOP UVDM Update Count` est incrémenté sur un événement dans
`AppleHPMInterface::processInterruptEvents` (`0xfffffe0009bf4020`).
Ce chemin ne décode pas de payload thermique Dell. Une valeur non nulle
de ce compteur ne prouve donc pas la réception d'une température ou d'un profil.
La lecture HAL voisine, à l'offset de vtable `0x958`, est `getCFVidStatus`
(`0xfffffe0009c2706c`, registre `0x5e`), pas un second tampon de données UVDM.

La prochaine étape utile est de reconstruire l'enveloppe et la négociation
du transport Dell correspondant au contrôleur PD de cette génération.
Répéter les lectures passives déjà vides ou réessayer Enter/Exit Mode 1
ne résout pas cette incertitude. Aucun nouvel envoi n'a été effectué pendant
cette analyse.

### Analyse supplémentaire des communications Windows

Les PE extraits du même updater ont été examinés, sans exécution :

- `pe/00edf52d.exe` est une DLL .NET `FwUpdateAPI.dll`. La méthode
  `SdkTbtBase.I2CRead` (RVA `0x4ee0`) transmet port, offset et longueur
  à `_driverIf.I2CRead` via une closure (RVA `0x724a`). La couche
  `SdkTbtController.I2CRead` (`0x5806`) vérifie le contrôleur Intel et son
  support de mise à jour. Ce n'est pas une API USB Dell de profils thermiques.
- `pe/01acacbd.exe` exporte `HID_i2cread` à `0x140009790`.
  Son paquet contient les mêmes champs `40 d6`, offset, longueur, adresse
  I2C et bit lecture `0x80` que notre pont. Les douze appels directs retrouvés
  visent l'adresse 8 bits `0x72`, avec préparation d'un accès à des registres
  32 bits. Aucun de ces appels ne fournit la négociation thermique recherchée.
- Les trois copies du plugin Realtek exportent `RS_Initialize`,
  `RS_GetVersionFromDevice`, `RS_GetVersionFromFwBin`, `RS_UpdateFW` et
  `RS_Finalize`. Les chaînes `CUSBDev::I2CReadImpl` / `I2CWriteImpl` identifient
  le transport interne ; elles ne démontrent pas un accès au bus de l'AMC6821.

Dans le firmware PD plus récent inclus dans l'updater, les helpers associés
à l'entrée (`0x70cf59`) et à la sortie (`0x70ce11`) modifient des états et
appellent le même helper avec des valeurs opposées pour le mode 1
(`0x70cfdb` / `0x70ce6f`). Cela renforce la nécessité de comprendre les effets
de bord ; ce code d'une autre génération ne prouve toujours pas la cause de
la coupure du WD22. Aucun ajustement de délai ne suffit à garantir une sortie
sans déconnexion sur la base de ces preuves.

### Capture pendant les requêtes

Le test précédent surveillait seulement `ReceiveVDM` pendant les requêtes.
La lecture Attention effectuée après la session interrompue ne permettait
pas d'exclure une réponse Attention pendant cette session.

`hpm-probe thermal-read` reprend uniquement les deux requêtes expérimentales
`12 a1 3c 41 02 0f ...` et `12 a1 3c 41 02 11 ...`, sans entrée/sortie de mode.
Il prend une référence des deux tampons avant chaque envoi, puis les relit
immédiatement et après des pauses de 10 ms pendant trois secondes. Il affiche
les changements de contenu, longueur, SOP ou compteur, y compris son bouclage.
Il revérifie connexion et identité toutes les 250 ms environ et arrête les
envois dès qu'une erreur est détectée. Ce polling peut encore manquer plusieurs
paquets qui se succèdent dans un même tampon entre deux lectures.

Seules des trames Dell SOP 0 structurées, commande `6` ou `0x12`, initiateur
ou ACK, contenant à l'offset 4 `01 81 0b ...` ou `01 81 0c ...`, sont signalées
comme signatures thermiques candidates. Un ACK vide n'est jamais compté comme
une lecture réussie. Tous les autres nouveaux messages restent affichés bruts.
Ce filtre est une hypothèse d'analyse et ne valide pas l'enveloppe.

Validation : compilation stricte et `make check` réussis, avec cas ACK vide,
signature candidate, mauvais SOP, trame tronquée, changement de contenu et
bouclage du compteur. L'essai matériel depuis l'agent a été refusé par sudo
avant lancement (`a password is required`). L'utilisateur a ensuite exécuté
le test avec le résultat suivant :

```text
0f : +35 ms, SOP 1, compteur 0, 52 a1 3c 41
11 : +32 ms, SOP 0, compteur 1, 52 a1 3c 41
Attention : vide avant et aucun changement observé pendant les deux captures
```

Aucun payload thermique et aucune perte de connexion PD n'ont été détectés.
L'état des écrans n'est pas indiqué dans ce log. Le premier ACK contient la
métadonnée SOP 1 : ne pas l'attribuer sans réserve au partenaire SOP 0.
La signature de la bibliothèque Apple a été revérifiée : pointeurs de sortie
SOP 32 bits, compteur 8 bits, longueur 64 bits ; pas d'inversion trouvée.
L'origine de cette métadonnée reste inconnue.

### Correction du format du firmware PD extrait

L'overlay Windows contient à `0x6eb0ed` une suite de blocs TI avec un en-tête
de huit octets : quatre entiers 16 bits little endian, numéro séquentiel,
longueur, `0x00ff`, `0x0077`. Il y a neuf blocs de `0x4000` octets puis un
dernier de `0x3800`. Les anciennes vues Thumb faites directement sur l'overlay
laissaient ces en-têtes au milieu du code. Leurs destinations d'appels
franchissant un bloc étaient donc incorrectes.

`python3 research/unpack-ti-pd.py` reconstitue l'image contiguë de 161 792 octets
dans `extracted/dock/tps6699-rom.bin`. Son SHA-256 est
`1284c718716067b2eaa674530c3efe6d263ea74b5af56b59c7c1ac620f0d4ed7`.
`python3 research/unpack-ti-pd.py --self-test` vérifie les longueurs, numéros
et données tronquées. Le vecteur initial `20006000 / 000000dd` est contrôlé.

Dans cette image reconstruite, les adresses ROM utiles sont :

| Fonction | Adresse ROM |
|---|---|
| Table des tâches ASCII | `0x266d0` |
| Conversion nom de tâche vers identifiant | `0x56e0` |
| Dispatcher des tâches | `0x5702` |
| Traitement CMsd / CMgd | `0x52a2` |
| Construction VDM Dell | `0x21d84` |
| Traitement Dell précédemment à l'offset `0x70cfe1` | `0x21eac` |
| Copie mémoire appelée par ces routines | `0x262e8` |

Les entrées ASCII CMsd/CMgd ont les indices 29/30, convertis en `0x11d`/`0x11e`.
Le dispatcher aboutit à `0x52a2`. CMsd copie 24 octets dans le buffer TX
`contexte + 0x9c`, puis demande l'opération `0x17`. CMgd récupère 24 octets
depuis `contexte + 0xb4` et efface le drapeau de réception. Les deux vérifient
le bit 1 de `contexte + 0xcd` avant ces opérations. Cette analyse porte sur
le TPS6699 plus récent, pas sur la ROM exacte du contrôleur PD du WD22TB4.

### Essai préparé : entrée sans sortie automatique

`thermal-enter-hold` vérifie le partenaire, capture les références des deux
canaux, puis envoie une seule trame `04 a1 3c 41`. Il exige un ACK de quatre
octets correspondant au SVID Dell, mode position 1, commande 4, SOP 0. Un
NAK, BUSY, dépassement d'une seconde sans ACK ou échec de connexion arrête
le test sans autre envoi. Après trois secondes depuis l'envoi, les lectures
`0f` et `11` utilisent la capture double canal déjà validée.

Il n'envoie **jamais Exit**, même en cas d'échec ou d'interruption : le mode
peut donc rester engagé. Ce choix isole Enter et les lectures du chemin de
sortie impliqué dans l'incident ; il ne garantit pas l'absence de coupure par
Enter lui-même. L'outil ne vérifie pas l'état des écrans. Les anciens arguments
`thermal-session` et `thermal-probe` restent désactivés.

Compilation et self-tests réussis. L'utilisateur a ensuite exécuté l'essai :

```text
Enter Mode 1 : send=0
+32 ms RX VDM SOP=0 count=2 len=4 : 84 a1 3c 41
```

L'en-tête little endian `0x413ca184` contient SVID Dell `0x413c`, position 1,
commande 4 et type de réponse 2 : **NAK**, entrée refusée. Les définitions
de champs sont vérifiables dans
[pd_vdo.h de Linux](https://github.com/torvalds/linux/blob/master/include/linux/usb/pd_vdo.h).
Le programme s'est arrêté avant les lectures thermiques, sans Exit. Ce log
ne donne ni la cause du refus, ni l'état antérieur du mode, ni l'état des écrans.
Il ne permet pas de conclure que le mode était déjà engagé. L'affichage distingue
désormais NAK et BUSY du timeout ou d'une connexion perdue ; la trame observée,
un BUSY, une troncature et une mauvaise position sont couverts par `make check`.
Un second essai a reçu le même NAK à +30 ms, SOP 0, compteur 3. L'utilisateur
confirme qu'aucun écran ne s'est coupé pendant cet essai. Cela valide seulement
l'absence de coupure lors de cette entrée refusée, pas lors d'une entrée acceptée.

### Contrôles précédant le gestionnaire Dell dans la ROM TPS6699

Analyse statique supplémentaire de l'image reconstruite, sans envoi matériel :

- `0xcaf2` décode l'en-tête reçu et vérifie SVID, position et contexte avant
  de sélectionner un état. À `0xcbcc..0xcbde`, les commandes Enter/Exit sont
  écartées lorsque le champ `contexte PD + 0x4a` vaut 1, ou lorsque la recherche
  du mode renvoie l'indice sentinelle 8. Le champ est identifié ci-dessous comme
  le rôle de données USB-PD.
- Pour Enter, `0xcc26` sélectionne l'état `0x50`. Le chemin `0x15f88` consulte
  l'octet `entrée de mode + 5`, une option de configuration et le helper
  `0xfe96`, puis sélectionne les états `0x51` ou `0x52`. Le helper vérifie
  notamment `entrée de mode + 4` et d'autres conditions de contexte.
- `0xcc6c` appelle indirectement le callback du SVID, pris dans une table en RAM
  (`0x200004c0 + port*0x50 + indexSVID*0x10`, offset `0x0c`). Pour les commandes
  `5`, `6`, `0x10`, `0x11`, `0x12`, `0x13`, cet appel est conditionné par
  l'octet non nul `entrée de mode + 5`. L'initialisation du pointeur Dell est
  désormais retrouvée dans les données compressées décrites ci-dessous.

Ces contrôles donnent des pistes de négociation, pas la cause démontrée du NAK
observé sur le WD22TB4. Aucun changement d'enveloppe, essai du mode 2 ou nouvel
Enter/Exit n'est ajouté sur cette seule base.

### Table de callbacks compressée et rôle de données

La routine de démarrage `0x26538` parcourt quatre descripteurs de 16 octets,
de `0x27050` à `0x27090`. Le premier est :

```text
source ROM   destination RAM   taille   routine
0x27090      0x200004c0        0x1d0   0x2659e
```

La routine `0x2659e` décompresse des séquences littérales, des zéros et des
copies depuis les octets précédents, avec chevauchement possible. Le pointeur
Dell est donc absent sous forme d'un entier 32 bits contigu dans l'image ROM.
`unpack-ti-pd.py` reproduit maintenant cette décompression et écrit les 464 octets
dans `extracted/dock/tps6699-initial-ram.bin`. Ses self-tests couvrent les trois
opérations, les comptes étendus, la troncature et les références invalides.
L'extraction réelle retrouve, pour les deux ports, l'entrée :

```text
3c 41 00 00 02 02 00 00 00 00 00 00 81 1f 02 00
SVID 413c ; deux modes ; premier indice interne 2 ; callback Thumb 0x21f81
```

Le chemin `0xcc6c -> callback 0x21f80 -> 0x21eac` est ainsi relié à la table
initialisée, et plus seulement supposé d'après les instructions du gestionnaire.
Les indices internes 2/3 sont distincts des positions de mode transmises sur
le câble et des classes thermiques de l'EC.

Le constructeur de paquets PD `0x1a7de`, à `0x1a8d4..0x1a8ea`, copie le bit 0
du champ `0x20000ff0 + port*0x274 + 0x4a` vers le bit 5 de l'en-tête PD SOP.
Ce bit représente le rôle de données : 1 = DFP/hôte, 0 = UFP/périphérique
([définition Linux](https://github.com/torvalds/linux/blob/master/include/linux/usb/pd.h)).
Dans le gestionnaire Dell de cette ROM, `0x12` livre les 24 octets reçus au buffer
CMgd côté UFP ; Attention (`6`) le fait côté DFP. Dans l'autre sens, le même
gestionnaire prépare l'émission depuis le buffer CMsd. La direction prévue de
notre enveloppe `0x12` est donc cohérente avec un Mac hôte et un dock périphérique
pour cette génération TI. Cela ne confirme ni l'état de négociation du WD22,
ni la compatibilité exacte de son ancien firmware PD avec cette ROM.

Validation : self-tests Python et extraction réelle réussis. Aucun paquet envoyé
au dock pendant cette analyse ; aucune commande de contrôle supplémentaire ajoutée.

### Diagnostic EC observable par HID et découverte nécessaire avant Enter

Le bit qui conditionne les commandes thermiques est accessible par la lecture
HID EC `0x03` existante, sans ajouter de commande matérielle :

| EC | Construction de la structure identité | Accès à l'état du port |
|---|---|---|
| 01.01.00.03 | `0x9d013078` | `0x9d00fbac` |
| 01.01.00.16 | `0x9d012e38` | `0x9d010114` |

La construction appelle l'accesseur pour les ports internes 0 et 1 et stocke
les résultats little endian aux offsets 8 et 10 de la structure de 103 octets.
L'accesseur retourne `contexte + 20` si le port est activé et connecté, zéro
sinon. Dans EC03, c'est exactement le champ dont `0x9d010eec` exige le bit
`0x0008` avant tout dispatch des commandes propriétaires. Le chemin de réponse
`0x9d00f054` exige aussi ce bit.

La lecture physique par l'agent donne `0x0060 / 0x0000` : le prérequis est absent
sur les deux ports exposés. Ce n'est pas une estimation à partir d'un ACK PD.
`dockctl info` affiche désormais ce diagnostic uniquement pour les deux versions
EC analysées. Un bit présent ne suffirait pas à garantir le transport complet.

Dans la ROM TPS6699, l'ACK vide a un chemin indépendant de la livraison à l'EC :
`0x23a58` route la commande `0x12` vers l'état `0x5a`, traité à `0x16394`.
À `0x163bc`, le callback est invoqué via `0xcc6c`, mais son retour n'est pas
contrôlé avant le passage à l'état `0x5b`. À `0x163d8`, cet état émet un ACK
de la commande `0x12`. Le garde `entrée de mode + 5` dans `0xcc6c` peut donc
empêcher la copie du payload sans empêcher cet ACK. C'est une explication
possible des ACK vides, pas une observation directe du firmware PD du WD22.

Un prérequis manquait aussi à `thermal-enter-hold` : dans `0xbbf0`, le traitement
Discover Modes écrit les positions annoncées (1, 2, ...) à `entrée de mode + 0x0e`
(`0xbd00`). L'accesseur `0xca14` relit ce champ côté UFP, et `0xca3c` s'en sert
pour convertir la position reçue en indice interne. Sans correspondance, il
retourne 8 ; `0xcbd2..0xcbde` refuse alors Enter/Exit. La routine de remise à
zéro `0xf868` efface notamment ces positions (`0xf8b8..0xf8ce`).

L'ancien `thermal-session` exécutait Discover avant Enter (ACK), alors que les
deux `thermal-enter-hold` ayant reçu NAK ne le faisaient pas. Cette différence
et le mécanisme de la ROM donnent une hypothèse testable pour les NAK, sans
prouver que l'ancien PD du WD22 suit exactement la même implémentation.

La version corrigée exécute donc Discover Dell, exige un ACK avec au moins un
VDO et la valeur `1` en première position, puis seulement l'Enter déjà prévu.
Une découverte refusée, vide, inattendue ou expirée arrête la séquence avant
Enter. Aucun Exit, reset ou setter thermique n'est ajouté. Le scénario reste
expérimental : un Enter accepté peut encore avoir des effets sur le dock.

Validation : `make check` et `./dockctl info` réussis. Tests ajoutés pour les
versions EC reconnues, les bits de diagnostic, Discover vide/NAK, mauvais SOP
et premier VDO inattendu. L'agent n'a pas exécuté la séquence active corrigée ;
l'essai utilisateur doit être suivi de `./dockctl info` pour observer l'état EC.

La recherche d'un firmware TI plus ancien via TPS6598X-CONFIG aboutit au
[téléchargement officiel](https://www.ti.com/tool/TPS6598X-CONFIG), qui redirige
vers une connexion TI et indique une approbation export. Aucun téléchargement
authentifié ni contournement n'a été effectué ; l'analyse reste fondée sur
les images Dell déjà disponibles.

### Lecture thermique réussie et acquittement à tester

L'essai utilisateur Discover Dell → Enter Mode 1 a reçu `44 a1 3c 41`, puis les
lectures `0f` et `11` ont livré leurs données dans le canal Attention, sur SOP 0.
`dockctl info` a ensuite retourné `0x0068 / 0x0000` : le bit EC nécessaire est
désormais présent sur le port 0. Le maintien des écrans reste à confirmer par
l'utilisateur ; le maintien du lien PD ne le prouve pas.

| Payload EC (après l'en-tête Attention `06 a1 3c 41`) | Interprétation EC03 |
|---|---|
| `01 81 0b 00 01` | mode 0 automatique, classe 1 : vitesse mesurée strictement entre 0 et 2750 tr/min |
| `01 81 0c 22 29 40` | locale 34 °C, distante 41 °C, module 64 °C |

Le profil est construit à `0x9d013778`, sa classe de vitesse à `0x9d013738`.
La classe 1 ne signifie donc pas une mesure exacte de 1900 tr/min. La réponse
température à `0x9d013850` contient trois octets interprétés comme signés dans
les calculs de l'EC. Les deux premiers proviennent des registres AMC6821
`0x0a` et `0x0b` (résolution 1 °C), le troisième de la lecture du module.
Les octets au-delà du payload sont ignorés : après la température, les réponses
de profil gardent un `0x40` résiduel qui ne fait pas partie de leurs cinq octets.

La réponse de profil se répète environ toutes les 125 ms. Après `11`, une réponse
température apparaît, puis le profil se répète à nouveau. Cela suggère un
acquittement de transport manquant, sans démontrer encore sa cause.

Dans la ROM TPS6699 plus récente, le traitement CMgd à `0x52a2` déclenche côté
DFP l'état `0x73`, puis le callback Dell construit la commande Message Received
`0x13` sans payload. Côté UFP, sa réception via `0x23a58` arrête le timer 24
et termine l'opération en cours. L'envoi Attention Dell à `0x16304` arme ce
timer pour `0x78` (120), avec callback `0x21fe6`. Cette proximité avec la cadence
observée motive le test, sans établir les unités du timer ni l'identité des ROM.

`thermal-read-ack` reprend les lectures existantes et envoie `13 a1 3c 41`
une seule fois par lecture, après la première réponse correspondante reconnue
dans le canal Attention. L'enveloppe doit être exactement `06 a1 3c 41`,
SOP 0, longueur 28, avec la signature EC attendue. Aucun acquittement sur le
contenu initial du tampon, aucun Enter/Exit, aucune consigne de ventilateur.
Une notification répétée peut néanmoins être ancienne : les compteurs HPM ne
sont pas des identifiants de transaction EC. Les lectures sans acquittement
restent disponibles séparément.

Validation : compilation et `make check` réussis, avec les payloads observés,
le décodage 34/41/64, le rejet des mauvais SOP/opcodes/longueurs et le contrôle
des quatre octets d'acquittement. Le test `thermal-read-ack` n'a pas encore été
exécuté sur le dock. Aucun réglage de ventilateur n'a été envoyé.

### Résultat de l'acquittement unique et essai borné des répétitions

L'utilisateur a exécuté `thermal-read-ack` : mode 0, classe 1, températures
34/35/64 °C. Les deux envois `0x13` réussissent côté API, mais les notifications
continuent. Après chaque accusé, la prochaine notification arrive environ 42 ms
plus tard, puis la cadence revient vers 125 ms. Cela ne prouve ni que l'accusé
est ignoré, ni qu'il est accepté par le dock.

Dans l'EC03 exact, `0x9d00f054` envoie CMsd et examine son résultat. En cas
d'échec ou de statut non nul, une réponse normale est copiée dans `0xa00003e8`
si le flag `gp-32595` n'est pas déjà posé. Une réponse plus récente ne remplace
donc pas nécessairement cette réponse en attente. La boucle à `0x9d017fd8`
efface ce flag, appelle `0x9d00f1ac` pour réémettre, et le chemin d'erreur peut
le poser de nouveau. Cela fournit une explication possible au retour du profil
après la réponse température. Il faut tester les acquittements des réémissions
avant de conclure que la trame `0x13` est mauvaise.

`thermal-read-ack-loop` garde les deux lectures et leurs fenêtres de trois
secondes. Il acquitte chaque changement Attention reconnu, profil ou température,
y compris un profil pendant la lecture température, avec un maximum de 16
accusés par phase (32 au total). Le tampon initial n'est pas acquitté ; le
filtrage SOP/en-tête/longueur/signature reste identique. Après le plafond,
l'observation continue sans autre accusé. Aucun Enter, Exit ou setter ajouté.
`make check` passe, notamment les limites 0/1/16 et la réponse différée d'un
autre type. L'essai physique reste à faire.

### Courbes EC03 du module de type 8 et souffle à 64 °C

Le choix de consigne à `0x9d0134a8` prend le maximum des trois courbes ci-dessous.
Les entrées de huit octets contiennent un seuil bas signé, un seuil haut signé,
deux octets de padding, puis la consigne RPM little endian. Les adresses sont
`0x9d0190f0`, `0x9d019120`, `0x9d019150` (l'addition MIPS utilise un immédiat signé).

| Palier RPM | Locale bas/haut °C | Distante bas/haut °C | Module bas/haut °C |
|---|---|---|---|
| 0 | -128/45 | -128/58 | -128/66 |
| 1900 | 40/62 | 50/70 | 50/73 |
| 2200 | 56/76 | 64/77 | 65/83 |
| 2900 | 70/81 | 72/87 | 70/88 |
| 3000 | 75/86 | 77/91 | 75/92 |
| 3600 | 80/127 | 82/127 | 80/127 |

La fonction `0x9d013260` monte d'un palier si T ≥ seuil haut et descend si
T ≤ seuil bas. À 64 °C, le module peut donc rester au palier 1900 après avoir
atteint 66 °C : il doit revenir à 50 °C pour repasser de ce palier à zéro.
C'est compatible avec le souffle actuel, mais les indices de courbe internes
et la vitesse exacte ne sont pas exposés par nos lectures. Forcer 1900 tr/min
ne garantit donc aucune réduction du bruit. Aucun forçage n'a été ajouté.

### Acquittements répétés validés et première écriture automatique préparée

L'essai utilisateur `thermal-read-ack-loop` reçoit le profil à +48 ms,
l'acquitte, reçoit une répétition à +93 ms et l'acquitte aussi. Aucun autre
changement Attention n'est observé pendant le reste des trois secondes.
La lecture température reçoit 33/35/64 °C à +50 ms, un accusé suffit, sans
répétition observée. Ce résultat valide le traitement des réponses réémises
sur ce dock, dans ces fenêtres. Les lectures usuelles emploient désormais
ce même mécanisme ; le plafond reste de 16 accusés par requête.

Le chemin d'écriture EC03 a été revérifié à `0x9d0137cc` : il exige le port
upstream actif, stocke le mode de l'octet 2, puis la classe de l'octet 3.
La classe 1 vaut 1900 RPM, mais cette consigne n'est utilisée à la place des
courbes que pour le mode 1. Le mode 0 garde les courbes. La réponse via
`0x9d010a50` est `01 80 01` pour succès, `01 80 04` pour refus.

`thermal-auto` utilise le payload fixe `02 10 00 01`, précédé de l'en-tête
`12 a1 3c 41`, complété à 28 octets. Aucun mode ou classe arbitraire n'est
accepté en CLI. Le test exige d'abord les deux lectures thermiques avec
acquittements, envoie le setter une fois, capture/acquitte sa réponse puis
relit `0f`. Il exige le succès EC et le mode relu 0 pour annoncer la réussite.
Un ACK VDM vide ne suffit pas. La réponse EC ne contient pas d'identifiant de
commande : ce prototype suppose l'absence d'un autre client de contrôle actif.
Le mode étant déjà 0, cet essai ne prouvera pas une transition de mode ou un
changement physique de vitesse ; il vérifiera l'acceptation du chemin d'écriture.

L'échange commun reste borné à trois secondes et vérifie la connexion ; il
n'envoie aucun Exit ou reset. La commande automatique est compilée et ses
payloads/décodages sont testés par `make check`. L'exécution matérielle reste
à faire. Aucun contrôle forcé de vitesse ni arrêt du ventilateur n'est ajouté.

### Écriture automatique validée ; essai temporaire de mode 1

L'utilisateur a exécuté `thermal-auto` avec succès. Le setter reçoit son ACK
VDM à +32 ms, puis une Attention `06 a1 3c 41 01 80 01 01 ...` à +50 ms.
Seuls les trois octets `01 80 01` appartiennent au statut EC. Après acquittement,
la relecture `0f` retourne `01 81 0b 00 01` : mode automatique confirmé.
Températures préalables : 33/35/64 °C. Ceci valide l'acceptation de l'écriture,
sans encore démontrer de changement de mode ou de vitesse.

`thermal-low-test` prépare cette transition avec le payload fixe `02 10 01 01`
(mode forcé, classe 1 = consigne 1900 RPM). Il exige initialement un profil
automatique et une classe mesurée 0 ou 1, ainsi que les trois températures
dans [0,62[, [0,70[, [0,73[. Ces bornes hautes sont les seuils des courbes EC03
de type 8 où la consigne normale monte de 1900 à 2200 RPM ; elles ne constituent
pas une garantie thermique générale. Les indices d'hystérésis restent inconnus.

Après l'écriture, succès EC et mode 1 doivent être relus. Des lectures de
température suivent jusqu'à environ 12 secondes depuis le début du setter.
Le dépassement d'un seuil, une erreur ou une demande d'interruption arrête
l'essai ; le même setter en mode 0 est toujours tenté après toute tentative
d'écriture forcée, puis son succès et son mode relu sont vérifiés. Le temps
n'est pas une échéance matérielle : les appels I/O peuvent retarder le retour.

SIGINT, SIGTERM et SIGHUP posent un drapeau ; aucun I/O n'est fait dans le
gestionnaire de signal. SIGPIPE est ignoré pendant cet essai. L'échange courant
se termine avant la restauration ; SIGKILL, un crash ou une perte de liaison
ne permettent pas de garantir celle-ci. Un échec affiche explicitement le
retour non confirmé et la commande `thermal-auto`. Cette dernière envoie
désormais la restauration sans exiger de télémétrie préalable.

La construction des trames rejette tout mode hors 0/1, tout mode non nul pour
une lecture et toute commande hors 0f/10/11 ; la classe du setter reste fixée
à 1. Aucun arrêt, mode forcé permanent ou vitesse arbitraire n'est exposé.
`make check` passe, avec vérification de la trame automatique, de la trame
1900 RPM, des valeurs rejetées et des limites de température sur chaque capteur.
L'essai forcé n'a pas encore été exécuté sur le dock.

### Transition 0 → 1 → 0 validée, sans différence audible notable

L'utilisateur a exécuté `thermal-low-test`. Le setter 1900 reçoit `01 80 01`,
la lecture suivante retourne `01 81 0b 01 01` : mode 1 et classe mesurée 1.
Deux lectures température retournent 33/35/64 °C. Le retour automatique reçoit
`01 80 01`, puis `01 81 0b 00 01`. Aucune différence audible notable n'a été
rapportée. La transition et la restauration du mode sont donc validées sur
le matériel ; une variation physique de vitesse n'est pas démontrée.

Le dispatch EC03 à `0x9d010eec` route bien `0x10` vers `0x9d0137cc`. Ce handler
lit uniquement mode/classe et ne consomme aucun RPM fourni après ces octets :
classe 0 → zéro, classe 1 → 1900, classe 2 → 3600, autres classes → erreur.
Cela exclut un réglage arbitraire via cette commande, sans exclure l'existence
d'autres chemins non identifiés. Le mode est écrit avant cette validation :
essayer une classe inconnue n'est pas une simple lecture sans effet.

`thermal-high-test` réutilise l'essai borné avec le payload `02 10 01 02`.
Les valeurs internes automatique/basse/haute sont traduites en mode EC 0/1
et classe 1/2 ; la valeur interne haute ne devient jamais un mode EC 2.
Le test conserve les préconditions et limites conservatrices de l'essai bas,
lit aussi le profil pendant la surveillance et cherche mode 1 + classe 2.
La classe 2 établit une mesure d'au moins 2750 RPM, pas exactement 3600 RPM.
Le retour automatique est tenté et vérifié même si la hausse n'a pas été
observée ; cette absence est signalée avec un code de sortie non nul.

La trame haute complète et le rejet des réglages hors liste ont été ajoutés
au self-test. `make check` passe. Aucun essai à 3600 RPM n'a encore été exécuté
sur le dock et aucun arrêt du ventilateur n'est exposé par le programme.

### Contrôle physique confirmé par l'essai 3600 RPM

L'utilisateur rapporte une accélération audible pendant `thermal-high-test`.
Le setter reçoit `01 80 01`. La première lecture de profil est encore
`01 81 0b 01 01`, puis une lecture ultérieure retourne `01 81 0b 01 02` :
mode forcé et classe mesurée au moins 2750 RPM. La température lue reste
33/35/64 °C. Le changement de bruit et le changement de classe corroborent
l'effet physique du contrôle depuis macOS.

Après restauration, succès EC et profil `01 81 0b 00 02` sont reçus : mode
automatique effectif, vitesse encore en classe 2 au moment de la mesure.
Il ne faut pas confondre confirmation de mode et stabilisation de vitesse.
Une lecture ultérieure est nécessaire pour établir le ralentissement.

Le code EC03 `0x9d016bc8` convertit la consigne en cible tachymétrique et écrit
les registres AMC6821 `0x1e/0x1f`. La mesure classifiée à `0x9d013738` provient
de la fonction tachymètre `0x9d016ac4`, indépendamment du mode enregistré.
La [documentation TI AMC6821, section Software-RPM Control, p. 25–26](https://www.ti.com/lit/ds/symlink/amc6821.pdf)
décrit des ajustements périodiques bornés du PWM vers la cible. Un délai entre
consigne et mesure est donc compatible avec cette architecture ; cela ne
prouve pas à lui seul la cause du dernier relevé ni le temps de stabilisation
sur ce dock. Aucun délai exact n'est déduit de la trace.

État acquis : lectures des températures et de la classe de vitesse, passage
forcé, effet physique et restauration du mode automatique. Objectif acoustique
non résolu : le palier 1900 n'a pas réduit le bruit et aucune commande de
consigne intermédiaire sous 1900 n'est établie dans le chemin identifié.

### Ralentissement après retour automatique confirmé

La lecture utilisateur suivante part d'un tampon initial `01 81 0b 00 02`,
mais reçoit après sa requête `0f` une nouvelle Attention `01 81 0b 00 01`
à +49 ms (compteur 4 → 5). C'est bien une nouvelle mesure : mode 0 et classe 1.
La réponse température suivante est 33/35/64 °C. Le retour en dessous de
2750 RPM après restauration automatique est donc confirmé, sans pouvoir en
déduire la vitesse exacte ni le délai de stabilisation entre les deux essais.
Le cycle complet commande haute → accélération mesurée et audible → mode
automatique → ralentissement mesuré est validé. Aucun nouvel essai matériel
ni changement de commande n'a été effectué pour consigner ce résultat.

### Recherche d'un palier inférieur et essai acoustique bref

La recherche des instructions MIPS de stockage via `gp` dans l'image EC03
retrouve une seule écriture directe du mode (`gp+0x8088`, à `0x9d0137fc`) et
deux écritures directes de la consigne forcée (`gp+0x808c`, à `0x9d013810` et
`0x9d013828`), toutes dans le handler connu. Les six écritures directes de la
consigne finale `gp+0x80f0` sont dans la sélection des courbes et de l'override.
Les appels directs de `0x9d016bc8` viennent de `0x9d01322c` (séquence de démarrage)
et `0x9d0135a0` (régulation) ; ceux de `0x9d016a90` et `0x9d016b2c` sont également
internes à cette gestion. Cela ne couvre pas toutes les écritures indirectes,
mais ne révèle aucun setter supplémentaire de RPM arbitraire.

Le dispatcher accepte aussi un transport de tâches PD via `0x9d010bb0` : il
sélectionne un contexte PD, construit une commande 4CC et appelle `0x9d00e79c`.
Ce n'est pas une preuve d'accès au bus de l'AMC6821. La commande propriétaire
`0x1e`, à `0x9d010a84`, lit le registre PD fixe `0x5f` ; elle n'expose pas de
registre I²C arbitraire. Aucun de ces chemins n'a été sondé sur le matériel.

Faute de réglage fin établi, `thermal-stop-test` expose seulement un essai
acoustique court : payload fixe `02 10 01 00`, consigne zéro, objectif neuf
secondes depuis l'envoi, puis restauration automatique. Il réutilise le même
chemin d'écriture, les ACK bornés et les gestionnaires de signaux que les tests
déjà validés. Les limites spécifiques sont locale <40, distante <45, module
<66 °C, avec rejet des températures négatives. Ce sont des limites d'essai
choisies pour les relevés actuels, pas des seuils validant un usage fanless.
Le test n'autorise pas un mode d'arrêt permanent ou un cycle automatique répétitif.

Après restauration, jusqu'à trois lectures `0f` cherchent mode 0 et classe
mesurée 1 ou 2 pour confirmer la rotation. Une classe 0 ne prouve pas à elle
seule un arrêt, car elle inclut aussi le tachymètre indisponible. Toute absence
de confirmation est affichée et entraîne un code de sortie non nul. Les
limites habituelles de restauration restent applicables : perte de liaison,
crash ou SIGKILL peuvent empêcher le retour automatique.

`make check` valide la trame stop complète, la distinction stop/automatique,
le refus des réglages hors liste, les limites sur les trois capteurs et la
reconnaissance de rotation après restauration. Aucun arrêt physique n'a encore
été exécuté et aucune commande matérielle n'a été envoyée durant cette analyse.

### Arrêt bref et reprise validés ; caractérisation thermique préparée

L'utilisateur constate l'arrêt lors de `thermal-stop-test`. Le setter est
accepté (`01 80 01`). La relecture précoce reste `01 81 0b 01 01`, puis la
température est encore 33/35/64 °C. Après retour automatique, deux lectures
successives donnent `01 81 0b 00 01` : rotation et mode automatique confirmés.
L'arrêt lui-même repose sur l'observation utilisateur, pas sur une classe 0
capturée ; neuf secondes sans hausse mesurée ne prouvent pas un équilibre
thermique sans ventilation.

`thermal-silence-test` reprend le même code avec une durée cible de 60 secondes,
sans changement de seuil, de payload, de procédure de restauration ni de
vérification de rotation. Il imprime les températures initiales, les points
successifs et les maxima observés. Le début du chronométrage précède l'envoi
du setter ; les points sont horodatés à la fin de leur fenêtre de lecture.
L'échéance et les seuils restent contrôlés par le processus entre les échanges,
donc dépendants de leurs latences. Aucun watchdog autonome du dock n'est établi.

Les durées permises sont 9 ou 60 secondes pour l'arrêt et 12 pour les autres
essais. Aucun argument de durée libre ni répétition automatique n'est ajouté.
Les self-tests couvrent ces bornes en plus des trames et seuils existants ;
`make check` passe. L'essai d'une minute n'a pas encore été exécuté. Une future
gestion intermittente nécessitera d'abord ces mesures ; aucun profil Silence
permanent ni daemon n'est implémenté à ce stade.

### Résultats de l'essai thermique de 60 secondes

L'essai utilisateur commence à 33/35/62 °C. Les fins de lectures successives,
comptées depuis le début de la tentative d'arrêt, sont :

| Fin de lecture (s) | Locale °C | Distante °C | Module °C |
|---|---|---|---|
| 9,048 | 33 | 35 | 64 |
| 12,063 | 33 | 35 | 64 |
| 15,085 | 33 | 35 | 64 |
| 18,115 | 33 | 35 | 64 |
| 21,133 | 33 | 35 | 64 |
| 24,146 | 33 | 35 | 64 |
| 27,153 | 33 | 35 | 64 |
| 30,170 | 33 | 35 | 64 |
| 33,177 | 33 | 35 | 64 |
| 36,185 | 33 | 36 | 64 |
| 39,205 | 33 | 35 | 64 |
| 42,219 | 33 | 36 | 64 |
| 45,236 | 33 | 36 | 64 |
| 48,255 | 33 | 36 | 64 |
| 51,277 | 34 | 36 | 64 |
| 54,298 | 34 | 36 | 64 |
| 57,316 | 34 | 36 | 64 |
| 60,331 | 34 | 36 | 64 |

Maxima : 34/36/64 °C, soit +1/+1/+2 °C par rapport à la lecture initiale.
Aucun seuil 40/45/66 n'est atteint. La température distante oscille d'abord
entre 35 et 36, et la locale monte à 34 en fin d'essai : ce n'est pas une preuve
d'équilibre thermique, et il ne faut pas extrapoler une pente linéaire.
Le module était déjà à 64 °C dans plusieurs essais antérieurs ventilés ; le
delta initial de deux degrés ne suffit pas à établir un lien causal quantifié.

Le retour automatique reçoit un succès EC puis deux lectures mode 0/classe 1,
confirmant la rotation. La lecture précoce du profil pendant l'arrêt était
encore mode 1/classe 1 ; aucune lecture de classe 0 n'a été faite plus tard
dans cette minute, car la surveillance ne relisait que les températures.
La trace ne prouve donc pas à elle seule une immobilité continue sur 60 secondes.

Cette observation justifie de conserver l'essai de silence temporaire borné,
mais ne valide ni l'arrêt permanent ni un cycle intermittent autonome.
Les commandes et seuils n'ont pas été modifiés sur la seule base de ce résultat.

## CLI et bibliothèque — 7 octobre 2026

Le transport testé dans `hpm-probe` est porté dans `lib/hpm.c` et partagé par
la CLI et une API C (`include/dock.h`, bibliothèques statique/dynamique).
Les négociations restent explicites, sans Exit. Identité Dell et EC connu
sont exigés, le RID est découvert, et un verrou de processus est partagé avec
les commandes actives du prototype. Les registres de puissance sont exposés
bruts : leur disposition Apple n'est pas assimilée au format TI générique.

Le watcher possède une simulation indépendante du matériel. Défauts : session
300 s, silence cible 60 s, automatique au moins 30 s, reprise du silence à
38/43/64 °C, retour automatique à 40/45/66 °C. Une écriture ambiguë déclenche
la restauration ; annulation et retard sont vérifiés entre échanges. Les
latences I/O et l'absence de watchdog matériel restent des limites. Un callback
bloquant ou un processus suspendu ne peut pas surveiller le dock.

`make check` réussit : décodage HID/HPM, politique thermique, hystérésis,
expiration, erreurs, restauration, annulation, retard, arguments CLI et JSON,
ainsi que les anciens auto-tests. La bibliothèque liée statiquement et
dynamiquement lit l'identité réelle. macOS annonce 88 W et 4500 mA pour son
chargeur ; le dock déclare toujours 130 W pour son bloc. Aucune consommation
instantanée ni attribution certaine du chargeur au dock n'en est déduite.

Le transport HPM refactorisé et les cycles du watcher n'ont pas encore été
exécutés sous sudo sur le matériel. La validation suivante commence par
`sudo ./dockctl inspect --json`, puis un essai borné de `watch --silence`.
Les succès du prototype ne constituent pas une validation du nouveau code.

## Annulation du watcher et recherche de puissance/ports — suite du 7 octobre

### Trace physique du watcher et Ctrl-C

La trace utilisateur valide le début du watcher : plusieurs lectures en mode
0/classe 1, puis mode 1/classe 0 à 33/35/64 °C. Après plusieurs Ctrl-C, aucune
ligne de restauration n'était visible dans l'extrait. Lors de l'inspection par
l'agent, aucun processus dockctl n'était encore actif ; l'état final du
ventilateur n'a pas pu être relu car sudo exige le mot de passe dans Terminal.
Cela ne suffit pas à diagnostiquer un blocage permanent.

Le code attendait la fin d'une lecture de six secondes avant de vérifier le
signal, puis effectuait deux autres opérations de six secondes, sans message
intermédiaire. Correction : message async-signal-safe via write() sur stderr,
contrôle d'annulation dans la boucle d'échange HPM du watcher et avant une
consigne d'arrêt. La restauration et la lecture finale ignorent l'annulation.
Un test simule l'annulation au milieu d'une lecture après arrêt : la restauration
s'exécute et l'état final est automatique. Aucun mécanisme ne peut interrompre
un appel kernel HPM bloqué de façon sûre dans ce processus.

### Profils et télémétrie obtenus sans sudo

Lecture de propriétés AppleSmartBattery via IORegistryEntryCreateCFProperties.
Les clés sont privées et les unités inférées ; ces champs ne viennent pas d'une
lecture directe de l'EC du dock. `dockctl host-power --json` et l'API
`dock_read_host_power()` les rendent disponibles avec cette provenance.

| Propriété observée | Valeur brute | Interprétation |
|---|---|---|
| AdapterDetails.Watts | 88 | Puissance annoncée, W |
| AdapterDetails.AdapterVoltage | 19500 | Tension annoncée, mV |
| AdapterDetails.Current | 4500 | Courant plafond annoncé, mA |
| UsbHvcMenu index 0 | 5000 / 3000 | Profil 5 V / 3 A |
| UsbHvcMenu index 1 | 19500 / 4500 | Profil 19,5 V / 4,5 A |
| UsbHvcHvcIndex | 1 | Sélection Apple ; pas une position PDO décodée |
| PowerTelemetryData.SystemVoltageIn | 19234 | Entrée Mac, mV |
| PowerTelemetryData.SystemCurrentIn | 1782 | Entrée Mac, mA |
| PowerTelemetryData.SystemPowerIn | 34274 | Entrée Mac, mW |

Les trois dernières valeurs proviennent du même snapshot. 19,234 × 1,782 =
34,274988 W, cohérent avec les 34,274 W exposés. Un relevé précédent donnait
19,300 V / 1,501 A / 28,976 W. 19,5 × 4,5 = 87,75 W explique aussi les 88 W
arrondis du chargeur annoncé. Ce sont des indices de cohérence, pas un étalonnage
avec wattmètre. Les valeurs peuvent rester en cache, et l'attribution de la
source au dock n'est pas prouvée par ce chemin. Ne pas confondre consommation
d'entrée du Mac, charge de sa batterie, consommation interne du dock et total
à la prise. `PowerOutDetails` n'était pas présent : aucune donnée par port
obtenue par ce relevé. Même présent, ce champ concernerait les ports du Mac et
ne démontrerait pas une mesure des ports aval du dock.

### RPM exacts : frontière du protocole retrouvée dans EC03

La fonction `0x9d016ac4` lit les registres 0x08/0x09 du contrôleur à adresse
I2C 8 bits 0x30, puis appelle `0x9d0169cc` pour convertir le tachymètre via
6 000 000 / tach. Ses appels directs retrouvés sont à `0x9d013224` (logique
interne), `0x9d013740` (classification) et `0x9d016b3c` (régulation).
Le handler VDM 0x0f passe par le classificateur `0x9d013738`, qui réduit le
résultat à 0/1/2, puis n'envoie que cette classe. Aucun de ces chemins n'envoie
le RPM exact au PC. Cela établit où la précision est perdue ; aucun accès de
lecture externe à ces registres n'est encore validé.

### Commande de ports : piste réelle, pas une commande prête à utiliser

La table du dispatcher EC03 à `0x9d010f68` associe l'opcode interne 0x04 au
handler `0x9d010e04`. Ce handler exige le port upstream sélectionné et accepte
des sous-commandes 1 à 6. Les sous-commandes 1 et 4 appellent respectivement
`0x9d0106a4` et `0x9d010588`, qui ciblent les contextes PD 4 et 5 pour un module
8. La seconde écrit le registre PD 0x52 (valeur 0 ou 3) ; les deux chemins
peuvent envoyer la tâche ASCII **HRST**, selon l'état de connexion. Les
sous-commandes 2/3 changent des états via `0x9d017824` avant reconfiguration.

Il reste à identifier le sens des bits, l'association des contextes aux prises
physiques et les conséquences sur vidéo/alimentation. Aucun de ces paquets
n'a été envoyé et aucune commande de contrôle de port n'a été ajoutée. Le
présenter déjà comme « couper seulement la prise USB choisie » serait faux.

### Contrat PD et puissance totale

Les registres actifs PDO/RDO 0x34/0x35 sont documentés chez TI, mais la disposition
Apple doit être validée sur la capture réelle :
[TI Host Interface](https://www.ti.com/lit/ug/slvuan1a/slvuan1a.pdf).
Le dump `sudo ./dockctl power --json` a été demandé à l'utilisateur. Pas de
commande ADC, modification de contrat, reset ni écriture de courbe envoyée.
Aucune mesure de consommation totale ou par port du dock n'a encore été
établie. Les résultats ci-dessus ne doivent pas être extrapolés à ces mesures.

### Capture PD reçue et décodage corrélé

La sortie fournie ensuite par l'utilisateur est conservée sans modification
dans `research/power-capture.json`. Le `null` du contrat correspond à l'ancien
programme, avant ce décodage. RID 2, état Power Status connecté.

- `0x30` : compteur 2, PDO1 `0x3f01912c` = fixe 5 V / 3 A, PDO2
  `0x000619c2` = fixe 19,5 V / 4,5 A. Les champs USB-PD utilisent 50 mV et 10 mA.
- `0x34` : succès avec zéro octet. Ne pas interpréter cela comme puissance zéro.
- `0x35` : dix octets `d6 09 87 27 c2 19 06 00 fc 2e`. Disposition inférée :
  RDO `0x278709d6`, PDO actif `0x000619c2`, trailer inconnu `0x2efc`.
- Le RDO sélectionne l'objet 2 et demande un courant de fonctionnement de
  4500 mA. Son champ maximum vaut 4700 mA, GiveBack vaut 0, Capability Mismatch
  vaut 1. Le PDO embarqué est identique à la deuxième offre source ; cela
  concorde avec l'index Apple 1, 19500 mV et 4500 mA observés indépendamment.
- Capacité de fonctionnement : 19500 × 4500 / 1000000 = **87,75 W**. Le maximum
  demandé de 4,7 A n'est ni une mesure ni la capacité accordée de la source.

Les positions et unités PDO/RDO sont recoupées avec les définitions USB-PD du
[noyau Linux](https://github.com/torvalds/linux/blob/master/include/linux/usb/pd.h).
La disposition Apple de dix octets demeure une inférence locale, pas un format
TI générique annoncé comme compatible universellement. `dock_decode_pd()` exige
longueurs, compteur, connexion, PDO fixe reconnu, position cohérente, égalité
exacte du PDO et cohérence des courants. Les types PPS/EPR/GiveBack et les
formats inconnus ne sont pas devinés. Les deux octets finaux restent bruts.

La CLI `power` et la bibliothèque exposent maintenant ce décodage, accompagné
du drapeau `apple_layout_inferred`. Tests sur les octets fournis : lecture
positive, PDO contradictoire, déconnexion, format court, compteur invalide,
échec de registre et demande dépassant l'offre sans Capability Mismatch.
Le projet [CableScope](https://github.com/tzzs/cablescope) a aussi été consulté
comme piste de lecture des propriétés de source par port ; sa classe
IOPortFeaturePowerSource n'a donné aucune entrée sur ce Mac. Aucune nouvelle
commande matérielle n'a été envoyée pour ce décodage.

### Watcher : stabilité thermique et limite de cinq minutes

La dernière trace utilisateur confirme une sortie Ctrl-C avec message immédiat,
puis `[finished]` en automatique, classe 1. Elle montre également un silence
court suivi d'une reprise à 33/35/64 °C. L'ancien journal remplaçait la mesure
déclenchante par celle obtenue après le réglage ; sans horodatage ni motif, cette
trace ne permet pas de départager un pic thermique et l'expiration du délai.
Le watcher émet désormais le motif et la mesure avant écriture, puis les relevés
suivants avec temps écoulé et temporisation. Les erreurs de lecture partielle
conservent la dernière mesure complète pour le journal de restauration.

À la demande de l'utilisateur, le silence dépend des températures avec un maximum
de 300 secondes par période. Défauts : ventilation à 42/47/68 °C après 30 secondes
de dépassement continu ; reprise du silence à 40/45/66 °C ou moins pendant
30 secondes, après au moins 30 secondes en automatique. Les seuils, la durée de
confirmation (1–120 s), la période silencieuse (9–300 s) et le minimum automatique
(30–600 s) sont configurables dans la CLI et la bibliothèque. Le passage d'un
capteur chaud à un autre ne remet pas à zéro un dépassement resté continu ; un
retour de tous les capteurs sous leurs seuils le remet à zéro.

Garde-fous critiques 55/60/73 °C : restauration sans temporisation de stabilité,
comme pour l'expiration, l'annulation et les erreurs. Ce sont des choix
expérimentaux du projet, pas des limites constructeur certifiées ; ils sont
inférieurs ou égaux aux seuils EC03 du palier suivant (62/70/73 °C). La latence
des échanges HPM demeure : cinq minutes est une échéance logicielle, pas une
coupure matérielle garantie à la milliseconde.

API C version 2 : options, état de temporisation et callback événementiel ont
changé ; les intégrations doivent être recompilées. Les simulations couvrent
le dépassement durable, un pic isolé, les seuils critiques sans délai, plusieurs
cycles de cinq minutes, les mesures conservées et la restauration sur erreur
ou annulation. Aucune nouvelle commande matérielle n'a été envoyée pendant ce
développement ; ces nouveaux paramètres restent à tester sur le dock.
