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
// clang-format on

/* --- Minimal Bash Loadable Builtin ABI Declarations --- */
#define EXECUTION_SUCCESS 0
#define EXECUTION_FAILURE 1
#define EX_USAGE 258
#define BUILTIN_ENABLED 0x01

typedef struct word_desc {
  char *word;
  int flags;
} WORD_DESC;

typedef struct word_list {
  struct word_list *next;
  WORD_DESC *word;
} WORD_LIST;

typedef int sh_builtin_func_t(WORD_LIST *);

struct builtin {
  const char *name;
  sh_builtin_func_t *function;
  int flags;
  char *const *long_doc;
  const char *short_doc;
  char *handle;
};

extern void *bind_variable(const char *name, const char *value, int flags);

/* --- Wily IPC Implementation --- */
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
      return 0;
    got += (size_t)n;
  }
  return (ssize_t)got;
}

/* Builtin: wily_dial <fifo_path> -> sets WILY_FD */
static int wily_dial_builtin(WORD_LIST *list) {
  if (!list || !list->word || !list->word->word)
    return EX_USAGE;
  const char *fifo_path = list->word->word;

  char sock_path[] = "/tmp/wily.XXXXXX";
  int tmp_fd = mkstemp(sock_path);
  if (tmp_fd < 0)
    return EXECUTION_FAILURE;
  close(tmp_fd);
  unlink(sock_path);

  int srv = socket(AF_UNIX, SOCK_STREAM, 0);
  if (srv < 0)
    return EXECUTION_FAILURE;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(srv, 1) < 0) {
    close(srv);
    unlink(sock_path);
    return EXECUTION_FAILURE;
  }

  int fifo_fd = open(fifo_path, O_WRONLY);
  if (fifo_fd < 0) {
    close(srv);
    unlink(sock_path);
    return EXECUTION_FAILURE;
  }
  ssize_t wn = write(fifo_fd, sock_path, strlen(sock_path));
  close(fifo_fd);
  if (wn != (ssize_t)strlen(sock_path)) {
    close(srv);
    unlink(sock_path);
    return EXECUTION_FAILURE;
  }

  int conn = accept(srv, NULL, NULL);
  close(srv);
  unlink(sock_path);
  if (conn < 0)
    return EXECUTION_FAILURE;

  char fd_buf[32];
  snprintf(fd_buf, sizeof(fd_buf), "%d", conn);
  bind_variable("WILY_FD", fd_buf, 0);
  return EXECUTION_SUCCESS;
}

/* Builtin: wily_write_pkt <fd> <t> <m> <w> <p0> <p1> <flag> <text> */
static int wily_write_pkt_builtin(WORD_LIST *list) {
  const char *args[8];
  int i = 0;
  for (WORD_LIST *l = list; l && i < 8; l = l->next, i++) {
    args[i] = l->word->word;
  }
  if (i < 8)
    return EX_USAGE;

  int fd = atoi(args[0]);
  uint16_t t = (uint16_t)strtoul(args[1], NULL, 0);
  uint16_t m = (uint16_t)strtoul(args[2], NULL, 0);
  uint16_t w = (uint16_t)strtoul(args[3], NULL, 0);
  uint32_t p0 = (uint32_t)strtoul(args[4], NULL, 0);
  uint32_t p1 = (uint32_t)strtoul(args[5], NULL, 0);
  uint16_t flag = (uint16_t)strtoul(args[6], NULL, 0);
  const char *text = args[7];

  size_t slen = strlen(text);
  uint32_t total_len = (uint32_t)(HEADERSIZE + slen + 1);

  uint8_t hdr[HEADERSIZE];
  uint16_t cookie_be = htons(COOKIE);
  uint16_t t_be = htons(t);
  uint32_t len_be = htonl(total_len);
  uint16_t m_be = htons(m);
  uint16_t w_be = htons(w);
  uint32_t p0_be = htonl(p0);
  uint32_t p1_be = htonl(p1);
  uint16_t flag_be = htons(flag);

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
      (slen > 0 && write_all(fd, text, slen) < 0) ||
      write_all(fd, &nul, 1) < 0) {
    return EXECUTION_FAILURE;
  }
  return EXECUTION_SUCCESS;
}

static void set_num_var(const char *name, unsigned long val) {
  char num_buf[32];
  snprintf(num_buf, sizeof(num_buf), "%lu", val);
  bind_variable(name, num_buf, 0);
}

/* Builtin: wily_read_pkt <fd> -> sets WILY_PKT_{T,M,W,P0,P1,FLAG,S}
 * Returns: 0 = ok, 1 = EOF, 2 = error */
static int wily_read_pkt_builtin(WORD_LIST *list) {
  if (!list || !list->word)
    return EX_USAGE;
  int fd = atoi(list->word->word);

  uint8_t hdr[HEADERSIZE];
  ssize_t rn = read_all(fd, hdr, HEADERSIZE);
  if (rn == 0)
    return 1;
  if (rn < 0)
    return 2;

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
  if (cookie != COOKIE || tlen < HEADERSIZE)
    return 2;

  size_t body_len = tlen - HEADERSIZE;
  char *buf = (char *)malloc(body_len + 1);
  if (!buf)
    return 2;

  if (body_len > 0) {
    ssize_t bn = read_all(fd, buf, body_len);
    if (bn <= 0) {
      free(buf);
      return (bn == 0) ? 1 : 2;
    }
  }
  buf[body_len] = '\0';
  if (body_len > 0 && buf[body_len - 1] == '\0') {
    buf[body_len - 1] = '\0';
  }

  set_num_var("WILY_PKT_T", ntohs(t_be));
  set_num_var("WILY_PKT_M", ntohs(m_be));
  set_num_var("WILY_PKT_W", ntohs(w_be));
  set_num_var("WILY_PKT_P0", ntohl(p0_be));
  set_num_var("WILY_PKT_P1", ntohl(p1_be));
  set_num_var("WILY_PKT_FLAG", ntohs(flag_be));
  bind_variable("WILY_PKT_S", buf, 0);

  free(buf);
  return EXECUTION_SUCCESS;
}

/* Builtin: wily_poll <fd> [timeout_ms] -> 0 if data ready, 1 if would block */
static int wily_poll_builtin(WORD_LIST *list) {
  if (!list || !list->word)
    return EX_USAGE;
  int fd = atoi(list->word->word);
  if (fd < 0)
    return EXECUTION_FAILURE;

  int timeout_ms = 0;
  if (list->next && list->next->word && list->next->word->word) {
    timeout_ms = atoi(list->next->word->word);
  }

  struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
  int rc = poll(&pfd, 1, timeout_ms);
  return (rc > 0) ? EXECUTION_SUCCESS : EXECUTION_FAILURE;
}

/* Builtin: wily_hangup <fd> */
static int wily_hangup_builtin(WORD_LIST *list) {
  if (list && list->word) {
    int fd = atoi(list->word->word);
    if (fd >= 0)
      close(fd);
  }
  return EXECUTION_SUCCESS;
}

/* --- Builtin Registration Structs --- */
static char *const dial_doc[] = {"Connect to Wily FIFO and set WILY_FD.", NULL};
static char *const write_doc[] = {"Send a Wily IPC packet.", NULL};
static char *const read_doc[] = {"Receive a Wily IPC packet into WILY_PKT_*.",
                                 NULL};
static char *const poll_doc[] = {
    "Return 0 if socket has readable bytes, 1 otherwise.", NULL};
static char *const hang_doc[] = {"Close Wily socket fd.", NULL};

struct builtin wily_dial_struct = {"wily_dial",           wily_dial_builtin,
                                   BUILTIN_ENABLED,       dial_doc,
                                   "wily_dial fifo_path", 0};
struct builtin wily_write_pkt_struct = {
    "wily_write_pkt",
    wily_write_pkt_builtin,
    BUILTIN_ENABLED,
    write_doc,
    "wily_write_pkt fd t m w p0 p1 flag text",
    0};
struct builtin wily_read_pkt_struct = {
    "wily_read_pkt", wily_read_pkt_builtin, BUILTIN_ENABLED,
    read_doc,        "wily_read_pkt fd",    0};
struct builtin wily_poll_struct = {
    "wily_poll", wily_poll_builtin,           BUILTIN_ENABLED,
    poll_doc,    "wily_poll fd [timeout_ms]", 0};
struct builtin wily_hangup_struct = {"wily_hangup",    wily_hangup_builtin,
                                     BUILTIN_ENABLED,  hang_doc,
                                     "wily_hangup fd", 0};
