#ifndef MINIAS_SOURCE_STATEMENT_EVENT_H
#define MINIAS_SOURCE_STATEMENT_EVENT_H

#include <stdbool.h>
#include <stddef.h>

/* Source -> Common is an event boundary, not a shared parser API.
 * Source owns lexical splitting, label recognition, expansion order and
 * stable site identity; Common exclusively defines the recognized label or
 * parses the ordinary statement and mutates Symbols/Fragments. Both kinds
 * carry writable, synchronously borrowed text: LABEL is a bare identifier
 * without ':'; TEXT is a non-label statement. Neither may be retained. */
typedef enum MiniAsSourceStatementKind {
    MINIAS_SOURCE_STATEMENT_TEXT,
    MINIAS_SOURCE_STATEMENT_LABEL
} MiniAsSourceStatementKind;

typedef struct MiniAsSourceStatementEvent {
    char *text;
    size_t line;
    size_t site;
    MiniAsSourceStatementKind kind;
} MiniAsSourceStatementEvent;

/* This consumer is pass-local. Macro/REPT/IRP recursion must call the same
 * sink in lexical order; no duplicate parser, queued stale text or second
 * mutable Common session is permitted. */
typedef struct MiniAsSourceStatementSink {
    void *user;
    bool (*consume)(void *user,const MiniAsSourceStatementEvent *event);
} MiniAsSourceStatementSink;

#endif
