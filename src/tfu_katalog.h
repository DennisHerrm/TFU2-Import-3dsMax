// ============================================================
//  TFU2 Import - Katalog: was steht im Spiel zur Auswahl
//
//  Figuren:  jede .gto unter .../Characters/... ausser den LOD-Stufen
//            (_LOD1 usw.). Die zugehoerige *.actor.xml (falls eine
//            auf das Modell zeigt) liefert die Materialzuordnung.
//  Animationen: jede .animations-Datei. Der Rig-Typ steht im Pfad
//            (Animation/ingame/characters/<rig>/...) und passt zum
//            Ordner der Figur (Characters/<rig>/<figur>/...).
//
//  Materialien: actor.xml -> ActorMaterialMapping (mGtoMaterial ->
//  mDefMaterial). Die .material nennt ihre Texturen ueber
//  <property name="colorMap" type="texture" default="pfad.dds">.
// ============================================================
#pragma once

#include "tfu_pak.h"

#include <map>
#include <string>
#include <vector>

namespace tfu {

struct FigurEintrag {
    std::string gto;           // Pfad im Archiv
    std::string name;          // Dateiname ohne Endung
    std::string ordner;        // Ordner der Figur, z.B. Game/Disc/Characters/maleAverage/playerStarkillerMcQuarrie
    std::string rig;           // z.B. maleAverage
    std::string actor;         // *.actor.xml, die das Modell benutzt (kann leer sein)
    bool skelett = false;      // die GTO hat ein Skeleton-Objekt (sonst ein starres Teil)
    std::vector<std::string> actors;   // alle *.actor.xml, die auf das Modell zeigen
};

struct AnimEintrag {
    std::string pfad;
    std::string name;          // Dateiname ohne Endung
    std::string gruppe;        // Ordner unter Animation/, z.B. ingame/characters/maleAverage/clips
    mutable int bilder = 0;    // Bildzahl (30 fps), bekannt nach Katalog::AnimCrcs
};

struct TexturSatz {
    std::string farbe, normal, glanz;   // Pfade im Archiv
    bool alpha = false;                 // Alpha-Blend oder Alpha-Test im Material
    std::string materialDatei;
};

class Katalog {
public:
    bool Baue(const Pakete& p, std::string& fehler);
    std::vector<FigurEintrag> figuren;
    std::vector<AnimEintrag> animationen;

    // Knochen-CRCs je Animation (nur die Spurtabellen, beim ersten Aufruf
    // einmal fuer alle gelesen). Grundlage fuer "passt zum Skelett".
    const std::vector<std::vector<uint32_t>>& AnimCrcs(const Pakete& p) const;

    // Die Animationen, die das Spiel dieser Figur zuordnet (Indizes in
    // animationen, ohne Skelettpruefung):
    //  - AnimIDs aus den Chore-Sets ihrer Actors (zed_components_chore_resource,
    //    Moveset-Resourcen, auch ueber mKeyBaseActor) und aus dem ChoreData-Ordner
    //    mit ihrem Namen (DarthVader, Juno, ewok, Player ...);
    //  - Zwischensequenz-Clips, deren Name auf die Figur endet (..._darthVader).
    std::vector<size_t> EigeneAnimationen(const Pakete& p, const FigurEintrag& f, std::vector<std::string>* quellen = nullptr) const;

private:
    mutable std::vector<std::vector<uint32_t>> animCrcs_;
};

// Knochen-CRCs eines Modells (fuer den Filter, ohne Meshes zu bauen).
std::vector<uint32_t> ModellCrcs(const Pakete& p, const std::string& gto);

// Liegt ein Clip im Ordner des Rigs oder der Figur? (".../characters/maleAverage/...",
// ".../ATST/..."). Die Knochennamen sind bei allen Menschen gleich - erst der
// Ordner trennt maleAverage von maleBrute.
bool GleichesRig(const FigurEintrag& f, const AnimEintrag& a);

// Passt ein Clip zu einem Skelett? Mindestens die Haelfte seiner Spuren
// muss im Skelett vorkommen (und wenigstens vier, wo es so viele gibt).
bool PasstZu(const std::vector<uint32_t>& clip, const std::vector<uint32_t>& skelettSortiert);

// Rig-Ordner aus einem Pfad (das Glied nach "characters/").
std::string RigAus(const std::string& pfad);

// GTO-Materialname -> Texturen, ueber actor.xml (und Rueckfall: gleichnamige
// .material im Ordner der Figur).
std::map<std::string, TexturSatz> LoeseMaterialien(const Pakete& p, const FigurEintrag& f,
                                                   const std::vector<std::string>& gtoMaterialien,
                                                   std::vector<std::string>* protokoll = nullptr);

// Textur aus dem Archiv in den Zwischenspeicher legen. Normalmaps im
// DXT5nm-Format (X in Alpha, Y in Gruen) werden in eine gewoehnliche
// RGB-Normalmap (PNG) umgerechnet. Liefert den Pfad auf der Platte.
std::wstring TexturAufPlatte(const Pakete& p, const std::string& archivPfad, bool istNormal,
                             const std::wstring& cacheOrdner, std::string& fehler);

// Einfache XML-Hilfe: alle Werte zwischen <tag> und </tag>.
std::vector<std::string> XmlWerte(const std::string& text, const std::string& tag);

} // namespace tfu
