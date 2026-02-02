Wily
----
My tweaked and cleaned-up[1] take on Gary Capell's emulation of the Acme editor.

<img width="1128" height="687" alt="Wily, running on Ubuntu, with one 'win' tile." src="https://github.com/user-attachments/assets/bbfd4b27-99ec-44ce-b36f-304631553b20" />

Cool kids use the great Lunix port[2] from [Plan9Port Acme](https://github.com/tkslide/plan9port) these days, but `wily` is my first Acme-like editor, and I still use it where a full Plan9Port installation is a no-go.

See [Original README](README.orig) for copyright, permissions etc.

## Useful links
[Original sources on SourceForge](https://sourceforge.net/projects/wily/)

[Wily Homepage](http://www.cs.yorku.ca/~oz/wily/index.html)

[Introduction to Wily](http://www.cs.yorku.ca/~oz/wily/intro.html)


## Changes
- Compilable with C23 gcc
- Added scrolling with the mouse wheel
- Removed the version notice in the tagline (because I know it's Wily and I need room there)
- `win` is working again
- Python3 and Tcl modules are functional again (Tcl's `win list` delivers IDs *and* names - some old scripts might need some attention)
- It's still a bloody mess (especially the Makefiles -- not proud of their current state, but they mostly do what I expect from them)
- I removed the "old luggage" -- I don't think defunct old tools for web browsing and mailbox management are worth my time now (I might reintegrate them at some later point).

## Work in Progress
- ~~Re-enabling `win` and other tools.~~
- ~~Python3 integration(?)~~
- "poor man's `plumber`" - xdg-open integration
- Documentation cleanup - maybe not getting rid of anything, but the number of collected files in every format known to the humanity is overwhelming.
  It's good to have standards, there are so many of them to choose from.


[1] Or broken, but at least it compiles on my relatively modern Linux... WIP, anyway.
Praise the original author, blame me, m'kay?
[2] ... and the even cooler kids get (Acme2K)[https://github.com/karahobny/acme2k]


