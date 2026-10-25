open-homm5 @VERSION@ - a small add-on for Heroes of Might and Magic V (Tribes of the East + MMH5.5)

What it is: one file, dinput8.dll, that the game loads at start-up. It makes AI turns in co-op games much faster. It adds files to the game's bin folder and never changes the game's own files.

You need: the Steam game with MMH5.5 already installed (MMH55_Utility_64.exe in the game's bin folder). This add-on does not install MMH5.5.

Install on Windows: extract this zip (right-click it, "Extract All"), then double-click install.bat. It finds your Steam copy of the game and tells you what it did.
Install on Linux (Steam / Proton): extract the zip, open a terminal in the folder, and run: bash install.sh. At the end it gives you one line to paste into the game's Steam launch options.

Check that it works: start the game. A file named open-homm5.log appears in the game's bin folder. If it is there, the add-on is running.
Everyone who plays the same co-op game should install it.

Random map templates: the install also adds ready templates (names start with OH-) to the game's random map list. Every player of a multiplayer game needs the same ones, so everyone should install the same version.

Settings (you can skip this): open-homm5.ini in the bin folder. Every line is commented out and shows the default.
Uninstall: double-click uninstall.bat (Windows) or run: bash uninstall.sh (Linux). Only our own files are removed (dinput8.dll, the log, our templates, and your settings file if you say yes). Templates you added yourself stay.

Project page, guides and bug reports: https://github.com/jozrcr/open-homm5
