#ifndef MINIAS_SOURCE_STATEMENT_EVENT_H
#define MINIAS_SOURCE_STATEMENT_EVENT_H

#include <stdbool.h>
#include <stddef.h>

/* Source -> Common is an event boundary, not a shared parser API.
 * Source owns lexical splitting, expansion order and stable site identity;
 * Common owns the only statement parser and all Fragment/Symbol mutations.
 * The event's text is a writable, borrowed slice: the consumer may edit it
 * synchronously, but must not retain it after returning. */
typedef struct MiniAsSourceStatementEvent {
    char *text;
    size_t line;
    size_t site;
} MiniAsSourceStatementEvent;

/* This consumer is pass-local. Macro/REPT/IRP recursion must call the same
 * sink in lexical order; no duplicate parser, queued stale text or second
 * mutable Common session is permitted. */
typedef struct MiniAsSourceStatementSink {
    void *user;
    bool (*consume)(void *user,const MiniAsSourceStatementEvent *event);
} MiniAsSourceStatementSink;

#endif
