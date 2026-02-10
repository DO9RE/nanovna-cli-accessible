# Smith Diagramm Modi - Detaillierte Beschreibung der räumlichen Relationen

**Datum:** 2026-02-09  
**Version:** 1.0  
**Zielgruppe:** Blinde und hochgradig sehbehinderte Benutzer mit 3D-Audio-Headsets

---

## Überblick

Dieses Dokument beschreibt die sechs verfügbaren Modi zur akustischen Visualisierung von Smith Diagrammen. Jeder Modus nutzt räumliches Audio (3D-Sound), um die Position von Messpunkten im Smith Diagramm hörbar zu machen.

**Wichtige Konzepte:**
- **Listener (Hörer)**: Sie befinden sich im Zentrum des Smith Diagramms (perfekte Anpassung, 50Ω, SWR=1.0)
- **Events (Ereignisse)**: Audio-Signale, die im Raum um Sie herum positioniert werden
- **Spatial Audio (Räumliches Audio)**: Nutzt Stereo oder Surround-Sound (5.1/7.1), um Position und Bewegung zu vermitteln

---

## Modus 1: CARTESIAN (Kartesisch)

### Konzept
Das Smith Diagramm wird als kartesisches Koordinatensystem interpretiert. Sie stehen im Zentrum (Nullpunkt), und der Messpunkt bewegt sich im Raum um Sie herum.

### Spatial Mapping (Räumliche Zuordnung)

#### **Horizontal (Links-Rechts-Achse):**
- Steuert: **Re(Γ)** (Realteil des Reflexionskoeffizienten)
- Wertebereich: -1.0 (ganz links) bis +1.0 (ganz rechts)
- Nullpunkt: Direkt vor/hinter Ihnen (mittig)
- **Audio-Position:**
  - Re(Γ) = -1.0 → Sound kommt von ganz links
  - Re(Γ) = 0.0 → Sound kommt von vorne/hinten (zentriert)
  - Re(Γ) = +1.0 → Sound kommt von ganz rechts

#### **Vertikal (Vorne-Hinten-Achse):**
- Steuert: **Im(Γ)** (Imaginärteil des Reflexionskoeffizienten)
- Wertebereich: -1.0 (hinten) bis +1.0 (vorne)
- Nullpunkt: Direkt links/rechts von Ihnen
- **Audio-Position:**
  - Im(Γ) = +1.0 → Sound kommt von vorne
  - Im(Γ) = 0.0 → Sound kommt von der Seite (90° links oder rechts)
  - Im(Γ) = -1.0 → Sound kommt von hinten

#### **Listener-Event-Relation:**
Sie stehen im Zentrum eines Kreises (dem Smith Diagramm). Wenn Sie sich vorstellen, dass Sie nach Norden schauen:
- Norden (vorne) = Im(Γ) = +1.0 (induktive Impedanz)
- Osten (rechts) = Re(Γ) = +1.0 (hoher Widerstand)
- Süden (hinten) = Im(Γ) = -1.0 (kapazitive Impedanz)
- Westen (links) = Re(Γ) = -1.0 (niedriger Widerstand)

### Achsenkreuzungen (Axis Crossing Events)
Aus der akustischen Perspektive des Zuhörers im Zentrum des Smith Raums:

**Horizontal (Links-Rechts Achse - Re(Γ) = 0):**
- Akustische Wahrnehmung: Überquerung von links nach rechts (oder umgekehrt)
- Bedeutung (Smith Diagramm): Normalisierter Widerstand = 1 (R = R₀ = 50Ω)
- Klang: Pitched Sweep (aufwärts = nach rechts, abwärts = nach links)
- Sound-Position: Zentrum (links-rechts Achse), variiert vorne/hinten je nach Im(Γ)

**Vertikal (Vorne-Hinten Achse - Im(Γ) = 0):**
- Akustische Wahrnehmung: Überquerung von hinten nach vorne (oder umgekehrt)
- Bedeutung (Smith Diagramm): Impedanz ist rein resistiv (keine Reaktanz)
- Klang: Pitched Sweep (aufwärts = nach vorne, abwärts = nach hinten)
- Sound-Position: Zentrum (vorne-hinten Achse), variiert links/rechts je nach Re(Γ)

**Hinweis:** Die akustische Achsenzuordnung (Re→X/Links-Rechts, Im→Y/Vorne-Hinten) unterscheidet sich von der traditionellen Smith Diagramm Terminologie, ist aber intuitiver für die räumliche Navigation.

### Anwendungsfall
- Ideal für: Anfänger, intuitive Exploration
- Gut für: Verstehen der grundlegenden Smith-Geometrie
- Vorteil: Entspricht räumlichen Denkmustern wie taktile Geometrie

---

## Modus 2: POLAR (Polar / Umkreisend)

### Konzept
Nutzt die natürliche polare Struktur des Smith Diagramms. Sie stehen im Zentrum, und der Ton **kreist um Sie herum**. Der Winkel des Reflexionskoeffizienten bestimmt die Richtung, der Betrag die Entfernung/Lautstärke.

### Spatial Mapping (Räumliche Zuordnung)

#### **Winkel (Rotation um Sie herum):**
- Steuert: **∠Γ** (Phase des Reflexionskoeffizienten)
- Wertebereich: -180° bis +180° (voller Kreis)
- **Audio-Position:**
  - ∠Γ = 0° → Sound direkt vor Ihnen
  - ∠Γ = +90° → Sound rechts von Ihnen
  - ∠Γ = ±180° → Sound direkt hinter Ihnen
  - ∠Γ = -90° → Sound links von Ihnen

#### **Radius (Entfernung zum Zentrum):**
- Steuert: **|Γ|** (Betrag des Reflexionskoeffizienten)
- Wertebereich: 0.0 (Zentrum) bis 1.0 (Rand)
- **Audio-Eigenschaften:**
  - |Γ| = 0.0 → Leise, zentral (perfekte Anpassung)
  - |Γ| zunehmend → Lauter, "entfernter" (schlechtere Anpassung)
  - |Γ| → 1.0 → Sehr laut, am Rand (sehr schlechte Anpassung)

#### **Listener-Event-Relation:**
Sie stehen im absoluten Zentrum eines Kreises. Der Ton bewegt sich **um Sie herum im Kreis**:
- **Uhrzeigersinn-Rotation** (0° → +90° → ±180° → -90° → 0°):
  - Typisch bei zunehmender Frequenz über ein festes Bauteil
  - Häufig bei Kabeln (λ/4-Transformation)
- **Gegenuhrzeigersinn-Rotation**:
  - Seltener, spezielle Impedanz-Charakteristiken

### Achsenkreuzungen (Cardinal Direction Events)
In diesem Modus sind Achsenkreuzungen anders definiert:

**Kardinale Richtungen (0°, 90°, 180°, -90°):**
- Bedeutung: Hauptrichtungen im Smith Diagramm
- 0° (vorne): Z > Z₀, rein resistiv (hoher Widerstand)
- +90° (rechts): Induktiv (positive Reaktanz)
- ±180° (hinten): Z < Z₀, rein resistiv (niedriger Widerstand)
- -90° (links): Kapazitiv (negative Reaktanz)
- Klang: Pitched Sweep beim Überqueren dieser Richtungen

### Bewegungsmuster
**Radiale Bewegung (zum Zentrum):**
- Sound wird leiser und zieht sich zur Mitte
- Bedeutung: Resonanz! Impedanz nähert sich Z₀

**Kreisförmige Bewegung:**
- Sound kreist gleichmäßig um Sie
- Bedeutung: Frequenz-Sweep über reaktives Bauteil

### Anwendungsfall
- Ideal für: Fortgeschrittene, Kabel-Analysen
- Gut für: Erkennen von Rotationsmustern (λ/4-Transformationen)
- Vorteil: Natürlich für polares Smith-Diagramm, starke psychoakustische Wahrnehmung

---

## Modus 3: IMPEDANCE_DIRECT (Direkte Impedanz)

### Konzept
Bildet Impedanzwerte (R und X) direkt auf räumliche Position ab, ohne Umweg über Γ. Einfacher für Benutzer, die direkt in Ohm denken.

### Spatial Mapping (Räumliche Zuordnung)

#### **Horizontal (Links-Rechts-Achse):**
- Steuert: **R** (Widerstand in Ω)
- Normalisiert: 0Ω-200Ω → -1.0 bis +1.0, zentriert bei 50Ω
- **Audio-Position:**
  - R = 0Ω → Ganz links
  - R = 50Ω → Mittig (Z₀)
  - R = 200Ω → Ganz rechts

#### **Vertikal (Vorne-Hinten-Achse):**
- Steuert: **X** (Reaktanz in Ω)
- Wertebereich: -200Ω bis +200Ω → -1.0 bis +1.0
- **Audio-Position:**
  - X = +200Ω → Weit vorne (stark induktiv)
  - X = 0Ω → Mittig (rein resistiv)
  - X = -200Ω → Weit hinten (stark kapazitiv)

#### **Listener-Event-Relation:**
Sie stehen im Zentrum bei R=50Ω, X=0Ω (perfekte Anpassung):
- Nach vorne/hinten: Reaktanz ändert sich (Spule vs. Kondensator)
- Nach links/rechts: Widerstand ändert sich (niedrig vs. hoch)

### Achsenkreuzungen
**Hinweis:** Die Achsenkreuzungs-Detektion basiert auf den Smith-Parametern Re(Γ) und Im(Γ), nicht direkt auf R und X:

**Horizontal (Links-Rechts - Re(Γ) = 0):**
- Akustische Position: Durchquerung der Links-Rechts Achse
- Entspricht ungefähr: R ≈ 50Ω (Widerstand = Systemimpedanz)
- Position: Bewegt sich vorne-hinten je nach X

**Vertikal (Vorne-Hinten - Im(Γ) = 0):**
- Akustische Position: Durchquerung der Vorne-Hinten Achse  
- Bedeutung: Rein resistive Impedanz (X = 0)
- Position: Bewegt sich links-rechts je nach R

### Anwendungsfall
- Ideal für: Anfänger ohne Smith-Diagramm-Erfahrung
- Gut für: Direktes Verstehen von Impedanzwerten
- Vorteil: Einfache Interpretation, keine Gamma-Konversion nötig

---

## Modus 4: SWR_CIRCLES (SWR-Kreise)

### Konzept
Fokussiert auf konstante SWR-Kreise im Smith Diagramm. Nützlich für Antennenabstimmung, wo das Ziel ist, SWR zu minimieren.

### Spatial Mapping (Räumliche Zuordnung)

#### **Radiale Position:**
- Steuert: **SWR-Wert**
- Zentrum = SWR 1.0 (perfekt)
- Nach außen = Höheres SWR (schlechter)
- **Audio-Eigenschaften:**
  - SWR ≈ 1.0 → Leise, zentral
  - SWR 1.5-2.0 → Mäßig laut
  - SWR > 3.0 → Sehr laut, "am Rand"

#### **Listener-Event-Relation:**
Sie stehen im Zentrum (SWR=1.0). Der Sound "entfernt" sich von Ihnen, je schlechter das SWR wird:
- Nah bei Ihnen = Gut abgestimmt
- Weit weg = Schlecht abgestimmt

### Schwellenwert-Events
**SWR-Schwellen (z.B. 1.5, 2.0, 3.0):**
- Klang: Markerton beim Überschreiten
- Bedeutung: Warnung bei schlechter werdender Anpassung

### Anwendungsfall
- Ideal für: Antennen-Tuning
- Gut für: Schnelles Finden von Resonanzpunkten
- Vorteil: Direkte Rückmeldung zur Anpassungsqualität

---

## Modus 5: TIME_DOMAIN_CUES (Zeitdomäne mit Smith-Cues)

### Konzept
Erweitert den Standard-Acoustic-Mode (Frequenz-Sweep) um subtile räumliche Smith-Cues im Hintergrund.

### Spatial Mapping (Räumliche Zuordnung)

#### **Primäre Achse (Links-Rechts):**
- Steuert: **Frequenz** (wie im Acoustic-Mode)
- Fortschreiten von links nach rechts über Frequenzband

#### **Sekundäre Cues:**
- **Vorne-Hinten Balance:** Subtile Modulation basierend auf Im(Γ)
- **Klangfarbe:** Ändert sich mit Re(Γ)

#### **Listener-Event-Relation:**
Sie "schauen" über das Frequenzband von links nach rechts. Gleichzeitig hören Sie subtile räumliche Hinweise:
- Tonhöhe = Hauptkurven-Wert (SWR, RL, etc.)
- Räumliche Position = Smith-Position als Kontext

### Achsenkreuzungen
Wie Modus 1 (Kartesisch), aber weniger prominent, da Frequenz die Hauptachse ist.

### Anwendungsfall
- Ideal für: Benutzer des Acoustic-Mode, die Smith-Kontext wünschen
- Gut für: Gleichzeitige Frequenz- und Smith-Analyse
- Vorteil: Rückwärtskompatibel, optional aktivierbar

---

## Modus 6: HYBRID_MULTI (Hybrid Multi-Layer)

### Konzept
Kombiniert mehrere Ansätze gleichzeitig. Mehrere Audio-Streams laufen parallel und vermitteln verschiedene Informationen.

### Spatial Mapping (Räumliche Zuordnung)

#### **Layer 1 (Hauptsignal):**
- Wählbar: Kartesisch ODER Polar
- Volle räumliche Position

#### **Layer 2 (Kontext-Cues):**
- Subtile Hintergrund-Sounds für andere Parameter
- Z.B. SWR als Lautstärke-Modulation

#### **Layer 3 (Marker):**
- Ereignis-Sounds bei wichtigen Punkten
- Z.B. Resonanz, Achsenkreuzungen

#### **Listener-Event-Relation:**
Sie befinden sich in einem "mehrschichtigen" Audioraum:
- Vordergrund: Hauptposition (Polar oder Kartesisch)
- Mittelgrund: Kontext-Informationen
- Hintergrund: Marker und Warnungen

### Anwendungsfall
- Ideal für: Experten mit räumlichem Vorstellungsvermögen
- Gut für: Maximale Informationsdichte
- Vorteil: Flexible Kombination aller Strategien

---

## Zusammenfassung der Listener-Event-Relationen

### Zentrale Position (Sie als Listener)
**In allen Modi stehen Sie:**
- Im Zentrum des Smith Diagramms
- An der perfekten Anpassung (50Ω, SWR=1.0, Γ=0)
- Im "optimalen Punkt"

### Event-Positionierung
**Messpunkte (Events) bewegen sich:**
- **Kartesisch:** Rechteckig um Sie herum (X/Y-Koordinaten)
- **Polar:** Kreisend um Sie herum (Winkel und Radius)
- **Impedance:** Relativ zu Ihrer Position (R/X-Werte)
- **SWR:** Radial von Ihnen weg (je schlechter das SWR)

### 3D-Audio-Technologie
**Stereo (2.0):**
- Nur Links-Rechts-Trennung
- Vorne-Hinten durch Klangfarbe simuliert

**Surround 5.1:**
- Volle 360°-Abdeckung
- Front, Side, Back Lautsprecher

**Surround 7.1:**
- Optimale 360°-Abdeckung
- Zusätzliche Seitenlautsprecher für präzise 90°-Ortung

---

## Tipps für blinde Benutzer

### Räumliche Orientierung aufbauen
1. **Test-Töne nutzen:**
   - Starten Sie mit Center Pulse aktiviert
   - Hören Sie das Referenzsignal aus der Mitte

2. **Modi schrittweise lernen:**
   - Beginnen Sie mit Modus 1 (Kartesisch) - am intuitivsten
   - Dann Modus 3 (Impedance Direct) - einfache Interpretation
   - Fortgeschrittene: Modus 2 (Polar) - volle Smith-Struktur

3. **Achsenkreuzungen nutzen:**
   - Aktivieren Sie Axis Events für akustische Landmarken
   - Diese helfen, sich im Smith-Raum zu orientieren

4. **Surround-Setup optimal nutzen:**
   - Tragen Sie das Headset mittig
   - Kalibrieren Sie Lautstärken in den Surround-Einstellungen

### Verständnis vertiefen
- **Smith-Diagramm-Analogien:**
  - Kartesisch = Stadtplan mit Straßenraster
  - Polar = Kompass mit Richtungen und Entfernungen
  - Impedance = Höhenkarte mit Höhenlinien

- **Acoustic Events als Landmarken:**
  - Achsenkreuzungen = Straßenkreuzungen
  - Center Pulse = Ihr Standort (GPS-Signal)
  - SWR-Schwellen = Zonengrenzen

---

## Technische Details

### Audio-Engine
- Sample Rate: 44100 Hz
- Stereo/Surround: Dynamisch basierend auf Hardware
- 3D-Panning: VBAP (Vector Base Amplitude Panning)

### Konfigurierbare Parameter
- **Center Pulse:** Waveform, Volume, Interval
- **Axis Events:** Sound Type, Volume, Pitch Range
- **Ambient Cues:** Noise Type, Volume
- **Surround:** Front/Back/Side Distance, Fading Curve

---

## Weitere Ressourcen

- **Hilfe im Programm:** Drücken Sie `?` im Smith-Konfigurations-Menü
- **Tutorials:** Siehe `ideas/smith_diagram_visualization/06_BENUTZERHILFE.md`
- **Technische Details:** Siehe `ideas/smith_diagram_visualization/00_DATENANALYSE.md`

---

**73 DE DO9RE** 🎙️📡
