-- ============================================================
--  TFU2Menu_2025_2027.ms - Menueeintrag ab 3ds Max 2025
--
--  Ab 2025 entstehen Menues nur im Callback #cuiRegisterMenus
--  (CuiMenuManager). Laeuft als PRE-Start-up-Skript, sonst meldet
--  sich der Callback zu spaet an (Lehre aus dem SWBF2 Import).
--  647394 = Action-Table der MacroScripts; permanentId = Makro`Kategorie.
--  Eigene GUIDs - sie duerfen mit keinem anderen Paket kollidieren.
-- ============================================================
(
  local sSubMenuGuid = "AC158C74-7091-4149-9C5E-F42D5831F33A"
  local sActionGuid  = "2BD1BC8D-94BD-494A-9334-A839EF34F24E"
  local sAnimGuid    = "2C4084D8-84BC-4483-B709-D232F6365E30"

  fn TFU2RegisterMenu =
  (
    local mgr = callbacks.notificationParam()
    if (mgr != undefined) then
    (
      try
      (
        local mnuSub = mgr.mainMenuBar.CreateSubMenu sSubMenuGuid "TFU2 Tool"
        if (mnuSub != undefined) then
        (
          mnuSub.CreateAction sActionGuid 647394 "TFU2Import_Open`TFU2 Tool"
          mnuSub.CreateAction sAnimGuid 647394 "TFU2Import_Anim`TFU2 Tool"
        )
        else
          format "[TFU2] CreateSubMenu returned undefined - GUID already taken?\n"
      )
      catch (format "[TFU2] menu could not be created: %\n" (getCurrentException()))
    )
  )

  callbacks.removeScripts id:#tfu2ImportMenu
  callbacks.addScript #cuiRegisterMenus TFU2RegisterMenu id:#tfu2ImportMenu
)
