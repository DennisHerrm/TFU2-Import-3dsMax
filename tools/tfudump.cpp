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
//    tfudump <spielordner> stretch <name>       eigene Clips, die Knochenlaengen > 15 % aendern
//    tfudump <spielordner> animhash             Pruefsumme ueber alle dekodierten Werte
//  Spielordner: TFU2 oder TFU1 (wird erkannt).
//
//  Dieselben Quellen wie das Plugin - was hier stimmt, stimmt dort.
// ============================================================
#include "tfu_anim.h"
#include "tfu_gto.h"
#include "tfu_katalog.h"
#include "tfu_model.h"
#include "tfu_pak.h"

#include <algorithm>
#include <array>
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

// Pruefsumme ueber alle dekodierten Werte (Vergleich vor/nach Decoder-Aenderungen).
int AnimHash(const Pakete& p, const Katalog& k) {
    uint64_t h = 1469598103934665603ull;
    auto misch = [&](const void* q, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(q);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    size_t ok = 0;
    for (const AnimEintrag& a : k.animationen) {
        std::vector<uint8_t> roh;
        std::string fehler;
        AnimClip c;
        if (!p.Lies(a.pfad, roh, fehler) || !c.Lies(roh, fehler)) continue;
        ++ok;
        for (const AnimSpur& s : c.spuren) {
            misch(s.keys.data(), s.keys.size() * sizeof(s.keys[0]));
            misch(s.t.data(), s.t.size() * sizeof(float));
            misch(s.r.data(), s.r.size() * sizeof(float));
        }
    }
    std::printf("ANIMHASH %016llx over %zu clips\n", static_cast<unsigned long long>(h), ok);
    return 0;
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
    if (befehl == "animhash") return AnimHash(p, k);
    if (befehl == "diff" && argc > 5) {
        // tfudump <spiel A> diff <spiel B> <clip in A> <clip in B>:
        // je Bild und Knochen der Winkel zwischen den Rotationen (Keys linear/nlerp
        // abgetastet) und der Abstand der Wurzel-Translation.
        Pakete pb;
        if (!pb.Oeffne(argv[3], fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
        AnimClip ca, cb;
        std::vector<uint8_t> roh;
        if (!p.Lies(Utf8(argv[4]), roh, fehler) || !ca.Lies(roh, fehler) || !pb.Lies(Utf8(argv[5]), roh, fehler) || !cb.Lies(roh, fehler)) {
            std::printf("ERROR %s\n", fehler.c_str());
            return 1;
        }
        auto taste = [](const AnimSpur& s, float bild, float q[4], float t[3]) {
            size_t i = 0;
            while (i + 1 < s.keys.size() && s.keys[i + 1] <= bild) ++i;
            size_t j = std::min(i + 1, s.keys.size() - 1);
            float w = (j == i || s.keys[j] == s.keys[i]) ? 0.0f : (bild - s.keys[i]) / float(s.keys[j] - s.keys[i]);
            w = std::clamp(w, 0.0f, 1.0f);
            if (s.hatR) {
                const float* a = &s.r[i * 4];
                const float* b = &s.r[j * 4];
                const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
                const float sg = d < 0 ? -1.0f : 1.0f;
                float l = 0;
                for (int c = 0; c < 4; ++c) { q[c] = a[c] * (1 - w) + sg * b[c] * w; l += q[c] * q[c]; }
                l = std::sqrt(l);
                for (int c = 0; c < 4; ++c) q[c] /= l;
            }
            if (s.hatT) for (int c = 0; c < 3; ++c) t[c] = s.t[i * 3 + c] * (1 - w) + s.t[j * 3 + c] * w;
        };
        std::map<uint32_t, const AnimSpur*> nachB;
        for (const AnimSpur& s : cb.spuren) nachB[s.crc] = &s;
        const int bilder = std::min(ca.bilder, cb.bilder);
        double summe = 0;
        size_t n = 0;
        float maxW = 0, maxT = 0;
        size_t ueber5 = 0, ueber15 = 0;
        for (const AnimSpur& sa : ca.spuren) {
            const auto it = nachB.find(sa.crc);
            if (it == nachB.end() || !sa.hatR || !it->second->hatR || sa.keys.empty() || it->second->keys.empty()) continue;
            float mittel = 0;
            for (int f = 0; f < bilder; ++f) {
                float qa[4], qb[4], ta[3] = {}, tb[3] = {};
                taste(sa, float(f), qa, ta);
                taste(*it->second, float(f), qb, tb);
                const float d = std::fabs(qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3]);
                const float grad = 2.0f * std::acos(std::min(1.0f, d)) * 57.29578f;
                mittel += grad;
                maxW = std::max(maxW, grad);
                if (sa.crc == 0xeb79e903u || sa.crc == 0xf262d842u) {   // root[0], root[1]: Weg der Figur
                    const float dt = std::sqrt((ta[0] - tb[0]) * (ta[0] - tb[0]) + (ta[1] - tb[1]) * (ta[1] - tb[1]) + (ta[2] - tb[2]) * (ta[2] - tb[2]));
                    maxT = std::max(maxT, dt);
                }
            }
            mittel /= float(std::max(1, bilder));
            summe += mittel;
            ++n;
            if (mittel > 5) ++ueber5;
            if (mittel > 15) ++ueber15;
        }
        std::printf("DIFF frames %d/%d  bones %zu  mean %.2f deg  max %.1f deg  bones>5deg %zu  >15deg %zu  root path %.3f m\n",
                    ca.bilder, cb.bilder, n, n ? summe / n : 0.0, maxW, ueber5, ueber15, maxT);
        return 0;
    }
    if (befehl == "diffpose" && argc > 7) {
        // tfudump <spiel A> diffpose <spiel B> <clip A> <clip B> <figur A> <figur B>:
        // Gelenkpositionen je Bild (Vorwaertskinematik mit dem Skelett der jeweiligen
        // Figur), Huefte in den Ursprung, Drehung um Y bestmoeglich angeglichen;
        // gemeldet wird der mittlere Abstand der Koerpergelenke (cm).
        Pakete pb;
        Katalog kb;
        if (!pb.Oeffne(argv[3], fehler) || !kb.Baue(pb, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
        struct Seite { const Pakete* p; Modell m; AnimClip c; };
        Seite s[2];
        s[0].p = &p;
        s[1].p = &pb;
        const FigurEintrag* fa = FindeFigur(k, Utf8(argv[6]));
        const FigurEintrag* fb = FindeFigur(kb, Utf8(argv[7]));
        const std::string clips[2] = { Utf8(argv[4]), Utf8(argv[5]) };
        const FigurEintrag* figs[2] = { fa, fb };
        for (int i = 0; i < 2; ++i) {
            std::vector<uint8_t> roh;
            GtoDatei g;
            if (figs[i] == nullptr || !s[i].p->Lies(figs[i]->gto, roh, fehler) || !g.Lies(roh, fehler) || !s[i].m.Lies(g, fehler) ||
                !s[i].p->Lies(clips[i], roh, fehler) || !s[i].c.Lies(roh, fehler)) {
                std::printf("ERROR side %d %s\n", i, fehler.c_str());
                return 1;
            }
        }
        // Koerpergelenke, die beide Skelette haben
        static const char* const kGelenke[] = { "hips[0]", "spine[1]", "spine[3]", "neck[1]", "lUpperArm[0]", "lLowerArm[0]", "lHand[0]",
                                                "rUpperArm[0]", "rLowerArm[0]", "rHand[0]", "lUpperLeg[0]", "lLowerLeg[0]", "lFoot[0]",
                                                "rUpperLeg[0]", "rLowerLeg[0]", "rFoot[0]" };
        // Lokale Matrix (Zeilenvektoren wie BasePoseMatrices): Zeilen 0-2 Achsen, Zeile 3 Verschiebung
        auto ausQuat = [](const float q[4], const float t[3], float m[16]) {
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            const float r[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                                 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                                 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) };
            for (int a = 0; a < 3; ++a) { for (int b = 0; b < 3; ++b) m[a * 4 + b] = r[a * 3 + b]; m[a * 4 + 3] = 0; }
            m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = 1;
        };
        auto mal = [](const float a[16], const float b[16], float o[16]) {
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { float v = 0; for (int q = 0; q < 4; ++q) v += a[i * 4 + q] * b[q * 4 + j]; o[i * 4 + j] = v; }
        };
        auto taste = [](const AnimSpur& sp, float bild, float q[4], float t[3]) {
            size_t i = 0;
            while (i + 1 < sp.keys.size() && sp.keys[i + 1] <= bild) ++i;
            const size_t j = std::min(i + 1, sp.keys.size() - 1);
            float w = (j == i || sp.keys[j] == sp.keys[i]) ? 0.0f : std::clamp((bild - sp.keys[i]) / float(sp.keys[j] - sp.keys[i]), 0.0f, 1.0f);
            if (sp.hatR) {
                const float* a = &sp.r[i * 4];
                const float* b = &sp.r[j * 4];
                const float sg = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]) < 0 ? -1.0f : 1.0f;
                float l = 0;
                for (int c = 0; c < 4; ++c) { q[c] = a[c] * (1 - w) + sg * b[c] * w; l += q[c] * q[c]; }
                for (int c = 0; c < 4; ++c) q[c] /= std::sqrt(l);
            }
            if (sp.hatT) for (int c = 0; c < 3; ++c) t[c] = sp.t[i * 3 + c] * (1 - w) + sp.t[j * 3 + c] * w;
        };
        // Weltpositionen der Gelenke in Bild f
        auto pose = [&](const Seite& se, int f, std::vector<std::array<float, 3>>& aus) {
            std::map<uint32_t, const AnimSpur*> spur;
            for (const AnimSpur& sp : se.c.spuren) spur[sp.crc] = &sp;
            std::vector<std::array<float, 16>> welt(se.m.knochen.size());
            for (size_t i = 0; i < se.m.knochen.size(); ++i) {
                const Knochen& b = se.m.knochen[i];
                float lok[16];
                std::copy(b.lokal, b.lokal + 16, lok);
                const auto it = spur.find(b.crc);
                if (it != spur.end() && !it->second->keys.empty()) {
                    float q[4] = { 0, 0, 0, 1 }, t[3] = { b.lokal[12], b.lokal[13], b.lokal[14] };
                    taste(*it->second, float(f), q, t);
                    if (it->second->hatR) ausQuat(q, t, lok);
                    else { lok[12] = t[0]; lok[13] = t[1]; lok[14] = t[2]; }
                }
                if (b.eltern >= 0) mal(lok, welt[static_cast<size_t>(b.eltern)].data(), welt[i].data());
                else std::copy(lok, lok + 16, welt[i].data());
            }
            aus.clear();
            for (const char* g : kGelenke) {
                const int i = se.m.KnochenNachName(g);
                if (i < 0) { aus.push_back({ 1e9f, 0, 0 }); continue; }
                aus.push_back({ welt[i][12], welt[i][13], welt[i][14] });
            }
        };
        const int bilder = std::min(s[0].c.bilder, s[1].c.bilder);
        double summe = 0, schlimmstes = 0;
        size_t n = 0;
        std::vector<std::array<float, 3>> pa, pb2;
        for (int f = 0; f < bilder; ++f) {
            pose(s[0], f, pa);
            pose(s[1], f, pb2);
            // relativ zur Huefte, dann beste Drehung um Y (geschlossen)
            double sxz = 0, sxx = 0;
            std::vector<std::array<double, 3>> a, b;
            for (size_t i = 0; i < pa.size(); ++i) {
                if (pa[i][0] > 1e8f || pb2[i][0] > 1e8f) continue;
                a.push_back({ pa[i][0] - pa[0][0], pa[i][1] - pa[0][1], pa[i][2] - pa[0][2] });
                b.push_back({ pb2[i][0] - pb2[0][0], pb2[i][1] - pb2[0][1], pb2[i][2] - pb2[0][2] });
            }
            for (size_t i = 0; i < a.size(); ++i) {
                sxx += a[i][0] * b[i][0] + a[i][2] * b[i][2];
                sxz += a[i][2] * b[i][0] - a[i][0] * b[i][2];
            }
            const double th = std::atan2(sxz, sxx), c = std::cos(th), sn = std::sin(th);
            double bild = 0;
            for (size_t i = 1; i < a.size(); ++i) {
                const double x = c * a[i][0] + sn * a[i][2], z = -sn * a[i][0] + c * a[i][2];
                bild += std::sqrt((x - b[i][0]) * (x - b[i][0]) + (a[i][1] - b[i][1]) * (a[i][1] - b[i][1]) + (z - b[i][2]) * (z - b[i][2]));
            }
            if (a.size() > 1) bild /= double(a.size() - 1);
            summe += bild;
            schlimmstes = std::max(schlimmstes, bild);
            ++n;
            if (f == 0 && argc > 8) {
                for (size_t i = 0; i < a.size(); ++i)
                    std::printf("  %-12s A %6.3f %6.3f %6.3f   B %6.3f %6.3f %6.3f\n", kGelenke[i], a[i][0], a[i][1], a[i][2], b[i][0], b[i][1], b[i][2]);
            }
        }
        std::printf("POSE frames %d/%d  mean %.1f cm  worst frame %.1f cm\n", s[0].c.bilder, s[1].c.bilder, n ? 100.0 * summe / n : 0.0, 100.0 * schlimmstes);
        return 0;
    }
    if (befehl == "cliplist") {
        // Je Clip: Name, Bilder, Spuren, Pruefsumme der dekodierten Werte, Pfad;
        // mit Figur nur ihre eigenen Clips (Spalte OWN 1/0).
        const FigurEintrag* f = arg3.empty() ? nullptr : FindeFigur(k, arg3);
        std::vector<char> eigen(k.animationen.size(), 0);
        if (f != nullptr) for (size_t i : k.EigeneAnimationen(p, *f)) eigen[i] = 1;
        for (size_t i = 0; i < k.animationen.size(); ++i) {
            const AnimEintrag& a = k.animationen[i];
            std::vector<uint8_t> roh;
            AnimClip c;
            if (!p.Lies(a.pfad, roh, fehler) || !c.Lies(roh, fehler)) continue;
            uint64_t h = 1469598103934665603ull;
            for (const AnimSpur& sp : c.spuren) {
                for (const auto* v : { static_cast<const void*>(sp.t.data()), static_cast<const void*>(sp.r.data()) }) {
                    const size_t n = (v == sp.t.data() ? sp.t.size() : sp.r.size()) * sizeof(float);
                    const uint8_t* b = static_cast<const uint8_t*>(v);
                    for (size_t j = 0; j < n; ++j) { h ^= b[j]; h *= 1099511628211ull; }
                }
            }
            std::printf("CLIP\t%s\t%d\t%zu\t%016llx\t%d\t%s\n", a.name.c_str(), c.bilder, c.spuren.size(),
                        static_cast<unsigned long long>(h), eigen[i], a.pfad.c_str());
        }
        return 0;
    }
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
        const auto t2 = std::chrono::steady_clock::now();
        std::vector<std::string> quellen;
        const std::vector<size_t> eigene = k.EigeneAnimationen(p, *f, &quellen);
        size_t eigenePassend = 0;
        for (size_t i : eigene) if (PasstZu(alle[i], sk)) ++eigenePassend;
        const double s2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t2).count();
        std::printf("CHARACTER %s  bones %zu  own %zu (fitting %zu, %zu chore files, %.2f s)  same rig %zu  fitting animations %zu of %zu  (%.2f s)\n",
                    f->name.c_str(), sk.size(), eigene.size(), eigenePassend, quellen.size(), s2, gleich, n, alle.size(), s1);
        for (const auto& g : gruppen) std::printf("  %5zu  %s\n", g.second, g.first.c_str());
        for (const std::string& q : quellen) std::printf("SOURCE %s\n", q.c_str());
        for (const std::string& a : f->actors) std::printf("ACTOR %s\n", a.c_str());
        return 0;
    }
    if (befehl == "stretch") {
        // Eigene Clips einer Figur: weicht eine Knochenlaenge (Translation der Spur)
        // um mehr als 15 % von der Bindepose ab? Dann passt der Clip nicht zum Rig.
        const FigurEintrag* f = FindeFigur(k, arg3);
        if (f == nullptr) { std::printf("ERROR no such character\n"); return 1; }
        std::vector<uint8_t> roh;
        GtoDatei g;
        Modell m;
        if (!p.Lies(f->gto, roh, fehler) || !g.Lies(roh, fehler) || !m.Lies(g, fehler)) { std::printf("ERROR %s\n", fehler.c_str()); return 1; }
        std::map<uint32_t, float> laenge;
        for (const Knochen& b : m.knochen) {
            const float l = std::sqrt(b.lokal[12] * b.lokal[12] + b.lokal[13] * b.lokal[13] + b.lokal[14] * b.lokal[14]);
            if (l > 0.03f && b.eltern >= 0 && b.name.find("root") == std::string::npos && b.name.find("hips") == std::string::npos &&
                b.name.find("Weapon") == std::string::npos && b.name.find("collision") == std::string::npos &&
                b.name.find("holster") == std::string::npos && b.name.find("Jiggle") == std::string::npos) laenge[b.crc] = l;
        }
        const auto& alle = k.AnimCrcs(p);
        const std::vector<uint32_t> sk = ModellCrcs(p, f->gto);
        size_t gut = 0, schlecht = 0;
        for (size_t i : k.EigeneAnimationen(p, *f)) {
            if (!PasstZu(alle[i], sk)) continue;
            AnimClip c;
            if (!p.Lies(k.animationen[i].pfad, roh, fehler) || !c.Lies(roh, fehler)) continue;
            float schlimm = 0.0f;
            uint32_t welcher = 0;
            for (const AnimSpur& sp : c.spuren) {
                const auto it = laenge.find(sp.crc);
                if (it == laenge.end() || !sp.hatT) continue;
                for (size_t q = 0; q + 2 < sp.t.size(); q += 3) {
                    const float l = std::sqrt(sp.t[q] * sp.t[q] + sp.t[q + 1] * sp.t[q + 1] + sp.t[q + 2] * sp.t[q + 2]);
                    const float d = std::fabs(l / it->second - 1.0f);
                    if (d > schlimm) { schlimm = d; welcher = sp.crc; }
                }
            }
            std::string bname;
            for (const Knochen& b : m.knochen) if (b.crc == welcher) bname = b.name;
            if (schlimm > 0.15f) { ++schlecht; std::printf("STRETCH %3.0f%% %-20s %s\n", schlimm * 100.0f, bname.c_str(), k.animationen[i].pfad.c_str()); }
            else ++gut;
        }
        std::printf("CHARACTER %s  ok %zu  stretched %zu\n", f->name.c_str(), gut, schlecht);
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
        LoeseMaterialien(p, *f, m.materialien, &prot, &m.materialDaten);
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
