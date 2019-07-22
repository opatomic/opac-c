/*
 * Copyright 2018-2019 Opatomic
 * Open sourced with ISC license. Refer to LICENSE for details.
 */

#include <stddef.h>

#include "opacidmap.h"
#include "opacore.h"
#include "rbt_iter.h"


static int compare(const void* key, const RBTreeNode* node) {
	OASSERT(key != NULL && node != NULL);
	opacid ida = *((opacid*)key);
	opacidmapItem* i = list_entry(node, opacidmapItem, node);
	opacid idb = i->id;
	return ida < idb ? -1 : (ida > idb ? 1 : 0);
}

static void opacidmapLock(opacidmap* m) {
	#ifndef OPA_NOTHREADS
		if (m->sync) {
			opamutexLock(&m->m);
		}
	#else
		UNUSED(m);
	#endif
}

static void opacidmapUnlock(opacidmap* m) {
	#ifndef OPA_NOTHREADS
		if (m->sync) {
			opamutexLock(&m->m);
		}
	#else
		UNUSED(m);
	#endif
}

void opacidmapInit(opacidmap* m) {
	rbt_init(&m->t, compare);
	#ifndef OPA_NOTHREADS
		m->sync = 0;
	#endif
}

#ifndef OPA_NOTHREADS
void opacidmapInitMT(opacidmap* m) {
	opacidmapInit(m);
	opamutexInit(&m->m);
	m->sync = 1;
}
#endif

void opacidmapClose(opacidmap* m) {
	#ifndef OPA_NOTHREADS
		if (m->sync) {
			opamutexDestroy(&m->m);
		}
	#endif
	m->t.root = NULL;
}

// return 1 if added to map; return 0 if not added (because it already exists in map)
int opacidmapAdd(opacidmap* m, opacidmapItem* i) {
	opacidmapLock(m);
	RBTreeNode* existNode = rbt_insert(&m->t, &i->id, &i->node);
	if (existNode != NULL) {
		// id already exists! replace inserted node with original node (err code will be returned)
		opacidmapItem* existItem = list_entry(existNode, opacidmapItem, node);
		rbt_insert(&m->t, &existItem->id, &existItem->node);
	}
	opacidmapUnlock(m);
	return existNode == NULL ? 1 : 0;
}

opacidmapItem* opacidmapGet(opacidmap* m, opacid key, int remove) {
	opacidmapItem* i = NULL;
	opacidmapLock(m);
	RBTreeNode* n = rbt_find(&m->t, &key);
	if (n != NULL) {
		if (remove) {
			rbt_remove(&m->t, n);
		}
		i = list_entry(n, opacidmapItem, node);
	}
	opacidmapUnlock(m);
	return i;
}

void opacidmapIterate(opacidmap* m, void* context, void (*cb)(void* context, const opacidmapItem* i)) {
	opacidmapLock(m);
	struct rbt_node* n = rbt_iter_first(&m->t);
	while (n != NULL) {
		// note: get next pointer in case item is freed in callback
		struct rbt_node* next = rbt_iter_next(n);
		cb(context, list_entry(n, opacidmapItem, node));
		n = next;
	}
	opacidmapUnlock(m);
}
