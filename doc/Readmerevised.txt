MESS Apple-1 Programs (revised Feb 22, 2012)

Important: To load a program you must first type C100R and hit enter. This allows MESS to read the .wav file as a cassette tape.

Example: "Microchess"

C100R (hit enter)
1000.18FFR (play Tape and wait for the cursor to drop)
1000R (hit enter) (you must be in full emulation at this point before you hit enter "Scroll Lock")

Tip: To restart Microchess hit f12, and retype 1000R
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
Note: Some games/programs require the “Apple-1 Basic” to be loaded first.

How to load a game in MESS that requires "BASIC"

Example: "Slots" [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

C100R (hit enter)
E000.EFFFR (hit "Play", "Return to Prior Menu", "Return to System" wait for cursor to drop)
E000R (hit enter) (you must be in full emulation before you hit enter "Scroll Lock")
You should now see an arrow  >  (this means you have successfully loaded "BASIC")
Hit f12 (this will bring your cursor down)
Mount "Slots.wav" on Cassette (Scroll Lock, Tab, File Manager, Cassette) (then "Return to Prior Menu", " Return to System")
C100R (hit enter)
4A.00FFR300.FFFR (hit "Play", "Return to Prior Menu", "Return to System" wait for cursor to drop) 
E2B3R (hit enter) (you must be in full emulation before you hit enter "Scroll Lock")
Type "RUN" (without quotes)

DONE!

With a little luck your game should now be loaded and ready to play. If your game didn’t load you may have to start over (f2 will clear the screen, f12 to drop cursor down). If a program fails to load it’s usually because of a skipped step, or something was incorrectly typed.

Tip: You can always restart a basic game by hitting the f12 key, type E2B3R hit enter, type RUN.   
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
Extra blah blah ...

Apple-1 "BASIC"

If you can't load/run Apple-1 "BASIC" and you are 100% sure you did everything right, then unfortunately it's up to you to find a "BASIC" wave file that DOES work! I've had a number of complaints about "BASIC" not working (in MESS) even though it works (loads and runs) just fine for me in MESS (and Agat Emulator). With that said the problem is a bug in MESS. So please take this matter up with the more tech skilled MESS programmers.

I've included a new “BASIC.wav” file (thanks to Robbbert!). This should work for you, but I can’t/won't make any guarantees it will (re-read the above paragraph).

If you notice any game/s not working please let me know. I'll do my best to fix any non-working MESS Apple-1 games.

Anyway have fun with these MESS Apple-1 programs. I'll be adding more in the future.

Anonymous01
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
To ALL the skilled, and intellectually gifted people at the MESS forums that have helped me. Thank you!
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
Programs/Games


ASMmchess - load at 300.BFFR, enter at 300R

APPLE30TH (graphics demo) - load at 280.FFFR, enter at 280R

Apple1Basic - load at E000.EFFFR, enter at E000R

Blackjack - [Req BASIC][load at 4A.00FFR800.0FFFR, enter at E2B3R]

Bowling - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

AppleI Enhanced Checkers - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

Craps - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

Deal or No Deal - [Req BASIC][load at 4A.00FFR300.3FFFR, enter at E2B3R]

Football - [Req BASIC][load at 4A.00FFR400.2FFFR, enter at E2B3R]

Hamurabi - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

HiLo - [Req BASIC][load at 4A.00FFR800.FFFR, enter at E2B3R]

Life - load at 2000.21FFR, enter at 2000R

Lunar Lander - load at 300.0A00R, enter at 0300R

LunarLander (ASCII Graphics) - [Req BASIC][load at 4A.00FFR0300.0FFFR, enter at E2B3R]

Mastermind - load at 300.03FFR, enter at 0300R

Microchess - load at 1000.18FFR, enter at 1000R

Pasart - load at 300.5FFR, enter at 300R

Slots - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

StarTrek - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

StarTrek2003 - [Req BASIC][load at 4A.00FFR300.3FFFR, enter at E2B3R]

WordCross - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R

Wumpus - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

Buzzword - [Req BASIC][load at 4A.00FFR300.FFFR, enter at E2B3R]

AceyDucey - [Req BASIC][load at 4A.00FFR800.FFFR, enter at E2B3R]