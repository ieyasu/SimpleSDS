/* util.c - Utility functions for SDS converter.
 * Copyright (C) 2011 Matthew Bishop
 */
#include "sds.h"
#include <stdio.h>
#include <string.h>

void *sds_alloc(size_t bytes)
{
    void *p = malloc(bytes);
    if (!p) {
        fprintf(stderr, "Failed to malloc(%u)\n", (unsigned)bytes);
        abort();
    }
    return p;
}

void *sds_alloc0(size_t bytes)
{
    void *p = calloc(1, bytes);
    if (!p) {
        fprintf(stderr, "Failed to calloc(1, %u)\n", (unsigned)bytes);
        abort();
    }
    return p;
}

void *sds_realloc(void *ptr, size_t bytes)
{
    void *p = realloc(ptr, bytes);
    if (!p) {
        fprintf(stderr, "Failed to realloc(%p, %u)\n", ptr, (unsigned)bytes);
        abort();
    }
    return p;
}

char *sds_strdup(const char *s)
{
    char *t = sds_alloc(strlen(s) + 1);
    strcpy(t, s);
    return t;
}

static SDSList *list_r(SDSList *prev, SDSList *l)
{
    SDSList *next = l->next;
    l->next = prev;
    return next ? list_r(l, next) : l;
}

size_t sds_list_count(SDSList *l)
{
    size_t n = 0;
    while (l) {
        n++;
        l = l->next;
    }
    return n;
}

SDSList *sds_list_reverse(SDSList *l)
{
    if (!l) return NULL;
    return list_r(NULL, l);
}

SDSList *sds_list_find(SDSList *l, char *key)
{
    for (; l != NULL; l = l->next) {
        if (!strcmp(key, l->key))
            return l;
    }
    return NULL;
}
