// Tekkforge - Frequenz-Guide je Modus. Die Werte sind Faustregeln fuer Hardtekk, das Ohr entscheidet.
#pragma once
#include <vector>

namespace tk
{
struct Zone { const char* name; float lo, hi; const char* what; };
struct Guide { const char* job; std::vector<Zone> zones; std::vector<const char*> check; };

struct Recipe { const char* title; std::vector<const char*> steps; };

inline std::vector<Recipe> recipesFor (int mode)
{
    const Recipe trace { u8"Bass nachzeichnen",
        { u8"Sample auf eine Ebene ziehen und auf der Seite Sample die Ansicht Spektrogramm öffnen.",
          u8"Die unterste helle Linie ist der Grundton. Fahr mit der Maus darüber: Hz und Note stehen daneben.",
          u8"In dieser Ebene den Filter auf BP stellen, Cutoff auf die tiefste Note der Linie, Reso hoch.",
          u8"Unten bei LFO 1 die Form der Linie zeichnen. Stelle Takt und eine passende Länge ein.",
          u8"LFO 1 auf den Cutoff ziehen und mit dem kleinen Punkt am Regler die Stärke einstellen.",
          u8"Drive der Ebene für mehr Bass-Obertöne hochdrehen." } };
    const Recipe rhythm { u8"Rhythmischer Bass",
        { u8"Sample laden. In einer Ebene Spielart auf Notenlänge, Sustain auf 1 und Sub auf etwa 0,4 stellen.",
          u8"Der Sub spielt die Note aus dem Piano Roll. Notiere dort die Bassnoten in der Tonart des Tracks.",
          u8"LFO 1: Form Gate, Takt, Teiler 1/16, Start Frei.",
          u8"Reiter LFO 1 auf den Sub-Regler ziehen. Am kleinen Punkt die Stärke einstellen, nach unten für negativ.",
          u8"Andere Teiler ausprobieren. 1/8 groovt ruhiger, 1/16 treibt stärker." } };
    if (mode == KICK)
        return {
            { u8"Kick bauen",
              { u8"Topkick: Welle wählen, Länge 30 bis 80 ms, Pitch Menge hoch und Pitch Zeit um 15 ms. Lowcut um 240 Hz.",
                u8"Bass: Sinus, Pitch Menge 24 bis 48, Pitch Zeit 30 bis 60 ms. Das ist der Punch.",
                u8"Bass-Länge: Regler Länge stellen oder Spielart Notenlänge nutzen, dann bestimmt der Piano Roll die Länge.",
                u8"Drive der Bass-Ebene langsam hochdrehen, bis der Bass Biss bekommt. Tekk klingt am härtesten.",
                u8"Mit M und S einzelne Ebenen anhören. Topkick und Bass müssen getrennt gut klingen.",
                u8"Layer für zusätzlichen Körper nutzen, zum Beispiel ein Sample, Lowcut um 150 Hz." } },
            { u8"Sample zu Kick-Bass",
              { u8"Sample auf die Quelle der Ebene Bass ziehen. Sie schaltet auf Sample um.",
                u8"Links die Messwerte prüfen und „Vorschläge übernehmen“ drücken.",
                u8"Die empfohlene Note spielen. Klingt der Schwanz sauber, stimmt das Tuning.",
                u8"Länge stellen, bis der Schwanz vor der nächsten Kick endet.",
                u8"Sub und Drive nach Gehör nachregeln." } },
            rhythm,
            { u8"Sidechain",
              { u8"Bei der Ebene Bass auf FX klicken und in einem Slot Sidechain laden.",
                u8"Am einfachsten ist die Quelle Takt: Der Bass duckt sich auf jeder Viertel.",
                u8"Mit Bis Freq auf etwa 200 Hz wird nur der Tiefbass geduckt.",
                u8"Tiefe und Länge einstellen, bis Kick und Bass nicht mehr kollidieren.",
                u8"Liegt der Bass in einer eigenen Instanz: Quelle Link wählen und den Peak Controller der Kick in FL auf Pegel linken." } } };
    return {
        { mode == MELO ? u8"Sample zu Melo" : u8"Sample zu Stab",
          { u8"Ein tonales Sample auf die Quelle einer Ebene ziehen. Tekkforge misst den Grundton.",
            u8"„Vorschläge übernehmen“ drücken. Die Tonhöhe folgt danach der gespielten Note.",
            mode == MELO ? u8"Melodie im Piano Roll schreiben und auf die Tonart der Kick achten." : u8"Rechts unten den Akkord wählen, Moll passt oft gut zu Hardtekk.",
            u8"Lowcut so hoch setzen, dass der Kick-Bass frei bleibt." } },
        rhythm, trace };
}

inline const Guide& guideFor (int mode)
{
    static const Guide g[3] = {
        { u8"Die Hookline. Sie sitzt über der Kick und trägt die Melodie, ohne mit dem Bass zu kämpfen.",
          { { u8"Frei halten", 20, 150, u8"Hier gehört die Kick hin. Lowcut setzen." },
            { u8"Körper", 200, 1000, u8"Grundton der Melodie, macht den Sound voll." },
            { u8"Präsenz", 1500, 5000, u8"Hier hört man die Hookline im Mix." },
            { u8"Luft", 6000, 12000, u8"Glanz und Offenheit, sparsam dosieren." } },
          { u8"Lowcut etwa 120 bis 200 Hz, je nach Tonlage der Melodie.",
            u8"Tonart prüfen: Melodie und Kick-Bass müssen zusammenpassen.",
            u8"Länge an das Tempo anpassen. Bei 180 BPM dauert eine 1/8 nur rund 167 ms.",
            u8"Unison macht breit, aber unter 200 Hz sollte es mono bleiben.",
            u8"Harte Präsenz bei 2 bis 4 kHz nervt schnell. Im Zweifel leiser mischen." } },
        { u8"Kurze Akkordschläge für Druck und Spannung. Sie füllen die Mitten zwischen den Kicks.",
          { { u8"Frei halten", 20, 180, u8"Kein Platz für Stabs, Lowcut setzen." },
            { u8"Körper", 200, 800, u8"Akkord-Grundtöne, Wärme und Gewicht." },
            { u8"Biss", 1000, 4000, u8"Filter-Hüllkurve öffnet hier den Angriff." },
            { u8"Breite", 4000, 10000, u8"Unison und Chorus wirken hier am stärksten." } },
          { u8"Lowcut etwa 150 bis 250 Hz.",
            u8"Akkorde im Bereich unter 300 Hz matschen. Dann höher spielen oder Quinte nehmen.",
            u8"Kurze Hüllkurve: Stabs leben davon, dass sie schnell wieder Platz machen.",
            u8"Stabs zwischen die Kicks setzen, nicht direkt darauf.",
            u8"Stereo-Check: Klingt der Stab in Mono noch? Sonst Breite zurücknehmen." } },
        { u8"Drei Ebenen: Der Topkick bringt den Anschlag, der Bass trägt Sub und Schwanz, der Layer gibt Körper.",
          { { u8"Unter dem Sub", 20, 30, u8"Lowcut setzen, spart Headroom." },
            { u8"Sub", 45, 80, u8"Fundament aus der Ebene Bass, gestimmt und mono." },
            { u8"Punch", 80, 200, u8"Der Schlag, kommt aus dem Pitch-Sweep." },
            { u8"Mitten", 300, 2000, u8"Verzerrung, Körper und Tekk-Charakter." },
            { u8"Klick", 2000, 8000, u8"Der Attack aus dem Topkick, schneidet durch den Mix." } },
          { u8"Topkick: Lowcut mindestens 120 bis 240 Hz, sonst doppelt sich der Bass.",
            u8"Bass: Pitch Menge 24 bis 48 Halbtöne, Pitch Zeit 30 bis 60 ms.",
            u8"Tiefe Noten etwa zwischen 40 und 65 Hz nehmen, die Note steht oben rechts.",
            u8"Unter etwa 120 Hz mono bleiben. Unison und Breite aus.",
            u8"Länge ans Tempo binden: Bei 180 BPM liegen nur rund 333 ms zwischen zwei Kicks." } } };
    return g[mode];
}
} // namespace tk
