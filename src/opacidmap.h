/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#ifndef OPACIDMAP_H_
#define OPACIDMAP_H_

#include <stdint.h>

#ifndef OPA_NOTHREADS
#include "opamutex.h"
#endif

#include "rbt.h"

typedef struct rbt_node RBTreeNode;
typedef struct rbt RBTree;
typedef int64_t opacid;

typedef struct {
	RBTreeNode node;
	opacid id;
} opacidmapItem;

typedef struct {
#ifndef OPA_NOTHREADS
	opamutex m;
	char sync;
#endif
	RBTree t;
} opacidmap;

void opacidmapInit(opacidmap* m);
#ifndef OPA_NOTHREADS
void opacidmapInitMT(opacidmap* m);
#endif
int opacidmapAdd(opacidmap* m, opacidmapItem* i);
opacidmapItem* opacidmapGet(opacidmap* m, opacid key, int remove);
void opacidmapIterate(opacidmap* m, void* context, void (*cb)(void* context, const opacidmapItem* i));
void opacidmapClose(opacidmap* m);



#endif
