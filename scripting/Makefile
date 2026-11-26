CC       ?= gcc
PYTHON   ?= python3
PERL     ?= perl
RACO     ?= raco
LUA      ?= lua
BASH     ?= bash
GAWK     ?= gawk
CFLAGS   ?= -O2 -Wall -Wextra
PREFIX   ?= /usr/local

# Subdirectories (defaults to current directory; override e.g. RKT_DIR=racket LUA_DIR=lua)
PY_DIR   ?= python
PL_DIR   ?= perl
RKT_DIR  ?= racket
LUA_DIR  ?= lua
BASH_DIR ?= bash
AWK_DIR  ?= awk

# Platform-specific shared library flags
UNAME_S  := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LDFLAGS_SO := -bundle -undefined dynamic_lookup
else
  LDFLAGS_SO := -shared -fPIC
endif

# --- Python Configuration ---
PY_VER   := $(shell $(PYTHON) -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null)
ifneq ($(wildcard /usr/local/lib/python$(PY_VER)/dist-packages),)
  PYDIR  ?= $(PREFIX)/lib/python$(PY_VER)/dist-packages
else
  PYDIR  ?= $(shell $(PYTHON) -c 'import sysconfig; print(sysconfig.get_path("purelib"))' 2>/dev/null || echo $(PREFIX)/lib/python$(PY_VER)/site-packages)
endif

# --- Perl Configuration ---
PLDIR    ?= $(shell $(PERL) -MConfig -e 'print $$Config{sitelib}' 2>/dev/null || echo $(PREFIX)/share/perl5)

# --- Lua Configuration ---
LUA_VER  := $(shell $(LUA) -e 'print(_VERSION:match("%d+%.%d+"))' 2>/dev/null || echo 5.4)
LUA_INC  := $(shell pkg-config --cflags lua$(LUA_VER) 2>/dev/null || \
                    pkg-config --cflags lua-$(LUA_VER) 2>/dev/null || \
                    pkg-config --cflags lua 2>/dev/null)
ifeq ($(strip $(LUA_INC)),)
  ifneq ($(wildcard /usr/include/lua$(LUA_VER)/lua.h),)
    LUA_INC := -I/usr/include/lua$(LUA_VER)
  endif
endif
LUADIR   ?= $(PREFIX)/share/lua/$(LUA_VER)
CLUADIR  ?= $(PREFIX)/lib/lua/$(LUA_VER)

# --- Bash Configuration ---
BASH_INC := $(shell pkg-config --cflags bash 2>/dev/null)
ifeq ($(strip $(BASH_INC)),)
  ifneq ($(wildcard /usr/include/bash/builtins.h),)
    BASH_INC := -I/usr/include/bash -I/usr/include/bash/include -I/usr/include/bash/builtins
  endif
endif
BASHDIR  ?= $(PREFIX)/lib/bash

# --- GNU Awk Configuration ---
GAWK_INC := $(shell pkg-config --cflags gawk 2>/dev/null)
ifeq ($(strip $(GAWK_INC)),)
  ifneq ($(wildcard /usr/local/include/gawkapi.h),)
    GAWK_INC := -I/usr/local/include
  endif
endif
AWKEXTDIR := $(shell $(GAWK) 'BEGIN { split(ENVIRON["AWKLIBPATH"], p, ":"); print p[1] }' 2>/dev/null)
ifeq ($(strip $(AWKEXTDIR)),)
  AWKEXTDIR := $(PREFIX)/lib/gawk
endif
AWKLIBDIR ?= $(PREFIX)/share/awk

# --- Targets ---
all: lua bash awk

lua: $(LUA_DIR)/wily_unix.so
bash: $(BASH_DIR)/wily_bash.so
awk: $(AWK_DIR)/wily_sock.so

$(LUA_DIR)/wily_unix.so: $(LUA_DIR)/wily_unix.c
	$(CC) $(LDFLAGS_SO) $(CFLAGS) $(LUA_INC) $< -o $@

$(BASH_DIR)/wily_bash.so: $(BASH_DIR)/wily_bash.c
	$(CC) $(LDFLAGS_SO) $(CFLAGS) $(BASH_INC) $< -o $@

$(AWK_DIR)/wily_sock.so: $(AWK_DIR)/wily_sock.c
	$(CC) $(LDFLAGS_SO) $(CFLAGS) $(GAWK_INC) $< -o $@

install: install-python install-perl install-racket install-lua install-bash install-awk

install-python: $(PY_DIR)/wily.py
	install -d $(DESTDIR)$(PYDIR)
	install -m 0644 $(PY_DIR)/wily.py $(DESTDIR)$(PYDIR)/wily.py

install-perl: $(PL_DIR)/lib/Wily/Client.pm
	install -d $(DESTDIR)$(PLDIR)/Wily
	install -m 0644 $(PL_DIR)/lib/Wily/Client.pm $(DESTDIR)$(PLDIR)/Wily/Client.pm

install-racket: $(RKT_DIR)/wily.rkt $(RKT_DIR)/info.rkt
	$(RACO) pkg install --installation --auto --batch --copy --name wily $(RKT_DIR)

install-lua: $(LUA_DIR)/wily_unix.so $(LUA_DIR)/wily.lua
	install -d $(DESTDIR)$(LUADIR) $(DESTDIR)$(CLUADIR)
	install -m 0644 $(LUA_DIR)/wily.lua $(DESTDIR)$(LUADIR)/wily.lua
	install -m 0755 $(LUA_DIR)/wily_unix.so $(DESTDIR)$(CLUADIR)/wily_unix.so

install-bash: $(BASH_DIR)/wily_bash.so $(BASH_DIR)/wily.bash
	install -d $(DESTDIR)$(BASHDIR)
	install -m 0755 $(BASH_DIR)/wily_bash.so $(DESTDIR)$(BASHDIR)/wily_bash.so
	install -m 0644 $(BASH_DIR)/wily.bash $(DESTDIR)$(BASHDIR)/wily.bash

install-awk: $(AWK_DIR)/wily_sock.so $(AWK_DIR)/wily.awk
	install -d $(DESTDIR)$(AWKEXTDIR) $(DESTDIR)$(AWKLIBDIR)
	install -m 0755 $(AWK_DIR)/wily_sock.so $(DESTDIR)$(AWKEXTDIR)/wily_sock.so
	install -m 0644 $(AWK_DIR)/wily.awk $(DESTDIR)$(AWKLIBDIR)/wily.awk

uninstall: uninstall-python uninstall-perl uninstall-racket uninstall-lua uninstall-bash uninstall-awk

uninstall-python:
	rm -f $(DESTDIR)$(PYDIR)/wily.py

uninstall-perl:
	rm -f $(DESTDIR)$(PLDIR)/Wily/Client.pm
	-rmdir $(DESTDIR)$(PLDIR)/Wily 2>/dev/null || true

uninstall-racket:
	$(RACO) pkg remove --installation --batch wily

uninstall-lua:
	rm -f $(DESTDIR)$(LUADIR)/wily.lua $(DESTDIR)$(CLUADIR)/wily_unix.so

uninstall-bash:
	rm -f $(DESTDIR)$(BASHDIR)/wily_bash.so $(DESTDIR)$(BASHDIR)/wily.bash

uninstall-awk:
	rm -f $(DESTDIR)$(AWKEXTDIR)/wily_sock.so $(DESTDIR)$(AWKLIBDIR)/wily.awk

clean:
	rm -f $(LUA_DIR)/wily_unix.so $(BASH_DIR)/wily_bash.so $(AWK_DIR)/wily_sock.so

.PHONY: all lua bash awk install install-python install-perl install-racket install-lua install-bash install-awk \
        uninstall uninstall-python uninstall-perl uninstall-racket uninstall-lua uninstall-bash uninstall-awk clean
