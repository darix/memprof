#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "json.h"
#include "tracer.h"
#include "tramp.h"
#include "util.h"

struct memprof_resources_stats {
  long nsignals;

  long inblock;
  long oublock;

  int64_t utime;
  int64_t stime;
};

static struct tracer tracer;
static struct memprof_resources_stats stats;

static void
resources_trace_start() {
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);

  stats.nsignals = -usage.ru_nsignals;

  stats.inblock = -usage.ru_inblock;
  stats.oublock = -usage.ru_oublock;

  stats.stime = -TVAL_TO_INT64(usage.ru_stime);
  stats.utime = -TVAL_TO_INT64(usage.ru_utime);
}

static void
resources_trace_dump(yajl_gen gen) {
  { // calculate diff before dump, since stop is called after dump
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);

    stats.nsignals += usage.ru_nsignals;

    stats.inblock += usage.ru_inblock;
    stats.oublock += usage.ru_oublock;

    stats.stime += TVAL_TO_INT64(usage.ru_stime);
    stats.utime += TVAL_TO_INT64(usage.ru_utime);
  }

  yajl_gen_cstr(gen, "signals");
  yajl_gen_integer(gen, stats.nsignals);

  yajl_gen_cstr(gen, "inputs");
  yajl_gen_integer(gen, stats.inblock);

  yajl_gen_cstr(gen, "outputs");
  yajl_gen_integer(gen, stats.oublock);

  yajl_gen_cstr(gen, "stime");
  yajl_gen_integer(gen, stats.stime);

  yajl_gen_cstr(gen, "utime");
  yajl_gen_integer(gen, stats.utime);
}

static void
resources_trace_stop() {
}

static void
resources_trace_reset() {
}

void install_resources_tracer()
{
  tracer.start = resources_trace_start;
  tracer.stop = resources_trace_stop;
  tracer.reset = resources_trace_reset;
  tracer.dump = resources_trace_dump;
  tracer.id = "resources";

  trace_insert(&tracer);
}
