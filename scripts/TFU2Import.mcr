-- ============================================================
--  TFU2Import.mcr - Menue "TFU2 Tool"
--    "Import TFU2"       das Figurenfenster   (Tfu2Cpp.showDialog)
--    "TFU2 Animations"   das Animationsfenster (Tfu2Cpp.showAnimDialog)
-- ============================================================

macroScript TFU2Import_Open
    category:"TFU2 Tool"
    buttonText:"Import TFU2"
    toolTip:"Star Wars: The Force Unleashed I + II - import a character straight from the game"
(
    on execute do
    (
        local oTfu = undefined
        try (if (Tfu2Cpp != undefined) then oTfu = Tfu2Cpp) catch (oTfu = undefined)
        if (oTfu == undefined) then
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*Tfu2Cpp*" do oTfu = i) catch ()
        if (oTfu != undefined) then
            oTfu.showDialog()
        else
            messageBox "TFU2Import.dlu is not loaded.\n\nPlease run INSTALLIERE.bat again and restart 3ds Max." title:"TFU2 Tool"
    )
)

macroScript TFU2Import_Anim
    category:"TFU2 Tool"
    buttonText:"TFU2 Animations"
    toolTip:"Star Wars: The Force Unleashed I + II - load animations onto the character in the scene"
(
    on execute do
    (
        local oTfu = undefined
        try (if (Tfu2Cpp != undefined) then oTfu = Tfu2Cpp) catch (oTfu = undefined)
        if (oTfu == undefined) then
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*Tfu2Cpp*" do oTfu = i) catch ()
        if (oTfu != undefined) then
            oTfu.showAnimDialog()
        else
            messageBox "TFU2Import.dlu is not loaded.\n\nPlease run INSTALLIERE.bat again and restart 3ds Max." title:"TFU2 Tool"
    )
)
