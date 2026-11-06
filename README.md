# Tekkforge

Multisynth als VST3 für FL Studio, mit fünf Modi: Melo One-Shot, Stab, Topkick, Kickbass und Full Kick.

## Neu in 0.4

**Oszillatoren:** 36 Wellen in sieben Gruppen (Grundformen, Bass, FM, Harmonisch, Digital, Vokal, Rauschen), jede mit 16 Positionen und bandbegrenzt. Dazu neun Warp-Arten (Sync, Bend, PWM, Spiegel, Fold, Quantize, Asym, Self-FM).

**Bass:** Tonlage Normal, Bass-Lage oder Fest. Bass-Lage legt jede Taste zwischen 40 und 80 Hz. Dazu Tail Pitch, Obertöne 2, 3 und 5, Mono bis, Phase 180, Boost vor dem Drive, zwei Drive-Stufen, zehn Verzerrungsarten und zehn fertige Bass-Charaktere (fünf für den Topkick).

**Filter:** LP 12 und 24, HP 12 und 24, Bandpass, Notch, Comb + und -, Formant.

**EQ:** Eigene Seite, je ein EQ für Master und jede Ebene mit acht Bändern. Acht Formen, Flanken 12 bis 48 dB, Stereo, Mid, Side, Links, Rechts und Dynamik pro Band. Punkte ziehen, Mausrad für Q, Doppelklick zum Anlegen und Löschen, Rechtsklick für die Form.

**Effekte neu:** OTT, Transient, Waveshaper, Stereo, Freq Shift, Ring Mod, Rumble, Tape, Gate.

**Oberfläche:** Die Synth-Seite zeigt oben drei Ebenen und darunter alle Werkzeuge der gewählten Ebene. Alte Presets werden beim Laden umgerechnet.

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

- **Modi:** Melo, Stab und Kick. Jeder Modus merkt sich eigene Einstellungen.
- **Ebenen:** Jeder Modus hat drei Ebenen untereinander, im Kick-Modus Topkick, Bass und Layer. Jede Ebene ist ein kleiner Synth mit Quelle, Hüllkurve, Pitch, Filter, Drive, Sub, Lowcut und eigenen FX. M schaltet stumm, S hört sie einzeln, FX öffnet ihre Effekte.
- **Quelle:** Welle von Sinus bis Rauschen oder Sample. Ziehe eine Audiodatei direkt auf die Quelle einer Ebene.
- **Länge:** Der Regler Länge bestimmt die Dauer der Ebene. Bei Spielart Notenlänge kürzt zusätzlich die Note im Piano Roll, und Sustain hält den Ton.
- **Vorhören:** Bei gestopptem FL spielt Tekkforge nach jeder Reglerbewegung die letzte Note kurz an. Der Knopf oben schaltet das aus.
- **Env und LFO wie in Vital:** Unten liegen die Reiter Env 1 bis 3 und LFO 1 bis 4. Ziehe einen Reiter auf einen Regler. Am Regler erscheint ein kleiner Punkt: Nach oben oder unten ziehen stellt die Stärke ein, Rechtsklick entfernt die Verbindung. Der helle Bogen zeigt den Bereich, der Punkt im Ring die aktuelle Position. Quellen laufen pro gespielter Note.
- **LFO zeichnen:** Zeichne die Form direkt mit der Maus oder wähle eine Vorlage. Start: Frei läuft durch, Je Note beginnt neu, Einmal läuft einmal durch wie eine Hüllkurve.
- **FX:** Master hat acht Slots, jede Ebene drei. Jeder Effekt darf mehrfach vorkommen.
- **Sidechain:** Lade den Effekt in die Bass-Ebene. Quelle Takt duckt auf jeder Viertel, Note bei jeder Note, Link über den Regler Pegel (zum Beispiel mit dem Peak Controller in FL) und Audio über den Sidechain-Eingang. Bis Freq begrenzt das Ducken auf die Bässe.
- **Sample:** Zeigt das Sample der gewählten Ebene groß, mit Spektrogramm, Tonhöhe, Loop und Startpunkt.
- **Matrix:** Alle Verbindungen in einer Liste.
- **Guide:** Frequenzbereiche, Anleitungen und Vorschläge für geladene Samples.
- **Presets:** „In Tekkforge speichern“ legt das Preset in deiner internen Liste ab. „Als Datei exportieren“ schreibt eine `.tkf`-Datei. Samples stecken mit drin. „Sound fertig rendern“ rechnet alles in ein Sample und setzt den Rest auf Standard.
- **Farbdesign:** Acht Designs auf der Presets-Seite.

## Automation in FL Studio

Alle Regler stehen in FL als Parameter bereit: Regler bewegen, dann Tools > Last tweaked > Create automation clip.
FX-Regler heißen in FL nur „FX1 Wert 1“ usw., zeigen im Wert aber den Namen des geladenen Effekts.

## Destructor

Eigene Kette: Drive, Wavefold, Crush und danach ein eingebauter Phaser mit eigenem Mix, Tempo und Feedback.

## Updates

Ab dieser Version gibt es einen Installer. Lade einmal `Tekkforge-Setup.exe` aus den Releases von `brucknerjulian55-netizen/tekkforge` und starte ihn. Er legt das Plugin im VST3-Ordner ab.

Danach läuft jedes Update so: Das Plugin zeigt **Update installieren**, du klickst darauf und bestätigst. Es lädt den Installer, der FL Studio zum Schließen auffordert und das Plugin ersetzt. Projekte und Presets bleiben erhalten. Das Plugin prüft beim Öffnen nur die Versionsnummer bei GitHub und sendet keine Projektdaten.

Windows kann beim ersten Start warnen, weil der Installer nicht signiert ist. Dann auf „Weitere Informationen“ und „Trotzdem ausführen“ klicken.

So veröffentlichst du ein Update: In `CMakeLists.txt` die Versionsnummer erhöhen und die Änderungen hochladen. GitHub baut dann den Installer und legt das Release selbst an.

## Tests

Klangkern ohne JUCE prüfen:

    g++ -std=c++17 -O2 -ISource Tests/engine_test.cpp -o engine_test && ./engine_test

## Lizenz

JUCE 8 steht unter AGPLv3 oder einer kommerziellen Lizenz. Vor einer Weitergabe `LICENSE.md` von JUCE prüfen.
