Need for Speed: Underground 2 (Xbox recomp) -- Steam Deck
==========================================================

Install
-------
1. Copy this nfsu2x folder anywhere on the Deck (internal storage or the
   SD card), e.g. /home/deck/Games/nfsu2x.
2. Put your extracted NTSC-U Xbox disc into nfsu2x/game/ (default.xbe,
   NFSUNDER/, B3/, ...). Unpacked files, not the ISO.
3. Desktop Mode: run nfsu2x-install.sh once (double-click -> Execute; if
   it opens in a text editor instead, right-click it -> Properties ->
   Permissions -> "Is executable", or in Konsole: sh nfsu2x-install.sh).
   It puts "Need for Speed Underground 2" with its icon in the
   applications menu and on the desktop. (nfsu2x.sh also does this by
   itself on every start if the entry is missing or the folder moved.) Then Steam -> Games -> Add a Non-Steam
   Game to My Library -> tick "Need for Speed Underground 2" -> Add
   Selected Programs, and play it from Game Mode.
   (Already added nfsu2x.sh by hand? Steam -> its Properties -> click the
   empty icon square -> pick nfsu2x.png.)
   Optional: Properties -> Compatibility -> "Steam Linux Runtime 3.0
   (sniper)" runs it inside the runtime it was built for.

Controls
--------
The Deck's own controls through Steam Input (Gamepad layout), mapped by
label, as the game shows them:
  A B X Y        A B X Y
  L1 / R1        White / Black ("Black" in the replay legend reads "RB")
  L2 / R2        left / right trigger (analog)
  Menu (≡)       Start        View (⧉)  Back
Back grips (L4/L5/R4/R5) are free for your own bindings in Steam's
controller settings. A second pad plays player 2 (split screen).
Vibration plays on the Deck's haptics; strength is Options -> Gameplay ->
Vibration (Off, 10%..100%).

Clocks
------
The Deck's CPU/GPU clocks and power limit are Steam's: Quick Access (...)
-> Performance (TDP limit, manual GPU clock, frame limit). The game
leaves them as you set them and logs them in nfsu2x_log.txt ("[clock]"
lines, also every time you change them). The NFSU2_CPU_MHZ / _GPU_MHZ /
_MEM_MHZ settings of the Switch build do nothing here.
Races run uncapped (up to 60 fps); set a frame limit in Quick Access, or
NFSU2_FPS_CAP=30 in nfsu2x_env.txt, to save battery.

Files
-----
nfsu2x_env.txt      runtime switches (see the comments in it)
nfsu2x_options.txt  the port's options (Video rows, vibration, volumes)
nfsu2x_log.txt      log of the last run (nfsu2x_log.prev.txt the one before)
game/UDATA          profiles and saves
