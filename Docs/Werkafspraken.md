# Werkafspraken

## Communicatie
- Antwoord in het Nederlands. Code, identifiers, commit-berichten en repo-documentatie blijven Engels.
- Ik doe zelf het editor-/designerwerk (layouts, Blueprints, wiring, tuning via Project Settings).
  Jij doet code, scripts, diagnose en uitleg. Moet iets in de editor gebeuren, beschrijf dan
  stap voor stap wat ik moet doen.
- "Screenshot bijgevoegd" (of "zie screenshot") betekent: de nieuwste afbeelding in `Docs/Screenshots/`
  (lokaal, gitignored). Lees die zelf; screenshots komen niet mee in het bericht.

## Werkwijze bij nieuwe features
1. Stel eerst gerichte vragen (meerkeuze, met een aanbevolen optie).
2. Vat daarna het plan kort samen, inclusief de editorstappen die ik moet doen.
3. Bouw pas na mijn "akkoord" / "bouw het zo".
- Bij kleine fixes of een expliciete opdracht ("draai het om") sla je de vragen over.

## Git
- Commit en push alleen als ik daar die beurt expliciet om vraag ("commit", "commit en push").
  Na een werkende wijziging: rapporteer de status en wacht.
- Experimenten krijgen een eigen branch plus een tag op het startpunt als terugweg
  (bijv. branch `foo-experiment`, tag `before-foo`). Na goedkeuring merge je naar de hoofdbranch.
- Binaire assets via Git LFS. Third-party packs/content blijven buiten git (gitignore).
  Controleer `git status` op nieuwe mappen vóór een `git add`.
- Elk third-party asset (packs, fonts, ...) krijgt een regel met de licentie in een licentieoverzicht
  (bijv. `Docs/Licenses/README.md`).
- Meld eerlijk als iets gecommit is zonder dat het gecompileerd/getest is.

## Debuggen
- Bij runtimeproblemen: voeg tijdelijke debuglogs toe met een unieke tag (bijv. `[ReserveFlightDebug]`)
  langs de hele keten, plus een tabel die uitlegt wat elke regel betekent. Ik plak de gefilterde output terug.
- Zet de logs bij voorkeur niet in headers (dan werkt hot reload).
- Verwijder ze zodra ik bevestig dat het werkt, en check dat de diff alleen de echte fix bevat. Dat gebeurt vóór de commit.

## Documentatie in de repo
- `CLAUDE.md`: alleen huidig gedrag en regels.
- `Docs/Architecture.md`: de mechanica per systeem. Lees het relevante deel vóór je een systeem wijzigt
  en houd het bij.
- `Docs/STATUS.md`: korte huidige stand + open werk. Lees het aan het begin van een sessie,
  overschrijf het (niet aanvullen) als de status verandert.
- `Docs/DEVLOG.md`: gedateerde geschiedenis (beslissingen, bugs, verworpen aanpakken). Vul het aan na een
  noemenswaardig stuk werk. Niet helemaal lezen, maar doorzoeken.
- Wijzigingen die in binaire assets moeten gebeuren noteer je in het commit-bericht of de DEVLOG.

## Code en data
- Gedrag zo veel mogelijk in code (binaire assets zijn niet te diffen of te bewerken).
- Tuningwaarden staan in config (bijv. `Config/DefaultGame.ini` via settings-klassen), niet als
  hardcoded defaults. Let op: een default in code wijzigen doet niets als de ini de waarde al zet.
- Data-gedreven waar het kan (nieuwe stats/resources = datawijziging, geen code).
- Draai na elke wijziging aan een subsysteem de automatische tests. Tests zijn onafhankelijk van
  productiedata (eigen testdata).
- Volg de stijl, naamgeving en commentaardichtheid van de omringende code.

## Unreal-specifiek
- Kijk vóór een build of headless editorrun of de editor draait (`tasklist | grep -i unrealeditor`).
  - Alleen .cpp gewijzigd → laat mij Live Coding doen (Ctrl+Alt+F11).
  - Header/UPROPERTY/UFUNCTION/members gewijzigd → vraag mij de editor te sluiten en build pas na mijn bevestiging.
  - Sluit of kill nooit zelf mijn editor.
- Binaire assets bewerk je headless met editor-Python (`UnrealEditor-Cmd.exe ... -run=pythonscript`),
  alleen met de editor dicht. Log met een unieke tag en grep de output.
