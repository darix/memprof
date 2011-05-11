#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "arch.h"
#include "bin_api.h"
#include "json.h"
#include "tracer.h"
#include "tramp.h"
#include "util.h"

struct memprof_fd_stats {
  size_t read_calls;
  uint32_t read_time;
  size_t read_requested_bytes;
  size_t read_actual_bytes;

  size_t write_calls;
  uint32_t write_time;
  size_t write_requested_bytes;
  size_t write_actual_bytes;

  size_t recv_calls;
  uint32_t recv_time;
  ssize_t recv_actual_bytes;

  size_t connect_calls;
  uint32_t connect_time;

  size_t select_calls;
  uint32_t select_time;

  size_t poll_calls;
  uint32_t poll_time;
};

static struct tracer tracer;
static struct memprof_fd_stats stats;

static ssize_t
read_tramp(int fildes, void *buf, size_t nbyte) {
  uint64_t millis = 0;
  int err;
  ssize_t ret;

  millis = timeofday_ms();
  ret = read(fildes, buf, nbyte);
  err = errno;
  millis = timeofday_ms() - millis;

  stats.read_time += millis;
  stats.read_calls++;
  stats.read_requested_bytes += nbyte;
  if (ret > 0)
    stats.read_actual_bytes += ret;

  errno = err;
  return ret;
}

static ssize_t
write_tramp(int fildes, const void *buf, size_t nbyte) {
  uint64_t millis = 0;
  int err;
  ssize_t ret;

  millis = timeofday_ms();
  ret = write(fildes, buf, nbyte);
  err = errno;
  millis = timeofday_ms() - millis;

  stats.write_time += millis;
  stats.write_calls++;
  stats.write_requested_bytes += nbyte;
  if (ret > 0)
    stats.write_actual_bytes += ret;

  errno = err;
  return ret;
}

static ssize_t
recv_tramp(int socket, void *buffer, size_t length, int flags) {
  uint64_t millis = 0;
  int err;
  ssize_t ret;

  millis = timeofday_ms();
  ret = recv(socket, buffer, length, flags);
  err = errno;
  millis = timeofday_ms() - millis;

  stats.recv_time += millis;
  stats.recv_calls++;
  if (ret > 0)
    stats.recv_actual_bytes += ret;

  errno = err;
  return ret;
}

static int
connect_tramp(int socket, const struct sockaddr *address, socklen_t address_len) {
  uint64_t millis = 0;
  int err, ret;

  millis = timeofday_ms();
  ret = connect(socket, address, address_len);
  err = errno;
  millis = timeofday_ms() - millis;

  stats.connect_time += millis;
  stats.connect_calls++;

  errno = err;
  return ret;
}

static int
select_tramp(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout)
{
  uint64_t millis = 0;
  int ret, err;

  millis = timeofday_ms();
  ret = select(nfds, readfds, writefds, errorfds, timeout);
  err = errno;
  millis = timeofday_ms() - millis;

  stats.select_time += millis;
  stats.select_calls++;

  errno = err;
  return ret;
}

static int
poll_tramp(struct pollfd fds[], nfds_t nfds, int timeout)
{
  uint64_t millis = 0;
  int ret, err;

  millis = timeofday_ms();
  ret = poll(fds, nfds, timeout);
  err = errno;
  millis = timeofday_ms() - millis;

  stats.poll_time += millis;
  stats.poll_calls++;

  errno = err;
  return ret;
}

static void
fd_trace_start() {
  static int inserted = 0;

  if (!inserted)
    inserted = 1;
  else
    return;

  insert_tramp("read", read_tramp);
  insert_tramp("write", write_tramp);
  insert_tramp("poll", poll_tramp);

  #ifdef HAVE_MACH
  insert_tramp("select$DARWIN_EXTSN", select_tramp);
  #else
  insert_tramp("select", select_tramp);
  insert_tramp("connect", connect_tramp);
  insert_tramp("recv", recv_tramp);
  #endif
}

static void
fd_trace_stop() {
}

static void
fd_trace_reset() {
  memset(&stats, 0, sizeof(stats));
}

static void
fd_trace_dump(yajl_gen gen) {
  if (stats.read_calls > 0) {
    yajl_gen_cstr(gen, "read");
    yajl_gen_map_open(gen);
    yajl_gen_cstr(gen, "calls");
    yajl_gen_integer(gen, stats.read_calls);
    yajl_gen_cstr(gen, "time");
    yajl_gen_integer(gen, stats.read_time);
    yajl_gen_cstr(gen, "requested");
    yajl_gen_integer(gen, stats.read_requested_bytes);
    yajl_gen_cstr(gen, "actual");
    yajl_gen_integer(gen, stats.read_actual_bytes);
    yajl_gen_map_close(gen);
  }

  if (stats.write_calls > 0) {
    yajl_gen_cstr(gen, "write");
    yajl_gen_map_open(gen);
    yajl_gen_cstr(gen, "calls");
    yajl_gen_integer(gen, stats.write_calls);
    yajl_gen_cstr(gen, "time");
    yajl_gen_integer(gen, stats.write_time);
    yajl_gen_cstr(gen, "requested");
    yajl_gen_integer(gen, stats.write_requested_bytes);
    yajl_gen_cstr(gen, "actual");
    yajl_gen_integer(gen, stats.write_actual_bytes);
    yajl_gen_map_close(gen);
  }

  if (stats.recv_calls > 0) {
    yajl_gen_cstr(gen, "recv");
    yajl_gen_map_open(gen);
    yajl_gen_cstr(gen, "calls");
    yajl_gen_integer(gen, stats.recv_calls);
    yajl_gen_cstr(gen, "time");
    yajl_gen_integer(gen, stats.recv_time);
    yajl_gen_cstr(gen, "actual");
    yajl_gen_integer(gen, stats.recv_actual_bytes);
    yajl_gen_map_close(gen);
  }

  if (stats.connect_calls > 0) {
    yajl_gen_cstr(gen, "connect");
    yajl_gen_map_open(gen);
    yajl_gen_cstr(gen, "calls");
    yajl_gen_integer(gen, stats.connect_calls);
    yajl_gen_cstr(gen, "time");
    yajl_gen_integer(gen, stats.connect_time);
    yajl_gen_map_close(gen);
  }

  if (stats.select_calls > 0) {
    yajl_gen_cstr(gen, "select");
    yajl_gen_map_open(gen);
    yajl_gen_cstr(gen, "calls");
    yajl_gen_integer(gen, stats.select_calls);
    yajl_gen_cstr(gen, "time");
    yajl_gen_integer(gen, stats.select_time);
    yajl_gen_map_close(gen);
  }

  if (stats.poll_calls > 0) {
    yajl_gen_cstr(gen, "poll");
    yajl_gen_map_open(gen);
    yajl_gen_cstr(gen, "calls");
    yajl_gen_integer(gen, stats.poll_calls);
    yajl_gen_cstr(gen, "time");
    yajl_gen_integer(gen, stats.poll_time);
    yajl_gen_map_close(gen);
  }
}

void install_fd_tracer()
{
  tracer.start = fd_trace_start;
  tracer.stop = fd_trace_stop;
  tracer.reset = fd_trace_reset;
  tracer.dump = fd_trace_dump;
  tracer.id = "fd";

  trace_insert(&tracer);
}
