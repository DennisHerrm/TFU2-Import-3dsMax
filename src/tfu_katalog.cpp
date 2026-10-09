#include "tfu_katalog.h"
#include "tfu_gto.h"

#include "miniz.h"
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

#include <algorithm>
#include <set>
#include <cmath>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace tfu {

namespace {

bool Enthaelt(const std::string& klein, const char* teil) { return klein.find(teil) != std::string::npos; }

std::string Trimme(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    return s.substr(a);
}

std::string Ordner(const std::string& pfad) {
    const size_t p = pfad.find_last_of('/');
    return p == std::string::npos ? std::string() : pfad.substr(0, p);
}

std::string Attribut(const std::string& tag, const char* name) {
    const std::string such = std::string(name) + "=\"";
    const size_t p = tag.find(such);
    if (p == std::string::npos) return std::string();
    const size_t a = p + such.size();
    const size_t e = tag.find('"', a);
    return e == std::string::npos ? std::string() : tag.substr(a, e - a);
}

bool IstLod(const std::string& kleinName) {
    // "..._lod1.gto", "..._lod2.gto"
    const size_t p = kleinName.rfind("_lod");
    return p != std::string::npos && p + 4 < kleinName.size() && kleinName[p + 4] >= '0' && kleinName[p + 4] <= '9';
}

// Texturen aus einem <materialDefinition>-Text (.material oder in der GTO eingebettet).
// TFU1 nennt sie "Diffuse"/"BaseTexture", "Normal"/"NormalMap", "SpecularandAO".
TexturSatz LiesMaterialText(const Pakete& p, const std::string& text) {
    TexturSatz t;
    size_t pos = 0;
    while ((pos = text.find("<property", pos)) != std::string::npos) {
        const size_t e = text.find('>', pos);
        if (e == std::string::npos) break;
        const std::string tag = text.substr(pos, e - pos);
        pos = e;
        const std::string name = Klein(Attribut(tag, "name"));
        const std::string typ = Attribut(tag, "type");
        const std::string wert = Attribut(tag, "default");
        if (typ == "texture" && !wert.empty() && p.Hat(wert)) {
            if (t.farbe.empty() && (Enthaelt(name, "color") || Enthaelt(name, "diffuse") || (p.Spiel() == 1 && name == "basetexture"))) t.farbe = wert;
            else if (t.normal.empty() && Enthaelt(name, "normal")) t.normal = wert;
            else if (t.glanz.empty() && Enthaelt(name, "spec")) t.glanz = wert;
        } else if ((name == "alpha" || name == "alphatest") && typ == "bool" &&
                   (Klein(wert) == "true" || (p.Spiel() == 1 && wert == "1"))) {
            t.alpha = true;
        }
    }
    return t;
}

TexturSatz LiesMaterial(const Pakete& p, const std::string& pfad) {
    std::vector<uint8_t> roh;
    std::string f;
    if (!p.Lies(pfad, roh, f)) { TexturSatz t; t.materialDatei = pfad; return t; }
    TexturSatz t = LiesMaterialText(p, std::string(roh.begin(), roh.end()));
    t.materialDatei = pfad;
    return t;
}

} // namespace

std::vector<std::string> XmlWerte(const std::string& text, const std::string& tag) {
    std::vector<std::string> aus;
    const std::string auf = "<" + tag + ">", zu = "</" + tag + ">";
    size_t p = 0;
    while ((p = text.find(auf, p)) != std::string::npos) {
        const size_t a = p + auf.size();
        const size_t e = text.find(zu, a);
        if (e == std::string::npos) break;
        aus.push_back(Trimme(text.substr(a, e - a)));
        p = e + zu.size();
    }
    return aus;
}

std::string RigAus(const std::string& pfad) {
    const std::string k = Klein(pfad);
    const size_t p = k.find("characters/");
    if (p == std::string::npos) return std::string();
    size_t a = p + 11;
    // TFU1: Scum/characters/PCDX/maleAverage/rigs/player/... - Plattformordner ueberspringen
    if (k.compare(a, 5, "pcdx/") == 0) a += 5;
    const size_t e = pfad.find('/', a);
    return e == std::string::npos ? std::string() : pfad.substr(a, e - a);
}

bool Katalog::Baue(const Pakete& p, std::string& fehler) {
    figuren.clear();
    animationen.clear();
    std::map<std::string, size_t> nachGto;     // Schluessel -> Index in figuren
    std::vector<const PakEintrag*> actors;
    for (const PakEintrag& e : p.Eintraege()) {
        const std::string k = Klein(e.name);
        if (EndetMit(k, ".gto")) {
            if (!Enthaelt(k, "/characters/") || IstLod(Blatt(k))) continue;
            FigurEintrag f;
            f.gto = e.name;
            f.name = OhneEndung(Blatt(e.name));
            f.ordner = Ordner(e.name);
            f.rig = RigAus(e.name);
            f.spiel = p.Spiel();
            nachGto.emplace(Pakete::Schluessel(e.name), figuren.size());
            figuren.push_back(f);
        } else if (EndetMit(k, ".animations")) {
            AnimEintrag a;
            a.pfad = e.name;
            a.name = OhneEndung(Blatt(e.name));
            const size_t pa = k.find("/animation/");
            a.gruppe = Ordner(pa == std::string::npos ? e.name : e.name.substr(pa + 11));
            animationen.push_back(a);
        } else if (EndetMit(k, ".actor.xml")) {
            actors.push_back(&e);
        }
    }
    // Hat das Modell ein Skelett? Die Stringtabelle steht am Anfang der GTO; dort
    // steht das Protokoll "Skeleton", wenn es eines gibt. Dafuer reichen die
    // ersten Kilobytes - ganz entpackt wird nichts.
    for (FigurEintrag& fi : figuren) {
        const PakEintrag* e = p.Finde(fi.gto);
        std::vector<uint8_t> anfang, klar;
        if (e == nullptr || !p.LiesAnfang(*e, 24 * 1024, anfang) || !EntpackeGzipAnfang(anfang, 64 * 1024, klar)) continue;
        static const char such[] = "\0Skeleton\0";
        fi.skelett = std::search(klar.begin(), klar.end(), such, such + sizeof such - 1) != klar.end();
    }
    // Welche actor.xml zeigt auf welches Modell? Bevorzugt eine mit Materialzuordnung.
    std::vector<uint8_t> roh;
    std::string f;
    std::map<std::string, bool> mitZuordnung;
    for (const PakEintrag* e : actors) {
        if (!p.Lies(*e, roh, f)) continue;
        const std::string text(roh.begin(), roh.end());
        const std::vector<std::string> m = XmlWerte(text, "mkeyGTOModel");
        if (m.empty() || m[0].empty()) continue;
        const auto it = nachGto.find(Pakete::Schluessel(m[0]));
        if (it == nachGto.end()) continue;
        const bool zuordnung = text.find("<ActorMaterialMapping>") != std::string::npos;
        FigurEintrag& fi = figuren[it->second];
        fi.actors.push_back(e->name);
        if (fi.actor.empty() || (zuordnung && !mitZuordnung[fi.gto])) {
            fi.actor = e->name;
            mitZuordnung[fi.gto] = zuordnung;
        }
    }
    auto kleiner = [](const std::string& a, const std::string& b) { return Klein(a) < Klein(b); };
    std::sort(figuren.begin(), figuren.end(), [&](const FigurEintrag& a, const FigurEintrag& b) {
        if (Klein(a.rig) != Klein(b.rig)) return kleiner(a.rig, b.rig);
        return kleiner(a.name, b.name);
    });
    std::sort(animationen.begin(), animationen.end(), [&](const AnimEintrag& a, const AnimEintrag& b) {
        if (Klein(a.gruppe) != Klein(b.gruppe)) return kleiner(a.gruppe, b.gruppe);
        return kleiner(a.name, b.name);
    });
    if (figuren.empty()) { fehler = "no character models found in the game data"; return false; }
    return true;
}

const std::vector<std::vector<uint32_t>>& Katalog::AnimCrcs(const Pakete& p) const {
    if (animCrcs_.size() == animationen.size()) return animCrcs_;
    animCrcs_.assign(animationen.size(), {});
    std::vector<uint8_t> d;
    std::string f;
    for (size_t i = 0; i < animationen.size(); ++i) {
        if (!p.Lies(animationen[i].pfad, d, f)) continue;
        size_t mi = std::string::npos;
        for (size_t k = 0; k + 4 <= d.size() && k < 4096; ++k)
            if (d[k] == 'm' && d[k + 1] == 'i' && d[k + 2] == 'n' && d[k + 3] == 'a') { mi = k; break; }
        if (mi == std::string::npos || mi + 0x40 > d.size()) continue;
        uint32_t n, tab;
        float dauer;
        std::memcpy(&n, &d[mi + 0x14], 4);
        std::memcpy(&dauer, &d[mi + 0x18], 4);
        std::memcpy(&tab, &d[mi + 0x30], 4);
        animationen[i].bilder = (dauer > 0.0f && dauer < 1e5f) ? static_cast<int>(std::lround(dauer * 30.0f)) + 1 : 0;
        const size_t t = mi + 0x40 + tab;
        if (n > 4096 || t + static_cast<size_t>(n) * 16 > d.size()) continue;
        std::vector<uint32_t>& z = animCrcs_[i];
        z.resize(n);
        for (uint32_t k = 0; k < n; ++k) std::memcpy(&z[k], &d[t + static_cast<size_t>(k) * 16], 4);
    }
    return animCrcs_;
}

std::vector<uint32_t> ModellCrcs(const Pakete& p, const std::string& gto) {
    std::vector<uint32_t> aus;
    std::vector<uint8_t> roh;
    std::string f;
    GtoDatei g;
    if (!p.Lies(gto, roh, f) || !g.Lies(roh, f)) return aus;
    for (const GtoObjekt& o : g.objekte) {
        if (o.protokoll != "Skeleton") continue;
        const GtoKomponente* k = o.Finde("Skeleton");
        const GtoEigenschaft* e = k ? k->Finde("BoneCRCs") : nullptr;
        if (e == nullptr) continue;
        for (size_t i = 0; i < e->Werte(); ++i) aus.push_back(static_cast<uint32_t>(g.Int(*e, i)));
    }
    std::sort(aus.begin(), aus.end());
    return aus;
}

namespace {

std::string OhneKommentare(std::string t) {
    size_t p = 0;
    while ((p = t.find("<!--", p)) != std::string::npos) {
        const size_t e = t.find("-->", p + 4);
        t.erase(p, (e == std::string::npos ? t.size() : e + 3) - p);
    }
    return t;
}

// Alle Werte eines Attributs (name="...") in einem Text.
std::vector<std::string> AttributWerte(const std::string& text, const std::string& name) {
    std::vector<std::string> aus;
    const std::string such = name + "=\"";
    size_t p = 0;
    while ((p = text.find(such, p)) != std::string::npos) {
        const size_t a = p + such.size();
        const size_t e = text.find('"', a);
        if (e == std::string::npos) break;
        aus.push_back(text.substr(a, e - a));
        p = e + 1;
    }
    return aus;
}

std::string Lesen(const Pakete& p, const std::string& pfad) {
    std::vector<uint8_t> roh;
    std::string f;
    if (!p.Lies(pfad, roh, f)) return std::string();
    return std::string(roh.begin(), roh.end());
}

// Glieder ohne Zahl am Ende ("mynock1" -> "mynock")
std::string OhneZiffern(std::string s) {
    while (!s.empty() && s.back() >= '0' && s.back() <= '9') s.pop_back();
    return s;
}

} // namespace

std::vector<size_t> Katalog::EigeneAnimationen(const Pakete& p, const FigurEintrag& f, std::vector<std::string>* quellen) const {
    std::vector<std::string> choreDateien;   // .choresetgroup.xml / .choreset.xml
    std::set<std::string> gesehen;
    auto merke = [&](const std::string& pfad) {
        const std::string k = Pakete::Schluessel(pfad);
        if (!k.empty() && gesehen.insert(k).second && p.Hat(k)) choreDateien.push_back(k);
    };
    // 1. Actors (und ihre Basis-Actors): Chore- und Moveset-Resourcen
    std::vector<std::string> offen(f.actors.begin(), f.actors.end());
    std::set<std::string> actorsGesehen;
    while (!offen.empty() && actorsGesehen.size() < 32) {
        const std::string a = offen.back();
        offen.pop_back();
        if (!actorsGesehen.insert(Klein(a)).second) continue;
        const std::string text = OhneKommentare(Lesen(p, a));
        size_t pos = 0;
        while ((pos = text.find("<zed_components_", pos)) != std::string::npos) {
            const size_t e = text.find('>', pos);
            const size_t z = text.find('<', e == std::string::npos ? pos + 1 : e);
            if (e == std::string::npos || z == std::string::npos) break;
            std::string wert = Trimme(text.substr(e + 1, z - e - 1));
            if (EndetMit(wert, ".xml")) merke(wert);
            pos = z;
        }
        for (const std::string& b : XmlWerte(text, "mKeyBaseActor")) {
            if (b.empty()) continue;
            const std::string blatt = Klein(Blatt(b));
            for (const PakEintrag& e : p.Eintraege())
                if (Klein(Blatt(e.name)) == blatt) { offen.push_back(e.name); break; }
        }
    }
    // 2. ChoreData-Ordner mit dem Namen der Figur (DarthVader, Juno, ewok, Player ...)
    const std::string figurOrdner = Klein(Blatt(f.ordner)), figurName = Klein(f.name);
    static const char* const kAllgemein[] = { "maleaverage", "malebrute", "common", "props", "femaleaverage", "maledwarf" };
    for (const PakEintrag& e : p.Eintraege()) {
        const std::string k = Klein(e.name);
        const size_t c = k.find("/choredata/");
        if (c == std::string::npos || !(EndetMit(k, ".choreset.xml") || EndetMit(k, ".choresetgroup.xml"))) continue;
        const size_t a = c + 11, z = k.find('/', a);
        if (z == std::string::npos) continue;
        const std::string ordner = k.substr(a, z - a);
        bool allgemein = false;
        for (const char* x : kAllgemein) if (ordner == x) allgemein = true;
        if (allgemein || ordner.size() < 4) continue;
        if (figurOrdner.compare(0, ordner.size(), ordner) == 0 || figurName.compare(0, ordner.size(), ordner) == 0) merke(e.name);
    }
    // Gruppen aufloesen, AnimIDs einsammeln
    std::set<std::string> ids;
    for (size_t i = 0; i < choreDateien.size(); ++i) {
        const std::string text = OhneKommentare(Lesen(p, choreDateien[i]));
        if (EndetMit(choreDateien[i], ".choresetgroup.xml")) {
            for (const std::string& s : AttributWerte(text, "Path")) merke(s);
        }
        for (const std::string& id : AttributWerte(text, "AnimID")) ids.insert(Klein(id));
    }
    if (quellen != nullptr) *quellen = choreDateien;
    std::vector<size_t> aus;
    for (size_t i = 0; i < animationen.size(); ++i) {
        const AnimEintrag& a = animationen[i];
        if (!KuerzelPasst(f, a)) continue;          // Gegenseite eines Finishers/Saberlocks
        const std::string n = Klein(a.name);
        if (ids.count(n)) { aus.push_back(i); continue; }
        // 3. Zwischensequenzen: "igc_KAM2_070_JunoAttackVader_juno" - das letzte Glied nennt die Figur.
        const std::string g = Klein(a.gruppe);
        if (g.find("igc") == std::string::npos && g.find("vignette") == std::string::npos) continue;
        const size_t u = n.rfind('_');
        const std::string glied = OhneZiffern(u == std::string::npos ? n : n.substr(u + 1));
        if (glied.size() < 4) continue;
        for (const std::string& fig : { figurName, figurOrdner }) {
            if (fig == glied || fig.compare(0, glied.size(), glied) == 0 ||
                (fig.size() > glied.size() && fig.compare(fig.size() - glied.size(), glied.size(), glied) == 0)) {
                aus.push_back(i);
                break;
            }
        }
    }
    return aus;
}

bool KuerzelPasst(const FigurEintrag& f, const AnimEintrag& a) {
    if (f.spiel == 1) {
        // TFU1, gemessen wie unten (Glied 2/3 der Clipnamen je Ordner Animation/characters/PCDX/<rig>):
        // "player_ma_com_...", "femaleRancor_frs_com_roar", "kazdan_kz_com_saber" ...
        static const char* const kRig1[][3] = {
            { "maleaverage", "ma", nullptr }, { "malebrute", "mb", nullptr }, { "femaleaverage", "fa", nullptr },
            { "maledwarf", "md", nullptr }, { "femalerancor", "frs", "frc" }, { "rancor", "brs", nullptr },
            { "kazdan", "kz", nullptr }, { "felucianbrute", "fb", nullptr }, { "giantslug", "gsl", nullptr },
            { "gonk", "gnk", nullptr }, { "junktitan", "jkt", nullptr }, { "droidhover", "dh", nullptr },
            { "astromech", "am", nullptr }, { "wookiee", "wk", nullptr }, { "wookieebrute", "wb", nullptr },
            { "trix", "trix", nullptr }, { "tendril", "tdl", nullptr }, { "turret", "tu", nullptr },
            { "turretunmanned", "tum", nullptr }, { "droidmouse", "dm", nullptr }, { "sarlaccspike", "spk", nullptr },
        };
        const std::string rig = Klein(f.rig);
        const char* const* eigen = nullptr;
        for (const auto& r : kRig1) if (rig == r[0]) eigen = &r[1];
        if (eigen == nullptr) return true;
        const std::string n = "_" + Klein(a.name) + "_";
        bool fremd = false;
        for (const auto& r : kRig1) {
            for (int j = 1; j < 3 && r[j] != nullptr; ++j) {
                if (n.find(std::string("_") + r[j] + "_") == std::string::npos) continue;
                if ((eigen[0] != nullptr && std::strcmp(eigen[0], r[j]) == 0) || (eigen[1] != nullptr && std::strcmp(eigen[1], r[j]) == 0))
                    return true;
                fremd = true;
            }
        }
        return !fremd;
    }
    // Gemessen: haeufigstes Zweibuchstaben-Glied der Clips je Rig-Ordner.
    static const char* const kRig[][2] = {
        { "maleaverage", "ma" }, { "malebrute", "mb" }, { "femaleaverage", "fa" }, { "maledwarf", "md" },
        { "giant", "ga" }, { "terrorgiant", "ga" }, { "gorillaboss", "gb" }, { "titandroid", "td" },
        { "astromech", "am" }, { "titanspawn", "ds" }, { "droidspawn", "ds" },
    };
    static const char* const kAlle[] = { "ma", "mb", "fa", "md", "ga", "gb", "td", "am", "ds", "gs", "pd", "dm", "my" };
    const std::string rig = Klein(f.rig);
    std::string eigenes;
    for (const auto& r : kRig) if (rig == r[0]) eigenes = r[1];
    if (eigenes.empty()) return true;                 // Einzelstuecke (unique): kein Kuerzel bekannt
    const std::string n = "_" + Klein(a.name) + "_";
    bool fremd = false;
    for (const char* k : kAlle) {
        const std::string glied = std::string("_") + k + "_";
        if (n.find(glied) == std::string::npos) continue;
        if (eigenes == k) return true;
        fremd = true;
    }
    return !fremd;
}

bool GleichesRig(const FigurEintrag& f, const AnimEintrag& a) {
    // "unique" ist ein Sammelordner fuer Einzelstuecke (Gonk, Sonden-Droide,
    // AT-ST, Yoda ...) - dort zaehlt nur der Ordner der Figur selbst.
    if (!KuerzelPasst(f, a)) return false;
    std::string rig = Klein(f.rig);
    if (rig == "unique") rig.clear();
    const std::string figur = Klein(Blatt(f.ordner));
    const std::string g = Klein(a.gruppe) + "/";
    size_t p = 0;
    while (p < g.size()) {
        const size_t e = g.find('/', p);
        const std::string glied = g.substr(p, e - p);
        if (!glied.empty() && (glied == rig || glied == figur)) return true;
        p = e + 1;
    }
    return false;
}

bool PasstZu(const std::vector<uint32_t>& clip, const std::vector<uint32_t>& skelett) {
    if (clip.empty() || skelett.empty()) return false;
    size_t treffer = 0;
    for (uint32_t c : clip) if (std::binary_search(skelett.begin(), skelett.end(), c)) ++treffer;
    // Mindestens vier Treffer (sonst passt jeder Ein-Spur-Helfer mit "root[0]").
    return treffer * 2 >= clip.size() && treffer >= std::min<size_t>(4, skelett.size());
}

std::map<std::string, TexturSatz> LoeseMaterialien(const Pakete& p, const FigurEintrag& f,
                                                   const std::vector<std::string>& gtoMaterialien,
                                                   std::vector<std::string>* protokoll,
                                                   const std::vector<std::string>* eingebettet) {
    std::map<std::string, TexturSatz> aus;
    // 1. actor.xml (und ihre Basis-Actors) - Zuordnung mGtoMaterial -> mDefMaterial
    std::map<std::string, std::string> zuordnung;    // klein(gtoMat) -> .material
    std::string actor = f.actor;
    for (int tiefe = 0; tiefe < 4 && !actor.empty(); ++tiefe) {
        std::vector<uint8_t> roh;
        std::string fe;
        if (!p.Lies(actor, roh, fe)) break;
        const std::string text(roh.begin(), roh.end());
        for (const std::string& block : XmlWerte(text, "ActorMaterialMapping")) {
            const std::vector<std::string> def = XmlWerte(block, "mDefMaterial");
            const std::vector<std::string> gm = XmlWerte(block, "mGtoMaterial");
            if (def.empty() || gm.empty() || def[0].empty()) continue;
            zuordnung.emplace(Klein(gm[0]), def[0]);
        }
        const std::vector<std::string> basis = XmlWerte(text, "mKeyBaseActor");
        actor.clear();
        if (!basis.empty() && !basis[0].empty()) {
            // Basis-Actor steht nur mit Dateinamen da - im Ordner des Actors oder darueber suchen.
            const std::string blatt = Klein(Blatt(basis[0]));
            for (const PakEintrag& e : p.Eintraege()) {
                if (Klein(Blatt(e.name)) == blatt) { actor = e.name; break; }
            }
        }
    }
    // 2. Alle .material im Ordner der Figur (Rueckfall nach Namensteil)
    std::vector<std::string> materialDateien;
    const std::string ordnerK = Klein(f.ordner) + "/";
    for (const PakEintrag& e : p.Eintraege()) {
        const std::string k = Klein(e.name);
        if (EndetMit(k, ".material") && k.compare(0, ordnerK.size(), ordnerK) == 0) materialDateien.push_back(e.name);
    }
    // TFU1: mDefMaterial ist nur ein Name ("Apprentice_Tiefactory_ACT1_bodyflesh") -
    // die gleichnamige .material, bevorzugt unter dem Ordner der Figur.
    std::map<std::string, std::string> nachStamm;     // klein(stamm) -> .material
    if (p.Spiel() == 1) {
        const std::string figurOrdner = Klein(f.ordner) + "/";
        for (const PakEintrag& e : p.Eintraege()) {
            const std::string k = Klein(e.name);
            if (!EndetMit(k, ".material")) continue;
            const std::string stamm = Klein(OhneEndung(Blatt(e.name)));
            const bool nah = k.compare(0, figurOrdner.size(), figurOrdner) == 0;
            auto it = nachStamm.find(stamm);
            if (it == nachStamm.end()) nachStamm.emplace(stamm, e.name);
            else if (nah && Klein(it->second).compare(0, figurOrdner.size(), figurOrdner) != 0) it->second = e.name;
        }
    }
    for (size_t mi = 0; mi < gtoMaterialien.size(); ++mi) {
        const std::string& gm = gtoMaterialien[mi];
        std::string mat;
        const auto it = zuordnung.find(Klein(gm));
        if (it != zuordnung.end() && p.Hat(it->second)) mat = it->second;
        if (mat.empty() && it != zuordnung.end() && !nachStamm.empty()) {
            const auto s = nachStamm.find(Klein(it->second));
            if (s != nachStamm.end()) mat = s->second;
        }
        if (mat.empty()) {
            // "figur-upperBody" -> eine .material, deren Name auf "upperbody" endet
            const size_t s = gm.rfind('-');
            const std::string teil = Klein(s == std::string::npos ? gm : gm.substr(s + 1));
            for (const std::string& m : materialDateien) {
                const std::string stamm = Klein(OhneEndung(Blatt(m)));
                if (!teil.empty() && stamm.size() >= teil.size() &&
                    stamm.compare(stamm.size() - teil.size(), teil.size(), teil) == 0 && !EndetMit(stamm, "_wet")) {
                    mat = m;
                    break;
                }
            }
        }
        TexturSatz t;
        if (!mat.empty()) t = LiesMaterial(p, mat);
        // Rueckfall TFU1: das in der GTO eingebettete Material
        std::string quelle = mat;
        if (t.farbe.empty() && eingebettet != nullptr && mi < eingebettet->size() && !(*eingebettet)[mi].empty()) {
            const TexturSatz e = LiesMaterialText(p, (*eingebettet)[mi]);
            if (!e.farbe.empty()) {
                t = e;
                quelle = "(embedded in GTO)";
            }
        }
        if (protokoll != nullptr)
            protokoll->push_back(gm + " -> " + (quelle.empty() ? std::string("(no material file)") : quelle) +
                                 "  color " + (t.farbe.empty() ? "-" : Blatt(t.farbe)) +
                                 "  normal " + (t.normal.empty() ? "-" : Blatt(t.normal)) +
                                 "  spec " + (t.glanz.empty() ? "-" : Blatt(t.glanz)));
        aus[gm] = t;
    }
    return aus;
}

std::wstring TexturAufPlatte(const Pakete& p, const std::string& archivPfad, bool istNormal,
                             const std::wstring& cacheOrdner, std::string& fehler) {
    const PakEintrag* e = p.Finde(archivPfad);
    if (e == nullptr) { fehler = "texture not in game: " + archivPfad; return std::wstring(); }
    std::string rel = e->name;
    std::replace(rel.begin(), rel.end(), '/', '\\');
    std::wstring ziel = cacheOrdner + L"\\" + Breit(rel);
    if (istNormal) ziel = ziel.substr(0, ziel.size() - 4) + L"_rgb.png";
    if (GetFileAttributesW(ziel.c_str()) != INVALID_FILE_ATTRIBUTES) return ziel;   // schon da

    // Ordner anlegen
    for (size_t i = cacheOrdner.size() + 1; i < ziel.size(); ++i) {
        if (ziel[i] == L'\\') CreateDirectoryW(ziel.substr(0, i).c_str(), nullptr);
    }
    std::vector<uint8_t> d;
    if (!p.Lies(*e, d, fehler)) return std::wstring();

    std::vector<uint8_t> schreiben;
    if (istNormal) {
        // DDS-Kopf: DXT5 erwartet. Oberste Mip-Stufe dekodieren.
        if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) { fehler = "not a DDS: " + archivPfad; return std::wstring(); }
        uint32_t h, w, fourcc;
        std::memcpy(&h, &d[12], 4);
        std::memcpy(&w, &d[16], 4);
        std::memcpy(&fourcc, &d[84], 4);
        const bool dxt5 = fourcc == 0x35545844u, dxt1 = fourcc == 0x31545844u;
        if ((!dxt5 && !dxt1) || w == 0 || h == 0 || w > 8192 || h > 8192) {
            // Unbekanntes Format: unveraendert durchreichen, Max liest DDS selbst.
            ziel = ziel.substr(0, ziel.size() - 8) + L".dds";
            schreiben = d;
        } else {
            const size_t bw = (w + 3) / 4, bh = (h + 3) / 4, blk = dxt5 ? 16 : 8;
            if (128 + bw * bh * blk > d.size()) { fehler = "DDS truncated: " + archivPfad; return std::wstring(); }
            std::vector<uint8_t> rgba(static_cast<size_t>(bw * 4) * (bh * 4) * 4);
            const size_t zeile = bw * 4 * 4;
            for (size_t by = 0; by < bh; ++by) {
                for (size_t bx = 0; bx < bw; ++bx) {
                    const uint8_t* q = &d[128 + (by * bw + bx) * blk];
                    uint8_t* z = &rgba[(by * 4) * zeile + bx * 16];
                    if (dxt5) bcdec_bc3(q, z, static_cast<int>(zeile));
                    else bcdec_bc1(q, z, static_cast<int>(zeile));
                }
            }
            // DXT5nm erkennen: Rot fast ueberall 255 und Blau fast 0.
            size_t rot = 0, blau = 0, n = 0;
            for (size_t i = 0; i < rgba.size(); i += 4 * 61) { rot += rgba[i]; blau += rgba[i + 2]; ++n; }
            const bool nm = dxt5 && n > 0 && rot / n > 240 && blau / n < 15;
            std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    const uint8_t* s = &rgba[y * zeile + x * 4];
                    uint8_t* t = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                    if (nm) {
                        const float nx = s[3] / 127.5f - 1.0f, ny = s[1] / 127.5f - 1.0f;
                        const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                        t[0] = s[3];
                        t[1] = s[1];
                        t[2] = static_cast<uint8_t>(std::lround((nz * 0.5f + 0.5f) * 255.0f));
                    } else {
                        t[0] = s[0]; t[1] = s[1]; t[2] = s[2];
                    }
                }
            }
            size_t laenge = 0;
            void* png = tdefl_write_image_to_png_file_in_memory(rgb.data(), static_cast<int>(w), static_cast<int>(h), 3, &laenge);
            if (png == nullptr) { fehler = "PNG encode failed: " + archivPfad; return std::wstring(); }
            schreiben.assign(static_cast<uint8_t*>(png), static_cast<uint8_t*>(png) + laenge);
            mz_free(png);
        }
    } else {
        schreiben.swap(d);
    }
    HANDLE h = CreateFileW(ziel.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { fehler = "cannot write " + Utf8(ziel); return std::wstring(); }
    DWORD geschrieben = 0;
    const BOOL ok = WriteFile(h, schreiben.data(), static_cast<DWORD>(schreiben.size()), &geschrieben, nullptr);
    CloseHandle(h);
    if (!ok || geschrieben != schreiben.size()) { DeleteFileW(ziel.c_str()); fehler = "write failed " + Utf8(ziel); return std::wstring(); }
    return ziel;
}

} // namespace tfu
