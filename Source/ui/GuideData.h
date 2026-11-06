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
        { u8"Sample laden und auf der Seite Sample die Ansicht Spektrogramm öffnen.",
          u8"Die unterste helle Linie ist der Grundton. Fahr mit der Maus darüber: Hz und Note stehen daneben.",
          u8"Auf der Seite FX einen Filter als BP laden. Cutoff auf die tiefste Note der Linie stellen, Reso hoch.",
          u8"Seite Auto: Ziel auf FX Filter Cutoff setzen, Start auf Loop Takt, Länge passend zur Phrase wählen.",
          u8"Die Linie aus dem Spektrogramm als Kurve nachzeichnen. Höher in der Kurve heißt höherer Cutoff.",
          u8"Mix und Pegel nach Gehör, danach einen Drive-Slot hinter den Filter für mehr Bass-Obertöne." } };
    const Recipe rhythm { u8"Rhythmischer Bass",
        { u8"Sample laden. Spielart auf Notenlänge und Sub-Pegel auf etwa 0,35 stellen.",
          u8"Der Sub spielt die Note aus dem Piano Roll. Notiere dort die Bassnoten in Tonart des Tracks.",
          u8"Auf der Synth-Seite LFO 1 auf Rechteck, Takt und 1/16 stellen, Neustart auf Frei.",
          u8"LFO 1 aus der Leiste auf den Sub-Pegel ziehen. Menge auf der Seite Mod auf etwa +0,35 stellen.",
          u8"Andere Teiler ausprobieren. 1/8 groovt ruhiger, 1/16 treibt stärker.",
          u8"Im Spektrum prüfen: Der Sub sollte in der Zone Sub liegen und nicht gegen die Kick arbeiten." } };
    switch (mode)
    {
        case KICKBASS: return {
            { u8"Sample zu Kick-Bass",
              { u8"Sample auf einen Oszillator ziehen. Er schaltet dann auf Sample um.",
                u8"Links die Messwerte prüfen und „Vorschläge übernehmen“ drücken.",
                u8"Die empfohlene Note spielen. Klingt der Schwanz sauber, stimmt das Tuning.",
                u8"Spielart Notenlänge nutzen oder den Regler Länge stellen, bis der Schwanz vor der nächsten Kick endet.",
                u8"Drive und Sub nach Gehör nachregeln. Mono unter etwa 120 Hz halten." } },
            rhythm, trace,
            { u8"Sidechain",
              { u8"Auf der FX-Seite in einem Slot den Effekt Sidechain laden. Er gehört in den Bass, nicht in die Kick.",
                u8"Am einfachsten ist die Quelle Takt: Der Bass duckt sich auf jeder Viertel.",
                u8"Mit Bis Freq auf etwa 200 Hz wird nur der Tiefbass geduckt, der Rest bleibt laut.",
                u8"Tiefe und Länge einstellen, bis Kick und Bass nicht mehr kollidieren.",
                u8"Für echte Kick-Muster: Quelle Link wählen und in FL den Peak Controller der Kick auf Pegel linken." } } };
        case FULLKICK: return {
            { u8"Sample zur Kick machen",
              { u8"Sample in Tekkforge ziehen und „Vorschläge übernehmen“ drücken.",
                u8"Die empfohlene Note spielen. Der Schwanz sollte zwischen 45 und 80 Hz liegen.",
                u8"Fehlt Druck, unter Sample die Tonhöhe auf Note + Kurve stellen und die Pitch-Hüllkurve nutzen oder den Sub anheben.",
                u8"Fehlt Attack, den Click erhöhen oder einen Topkick darüberlegen.",
                u8"Zum Schluss mit „Sound fertig rendern“ festschreiben." } },
            trace };
        case TOPKICK: return {
            { u8"Sample zu Topkick",
              { u8"Ein kurzes Sample mit klarem Anschlag laden, etwa Snare, Clap oder Kick.",
                u8"„Vorschläge übernehmen“ drücken. Der Lowcut entfernt den Bass, die Hüllkurve kürzt.",
                u8"Den Regler Länge zwischen 20 und 80 ms einstellen.",
                u8"Zusammen mit der Kick hören und die Phase prüfen.",
                u8"Mit Drive oder einem Clipper-Slot härter machen." } } };
        case MELO: return {
            { u8"Sample zu Melo",
              { u8"Ein tonales Sample laden. Tekkforge misst den Grundton.",
                u8"„Vorschläge übernehmen“ drücken. Die Tonhöhe folgt danach der gespielten Note.",
                u8"Melodie im Piano Roll schreiben und auf die Tonart der Kick achten.",
                u8"Lowcut so hoch setzen, dass der Kick-Bass frei bleibt." } },
            rhythm, trace };
        default: return {
            { u8"Sample zu Stab",
              { u8"Ein kurzes tonales Sample laden und „Vorschläge übernehmen“ drücken.",
                u8"Im Synth-Tab den Akkord wählen, Moll passt oft gut zu Hardtekk.",
                u8"Filter-Env und Länge kurz halten, damit der Stab Platz macht.",
                u8"Stabs zwischen die Kicks setzen." } },
            rhythm, trace };
    }
}

inline const Guide& guideFor (int mode)
{
    static const Guide g[5] = {
        { u8"Die Hookline. Sie sitzt über der Kick und trägt die Melodie, ohne mit dem Bass zu kämpfen.",
          { { u8"Frei halten", 20, 150, u8"Hier gehört die Kick hin. Lowcut setzen." },
            { u8"Körper", 200, 1000, u8"Grundton der Melodie, macht den Sound voll." },
            { u8"Präsenz", 1500, 5000, u8"Hier hört man die Hookline im Mix." },
            { u8"Luft", 6000, 12000, u8"Glanz und Offenheit, sparsam dosieren." } },
          { u8"Lowcut etwa 120 bis 200 Hz, je nach Tonlage der Melodie.",
            u8"Tonart prüfen: Melodie und Kick-Bass müssen zusammenpassen.",
            u8"Decay an das Tempo anpassen. Bei 180 BPM dauert eine 1/8 nur rund 167 ms.",
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
        { u8"Der Angriff, der sich durch alles schneidet. Er liegt auf der Kick und bringt den Klick.",
          { { u8"Frei halten", 20, 120, u8"Gehört dem Kick-Bass. Hier nichts lassen." },
            { u8"Punch", 150, 500, u8"Der Schlag in der Brust, darf nur kurz sein." },
            { u8"Klick", 1000, 5000, u8"Macht die Kick auch auf kleinen Boxen hörbar." },
            { u8"Spitze", 5000, 10000, u8"Harte Kante, wenig reicht." } },
          { u8"Lowcut mindestens 120 bis 180 Hz, sonst doppelt sich der Bass.",
            u8"Kurz halten: etwa 20 bis 80 ms reichen.",
            u8"Pitch-Sweep schnell wählen, etwa 10 bis 25 ms.",
            u8"Phase prüfen: Topkick mit Kick-Bass im Layer hören und Wirkung vergleichen.",
            u8"Drive vor dem Lowcut denken: Verzerrung baut neue Obertöne auf." } },
        { u8"Kick und Bass in einem Sound. Der Schwanz ist die Basslinie, also muss er sauber gestimmt sein.",
          { { u8"Unter dem Sub", 20, 35, u8"Kostet nur Headroom. Lowcut setzen." },
            { u8"Sub", 40, 80, u8"Das Fundament. Sauber, mono und gestimmt." },
            { u8"Bass-Obertöne", 100, 300, u8"Macht den Bass auf kleinen Boxen hörbar." },
            { u8"Knurren", 300, 1000, u8"Tekk-Charakter aus der Verzerrung." } },
          { u8"Tiefe Noten etwa zwischen 40 und 65 Hz nehmen, die Note steht oben rechts.",
            u8"Unter etwa 120 Hz mono bleiben. Unison und Breite aus.",
            u8"Länge ans Tempo binden: Der Schwanz muss vor der nächsten Kick enden.",
            u8"Gate ist hier an: Die Notenlänge im Piano Roll bestimmt die Länge.",
            u8"Tuning mit einem Tuner oder dem Analyzer prüfen, nicht nur nach Gehör." } },
        { u8"Die komplette Kick als ein Sound: Klick, Punch und Schwanz. Das Zentrum des Tracks.",
          { { u8"Unter dem Sub", 20, 30, u8"Lowcut setzen, spart Headroom." },
            { u8"Sub", 45, 80, u8"Fundament, passend zur Tonart gestimmt." },
            { u8"Punch", 80, 200, u8"Der Schlag, kommt aus dem Pitch-Sweep." },
            { u8"Mitten", 300, 2000, u8"Verzerrung, Körper und Tekk-Charakter." },
            { u8"Klick", 2000, 8000, u8"Der Attack, schneidet durch den Mix." } },
          { u8"Lowcut etwa bei 25 bis 35 Hz.",
            u8"Pitch-Hüllkurve zuerst bauen: Menge 36 bis 60 Halbtöne, Zeit 30 bis 90 ms.",
            u8"Kick mono halten, besonders unter 120 Hz.",
            u8"Pegelspitzen im Blick behalten: Rote Anzeige heißt Drive oder Pegel senken.",
            u8"Länge ans Tempo binden: Bei 180 BPM liegen nur rund 333 ms zwischen zwei Kicks." } } };
    return g[mode];
}
} // namespace tk
