// ============================================================
//  TFU2 Import - der Max-Teil: Skelett, Meshes, Skin, Materialien,
//  Animation.
//
//  Raum: das Spiel rechnet in Metern mit Y oben und Zeilenvektoren
//  (Zeilen 0-2 Achsen, Zeile 3 Verschiebung) - wie Max. Umgerechnet
//  wird nur an der Wurzel: Welt_max = Welt_spiel * A mit
//  (x, y, z) -> (x, -z, y); lokale Lagen der Kinder bleiben gleich,
//  nur die Verschiebung wird mit dem Massstab multipliziert.
//
//  Viele Einzelheiten stammen aus dem SWBF2 Import, wo sie in Max
//  2016-2027 gemessen wurden (Auto-Align aus, Einhaengen vor dem
//  Setzen der Weltlage, Skin erst nach EvalWorldState fuellen,
//  Drehkeys ueber SetValue(CTRL_ABSOLUTE) im Animationsmodus).
// ============================================================
#include "tfu2import.h"
#include "tfu_anim.h"
#include "tfu_gto.h"
#include "tfu_model.h"

#include <MeshNormalSpec.h>
#include <iskin.h>
#include <stdmat.h>
#include <bitmap.h>
#include <modstack.h>
#include <notetrck.h>
#include <maxscript/maxscript.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#ifndef BONE_OBJ_CLASSID
#define BONE_OBJ_CLASSID Class_ID(BONE_OBJ_CLASS_ID, 0)
#endif

extern HINSTANCE hInstance;

namespace tfu2 {

// ------------------------------------------------------------
//  Ablage, Einstellungen, Protokoll
// ------------------------------------------------------------
std::wstring Ablage() {
    wchar_t puffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", puffer, MAX_PATH);
    std::wstring o = (n > 0 && n < MAX_PATH) ? std::wstring(puffer) : std::wstring(L".");
    o += L"\\TFU2Import";
    CreateDirectoryW(o.c_str(), nullptr);
    return o;
}

namespace {

std::map<std::wstring, std::wstring> LiesEinstellungen() {
    std::map<std::wstring, std::wstring> m;
    FILE* f = _wfopen((Ablage() + L"\\einstellungen.txt").c_str(), L"rb");
    if (f == nullptr) return m;
    std::string inhalt;
    char puffer[4096];
    size_t n;
    while ((n = std::fread(puffer, 1, sizeof puffer, f)) > 0) inhalt.append(puffer, n);
    std::fclose(f);
    size_t p = 0;
    while (p < inhalt.size()) {
        size_t e = inhalt.find('\n', p);
        if (e == std::string::npos) e = inhalt.size();
        std::string z = inhalt.substr(p, e - p);
        p = e + 1;
        while (!z.empty() && (z.back() == '\r' || z.back() == ' ')) z.pop_back();
        const size_t g = z.find('=');
        if (g == std::string::npos || z.empty() || z[0] == '#') continue;
        m[tfu::Breit(z.substr(0, g))] = tfu::Breit(z.substr(g + 1));
    }
    return m;
}

} // namespace

std::wstring LiesEinstellung(const std::wstring& schluessel) {
    const auto m = LiesEinstellungen();
    const auto it = m.find(schluessel);
    return it == m.end() ? std::wstring() : it->second;
}

void SchreibeEinstellung(const std::wstring& schluessel, const std::wstring& wert) {
    auto m = LiesEinstellungen();
    m[schluessel] = wert;
    FILE* f = _wfopen((Ablage() + L"\\einstellungen.txt").c_str(), L"wb");
    if (f == nullptr) return;
    std::fputs("# TFU2 Import - Einstellungen\r\n", f);
    for (const auto& kv : m) {
        const std::string z = tfu::Utf8(kv.first) + "=" + tfu::Utf8(kv.second) + "\r\n";
        std::fwrite(z.data(), 1, z.size(), f);
    }
    std::fclose(f);
}

namespace {
FILE* g_log = nullptr;
}

void LogNeu(const char* titel) {
    if (g_log != nullptr) std::fclose(g_log);
    g_log = _wfopen((Ablage() + L"\\import.log").c_str(), L"wb");
    if (g_log != nullptr) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::fprintf(g_log, "TFU2 Import %ls  %04d-%02d-%02d %02d:%02d:%02d  Max %d\r\n%s\r\n", TFU2IMPORT_VERSION_STR, st.wYear,
                     st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, MAX_RELEASE, titel);
        std::fflush(g_log);
    }
}

void Log(const char* format, ...) {
    if (g_log == nullptr) return;
    char puffer[2048];
    va_list a;
    va_start(a, format);
    std::vsnprintf(puffer, sizeof puffer, format, a);
    va_end(a);
    std::fputs(puffer, g_log);
    std::fputs("\r\n", g_log);
    std::fflush(g_log);
}

// ------------------------------------------------------------
//  Spiel laden (einmal je Ordner)
// ------------------------------------------------------------
bool Spiel(const std::wstring& ordner, const tfu::Pakete*& pakete, const tfu::Katalog*& katalog, std::string& fehler) {
    static std::unique_ptr<tfu::Pakete> p;
    static std::unique_ptr<tfu::Katalog> k;
    static std::wstring geladen;
    std::wstring o = ordner;
    if (o.empty()) o = LiesEinstellung(L"Spielordner");
    if (!p || _wcsicmp(o.c_str(), geladen.c_str()) != 0) {
        auto np = std::make_unique<tfu::Pakete>();
        auto nk = std::make_unique<tfu::Katalog>();
        if (!np->Oeffne(o, fehler) || !nk->Baue(*np, fehler)) return false;
        p = std::move(np);
        k = std::move(nk);
        geladen = o;
        SchreibeEinstellung(L"Spielordner", p->Ordner());
    }
    pakete = p.get();
    katalog = k.get();
    return true;
}

namespace {

// ------------------------------------------------------------
//  Zahlen und Matrizen
// ------------------------------------------------------------
Matrix3 Achsen() {
    // (x, y, z) -> (x, -z, y): Y oben wird Z oben, Determinante +1.
    Matrix3 a;
    a.IdentityMatrix();
    a.SetRow(0, Point3(1.0f, 0.0f, 0.0f));
    a.SetRow(1, Point3(0.0f, 0.0f, 1.0f));
    a.SetRow(2, Point3(0.0f, -1.0f, 0.0f));
    a.SetRow(3, Point3(0.0f, 0.0f, 0.0f));
    return a;
}

Matrix3 AusFeld(const float m[16]) {
    Matrix3 r;
    r.IdentityMatrix();
    for (int z = 0; z < 4; ++z) r.SetRow(z, Point3(m[z * 4 + 0], m[z * 4 + 1], m[z * 4 + 2]));
    return r;
}

// Drehung x,y,z,w in Zeilen: die Zeilen sind die Spalten der ueblichen
// Rotationsmatrix (gemessen: BasePoseMatrices = R(q) transponiert).
void QuatInZeilen(const float* q, Matrix3& m) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                             { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                             { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
    for (int r = 0; r < 3; ++r)
        m.SetRow(r, Point3(static_cast<float>(R[0][r]), static_cast<float>(R[1][r]), static_cast<float>(R[2][r])));
}

// Massstab: 1 m = 39,37 Einheiten (wie der SWBF2 Import); anders mit
// EinheitenJeMeter=... in einstellungen.txt, 0 = Systemeinheit.
float Massstab() {
    double jeMeter = 39.37007874015748;
    const std::wstring w = LiesEinstellung(L"EinheitenJeMeter");
    if (!w.empty()) {
        const std::string s = tfu::Utf8(w);
        double v = 0.0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec == std::errc() && v >= 0.0 && v < 1e6) jeMeter = v;
    }
    if (jeMeter > 0.0) return static_cast<float>(jeMeter);
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const double m = GetSystemUnitScale(UNITS_METERS);
#else
    const double m = GetMasterScale(UNITS_METERS);
#endif
    return m > 0.0 ? static_cast<float>(1.0 / m) : 1.0f;
}

std::string ZahlText(float v) {
    char b[32];
    const auto r = std::to_chars(b, b + sizeof b, v);
    return std::string(b, r.ptr);
}

// Feste Nachkommastellen mit Punkt - printf folgt in Max der Regionseinstellung.
std::string Dez(double v, int stellen) {
    char b[64];
    const auto r = std::to_chars(b, b + sizeof b, v, std::chars_format::fixed, stellen);
    return std::string(b, r.ptr);
}

std::string MatrixText(const Matrix3& m) {
    std::string s;
    for (int z = 0; z < 4; ++z) {
        const Point3 p = m.GetRow(z);
        s += ZahlText(p.x) + " " + ZahlText(p.y) + " " + ZahlText(p.z) + (z < 3 ? " " : "");
    }
    return s;
}

bool MatrixAusText(const std::string& s, Matrix3& m) {
    float v[12];
    const char* p = s.data();
    const char* e = s.data() + s.size();
    for (int i = 0; i < 12; ++i) {
        while (p < e && *p == ' ') ++p;
        const auto r = std::from_chars(p, e, v[i]);
        if (r.ec != std::errc()) return false;
        p = r.ptr;
    }
    m.IdentityMatrix();
    for (int z = 0; z < 4; ++z) m.SetRow(z, Point3(v[z * 3], v[z * 3 + 1], v[z * 3 + 2]));
    return true;
}

MSTR M(const std::string& s) { return MSTR::FromUTF8(s.c_str()); }

std::string NodeProp(INode* n, const MCHAR* key) {
    MSTR v;
    if (n == nullptr || !n->GetUserPropString(MSTR(key), v)) return std::string();
    return tfu::Utf8(std::wstring(v.data()));
}

struct AnimationAus {
    BOOL vorher;
    AnimationAus() : vorher(Animating()) { SuspendAnimate(); AnimateOff(); }
    ~AnimationAus() { ResumeAnimate(); if (vorher) AnimateOn(); }
};

// Knochen, die im Spielmodell an der Huefte haengen, in der Animation aber
// am Oberschenkel (gemessen an den Maya-XML-Clips: holster und
// UpperLegWeight[0]). Mit der Hierarchie der Animation folgen sie dem Bein.
const char* const kUmhaengen[][2] = {
    { "holster[1]", "lUpperLeg[0]" }, { "holster[0]", "rUpperLeg[0]" },
    { "lUpperLegWeight[0]", "lUpperLeg[0]" }, { "rUpperLegWeight[0]", "rUpperLeg[0]" },
};

// ------------------------------------------------------------
//  Skelett
// ------------------------------------------------------------
struct Skelett {
    std::vector<INode*> knoten;
    std::vector<Matrix3> weltSpiel;    // Bindepose im Spielraum (schon gedreht, siehe korrektur)
    std::vector<int> eltern;           // nach dem Umhaengen
    Matrix3 korrektur;                 // Drehung, die auch die Vertices bekommen
};

// ------------------------------------------------------------
//  Blickrichtung
//
//  Im Modell steht root[0] um -90 Grad um Y gedreht (gemessen an
//  Starkiller); die Figur blickt in der Bindepose deshalb nach +X, also
//  in Max nach rechts. Die Animationen setzen root[0] auf ihre eigene
//  Drehung (meist keine) - dann blickt die Figur nach -Y, in die
//  Vorderansicht. Damit die Bindepose genauso dasteht, wird die ganze
//  Figur (Knochen UND Vertices) um die Umkehrung der Wurzeldrehung
//  gedreht. Danach ist root[0] ungedreht, und Bindepose wie Animation
//  blicken nach vorn.
// ------------------------------------------------------------
Matrix3 BlickKorrektur(const tfu::Modell& m, const std::vector<Matrix3>& weltSpiel) {
    Matrix3 k;
    k.IdentityMatrix();
    int w = m.KnochenNachName("root[0]");
    if (w < 0)
        for (size_t i = 0; i < m.knochen.size(); ++i) if (m.knochen[i].eltern < 0) { w = static_cast<int>(i); break; }
    if (w < 0 || static_cast<size_t>(w) >= weltSpiel.size()) return k;
    Matrix3 r = weltSpiel[static_cast<size_t>(w)];
    r.SetRow(3, Point3(0.0f, 0.0f, 0.0f));
    r.Orthogonalize();
    k = Inverse(r);
    k.SetRow(3, Point3(0.0f, 0.0f, 0.0f));
    return k;
}

Skelett BaueSkelett(Interface* ip, const tfu::Modell& m, const tfu::FigurEintrag& f, float mass, const std::string& id) {
    Skelett s;
    const size_t n = m.knochen.size();
    s.knoten.assign(n, nullptr);
    s.weltSpiel.resize(n);
    s.eltern.resize(n);
    // Weltlagen im Spielraum (Kind * Eltern); die Eltern stehen in der Datei vorn.
    std::vector<int> reihenfolge;
    {
        std::vector<int> tiefe(n, -1);
        std::function<int(int)> t = [&](int i) -> int {
            if (tiefe[static_cast<size_t>(i)] >= 0) return tiefe[static_cast<size_t>(i)];
            const int e = m.knochen[static_cast<size_t>(i)].eltern;
            tiefe[static_cast<size_t>(i)] = 0;
            tiefe[static_cast<size_t>(i)] = (e < 0) ? 0 : t(e) + 1;
            return tiefe[static_cast<size_t>(i)];
        };
        for (size_t i = 0; i < n; ++i) { t(static_cast<int>(i)); reihenfolge.push_back(static_cast<int>(i)); }
        std::stable_sort(reihenfolge.begin(), reihenfolge.end(), [&](int a, int b) { return tiefe[static_cast<size_t>(a)] < tiefe[static_cast<size_t>(b)]; });
    }
    for (int i : reihenfolge) {
        const tfu::Knochen& b = m.knochen[static_cast<size_t>(i)];
        const Matrix3 lokal = AusFeld(b.lokal);
        s.weltSpiel[static_cast<size_t>(i)] = (b.eltern >= 0) ? lokal * s.weltSpiel[static_cast<size_t>(b.eltern)] : lokal;
        s.eltern[static_cast<size_t>(i)] = b.eltern;
    }
    for (const auto& u : kUmhaengen) {
        const int k = m.KnochenNachName(u[0]), e = m.KnochenNachName(u[1]);
        if (k >= 0 && e >= 0) s.eltern[static_cast<size_t>(k)] = e;
    }
    s.korrektur = BlickKorrektur(m, s.weltSpiel);
    for (Matrix3& w : s.weltSpiel) w = w * s.korrektur;

    const Matrix3 a = Achsen();
    AnimationAus keineKeys;
    for (size_t i = 0; i < n; ++i) {
        Object* obj = static_cast<Object*>(ip->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID));
        const bool istBone = obj != nullptr;
        if (obj == nullptr) obj = static_cast<Object*>(ip->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
        if (obj == nullptr) continue;
        INode* node = ip->CreateObjectNode(obj);
        if (node == nullptr) continue;
        MSTR name = M(m.knochen[i].name);
        node->SetName(name.data());
        node->ShowBone(1);
        node->SetBoneNodeOnOff(TRUE, 0);
        node->SetBoneAutoAlign(FALSE);
        node->SetBoneFreezeLen(TRUE);
        node->SetRenderable(FALSE);
        if (istBone) {
            Object* bo = node->GetObjectRef();
            if (bo != nullptr) bo = bo->FindBaseObject();
            IParamBlock2* pb = bo ? bo->GetParamBlockByID(0) : nullptr;
            if (pb != nullptr) { pb->SetValue(0, 0, 0.0f); pb->SetValue(1, 0, 0.0f); }
        }
        char crc[16];
        std::snprintf(crc, sizeof crc, "%08x", m.knochen[i].crc);
        node->SetUserPropString(MSTR(_T("tfu2_crc")), M(crc));
        s.knoten[i] = node;
    }
    // Erst einhaengen, dann die Weltlage setzen (Eltern vor Kind).
    for (size_t i = 0; i < n; ++i) {
        if (s.knoten[i] == nullptr) continue;
        const int e = s.eltern[i];
        if (e >= 0 && s.knoten[static_cast<size_t>(e)] != nullptr) s.knoten[static_cast<size_t>(e)]->AttachChild(s.knoten[i], 0);
    }
    for (int i : reihenfolge) {
        INode* node = s.knoten[static_cast<size_t>(i)];
        if (node == nullptr) continue;
        Matrix3 w = s.weltSpiel[static_cast<size_t>(i)] * a;
        const Point3 t = w.GetRow(3);
        w.SetRow(3, Point3(t.x * mass, t.y * mass, t.z * mass));
        node->SetNodeTM(0, w);
        // Ruhelage LOKAL im Spielraum, bezogen auf den Eltern in Max - fuer die Animation.
        const int e = s.eltern[static_cast<size_t>(i)];
        Matrix3 lokal = s.weltSpiel[static_cast<size_t>(i)];
        if (e >= 0) lokal = s.weltSpiel[static_cast<size_t>(i)] * Inverse(s.weltSpiel[static_cast<size_t>(e)]);
        node->SetUserPropString(MSTR(_T("tfu2_rest")), M(MatrixText(lokal)));
        // Jeder Knoten traegt Figur und Kennung: manche Skelette haben mehrere
        // Wurzeln (Juno, Gorilla-Boss: root[0] und shape_0..n).
        node->SetUserPropString(MSTR(_T("tfu2_id")), M(id));
        node->SetUserPropString(MSTR(_T("tfu2_figur")), M(f.gto));
    }
    return s;
}

// Eine Kennung je Import - verbindet Knochen und Meshes einer Figur.
std::string NeueId() {
    static unsigned zaehler = 0;
    char b[48];
    std::snprintf(b, sizeof b, "%llx-%u", static_cast<unsigned long long>(std::chrono::system_clock::now().time_since_epoch().count()), ++zaehler);
    return b;
}

// ------------------------------------------------------------
//  Meshes
// ------------------------------------------------------------
INode* BaueMesh(Interface* ip, const tfu::Teilmesh& t, const std::string& name, float mass, const Matrix3& korrektur) {
    const int nv = static_cast<int>(t.Vertices());
    const int nf = static_cast<int>(t.dreiecke.size() / 3);
    if (nv == 0 || nf == 0) return nullptr;
    TriObject* tri = CreateNewTriObject();
    if (tri == nullptr) return nullptr;
    Mesh& mesh = tri->GetMesh();
    mesh.setNumVerts(nv);
    for (int v = 0; v < nv; ++v) {
        const float* q = &t.pos[static_cast<size_t>(v) * 3];
        const Point3 p = Point3(q[0], q[1], q[2]) * korrektur;   // Spielraum, wie das Skelett gedreht
        mesh.setVert(v, p.x * mass, -p.z * mass, p.y * mass);
    }
    mesh.setNumFaces(nf);
    for (int f = 0; f < nf; ++f) {
        const uint32_t* d = &t.dreiecke[static_cast<size_t>(f) * 3];
        mesh.faces[f].setVerts(static_cast<int>(d[0]), static_cast<int>(d[1]), static_cast<int>(d[2]));
        mesh.faces[f].setEdgeVisFlags(1, 1, 1);
        mesh.faces[f].setSmGroup(1);
        mesh.faces[f].setMatID(0);
    }
    if (!t.uv.empty()) {
        // Die Datei fuehrt V nach unten (D3D); Max nach oben.
        mesh.setNumTVerts(nv);
        for (int v = 0; v < nv; ++v) mesh.setTVert(v, t.uv[static_cast<size_t>(v) * 2], 1.0f - t.uv[static_cast<size_t>(v) * 2 + 1], 0.0f);
        mesh.setNumTVFaces(nf);
        for (int f = 0; f < nf; ++f)
            mesh.tvFace[f].setTVerts(static_cast<int>(t.dreiecke[static_cast<size_t>(f) * 3]), static_cast<int>(t.dreiecke[static_cast<size_t>(f) * 3 + 1]),
                                     static_cast<int>(t.dreiecke[static_cast<size_t>(f) * 3 + 2]));
    }
    if (!t.farbe.empty()) {
        mesh.setMapSupport(0, TRUE);
        mesh.setNumMapVerts(0, nv);
        for (int v = 0; v < nv; ++v) {
            const float* c = &t.farbe[static_cast<size_t>(v) * 4];
            mesh.setMapVert(0, v, Point3(c[0], c[1], c[2]));
        }
        mesh.setNumMapFaces(0, nf);
        for (int f = 0; f < nf; ++f)
            mesh.mapFaces(0)[f].setTVerts(static_cast<int>(t.dreiecke[static_cast<size_t>(f) * 3]), static_cast<int>(t.dreiecke[static_cast<size_t>(f) * 3 + 1]),
                                          static_cast<int>(t.dreiecke[static_cast<size_t>(f) * 3 + 2]));
    }
    mesh.InvalidateTopologyCache();
    mesh.InvalidateGeomCache();
    mesh.buildNormals();
    // Die Normalen der Datei als explizite Normalen: das Spiel trennt Vertices an
    // UV-Naehten, eine Glaettungsgruppe allein zeigte dort Kanten.
    if (!t.nrm.empty()) {
        mesh.SpecifyNormals();
        MeshNormalSpec* ns = mesh.GetSpecifiedNormals();
        if (ns != nullptr) {
            ns->ClearAndFree();
            ns->SetParent(&mesh);
            ns->SetNumNormals(nv);
            for (int v = 0; v < nv; ++v) {
                const float* q = &t.nrm[static_cast<size_t>(v) * 3];
                const Point3 g = VectorTransform(korrektur, Point3(q[0], q[1], q[2]));
                Point3 n(g.x, -g.z, g.y);
                const float l = Length(n);
                ns->Normal(v) = (l > 1e-8f) ? n / l : Point3(0, 0, 1);
                ns->SetNormalExplicit(v, true);
            }
            ns->SetNumFaces(nf);
            for (int f = 0; f < nf; ++f) {
                MeshNormalFace& nfc = ns->Face(f);
                nfc.SpecifyAll(true);
                for (int k = 0; k < 3; ++k) nfc.SetNormalID(k, static_cast<int>(t.dreiecke[static_cast<size_t>(f) * 3 + static_cast<size_t>(k)]));
            }
            ns->SetFlag(MESH_NORMAL_MODIFIER_SUPPORT, true);
        }
    }
    INode* node = ip->CreateObjectNode(tri);
    if (node == nullptr) return nullptr;
    MSTR mn = M(name);
    node->SetName(mn.data());
    Matrix3 eins;
    eins.IdentityMatrix();
    node->SetNodeTM(0, eins);
    return node;
}

// ------------------------------------------------------------
//  Skin
// ------------------------------------------------------------
bool BaueSkin(Interface* ip, INode* node, const tfu::Teilmesh& t, const Skelett& s, size_t& gewichtet) {
    if (t.knochen.empty()) return false;
    const size_t nv = t.Vertices();
    std::set<int> benutzt;
    for (size_t i = 0; i < t.knochen.size(); ++i)
        if (t.knochen[i] >= 0 && t.gewicht[i] > 0.0f && static_cast<size_t>(t.knochen[i]) < s.knoten.size() && s.knoten[static_cast<size_t>(t.knochen[i])])
            benutzt.insert(t.knochen[i]);
    if (benutzt.empty()) return false;
    Modifier* mod = static_cast<Modifier*>(ip->CreateInstance(OSM_CLASS_ID, SKIN_CLASSID));
    ISkinImportData* imp = mod ? static_cast<ISkinImportData*>(mod->GetInterface(I_SKINIMPORTDATA)) : nullptr;
    if (mod == nullptr || imp == nullptr) return false;
    Interface7* ip7 = GetCOREInterface7();
    if (ip7 == nullptr || ip7->AddModifier(*node, *mod) != Interface7::kRES_SUCCESS) return false;
    for (int b : benutzt) imp->AddBoneEx(s.knoten[static_cast<size_t>(b)], FALSE);
    node->EvalWorldState(ip->GetTime());
    for (size_t v = 0; v < nv; ++v) {
        Tab<INode*> bones;
        Tab<float> w;
        for (int k = 0; k < 4; ++k) {
            const int b = t.knochen[v * 4 + static_cast<size_t>(k)];
            float g = t.gewicht[v * 4 + static_cast<size_t>(k)];
            if (b < 0 || g <= 0.0f || static_cast<size_t>(b) >= s.knoten.size() || s.knoten[static_cast<size_t>(b)] == nullptr) continue;
            INode* bn = s.knoten[static_cast<size_t>(b)];
            bones.Append(1, &bn);
            w.Append(1, &g);
        }
        if (bones.Count() > 0 && imp->AddWeights(node, static_cast<int>(v), bones, w)) ++gewichtet;
    }
    node->EvalWorldState(ip->GetTime());
    return true;
}

// ------------------------------------------------------------
//  Materialien
// ------------------------------------------------------------
const Class_ID kNormalBump(0x243e22c6, 0x63f6a014);

BitmapTex* Bitmap(const std::wstring& datei, bool linear) {
    BitmapTex* bt = NewDefaultBitmapTex();
    if (bt == nullptr) return nullptr;
    bt->SetMapName(datei.c_str());
    const size_t p = datei.find_last_of(L"\\/");
    bt->SetName(MSTR(datei.substr(p == std::wstring::npos ? 0 : p + 1).c_str()));
    if (linear) {
        BitmapInfo bi;
        bi.SetName(datei.c_str());
        bi.SetCustomGamma(1.0f);
        bi.SetCustomFlag(BMM_CUSTOM_GAMMA);
        bt->SetBitmapInfo(bi);
    }
    return bt;
}

Mtl* BaueMaterial(Interface* ip, const std::string& name, const std::wstring& farbe, const std::wstring& normal,
                  const std::wstring& glanz, bool alpha) {
    StdMat2* m = NewDefaultStdMat();
    if (m == nullptr) return nullptr;
    MSTR mn = M(name);
    m->SetName(mn);
    if (!farbe.empty()) {
        if (BitmapTex* bt = Bitmap(farbe, false)) {
            const int k = static_cast<int>(m->StdIDToChannel(ID_DI));
            m->SetSubTexmap(k, bt);
            m->EnableMap(k, TRUE);
            m->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED, TRUE);
            ip->ActivateTexture(bt, m);
        }
        if (alpha) {
            if (BitmapTex* ot = Bitmap(farbe, false)) {
                ot->SetAlphaAsMono(TRUE);
                const int k = static_cast<int>(m->StdIDToChannel(ID_OP));
                m->SetSubTexmap(k, ot);
                m->EnableMap(k, TRUE);
                m->SetTwoSided(TRUE);
            }
        }
    }
    if (!glanz.empty()) {
        if (BitmapTex* st = Bitmap(glanz, true)) {
            const int k = static_cast<int>(m->StdIDToChannel(ID_SS));
            m->SetSubTexmap(k, st);
            m->EnableMap(k, TRUE);
            m->SetTexmapAmt(k, 0.5f, 0);
        }
    }
    if (!normal.empty()) {
        BitmapTex* nb = Bitmap(normal, true);
        Texmap* gn = static_cast<Texmap*>(ip->CreateInstance(TEXMAP_CLASS_ID, kNormalBump));
        IParamBlock2* pb = gn ? gn->GetParamBlockByID(0) : nullptr;
        if (nb != nullptr && pb != nullptr) {
            pb->SetValue(2 /*normal_map*/, 0, static_cast<Texmap*>(nb));
            const int k = static_cast<int>(m->StdIDToChannel(ID_BU));
            m->SetSubTexmap(k, gn);
            m->EnableMap(k, TRUE);
            m->SetTexmapAmt(k, 1.0f, 0);
        }
    }
    return m;
}

std::string KurzName(const std::string& gtoMaterial) {
    const size_t p = gtoMaterial.rfind('-');
    return p == std::string::npos ? gtoMaterial : gtoMaterial.substr(p + 1);
}

// ------------------------------------------------------------
//  Szene durchsuchen
// ------------------------------------------------------------
void Sammle(INode* n, std::vector<INode*>& aus) {
    if (n == nullptr) return;
    aus.push_back(n);
    for (int i = 0; i < n->NumberOfChildren(); ++i) Sammle(n->GetChildNode(i), aus);
}

std::vector<INode*> AlleKnoten(Interface* ip) {
    std::vector<INode*> alle;
    INode* root = ip->GetRootNode();
    for (int i = 0; i < root->NumberOfChildren(); ++i) Sammle(root->GetChildNode(i), alle);
    return alle;
}

// Die Figur, auf die eine Animation soll: die Auswahl (Knochen oder Mesh),
// sonst die zuletzt angelegte. Liefert ihre Kennung.
std::string ZielId(Interface* ip) {
    for (int i = 0; i < ip->GetSelNodeCount(); ++i) {
        const std::string id = NodeProp(ip->GetSelNode(i), _T("tfu2_id"));
        if (!id.empty()) return id;
    }
    INode* beste = nullptr;
    for (INode* n : AlleKnoten(ip)) {
        if (NodeProp(n, _T("tfu2_crc")).empty() || NodeProp(n, _T("tfu2_id")).empty()) continue;
        if (beste == nullptr || n->GetHandle() > beste->GetHandle()) beste = n;
    }
    return beste ? NodeProp(beste, _T("tfu2_id")) : std::string();
}

// Alle Knochen einer Figur (Knoten mit dieser Kennung und einem CRC).
std::vector<INode*> KnochenMitId(Interface* ip, const std::string& id) {
    std::vector<INode*> aus;
    if (id.empty()) return aus;
    for (INode* n : AlleKnoten(ip))
        if (!NodeProp(n, _T("tfu2_crc")).empty() && NodeProp(n, _T("tfu2_id")) == id) aus.push_back(n);
    return aus;
}

} // namespace

std::vector<uint32_t> CrcsInSzene(const std::string& id, std::string* figur) {
    std::vector<uint32_t> aus;
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return aus;
    const std::vector<INode*> knochen = KnochenMitId(ip, id.empty() ? ZielId(ip) : id);
    if (knochen.empty()) return aus;
    if (figur != nullptr) *figur = NodeProp(knochen.front(), _T("tfu2_figur"));
    for (INode* n : knochen) aus.push_back(static_cast<uint32_t>(std::strtoul(NodeProp(n, _T("tfu2_crc")).c_str(), nullptr, 16)));
    std::sort(aus.begin(), aus.end());
    return aus;
}

// ------------------------------------------------------------
//  Figur importieren
// ------------------------------------------------------------
bool ImportiereFigur(const tfu::Pakete& p, const tfu::FigurEintrag& f, const ImportOptionen& o, std::wstring& bericht) {
    Interface* ip = GetCOREInterface();
    LogNeu(("Figur " + f.gto).c_str());
    std::vector<uint8_t> roh;
    std::string fehler;
    tfu::GtoDatei g;
    tfu::Modell m;
    if (!p.Lies(f.gto, roh, fehler) || !g.Lies(roh, fehler) || !m.Lies(g, fehler)) {
        Log("FEHLER %s", fehler.c_str());
        bericht = tfu::Breit(f.name + ": " + fehler);
        return false;
    }
    const float mass = Massstab();
    Log("Modell %s: %zu Knochen, %zu Teilmeshes, %zu Materialien, Actor %s, Massstab %.4f", m.name.c_str(), m.knochen.size(),
        m.meshes.size(), m.materialien.size(), f.actor.empty() ? "-" : f.actor.c_str(), mass);

    theHold.Suspend();
    ip->DisableSceneRedraw();
    const std::string id = NeueId();
    Skelett s = BaueSkelett(ip, m, f, mass, id);
    size_t bones = 0;
    for (INode* n : s.knoten) if (n) ++bones;

    // Meshes
    std::vector<INode*> meshKnoten(m.meshes.size(), nullptr);
    size_t verts = 0, tris = 0;
    for (size_t i = 0; i < m.meshes.size(); ++i) {
        const tfu::Teilmesh& t = m.meshes[i];
        char nr[16];
        std::snprintf(nr, sizeof nr, "%02zu", i);
        meshKnoten[i] = BaueMesh(ip, t, f.name + "_" + nr + "_" + KurzName(t.material), mass, s.korrektur);
        if (meshKnoten[i] != nullptr) {
            verts += t.Vertices();
            tris += t.dreiecke.size() / 3;
            meshKnoten[i]->SetUserPropString(MSTR(_T("tfu2_id")), M(id));
            meshKnoten[i]->SetUserPropString(MSTR(_T("tfu2_figur")), M(f.gto));
        }
        Log("Mesh %2zu %-28s %-48s %6zu Vertices %6zu Dreiecke", i, t.name.c_str(), t.material.c_str(), t.Vertices(), t.dreiecke.size() / 3);
    }

    // Skin
    size_t geskinnt = 0, gewichtet = 0;
    if (o.skin && bones > 0) {
        for (size_t i = 0; i < m.meshes.size(); ++i) {
            if (meshKnoten[i] && BaueSkin(ip, meshKnoten[i], m.meshes[i], s, gewichtet)) ++geskinnt;
        }
    }

    // Materialien
    size_t materialien = 0, texturen = 0;
    if (o.texturen) {
        std::vector<std::string> prot;
        const auto saetze = tfu::LoeseMaterialien(p, f, m.materialien, &prot);
        for (const std::string& z : prot) Log("Material %s", z.c_str());
        const std::wstring cache = Ablage() + L"\\textures";
        CreateDirectoryW(cache.c_str(), nullptr);
        std::map<std::string, Mtl*> fertig;
        int slot = 0;
        for (size_t i = 0; i < m.meshes.size(); ++i) {
            if (meshKnoten[i] == nullptr) continue;
            const std::string& gm = m.meshes[i].material;
            auto it = fertig.find(gm);
            if (it == fertig.end()) {
                const auto st = saetze.find(gm);
                tfu::TexturSatz ts;
                if (st != saetze.end()) ts = st->second;
                auto platte = [&](const std::string& a, bool normal) -> std::wstring {
                    if (a.empty()) return std::wstring();
                    std::string fe;
                    const std::wstring w = tfu::TexturAufPlatte(p, a, normal, cache, fe);
                    if (w.empty()) Log("Textur FEHLER %s", fe.c_str());
                    else ++texturen;
                    return w;
                };
                Mtl* mtl = BaueMaterial(ip, KurzName(gm), platte(ts.farbe, false), platte(ts.normal, true), platte(ts.glanz, false), ts.alpha);
                it = fertig.emplace(gm, mtl).first;
                if (mtl != nullptr) {
                    ++materialien;
                    if (slot < 24) ip->PutMtlToMtlEditor(mtl, slot++);
                }
            }
            if (it->second != nullptr) meshKnoten[i]->SetMtl(it->second);
        }
    }

    // Alles in eine Gruppe von Ebenen? Einfacher: Auswahl auf das Ergebnis.
    ip->ClearNodeSelection(FALSE);
    for (INode* n : meshKnoten) if (n) ip->SelectNode(n, 0);
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());

    Log("Fertig: %zu Knochen, %zu Meshes (%zu Vertices, %zu Dreiecke), %zu mit Skin (%zu Vertices gewichtet), %zu Materialien, %zu Texturen",
        bones, verts ? m.meshes.size() : 0, verts, tris, geskinnt, gewichtet, materialien, texturen);
    char b[512];
    std::snprintf(b, sizeof b, "%s: %zu bones, %zu meshes (%zu vertices, %zu triangles), %zu skinned, %zu materials, %zu textures",
                  f.name.c_str(), bones, m.meshes.size(), verts, tris, geskinnt, materialien, texturen);
    bericht = tfu::Breit(b);
    return bones > 0 || verts > 0;
}

// ------------------------------------------------------------
//  Animation
//
//  Einzelner Clip: Bild 0 bis Clipende, Keys an den gespeicherten Bildern.
//  Folge ("Load all to timeline", wie im SWBF2 Import):
//   * Bild 0 haelt die Bindepose, der erste Clip beginnt nach dem Abstand,
//     jeder weitere nach Clipende + Abstand.
//   * Jede Sequenz steht fuer sich: ein Knochen ohne Spur bekommt an BEIDEN
//     Enden die Ruhelage, eine Spur, die spaeter beginnt oder frueher endet,
//     ihren ersten/letzten Wert - sonst laeuft ein Clip in den naechsten.
//   * Notizspur auf der Szenenwurzel: zwei Keys je Sequenz (Anfang, Ende),
//     beide mit dem Namen (wie XFBIN, Animation Merge Tool und SWBF2).
//   * Custom Attributes "NeoDexSequenceData" auf der Szenenwurzel - dieselbe
//     Definition wie WhiteoutDex/SWBF2 (Namen, Anfangs- und Endbild ...).
// ------------------------------------------------------------
namespace {

bool FrischeController(Interface* ip, INode* n, Control*& rot, Control*& pos) {
    Control* tmc = n->GetTMController();
    if (tmc == nullptr) return false;
    Control* r = static_cast<Control*>(ip->CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    Control* q = static_cast<Control*>(ip->CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
    if (r != nullptr) tmc->SetRotationController(r);
    if (q != nullptr) tmc->SetPositionController(q);
    rot = tmc->GetRotationController();
    pos = tmc->GetPositionController();
    return rot != nullptr && pos != nullptr;
}

void ZuMax(Matrix3 lokal, bool wurzel, float mass, Quat& q, Point3& p) {
    if (wurzel) lokal = lokal * Achsen();
    const Point3 t = lokal.GetRow(3);
    p = Point3(t.x * mass, t.y * mass, t.z * mass);
    lokal.SetRow(3, Point3(0.0f, 0.0f, 0.0f));
    q = Quat(lokal);
}

struct Bein {
    INode* n = nullptr;
    Matrix3 ruhe;
    bool istWurzel = false;
};

struct ZielSkelett {
    std::vector<Bein> knochen;
    std::unordered_map<uint32_t, size_t> nachCrc;   // -> Index in knochen
    std::string figur;
};

bool HoleSkelett(Interface* ip, const std::string& idWunsch, ZielSkelett& z) {
    const std::string id = idWunsch.empty() ? ZielId(ip) : idWunsch;
    for (INode* n : KnochenMitId(ip, id)) {
        Bein b;
        if (!MatrixAusText(NodeProp(n, _T("tfu2_rest")), b.ruhe)) continue;
        b.n = n;
        b.istWurzel = n->GetParentNode() == nullptr || n->GetParentNode()->IsRootNode();
        z.nachCrc[static_cast<uint32_t>(std::strtoul(NodeProp(n, _T("tfu2_crc")).c_str(), nullptr, 16))] = z.knochen.size();
        z.knochen.push_back(b);
        if (z.figur.empty()) z.figur = NodeProp(n, _T("tfu2_figur"));
    }
    return !z.knochen.empty();
}

size_t Passend(const tfu::AnimClip& c, const ZielSkelett& z) {
    size_t n = 0;
    for (const tfu::AnimSpur& s : c.spuren) if (z.nachCrc.count(s.crc)) ++n;
    return n;
}

bool PasstClip(const tfu::AnimClip& c, const ZielSkelett& z) {
    const size_t n = Passend(c, z);
    return n > 0 && n * 2 >= c.spuren.size();       // dieselbe Regel wie tfu::PasstZu
}

// Spur je Knochen (Index in z.knochen), nullptr = keine
std::vector<const tfu::AnimSpur*> SpurenJeKnochen(const tfu::AnimClip& c, const ZielSkelett& z) {
    std::vector<const tfu::AnimSpur*> aus(z.knochen.size(), nullptr);
    for (const tfu::AnimSpur& s : c.spuren) {
        const auto it = z.nachCrc.find(s.crc);
        if (it != z.nachCrc.end() && !s.keys.empty()) aus[it->second] = &s;
    }
    return aus;
}

// ------------------------------------------------------------
//  "Face front": die Wurzel am Clipanfang nach vorn drehen.
//
//  Gemessen an 789 Starkiller-Clips: root[0] steht am ersten Key meist
//  +90 Grad (559) oder -90 Grad (177) um Y gedreht, nur 24 Clips gerade.
//  Im Spiel ist das gleichgueltig - die Wurzelbewegung wird RELATIV zur
//  Blickrichtung der Figur angewendet. In Max blickte die Figur dadurch
//  meist nach rechts. Deshalb: Gier und waagerechte Lage des ersten
//  Wurzel-Keys herausrechnen; jeder Clip beginnt im Ursprung und blickt
//  nach vorn, Drehungen und Wege WAEHREND des Clips bleiben erhalten.
// ------------------------------------------------------------
struct Ausrichtung {
    bool aktiv = false;
    size_t wurzel = static_cast<size_t>(-1);    // Index in ZielSkelett::knochen
    Matrix3 y;                                  // Gegendrehung um die Hochachse (Spielraum)
    Point3 t0 = Point3(0.0f, 0.0f, 0.0f);       // waagerechte Startlage
};

// Lokale Lage bei Key k (Drehung/Verschiebung aus der Spur, was fehlt aus der Ruhelage).
Matrix3 LokalBei(const Bein& b, const tfu::AnimSpur* s, size_t k, bool wurzelBewegung, const Ausrichtung* aus = nullptr,
                 size_t bi = static_cast<size_t>(-1)) {
    Matrix3 l = b.ruhe;
    if (s == nullptr) return l;
    const Point3 t0 = l.GetRow(3);
    if (s->hatR) QuatInZeilen(&s->r[k * 4], l);
    l.SetRow(3, t0);
    if (s->hatT && !(b.istWurzel && !wurzelBewegung)) l.SetRow(3, Point3(s->t[k * 3], s->t[k * 3 + 1], s->t[k * 3 + 2]));
    if (aus != nullptr && aus->aktiv && bi == aus->wurzel) {
        l.SetRow(3, l.GetRow(3) - aus->t0);
        l = l * aus->y;
    }
    return l;
}

Ausrichtung RichteAus(const ZielSkelett& z, const std::vector<const tfu::AnimSpur*>& spuren, bool wurzelBewegung, bool nachVorn) {
    Ausrichtung a;
    if (!nachVorn) return a;
    for (size_t i = 0; i < z.knochen.size(); ++i) {
        const MCHAR* n = z.knochen[i].n->GetName();
        if (n != nullptr && std::wstring(n) == L"root[0]") { a.wurzel = i; break; }
    }
    if (a.wurzel == static_cast<size_t>(-1))
        for (size_t i = 0; i < z.knochen.size(); ++i) if (z.knochen[i].istWurzel && spuren[i] != nullptr) { a.wurzel = i; break; }
    if (a.wurzel == static_cast<size_t>(-1) || spuren[a.wurzel] == nullptr) return a;
    const Matrix3 l0 = LokalBei(z.knochen[a.wurzel], spuren[a.wurzel], 0, wurzelBewegung);
    const Point3 x = l0.GetRow(0);                         // lokale X-Achse in der Welt
    const double gier = std::atan2(-static_cast<double>(x.z), static_cast<double>(x.x));
    const float c = static_cast<float>(std::cos(gier)), sn = static_cast<float>(std::sin(gier));
    a.y.IdentityMatrix();
    a.y.SetRow(0, Point3(c, 0.0f, sn));
    a.y.SetRow(1, Point3(0.0f, 1.0f, 0.0f));
    a.y.SetRow(2, Point3(-sn, 0.0f, c));
    a.y.SetRow(3, Point3(0.0f, 0.0f, 0.0f));
    if (wurzelBewegung) {
        const Point3 t = l0.GetRow(3);
        a.t0 = Point3(t.x, 0.0f, t.z);
    }
    a.aktiv = true;
    return a;
}

// Keys setzen mit fortlaufendem Quaternion-Vorzeichen.
struct KeySetzer {
    Control* rot = nullptr;
    Control* pos = nullptr;
    bool wurzel = false;
    float mass = 1.0f;
    Quat vorher;
    bool erster = true;
    size_t keys = 0, flips = 0;
    void Setze(TimeValue t, const Matrix3& l) {
        Quat q;
        Point3 p;
        ZuMax(l, wurzel, mass, q, p);
        if (!erster && (q.x * vorher.x + q.y * vorher.y + q.z * vorher.z + q.w * vorher.w) < 0.0f) {
            q = Quat(-q.x, -q.y, -q.z, -q.w);
            ++flips;
        }
        erster = false;
        vorher = q;
        rot->SetValue(t, &q, 1, CTRL_ABSOLUTE);
        pos->SetValue(t, &p, 1, CTRL_ABSOLUTE);
        ++keys;
    }
};

#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
bool FuehreSkript(const std::wstring& skript) {
    return ExecuteMAXScriptScript(skript.c_str(), MAXScript::ScriptSource::NonEmbedded, TRUE) != FALSE;
}
#else
bool FuehreSkript(const std::wstring& skript) {
    return ExecuteMAXScriptScript(skript.c_str(), TRUE) != FALSE;
}
#endif

std::wstring SkriptText(const std::string& s) {
    std::wstring w;
    for (wchar_t c : tfu::Breit(s)) {
        if (c == L'\\' || c == L'"') w += L'\\';
        w += c;
    }
    return w;
}

void LoescheNotizspuren(INode* w) {
    while (w != nullptr && w->NumNoteTracks() > 0) w->DeleteNoteTrack(w->GetNoteTrack(0), TRUE);
}

DWORD WINAPI KeinFortschritt(LPVOID) { return 0; }

} // namespace

std::vector<SzenenFigur> FigurenInSzene() {
    std::vector<SzenenFigur> aus;
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return aus;
    std::map<std::string, size_t> nachId;
    std::map<std::string, ULONG> neueste;
    for (INode* n : AlleKnoten(ip)) {
        if (NodeProp(n, _T("tfu2_crc")).empty()) continue;
        const std::string id = NodeProp(n, _T("tfu2_id"));
        if (id.empty()) continue;
        auto it = nachId.find(id);
        if (it == nachId.end()) {
            SzenenFigur f;
            f.id = id;
            f.gto = NodeProp(n, _T("tfu2_figur"));
            f.name = tfu::OhneEndung(tfu::Blatt(f.gto));
            it = nachId.emplace(id, aus.size()).first;
            aus.push_back(f);
        }
        aus[it->second].knochen++;
        neueste[id] = std::max(neueste[id], n->GetHandle());
    }
    const std::string gewaehlt = ZielId(ip);
    std::sort(aus.begin(), aus.end(), [&](const SzenenFigur& a, const SzenenFigur& b) {
        if ((a.id == gewaehlt) != (b.id == gewaehlt)) return a.id == gewaehlt;
        return neueste[a.id] > neueste[b.id];
    });
    return aus;
}

bool WendeAnimationAn(const tfu::Pakete& p, const std::string& animPfad, bool wurzelBewegung, bool nachVorn, const std::string& id,
                      std::wstring& bericht) {
    Interface* ip = GetCOREInterface();
    LogNeu(("Animation " + animPfad).c_str());
    ZielSkelett z;
    if (!HoleSkelett(ip, id, z)) { bericht = L"No TFU2 character in the scene - import one first."; return false; }
    std::vector<uint8_t> roh;
    std::string fehler;
    tfu::AnimClip c;
    if (!p.Lies(animPfad, roh, fehler) || !c.Lies(roh, fehler)) {
        Log("FEHLER %s", fehler.c_str());
        bericht = tfu::Breit(fehler);
        return false;
    }
    const size_t passend = Passend(c, z);
    Log("Clip: %s s, %d Bilder, %zu Spuren, davon %zu im Skelett (%zu Knochen) der Figur %s", Dez(c.dauer, 3).c_str(), c.bilder,
        c.spuren.size(), passend, z.knochen.size(), z.figur.c_str());
    if (!PasstClip(c, z)) {
        char b[256];
        std::snprintf(b, sizeof b, "This animation does not fit the character: only %zu of %zu tracks match its skeleton. Nothing changed.",
                      passend, c.spuren.size());
        bericht = tfu::Breit(b);
        Log("%s", b);
        return false;
    }
    if (GetFrameRate() != 30) SetFrameRate(30);
    const int tpf = GetTicksPerFrame();
    const float mass = Massstab();
    const std::vector<const tfu::AnimSpur*> spuren = SpurenJeKnochen(c, z);
    const Ausrichtung aus = RichteAus(z, spuren, wurzelBewegung, nachVorn);
    size_t keys = 0, mitKeys = 0, flips = 0;
    theHold.Suspend();
    ip->DisableSceneRedraw();
    LoescheNotizspuren(ip->GetRootNode());     // die Sequenzen einer frueheren Folge gelten nicht mehr
    SuspendAnimate();
    for (size_t i = 0; i < z.knochen.size(); ++i) {
        const Bein& b = z.knochen[i];
        KeySetzer ks;
        if (!FrischeController(ip, b.n, ks.rot, ks.pos)) continue;
        ks.wurzel = b.istWurzel;
        ks.mass = mass;
        ks.Setze(0, b.ruhe);                   // Grundwert (ohne Animationsmodus: kein Key)
        ks.keys = 0;
        const tfu::AnimSpur* s = spuren[i];
        if (s == nullptr) continue;
        ++mitKeys;
        AnimateOn();
        for (size_t k = 0; k < s->keys.size(); ++k) ks.Setze(static_cast<TimeValue>(s->keys[k]) * tpf, LokalBei(b, s, k, wurzelBewegung, &aus, i));
        AnimateOff();
        keys += ks.keys;
        flips += ks.flips;
    }
    ResumeAnimate();
    ip->SetAnimRange(Interval(0, std::max(1, c.bilder - 1) * tpf));
    ip->SetTime(0);
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());
    const std::string name = tfu::OhneEndung(tfu::Blatt(animPfad));
    Log("Fertig: %zu Knochen mit Keys, %zu Keys, %zu Vorzeichenwechsel, Bereich 0..%d", mitKeys, keys, flips, c.bilder - 1);
    char b[400];
    std::snprintf(b, sizeof b, "%s: %d frames (%s s), %zu of %zu tracks on %s, %zu keys", name.c_str(), c.bilder,
                  Dez(c.dauer, 2).c_str(), passend, c.spuren.size(), tfu::OhneEndung(tfu::Blatt(z.figur)).c_str(), keys);
    bericht = tfu::Breit(b);
    return true;
}

bool WendeFolgeAn(const tfu::Pakete& p, const std::vector<std::string>& pfade, int abstand, bool notiz, bool wurzelBewegung, bool nachVorn,
                  const std::string& id, std::vector<Sequenz>& plan, std::wstring& bericht) {
    const auto t0 = std::chrono::steady_clock::now();
    Interface* ip = GetCOREInterface();
    LogNeu(("Folge mit " + std::to_string(pfade.size()) + " Clips").c_str());
    plan.clear();
    ZielSkelett z;
    if (!HoleSkelett(ip, id, z)) { bericht = L"No TFU2 character in the scene - import one first."; return false; }
    abstand = std::max(0, std::min(abstand, 1000));

    // Clips lesen, nur passende behalten
    std::vector<tfu::AnimClip> clips;
    std::vector<std::string> namen;
    size_t unpassend = 0, kaputt = 0;
    for (const std::string& pfad : pfade) {
        std::vector<uint8_t> roh;
        std::string fehler;
        tfu::AnimClip c;
        if (!p.Lies(pfad, roh, fehler) || !c.Lies(roh, fehler)) { ++kaputt; Log("nicht lesbar %s: %s", pfad.c_str(), fehler.c_str()); continue; }
        if (!PasstClip(c, z)) { ++unpassend; continue; }
        namen.push_back(tfu::OhneEndung(tfu::Blatt(pfad)));
        clips.push_back(std::move(c));
    }
    if (clips.empty()) {
        bericht = L"None of the " + std::to_wstring(pfade.size()) + L" clips fits this character - nothing changed.";
        return false;
    }
    // Zeitplan in Bildern
    if (GetFrameRate() != 30) SetFrameRate(30);
    const TimeValue tpf = GetTicksPerFrame();
    int at = abstand;
    for (size_t i = 0; i < clips.size(); ++i) {
        Sequenz sq;
        sq.name = namen[i];
        sq.start = at;
        sq.ende = at + std::max(1, clips[i].bilder - 1);
        plan.push_back(sq);
        at = sq.ende + abstand;
    }
    std::vector<std::vector<const tfu::AnimSpur*>> spuren;
    spuren.reserve(clips.size());
    for (const tfu::AnimClip& c : clips) spuren.push_back(SpurenJeKnochen(c, z));
    std::vector<Ausrichtung> ausr;
    for (const auto& sp : spuren) ausr.push_back(RichteAus(z, sp, wurzelBewegung, nachVorn));

    const float mass = Massstab();
    size_t keys = 0, flips = 0;
    bool abgebrochen = false;
    theHold.Suspend();
    ip->DisableSceneRedraw();
    ip->ProgressStart(_T("TFU2: loading animations into the timeline"), TRUE, KeinFortschritt, nullptr);
    SuspendAnimate();
    for (size_t i = 0; i < z.knochen.size() && !abgebrochen; ++i) {
        const Bein& b = z.knochen[i];
        KeySetzer ks;
        if (!FrischeController(ip, b.n, ks.rot, ks.pos)) continue;
        ks.wurzel = b.istWurzel;
        ks.mass = mass;
        ks.Setze(0, b.ruhe);
        ks.keys = 0;
        AnimateOn();
        ks.Setze(0, b.ruhe);                                  // Bild 0: Bindepose
        for (size_t c = 0; c < clips.size(); ++c) {
            const TimeValue a = plan[c].start * tpf, e = plan[c].ende * tpf;
            const tfu::AnimSpur* s = spuren[c][i];
            if (s == nullptr) {
                ks.Setze(a, b.ruhe);
                ks.Setze(e, b.ruhe);
                continue;
            }
            const size_t n = s->keys.size();
            if (s->keys.front() > 0) ks.Setze(a, LokalBei(b, s, 0, wurzelBewegung, &ausr[c], i));
            for (size_t k = 0; k < n; ++k) {
                const TimeValue t = a + static_cast<TimeValue>(s->keys[k]) * tpf;
                if (t > e) break;
                ks.Setze(t, LokalBei(b, s, k, wurzelBewegung, &ausr[c], i));
            }
            if (a + static_cast<TimeValue>(s->keys[n - 1]) * tpf < e) ks.Setze(e, LokalBei(b, s, n - 1, wurzelBewegung, &ausr[c], i));
        }
        AnimateOff();
        keys += ks.keys;
        flips += ks.flips;
        ip->ProgressUpdate(static_cast<int>(100.0 * static_cast<double>(i + 1) / static_cast<double>(z.knochen.size())), FALSE);
        if (ip->GetCancel()) abgebrochen = true;
    }
    ResumeAnimate();
    ip->ProgressEnd();
    if (abgebrochen) ip->SetCancel(FALSE);

    // Notizspur auf der Szenenwurzel
    size_t notizKeys = 0;
    INode* w = ip->GetRootNode();
    LoescheNotizspuren(w);
    if (notiz && w != nullptr) {
        DefNoteTrack* nt = static_cast<DefNoteTrack*>(NewDefaultNoteTrack());
        if (nt != nullptr) {
            for (const Sequenz& sq : plan) {
                const MSTR text = M(sq.name);
                NoteKey* k0 = new NoteKey(sq.start * tpf, text, 0);
                NoteKey* k1 = new NoteKey(sq.ende * tpf, text, 0);
                nt->keys.Append(1, &k0);
                nt->keys.Append(1, &k1);
            }
            w->AddNoteTrack(nt);
            notizKeys = static_cast<size_t>(nt->keys.Count());
        }
    }
    // Custom Attributes NeoDexSequenceData (Definition wie WhiteoutDex/SWBF2)
    bool caOk = false;
    if (notiz) {
        std::wstring namenL, startL, endeL, nlL, rarL, spL, extL, grL;
        for (size_t i = 0; i < plan.size(); ++i) {
            const wchar_t* k = i ? L"," : L"";
            namenL += k + (L"\"" + SkriptText(plan[i].name) + L"\"");
            startL += k + std::to_wstring(plan[i].start);
            endeL += k + std::to_wstring(plan[i].ende);
            nlL += k + std::wstring(L"false");
            rarL += k + std::wstring(L"0.0");
            spL += k + std::wstring(L"0.0");
            extL += k + std::wstring(L"\"\"");
            grL += k + std::wstring(L"\"\"");
        }
        const std::wstring sk =
            L"(\n local ca = undefined\n try (ca = ::NeoDexSequenceCA) catch (ca = undefined)\n"
            L" if (ca == undefined) do ca = attributes \"NeoDexSequenceData\" (\n  parameters main (\n"
            L"   seqNames type:#stringTab tabSizeVariable:true\n   startFrames type:#intTab tabSizeVariable:true\n"
            L"   endFrames type:#intTab tabSizeVariable:true\n   nonLooping type:#boolTab tabSizeVariable:true\n"
            L"   rarity type:#floatTab tabSizeVariable:true\n   moveSpeed type:#floatTab tabSizeVariable:true\n"
            L"   seqExtents type:#stringTab tabSizeVariable:true\n   sharedGroup type:#stringTab tabSizeVariable:true\n  )\n )\n"
            L" for i = custAttributes.count rootNode to 1 by -1 do if (custAttributes.get rootNode i).name == \"NeoDexSequenceData\" do custAttributes.delete rootNode i\n"
            L" custAttributes.add rootNode ca\n"
            L" rootNode.seqNames = #(" + namenL + L")\n rootNode.startFrames = #(" + startL + L")\n rootNode.endFrames = #(" + endeL +
            L")\n rootNode.nonLooping = #(" + nlL + L")\n rootNode.rarity = #(" + rarL + L")\n rootNode.moveSpeed = #(" + spL +
            L")\n rootNode.seqExtents = #(" + extL + L")\n rootNode.sharedGroup = #(" + grL + L")\n OK\n)\n";
        caOk = FuehreSkript(sk);
    }
    ip->SetAnimRange(Interval(0, std::max(1, plan.back().ende) * tpf));
    ip->SetTime(0);
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());

    const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Log("Folge: %zu Clips (%zu passen nicht, %zu nicht lesbar), %zu Knochen, %zu Keys, %zu Vorzeichenwechsel, Bereich 0..%d, %s s, "
        "Notizspur %zu Keys, Custom Attributes %s%s",
        clips.size(), unpassend, kaputt, z.knochen.size(), keys, flips, plan.back().ende, Dez(sek, 1).c_str(), notizKeys,
        notiz ? (caOk ? "ok" : "FEHLGESCHLAGEN") : "aus", abgebrochen ? " - ABGEBROCHEN" : "");
    char b[400];
    std::snprintf(b, sizeof b, "%zu clips in the timeline (gap %d frames, 0 - %d)%s; %zu keys in %s s%s%s", clips.size(), abstand,
                  plan.back().ende, unpassend ? (", " + std::to_string(unpassend) + " did not fit").c_str() : "", keys, Dez(sek, 1).c_str(),
                  notiz ? "; note track + sequence attributes" : "", abgebrochen ? " - CANCELLED, bones after that keep the bind pose" : "");
    bericht = tfu::Breit(b);
    return true;
}

bool LiesSequenzen(std::vector<Sequenz>& aus) {
    aus.clear();
    Interface* ip = GetCOREInterface();
    INode* w = ip ? ip->GetRootNode() : nullptr;
    if (w == nullptr) return false;
    const TimeValue tpf = std::max(1, GetTicksPerFrame());
    for (int i = 0; i < w->NumNoteTracks(); ++i) {
        DefNoteTrack* nt = static_cast<DefNoteTrack*>(w->GetNoteTrack(i));
        if (nt == nullptr) continue;
        for (int k = 0; k + 1 < nt->keys.Count(); ++k) {
            NoteKey* a = nt->keys[k];
            NoteKey* e = nt->keys[k + 1];
            if (a == nullptr || e == nullptr || a->note != e->note) continue;
            Sequenz sq;
            sq.name = tfu::Utf8(std::wstring(a->note.data()));
            sq.start = static_cast<int>(a->time / tpf);
            sq.ende = static_cast<int>(e->time / tpf);
            aus.push_back(sq);
            ++k;
        }
    }
    return !aus.empty();
}

void ZeigeBereich(int startBild, int endeBild) {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return;
    const TimeValue tpf = GetTicksPerFrame();
    ip->SetAnimRange(Interval(startBild * tpf, std::max(startBild + 1, endeBild) * tpf));
    ip->SetTime(startBild * tpf);
}

uint32_t ThemeFarbe(int welche) { return static_cast<uint32_t>(GetCustSysColor(welche)); }


// ------------------------------------------------------------
//  Datei -> Importieren
// ------------------------------------------------------------
int ImportiereEingang(const MCHAR* pfad, BOOL ohneRueckfragen) {
    const std::wstring w = pfad ? pfad : L"";
    if (tfu::EndetMit(tfu::Utf8(w), ".gto")) {
        // Lose Datei: Figur ohne Spielpakete (keine Texturen).
        FILE* f = _wfopen(w.c_str(), L"rb");
        if (f == nullptr) return 0;
        std::vector<uint8_t> roh;
        char puffer[65536];
        size_t n;
        while ((n = std::fread(puffer, 1, sizeof puffer, f)) > 0) roh.insert(roh.end(), puffer, puffer + n);
        std::fclose(f);
        LogNeu(("Lose Datei " + tfu::Utf8(w)).c_str());
        tfu::GtoDatei g;
        tfu::Modell m;
        std::string fehler;
        if (!g.Lies(roh, fehler) || !m.Lies(g, fehler)) {
            Log("FEHLER %s", fehler.c_str());
            if (!ohneRueckfragen) MessageBoxW(GetCOREInterface()->GetMAXHWnd(), tfu::Breit(fehler).c_str(), L"TFU2 Import", MB_ICONERROR);
            return 0;
        }
        Interface* ip = GetCOREInterface();
        tfu::FigurEintrag fe;
        fe.gto = tfu::Utf8(w);
        fe.name = tfu::OhneEndung(tfu::Blatt(fe.gto));
        fe.rig = tfu::RigAus(fe.gto);
        const float mass = Massstab();
        theHold.Suspend();
        const std::string id = NeueId();
        Skelett s = BaueSkelett(ip, m, fe, mass, id);
        for (size_t i = 0; i < m.meshes.size(); ++i) {
            char nr[16];
            std::snprintf(nr, sizeof nr, "%02zu", i);
            INode* node = BaueMesh(ip, m.meshes[i], fe.name + "_" + nr + "_" + KurzName(m.meshes[i].material), mass, s.korrektur);
            if (node == nullptr) continue;
            node->SetUserPropString(MSTR(_T("tfu2_id")), M(id));
            node->SetUserPropString(MSTR(_T("tfu2_figur")), M(fe.gto));
            size_t gw = 0;
            BaueSkin(ip, node, m.meshes[i], s, gw);
        }
        theHold.Resume();
        ip->RedrawViews(ip->GetTime());
        return 1;
    }
    // Spiel: das Fenster fuer diesen Ordner oeffnen.
    SchreibeEinstellung(L"Spielordner", w);
    return OeffneFenster() > 0 ? 1 : 0;
}

} // namespace tfu2
