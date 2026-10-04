#include "tfu_model.h"

#include <cstring>
#include <type_traits>

namespace tfu {

namespace {

struct DeclElement {
    int stream = 0, offset = 0, typ = 0, usage = 0, usageIndex = 0;
};

float HalbZuFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    uint32_t f;
    if (e == 0) f = (m == 0) ? (s << 31) : 0u;   // Subnormale sind fuer Geometrie belanglos
    else if (e == 31) f = (s << 31) | 0x7f800000u | (m << 13);
    else f = (s << 31) | ((e + 112) << 23) | (m << 13);
    float r;
    std::memcpy(&r, &f, 4);
    return r;
}

// Ein Element einer D3D9-Vertexdeklaration lesen, bis zu vier Werte.
// Bytes (D3DCOLOR, UBYTE4) bleiben in Speicherreihenfolge 0..255.
int LiesElement(const uint8_t* q, int typ, float out[4]) {
    auto f32 = [&](int i) { float v; std::memcpy(&v, q + 4 * i, 4); return v; };
    auto s16 = [&](int i) { int16_t v; std::memcpy(&v, q + 2 * i, 2); return v; };
    auto u16 = [&](int i) { uint16_t v; std::memcpy(&v, q + 2 * i, 2); return v; };
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (typ) {
    case 0: out[0] = f32(0); return 1;                                         // FLOAT1
    case 1: out[0] = f32(0); out[1] = f32(1); return 2;                        // FLOAT2
    case 2: for (int i = 0; i < 3; ++i) out[i] = f32(i); return 3;             // FLOAT3
    case 3: for (int i = 0; i < 4; ++i) out[i] = f32(i); return 4;             // FLOAT4
    case 4: case 5: for (int i = 0; i < 4; ++i) out[i] = q[i]; return 4;       // D3DCOLOR, UBYTE4
    case 8: for (int i = 0; i < 4; ++i) out[i] = q[i] / 255.0f; return 4;      // UBYTE4N
    case 6: out[0] = s16(0); out[1] = s16(1); return 2;                        // SHORT2
    case 7: for (int i = 0; i < 4; ++i) out[i] = s16(i); return 4;             // SHORT4
    case 9: out[0] = s16(0) / 32767.0f; out[1] = s16(1) / 32767.0f; return 2;  // SHORT2N
    case 10: for (int i = 0; i < 4; ++i) out[i] = s16(i) / 32767.0f; return 4; // SHORT4N
    case 11: out[0] = u16(0) / 65535.0f; out[1] = u16(1) / 65535.0f; return 2; // USHORT2N
    case 12: for (int i = 0; i < 4; ++i) out[i] = u16(i) / 65535.0f; return 4; // USHORT4N
    case 15: out[0] = HalbZuFloat(u16(0)); out[1] = HalbZuFloat(u16(1)); return 2;
    case 16: for (int i = 0; i < 4; ++i) out[i] = HalbZuFloat(u16(i)); return 4;
    case 13: case 14: {                                                        // UDEC3, DEC3N
        uint32_t v;
        std::memcpy(&v, q, 4);
        for (int i = 0; i < 3; ++i) {
            int x = static_cast<int>((v >> (10 * i)) & 1023);
            if (typ == 14) { if (x >= 512) x -= 1024; out[i] = x / 511.0f; }
            else out[i] = static_cast<float>(x);
        }
        return 3;
    }
    default: return 0;
    }
}

int TypBytes(int typ) {
    static const int g[] = { 4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8 };
    return (typ >= 0 && typ <= 16) ? g[typ] : 0;
}

} // namespace

int Modell::KnochenNachName(const std::string& n) const {
    for (size_t i = 0; i < knochen.size(); ++i) if (knochen[i].name == n) return static_cast<int>(i);
    return -1;
}

bool Modell::Lies(const GtoDatei& g, std::string& fehler) {
    knochen.clear();
    meshes.clear();
    materialien.clear();

    // Alle PlatformGeometry-Objekte in Dateireihenfolge: "SharedVertexData = k"
    // in einem Strom heisst, die Vertices stehen im k-ten Objekt (gemessen an
    // Levelmodellen; dann ist NumVerts 0 und die Indizes zeigen in den Puffer
    // des anderen Teilmeshes).
    std::vector<const GtoObjekt*> geometrien;
    for (const GtoObjekt& o : g.objekte) if (o.protokoll == "PlatformGeometry") geometrien.push_back(&o);

    for (size_t oi = 0; oi < g.objekte.size(); ++oi) {
        const GtoObjekt& o = g.objekte[oi];
        if (o.protokoll == "Geometry" && name.empty()) {
            if (const GtoKomponente* k = o.Finde("Model")) {
                if (const GtoEigenschaft* e = k->Finde("Name")) name = g.String(*e);
            }
            if (name.empty()) name = o.name;
        } else if (o.protokoll == "Materials") {
            for (const GtoKomponente& k : o.komponenten) {
                if (const GtoEigenschaft* e = k.Finde("Name")) materialien.push_back(g.String(*e));
            }
        } else if (o.protokoll == "Skeleton") {
            const GtoKomponente* k = o.Finde("Skeleton");
            if (k == nullptr) continue;
            const GtoEigenschaft* en = k->Finde("BoneNames");
            const GtoEigenschaft* ec = k->Finde("BoneCRCs");
            const GtoEigenschaft* ep = k->Finde("BoneParents");
            const GtoEigenschaft* em = k->Finde("BasePoseMatrices");
            if (en == nullptr || ep == nullptr || em == nullptr) { fehler = "skeleton incomplete"; return false; }
            const size_t n = en->anzahl;
            knochen.resize(n);
            for (size_t i = 0; i < n; ++i) {
                Knochen& b = knochen[i];
                b.name = g.String(*en, i);
                b.crc = ec ? static_cast<uint32_t>(g.Int(*ec, i)) : 0;
                b.eltern = g.Int(*ep, i);
                if (b.eltern >= static_cast<int>(n) || b.eltern == static_cast<int>(i)) b.eltern = -1;
                if (em->breite == 16 && i < em->anzahl)
                    for (int k2 = 0; k2 < 16; ++k2) b.lokal[k2] = g.Float(*em, i * 16 + static_cast<size_t>(k2));
            }
        } else if (o.protokoll == "RenderMesh") {
            // Das zugehoerige PlatformGeometry-Objekt folgt (gleicher Name).
            const GtoObjekt* geo = nullptr;
            for (size_t j = oi + 1; j < g.objekte.size() && j <= oi + 2; ++j) {
                if (g.objekte[j].protokoll == "PlatformGeometry") { geo = &g.objekte[j]; break; }
            }
            if (geo == nullptr) continue;
            const GtoKomponente* info = o.Finde("MeshInfo");
            Teilmesh m;
            std::vector<int> palette;
            if (info != nullptr) {
                if (const GtoEigenschaft* e = info->Finde("MaterialName")) m.material = g.String(*e);
                if (const GtoEigenschaft* e = info->Finde("UniqueSubmeshName")) m.name = g.String(*e);
                if (m.name.empty()) if (const GtoEigenschaft* e = info->Finde("SubmeshName")) m.name = g.String(*e);
                if (const GtoEigenschaft* e = info->Finde("BonePalette"))
                    for (size_t i = 0; i < e->Werte(); ++i) palette.push_back(g.Int(*e, i));
            }
            const GtoKomponente* gi = geo->Finde("GeometryInfo");
            const GtoKomponente* ix = geo->Finde("Indices");
            if (gi == nullptr || ix == nullptr) continue;
            const GtoEigenschaft* ept = gi->Finde("PrimitiveType");
            const GtoEigenschaft* eid = ix->Finde("IndexData");
            if (eid == nullptr) continue;
            m.primitivTyp = ept ? g.Int(*ept) : 4;

            // Stroeme und Deklaration
            struct Strom { const uint8_t* daten = nullptr; size_t groesse = 0; int schritt = 0; };
            Strom strom[4];
            std::vector<DeclElement> decl;
            auto lieStrom = [&](const GtoKomponente& k, Strom& s) {
                const GtoEigenschaft* ed = k.Finde("VertexData");
                const GtoEigenschaft* es = k.Finde("VertexStride");
                if (ed == nullptr || es == nullptr) return;
                s.daten = g.Roh(*ed);
                s.groesse = ed->Werte();
                s.schritt = g.Int(*es);
            };
            for (const GtoKomponente& k : geo->komponenten) {
                if (k.name.compare(0, 6, "Stream") == 0 && k.name.size() == 7) {
                    const int s = k.name[6] - '0';
                    if (s < 0 || s > 3) continue;
                    if (const GtoEigenschaft* sh = k.Finde("SharedVertexData")) {
                        const int quelle = g.Int(*sh);
                        if (quelle >= 0 && static_cast<size_t>(quelle) < geometrien.size())
                            if (const GtoKomponente* qk = geometrien[static_cast<size_t>(quelle)]->Finde(k.name.c_str()))
                                lieStrom(*qk, strom[s]);
                    } else {
                        lieStrom(k, strom[s]);
                    }
                } else if (k.name == "VertexDecl") {
                    DeclElement d;
                    if (const GtoEigenschaft* e = k.Finde("Stream")) d.stream = g.Int(*e);
                    if (const GtoEigenschaft* e = k.Finde("Offset")) d.offset = g.Int(*e);
                    if (const GtoEigenschaft* e = k.Finde("Type")) d.typ = g.Int(*e);
                    if (const GtoEigenschaft* e = k.Finde("Usage")) d.usage = g.Int(*e);
                    if (const GtoEigenschaft* e = k.Finde("UsageIndex")) d.usageIndex = g.Int(*e);
                    decl.push_back(d);
                }
            }
            // Vertexzahl aus dem Puffer selbst (bei geteilten Puffern steht NumVerts auf 0).
            if (strom[0].daten == nullptr || strom[0].schritt <= 0) continue;
            const size_t nv = strom[0].groesse / static_cast<size_t>(strom[0].schritt);
            m.pos.assign(nv * 3, 0.0f);
            bool hatPos = false, hatBI = false, hatBW = false;
            std::vector<float> bi, bw;
            for (const DeclElement& d : decl) {
                if (d.stream < 0 || d.stream > 3 || strom[d.stream].daten == nullptr) continue;
                const Strom& s = strom[d.stream];
                if (static_cast<size_t>(s.schritt) * nv > s.groesse || d.offset + TypBytes(d.typ) > s.schritt) continue;
                std::vector<float>* ziel = nullptr;
                int breite = 0;
                if (d.usage == 1 && d.usageIndex == 0) { ziel = &m.pos; breite = 3; hatPos = true; }
                else if (d.usage == 8 && d.usageIndex == 0) { m.nrm.assign(nv * 3, 0.0f); ziel = &m.nrm; breite = 3; }
                else if (d.usage == 32 && d.usageIndex == 0) { m.uv.assign(nv * 2, 0.0f); ziel = &m.uv; breite = 2; }
                else if (d.usage == 1024 && d.usageIndex == 0) { m.farbe.assign(nv * 4, 0.0f); ziel = &m.farbe; breite = 4; }
                else if (d.usage == 4) { bi.assign(nv * 4, 255.0f); ziel = &bi; breite = 4; hatBI = true; }
                else if (d.usage == 2) { bw.assign(nv * 4, 0.0f); ziel = &bw; breite = 4; hatBW = true; }
                if (ziel == nullptr) continue;
                for (size_t v = 0; v < nv; ++v) {
                    float w[4];
                    LiesElement(s.daten + v * static_cast<size_t>(s.schritt) + static_cast<size_t>(d.offset), d.typ, w);
                    for (int c = 0; c < breite; ++c) (*ziel)[v * static_cast<size_t>(breite) + static_cast<size_t>(c)] = w[c];
                }
            }
            if (!hatPos) continue;
            if (!m.farbe.empty()) for (float& c : m.farbe) c /= 255.0f;

            // Einfluesse: Byte i der Gewichte gehoert zu Byte i der Indizes.
            if (hatBI && !palette.empty()) {
                m.knochen.assign(nv * 4, -1);
                m.gewicht.assign(nv * 4, 0.0f);
                for (size_t v = 0; v < nv; ++v) {
                    for (int k = 0; k < 4; ++k) {
                        const int lokal = static_cast<int>(bi[v * 4 + static_cast<size_t>(k)]);
                        float w = hatBW ? bw[v * 4 + static_cast<size_t>(k)] / 255.0f : (k == 0 ? 1.0f : 0.0f);
                        if (lokal < 0 || lokal >= static_cast<int>(palette.size()) || w <= 0.0f) continue;
                        m.knochen[v * 4 + static_cast<size_t>(k)] = palette[static_cast<size_t>(lokal)];
                        m.gewicht[v * 4 + static_cast<size_t>(k)] = w;
                    }
                }
            }

            // Dreiecke: 5 = Streifen (mit entarteten Dreiecken als Naht), 4 = Liste.
            std::vector<uint32_t> idx(eid->Werte());
            for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<uint32_t>(g.Int(*eid, i)) & 0xFFFFu;
            if (m.primitivTyp == 5) {
                for (size_t k = 0; k + 2 < idx.size(); ++k) {
                    const uint32_t a = idx[k], b = idx[k + 1], c = idx[k + 2];
                    if (a == b || b == c || a == c) continue;
                    if (a >= nv || b >= nv || c >= nv) continue;
                    if (k & 1) { m.dreiecke.push_back(a); m.dreiecke.push_back(c); m.dreiecke.push_back(b); }
                    else { m.dreiecke.push_back(a); m.dreiecke.push_back(b); m.dreiecke.push_back(c); }
                }
            } else {
                for (size_t k = 0; k + 2 < idx.size(); k += 3) {
                    if (idx[k] >= nv || idx[k + 1] >= nv || idx[k + 2] >= nv) continue;
                    m.dreiecke.insert(m.dreiecke.end(), { idx[k], idx[k + 1], idx[k + 2] });
                }
            }
            // Nur die benutzten Vertices behalten (geteilte Puffer enthalten alle Teilmeshes).
            {
                std::vector<int64_t> neu(nv, -1);
                size_t zahl = 0;
                for (uint32_t& i : m.dreiecke) {
                    if (neu[i] < 0) neu[i] = static_cast<int64_t>(zahl++);
                    i = static_cast<uint32_t>(neu[i]);
                }
                if (zahl < nv) {
                    auto verdichte = [&](auto& feld, size_t breite) {
                        if (feld.empty()) return;
                        auto alt = feld;
                        feld.assign(zahl * breite, typename std::decay_t<decltype(feld)>::value_type());
                        for (size_t v = 0; v < nv; ++v) {
                            if (neu[v] < 0) continue;
                            for (size_t c = 0; c < breite; ++c) feld[static_cast<size_t>(neu[v]) * breite + c] = alt[v * breite + c];
                        }
                    };
                    verdichte(m.pos, 3);
                    verdichte(m.nrm, 3);
                    verdichte(m.uv, 2);
                    verdichte(m.farbe, 4);
                    verdichte(m.knochen, 4);
                    verdichte(m.gewicht, 4);
                }
            }
            if (m.dreiecke.empty()) continue;
            if (m.name.empty()) m.name = "mesh" + std::to_string(meshes.size());
            meshes.push_back(std::move(m));
        }
    }
    if (meshes.empty() && knochen.empty()) { fehler = "no meshes and no skeleton in the GTO"; return false; }
    return true;
}

} // namespace tfu
