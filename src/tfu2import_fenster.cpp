// ============================================================
//  TFU2 Import - das Figurenfenster (modal)
//
//  Aufbau und Gestaltung wie beim SWBF2 Import: Spielordner oben,
//  Statuszeile, Reiter nach Figurenart, Suche, selbst gezeichnete
//  zweizeilige Liste, Detailzeile, Fusszeile mit den Knoepfen.
//  Farben aus Max' Theme, Kontraste nach WCAG (tfu2_ui.h), dunkle
//  Titelleiste. Das Fenster ist modal - Max' Tastenkuerzel greifen
//  dann nicht in die Suche.
// ============================================================
#include "tfu2import.h"
#include "tfu2import_res.h"
#include "tfu2_ui.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <string>
#include <vector>

extern HINSTANCE hInstance;

namespace tfu2 {

namespace {

using tfu2ui::Mische;

constexpr int kKategorien = 5;
const wchar_t* const kReiter[kKategorien] = { L"Starkiller", L"Characters", L"Creatures + droids", L"Other", L"All" };

// Figurenart fuer die Reiter
int Kategorie(const tfu::FigurEintrag& f) {
    const std::string n = tfu::Klein(tfu::Blatt(f.ordner)), name = tfu::Klein(f.name), rig = tfu::Klein(f.rig);
    if (n.compare(0, 6, "player") == 0 || name.compare(0, 6, "player") == 0) return 0;
    if (rig == "maleaverage" || rig == "femaleaverage" || rig == "malebrute" || rig == "maledwarf") return 1;
    if (rig == "giant" || rig == "gorillaboss" || rig == "terrorgiant" || rig == "titandroid" || rig == "titanspawn" ||
        rig == "astromech" || rig == "unique")
        return 2;
    return 3;
}

enum : unsigned { kL = 1, kO = 2, kR = 4, kU = 8 };   // Anker: links, oben, rechts, unten
struct Anker {
    int id;
    RECT start;
    unsigned flags;
};

struct Fenster {
    HWND h = nullptr;
    tfu2ui::Palette pal;
    HFONT fontNormal = nullptr, fontFett = nullptr;
    int zeilenHoehe = 16;
    const tfu::Pakete* pakete = nullptr;
    const tfu::Katalog* katalog = nullptr;
    std::wstring ordner;
    std::vector<size_t> sichtbar;
    size_t anzahl[kKategorien] = {};
    int kategorie = 0;
    bool statisch = false;
    bool importiert = false;
    std::wstring status;
    bool statusFehler = false;
    std::vector<Anker> anker;
    SIZE startClient{ 0, 0 }, startFenster{ 0, 0 };
};

Fenster* Zustand(HWND h) { return reinterpret_cast<Fenster*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

void Status(Fenster& f, const std::wstring& t, bool fehler = false) {
    f.status = t;
    f.statusFehler = fehler;
    InvalidateRect(GetDlgItem(f.h, IDC_STATUS), nullptr, FALSE);
    UpdateWindow(GetDlgItem(f.h, IDC_STATUS));
}

// ------------------------------------------------------------
//  Spielordner
// ------------------------------------------------------------
bool IstSpielordner(const std::wstring& o) {
    if (o.empty()) return false;
    const DWORD a = GetFileAttributesW((o + L"\\LevelPacks\\pak0.lp").c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Registrywert(HKEY wurzel, const wchar_t* schluessel, const wchar_t* name) {
    wchar_t puffer[MAX_PATH] = {};
    DWORD groesse = sizeof puffer;
    if (RegGetValueW(wurzel, schluessel, name, RRF_RT_REG_SZ, nullptr, puffer, &groesse) != ERROR_SUCCESS) return std::wstring();
    return puffer;
}

// Steam-Bibliotheken nach dem Spiel absuchen (Standardordner, libraryfolders.vdf).
std::wstring SucheSpiel() {
    std::vector<std::wstring> steam;
    for (const std::wstring& s : { Registrywert(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
                                   Registrywert(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath") })
        if (!s.empty()) steam.push_back(s);
    steam.push_back(L"C:\\Program Files (x86)\\Steam");
    const std::wstring spiel = L"\\steamapps\\common\\Star Wars The Force Unleashed 2";
    for (const std::wstring& s : steam) {
        std::wstring o = s;
        std::replace(o.begin(), o.end(), L'/', L'\\');
        if (IstSpielordner(o + spiel)) return o + spiel;
        // weitere Bibliotheken: "path"  "D:\\SteamLibrary"
        FILE* vdf = _wfopen((o + L"\\steamapps\\libraryfolders.vdf").c_str(), L"rb");
        if (vdf == nullptr) continue;
        std::string t;
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, vdf)) > 0) t.append(b, n);
        std::fclose(vdf);
        size_t p = 0;
        while ((p = t.find("\"path\"", p)) != std::string::npos) {
            const size_t a = t.find('"', p + 6), e = (a == std::string::npos) ? a : t.find('"', a + 1);
            if (a == std::string::npos || e == std::string::npos) break;
            std::string pfad = t.substr(a + 1, e - a - 1);
            std::string sauber;
            for (size_t i = 0; i < pfad.size(); ++i) {
                if (pfad[i] == '\\' && i + 1 < pfad.size() && pfad[i + 1] == '\\') ++i;
                sauber += pfad[i];
            }
            const std::wstring kand = tfu::Breit(sauber) + spiel;
            if (IstSpielordner(kand)) return kand;
            p = e + 1;
        }
    }
    return std::wstring();
}

bool WaehleOrdner(HWND besitzer, std::wstring& ordner) {
#ifndef __IFileOpenDialog_INTERFACE_DEFINED__
    // Das SDK von Max 2016 stellt eine Windows-Version vor Vista ein - dann gibt
    // es IFileOpenDialog nicht, und der klassische Ordnerdialog tut es.
    BROWSEINFOW bi = {};
    bi.hwndOwner = besitzer;
    bi.lpszTitle = L"Star Wars The Force Unleashed 2 - the folder with SWTFU2.exe and LevelPacks";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (id == nullptr) return false;
    wchar_t pfad[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(id, pfad) != FALSE;
    CoTaskMemFree(id);
    if (ok) ordner = pfad;
    return ok;
#else
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Star Wars The Force Unleashed 2 - the folder with SWTFU2.exe and LevelPacks");
    bool ok = false;
    if (SUCCEEDED(dlg->Show(besitzer))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR pfad = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pfad))) {
                ordner = pfad;
                CoTaskMemFree(pfad);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
#endif
}

// ------------------------------------------------------------
//  Masse und Anordnung (mitwachsendes Fenster)
// ------------------------------------------------------------
int EintragsHoehe(HWND h) {
    const int z = tfu2ui::ZeilenHoehe(h);
    return 2 * z + std::max(4, z / 3) + 2;
}

void MerkeAnker(Fenster& f) {
    struct { int id; unsigned fl; } const liste[] = {
        { IDC_ORDNER_LABEL, kL | kO }, { IDC_ORDNER, kL | kO | kR }, { IDC_DURCHSUCHEN, kO | kR }, { IDC_STATUS, kL | kO | kR },
        { IDC_TAB0, kL | kO }, { IDC_TAB0 + 1, kL | kO }, { IDC_TAB0 + 2, kL | kO }, { IDC_TAB0 + 3, kL | kO }, { IDC_TAB0 + 4, kL | kO },
        { IDC_STATISCH, kO | kR }, { IDC_SUCHE_LABEL, kL | kO }, { IDC_SUCHE, kL | kO | kR }, { IDC_ANZAHL, kO | kR },
        { IDC_LISTE, kL | kO | kR | kU }, { IDC_DETAIL, kL | kR | kU }, { IDC_FUSS, kL | kR | kU },
        { IDC_ANIMFENSTER, kR | kU }, { IDOK, kR | kU }, { IDCANCEL, kR | kU },
    };
    RECT c{};
    GetClientRect(f.h, &c);
    f.startClient = { c.right - c.left, c.bottom - c.top };
    RECT w{};
    GetWindowRect(f.h, &w);
    f.startFenster = { w.right - w.left, w.bottom - w.top };
    for (const auto& e : liste) {
        HWND h = GetDlgItem(f.h, e.id);
        if (h == nullptr) continue;
        RECT r{};
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, f.h, reinterpret_cast<POINT*>(&r), 2);
        f.anker.push_back({ e.id, r, e.fl });
    }
}

void Ordne(Fenster& f, int cx, int cy) {
    if (f.anker.empty()) return;
    const int dx = cx - f.startClient.cx, dy = cy - f.startClient.cy;
    HDWP h = BeginDeferWindowPos(static_cast<int>(f.anker.size()));
    for (const Anker& a : f.anker) {
        RECT n = a.start;
        if (a.flags & kR) { if (a.flags & kL) n.right += dx; else { n.left += dx; n.right += dx; } }
        if (a.flags & kU) { if (a.flags & kO) n.bottom += dy; else { n.top += dy; n.bottom += dy; } }
        HWND c = GetDlgItem(f.h, a.id);
        if (h != nullptr && c != nullptr)
            h = DeferWindowPos(h, c, nullptr, n.left, n.top, n.right - n.left, n.bottom - n.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (h != nullptr) EndDeferWindowPos(h);
    InvalidateRect(f.h, nullptr, TRUE);
}

// ------------------------------------------------------------
//  Zeichnen
// ------------------------------------------------------------
void ZeichneEintrag(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    const bool sel = (d.itemState & ODS_SELECTED) != 0;
    HBRUSH hg = CreateSolidBrush(sel ? f.pal.auswahl : f.pal.feld);
    FillRect(dc, &r, hg);
    DeleteObject(hg);
    if (f.katalog == nullptr || d.itemID == static_cast<UINT>(-1) || d.itemID >= f.sichtbar.size()) return;
    const tfu::FigurEintrag& fi = f.katalog->figuren[f.sichtbar[d.itemID]];
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const RECT z1 = { r.left + rand, r.top + rand / 2, r.right - rand, r.top + rand / 2 + f.zeilenHoehe };
    const RECT z2 = { z1.left, z1.bottom, z1.right, z1.bottom + f.zeilenHoehe };
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ altF = SelectObject(dc, f.fontNormal);

    // Rechts in Zeile 1: Rig und Herkunft, gedaempft.
    const bool dlc = tfu::Klein(fi.gto).find("game/dlc/") == 0;
    std::wstring meta = tfu::Breit(fi.rig);
    if (!fi.skelett) meta += L"  \u00B7  static";
    if (dlc) meta += L"  \u00B7  DLC";
    RECT mess = z1;
    DrawTextW(dc, meta.c_str(), -1, &mess, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    const int mbreite = static_cast<int>(mess.right - mess.left);
    RECT mz = { z1.right - mbreite, z1.top, z1.right, z1.bottom };
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    DrawTextW(dc, meta.c_str(), -1, &mz, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    // Links in Zeile 1: der Name, halbfett.
    RECT nz = z1;
    nz.right = mz.left - 2 * rand;
    SelectObject(dc, f.fontFett != nullptr ? f.fontFett : f.fontNormal);
    SetTextColor(dc, sel ? f.pal.auswahlText : f.pal.feldText);
    const std::wstring name = tfu::Breit(fi.name);
    DrawTextW(dc, name.c_str(), -1, &nz, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // Zeile 2: wo die Figur im Spiel liegt (und ob sie einen Actor hat).
    SelectObject(dc, f.fontNormal);
    std::string ort = fi.ordner;
    const size_t c = tfu::Klein(ort).find("characters/");
    if (c != std::string::npos) ort = ort.substr(c + 11);
    std::wstring unter = tfu::Breit(ort);
    if (!fi.actor.empty()) unter += L"  \u00B7  " + tfu::Breit(tfu::Blatt(fi.actor));
    RECT uz = z2;
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    DrawTextW(dc, unter.c_str(), -1, &uz, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, altF);
    if (!sel) tfu2ui::Trennlinie(f.pal, dc, r, rand);
    if ((d.itemState & ODS_FOCUS) != 0 && (d.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT fr = r;
        DrawFocusRect(dc, &fr);
    }
}

// ------------------------------------------------------------
//  Liste und Anzeige
// ------------------------------------------------------------
int Auswahl(const Fenster& f) {
    const LRESULT s = SendDlgItemMessageW(f.h, IDC_LISTE, LB_GETCURSEL, 0, 0);
    if (s == LB_ERR || s < 0 || static_cast<size_t>(s) >= f.sichtbar.size()) return -1;
    return static_cast<int>(s);
}

void Bedienbarkeit(Fenster& f) {
    const bool liste = f.katalog != nullptr;
    for (int k = 0; k < kKategorien; ++k) EnableWindow(GetDlgItem(f.h, IDC_TAB0 + k), liste);
    EnableWindow(GetDlgItem(f.h, IDC_STATISCH), liste);
    EnableWindow(GetDlgItem(f.h, IDC_SUCHE), liste);
    EnableWindow(GetDlgItem(f.h, IDC_LISTE), liste);
    EnableWindow(GetDlgItem(f.h, IDOK), liste && Auswahl(f) >= 0);
    EnableWindow(GetDlgItem(f.h, IDC_ANIMFENSTER), liste);
    InvalidateRect(GetDlgItem(f.h, IDOK), nullptr, FALSE);
}

void ZeigeDetail(Fenster& f) {
    const int sel = Auswahl(f);
    std::wstring t;
    if (sel >= 0) t = tfu::Breit(f.katalog->figuren[f.sichtbar[static_cast<size_t>(sel)]].gto);
    else if (!f.sichtbar.empty()) t = L"Double-click a character or select it and press Import.";
    else if (f.katalog != nullptr) t = L"Nothing matches - try another tab or a shorter search.";
    SetDlgItemTextW(f.h, IDC_DETAIL, t.c_str());
}

std::vector<std::string> Suchwoerter(HWND h, int id) {
    wchar_t puffer[512] = {};
    GetDlgItemTextW(h, id, puffer, 512);
    const std::string s = tfu::Klein(tfu::Utf8(puffer));
    std::vector<std::string> woerter;
    std::string wort;
    for (char c : s) {
        if (c == ' ' || c == '\t') { if (!wort.empty()) { woerter.push_back(wort); wort.clear(); } }
        else wort += c;
    }
    if (!wort.empty()) woerter.push_back(wort);
    return woerter;
}

void FuelleListe(Fenster& f) {
    HWND lb = GetDlgItem(f.h, IDC_LISTE);
    std::string vorher;
    const int alt = Auswahl(f);
    if (alt >= 0) vorher = f.katalog->figuren[f.sichtbar[static_cast<size_t>(alt)]].gto;
    const std::vector<std::string> woerter = Suchwoerter(f.h, IDC_SUCHE);
    f.sichtbar.clear();
    std::fill(std::begin(f.anzahl), std::end(f.anzahl), size_t(0));
    if (f.katalog != nullptr) {
        for (size_t i = 0; i < f.katalog->figuren.size(); ++i) {
            const tfu::FigurEintrag& fi = f.katalog->figuren[i];
            if (!fi.skelett && !f.statisch) continue;
            const int art = Kategorie(fi);
            ++f.anzahl[art];
            ++f.anzahl[kKategorien - 1];
            if (f.kategorie < kKategorien - 1 && art != f.kategorie) continue;
            const std::string k = tfu::Klein(fi.gto);
            bool alle = true;
            for (const std::string& w : woerter) if (k.find(w) == std::string::npos) { alle = false; break; }
            if (alle) f.sichtbar.push_back(i);
        }
    }
    for (int k = 0; k < kKategorien; ++k) {
        std::wstring t = kReiter[k];
        if (f.katalog != nullptr) t += L"  " + std::to_wstring(f.anzahl[k]);
        SetDlgItemTextW(f.h, IDC_TAB0 + k, t.c_str());
        InvalidateRect(GetDlgItem(f.h, IDC_TAB0 + k), nullptr, FALSE);
    }
    InvalidateRect(GetDlgItem(f.h, IDC_STATISCH), nullptr, FALSE);
    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_SETCOUNT, static_cast<WPARAM>(f.sichtbar.size()), 0);
    int neu = -1;
    if (!vorher.empty())
        for (size_t j = 0; j < f.sichtbar.size(); ++j)
            if (f.katalog->figuren[f.sichtbar[j]].gto == vorher) { neu = static_cast<int>(j); break; }
    SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(neu), 0);
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, nullptr, TRUE);
    std::wstring z;
    if (f.katalog != nullptr) z = std::to_wstring(f.sichtbar.size()) + L" of " + std::to_wstring(f.anzahl[f.kategorie]);
    SetDlgItemTextW(f.h, IDC_ANZAHL, z.c_str());
    ZeigeDetail(f);
    Bedienbarkeit(f);
}

void Lade(Fenster& f) {
    SetDlgItemTextW(f.h, IDC_ORDNER, f.ordner.empty() ? L"(not found - choose it with Browse)" : f.ordner.c_str());
    if (!IstSpielordner(f.ordner)) {
        f.pakete = nullptr;
        f.katalog = nullptr;
        Status(f, L"Choose the game folder (the one with SWTFU2.exe and LevelPacks).", !f.ordner.empty());
        FuelleListe(f);
        return;
    }
    Status(f, L"Reading the game packs\u2026");
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::string fehler;
    const bool ok = Spiel(f.ordner, f.pakete, f.katalog, fehler);
    SetCursor(alt);
    if (!ok) {
        f.pakete = nullptr;
        f.katalog = nullptr;
        Status(f, L"Could not read the game: " + tfu::Breit(fehler), true);
    } else {
        size_t mitSkelett = 0;
        for (const tfu::FigurEintrag& fi : f.katalog->figuren) if (fi.skelett) ++mitSkelett;
        Status(f, std::to_wstring(mitSkelett) + L" characters with a skeleton, " + std::to_wstring(f.katalog->figuren.size()) +
                      L" models, " + std::to_wstring(f.katalog->animationen.size()) + L" animations in " +
                      std::to_wstring(f.pakete->PaketZahl()) + L" packs.");
        SetDlgItemTextW(f.h, IDC_FUSS, (std::wstring(L"TFU2 Import ") + TFU2IMPORT_VERSION_STR + L"  \u00B7  " +
                                        std::to_wstring(f.katalog->figuren.size()) + L" models").c_str());
    }
    FuelleListe(f);
}

void Importiere(Fenster& f) {
    const int sel = Auswahl(f);
    if (sel < 0 || f.katalog == nullptr || f.pakete == nullptr) return;
    const tfu::FigurEintrag& fi = f.katalog->figuren[f.sichtbar[static_cast<size_t>(sel)]];
    Status(f, L"Importing " + tfu::Breit(fi.name) + L" (skeleton, meshes, skin, textures)\u2026");
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring bericht;
    ImportOptionen o;
    const bool ok = ImportiereFigur(*f.pakete, fi, o, bericht);
    SetCursor(alt);
    if (ok) f.importiert = true;
    Status(f, (ok ? L"Imported " : L"Import failed: ") + bericht, !ok);
}

void Einrichten(Fenster& f) {
    f.pal.Baue(&ThemeFarbe);
    f.fontNormal = reinterpret_cast<HFONT>(SendMessageW(f.h, WM_GETFONT, 0, 0));
    LOGFONTW lf{};
    if (f.fontNormal != nullptr && GetObjectW(f.fontNormal, sizeof(lf), &lf) == sizeof(lf)) {
        lf.lfWeight = FW_SEMIBOLD;
        f.fontFett = CreateFontIndirectW(&lf);
    }
    f.zeilenHoehe = tfu2ui::ZeilenHoehe(f.h);
    SendDlgItemMessageW(f.h, IDC_LISTE, LB_SETITEMHEIGHT, 0, EintragsHoehe(f.h));
    tfu2ui::DunkleTitelleiste(f.h, f.pal.dunkel);
    if (f.pal.dunkel) SetWindowTheme(GetDlgItem(f.h, IDC_LISTE), L"DarkMode_Explorer", nullptr);
    SetWindowTextW(f.h, (std::wstring(L"TFU2 Import ") + TFU2IMPORT_VERSION_STR).c_str());
    {
        const int innen = std::max(3, f.zeilenHoehe / 4);
        SendDlgItemMessageW(f.h, IDC_SUCHE, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(innen, innen));
    }
    SendDlgItemMessageW(f.h, IDC_SUCHE, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter by name, e.g.  vader  or  kota"));
    SetDlgItemTextW(f.h, IDC_FUSS, (std::wstring(L"TFU2 Import ") + TFU2IMPORT_VERSION_STR).c_str());
    MerkeAnker(f);
    f.kategorie = std::clamp(_wtoi(LiesEinstellung(L"Kategorie").c_str()), 0, kKategorien - 1);
    f.statisch = LiesEinstellung(L"StatischeTeile") == L"1";
    f.ordner = LiesEinstellung(L"Spielordner");
    if (!IstSpielordner(f.ordner)) {
        const std::wstring gefunden = SucheSpiel();
        if (!gefunden.empty()) f.ordner = gefunden;
    }
    Lade(f);
    SetFocus(GetDlgItem(f.h, f.katalog != nullptr ? IDC_SUCHE : IDC_DURCHSUCHEN));
}

INT_PTR Verarbeite(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        Fenster* neu = reinterpret_cast<Fenster*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(neu));
        neu->h = h;
        Einrichten(*neu);
        return FALSE;
    }
    if (msg == WM_MEASUREITEM) {
        MEASUREITEMSTRUCT* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mi != nullptr && mi->CtlID == IDC_LISTE) { mi->itemHeight = static_cast<UINT>(EintragsHoehe(h)); return TRUE; }
        return FALSE;
    }
    Fenster* f = Zustand(h);
    if (f == nullptr) return FALSE;
    switch (msg) {
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        const bool leise = id == IDC_ORDNER_LABEL || id == IDC_SUCHE_LABEL || id == IDC_DETAIL || id == IDC_FUSS || id == IDC_ANZAHL;
        SetBkColor(dc, f->pal.grund);
        SetTextColor(dc, leise ? f->pal.dim : f->pal.text);
        return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, f->pal.feld);
        SetTextColor(dc, f->pal.feldText);
        return reinterpret_cast<INT_PTR>(f->pal.pinselFeld);
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (d == nullptr) return FALSE;
        if (d->CtlType == ODT_LISTBOX) ZeichneEintrag(*f, *d);
        else if (d->CtlType == ODT_STATIC) tfu2ui::ZeichneStatus(f->pal, f->fontNormal, *d, f->status, f->statusFehler);
        else if (d->CtlType == ODT_BUTTON) {
            const int id = static_cast<int>(d->CtlID);
            const bool betont = (id >= IDC_TAB0 && id < IDC_TAB0 + kKategorien && id - IDC_TAB0 == f->kategorie) ||
                                (id == IDC_STATISCH && f->statisch) || id == IDOK;
            tfu2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, betont);
        }
        return TRUE;
    }
    case WM_SIZE:
        Ordne(*f, LOWORD(lp), HIWORD(lp));
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        for (int id : { IDC_SUCHE, IDC_LISTE }) tfu2ui::Kante(h, dc, id, f->pal.linie);
        EndPaint(h, &ps);
        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
        if (mm != nullptr && f->startFenster.cx > 0) { mm->ptMinTrackSize.x = f->startFenster.cx; mm->ptMinTrackSize.y = f->startFenster.cy; }
        return TRUE;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= IDC_TAB0 && id < IDC_TAB0 + kKategorien) {
            if (code == BN_CLICKED) {
                f->kategorie = id - IDC_TAB0;
                SchreibeEinstellung(L"Kategorie", std::to_wstring(f->kategorie));
                FuelleListe(*f);
            }
            return TRUE;
        }
        switch (id) {
        case IDC_DURCHSUCHEN:
            if (code == BN_CLICKED) {
                std::wstring o;
                if (WaehleOrdner(h, o)) {
                    // Auch LevelPacks oder ein Unterordner des Spiels ist recht.
                    if (!IstSpielordner(o) && IstSpielordner(o + L"\\..")) o += L"\\..";
                    wchar_t voll[MAX_PATH];
                    if (GetFullPathNameW(o.c_str(), MAX_PATH, voll, nullptr) > 0) o = voll;
                    f->ordner = o;
                    Lade(*f);
                }
            }
            return TRUE;
        case IDC_STATISCH:
            if (code == BN_CLICKED) {
                f->statisch = !f->statisch;
                SchreibeEinstellung(L"StatischeTeile", f->statisch ? L"1" : L"0");
                FuelleListe(*f);
            }
            return TRUE;
        case IDC_SUCHE:
            if (code == EN_CHANGE) FuelleListe(*f);
            return TRUE;
        case IDC_LISTE:
            if (code == LBN_SELCHANGE) { ZeigeDetail(*f); Bedienbarkeit(*f); }
            else if (code == LBN_DBLCLK && IsWindowEnabled(GetDlgItem(h, IDOK))) Importiere(*f);
            return TRUE;
        case IDOK:
            // Eingabetaste im Suchfeld: erst den ersten Treffer waehlen, dann importieren.
            if (Auswahl(*f) < 0 && !f->sichtbar.empty()) {
                SendDlgItemMessageW(h, IDC_LISTE, LB_SETCURSEL, 0, 0);
                ZeigeDetail(*f);
                Bedienbarkeit(*f);
                SetFocus(GetDlgItem(h, IDC_LISTE));
            } else if (IsWindowEnabled(GetDlgItem(h, IDOK))) {
                Importiere(*f);
            }
            return TRUE;
        case IDC_ANIMFENSTER:
            if (code == BN_CLICKED) OeffneAnimFenster(h);
            return TRUE;
        case IDCANCEL:
            EndDialog(h, f->importiert ? 1 : 0);
            return TRUE;
        default:
            break;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(h, f->importiert ? 1 : 0);
        return TRUE;
    case WM_DESTROY:
        f->pal.Frei();
        if (f->fontFett != nullptr) { DeleteObject(f->fontFett); f->fontFett = nullptr; }
        return FALSE;
    default:
        break;
    }
    return FALSE;
}

// Keine Ausnahme darf in Windows' Nachrichtenschleife und damit in Max gelangen.
INT_PTR CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    try {
        return Verarbeite(h, msg, wp, lp);
    } catch (const std::exception& x) {
        if (Fenster* f = Zustand(h)) Status(*f, L"Internal error: " + tfu::Breit(x.what()), true);
    } catch (...) {
        if (Fenster* f = Zustand(h)) Status(*f, L"Internal error.", true);
    }
    return FALSE;
}

} // namespace

int OeffneFenster() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Fenster f;
    const INT_PTR r = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_FIGUREN), GetCOREInterface()->GetMAXHWnd(), &DlgProc,
                                      reinterpret_cast<LPARAM>(&f));
    if (co == S_OK || co == S_FALSE) CoUninitialize();
    return r == -1 ? -1 : (r == 1 ? 1 : 0);
}

} // namespace tfu2
