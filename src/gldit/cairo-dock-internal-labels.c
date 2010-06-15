/**
* This file is a part of the Cairo-Dock project
*
* Copyright : (C) see the 'copyright' file.
* E-mail    : see the 'copyright' file.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>

#include "cairo-dock-modules.h"
#include "cairo-dock-load.h"
#include "cairo-dock-config.h"
#include "cairo-dock-draw.h"
#include "cairo-dock-dock-factory.h"
#include "cairo-dock-dock-facility.h"
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-internal-icons.h"
#include "cairo-dock-container.h"
#define _INTERNAL_MODULE_
#include "cairo-dock-internal-labels.h"

CairoConfigLabels myLabels;

static gboolean get_config (GKeyFile *pKeyFile, CairoConfigLabels *pLabels)
{
	gboolean bFlushConfFileNeeded = FALSE;
	
	gboolean bCustomFont = cairo_dock_get_boolean_key_value (pKeyFile, "Labels", "custom", &bFlushConfFileNeeded, TRUE, NULL, NULL);
	
	gchar *cFontDescription = (bCustomFont ? cairo_dock_get_string_key_value (pKeyFile, "Labels", "police", &bFlushConfFileNeeded, NULL, "Icons", NULL) : NULL);
	if (cFontDescription == NULL)
		cFontDescription = cairo_dock_get_default_system_font ();
	
	PangoFontDescription *fd = pango_font_description_from_string (cFontDescription);
	pLabels->iconTextDescription.cFont = g_strdup (pango_font_description_get_family (fd));
	pLabels->iconTextDescription.iSize = pango_font_description_get_size (fd);
	if (!pango_font_description_get_size_is_absolute (fd))
		pLabels->iconTextDescription.iSize /= PANGO_SCALE;
	if (!bCustomFont)
		pLabels->iconTextDescription.iSize *= 1.33;  // c'est pas beau, mais ca evite de casser tous les themes.
	if (pLabels->iconTextDescription.iSize == 0)
		pLabels->iconTextDescription.iSize = 14;
	pLabels->iconTextDescription.iWeight = pango_font_description_get_weight (fd);
	pLabels->iconTextDescription.iStyle = pango_font_description_get_style (fd);
	
	if (g_key_file_has_key (pKeyFile, "Labels", "size", NULL))  // anciens parametres.
	{
		pLabels->iconTextDescription.iSize = g_key_file_get_integer (pKeyFile, "Labels", "size", NULL);
		int iLabelWeight = g_key_file_get_integer (pKeyFile, "Labels", "weight", NULL);
		pLabels->iconTextDescription.iWeight = cairo_dock_get_pango_weight_from_1_9 (iLabelWeight);
		gboolean bLabelStyleItalic = g_key_file_get_boolean (pKeyFile, "Labels", "italic", NULL);
		if (bLabelStyleItalic)
			pLabels->iconTextDescription.iStyle = PANGO_STYLE_ITALIC;
		else
			pLabels->iconTextDescription.iStyle = PANGO_STYLE_NORMAL;
		
		pango_font_description_set_size (fd, pLabels->iconTextDescription.iSize * PANGO_SCALE);
		pango_font_description_set_weight (fd, pLabels->iconTextDescription.iWeight);
		pango_font_description_set_style (fd, pLabels->iconTextDescription.iStyle);
		
		g_free (cFontDescription);
		cFontDescription = pango_font_description_to_string (fd);
		g_key_file_set_string (pKeyFile, "Labels", "police", cFontDescription);
		bFlushConfFileNeeded = TRUE;
	}
	pango_font_description_free (fd);
	g_free (cFontDescription);
	
	int iShowLabel = cairo_dock_get_integer_key_value (pKeyFile, "Labels", "show_labels", &bFlushConfFileNeeded, -1, NULL, NULL);
	gboolean bShow, bLabelForPointedIconOnly;
	if (iShowLabel == -1)  // nouveau parametre
	{
		if (g_key_file_has_key (pKeyFile, "Labels", "show labels", NULL))
			bShow = g_key_file_get_boolean (pKeyFile, "Labels", "show labels", NULL);
		else
			bShow = TRUE;
		bLabelForPointedIconOnly = g_key_file_get_boolean (pKeyFile, "System", "pointed icon only", NULL);
		iShowLabel = (! bShow ? 0 : (bLabelForPointedIconOnly ? 1 : 2));
		g_key_file_set_integer (pKeyFile, "Labels", "show_labels", iShowLabel);
	}
	else
	{
		bShow = (iShowLabel != 0);
		bLabelForPointedIconOnly = (iShowLabel == 1);
	}
	//g_print ("labels : %d;%d\n", bShow, bLabelForPointedIconOnly);
	if (! bShow)
	{
		g_free (pLabels->iconTextDescription.cFont);
		pLabels->iconTextDescription.cFont = NULL;
		pLabels->iconTextDescription.iSize = 0;
	}
	pLabels->bLabelForPointedIconOnly = bLabelForPointedIconOnly;
	
	pLabels->iconTextDescription.bOutlined = cairo_dock_get_boolean_key_value (pKeyFile, "Labels", "text oulined", &bFlushConfFileNeeded, TRUE, NULL, NULL);
	
	double couleur_label[3] = {1., 1., 1.};
	cairo_dock_get_double_list_key_value (pKeyFile, "Labels", "text color start", &bFlushConfFileNeeded, pLabels->iconTextDescription.fColorStart, 3, couleur_label, "Icons", NULL);
	
	cairo_dock_get_double_list_key_value (pKeyFile, "Labels", "text color stop", &bFlushConfFileNeeded, pLabels->iconTextDescription.fColorStop, 3, couleur_label, "Icons", NULL);
	
	pLabels->iconTextDescription.bVerticalPattern = cairo_dock_get_boolean_key_value (pKeyFile, "Labels", "vertical label pattern", &bFlushConfFileNeeded, TRUE, "Icons", NULL);

	double couleur_backlabel[4] = {0., 0., 0., 0.5};
	cairo_dock_get_double_list_key_value (pKeyFile, "Labels", "text background color", &bFlushConfFileNeeded, pLabels->iconTextDescription.fBackgroundColor, 4, couleur_backlabel, "Icons", NULL);
	
	pLabels->iconTextDescription.iMargin = cairo_dock_get_integer_key_value (pKeyFile, "Labels", "text margin", &bFlushConfFileNeeded, 4, NULL, NULL);
	
	memcpy (&pLabels->quickInfoTextDescription, &pLabels->iconTextDescription, sizeof (CairoDockLabelDescription));
	pLabels->quickInfoTextDescription.cFont = g_strdup (pLabels->iconTextDescription.cFont);
	pLabels->quickInfoTextDescription.iSize = 12;
	pLabels->quickInfoTextDescription.iWeight = PANGO_WEIGHT_HEAVY;
	pLabels->quickInfoTextDescription.iStyle = PANGO_STYLE_NORMAL;
	
	gboolean bUseBackgroundForLabel = cairo_dock_get_boolean_key_value (pKeyFile, "Labels", "background for label", &bFlushConfFileNeeded, FALSE, "Icons", NULL);
	if (! bUseBackgroundForLabel)
		pLabels->iconTextDescription.fBackgroundColor[3] = 0;  // ne sera pas dessine.
	
	pLabels->iLabelSize = (pLabels->iconTextDescription.iSize != 0 ?
		pLabels->iconTextDescription.iSize +
		(pLabels->iconTextDescription.bOutlined ? 2 : 0) +
		2 * pLabels->iconTextDescription.iMargin : 0);
	
	return bFlushConfFileNeeded;
}


static void reset_config (CairoConfigLabels *pLabels)
{
	g_free (pLabels->iconTextDescription.cFont);
	g_free (pLabels->quickInfoTextDescription.cFont);
}


static void _reload_one_label (Icon *pIcon, CairoContainer *pContainer, CairoConfigLabels *pLabels)
{
	cairo_dock_load_icon_text (pIcon, &pLabels->iconTextDescription);
	double fMaxScale = cairo_dock_get_max_scale (pContainer);
	cairo_dock_load_icon_quickinfo (pIcon, &pLabels->quickInfoTextDescription, fMaxScale);
}
static void _cairo_dock_resize_one_dock (gchar *cDockName, CairoDock *pDock, gpointer data)
{
	cairo_dock_update_dock_size (pDock);
}
static void reload (CairoConfigLabels *pPrevLabels, CairoConfigLabels *pLabels)
{
	cairo_dock_foreach_icons ((CairoDockForeachIconFunc) _reload_one_label, pLabels);
	
	if (pPrevLabels->iLabelSize != pLabels->iLabelSize)
	{
		cairo_dock_foreach_docks ((GHFunc) _cairo_dock_resize_one_dock, NULL);
	}
}


DEFINE_PRE_INIT (Labels)
{
	pModule->cModuleName = "Labels";
	pModule->cTitle = N_("Captions");
	pModule->cIcon = "icon-labels.svg";
	pModule->cDescription = N_("Define icon caption and quick-info style.");
	pModule->iCategory = CAIRO_DOCK_CATEGORY_THEME;
	pModule->iSizeOfConfig = sizeof (CairoConfigLabels);
	pModule->iSizeOfData = 0;
	
	pModule->reload = (CairoDockInternalModuleReloadFunc) reload;
	pModule->get_config = (CairoDockInternalModuleGetConfigFunc) get_config;
	pModule->reset_config = (CairoDockInternalModuleResetConfigFunc) reset_config;
	pModule->reset_data = NULL;
	
	pModule->pConfig = (CairoInternalModuleConfigPtr) &myLabels;
	pModule->pData = NULL;
}