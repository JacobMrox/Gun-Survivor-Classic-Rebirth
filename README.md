# Why

Biohazard Gun Survivor was released on PC exclusively in China/Taiwan, and the port is infamous for being buggy and broken, in addition to this,
running it on modern systems is nearly impossible due to incompatibility issues. This project aims to solve these problems and issues by replacing old api calls and systems with new/modern ones,
along with adding new configuration settings.

# Project Status

• ~~Remove CD check~~
• ~~Add ability to play in window mode~~
• ~~Show window border in windowed mode~~
• ~~Add ability to lock mouse in window~~
• ~~Fix window title typo [GUN SURUIVOR] -> RESIDENT EVIL: SURVIVOR PC~~
• ~~Fix mouse crosshair alignment (replace crosshair)~~
• ~~Add custom resolution option~~
• Fix shadow/blood pool under enemies/npcs
• Fix Zombie footstep sound
• Fix movie player by replacing DirectShow with ffmpeg
• Add option to skip movies/videos
• Fix NPC/enemy lighting/shader
• Fix enemy AI/Animations
• Raw Input + Dinput
  
Add a proper in-game options menu backed by d3d8.ini

# Build Instructions

You need visual studio 2026:

* Open this repo in Visual Studio or Clone it.
* Close if open
* Run "Developer Command Prompt for VS"
* cd to your project dir (e.g. )
* In the Developer Command Prompt do a "vcpkg install"

