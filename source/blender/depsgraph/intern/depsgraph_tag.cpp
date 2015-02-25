/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2013 Blender Foundation.
 * All rights reserved.
 *
 * Original Author: Joshua Leung
 * Contributor(s): None Yet
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 * Core routines for how the Depsgraph works
 */

#include <stdio.h>
#include <cstring>
#include <queue>

extern "C" {
#include "BLI_utildefines.h"

#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_screen_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_node.h"

#include "DEG_depsgraph.h"

/* TODO(sergey): because of bloody "new" in the BKE_screen.h. */
unsigned int BKE_screen_visible_layers(bScreen *screen, Scene *scene);
} /* extern "C" */

#include "depsgraph_debug.h"
#include "depsnode.h"
#include "depsnode_component.h"
#include "depsnode_operation.h"
#include "depsgraph_intern.h"

/* ************************************************** */
/* Update Tagging/Flushing */

/* Data-Based Tagging ------------------------------- */

static void lib_id_recalc_tag(Main *bmain, ID *id)
{
	id->flag |= LIB_ID_RECALC;
	DEG_id_type_tag(bmain, GS(id->name));
}

static void lib_id_recalc_data_tag(Main *bmain, ID *id)
{
	id->flag |= LIB_ID_RECALC_DATA;
	DEG_id_type_tag(bmain, GS(id->name));
}

static void lib_id_recalc_tag_flag(Main *bmain, ID *id, int flag)
{
	if (flag) {
		/* This bit of code ensures legacy object->recalc flags
		 * are still filled in the same way as it was expected
		 * with the old dependency graph.
		 *
		 * This is because some areas like motion paths and likely
		 * some other physics baking process are doing manual scene
		 * update on all the frames, trying to minimize number of
		 * updates.
		 *
		 * But this flag will also let us to re-construct entry
		 * nodes for update after relations update and after layer
		 * visibility changes.
		 */
		short idtype = GS(id->name);
		if (idtype == ID_OB) {
			Object *object = (Object *)id;
			object->recalc |= (flag & OB_RECALC_ALL);
		}

		if (flag & OB_RECALC_OB)
			lib_id_recalc_tag(bmain, id);
		if (flag & (OB_RECALC_DATA | PSYS_RECALC))
			lib_id_recalc_data_tag(bmain, id);
	}
	else {
		lib_id_recalc_tag(bmain, id);
	}
}

static void anim_data_tag_update(Depsgraph *graph, ID *id)
{
	IDDepsNode *id_node = graph->find_id_node(id);
	if (id_node != NULL) {
		ComponentDepsNode *anim_comp = id_node->find_component(DEPSNODE_TYPE_ANIMATION);
		if (anim_comp != NULL) {
			anim_comp->tag_update(graph);
		}
	}
}

/* Tag all nodes in ID-block for update.
 * This is a crude measure, but is most convenient for old code.
 */
void DEG_graph_id_tag_update(Main *bmain, Depsgraph *graph, ID *id)
{
	IDDepsNode *node = graph->find_id_node(id);
	lib_id_recalc_tag(bmain, id);
	if (node != NULL) {
		node->tag_update(graph);
	}
}

/* Tag nodes related to a specific piece of data */
void DEG_graph_data_tag_update(Depsgraph *graph, const PointerRNA *ptr)
{
	DepsNode *node = graph->find_node_from_pointer(ptr, NULL);
	if (node) {
		node->tag_update(graph);
	}
	else {
		printf("Missing node in %s\n", __func__);
		BLI_assert(!"Shouldn't happens since it'll miss crucial update.");
	}
}

/* Tag nodes related to a specific property. */
void DEG_graph_property_tag_update(Depsgraph *graph,
                                   const PointerRNA *ptr,
                                   const PropertyRNA *prop)
{
	DepsNode *node = graph->find_node_from_pointer(ptr, prop);
	if (node) {
		node->tag_update(graph);
	}
	else {
		printf("Missing node in %s\n", __func__);
		BLI_assert(!"Shouldn't happens since it'll miss crucial update.");
	}
}

/* Tag given ID for an update in all the dependency graphs. */
void DEG_id_tag_update(ID *id, short flag)
{
	DEG_id_tag_update_ex(G.main, id, flag);
}

void DEG_id_tag_update_ex(Main *bmain, ID *id, short flag)
{
	if(id == NULL) {
		/* Ideally should not happen, but old depsgraph allowed this. */
		return;
	}
	DEG_DEBUG_PRINTF("%s: id=%s flag=%d\n", __func__, id->name, flag);
	lib_id_recalc_tag_flag(bmain, id, flag);
	for (Scene *scene = (Scene *)bmain->scene.first;
	     scene != NULL;
	     scene = (Scene *)scene->id.next)
	{
		if (scene->depsgraph) {
			Depsgraph *graph = scene->depsgraph;
			if (flag == 0) {
				/* TODO(sergey): Currently blender is still tagging IDs
				 * for recalc just using flag=0. This isn't totally correct
				 * but we'd better deal with such cases and don't fail.
				 */
				DEG_graph_id_tag_update(bmain, graph, id);
				continue;
			}
			if (flag & OB_RECALC_DATA && GS(id->name) == ID_OB) {
				Object *object = (Object*)id;
				if (object->data != NULL) {
					DEG_graph_id_tag_update(bmain,
					                        graph,
					                        (ID*)object->data);
				}
				if (flag & OB_RECALC_TIME) {
					anim_data_tag_update(graph, (ID*)object->data);
				}
			}
			if (flag & (OB_RECALC_OB|OB_RECALC_DATA)) {
				DEG_graph_id_tag_update(bmain, graph, id);
			}
			if (flag & OB_RECALC_TIME) {
				anim_data_tag_update(graph, id);
			}
		}
	}
}

/* Tag given ID type for update. */
void DEG_id_type_tag(Main *bmain, short idtype)
{
	if (idtype == ID_NT) {
		/* Stupid workaround so parent datablocks of nested nodetree get looped
		 * over when we loop over tagged datablock types.
		 */
		DEG_id_type_tag(bmain, ID_MA);
		DEG_id_type_tag(bmain, ID_TE);
		DEG_id_type_tag(bmain, ID_LA);
		DEG_id_type_tag(bmain, ID_WO);
		DEG_id_type_tag(bmain, ID_SCE);
	}
	/* We tag based on first ID type character to avoid
	 * looping over all ID's in case there are no tags.
	 */
	bmain->id_tag_update[((unsigned char *)&idtype)[0]] = 1;
}

/* Update Flushing ---------------------------------- */

/* FIFO queue for tagged nodes that need flushing */
/* XXX This may get a dedicated implementation later if needed - lukas */
typedef std::queue<OperationDepsNode*> FlushQueue;

/* Flush updates from tagged nodes outwards until all affected nodes are tagged. */
void DEG_graph_flush_updates(Main *bmain,
                             EvaluationContext *eval_ctx,
                             Depsgraph *graph)
{
	/* sanity check */
	if (graph == NULL)
		return;

	FlushQueue queue;
	/* Starting from the tagged "entry" nodes, flush outwards... */
	// NOTE: Also need to ensure that for each of these, there is a path back to
	//       root, or else they won't be done.
	// NOTE: Count how many nodes we need to handle - entry nodes may be
	//       component nodes which don't count for this purpose!
	for (Depsgraph::EntryTags::const_iterator it = graph->entry_tags.begin();
	     it != graph->entry_tags.end();
	     ++it)
	{
		OperationDepsNode *node = *it;
		IDDepsNode *id_node = node->owner->owner;
		queue.push(node);
		deg_editors_id_update(bmain, id_node->id);
	}

	while (!queue.empty()) {
		OperationDepsNode *node = queue.front();
		queue.pop();

		IDDepsNode *id_node = node->owner->owner;
		lib_id_recalc_tag(bmain, id_node->id);
		/* TODO(sergey): For until we've got proper data nodes in the graph. */
		lib_id_recalc_data_tag(bmain, id_node->id);

		/* Flush to nodes along links... */
		for (OperationDepsNode::Relations::const_iterator it = node->outlinks.begin();
		     it != node->outlinks.end();
		     ++it)
		{
			DepsRelation *rel = *it;
			OperationDepsNode *to_node = (OperationDepsNode *)rel->to;
			IDDepsNode *id_node = to_node->owner->owner;
			if ((to_node->flag & DEPSOP_FLAG_NEEDS_UPDATE) == 0) {
				ID *id = id_node->id;
				/* This code is used to preserve those areas which does direct
				 * object update,
				 *
				 * Plus it ensures visibility changes and relations and layers
				 * visibility update has proper flags to work with.
				 */
				if (GS(id->name) == ID_OB) {
					Object *object = (Object *)id;
					ComponentDepsNode *comp_node = to_node->owner;
					if (comp_node->type == DEPSNODE_TYPE_ANIMATION) {
						object->recalc |= OB_RECALC_TIME;
					}
					else if (comp_node->type == DEPSNODE_TYPE_TRANSFORM) {
						object->recalc |= OB_RECALC_OB;
					}
					else {
						object->recalc |= OB_RECALC_DATA;
					}
				}
				to_node->flag |= DEPSOP_FLAG_NEEDS_UPDATE;
				queue.push(to_node);
				deg_editors_id_update(bmain, id_node->id);
			}
		}
	}

	/* Clear entry tags, since all tagged nodes should now be reachable from root. */
	graph->entry_tags.clear();
}

/* Clear tags from all operation nodes. */
void DEG_graph_clear_tags(Depsgraph *graph)
{
	/* Go over all operation nodes, clearing tags. */
	for (Depsgraph::OperationNodes::const_iterator it = graph->operations.begin();
	     it != graph->operations.end();
	     ++it)
	{
		OperationDepsNode *node = *it;

		/* Clear node's "pending update" settings. */
		node->flag &= ~(DEPSOP_FLAG_DIRECTLY_MODIFIED | DEPSOP_FLAG_NEEDS_UPDATE);
		/* Reset so that it can be bumped up again. */
		node->num_links_pending = 0;
		node->scheduled = false;
	}

	/* Clear any entry tags which haven't been flushed. */
	graph->entry_tags.clear();
}

/* Update dependency graph when visible scenes/layers changes. */
void DEG_graph_on_visible_update(Main *bmain, Scene *scene)
{
	Depsgraph *graph = scene->depsgraph;
	wmWindowManager *wm = (wmWindowManager *)bmain->wm.first;
	int old_layers = graph->layers;
	if (wm != NULL) {
		BKE_main_id_flag_listbase(&bmain->scene, LIB_DOIT, true);
		graph->layers = 0;
		for (wmWindow *win = (wmWindow *)wm->windows.first;
		     win != NULL;
		     win = (wmWindow *)win->next)
		{
			Scene *scene = win->screen->scene;
			if (scene->id.flag & LIB_DOIT) {
				graph->layers |= BKE_screen_visible_layers(win->screen, scene);
				scene->id.flag &= ~LIB_DOIT;
			}
		}
	}
	else {
		/* All the layers for background render for now. */
		graph->layers = (1 << 20) - 1;
	}
	if (old_layers != graph->layers) {
		/* Tag all objects which becomes visible (or which becomes needed for dependencies)
		 * for recalc.
		 *
		 * This is mainly needed on file load only, after that updates of invisible objects
		 * will be stored in the pending list.
		 */
		for (Depsgraph::OperationNodes::const_iterator it = graph->operations.begin();
		     it != graph->operations.end();
		     ++it)
		{
			OperationDepsNode *node = *it;
			IDDepsNode *id_node = node->owner->owner;
			ID *id = id_node->id;
			if ((id->flag & LIB_ID_RECALC_ALL) != 0 ||
			    (id_node->layers & scene->lay_updated) == 0)
			{
				id_node->tag_update(graph);
			}
			/* A bit of magic: if object->recalc is set it means somebody tagged
			 * it for update. If corresponding ID recalc flags are zero it means
			 * graph has been evaluated after that and the recalc was skipped
			 * because of visibility check.
			 */
			if (GS(id->name) == ID_OB) {
				Object *object = (Object *)id;
				if ((id->flag & LIB_ID_RECALC_ALL) == 0 &&
				    (object->recalc & OB_RECALC_ALL) != 0)
				{
					id_node->tag_update(graph,
					                    (object->recalc & OB_RECALC_TIME) != 0);
				}
			}
		}
	}
	scene->lay_updated |= graph->layers;
}

void DEG_on_visible_update(Main *bmain, const bool do_time)
{
	for (Scene *scene = (Scene*)bmain->scene.first;
	     scene != NULL;
	     scene = (Scene*)scene->id.next)
	{
		if (scene->depsgraph != NULL) {
			DEG_graph_on_visible_update(bmain, scene);
		}
	}
}

/* Check if something was changed in the database and inform
 * editors about this.
 */
void DEG_ids_check_recalc(Main *bmain, Scene *scene, bool time)
{
	ListBase *lbarray[MAX_LIBARRAY];
	int a;
	bool updated = false;

	/* Loop over all ID types. */
	a  = set_listbasepointers(bmain, lbarray);
	while (a--) {
		ListBase *lb = lbarray[a];
		ID *id = (ID*)lb->first;

		/* We tag based on first ID type character to avoid
		 * looping over all ID's in case there are no tags.
		 */
		if (id && bmain->id_tag_update[(unsigned char)id->name[0]]) {
			updated = true;
			break;
		}
	}

	deg_editors_scene_update(bmain, scene, (updated || time));
}

void DEG_ids_clear_recalc(Main *bmain)
{
	ListBase *lbarray[MAX_LIBARRAY];
	bNodeTree *ntree;
	int a;

	/* TODO(sergey): Re-implement POST_UPDATE_HANDLER_WORKAROUND using entry_tags
	 * and id_tags storage from the new depenency graph.
	 */

	/* Loop over all ID types. */
	a  = set_listbasepointers(bmain, lbarray);
	while (a--) {
		ListBase *lb = lbarray[a];
		ID *id = (ID *)lb->first;

		/* We tag based on first ID type character to avoid
		 * looping over all ID's in case there are no tags.
		 */
		if (id && bmain->id_tag_update[(unsigned char)id->name[0]]) {
			for (; id; id = (ID *)id->next) {
				id->flag &= ~(LIB_ID_RECALC | LIB_ID_RECALC_DATA);

				/* Some ID's contain semi-datablock nodetree */
				ntree = ntreeFromID(id);
				if (ntree != NULL) {
					ntree->id.flag &= ~(LIB_ID_RECALC | LIB_ID_RECALC_DATA);
				}
			}
		}
	}

	memset(bmain->id_tag_update, 0, sizeof(bmain->id_tag_update));
}