-- ============================================================
--  TFU2Import.mcr - Menue "TFU2 Tool" -> "Import TFU2"
--  Oeffnet das Fenster aus der .dlu ueber Tfu2Cpp.showDialog().
-- ============================================================

macroScript TFU2Import_Open
    category:"TFU2 Tool"
    buttonText:"Import TFU2"
    toolTip:"Star Wars: The Force Unleashed II - characters and animations straight from the game"
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
            messageBox "TFU2Import.dlu is not loaded.\n\nPlease run INSTALL.bat again and restart 3ds Max." title:"TFU2 Tool"
    )
)
