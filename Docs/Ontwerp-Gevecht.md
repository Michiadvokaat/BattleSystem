# Ontwerp: autobattle-gevechtssysteem

Status: fase 1 t/m 5 klaar (2026-10-03). Fase 6 alleen als het nodig blijkt. Dit document is zelfstandig: het gaat uit van een **leeg Unreal Engine 5 C++-project** zonder bestaande gameplaycode. Werk het bij als besluiten veranderen of een fase klaar is (zet dan "Status" en de tabel "Besluiten" bij).

In dit document staat `<Module>` voor de naam van de gamemodule van het project. Alle code komt in `Source/<Module>/Public|Private/Combat/`.

## Uitgangspunten

- **Schaal:** tot ~50 units per gevecht. De opzet draait om eenvoud en determinisme; performance-trucs zijn niet nodig.
- **Beweging:** vrij (continue posities), met een eigen grid als hulp voor pathfinding, zicht en bezetting. Units ontwijken elkaar zacht.
- **Aanvallen:** melee, ranged (met projectielen), AoE en line of sight.
- **Doelwit:** standaard de dichtstbijzijnde vijand *via looppaden*. Aggro (taunt, threat, "wie mij raakt") kan dat overschrijven.
- **Deterministisch** voor replays en balans: dezelfde opstelling met dezelfde seed geeft hetzelfde gevecht, op dezelfde build. Fixed-point rekenen is niet nodig, want cross-platform multiplayer is geen doel; gewone `float`/`double` volstaat.
- **Alles op het eigen grid:** geen NavMesh, Detour Crowd, Character Movement, physics of overlap-events in de gevechtslogica. Die zijn niet deterministisch (asynchrone rebuilds, afhankelijk van de framerate). UE-collision mag alleen gebruikt worden voor muiskliks/selectie.
- **Eén arena-level** om te bouwen en te testen. Integratie in een groter spel valt buiten dit document.

## Architectuur

Drie lagen, met strikte richting: **grid ← simulatie ← weergave**. De simulatie weet niets van actors; de weergave leest alleen.

### Het grid

`ACombatGrid` (actor, met de hand in de arena geplaatst, één per level):

- Instelbaar: `CellSize` (standaard 100 cm), `Width`, `Height` in cellen. De oorsprong is de actorlocatie (hoek van cel 0,0); het grid ligt in het XY-vlak.
- Conversie wereld ↔ cel (`WorldToCell`, `CellToWorld` = celmidden) en `IsInBounds`.
- Per cel twee vlaggen: **loopbaar** en **blokkeert zicht** (zicht pas nodig vanaf fase 3).
- Een vloer-mesh plus optioneel debug-lijnen voor de cellen.

`ACombatObstacle` (actor, in de arena geplaatst): een footprint in cellen (afgeleid van een box-extent, afgerond op cellen) met `bBlocksWalkability` (standaard aan) en `bBlocksSight` (standaard aan). Bij het begin van het level registreren obstakels zich bij het grid, dat daarmee zijn vlaggen vult. Het grid verandert tijdens een gevecht niet.

`FindPath(Start, Goal)` op het grid: **A\***, 8 richtingen, **geen corner-cutting** (diagonaal mag alleen als beide orthogonale buren loopbaar zijn), kosten 1 en √2. Deterministisch: bij gelijke kosten een vaste volgorde van buren en een vaste tiebreak (bijvoorbeeld op celindex). Nodig vanaf fase 4.

### De simulatie (de waarheid)

De kern is een **gewone C++-klasse `FCombatSimulation`**, zonder `UWorld`, actors of timers. Daardoor kan dezelfde code met weergave draaien én headless (voor `Combat.Simulate` en `Combat.Batch`), en is hij makkelijk te testen.

- Invoer bij de start: een momentopname van het grid (afmetingen, celgrootte, vlaggen), een opstelling (welke units, welk team, welke startcel) en een seed.
- `Step()` voert één vaste stap uit (standaard 20 Hz, dus `FixedDt` = 0,05 s als constante). `DeltaTime` van de engine komt de simulatie nooit binnen.
- Units zijn structs (`FCombatUnit`) in een `TArray`:
  - ID, team, positie/snelheid (`FVector2D`) en straal
  - HP, doelwit-ID en aanvalscooldown
  - threat-lijst (vaste lengte) en taunt-bron/-duur
  - een verwijzing naar de definitie (Data Asset, alleen gelezen)
- Een event-buffer per stap (`Attack`, `Hit`, `Death`, `ProjectileSpawned`) die de weergave na de stap uitleest.

`UCombatSubsystem` (`UTickableWorldSubsystem`) is de dunne laag eromheen: hij bouwt bij het starten van een gevecht de simulatie op uit `ACombatGrid` en een opstelling, vult in `Tick(DeltaTime)` een accumulator en voert zo nodig 0..N `Step()`s uit (met een maximum per frame tegen een "spiral of death"), en geeft de events door aan de weergave. Hij levert ook de interpolatiefactor (accumulator / `FixedDt`).

**Regels voor determinisme:**

- Units worden altijd in ID-volgorde verwerkt.
- Toeval komt alleen uit één `FRandomStream` met een seed. Geen `FMath::Rand`/`FMath::FRand`.
- Geen `DeltaTime`, wall-clock-tijd of UE-physics in de logica.
- Geen itereren over `TMap`/`TSet` in de logica (gebruik `TArray`), en nooit sorteren of vergelijken op pointers.
- Schade en effecten gaan in **twee stappen**: eerst alle aanvallen van deze stap verzamelen, dan alles tegelijk toepassen. Zo maakt de verwerkingsvolgorde niet uit wie eerst sterft.
- Na elke stap wordt een **checksum** van de toestand berekend (bijvoorbeeld CRC32 over posities, HP en doelwitten), waarmee je kunt controleren of een replay identiek is.

### De weergave (volgt alleen)

`ACombatUnitActor` (Pawn of Actor, door het subsystem gespawnd per unit):

- Begint als simpele placeholder (cilinder of capsule-mesh in de teamkleur); een skeletal mesh met animaties kan later via een Blueprint-subklasse.
- Leest elke frame zijn unit-state en interpoleert tussen de vorige en de huidige simulatiestap, zodat beweging vloeiend oogt bij 20 Hz. Kijkrichting volgt de bewegingsrichting of het doelwit.
- Reageert op simulatie-events voor animatie, VFX en schadegetallen (eerst debug-tekst; later bijvoorbeeld widgets of Niagara).
- Doet zelf **geen** gameplaylogica en wordt niet door physics bewogen.

### Data

- `UCombatUnitDefinition` (Data Asset): naam, HP, snelheid, straal, een lijst aanvallen (type als GameplayTag: melee/ranged/AoE/taunt, bereik, cooldown, schade, projectielsnelheid, AoE-straal, heeft zicht nodig) en threat-modifiers. Plus de actorklasse voor de weergave.
- `UCombatSetup` (Data Asset): een lijst van (definitie, team, startcel). Hiermee worden test-opstellingen gemaakt zonder code.
- Afstemwaarden in **`UCombatSettings`** (`UDeveloperSettings`, Project Settings → Game → Combat, opgeslagen in `DefaultGame.ini`): tickrate, max stappen per frame, interval voor doelwitkeuze, hysterese, threat-verval en separation-sterkte. Nieuwe afstemwaarden komen daar, niet hardcoded.

### Relatie met het Gameplay Ability System (GAS)

GAS wordt **niet** gebruikt als runtime. Het botst met de kern van dit plan:

- Effecten met duur, periodieke effecten en ability tasks lopen op wereldtijd, timers en frames, niet op een vaste stap die je zelf aanroept.
- Het hangt aan een `UAbilitySystemComponent` op actors met UObject-abilities en -attribute sets, dus het draait niet in een headless `FCombatSimulation` zonder `UWorld`.
- Per gevecht ~50 actors spawnen maakt `Combat.Batch` (1000 gevechten in een paar seconden) onhaalbaar.
- Veel van GAS (replicatie, prediction) is voor multiplayer, wat geen doel is.

Wat wel wordt overgenomen:

- **GameplayTags** (module `GameplayTags`) in de simulatie en de data: aanvalstypes, statussen (`Status.Taunted`, `Status.Stunned`), immuniteiten en filters (bijvoorbeeld "raakt alleen `Unit.Flying`"). `FGameplayTag`/`FGameplayTagContainer` zijn lichte value-types zonder wereld of tijd. Tags worden gedefinieerd in `Config/DefaultGameplayTags.ini` of als native tags in C++.
- **Het datamodel van GAS, als eigen structs die op ticks draaien:**

  | GAS-begrip | Hier |
  |---|---|
  | Attribute set | Velden in `FCombatUnit` (HP, snelheid, later bijvoorbeeld armor) |
  | GameplayEffect | `FCombatEffectDefinition` in de Data Asset: modifiers op attributen, duur en periode in **ticks**, stackingregel, tags die het toekent (`GrantedTags`) en tags die het blokkeren (`BlockedByTags`) |
  | Actief effect | `FCombatActiveEffect` in een `TArray` per unit: definitie, bron-unit-ID, resterende ticks, stacks |
  | Gameplay ability | Een aanval/vaardigheid in `UCombatUnitDefinition`: type (tag), cooldown, targeting, en de effecten die hij toepast |
  | GameplayCue | Een cue-tag op een simulatie-event (`Cue.Hit.Fire`); de weergave koppelt tags aan VFX/geluid via een eigen tabel |

  Effecten worden, net als schade, in de twee-stappenfase toegepast en in vaste volgorde verwerkt (unit-ID, dan volgorde in de array).

## Fases

Elke fase levert iets op dat je kunt spelen en testen. Nieuwe C++-klassen vereisen een volledige build (projectbestanden opnieuw genereren, bouwen, editor herstarten); Live Coding is alleen geschikt voor wijzigingen in bestaande functies.

### Fase 1: grid, simulatiekern, arena en melee

**Doel:** twee teams lopen op elkaar af en vechten met melee tot één team over is.

- `ACombatGrid` en `ACombatObstacle` (zonder pathfinding; obstakels worden in deze fase nog niet ontweken).
- `FCombatSimulation` met vaste stap, unit-structs, seed, event-buffer en checksum; `UCombatSubsystem` eromheen.
- `UCombatUnitDefinition`, `UCombatSetup` en een paar test-definities (bijvoorbeeld "Krijger" en "Brute") plus een test-opstelling.
- `UCombatSettings`.
- Module `GameplayTags` toevoegen; aanvalstypes als tags (`Attack.Melee` enz.) in plaats van een enum.
- `ACombatUnitActor`-weergave met interpolatie en een simpele aanval- en sterfreactie.
- Level `Content/Maps/Arena-01.umap`: een `ACombatGrid`, een paar obstakels, een vaste camera van bovenaf, en een eigen `ACombatGameMode` die als GameMode Override op het level staat.
- **Beweging nog simpel:** recht op het doelwit af en stoppen binnen bereik. Doelwit = dichtstbijzijnde vijand hemelsbreed (tijdelijk). Gelijke afstand: laagste ID.
- Einde van het gevecht: één team heeft geen levende units meer, of een tijdslimiet (in ticks) is bereikt.
- Console-commando's:
  - `Combat.Start <seed> [setup]`: start een gevecht met weergave in de arena.
  - `Combat.Simulate <seed> [setup]`: draait een gevecht zonder weergave en print winnaar, duur (ticks) en eind-checksum.
- **Klaar als:** twee keer `Combat.Simulate 42` precies dezelfde checksum geeft, een andere seed (bij toeval in de logica) een andere, en een gevecht in de arena er goed uitziet.

### Fase 2: pathfinding, ontwijken en "dichtstbijzijnd via looppaden"

**Doel:** units lopen om obstakels heen, botsen niet in elkaar en kiezen de vijand die het snelst bereikbaar is.

- Per team een **afstandskaart**: multi-source Dijkstra over het grid vanaf de cellen van alle vijanden (zelfde 8-richtingenregels als A\*). Elke cel slaat op hoe ver de dichtstbijzijnde vijand is *en welke vijand dat is*. Herberekend op een interval (standaard 0,25 s = 5 ticks).
  - Het doelwit van een unit is dan direct af te lezen in de cel waar hij staat.
  - De looprichting is "naar de buurcel met de laagste afstand".
- **Path smoothing:** sturen naar het verst zichtbare punt verderop op de route (zichtbaar = rechte lijn over loopbare cellen), zodat units schuin lopen in plaats van van celmidden naar celmidden.
- **Separation-steering:** units duwen elkaar zacht weg binnen hun straal. Dat is O(n²) en bij 50 units ruim snel genoeg. Ze schuiven wel langs elkaar, maar blokkeren niet. Een unit mag nooit eindigen in een onloopbare cel.
- **Klaar als:** units netjes om een muur heen lopen naar een vijand die hemelsbreed verder weg is maar sneller bereikbaar, en de checksum-test uit fase 1 nog steeds slaagt.

### Fase 3: ranged, projectielen en line of sight

**Doel:** boogschutters houden afstand en schieten alleen als ze zicht hebben.

- De zichtvlag per cel (van `ACombatObstacle::bBlocksSight`) gaat meedoen.
- **LoS-raycast** over de cellen (DDA, deterministisch).
- **Ranged gedrag:** stoppen op aanvalsafstand mits er zicht is; anders verder lopen tot er zicht is.
- **Projectielen als simulatie-objecten:** positie, snelheid, doelwit en schade. Standaard volgen ze hun doelwit (homing), zodat ze nooit missen door afronding. Botsen ze onderweg tegen een zicht-blokkerende cel, dan zijn ze weg. Sterft het doelwit onderweg, dan verdwijnt het projectiel ook.
- De weergave toont projectielen (simpele mesh of Niagara) en volgt alleen.
- **Klaar als:** een boogschutter achter een muur eerst omloopt voordat hij schiet.

### Fase 4: aggro

**Doel:** tanks trekken vijanden naar zich toe.

- **Threat-lijst per unit** met vaste lengte (bijvoorbeeld 8 plekken). Bronnen: ontvangen schade × modifier, healing door vijanden, taunt. Elke tick neemt threat een vast percentage af. "Wie mij raakt" is daarmee geen apart mechanisme: het is ontvangen schade in de threat-lijst.
- **Doelwitkeuze** om de 0,25 s:
  1. Taunt actief? Dan de taunter.
  2. Anders: de hoogste threat boven de drempel.
  3. Anders: de dichtstbijzijnde vijand via de afstandskaart.

  Gewisseld wordt alleen als het nieuwe doelwit duidelijk beter is (hysterese, bijvoorbeeld 20% meer threat of 1,5 m dichterbij), zodat units niet heen en weer springen.
- Een unit met een afwijkend doelwit gebruikt een **eigen A\*** (`FindPath` op het grid, met smoothing) in plaats van de teamkaart.
- **Effectmodel invoeren** (zie "Relatie met GAS"): `FCombatEffectDefinition` en `FCombatActiveEffect` met duur in ticks, stacking en granted/blocked tags.
- **Taunt-vaardigheid** als aanvalstype `Attack.Taunt` in de unit-definitie; hij past een effect toe dat `Status.Taunted` toekent met de taunter als bron. Dat is het eerste effect.
- Debugweergave: een lijn van elke unit naar zijn doelwit, gekleurd naar de reden (dichtstbij, threat of taunt).
- **Klaar als:** een tank met taunt de vijanden wegtrekt van de boogschutters achter hem.

### Fase 5: AoE, replays en balanstools

**Doel:** gebiedsschade, en gevechten kunnen herhalen en massaal testen.

- **AoE-aanvallen:** cirkel of kegel rond het doel of de aanvaller, met een optionele vertraging (telegraph). Ze zoeken units via een simpele bucket-grid (cellen van ~2 m), zodat het later ook met meer units snel blijft.
- AoE, telegraphs en eventuele buffs/debuffs bouwen op hetzelfde effectmodel. Simulatie-events krijgen cue-tags; de weergave koppelt die via een tabel aan VFX en geluid.
- **Replay:** de opstelling, de seed en de build-versie opslaan (bijvoorbeeld als `USaveGame` of JSON), en het gevecht later opnieuw afspelen met weergave. De checksum laat zien dat het identiek is.
- **Balans:** `Combat.Batch <aantal> <setup>` draait N gevechten zonder weergave met verschillende seeds. Het geeft het winstpercentage per team, de gemiddelde duur en de schade per unit-type, en exporteert dat optioneel naar CSV in `Saved/`.
- **Klaar als:** 1000 gevechten zonder weergave in een paar seconden klaar zijn met bruikbare statistieken.

### Fase 6: navigatiegrid verfijnen (pas als het nodig blijkt)

**Doel:** nauwkeurigere paden en units van verschillende groottes.

- **Navigatielaag** afgeleid van het grid met een vaste onderverdeling (`NavSubdivision`: 1, 2 of 4). Obstakels en opstellingen blijven in hele cellen. 25 cm-cellen betekent 16× zoveel cellen; begin met factor 2 plus smoothing.
- **Clearance:** cellen bij obstakels dichtzetten op basis van de straal van een unit. Per groottecategorie een eigen kaart, bijvoorbeeld klein en groot.
- **Klaar als:** units met verschillende groottes zonder door muren te schuren door een smalle doorgang lopen.

## Besluiten (aan te passen)

| Onderwerp | Gekozen standaard | Alternatief |
|---|---|---|
| Simulatie-tick | 20 Hz, vaste stap | 30 Hz bij snelle units |
| Simulatiekern | Losse C++-klasse, headless te draaien | Logica direct in het subsystem |
| "Dichtstbijzijnd" | Via looppaden (afstandskaart) | Hemelsbreed |
| Projectielen | Homing, raken altijd tenzij geblokkeerd | Vrije vlucht, kunnen missen |
| Navigatiegrid | 100 cm met smoothing, verfijnen in fase 6 | Direct 25 cm |
| Afstemming | Eigen `UCombatSettings` | Hardcoded constanten |
| Weergave | Placeholder-meshes, later skeletal | Direct skeletal met animaties |
| GAS | Alleen GameplayTags en het datamodel (eigen structs op ticks) | Volledig GAS (vervalt: vaste tick, checksum, headless batch) |
| Melee-timing | Windup in ticks; hit vervalt als het doel tijdens de windup sterft | Schade direct bij de aanval |
| Toeval in fase 1 | Willekeurige vertraging (0..`MaxFirstAttackDelay`) van de eerste aanval, pas zodra een unit binnen bereik is | Variatie in schade |
| Doelwit ranged | Eerst de dichtstbijzijnde vijand in bereik met zicht, anders via looppaden | Altijd via looppaden |
| Meerdere aanvallen | Kortste bereik dat het doel nu kan raken; elke aanval eigen cooldown | Alleen de eerste aanval |
| Kiten | Nee, ranged stopt op bereik | Terugwijken bij melee dichtbij |
| Taunt | Gebied rond de tank, effect met duur | Eén doelwit |
| Doelwitprioriteit | Taunt > threat > zichtbaar (ranged) > dichtstbij, met hysterese | Zichtbaar vóór threat |
| Threat-bron | Ontvangen schade × ThreatMultiplier van de aanval | Alleen taunt |
| Threat-verval | Instelbaar: halfwaardetijd (standaard 4 s) of lineair per seconde | Eén vaste vorm |
| Effectmodel fase 4 | Tags, duur, stacking (geen attribuut-modifiers) | Ook modifiers |
| AoE-vormen | Cirkel op doel, cirkel rond aanvaller, kegel; telegraph op vaste plek, geen ontwijken | Alleen cirkels; ontwijk-AI |
| Friendly fire | Per aanval (bAffectsEnemies/bAffectsAllies) | Altijd alleen vijanden |
| Modifiers | Snelheid, uitgedeelde en ontvangen schade (per stack) | Later |
| Replay | JSON met setup-pad, seed, build, sim-instellingen en eind-checksum | USaveGame; volledig zelfstandige snapshot van unit-stats |
| Spelersinvoer | Commando's per unit (verplaatsen naar cel, spelersvaardigheid) met vaste vertraging (3 ticks), in een commando-log dat in de replay staat | Directe ingrepen in de simulatie |
| Verplaatsen vs. taunt | Het commando van de speler wint | Taunt wint |
| Spelersvaardigheden | Per unit-type in de Data Asset, zonder cooldown | Vaste vaardigheid voor iedere unit |
| Levels (LevelDesigner) | JSON in Levels/ (in git): grid, muur/heg/water, units; vervangt grid + opstelling van de arena | Data Assets |
| Replay en level | Volledige kopie van het level in de replay | Verwijzing + checksum |
