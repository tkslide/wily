// clang-format off
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include "gawkapi.h"
// clang-format on

static const gawk_api_t *api;
static awk_ext_id_t ext_id;
static const char *ext_version = "wily_sock 1.1";
static awk_bool_t (*init_func)(void) = NULL;
int plugin_is_GPL_compatible;

#define COOKIE 0xFEED
#define HEADERSIZE 22

static ssize_t write_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = write(fd, p + sent, len - sent);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    sent += (size_t)n;
  }
  return (ssize_t)sent;
}

static ssize_t read_all(int fd, void *buf, size_t len) {
  uint8_t *p = (uint8_t *)buf;
  size_t got = 0;
  while (got < len) {
    ssize_t n = read(fd, p + got, len - got);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      return 0; /* EOF */
    got += (size_t)n;
  }
  return (ssize_t)got;
}

/* wily_dial(fifo_path) -> fd (or -1 on error) */
static awk_value_t *do_wily_dial(int nargs, awk_value_t *result,
                                 struct awk_ext_func *unused) {
  (void)nargs;
  (void)unused;
  awk_value_t fifo_arg;
  if (!get_argument(0, AWK_STRING, &fifo_arg))
    return make_number(-1, result);

  char sock_path[] = "/tmp/wily.XXXXXX";
  int tmp_fd = mkstemp(sock_path);
  if (tmp_fd < 0)
    return make_number(-1, result);
  close(tmp_fd);
  unlink(sock_path);

  int srv = socket(AF_UNIX, SOCK_STREAM, 0);
  if (srv < 0)
    return make_number(-1, result);

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(srv, 1) < 0) {
    close(srv);
    unlink(sock_path);
    return make_number(-1, result);
  }

  int fifo_fd = open(fifo_arg.str_value.str, O_WRONLY);
  if (fifo_fd < 0) {
    close(srv);
    unlink(sock_path);
    return make_number(-1, result);
  }
  ssize_t wn = write(fifo_fd, sock_path, strlen(sock_path));
  close(fifo_fd);
  if (wn != (ssize_t)strlen(sock_path)) {
    close(srv);
    unlink(sock_path);
    return make_number(-1, result);
  }

  int conn = accept(srv, NULL, NULL);
  close(srv);
  unlink(sock_path);

  return make_number(conn, result);
}

/* wily_write_pkt(fd, t, m, w, p0, p1, flag, text) -> 0 on success, -1 on
 * failure */
static awk_value_t *do_wily_write_pkt(int nargs, awk_value_t *result,
                                      struct awk_ext_func *unused) {
  (void)nargs;
  (void)unused;
  awk_value_t v_fd, v_t, v_m, v_w, v_p0, v_p1, v_flag, v_text;
  if (!get_argument(0, AWK_NUMBER, &v_fd) ||
      !get_argument(1, AWK_NUMBER, &v_t) ||
      !get_argument(2, AWK_NUMBER, &v_m) ||
      !get_argument(3, AWK_NUMBER, &v_w) ||
      !get_argument(4, AWK_NUMBER, &v_p0) ||
      !get_argument(5, AWK_NUMBER, &v_p1) ||
      !get_argument(6, AWK_NUMBER, &v_flag) ||
      !get_argument(7, AWK_STRING, &v_text)) {
    return make_number(-1, result);
  }

  int fd = (int)v_fd.num_value;
  size_t slen = v_text.str_value.len;
  uint32_t total_len = (uint32_t)(HEADERSIZE + slen + 1);

  uint8_t hdr[HEADERSIZE];
  uint16_t cookie_be = htons(COOKIE);
  uint16_t t_be = htons((uint16_t)v_t.num_value);
  uint32_t len_be = htonl(total_len);
  uint16_t m_be = htons((uint16_t)v_m.num_value);
  uint16_t w_be = htons((uint16_t)v_w.num_value);
  uint32_t p0_be = htonl((uint32_t)v_p0.num_value);
  uint32_t p1_be = htonl((uint32_t)v_p1.num_value);
  uint16_t flag_be = htons((uint16_t)v_flag.num_value);

  memcpy(hdr + 0, &cookie_be, 2);
  memcpy(hdr + 2, &t_be, 2);
  memcpy(hdr + 4, &len_be, 4);
  memcpy(hdr + 8, &m_be, 2);
  memcpy(hdr + 10, &w_be, 2);
  memcpy(hdr + 12, &p0_be, 4);
  memcpy(hdr + 16, &p1_be, 4);
  memcpy(hdr + 20, &flag_be, 2);

  uint8_t nul = 0;
  if (write_all(fd, hdr, HEADERSIZE) < 0 ||
      (slen > 0 && write_all(fd, v_text.str_value.str, slen) < 0) ||
      write_all(fd, &nul, 1) < 0) {
    return make_number(-1, result);
  }
  return make_number(0, result);
}

static void set_arr_num(awk_array_t arr, const char *key, double val) {
  awk_value_t k, v;
  make_const_string(key, strlen(key), &k);
  make_number(val, &v);
  set_array_element(arr, &k, &v);
}

static void set_arr_str(awk_array_t arr, const char *key, const char *str,
                        size_t len) {
  awk_value_t k, v;
  make_const_string(key, strlen(key), &k);
  make_const_string(str, len, &v);
  set_array_element(arr, &k, &v);
}

/* wily_read_pkt(fd, out_array) -> 1 on success, 0 on EOF, -1 on error */
static awk_value_t *do_wily_read_pkt(int nargs, awk_value_t *result,
                                     struct awk_ext_func *unused) {
  (void)nargs;
  (void)unused;
  awk_value_t v_fd, v_arr;
  if (!get_argument(0, AWK_NUMBER, &v_fd) ||
      !get_argument(1, AWK_ARRAY, &v_arr)) {
    return make_number(-1, result);
  }

  int fd = (int)v_fd.num_value;
  awk_array_t arr = v_arr.array_cookie;
  clear_array(arr);

  uint8_t hdr[HEADERSIZE];
  ssize_t rn = read_all(fd, hdr, HEADERSIZE);
  if (rn == 0)
    return make_number(0, result);
  if (rn < 0)
    return make_number(-1, result);

  uint16_t cookie_be, t_be, m_be, w_be, flag_be;
  uint32_t len_be, p0_be, p1_be;
  memcpy(&cookie_be, hdr + 0, 2);
  memcpy(&t_be, hdr + 2, 2);
  memcpy(&len_be, hdr + 4, 4);
  memcpy(&m_be, hdr + 8, 2);
  memcpy(&w_be, hdr + 10, 2);
  memcpy(&p0_be, hdr + 12, 4);
  memcpy(&p1_be, hdr + 16, 4);
  memcpy(&flag_be, hdr + 20, 2);

  uint16_t cookie = ntohs(cookie_be);
  uint32_t tlen = ntohl(len_be);
  if (cookie != COOKIE || tlen < HEADERSIZE) {
    return make_number(-2, result);
  }

  size_t body_len = tlen - HEADERSIZE;
  char *buf = (char *)malloc(body_len + 1);
  if (!buf)
    return make_number(-1, result);

  if (body_len > 0) {
    ssize_t bn = read_all(fd, buf, body_len);
    if (bn <= 0) {
      free(buf);
      return make_number(bn == 0 ? 0 : -1, result);
    }
  }
  buf[body_len] = '\0';
  size_t text_len =
      (body_len > 0 && buf[body_len - 1] == '\0') ? body_len - 1 : body_len;

  set_arr_num(arr, "t", ntohs(t_be));
  set_arr_num(arr, "m", ntohs(m_be));
  set_arr_num(arr, "w", ntohs(w_be));
  set_arr_num(arr, "p0", ntohl(p0_be));
  set_arr_num(arr, "p1", ntohl(p1_be));
  set_arr_num(arr, "flag", ntohs(flag_be));
  set_arr_str(arr, "s", buf, text_len);

  free(buf);
  return make_number(1, result);
}

/* wily_poll(fd [, timeout_ms]) -> 1 if data ready, 0 if would block, -1 on
 * error */
static awk_value_t *do_wily_poll(int nargs, awk_value_t *result,
                                 struct awk_ext_func *unused) {
  (void)unused;
  awk_value_t v_fd, v_to;
  if (!get_argument(0, AWK_NUMBER, &v_fd))
    return make_number(-1, result);

  int fd = (int)v_fd.num_value;
  if (fd < 0)
    return make_number(0, result);

  int timeout_ms = 0;
  if (nargs >= 2 && get_argument(1, AWK_NUMBER, &v_to)) {
    timeout_ms = (int)v_to.num_value;
  }

  struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
  int rc = poll(&pfd, 1, timeout_ms);
  if (rc < 0)
    return make_number(-1, result);
  return make_number(rc > 0 ? 1 : 0, result);
}

/* wily_hangup(fd) */
static awk_value_t *do_wily_hangup(int nargs, awk_value_t *result,
                                   struct awk_ext_func *unused) {
  (void)nargs;
  (void)unused;
  awk_value_t v_fd;
  if (get_argument(0, AWK_NUMBER, &v_fd) && v_fd.num_value >= 0) {
    close((int)v_fd.num_value);
  }
  return make_number(0, result);
}

static awk_ext_func_t func_table[] = {
    {"wily_dial", do_wily_dial, 1, 1, awk_false, NULL},
    {"wily_write_pkt", do_wily_write_pkt, 8, 8, awk_false, NULL},
    {"wily_read_pkt", do_wily_read_pkt, 2, 2, awk_false, NULL},
    {"wily_poll", do_wily_poll, 2, 1, awk_false, NULL},
    {"wily_hangup", do_wily_hangup, 1, 1, awk_false, NULL},
};

dl_load_func(func_table, wily_sock, "")
