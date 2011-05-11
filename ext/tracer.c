#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "tracer.h"
#include "util.h"

static yajl_gen tracing_yajl_gen = NULL;

/*
   XXX if we ever need a linked list for anything else ever, remove this crap
       and switch to a generic macro-based linked list implementation
*/
struct tracer_list {
  struct tracer *tracer;
  struct tracer_list *next;
};

static struct tracer_list *tracer_list = NULL;

int
trace_insert(struct tracer *trace)
{
  assert(trace != NULL);

  struct tracer_list *entry = malloc(sizeof(*entry));
  entry->tracer = trace;

  entry->next = tracer_list;
  tracer_list = entry;
  return 0;
}

int
trace_remove(const char *id)
{
  struct tracer_list *tmp, *prev;
  tmp = prev = tracer_list;

  while (tmp) {
    if (strcmp(id, tmp->tracer->id) == 0) {
      tmp->next = tmp->next;
      free(tmp->tracer);
      free(tmp);
      return 0;
    }
    prev = tmp;
    tmp = tmp->next;
  }

  return 1;
}

static void
do_trace_invoke(struct tracer *trace, trace_fn fn)
{
  switch (fn) {
    case TRACE_START:
      trace->start();
      break;
    case TRACE_STOP:
      trace->stop();
      break;
    case TRACE_RESET:
      trace->reset();
      break;
    case TRACE_DUMP:
      yajl_gen_cstr(tracing_yajl_gen, trace->id);
      yajl_gen_map_open(tracing_yajl_gen);
      trace->dump(tracing_yajl_gen);
      yajl_gen_map_close(tracing_yajl_gen);
      break;
    default:
      dbg_printf("invoked a non-existant trace function type: %d", fn);
      assert(1==0);
  }

  return;
}

int
trace_invoke_all(trace_fn fn)
{
  struct tracer_list *tmp = tracer_list;
  while (tmp) {
    do_trace_invoke(tmp->tracer, fn);
    tmp = tmp->next;
  }
  return 0;
}

int
trace_invoke(const char *id, trace_fn fn)
{
  struct tracer_list *tmp = tracer_list;
  while (tmp) {
    if (strcmp(id, tmp->tracer->id) == 0) {
      do_trace_invoke(tmp->tracer, fn);
    }
    tmp = tmp->next;
  }
  return 0;
}

void
trace_set_output(yajl_gen gen)
{
  tracing_yajl_gen = gen;
}

yajl_gen
trace_get_output()
{
  return tracing_yajl_gen;
}
