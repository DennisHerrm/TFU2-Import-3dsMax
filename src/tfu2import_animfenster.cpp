// ============================================================
//  TFU2 Import - das Animationsfenster (modal)
//
//  Aufbau wie beim SWBF2 Import: oben Figur, Auswahl und Suche, dann
//  die Clipliste mit Kopfzeile (virtuell, LBS_NODATA), Statuszeile,
//  Sequenzen der Zeitleiste und unten Abstand, Notizspur, Wurzel-
//  bewegung, "Load all to timeline", "Load", "Close".
//
//  Show:
//    Own clips      was das Spiel der Figur zuordnet (Chore-Sets ihrer
//                   Actors, ChoreData-Ordner mit ihrem Namen, Zwischen-
//                   sequenzen mit ihrem Namen) und zu ihrem Skelett passt
//    Same rig       Clips im Ordner ihres Rigs, die zum Skelett passen
//    Fits skeleton  alle Clips, deren Knochen das Skelett hat
//    All            ohne Filter
//  Gibt es keine eigenen Clips, faellt die Auswahl auf "Same rig" zurueck,
//  dann auf "Fits skeleton".
// ============================================================
#include "tfu2import.h"
#include "tfu2import_res.h"
#include "tfu2_ui.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

extern HINSTANCE hInstance;

namespace tfu2 {

namespace {

const wchar_t* const kZeige[] = { L"Own clips", L"Same rig", L"Fits skeleton", L"All" };

struct Fenster {
    HWND h = nullptr;
    tfu2ui::Palette pal;
    HFONT fontNormal = nullptr, fontFett = nullptr;
    int zeilenHoehe = 16;
    const tfu::Pakete* pakete = nullptr;
    const tfu::Katalog* katalog = nullptr;
    int spiel = 0;                 // Spiel, dessen Katalog geladen ist (das der gewaehlten Figur)
    std::vector<SzenenFigur> figuren;
    int figurWahl = -1;
    int zeige = 0;
    std::vector<size_t> sichtbar;
    std::map<std::string, std::vector<size_t>> eigeneJeFigur;   // gto -> eigene Clips (Cache)
    std::vector<Sequenz> sequenzen;
    bool notiz = true;
    bool wurzel = true;
    bool vorn = true;
    std::wstring status;
    bool statusFehler = false;
};

Fenster* Zustand(HWND h) { return reinterpret_cast<Fenster*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

void Status(Fenster& f, const std::wstring& t, bool fehler = false) {
    f.status = t;
    f.statusFehler = fehler;
    InvalidateRect(GetDlgItem(f.h, IDC_A_STATUS), nullptr, FALSE);
    UpdateWindow(GetDlgItem(f.h, IDC_A_STATUS));
}

std::vector<std::string> Suchwoerter(HWND h) {
    wchar_t puffer[512] = {};
    GetDlgItemTextW(h, IDC_A_SUCHE, puffer, 512);
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

const SzenenFigur* GewaehlteFigur(const Fenster& f) {
    return (f.figurWahl >= 0 && static_cast<size_t>(f.figurWahl) < f.figuren.size()) ? &f.figuren[static_cast<size_t>(f.figurWahl)] : nullptr;
}

const tfu::FigurEintrag* KatalogFigur(const Fenster& f, const SzenenFigur& s) {
    if (f.katalog == nullptr) return nullptr;
    const std::string k = tfu::Klein(s.gto);
    for (const tfu::FigurEintrag& e : f.katalog->figuren) if (tfu::Klein(e.gto) == k) return &e;
    return nullptr;
}

// Katalog des Spiels, aus dem die gewaehlte Figur stammt (TFU1 oder TFU2).
bool LadeSpiel(Fenster& f) {
    const SzenenFigur* sf = GewaehlteFigur(f);
    const int nr = sf != nullptr ? sf->spiel : AktivesSpiel();
    if (nr == f.spiel && f.katalog != nullptr) return true;
    std::string fehler;
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool ok = SpielNr(nr, f.pakete, f.katalog, fehler);
    SetCursor(alt);
    f.eigeneJeFigur.clear();           // Indizes gehoeren zum alten Katalog
    if (!ok) {
        f.pakete = nullptr;
        f.katalog = nullptr;
        f.spiel = 0;
        Status(f, std::wstring(L"No game folder for ") + (nr == 1 ? L"TFU 1" : L"TFU 2") +
                      L" yet - open the character window (TFU2 Import), switch to it and pick the game folder.", true);
        return false;
    }
    f.spiel = nr;
    return true;
}

// ------------------------------------------------------------
//  Liste fuellen
// ------------------------------------------------------------
void Fuelle(Fenster& f) {
    HWND lb = GetDlgItem(f.h, IDC_A_LISTE);
    f.sichtbar.clear();
    std::wstring hinweis;
    if (f.katalog != nullptr) {
        const std::vector<std::string> woerter = Suchwoerter(f.h);
        const SzenenFigur* sf = GewaehlteFigur(f);
        int zeige = f.zeige;
        std::vector<uint32_t> skelett;
        const tfu::FigurEintrag* fe = nullptr;
        if (sf != nullptr) {
            skelett = CrcsInSzene(sf->id);
            fe = KatalogFigur(f, *sf);
        }
        if (zeige != 3 && skelett.empty()) {
            zeige = 3;
            hinweis = L"No TFU character in the scene - showing all clips. Import a character first.";
        }
        const std::vector<std::vector<uint32_t>>* crcs = nullptr;
        if (zeige != 3) {
            HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
            crcs = &f.katalog->AnimCrcs(*f.pakete);
            SetCursor(alt);
        }
        auto passt = [&](size_t i) { return crcs != nullptr && i < crcs->size() && tfu::PasstZu((*crcs)[i], skelett); };
        std::vector<char> nimm(f.katalog->animationen.size(), 0);
        size_t zahl = 0;
        if (zeige == 0 && fe != nullptr) {
            auto it = f.eigeneJeFigur.find(fe->gto);
            if (it == f.eigeneJeFigur.end()) it = f.eigeneJeFigur.emplace(fe->gto, f.katalog->EigeneAnimationen(*f.pakete, *fe)).first;
            for (size_t i : it->second) if (passt(i)) { nimm[i] = 1; ++zahl; }
            if (zahl == 0) { zeige = 1; hinweis = L"The game assigns no clips of its own to this character - showing its rig."; }
        } else if (zeige == 0) {
            zeige = 1;
        }
        if (zeige == 1) {
            if (fe != nullptr)
                for (size_t i = 0; i < nimm.size(); ++i) if (tfu::GleichesRig(*fe, f.katalog->animationen[i]) && passt(i)) { nimm[i] = 1; ++zahl; }
            if (zahl == 0) { zeige = 2; hinweis = L"No folder of its own for this rig - showing every clip that fits the skeleton."; }
        }
        if (zeige == 2) for (size_t i = 0; i < nimm.size(); ++i) nimm[i] = passt(i) ? 1 : 0;
        if (zeige == 3) std::fill(nimm.begin(), nimm.end(), 1);
        for (size_t i = 0; i < nimm.size(); ++i) {
            if (!nimm[i]) continue;
            const std::string k = tfu::Klein(f.katalog->animationen[i].pfad);
            bool alle = true;
            for (const std::string& w : woerter) if (k.find(w) == std::string::npos) { alle = false; break; }
            if (alle) f.sichtbar.push_back(i);
        }
    }
    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_SETCOUNT, static_cast<WPARAM>(f.sichtbar.size()), 0);
    SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, nullptr, TRUE);
    InvalidateRect(GetDlgItem(f.h, IDC_A_KOPF), nullptr, FALSE);
    if (f.katalog != nullptr) {
        std::wstring t = std::to_wstring(f.sichtbar.size()) + L" clips";
        if (const SzenenFigur* sf = GewaehlteFigur(f)) t += L" for " + tfu::Breit(sf->name);
        if (!hinweis.empty()) t += L"  ·  " + hinweis;
        Status(f, t);
    }
}

void FuelleFiguren(Fenster& f) {
    std::string vorher;
    if (const SzenenFigur* sf = GewaehlteFigur(f)) vorher = sf->id;
    f.figuren = FigurenInSzene();
    HWND c = GetDlgItem(f.h, IDC_A_FIGUR);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    f.figurWahl = -1;
    if (f.figuren.empty()) {
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(no TFU character in the scene)"));
        SendMessageW(c, CB_SETCURSEL, 0, 0);
        return;
    }
    for (size_t i = 0; i < f.figuren.size(); ++i) {
        const std::wstring t = tfu::Breit(f.figuren[i].name) + L"  (TFU " + std::to_wstring(f.figuren[i].spiel) + L", " +
                               std::to_wstring(f.figuren[i].knochen) + L" bones)";
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        if (!vorher.empty() && f.figuren[i].id == vorher) f.figurWahl = static_cast<int>(i);
    }
    if (f.figurWahl < 0) f.figurWahl = 0;      // Auswahl in der Szene, sonst die zuletzt importierte
    SendMessageW(c, CB_SETCURSEL, static_cast<WPARAM>(f.figurWahl), 0);
}

void FuelleSequenzen(Fenster& f) {
    HWND c = GetDlgItem(f.h, IDC_A_SEQUENZ);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    if (f.sequenzen.empty()) {
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(no sequences - use \"Load all to timeline\")"));
        SendMessageW(c, CB_SETCURSEL, 0, 0);
        EnableWindow(c, FALSE);
        return;
    }
    int ende = 0;
    for (const Sequenz& s : f.sequenzen) ende = std::max(ende, s.ende);
    const std::wstring alle = L"Whole timeline  (0 - " + std::to_wstring(ende) + L")";
    SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(alle.c_str()));
    for (size_t i = 0; i < f.sequenzen.size(); ++i) {
        const Sequenz& s = f.sequenzen[i];
        const std::wstring t = std::to_wstring(i + 1) + L"   " + tfu::Breit(s.name) + L"  (" + std::to_wstring(s.start) + L" - " +
                               std::to_wstring(s.ende) + L")";
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
    }
    SendMessageW(c, CB_SETCURSEL, 0, 0);
    EnableWindow(c, TRUE);
}

void ZeigeSequenz(Fenster& f) {
    if (f.sequenzen.empty()) return;
    const LRESULT i = SendDlgItemMessageW(f.h, IDC_A_SEQUENZ, CB_GETCURSEL, 0, 0);
    if (i == CB_ERR) return;
    if (i == 0) {
        int ende = 0;
        for (const Sequenz& s : f.sequenzen) ende = std::max(ende, s.ende);
        ZeigeBereich(0, ende);
        return;
    }
    const Sequenz& s = f.sequenzen[static_cast<size_t>(i - 1)];
    ZeigeBereich(s.start, s.ende);
    Status(f, L"Sequence " + std::to_wstring(i) + L": " + tfu::Breit(s.name) + L"  (" + std::to_wstring(s.start) + L" - " +
                  std::to_wstring(s.ende) + L")");
}

// ------------------------------------------------------------
//  Laden
// ------------------------------------------------------------
std::string FigurId(const Fenster& f) {
    const SzenenFigur* sf = GewaehlteFigur(f);
    return sf ? sf->id : std::string();
}

void Laden(Fenster& f) {
    if (f.katalog == nullptr) return;
    const LRESULT sel = SendDlgItemMessageW(f.h, IDC_A_LISTE, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || static_cast<size_t>(sel) >= f.sichtbar.size()) { Status(f, L"Select a clip first."); return; }
    const tfu::AnimEintrag& a = f.katalog->animationen[f.sichtbar[static_cast<size_t>(sel)]];
    std::wstring bericht;
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool ok = WendeAnimationAn(*f.pakete, a.pfad, f.wurzel, f.vorn, FigurId(f), bericht);
    SetCursor(alt);
    Status(f, bericht, !ok);
    if (ok) {
        f.sequenzen.clear();               // die Notizspur ist geloescht
        FuelleSequenzen(f);
    }
}

void AlleLaden(Fenster& f) {
    if (f.katalog == nullptr) return;
    if (f.sichtbar.empty()) { Status(f, L"No clips in the list."); return; }
    BOOL ok = FALSE;
    int abstand = static_cast<int>(GetDlgItemInt(f.h, IDC_A_ABSTAND, &ok, FALSE));
    if (!ok || abstand < 0) abstand = 10;
    abstand = std::min(abstand, 1000);
    if (f.sichtbar.size() > 50) {
        const SzenenFigur* sf = GewaehlteFigur(f);
        wchar_t frage[400];
        swprintf(frage, 400, L"Load %zu clips one after another onto %ls (gap %d frames)?\n\nThe timeline starts at frame 0 with the bind pose.",
                 f.sichtbar.size(), sf ? tfu::Breit(sf->name).c_str() : L"the skeleton", abstand);
        if (MessageBoxW(f.h, frage, L"TFU2 Animations", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    std::vector<std::string> pfade;
    for (size_t i : f.sichtbar) pfade.push_back(f.katalog->animationen[i].pfad);
    SchreibeEinstellung(L"Abstand", std::to_wstring(abstand));
    Status(f, L"Setting keys for " + std::to_wstring(pfade.size()) + L" clips… (Esc cancels)");
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring bericht;
    std::vector<Sequenz> plan;
    const bool gut = WendeFolgeAn(*f.pakete, pfade, abstand, f.notiz, f.wurzel, f.vorn, FigurId(f), plan, bericht);
    SetCursor(alt);
    Status(f, bericht, !gut);
    if (gut) {
        f.sequenzen = plan;
        FuelleSequenzen(f);
    }
}

// ------------------------------------------------------------
//  Zeichnen
// ------------------------------------------------------------
struct Spalten { int name, bilder, ordner, ende; };

Spalten SpaltenFuer(const RECT& r, int rand) {
    const int b = static_cast<int>(r.right - r.left) - 2 * rand;
    Spalten s{};
    s.name = r.left + rand;
    s.ordner = r.left + rand + b * 58 / 100;
    s.bilder = s.ordner - b * 10 / 100;
    s.ende = r.right - rand;
    return s;
}

void Text(HDC dc, const std::wstring& t, int links, int rechts, const RECT& r, UINT ausrichtung) {
    RECT z = { links, r.top, rechts, r.bottom };
    DrawTextW(dc, t.c_str(), -1, &z, ausrichtung | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void ZeichneKopf(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    FillRect(dc, &r, f.pal.pinselGrund);
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const Spalten s = SpaltenFuer(r, rand);
    HGDIOBJ alt = SelectObject(dc, f.fontFett != nullptr ? f.fontFett : f.fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, f.pal.dim);
    Text(dc, L"Name", s.name, s.bilder - rand, r, DT_LEFT);
    Text(dc, L"Frames", s.bilder, s.ordner - 2 * rand, r, DT_RIGHT);
    Text(dc, L"Folder", s.ordner, s.ende, r, DT_LEFT);
    SelectObject(dc, alt);
}

void ZeichneZeile(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    const bool sel = (d.itemState & ODS_SELECTED) != 0;
    HBRUSH hg = CreateSolidBrush(sel ? f.pal.auswahl : f.pal.feld);
    FillRect(dc, &r, hg);
    DeleteObject(hg);
    if (f.katalog == nullptr || d.itemID == static_cast<UINT>(-1) || d.itemID >= f.sichtbar.size()) return;
    const tfu::AnimEintrag& a = f.katalog->animationen[f.sichtbar[d.itemID]];
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const Spalten s = SpaltenFuer(r, rand);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ alt = SelectObject(dc, f.fontNormal);
    SetTextColor(dc, sel ? f.pal.auswahlText : f.pal.feldText);
    Text(dc, tfu::Breit(a.name), s.name, s.bilder - rand, r, DT_LEFT);
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    Text(dc, a.bilder > 0 ? std::to_wstring(a.bilder) : std::wstring(L"-"), s.bilder, s.ordner - 2 * rand, r, DT_RIGHT);
    Text(dc, tfu::Breit(a.gruppe), s.ordner, s.ende, r, DT_LEFT);
    SelectObject(dc, alt);
    if (!sel) tfu2ui::Trennlinie(f.pal, dc, r, rand);
}

INT_PTR Verarbeite(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Fenster* f = Zustand(h);
    switch (msg) {
    case WM_INITDIALOG: {
        f = reinterpret_cast<Fenster*>(lp);
        f->h = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(f));
        f->pal.Baue(&ThemeFarbe);
        f->fontNormal = reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0));
        LOGFONTW lf{};
        if (f->fontNormal != nullptr && GetObjectW(f->fontNormal, sizeof(lf), &lf) == sizeof(lf)) {
            lf.lfWeight = FW_SEMIBOLD;
            f->fontFett = CreateFontIndirectW(&lf);
        }
        f->zeilenHoehe = tfu2ui::ZeilenHoehe(h);
        tfu2ui::DunkleTitelleiste(h, f->pal.dunkel);
        if (f->pal.dunkel) {
            SetWindowTheme(GetDlgItem(h, IDC_A_LISTE), L"DarkMode_Explorer", nullptr);
            for (int id : { IDC_A_FIGUR, IDC_A_ZEIGE, IDC_A_SEQUENZ }) SetWindowTheme(GetDlgItem(h, id), L"DarkMode_CFD", nullptr);
        }
        SendDlgItemMessageW(h, IDC_A_LISTE, LB_SETITEMHEIGHT, 0, f->zeilenHoehe + 6);
        SetWindowTextW(h, (std::wstring(L"TFU2 Animations ") + TFU2IMPORT_VERSION_STR).c_str());
        const std::wstring abstand = LiesEinstellung(L"Abstand");
        SetDlgItemInt(h, IDC_A_ABSTAND, abstand.empty() ? 10 : static_cast<UINT>(_wtoi(abstand.c_str())), FALSE);
        f->notiz = LiesEinstellung(L"Notizspur") != L"0";
        f->wurzel = LiesEinstellung(L"Wurzelbewegung") != L"0";
        f->vorn = LiesEinstellung(L"NachVorn") != L"0";
        f->zeige = std::clamp(_wtoi(LiesEinstellung(L"Zeige").c_str()), 0, 3);
        for (const wchar_t* t : kZeige) SendDlgItemMessageW(h, IDC_A_ZEIGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        SendDlgItemMessageW(h, IDC_A_ZEIGE, CB_SETCURSEL, static_cast<WPARAM>(f->zeige), 0);
        SendDlgItemMessageW(h, IDC_A_SUCHE, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g.  idle  or  saber"));
        LiesSequenzen(f->sequenzen);
        FuelleSequenzen(*f);
        FuelleFiguren(*f);
        if (LadeSpiel(*f)) Fuelle(*f);
        return TRUE;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* m = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        const int zh = (f != nullptr) ? f->zeilenHoehe : tfu2ui::ZeilenHoehe(h);
        m->itemHeight = static_cast<UINT>(m->CtlType == ODT_COMBOBOX ? zh + 4 : zh + 6);
        return TRUE;
    }
    case WM_DRAWITEM: {
        if (f == nullptr) break;
        const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        switch (d->CtlID) {
        case IDC_A_LISTE: ZeichneZeile(*f, *d); return TRUE;
        case IDC_A_KOPF: ZeichneKopf(*f, *d); return TRUE;
        case IDC_A_STATUS: tfu2ui::ZeichneStatus(f->pal, f->fontNormal, *d, f->status, f->statusFehler); return TRUE;
        case IDC_A_FIGUR:
        case IDC_A_ZEIGE:
        case IDC_A_SEQUENZ: tfu2ui::ZeichneAuswahlfeld(f->pal, f->fontNormal, *d); return TRUE;
        case IDC_A_NOTIZ: tfu2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, f->notiz); return TRUE;
        case IDC_A_WURZEL: tfu2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, f->wurzel); return TRUE;
        case IDC_A_VORN: tfu2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, f->vorn); return TRUE;
        case IDC_A_LADEN: tfu2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, true); return TRUE;
        default: tfu2ui::ZeichneKnopf(f->pal, f->fontNormal, *d, false); return TRUE;
        }
    }
    case WM_COMMAND: {
        if (f == nullptr) break;
        const int code = HIWORD(wp);
        switch (LOWORD(wp)) {
        case IDC_A_SUCHE:
            if (code == EN_CHANGE) Fuelle(*f);
            return TRUE;
        case IDC_A_FIGUR:
            if (code == CBN_DROPDOWN) FuelleFiguren(*f);      // Szene kann sich geaendert haben
            if (code == CBN_SELCHANGE) {
                f->figurWahl = static_cast<int>(SendDlgItemMessageW(h, IDC_A_FIGUR, CB_GETCURSEL, 0, 0));
                if (f->figurWahl >= static_cast<int>(f->figuren.size())) f->figurWahl = -1;
                LadeSpiel(*f);         // die Figur kann aus dem anderen Spiel stammen
                Fuelle(*f);
            }
            return TRUE;
        case IDC_A_ZEIGE:
            if (code == CBN_SELCHANGE) {
                f->zeige = static_cast<int>(SendDlgItemMessageW(h, IDC_A_ZEIGE, CB_GETCURSEL, 0, 0));
                SchreibeEinstellung(L"Zeige", std::to_wstring(f->zeige));
                Fuelle(*f);
            }
            return TRUE;
        case IDC_A_SEQUENZ:
            if (code == CBN_SELCHANGE) ZeigeSequenz(*f);
            return TRUE;
        case IDC_A_NOTIZ:
            f->notiz = !f->notiz;
            SchreibeEinstellung(L"Notizspur", f->notiz ? L"1" : L"0");
            InvalidateRect(GetDlgItem(h, IDC_A_NOTIZ), nullptr, TRUE);
            return TRUE;
        case IDC_A_WURZEL:
            f->wurzel = !f->wurzel;
            SchreibeEinstellung(L"Wurzelbewegung", f->wurzel ? L"1" : L"0");
            InvalidateRect(GetDlgItem(h, IDC_A_WURZEL), nullptr, TRUE);
            return TRUE;
        case IDC_A_VORN:
            f->vorn = !f->vorn;
            SchreibeEinstellung(L"NachVorn", f->vorn ? L"1" : L"0");
            InvalidateRect(GetDlgItem(h, IDC_A_VORN), nullptr, TRUE);
            return TRUE;
        case IDC_A_LISTE:
            if (code == LBN_DBLCLK) Laden(*f);
            return TRUE;
        case IDC_A_LADEN: Laden(*f); return TRUE;
        case IDC_A_ALLE: AlleLaden(*f); return TRUE;
        case IDOK: Laden(*f); return TRUE;
        case IDCANCEL: EndDialog(h, 1); return TRUE;
        default: break;
        }
        break;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (f != nullptr && f->pal.pinselGrund != nullptr) {
            SetTextColor(reinterpret_cast<HDC>(wp), f->pal.text);
            SetBkColor(reinterpret_cast<HDC>(wp), f->pal.grund);
            return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (f != nullptr && f->pal.pinselFeld != nullptr) {
            SetTextColor(reinterpret_cast<HDC>(wp), f->pal.feldText);
            SetBkColor(reinterpret_cast<HDC>(wp), f->pal.feld);
            return reinterpret_cast<INT_PTR>(f->pal.pinselFeld);
        }
        break;
    case WM_PAINT: {
        if (f == nullptr) break;
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        for (int id : { IDC_A_SUCHE, IDC_A_ABSTAND, IDC_A_LISTE }) tfu2ui::Kante(h, dc, id, f->pal.linie);
        EndPaint(h, &ps);
        return TRUE;
    }
    case WM_DESTROY:
        if (f != nullptr) {
            if (f->fontFett != nullptr) DeleteObject(f->fontFett);
            f->pal.Frei();
        }
        break;
    default:
        break;
    }
    return FALSE;
}

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

int OeffneAnimFenster(HWND eltern) {
    Fenster f;
    const INT_PTR r = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_ANIMATIONEN),
                                      eltern != nullptr ? eltern : GetCOREInterface()->GetMAXHWnd(), &DlgProc, reinterpret_cast<LPARAM>(&f));
    return r == -1 ? -1 : 1;
}

} // namespace tfu2
