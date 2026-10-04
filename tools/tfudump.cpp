// ============================================================
//  tfudump - der Leser des Plugins ohne 3ds Max
//
//    tfudump <spielordner> list                 Figuren und Animationen zaehlen
//    tfudump <spielordner> model <name|pfad>    Skelett und Meshes einer Figur
//    tfudump <spielordner> anim <pfad> [n]      Spuren eines Clips (n Keys zeigen)
//    tfudump <spielordner> checkanims           alle Animationen dekodieren
//    tfudump <spielordner> checkmodels          alle Modelle lesen
//    tfudump <spielordner> materials <name>     Materialien und Texturen einer Figur
//    tfudump <spielordner> texture <pfad> <ziel> [normal]
//
//  Dieselben Quellen wie das Plugin - was hier stimmt, stimmt dort.
// ============================================================
#include "tfu_anim.h"
#include "tfu_gto.h"
#include "tfu_katalog.h"
#include "tfu_model.h"
#include "tfu_pak.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace tfu;

namespace {

const FigurEintrag* FindeFigur(const Katalog& k, const std::string& was) {
    const std::string w = Klein(was);
    for (const FigurEintrag& f : k.figuren) if (Klein(f.gto) == w || Klein(f.name) == w) return &f;
    for (const FigurEintrag& f : k.figuren) if (Klein(f.gto).find(w) != std::string::npos) return &f;
    return nullptr;
}

int Modell_(const Pakete& p, const Katalog& k, const std::string& was) {
    const FigurEintrag* f = FindeFigur(k, was);
    const std::string pfad = f ? f->gto : was;
    std::vector<uint8_t> roh;
    std::string fehler;
    if (!p.Lies(pfad, roh, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
    GtoDatei g;
    Modell m;
    if (!g.Lies(roh, fehler) || !m.Lies(g, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
    std::printf("MODEL %s  (%s)  bones %zu  meshes %zu  materials %zu  actor %s\n", m.name.c_str(), pfad.c_str(),
                m.knochen.size(), m.meshes.size(), m.materialien.size(), f ? f->actor.c_str() : "-");
    for (size_t i = 0; i < m.knochen.size(); ++i) {
        const Knochen& b = m.knochen[i];
        std::printf("BONE %3zu %-24s parent %3d crc %08x  t %.4f %.4f %.4f\n", i, b.name.c_str(), b.eltern, b.crc,
                    b.lokal[12], b.lokal[13], b.lokal[14]);
    }
    for (const Teilmesh& t : m.meshes) {
        size_t gewichtet = 0;
        for (size_t v = 0; v < t.Vertices(); ++v)
            if (!t.knochen.empty() && t.knochen[v * 4] >= 0) ++gewichtet;
        float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
        for (size_t v = 0; v < t.Vertices(); ++v)
            for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], t.pos[v * 3 + c]); hi[c] = std::max(hi[c], t.pos[v * 3 + c]); }
        std::printf("MESH %-24s mat %-50s verts %5zu tris %5zu prim %d uv %s nrm %s weighted %zu  box %.3f..%.3f %.3f..%.3f %.3f..%.3f\n",
                    t.name.c_str(), t.material.c_str(), t.Vertices(), t.dreiecke.size() / 3, t.primitivTyp,
                    t.uv.empty() ? "no" : "yes", t.nrm.empty() ? "no" : "yes", gewichtet, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    }
    return 0;
}

int Anim_(const Pakete& p, const std::string& pfad, int zeigen) {
    std::vector<uint8_t> roh;
    std::string fehler;
    if (!p.Lies(pfad, roh, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
    AnimClip c;
    if (!c.Lies(roh, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
    std::printf("CLIP %s  duration %.4f  frames %d  tracks %zu\n", pfad.c_str(), c.dauer, c.bilder, c.spuren.size());
    for (size_t i = 0; i < c.spuren.size(); ++i) {
        const AnimSpur& s = c.spuren[i];
        std::printf("TRACK %3zu crc %08x f0 %02x f1 %02x keys %3zu T %d R %d\n", i, s.crc, s.f0, s.f1, s.keys.size(), s.hatT, s.hatR);
        for (size_t k = 0; k < s.keys.size() && static_cast<int>(k) < zeigen; ++k) {
            std::printf("   key %3u", s.keys[k]);
            if (s.hatT) std::printf("  T %.6f %.6f %.6f", s.t[k * 3], s.t[k * 3 + 1], s.t[k * 3 + 2]);
            if (s.hatR) std::printf("  R %.6f %.6f %.6f %.6f", s.r[k * 4], s.r[k * 4 + 1], s.r[k * 4 + 2], s.r[k * 4 + 3]);
            std::printf("\n");
        }
    }
    return 0;
}

int PruefeAnims(const Pakete& p, const Katalog& k) {
    size_t ok = 0, schlecht = 0, spuren = 0, keys = 0;
    std::map<std::string, size_t> gruende;
    const auto t0 = std::chrono::steady_clock::now();
    for (const AnimEintrag& a : k.animationen) {
        std::vector<uint8_t> roh;
        std::string fehler;
        AnimClip c;
        if (!p.Lies(a.pfad, roh, fehler) || !c.Lies(roh, fehler)) {
            ++schlecht;
            if (gruende[fehler]++ < 3) std::printf("BAD %s: %s\n", a.pfad.c_str(), fehler.c_str());
            continue;
        }
        bool gut = true;
        for (const AnimSpur& s : c.spuren) {
            spuren++;
            keys += s.keys.size();
            for (size_t i = 1; i < s.keys.size(); ++i) if (s.keys[i] <= s.keys[i - 1]) gut = false;
            for (size_t i = 0; i + 3 < s.r.size(); i += 4) {
                const float l = s.r[i] * s.r[i] + s.r[i + 1] * s.r[i + 1] + s.r[i + 2] * s.r[i + 2] + s.r[i + 3] * s.r[i + 3];
                if (std::fabs(l - 1.0f) > 0.01f) gut = false;
            }
        }
        if (gut) ++ok;
        else { ++schlecht; if (gruende["checks"]++ < 3) std::printf("BAD %s: keys or quaternions\n", a.pfad.c_str()); }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("ANIMS ok %zu bad %zu tracks %zu keys %zu  %.1f s\n", ok, schlecht, spuren, keys, s);
    return schlecht == 0 ? 0 : 2;
}

int PruefeModelle(const Pakete& p) {
    size_t ok = 0, schlecht = 0, mitSkelett = 0, meshes = 0;
    for (const PakEintrag& e : p.Eintraege()) {
        if (!EndetMit(e.name, ".gto")) continue;
        std::vector<uint8_t> roh;
        std::string fehler;
        GtoDatei g;
        Modell m;
        if (!p.Lies(e, roh, fehler) || !g.Lies(roh, fehler) || !m.Lies(g, fehler)) {
            if (schlecht++ < 10) std::printf("BAD %s: %s\n", e.name.c_str(), fehler.c_str());
            continue;
        }
        ++ok;
        if (!m.knochen.empty()) ++mitSkelett;
        meshes += m.meshes.size();
    }
    std::printf("MODELS ok %zu bad %zu with skeleton %zu meshes %zu\n", ok, schlecht, mitSkelett, meshes);
    return schlecht == 0 ? 0 : 2;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: tfudump <game folder> list|model|anim|checkanims|checkmodels|materials|texture ...\n");
        return 1;
    }
    Pakete p;
    std::string fehler;
    const auto t0 = std::chrono::steady_clock::now();
    if (!p.Oeffne(argv[1], fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
    const std::string befehl = Utf8(argv[2]);
    const std::string arg3 = argc > 3 ? Utf8(argv[3]) : std::string();
    Katalog k;
    if (befehl != "anim" && befehl != "texture") {
        if (!k.Baue(p, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (befehl == "list") {
        std::printf("PAKS %zu  entries %zu  characters %zu  animations %zu  (%.2f s)\n", p.PaketZahl(), p.Eintraege().size(),
                    k.figuren.size(), k.animationen.size(), s);
        std::map<std::string, size_t> rigs;
        size_t mitActor = 0;
        for (const FigurEintrag& f : k.figuren) { rigs[f.rig]++; if (!f.actor.empty()) ++mitActor; }
        std::printf("with actor.xml %zu\n", mitActor);
        for (const auto& r : rigs) std::printf("RIG %-24s %zu\n", r.first.c_str(), r.second);
        size_t sk = 0;
        for (const FigurEintrag& f : k.figuren) if (f.skelett) ++sk;
        std::printf("with skeleton %zu\n", sk);
        if (arg3 == "all")
            for (const FigurEintrag& f : k.figuren)
                std::printf("CHAR %-20s %-40s %s %s\n", f.rig.c_str(), f.name.c_str(), f.skelett ? "SKEL" : "-", f.actor.c_str());
        return 0;
    }
    if (befehl == "model") return Modell_(p, k, arg3);
    if (befehl == "anim") return Anim_(p, arg3, argc > 4 ? _wtoi(argv[4]) : 3);
    if (befehl == "checkanims") return PruefeAnims(p, k);
    if (befehl == "checkmodels") return PruefeModelle(p);
    if (befehl == "fit") {
        // Welche Animationen passen zum Skelett einer Figur? (derselbe Filter wie im Fenster)
        const FigurEintrag* f = FindeFigur(k, arg3);
        if (f == nullptr) { std::printf("ERROR no such character\n"); return 1; }
        const auto t1 = std::chrono::steady_clock::now();
        const std::vector<uint32_t> sk = ModellCrcs(p, f->gto);
        const auto& alle = k.AnimCrcs(p);
        const double s1 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        size_t n = 0;
        std::map<std::string, size_t> gruppen;
        for (size_t i = 0; i < alle.size(); ++i) {
            if (!PasstZu(alle[i], sk)) continue;
            ++n;
            gruppen[k.animationen[i].gruppe]++;
            if (argc > 4 && Klein(k.animationen[i].pfad).find(Klein(Utf8(argv[4]))) != std::string::npos)
                std::printf("FIT %s\n", k.animationen[i].pfad.c_str());
        }
        size_t gleich = 0;
        for (size_t i = 0; i < k.animationen.size(); ++i) if (GleichesRig(*f, k.animationen[i]) && PasstZu(alle[i], sk)) ++gleich;
        std::printf("CHARACTER %s  bones %zu  same rig %zu  fitting animations %zu of %zu  (%.2f s)\n", f->name.c_str(), sk.size(), gleich, n,
                    alle.size(), s1);
        for (const auto& g : gruppen) std::printf("  %5zu  %s\n", g.second, g.first.c_str());
        return 0;
    }
    if (befehl == "materials") {
        const FigurEintrag* f = FindeFigur(k, arg3);
        if (f == nullptr) { std::printf("ERROR no such character\n"); return 1; }
        std::vector<uint8_t> roh;
        GtoDatei g;
        Modell m;
        if (!p.Lies(f->gto, roh, fehler) || !g.Lies(roh, fehler) || !m.Lies(g, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
        std::vector<std::string> prot;
        LoeseMaterialien(p, *f, m.materialien, &prot);
        std::printf("ACTOR %s\n", f->actor.c_str());
        for (const std::string& z : prot) std::printf("MAT %s\n", z.c_str());
        return 0;
    }
    if (befehl == "texture" && argc > 4) {
        const std::wstring ziel = TexturAufPlatte(p, arg3, argc > 5, argv[4], fehler);
        std::printf("%s %s\n", ziel.empty() ? "ERROR" : "OK", ziel.empty() ? fehler.c_str() : Utf8(ziel).c_str());
        return ziel.empty() ? 1 : 0;
    }
    std::printf("unknown command\n");
    return 1;
}
