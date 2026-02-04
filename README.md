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
- (A modified version of) `win` is working again.
- Python3 and Tcl modules are functional again (Tcl's `win list` delivers IDs *and* names - some old scripts might need some attention)
- Perl 5 module from CPAN, with a few characters modified
- It's still a bloody mess (especially the Makefiles -- not proud of their current state, but they mostly do what I expect from them)
- I removed the "old luggage" -- I don't think defunct old tools for web browsing and mailbox management are worth my time now (I might reintegrate them at some later point).
- Code tree was reorganised and cleaned up (WIP)
- `W` - use (the running instance of) Wily as $EDITOR. The script waits until the window is closed ("runs in foreground"), so it can be used with `git commit` and mail agents.
- `Save`/`Restore` - save and restore the session (documents and selections). A function similar to Acme's `Dump` and `Load` (actually I might rename them to match the original...)

## Work in Progress
- ~~Re-enabling `win` and other tools.~~
- ~~Python3 integration(?)~~
- "poor man's `plumber`" - xdg-open integration
- Documentation cleanup - maybe not getting rid of anything, but the number of collected files in every format known to the humanity is overwhelming.
  It's good to have standards, there are so many of them to choose from.
- Python: I probably need to change the window IDs in list() to integer.

## Disclaimer
0. It works on my machine(s). That's the point of the whole project.
1. it's not a "product" - I play with the code to make it usable _for me_. If anyone enjoys it -- good for them.
2. I am not the official "maintainer" (see #1)`
3. I work with various Unixes ("Unices"), but I only have access to a graphical console on Linux systems. If I broke anything and it doesn't compile or crashes on Solaris, HP-UX, \*BSD, WSL or Mac... NMP (see #1)
   I don't break anything on purpose, but I neither have time nor resources to make sure it runs everywhere - sorry, but not sorry.
4. I still accept constructive input, but (#1, again) I might prioritise based on my own current needs.

---
[1] Or broken, but at least it compiles on my relatively modern Linux... WIP, anyway.
Praise the original author, blame me, m'kay?
[2] ... and the even cooler kids get [Acme2K](https://github.com/karahobny/acme2k)

