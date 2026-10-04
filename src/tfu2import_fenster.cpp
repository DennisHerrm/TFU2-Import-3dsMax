// ============================================================
//  TFU2 Import - das Fenster
//
//  Links die Figuren des Spiels, rechts die Animationen. Das Fenster
//  ist nicht modal: Max bleibt bedienbar, man kann eine Animation
//  anlegen und gleich in der Zeitleiste abspielen.
// ============================================================
#include "tfu2import.h"
#include "tfu2import_res.h"

#include <shlobj.h>
#include <windowsx.h>

#include <string>
#include <vector>

extern HINSTANCE hInstance;

namespace tfu2 {

namespace {

HWND g_fenster = nullptr;
const tfu::Pakete* g_pakete = nullptr;
const tfu::Katalog* g_katalog = nullptr;

std::wstring Text(HWND d, int id) {
    wchar_t b[1024] = {};
    GetDlgItemTextW(d, id, b, 1024);
    return b;
}

void Status(HWND d, const std::wstring& t) { SetDlgItemTextW(d, IDC_STATUS, t.c_str()); }

bool Passt(const std::string& klein, const std::string& filter) {
    if (filter.empty()) return true;
    // Mehrere Woerter: alle muessen vorkommen.
    size_t p = 0;
    while (p < filter.size()) {
        size_t e = filter.find(' ', p);
        if (e == std::string::npos) e = filter.size();
        const std::string wort = filter.substr(p, e - p);
        if (!wort.empty() && klein.find(wort) == std::string::npos) return false;
        p = e + 1;
    }
    return true;
}

// Die Figur, zu der die Animationen passen sollen: die in der Szene (Auswahl
// oder zuletzt importiert), sonst die links gewaehlte (deren GTO wird dafuer
// kurz gelesen). Liefert ihren Katalogeintrag (oder nullptr) und ihr Skelett.
const tfu::FigurEintrag* ZielFigur(HWND d, std::vector<uint32_t>& skelett) {
    if (g_katalog == nullptr) return nullptr;
    std::string gto;
    skelett = CrcsInSzene(&gto);
    if (!skelett.empty()) {
        const std::string k = tfu::Klein(gto);
        for (const tfu::FigurEintrag& f : g_katalog->figuren) if (tfu::Klein(f.gto) == k) return &f;
        return nullptr;   // lose .gto: nur Skelett-Abgleich moeglich
    }
    static size_t gemerkt = static_cast<size_t>(-1);
    static std::vector<uint32_t> gemerktCrcs;
    HWND l = GetDlgItem(d, IDC_FIGUREN);
    const int sel = ListBox_GetCurSel(l);
    if (sel < 0) return nullptr;
    const size_t i = static_cast<size_t>(ListBox_GetItemData(l, sel));
    if (i >= g_katalog->figuren.size()) return nullptr;
    if (i != gemerkt) {
        gemerktCrcs = tfu::ModellCrcs(*g_pakete, g_katalog->figuren[i].gto);
        gemerkt = i;
    }
    skelett = gemerktCrcs;
    return &g_katalog->figuren[i];
}

void FuelleFiguren(HWND d) {
    HWND l = GetDlgItem(d, IDC_FIGUREN);
    SendMessageW(l, WM_SETREDRAW, FALSE, 0);
    ListBox_ResetContent(l);
    size_t zahl = 0;
    if (g_katalog != nullptr) {
        const std::string filter = tfu::Klein(tfu::Utf8(Text(d, IDC_FFILTER)));
        const bool statisch = IsDlgButtonChecked(d, IDC_STATISCH) == BST_CHECKED;
        for (size_t i = 0; i < g_katalog->figuren.size(); ++i) {
            const tfu::FigurEintrag& f = g_katalog->figuren[i];
            if (!f.skelett && !statisch) continue;
            if (!Passt(tfu::Klein(f.gto), filter)) continue;
            const std::wstring z = tfu::Breit(f.name + "   (" + f.rig + ")");
            const int pos = ListBox_AddString(l, z.c_str());
            ListBox_SetItemData(l, pos, static_cast<LPARAM>(i));
            ++zahl;
        }
    }
    SendMessageW(l, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(l, nullptr, TRUE);
    SetDlgItemTextW(d, IDC_FZAHL, (std::to_wstring(zahl) + L" characters").c_str());
}

void FuelleAnims(HWND d) {
    HWND l = GetDlgItem(d, IDC_ANIMS);
    SendMessageW(l, WM_SETREDRAW, FALSE, 0);
    ListBox_ResetContent(l);
    size_t zahl = 0;
    std::wstring rigText;
    if (g_katalog != nullptr) {
        const std::string filter = tfu::Klein(tfu::Utf8(Text(d, IDC_AFILTER)));
        // 0 = gleiches Rig (Ordner), 1 = passt zum Skelett, 2 = alle
        int art = static_cast<int>(SendDlgItemMessageW(d, IDC_AART, CB_GETCURSEL, 0, 0));
        std::vector<uint32_t> skelett;
        const tfu::FigurEintrag* figur = (art == 2) ? nullptr : ZielFigur(d, skelett);
        const std::vector<std::vector<uint32_t>>* clipCrcs = nullptr;
        if (art != 2 && skelett.empty()) {
            art = 2;
            rigText = L"  (pick a character to filter)";
        }
        if (art != 2) {
            HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
            clipCrcs = &g_katalog->AnimCrcs(*g_pakete);
            SetCursor(alt);
        }
        auto passt = [&](size_t i) { return i < clipCrcs->size() && tfu::PasstZu((*clipCrcs)[i], skelett); };
        if (art == 0) {
            // Ordner des Rigs/der Figur UND passendes Skelett. Ohne eigenen
            // Ordner (Yoda, Terror-Giant ...) Rueckfall auf den Skelett-Abgleich.
            bool eigene = false;
            if (figur != nullptr)
                for (size_t i = 0; i < g_katalog->animationen.size() && !eigene; ++i)
                    eigene = tfu::GleichesRig(*figur, g_katalog->animationen[i]) && passt(i);
            if (!eigene) art = 1;
            else rigText = L"  rig of " + tfu::Breit(figur->name);
        }
        if (art == 1) rigText = L"  fitting the skeleton";
        for (size_t i = 0; i < g_katalog->animationen.size(); ++i) {
            const tfu::AnimEintrag& a = g_katalog->animationen[i];
            const std::string k = tfu::Klein(a.pfad);
            if (art == 0 && !(tfu::GleichesRig(*figur, a) && passt(i))) continue;
            if (art == 1 && !passt(i)) continue;
            if (!Passt(k, filter)) continue;
            const std::wstring z = tfu::Breit(a.name + "   [" + a.gruppe + "]");
            const int pos = ListBox_AddString(l, z.c_str());
            ListBox_SetItemData(l, pos, static_cast<LPARAM>(i));
            ++zahl;
        }
    }
    SendMessageW(l, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(l, nullptr, TRUE);
    SetDlgItemTextW(d, IDC_AZAHL, (std::to_wstring(zahl) + L" animations" + rigText).c_str());
}

void Lade(HWND d) {
    std::string fehler;
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool ok = Spiel(Text(d, IDC_ORDNER), g_pakete, g_katalog, fehler);
    SetCursor(alt);
    if (!ok) {
        g_pakete = nullptr;
        g_katalog = nullptr;
        Status(d, L"Could not load the game: " + tfu::Breit(fehler));
    } else {
        SetDlgItemTextW(d, IDC_ORDNER, g_pakete->Ordner().c_str());
        Status(d, L"Game loaded: " + std::to_wstring(g_pakete->Eintraege().size()) + L" files in " +
                      std::to_wstring(g_pakete->PaketZahl()) + L" packs, " + std::to_wstring(g_katalog->figuren.size()) +
                      L" characters, " + std::to_wstring(g_katalog->animationen.size()) + L" animations.");
    }
    FuelleFiguren(d);
    FuelleAnims(d);
}

void Durchsuchen(HWND d) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = d;
    bi.lpszTitle = L"Select the Star Wars The Force Unleashed 2 folder (the one with SWTFU2.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (id == nullptr) return;
    wchar_t pfad[MAX_PATH] = {};
    if (SHGetPathFromIDListW(id, pfad)) {
        SetDlgItemTextW(d, IDC_ORDNER, pfad);
        Lade(d);
    }
    CoTaskMemFree(id);
}

void Importiere(HWND d) {
    if (g_katalog == nullptr) { Status(d, L"Load the game first."); return; }
    HWND l = GetDlgItem(d, IDC_FIGUREN);
    const int sel = ListBox_GetCurSel(l);
    if (sel < 0) { Status(d, L"Pick a character on the left."); return; }
    const size_t i = static_cast<size_t>(ListBox_GetItemData(l, sel));
    if (i >= g_katalog->figuren.size()) return;
    ImportOptionen o;
    o.texturen = IsDlgButtonChecked(d, IDC_TEXTUREN) == BST_CHECKED;
    SchreibeEinstellung(L"Texturen", o.texturen ? L"1" : L"0");
    Status(d, L"Importing " + tfu::Breit(g_katalog->figuren[i].name) + L" ...");
    UpdateWindow(d);
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring bericht;
    const bool ok = ImportiereFigur(*g_pakete, g_katalog->figuren[i], o, bericht);
    SetCursor(alt);
    Status(d, (ok ? L"Imported " : L"Import failed: ") + bericht);
    FuelleAnims(d);
}

void WendeAn(HWND d) {
    if (g_katalog == nullptr) { Status(d, L"Load the game first."); return; }
    HWND l = GetDlgItem(d, IDC_ANIMS);
    const int sel = ListBox_GetCurSel(l);
    if (sel < 0) { Status(d, L"Pick an animation on the right."); return; }
    const size_t i = static_cast<size_t>(ListBox_GetItemData(l, sel));
    if (i >= g_katalog->animationen.size()) return;
    const bool wurzel = IsDlgButtonChecked(d, IDC_WURZEL) == BST_CHECKED;
    SchreibeEinstellung(L"Wurzelbewegung", wurzel ? L"1" : L"0");
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring bericht;
    const bool ok = WendeAnimationAn(*g_pakete, g_katalog->animationen[i].pfad, wurzel, bericht);
    SetCursor(alt);
    Status(d, (ok ? L"Animation: " : L"") + bericht);
}

INT_PTR CALLBACK DlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_fenster = d;
        GetCOREInterface()->RegisterDlgWnd(d);
        SetDlgItemTextW(d, IDC_ORDNER, LiesEinstellung(L"Spielordner").c_str());
        const std::wstring tex = LiesEinstellung(L"Texturen"), wurz = LiesEinstellung(L"Wurzelbewegung");
        CheckDlgButton(d, IDC_TEXTUREN, tex == L"0" ? BST_UNCHECKED : BST_CHECKED);
        CheckDlgButton(d, IDC_WURZEL, wurz == L"0" ? BST_UNCHECKED : BST_CHECKED);
        {
            HWND c = GetDlgItem(d, IDC_AART);
            SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Same rig"));
            SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fits skeleton"));
            SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All"));
            const std::wstring art = LiesEinstellung(L"AnimFilter");
            SendMessageW(c, CB_SETCURSEL, (art == L"1") ? 1 : (art == L"2") ? 2 : 0, 0);
        }
        if (!Text(d, IDC_ORDNER).empty()) Lade(d);
        else Status(d, L"Pick the game folder (the one with SWTFU2.exe and LevelPacks).");
        (void)lp;
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_DURCHSUCHEN: Durchsuchen(d); return TRUE;
        case IDC_LADEN: Lade(d); return TRUE;
        case IDC_IMPORT: Importiere(d); return TRUE;
        case IDC_ANWENDEN: WendeAn(d); return TRUE;
        case IDC_AART:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                SchreibeEinstellung(L"AnimFilter", std::to_wstring(SendDlgItemMessageW(d, IDC_AART, CB_GETCURSEL, 0, 0)));
                FuelleAnims(d);
            }
            return TRUE;
        case IDC_STATISCH: FuelleFiguren(d); return TRUE;
        case IDC_FFILTER: if (HIWORD(wp) == EN_CHANGE) FuelleFiguren(d); return TRUE;
        case IDC_AFILTER: if (HIWORD(wp) == EN_CHANGE) FuelleAnims(d); return TRUE;
        case IDC_FIGUREN:
            if (HIWORD(wp) == LBN_DBLCLK) Importiere(d);
            else if (HIWORD(wp) == LBN_SELCHANGE && CrcsInSzene().empty()) FuelleAnims(d);
            return TRUE;
        case IDC_ANIMS: if (HIWORD(wp) == LBN_DBLCLK) WendeAn(d); return TRUE;
        case IDCANCEL: DestroyWindow(d); return TRUE;
        default: break;
        }
        break;
    case WM_ACTIVATE:
        // Zurueck im Fenster: die Figur in der Szene kann eine andere sein.
        if (LOWORD(wp) != WA_INACTIVE && g_katalog != nullptr &&
            SendDlgItemMessageW(d, IDC_AART, CB_GETCURSEL, 0, 0) != 2) FuelleAnims(d);
        return FALSE;
    case WM_CLOSE: DestroyWindow(d); return TRUE;
    case WM_DESTROY:
        GetCOREInterface()->UnRegisterDlgWnd(d);
        g_fenster = nullptr;
        return TRUE;
    default: break;
    }
    return FALSE;
}

} // namespace

int OeffneFenster() {
    if (g_fenster != nullptr) {
        ShowWindow(g_fenster, SW_SHOW);
        SetForegroundWindow(g_fenster);
        // Ordner kann sich geaendert haben (Datei -> Importieren mit einer .lp)
        const std::wstring o = LiesEinstellung(L"Spielordner");
        if (!o.empty() && _wcsicmp(o.c_str(), Text(g_fenster, IDC_ORDNER).c_str()) != 0) {
            SetDlgItemTextW(g_fenster, IDC_ORDNER, o.c_str());
            Lade(g_fenster);
        } else {
            FuelleAnims(g_fenster);   // die Figur in der Szene kann eine andere sein
        }
        return 1;
    }
    HWND h = CreateDialogParamW(hInstance, MAKEINTRESOURCEW(IDD_TFU2), GetCOREInterface()->GetMAXHWnd(), DlgProc, 0);
    if (h == nullptr) return -1;
    ShowWindow(h, SW_SHOW);
    return 1;
}

} // namespace tfu2
