-- ============================================================
--  TFU2Menu_2016_2024.ms - Menueeintrag fuer 3ds Max 2016 bis 2024
--  (altes menuMan-API, laeuft als Post-Start-up-Skript).
--  Ab 2025 gilt TFU2Menu_2025_2027.ms; die PackageContents.xml gibt
--  jeder Max-Fassung nur ihr Skript, sonst entstuende das Menue doppelt.
--  Ergebnis: Hauptmenue -> "TFU2 Tool" -> "Import TFU2"
-- ============================================================
(
  local sMenuName = "TFU2 Tool"
  local mnu = menuMan.findMenu sMenuName
  if (mnu == undefined) then
  (
    local mnuBar = menuMan.getMainMenuBar()
    mnu = menuMan.createMenu sMenuName
    local itmSub = menuMan.createSubMenuItem sMenuName mnu
    mnuBar.addItem itmSub (mnuBar.numItems())
  )
  local bFound = false
  for i = 1 to mnu.numItems() while (not bFound) do
    if ((mnu.getItem i).getTitle() == "Import TFU2") then bFound = true
  if (not bFound) then
  (
    local act = menuMan.createActionItem "TFU2Import_Open" "TFU2 Tool"
    act.setTitle "Import TFU2"
    act.setUseCustomTitle true
    mnu.addItem act -1
  )
  menuMan.updateMenuBar()
)
