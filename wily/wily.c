/*******************************************
 *	main(), parse arguments
 *******************************************/

#include "tile.h"
#include "wily.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int ncolumns = 2;
int tagheight;
Tile *wily = 0; /* encloses the whole application */

/* version */
char *wilytools = "Kill | Newcol Quit Putall Dotfiles Font ";
char *filetools = "Del Look .";
char *dirtools = "Del Look ..";
char *columntools = "Delcol New Cut Paste Snarf Anchor Split | ";

void _allocerror(char *s) {
  fprintf(stderr, "%s\n", s);
  abort();
}

/* Add  'cmd' to the visible list of running processes. */
void addrunning(char *cmd) { tag_addrunning(view_text(wily->tag), cmd); }

/* Remove 'cmd' from the visible list of running processes.*/
void rmrunning(char *cmd) { tag_rmtool(view_text(wily->tag), cmd); }

/* Initialize info about what bits of text get added to various tags.
 * Reads $WILYTOOLS or $HOME/.wilytools for regexp/tool pairs,
 * uses $WMAINTAG, $WCOLTAG $WFILETAG, $WDIRTAG
 * as the tools for the wily tag, column tag, file tags and directory tags
 * respectively.
 */
static void tools_init(void) {
  char *s;
  char t[200];

  if ((s = getenv("WILYTOOLS"))) {
    tag_init(s);
  } else {
    char *home = getenv("HOME");
    if (home) {
      char p[PATH_MAX];
      snprintf(p, sizeof(p), "%s/%s", home, ".wilytools");
      tag_init(p);
    }
  }

  if ((s = getenv("WCOLTAG"))) {
    if (columntools)
      free(columntools);
    columntools = strdup(s);
  }

  if ((s = getenv("WMAINTAG"))) {
    const char *base = wilytools ? wilytools : "";

    snprintf(t, sizeof(t), "%s%s", base, s);

    if (wilytools)
      free(wilytools);
    wilytools = strdup(t);
  }

  if ((s = getenv("WFILETAG"))) {
    if (filetools)
      free(filetools);
    filetools = strdup(s);
  }

  if ((s = getenv("WDIRTAG"))) {
    if (dirtools)
      free(dirtools);
    dirtools = strdup(s);
  }
}

static void usage(void) {
  fprintf(stderr, "wily [-c ncolumns] [-a tabsize] [-e command] [file1 ...]\n");
  exit(1);
}

static void args(int argc, char **argv, char **envp) {
  extern char *optarg;
  extern int optind;
  int c;
  char *cmd = 0;

  tabsize = 4; /* default */

  /* init libXg */
  xtbinit((Errfunc)error, "Wily", &argc, argv, 0);
  tagheight = font->height + 2 * INSET;

  while ((c = getopt(argc, argv, "c:a:e:")) != EOF) {
    switch (c) {
    case 'c':
      ncolumns = atoi(optarg);
      break;
    case 'a':
      tabsize = atoi(optarg);
      break;
    case 'e':
      cmd = optarg;
      break;
    default:
      usage();
      break;
    }
  }
  scroll_init();
  einit(Ekeyboard | Emouse);
  cursorswitch(cursor);
  wily_init();

  ex_init();

  if (optind < argc) {
    for (; optind < argc; optind++) {
      Path label;

      data_addcontext(0, label, argv[optind]);
      data_open(label, true);
    }
  } else if (cmd == 0) {
    data_open(wilydir, false);
  }

  if (cmd != 0)
    run(wily->tag, cmd, 0);
}

/* Initialise the base tile, with some columns */
void wily_init(void) {
  Text *t;
  int j;

  t = text_alloc(0, false);
  text_replaceutf(t, nr, wilytools);
  wily = tile_new(V, 0, screen.r.max.y, 0, 0, t, 0);
  tile_reshaped(wily);
  last_selection = 0;
  view_setlastselection(wily->tag);
  for (j = 0; j < ncolumns; j++)
    col_new(wily->tag, 0);
}

/* Reshape the base tile */
void ereshaped(Rectangle r) {
  if (!wily)
    return; /* not initialised yet */
  wily->cmax = r.max.x;
  wily->max = r.max.y;
  tile_reshaped(wily);
}

/* Main event loop */
static void mainloop(void) {
  ulong type;
  Event e;
  bool mouseaction = true;
  Point lastp = {0, 0};

  while ((type = eread(~0, &e))) {
    switch (type) {
    case Ekeyboard:
      if (mouseaction) {
        last_focus = point2view(lastp);
        mouseaction = false;
      }
      if (last_focus)
        dokeyboard(last_focus, e.kbdc);
      break;

    case Emouse:
      lastp = e.mouse.xy;
      mouseaction = true;
      if (e.mouse.buttons && (last_focus = point2view(e.mouse.xy)))
        domouse(last_focus, &e.mouse);
      break;

    default:
      dofd(type, e.n, (char *)e.data);
      break;
    }
  }
}

int main(int argc, char **argv, char **envp) {
  if (!getcwd(wilydir, MAXPATH))
    fatal("couldn't find out what directory this is");
  add_slash(wilydir);
  env_init(envp);
  tools_init();
  args(argc, argv, envp);
  mainloop();
  return 0;
}
