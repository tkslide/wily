// clang-format off
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <lua.h>
#include <lauxlib.h>
// clang-format on

/* wily_unix.dial(fifo_path) -> fd (integer) or nil, err */
static int l_dial(lua_State *L) {
  const char *fifo_path = luaL_checkstring(L, 1);

  char sock_path[] = "/tmp/wily.XXXXXX";
  int tmp_fd = mkstemp(sock_path);
  if (tmp_fd < 0) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  close(tmp_fd);
  unlink(sock_path);

  int srv = socket(AF_UNIX, SOCK_STREAM, 0);
  if (srv < 0) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(srv, 1) < 0) {
    int err = errno;
    close(srv);
    unlink(sock_path);
    lua_pushnil(L);
    lua_pushstring(L, strerror(err));
    return 2;
  }

  int fifo_fd = open(fifo_path, O_WRONLY);
  if (fifo_fd < 0) {
    int err = errno;
    close(srv);
    unlink(sock_path);
    lua_pushnil(L);
    lua_pushstring(L, strerror(err));
    return 2;
  }
  ssize_t wn = write(fifo_fd, sock_path, strlen(sock_path));
  int werr = errno;
  close(fifo_fd);
  if (wn != (ssize_t)strlen(sock_path)) {
    close(srv);
    unlink(sock_path);
    lua_pushnil(L);
    lua_pushstring(L, strerror(werr));
    return 2;
  }

  int conn = accept(srv, NULL, NULL);
  int aerr = errno;
  close(srv);
  unlink(sock_path);
  if (conn < 0) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(aerr));
    return 2;
  }

  lua_pushinteger(L, conn);
  return 1;
}

/* wily_unix.write_all(fd, data) -> true or nil, err */
static int l_write_all(lua_State *L) {
  int fd = (int)luaL_checkinteger(L, 1);
  size_t len = 0;
  const char *p = luaL_checklstring(L, 2, &len);
  size_t sent = 0;

  while (sent < len) {
    ssize_t n = write(fd, p + sent, len - sent);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      lua_pushnil(L);
      lua_pushstring(L, strerror(errno));
      return 2;
    }
    sent += (size_t)n;
  }
  lua_pushboolean(L, 1);
  return 1;
}

/* wily_unix.read_exact(fd, n) -> string or nil, err */
static int l_read_exact(lua_State *L) {
  int fd = (int)luaL_checkinteger(L, 1);
  size_t len = (size_t)luaL_checkinteger(L, 2);
  if (len == 0) {
    lua_pushliteral(L, "");
    return 1;
  }

  char *buf = (char *)malloc(len);
  if (!buf)
    return luaL_error(L, "out of memory");

  size_t got = 0;
  while (got < len) {
    ssize_t n = read(fd, buf + got, len - got);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      int err = errno;
      free(buf);
      lua_pushnil(L);
      lua_pushstring(L, strerror(err));
      return 2;
    }
    if (n == 0) {
      free(buf);
      lua_pushnil(L);
      lua_pushliteral(L, "Wily closed the IPC connection");
      return 2;
    }
    got += (size_t)n;
  }

  lua_pushlstring(L, buf, len);
  free(buf);
  return 1;
}

/* wily_unix.poll(fd, timeout_ms) -> boolean */
static int l_poll(lua_State *L) {
  int fd = (int)luaL_checkinteger(L, 1);
  int timeout_ms = (int)luaL_optinteger(L, 2, 0);
  struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
  lua_pushboolean(L, poll(&pfd, 1, timeout_ms) > 0);
  return 1;
}

/* wily_unix.close(fd) */
static int l_close(lua_State *L) {
  int fd = (int)luaL_checkinteger(L, 1);
  if (fd >= 0)
    close(fd);
  return 0;
}

static const luaL_Reg funcs[] = {
    {"dial", l_dial}, {"write_all", l_write_all}, {"read_exact", l_read_exact},
    {"poll", l_poll}, {"close", l_close},         {NULL, NULL}};

int luaopen_wily_unix(lua_State *L) {
#if LUA_VERSION_NUM >= 502
  luaL_newlib(L, funcs);
#else
  luaL_register(L, "wily_unix", funcs);
#endif
  return 1;
}
