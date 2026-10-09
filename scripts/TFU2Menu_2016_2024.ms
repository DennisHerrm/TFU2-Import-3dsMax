-- ============================================================
--  TFU2Menu_2016_2024.ms - Menueeintrag fuer 3ds Max 2016 bis 2024
--  (altes menuMan-API, laeuft als Post-Start-up-Skript).
--  Ab 2025 gilt TFU2Menu_2025_2027.ms; die PackageContents.xml gibt
--  jeder Max-Fassung nur ihr Skript, sonst entstuende das Menue doppelt.
--  Ergebnis: Hauptmenue -> "TFU Tool" -> "Import TFU" / "TFU Animations"
--  Bis 0.3.0 hiess das Menue "TFU2 Tool"; Max speichert Menues in der
--  Oberflaechendatei, das alte wird deshalb hier entfernt.
-- ============================================================
(
  local sMenuName = "TFU Tool"
  local mnuBar = menuMan.getMainMenuBar()
  local mnuAlt = menuMan.findMenu "TFU2 Tool"
  if (mnuAlt != undefined) then
  (
    for i = mnuBar.numItems() to 1 by -1 do
    (
      local itm = mnuBar.getItem i
      if (itm.getTitle() == "TFU2 Tool") then mnuBar.removeItemByPosition i
    )
    menuMan.unRegisterMenu mnuAlt
  )
  local mnu = menuMan.findMenu sMenuName
  if (mnu == undefined) then
  (
    mnu = menuMan.createMenu sMenuName
    local itmSub = menuMan.createSubMenuItem sMenuName mnu
    mnuBar.addItem itmSub (mnuBar.numItems())
  )
  for aItem in #(#("Import TFU", "TFU2Import_Open"), #("TFU Animations", "TFU2Import_Anim")) do
  (
    local bFound = false
    for i = 1 to mnu.numItems() while (not bFound) do
      if ((mnu.getItem i).getTitle() == aItem[1]) then bFound = true
    if (not bFound) then
    (
      local act = menuMan.createActionItem aItem[2] "TFU Tool"
      act.setTitle aItem[1]
      act.setUseCustomTitle true
      mnu.addItem act -1
    )
  )
  menuMan.updateMenuBar()
)
