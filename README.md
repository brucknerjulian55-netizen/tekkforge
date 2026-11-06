# Tekkforge

Multisynth als VST3 für FL Studio, mit fünf Modi: Melo One-Shot, Stab, Topkick, Kickbass und Full Kick.

## Bauen

**Auf dem eigenen PC**

1. Visual Studio Community installieren und dabei „Desktopentwicklung mit C++“ anhaken.
2. `build.bat` doppelklicken. Beim ersten Mal lädt das JUCE herunter und dauert einige Minuten.
3. Den Ordner `Tekkforge.vst3` nach `C:\Program Files\Common Files\VST3` kopieren.
4. In FL Studio: Options > Manage plugins > Find plugins. Tekkforge lädt wie jedes Instrument, auch im Patcher.

**Über GitHub, ohne Visual Studio**

1. Den Ordner als Repository hochladen. Das Windows-Build startet dann von selbst.
2. Unter Actions den letzten Lauf öffnen und `Tekkforge-VST3` herunterladen.

## Bedienung

- **Modus-Knöpfe oben:** Jeder Modus merkt sich eigene Einstellungen. FX und Auto gelten für alle Modi.
- **Synth:** Alle Regler sind drehbar, Doppelklick setzt sie zurück. In den Anzeigen lassen sich die Kurven direkt ziehen.
  Bei der Pitch-Kurve ändert das Mausrad die Krümmung.
- **FX:** Acht Slots in Reihenfolge von oben nach unten. Jeder Effekt darf mehrfach vorkommen.
  Slot anklicken, Effekt wählen. Reihen lassen sich ziehen, der Punkt rechts schaltet den Slot an oder aus.
- **Auto:** Vier interne Automationen. Kurve zeichnen, Ziel wählen oder „Ziel per Klick“ nutzen. Ein weißer Bogen am Regler zeigt die Bewegung.
- **Guide:** Frequenzbereiche und Prüfliste des aktuellen Modus. Die Zonen sind auch im Spektrum auf der Synth-Seite sichtbar.
- **Sample:** Audiodatei ins Fenster ziehen oder laden. Jeder Modus hat ein eigenes Sample bis 30 Sekunden. Es läuft durch Filter, Drive und FX.
  Beim Laden gehen Oszillator, Click und Sub auf null, damit du es sofort pur hörst.
- **Mod:** Drei LFOs und drei Envs. Zieh eine Quelle aus der Leiste über der Tastatur auf einen Regler, zum Beispiel Env 1 auf Tune für Pitch.
  Die Stärke stellst du auf der Mod-Seite ein. Rechtsklick auf den Regler entfernt eine Verbindung.
  LFOs und Envs laufen gemeinsam für alle Stimmen, nicht pro Stimme.
- **Presets:** „In Tekkforge speichern“ legt das Preset in deiner internen Liste ab, die in jedem Projekt verfügbar ist.
  „Als Datei exportieren“ schreibt eine `.tkf`-Datei an einen beliebigen Ort. Samples stecken mit drin.
  Dazu speichert FL Studio den ganzen Zustand samt Samples im Projekt.
- **Sound fertig rendern:** Der Knopf auf der Presets-Seite rechnet den aktuellen Sound samt FX, LFOs und Envs in ein Sample und spart so Rechenleistung live.
  Vorher fragt Tekkforge, ob du ein Preset speichern möchtest. Gerendert wird die zuletzt gespielte Note. Danach steht alles auf Standard, auch die anderen Modi, und nur das Sample bleibt.
- **Analyzer-Ansichten:** Spektren haben oben rechts die Ansichten Kurve, Balken und Spektrogramm. Fahr mit der Maus darüber, dann stehen Frequenz und Note daneben.
  Bei EQ und Filter liegt die Kurve über dem Analyzer und die Punkte lassen sich ziehen. Die Seite Sample zeigt das geladene Sample ebenfalls als Spektrogramm.
- **Anleitungen:** Auf der Seite Guide gibt es pro Modus Schritt-für-Schritt-Anleitungen. Mit geladenem Sample misst Tekkforge Grundton, Länge und Bassanteil und schlägt Einstellungen vor.
- **Farbdesign:** Auf der Presets-Seite stehen acht Designs. Die Wahl gilt für alle Instanzen.

## Automation in FL Studio

Alle Regler stehen in FL als Parameter bereit: Regler bewegen, dann Tools > Last tweaked > Create automation clip.
FX-Regler heißen in FL nur „FX1 Wert 1“ usw., zeigen im Wert aber den Namen des geladenen Effekts.

## Destructor

Eigene Kette: Drive, Wavefold, Crush und danach ein eingebauter Phaser mit eigenem Mix, Tempo und Feedback.

## Updates

Beim Öffnen fragt das Plugin bei GitHub nach der neuesten Version von `brucknerjulian55-netizen/tekkforge`. Dabei wird nichts installiert und es werden keine Projektdaten gesendet. Ist eine neuere Version da, erscheint ein Hinweis, der die Download-Seite öffnet. Ohne Internet oder ohne Repository bleibt der Hinweis weg.

So veröffentlichst du ein Update:

1. In `CMakeLists.txt` die Versionsnummer erhöhen, zum Beispiel `VERSION 0.2.0`.
2. Das öffentliche Repository `tekkforge` auf GitHub anlegen, falls es noch fehlt, und die Änderungen hochladen.
3. Unter Releases ein neues Release mit dem Tag `v0.2.0` anlegen. Das Windows-Build hängt die ZIP danach selbst an.

Danach alte `.vst3` bei geschlossenem FL Studio ersetzen.

## Tests

Klangkern ohne JUCE prüfen:

    g++ -std=c++17 -O2 -ISource Tests/engine_test.cpp -o engine_test && ./engine_test

## Lizenz

JUCE 8 steht unter AGPLv3 oder einer kommerziellen Lizenz. Vor einer Weitergabe `LICENSE.md` von JUCE prüfen.
