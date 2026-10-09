Suppressor - macOS installation
Side Key System - https://sidekey.fr

Suppressor is free. The first time you open it, it asks for your name and
email once to activate it.

INSTALLATION (VST3)

1. Open the DMG.
2. Drag "Suppressor.vst3" onto the "VST3 (drag plugin here)" shortcut, i.e. into

   /Library/Audio/Plug-Ins/VST3/

3. Restart your DAW and rescan plugins if needed.

If macOS blocks the plugin ("damaged" / "cannot be opened" message), the file
was quarantined by the download. Remove the quarantine flag in Terminal:

   sudo xattr -cr /Library/Audio/Plug-Ins/VST3/Suppressor.vst3

By installing and using Suppressor you accept the license agreement included in
this DMG (EULA.txt).
