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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Julian Eisel, Blender Foundation
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/windowmanager/intern/widgets/wm_widget.cpp
 *  \ingroup wm
 */

#include <string.h>

#include "BKE_context.h"

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_view3d_types.h"
#include "DNA_windowmanager_types.h"

#include "ED_screen.h"
#include "ED_view3d.h"

#include "MEM_guardedalloc.h"

#include "WM_api.h"

#include "wm_widgetmap.h"
#include "wm_widgetgroup.h"
#include "wm_widget.h" // own include

#include "wm.h" // tmp

#if 0
wmWidget::wmWidget()
{
	
}
#endif

/**
 * Free widget data, not widget itself.
 */
void widget_data_free(wmWidget *widget)
{
	if (widget->opptr.data) {
		WM_operator_properties_free(&widget->opptr);
	}

	MEM_freeN(widget->props);
	MEM_freeN(widget->ptr);
}

/**
 * Free and NULL \a widget.
 * \a widgetlist is allowed to be NULL.
 */
void widget_remove(ListBase *widgetlist, wmWidget *widget)
{
	widget_data_free(widget);
	if (widgetlist)
		BLI_remlink(widgetlist, widget);
	MEM_SAFE_FREE(widget);
}

void widget_find_active_3D_loop(const bContext *C, ListBase *visible_widgets)
{
	int selectionbase = 0;
	wmWidget *widget;

	for (LinkData *link = (LinkData *)visible_widgets->first; link; link = link->next) {
		widget = (wmWidget *)link->data;
		/* pass the selection id shifted by 8 bits. Last 8 bits are used for selected widget part id */
		widget->render_3d_intersection(C, widget, selectionbase << 8);

		selectionbase++;
	}
}

/**
 * Add \a widget to selection.
 * Reallocates memory for selected widgets so better not call for selecting multiple ones.
 */
void wm_widget_select(wmWidgetMap *wmap, bContext *C, wmWidget *widget)
{
	wmWidget ***sel = &wmap->wmap_context.selected_widgets;
	int *tot_selected = &wmap->wmap_context.tot_selected;

	if (!widget || (widget->flag & WM_WIDGET_SELECTED))
		return;

	(*tot_selected)++;

	*sel = (wmWidget **)MEM_reallocN(*sel, sizeof(**sel) * (*tot_selected));
	(*sel)[(*tot_selected) - 1] = widget;

	widget->flag |= WM_WIDGET_SELECTED;
	if (widget->select) {
		widget->select(C, widget, SEL_SELECT);
	}
	wmap->set_highlighted_widget(C, widget, widget->highlighted_part);

	ED_region_tag_redraw(CTX_wm_region(C));
}

/**
 * Remove \a widget from selection.
 * Reallocates memory for selected widgets so better not call for selecting multiple ones.
 */
void wm_widget_deselect(wmWidgetMap *wmap, const bContext *C, wmWidget *widget)
{
	wmWidget ***sel = &wmap->wmap_context.selected_widgets;
	int *tot_selected = &wmap->wmap_context.tot_selected;

	/* caller should check! */
	BLI_assert(widget->flag & WM_WIDGET_SELECTED);

	/* remove widget from selected_widgets array */
	for (int i = 0; i < (*tot_selected); i++) {
		if (widget_compare((*sel)[i], widget)) {
			for (int j = i; j < ((*tot_selected) - 1); j++) {
				(*sel)[j] = (*sel)[j + 1];
			}
			break;
		}
	}

	/* update array data */
	if ((*tot_selected) <= 1) {
		MEM_SAFE_FREE(*sel);
		*tot_selected = 0;
	}
	else {
		*sel = (wmWidget **)MEM_reallocN(*sel, sizeof(**sel) * (*tot_selected));
		(*tot_selected)--;
	}

	widget->flag &= ~WM_WIDGET_SELECTED;

	ED_region_tag_redraw(CTX_wm_region(C));
}

void widget_calculate_scale(wmWidget *widget, const bContext *C)
{
	const RegionView3D *rv3d = CTX_wm_region_view3d(C);
	float scale = 1.0f;

	if (rv3d && (U.tw_flag & V3D_3D_WIDGETS) == 0 && (widget->flag & WM_WIDGET_SCALE_3D)) {
		if (widget->get_final_position) {
			float position[3];

			widget->get_final_position(widget, position);
			scale = ED_view3d_pixel_size(rv3d, position) * (float)U.tw_size;
		}
		else {
			scale = ED_view3d_pixel_size(rv3d, widget->origin) * (float)U.tw_size;
		}
	}

	widget->scale = scale * widget->user_scale;
}

bool widget_compare(const wmWidget *a, const wmWidget *b)
{
	return STREQ(a->idname, b->idname);
}

