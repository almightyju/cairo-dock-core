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

#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include <cairo.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <gdk/gdkx.h>

#ifdef HAVE_GLITZ
#include <gdk/gdkx.h>
#include <glitz-glx.h>
#include <cairo-glitz.h>
#endif
#include <gtk/gtkgl.h>
#include <GL/glu.h>

#include "cairo-dock-menu.h"
#include "cairo-dock-draw.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-load.h"
#include "cairo-dock-icons.h"
#include "cairo-dock-applications-manager.h"
#include "cairo-dock-desktop-file-factory.h"
#include "cairo-dock-launcher-factory.h"
#include "cairo-dock-config.h"
#include "cairo-dock-container.h"
#include "cairo-dock-dock-facility.h"
#include "cairo-dock-notifications.h"
#include "cairo-dock-themes-manager.h"
#include "cairo-dock-dialogs.h"
#include "cairo-dock-file-manager.h"
#include "cairo-dock-log.h"
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-keybinder.h"
#include "cairo-dock-desklet.h"
#include "cairo-dock-draw-opengl.h"
#include "cairo-dock-flying-container.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-renderer-manager.h"
#include "cairo-dock-internal-accessibility.h"
#include "cairo-dock-internal-system.h"
#include "cairo-dock-internal-taskbar.h"
#include "cairo-dock-internal-views.h"
#include "cairo-dock-internal-labels.h"
#include "cairo-dock-internal-icons.h"
#include "cairo-dock-internal-background.h"
#include "cairo-dock-class-manager.h"
#include "cairo-dock-X-utilities.h"
#include "cairo-dock-callbacks.h"

static Icon *s_pIconClicked = NULL;  // pour savoir quand on deplace une icone a la souris. Dangereux si l'icone se fait effacer en cours ...
static int s_iClickX, s_iClickY;  // coordonnees du clic dans le dock, pour pouvoir initialiser le deplacement apres un seuil.
static CairoDock *s_pLastPointedDock = NULL;  // pour savoir quand on passe d'un dock a un autre.
static int s_iSidShowSubDockDemand = 0;
static int s_iSidShowAppliForDrop = 0;
static CairoDock *s_pDockShowingSubDock = NULL;  // on n'accede pas a son contenu, seulement l'adresse.
static CairoFlyingContainer *s_pFlyingContainer = NULL;

extern CairoDock *g_pMainDock;
extern gboolean g_bKeepAbove;

extern gint g_iXScreenWidth[2];
extern gint g_iXScreenHeight[2];
extern cairo_surface_t *g_pBackgroundSurfaceFull[2];

extern gboolean g_bUseOpenGL;
extern gboolean g_bLocked;
extern gboolean g_bEasterEggs;

static gboolean s_bHideAfterShortcut = FALSE;
static gboolean s_bFrozenDock = FALSE;
static gboolean s_bIconDragged = FALSE;

#define _mouse_is_really_outside(pDock) (pDock->container.iMouseX <= 0 || pDock->container.iMouseX >= pDock->container.iWidth || pDock->container.iMouseY <= 0 || pDock->container.iMouseY >= pDock->container.iHeight)
#define CD_CLICK_ZONE 5

void cairo_dock_freeze_docks (gboolean bFreeze)
{
	s_bFrozenDock = bFreeze;
}

gboolean cairo_dock_render_dock_notification (gpointer pUserData, CairoDock *pDock, cairo_t *pCairoContext)
{
	if (! pCairoContext)  // on n'a pas mis le rendu cairo ici a cause du rendu optimise.
	{
		glLoadIdentity ();
		glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | (pDock->pRenderer->bUseStencil ? GL_STENCIL_BUFFER_BIT : 0));
		cairo_dock_apply_desktop_background (CAIRO_CONTAINER (pDock));
		
		if (pDock->fHideOffset != 0)
		{
			double dy = pDock->iMaxDockHeight * pDock->fHideOffset * (pDock->container.bDirectionUp ? -1 : 1);
			if (pDock->container.bIsHorizontal)
				glTranslatef (0., dy, 0.);
			else
				glTranslatef (-dy, 0., 0.);
		}
		pDock->pRenderer->render_opengl (pDock);
	}
	return CAIRO_DOCK_LET_PASS_NOTIFICATION;
}

gboolean cairo_dock_on_expose (GtkWidget *pWidget,
	GdkEventExpose *pExpose,
	CairoDock *pDock)
{
	//g_print ("%s ((%d;%d) %dx%d) (%d)\n", __func__, pExpose->area.x, pExpose->area.y, pExpose->area.width, pExpose->area.height, pDock->bAtBottom);
	if (g_bUseOpenGL && pDock->pRenderer->render_opengl != NULL)
	{
		GdkGLContext *pGlContext = gtk_widget_get_gl_context (pDock->container.pWidget);
		GdkGLDrawable *pGlDrawable = gtk_widget_get_gl_drawable (pDock->container.pWidget);
		if (!gdk_gl_drawable_gl_begin (pGlDrawable, pGlContext))
			return FALSE;
		
		if (pExpose->area.x + pExpose->area.y != 0)
		{
			glEnable (GL_SCISSOR_TEST);  // ou comment diviser par 4 l'occupation CPU !
			glScissor ((int) pExpose->area.x,
				(int) (pDock->container.bIsHorizontal ? pDock->container.iHeight : pDock->container.iWidth) -
					pExpose->area.y - pExpose->area.height,  // lower left corner of the scissor box.
				(int) pExpose->area.width,
				(int) pExpose->area.height);
		}
		
		if (cairo_dock_is_loading ())
		{
			// on laisse transparent
		}
		else if (cairo_dock_is_hidden (pDock))
		{
			cairo_dock_render_hidden_dock_opengl (pDock);
		}
		else
		{
			cairo_dock_notify_on_container (CAIRO_CONTAINER (pDock), CAIRO_DOCK_RENDER_DOCK, pDock, NULL);
		}
		glDisable (GL_SCISSOR_TEST);
		
		if (gdk_gl_drawable_is_double_buffered (pGlDrawable))
			gdk_gl_drawable_swap_buffers (pGlDrawable);
		else
			glFlush ();
		gdk_gl_drawable_gl_end (pGlDrawable);
		
		return FALSE ;
	}
	
	if (pExpose->area.x + pExpose->area.y != 0)  // x et/ou y sont > 0.
	{
		if (! cairo_dock_is_hidden (pDock))
		{
			cairo_t *pCairoContext = cairo_dock_create_drawing_context_on_area (CAIRO_CONTAINER (pDock), &pExpose->area, NULL);
			
			if (pDock->fHideOffset != 0)
			{
				double dy = pDock->iMaxDockHeight * pDock->fHideOffset * (pDock->container.bDirectionUp ? 1 : -1);
				if (pDock->container.bIsHorizontal)
					cairo_translate (pCairoContext, 0., dy);
				else
					cairo_translate (pCairoContext, dy, 0.);
			}
			
			if (pDock->pRenderer->render_optimized != NULL)
				pDock->pRenderer->render_optimized (pCairoContext, pDock, &pExpose->area);
			else
				pDock->pRenderer->render (pCairoContext, pDock);
			cairo_dock_notify_on_container (CAIRO_CONTAINER (pDock), CAIRO_DOCK_RENDER_DOCK, pDock, pCairoContext);
			
			cairo_destroy (pCairoContext);
		}
		return FALSE;
	}
	
	
	cairo_t *pCairoContext = cairo_dock_create_drawing_context (CAIRO_CONTAINER (pDock));
	
	if (cairo_dock_is_loading ())  // transparent pendant le chargement.
	{
		
	}
	else if (cairo_dock_is_hidden (pDock))
	{
		cairo_dock_render_hidden_dock (pCairoContext, pDock);
	}
	else
	{
		if (pDock->fHideOffset != 0)
		{
			double dy = pDock->iMaxDockHeight * pDock->fHideOffset * (pDock->container.bDirectionUp ? 1 : -1);
			if (pDock->container.bIsHorizontal)
				cairo_translate (pCairoContext, 0., dy);
			else
				cairo_translate (pCairoContext, dy, 0.);
		}
		pDock->pRenderer->render (pCairoContext, pDock);
		cairo_dock_notify_on_container (CAIRO_CONTAINER (pDock), CAIRO_DOCK_RENDER_DOCK, pDock, pCairoContext);
	}
	
	cairo_destroy (pCairoContext);
	
#ifdef HAVE_GLITZ
	if (pDock->container.pDrawFormat && pDock->container.pDrawFormat->doublebuffer)
		glitz_drawable_swap_buffers (pDock->container.pGlitzDrawable);
#endif
	return FALSE;
}


static gboolean _cairo_dock_show_sub_dock_delayed (CairoDock *pDock)
{
	cd_debug ("");
	s_iSidShowSubDockDemand = 0;
	s_pDockShowingSubDock = NULL;
	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);
	if (icon != NULL && icon->pSubDock != NULL)
		cairo_dock_show_subdock (icon, pDock, FALSE);

	return FALSE;
}
static gboolean _cairo_dock_show_xwindow_for_drop (gpointer data)
{
	Window Xid = GPOINTER_TO_INT (data);
	cairo_dock_show_xwindow (Xid);
	s_iSidShowAppliForDrop = 0;
	return FALSE;
}
void cairo_dock_on_change_icon (Icon *pLastPointedIcon, Icon *pPointedIcon, CairoDock *pDock)
{
	//g_print ("%s (%x;%x)\n", __func__, pLastPointedIcon, pPointedIcon);
	//cd_debug ("on change d'icone dans %x (-> %s)", pDock, (pPointedIcon != NULL ? pPointedIcon->cName : "rien"));
	if (s_iSidShowSubDockDemand != 0 && pDock == s_pDockShowingSubDock)
	{
		//cd_debug ("on annule la demande de montrage de sous-dock");
		g_source_remove (s_iSidShowSubDockDemand);
		s_iSidShowSubDockDemand = 0;
		s_pDockShowingSubDock = NULL;
	}
	
	if (pDock->bIsDragging && s_iSidShowAppliForDrop != 0)
	{
		//cd_debug ("on annule la demande de montrage d'appli");
		g_source_remove (s_iSidShowAppliForDrop);
		s_iSidShowAppliForDrop = 0;
	}
	cairo_dock_replace_all_dialogs ();
	if (pDock->bIsDragging && CAIRO_DOCK_IS_APPLI (pPointedIcon))
	{
		s_iSidShowAppliForDrop = g_timeout_add (500, (GSourceFunc) _cairo_dock_show_xwindow_for_drop, GINT_TO_POINTER (pPointedIcon->Xid));
	}
	
	//g_print ("%x/%x , %x, %x\n", pDock, s_pLastPointedDock, pLastPointedIcon, pLastPointedIcon?pLastPointedIcon->pSubDock:NULL);
	if ((pDock == s_pLastPointedDock || s_pLastPointedDock == NULL) && pLastPointedIcon != NULL && pLastPointedIcon->pSubDock != NULL)  // on a quitte une icone ayant un sous-dock.
	{
		CairoDock *pSubDock = pLastPointedIcon->pSubDock;
		if (GTK_WIDGET_VISIBLE (pSubDock->container.pWidget))
		{
			//g_print ("on cache %s en changeant d'icone\n", pLastPointedIcon->cName);
			if (pSubDock->iSidLeaveDemand == 0)
			{
				//g_print ("  on retarde le cachage du dock de %dms\n", MAX (myAccessibility.iLeaveSubDockDelay, 330));
				pSubDock->iSidLeaveDemand = g_timeout_add (MAX (myAccessibility.iLeaveSubDockDelay, 330), (GSourceFunc) cairo_dock_emit_leave_signal, (gpointer) pSubDock);
			}
		}
		//else
		//	cd_debug ("pas encore visible !\n");
	}
	if (pPointedIcon != NULL && pPointedIcon->pSubDock != NULL && pPointedIcon->pSubDock != s_pLastPointedDock && (! myAccessibility.bShowSubDockOnClick || CAIRO_DOCK_IS_APPLI (pPointedIcon) || pDock->bIsDragging))  // on entre sur une icone ayant un sous-dock.
	{
		//cd_debug ("il faut montrer un sous-dock");
		if (pPointedIcon->pSubDock->iSidLeaveDemand != 0)
		{
			g_source_remove (pPointedIcon->pSubDock->iSidLeaveDemand);
			pPointedIcon->pSubDock->iSidLeaveDemand = 0;
		}
		if (myAccessibility.iShowSubDockDelay > 0)
		{
			//pDock->container.iMouseX = iX;
			if (s_iSidShowSubDockDemand != 0)
				g_source_remove (s_iSidShowSubDockDemand);
			s_iSidShowSubDockDemand = g_timeout_add (myAccessibility.iShowSubDockDelay, (GSourceFunc) _cairo_dock_show_sub_dock_delayed, pDock);
			s_pDockShowingSubDock = pDock;
			//cd_debug ("s_iSidShowSubDockDemand <- %d\n", s_iSidShowSubDockDemand);
		}
		else
			cairo_dock_show_subdock (pPointedIcon, pDock, FALSE);
		s_pLastPointedDock = pDock;
	}

	if (s_pLastPointedDock == NULL)
	{
		//g_print ("pLastPointedDock n'est plus null\n");
		s_pLastPointedDock = pDock;
	}
	if (pPointedIcon != NULL && pDock->pRenderer->render_opengl != NULL && ! CAIRO_DOCK_IS_SEPARATOR (pPointedIcon) && pPointedIcon->iAnimationState <= CAIRO_DOCK_STATE_MOUSE_HOVERED)
	{
		gboolean bStartAnimation = FALSE;
		cairo_dock_notify_on_container (CAIRO_CONTAINER (pDock), CAIRO_DOCK_ENTER_ICON, pPointedIcon, pDock, &bStartAnimation);
		
		if (bStartAnimation)
		{
			pPointedIcon->iAnimationState = CAIRO_DOCK_STATE_MOUSE_HOVERED;
			cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
		}
	}
}


void cairo_dock_stop_icon_glide (CairoDock *pDock)
{
	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		icon->fGlideOffset = 0;
		icon->iGlideDirection = 0;
	}
}
static void _cairo_dock_make_icon_glide (Icon *pPointedIcon, Icon *pMovingicon, CairoDock *pDock)
{
	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		if (icon == pMovingicon)
			continue;
		//if (pDock->container.iMouseX > s_pMovingicon->fDrawXAtRest + s_pMovingicon->fWidth * s_pMovingicon->fScale /2)  // on a deplace l'icone a droite.  // fDrawXAtRest
		if (pMovingicon->fXAtRest < pPointedIcon->fXAtRest)  // on a deplace l'icone a droite.
		{
			//g_print ("%s : %.2f / %.2f ; %.2f / %d (%.2f)\n", icon->cName, icon->fXAtRest, s_pMovingicon->fXAtRest, icon->fDrawX, pDock->container.iMouseX, icon->fGlideOffset);
			if (icon->fXAtRest > pMovingicon->fXAtRest && icon->fDrawX < pDock->container.iMouseX + 5 && icon->fGlideOffset == 0)  // icone entre l'icone deplacee et le curseur.
			{
				//g_print ("  %s glisse vers la gauche\n", icon->cName);
				icon->iGlideDirection = -1;
			}
			else if (icon->fXAtRest > pMovingicon->fXAtRest && icon->fDrawX > pDock->container.iMouseX && icon->fGlideOffset != 0)
			{
				//g_print ("  %s glisse vers la droite\n", icon->cName);
				icon->iGlideDirection = 1;
			}
			else if (icon->fXAtRest < pMovingicon->fXAtRest && icon->fGlideOffset > 0)
			{
				//g_print ("  %s glisse en sens inverse vers la gauche\n", icon->cName);
				icon->iGlideDirection = -1;
			}
		}
		else
		{
			//g_print ("deplacement de %s vers la gauche (%.2f / %d)\n", icon->cName, icon->fDrawX + icon->fWidth * (1+myIcons.fAmplitude) + myIcons.iIconGap, pDock->container.iMouseX);
			if (icon->fXAtRest < pMovingicon->fXAtRest && icon->fDrawX + icon->fWidth * (1+myIcons.fAmplitude) + myIcons.iIconGap >= pDock->container.iMouseX && icon->fGlideOffset == 0)  // icone entre l'icone deplacee et le curseur.
			{
				//g_print ("  %s glisse vers la droite\n", icon->cName);
				icon->iGlideDirection = 1;
			}
			else if (icon->fXAtRest < pMovingicon->fXAtRest && icon->fDrawX + icon->fWidth * (1+myIcons.fAmplitude) + myIcons.iIconGap <= pDock->container.iMouseX && icon->fGlideOffset != 0)
			{
				//g_print ("  %s glisse vers la gauche\n", icon->cName);
				icon->iGlideDirection = -1;
			}
			else if (icon->fXAtRest > pMovingicon->fXAtRest && icon->fGlideOffset < 0)
			{
				//g_print ("  %s glisse en sens inverse vers la droite\n", icon->cName);
				icon->iGlideDirection = 1;
			}
		}
	}
}
gboolean cairo_dock_on_motion_notify (GtkWidget* pWidget,
	GdkEventMotion* pMotion,
	CairoDock *pDock)
{
	static double fLastTime = 0;
	if (s_bFrozenDock && pMotion != NULL && pMotion->time != 0)
		return FALSE;
	Icon *pPointedIcon=NULL, *pLastPointedIcon = cairo_dock_get_pointed_icon (pDock->icons);
	int iLastMouseX = pDock->container.iMouseX;
	//g_print ("%s (%.2f;%.2f)\n", __func__, pMotion->x, pMotion->y);
	
	if (pMotion != NULL)
	{
		//g_print ("%s (%d,%d) (%d, %.2fms, bAtBottom:%d; bIsShrinkingDown:%d)\n", __func__, (int) pMotion->x, (int) pMotion->y, pMotion->is_hint, pMotion->time - fLastTime, pDock->bAtBottom, pDock->bIsShrinkingDown);
		//\_______________ On deplace le dock si ALT est enfoncee.
		if ((pMotion->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) && (pMotion->state & GDK_BUTTON1_MASK))
		{
			if (pDock->container.bIsHorizontal)
			{
				pDock->container.iWindowPositionX = pMotion->x_root - pDock->container.iMouseX;
				pDock->container.iWindowPositionY = pMotion->y_root - pDock->container.iMouseY;
				gtk_window_move (GTK_WINDOW (pWidget),
					pDock->container.iWindowPositionX,
					pDock->container.iWindowPositionY);
			}
			else
			{
				pDock->container.iWindowPositionX = pMotion->y_root - pDock->container.iMouseX;
				pDock->container.iWindowPositionY = pMotion->x_root - pDock->container.iMouseY;
				gtk_window_move (GTK_WINDOW (pWidget),
					pDock->container.iWindowPositionY,
					pDock->container.iWindowPositionX);
			}
			gdk_device_get_state (pMotion->device, pMotion->window, NULL, NULL);
			return FALSE;
		}
		
		//\_______________ On recupere la position de la souris.
		if (pDock->container.bIsHorizontal)
		{
			pDock->container.iMouseX = (int) pMotion->x;
			pDock->container.iMouseY = (int) pMotion->y;
		}
		else
		{
			pDock->container.iMouseX = (int) pMotion->y;
			pDock->container.iMouseY = (int) pMotion->x;
		}
		
		//\_______________ On tire l'icone volante.
		if (s_pFlyingContainer != NULL && ! pDock->container.bInside)
		{
			cairo_dock_drag_flying_container (s_pFlyingContainer, pDock);
		}
		
		//\_______________ On elague le flux des MotionNotify, sinon X en envoie autant que le permet le CPU !
		if (pMotion->time != 0 && pMotion->time - fLastTime < mySystem.fRefreshInterval && s_pIconClicked == NULL)
		{
			gdk_device_get_state (pMotion->device, pMotion->window, NULL, NULL);
			return FALSE;
		}
		
		//\_______________ On recalcule toutes les icones et on redessine.
		pPointedIcon = cairo_dock_calculate_dock_icons (pDock);
		if (myIcons.fAmplitude != 0)
			gtk_widget_queue_draw (pWidget);
		fLastTime = pMotion->time;
		
		//\_______________ On tire l'icone cliquee.
		if (s_pIconClicked != NULL && s_pIconClicked->iAnimationState != CAIRO_DOCK_STATE_REMOVE_INSERT && ! g_bLocked && ! myAccessibility.bLockIcons && ! myAccessibility.bLockAll && (fabs (pMotion->x - s_iClickX) > CD_CLICK_ZONE || fabs (pMotion->y - s_iClickY) > CD_CLICK_ZONE))
		{
			s_bIconDragged = TRUE;
			s_pIconClicked->iAnimationState = CAIRO_DOCK_STATE_FOLLOW_MOUSE;
			//pDock->fAvoidingMouseMargin = .5;
			pDock->iAvoidingMouseIconType = s_pIconClicked->iType;  // on pourrait le faire lors du clic aussi.
			s_pIconClicked->fScale = 1 + myIcons.fAmplitude;
			s_pIconClicked->fDrawX = pDock->container.iMouseX  - s_pIconClicked->fWidth * s_pIconClicked->fScale / 2;
			s_pIconClicked->fDrawY = pDock->container.iMouseY - s_pIconClicked->fHeight * s_pIconClicked->fScale / 2 ;
			s_pIconClicked->fAlpha = 0.75;
			if (myIcons.fAmplitude == 0)
				gtk_widget_queue_draw (pWidget);
		}

		//gdk_event_request_motions (pMotion);  // ce sera pour GDK 2.12.
		gdk_device_get_state (pMotion->device, pMotion->window, NULL, NULL);  // pour recevoir d'autres MotionNotify.
	}
	else  // cas d'un drag and drop.
	{
		//g_print ("motion on drag\n");
		//\_______________ On recupere la position de la souris.
		if (pDock->container.bIsHorizontal)
 			gdk_window_get_pointer (pWidget->window, &pDock->container.iMouseX, &pDock->container.iMouseY, NULL);
		else
			gdk_window_get_pointer (pWidget->window, &pDock->container.iMouseY, &pDock->container.iMouseX, NULL);
		
		//\_______________ On recalcule toutes les icones et on redessine.
		pPointedIcon = cairo_dock_calculate_dock_icons (pDock);
		gtk_widget_queue_draw (pWidget);
		
		pDock->fAvoidingMouseMargin = .25;  // on peut dropper entre 2 icones ...
		pDock->iAvoidingMouseIconType = CAIRO_DOCK_LAUNCHER;  // ... seulement entre 2 lanceurs.
	}
	
	//\_______________ On asservit les decorations au curseur.
	if (pDock->container.bInside)
	{
		if (myBackground.bDecorationsFollowMouse)
		{
			pDock->fDecorationsOffsetX = pDock->container.iMouseX - pDock->container.iWidth / 2;
			//g_print ("fDecorationsOffsetX <- %.2f\n", pDock->fDecorationsOffsetX);
		}
		else
		{
			if (pDock->container.iMouseX > iLastMouseX)
			{
				pDock->fDecorationsOffsetX += 10;
				if (pDock->fDecorationsOffsetX > pDock->container.iWidth / 2)
				{
					if (g_pBackgroundSurfaceFull[0] != NULL)
						pDock->fDecorationsOffsetX -= pDock->container.iWidth;
					else
						pDock->fDecorationsOffsetX = pDock->container.iWidth / 2;
				}
			}
			else
			{
				pDock->fDecorationsOffsetX -= 10;
				if (pDock->fDecorationsOffsetX < - pDock->container.iWidth / 2)
				{
					if (g_pBackgroundSurfaceFull[0] != NULL)
						pDock->fDecorationsOffsetX += pDock->container.iWidth;
					else
						pDock->fDecorationsOffsetX = - pDock->container.iWidth / 2;
				}
			}
		}
	}
	
	//\_______________ On gere le changement d'icone.
	gboolean bStartAnimation = FALSE;
	if (pPointedIcon != pLastPointedIcon || s_pLastPointedDock == NULL)
	{
		cairo_dock_on_change_icon (pLastPointedIcon, pPointedIcon, pDock);
		
		if (pPointedIcon != NULL && s_pIconClicked != NULL && cairo_dock_get_icon_order (s_pIconClicked) == cairo_dock_get_icon_order (pPointedIcon) && ! g_bLocked && ! myAccessibility.bLockIcons && ! myAccessibility.bLockAll)
		{
			g_print ("on change d'icone\n");
			_cairo_dock_make_icon_glide (pPointedIcon, s_pIconClicked, pDock);
			bStartAnimation = TRUE;
		}
	}
	
	//\_______________ On notifie tout le monde.
	cairo_dock_notify_on_container (CAIRO_CONTAINER (pDock), CAIRO_DOCK_MOUSE_MOVED, pDock, &bStartAnimation);
	if (bStartAnimation)
		cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
	
	return FALSE;
}

gboolean cairo_dock_emit_signal_on_dock (CairoDock *pDock, const gchar *cSignal)
{
	static gboolean bReturn;
	g_signal_emit_by_name (pDock->container.pWidget, cSignal, NULL, &bReturn);
	return FALSE;
}
gboolean cairo_dock_emit_leave_signal (CairoDock *pDock)
{
	return cairo_dock_emit_signal_on_dock (pDock, "leave-notify-event");
}
gboolean cairo_dock_emit_enter_signal (CairoDock *pDock)
{
	return cairo_dock_emit_signal_on_dock (pDock, "enter-notify-event");
}


void cairo_dock_leave_from_main_dock (CairoDock *pDock)
{
	//g_print ("%s (bMenuVisible:%d)\n", __func__, pDock->bMenuVisible);
	pDock->iAvoidingMouseIconType = -1;
	pDock->fAvoidingMouseMargin = 0;
	pDock->container.bInside = FALSE;
	
	//\_______________ On quitte si le menu est leve, pour rester en position haute.
	if (pDock->bMenuVisible)
		return ;
	
	//\_______________ On gere le drag d'une icone hors du dock.
	if (s_pIconClicked != NULL && (CAIRO_DOCK_IS_LAUNCHER (s_pIconClicked) || CAIRO_DOCK_IS_DETACHABLE_APPLET (s_pIconClicked) || CAIRO_DOCK_IS_USER_SEPARATOR(s_pIconClicked)) && s_pFlyingContainer == NULL && ! g_bLocked && ! myAccessibility.bLockIcons && ! myAccessibility.bLockAll)
	{
		g_print ("on a sorti %s du dock (%d;%d) / %dx%d\n", s_pIconClicked->cName, pDock->container.iMouseX, pDock->container.iMouseY, pDock->container.iWidth, pDock->container.iHeight);
		
		//if (! cairo_dock_hide_child_docks (pDock))  // on quitte si on entre dans un sous-dock, pour rester en position "haute".
		//	return ;
		
		CairoDock *pOriginDock = cairo_dock_search_dock_from_name (s_pIconClicked->cParentDockName);
		g_return_if_fail (pOriginDock != NULL);
		if (pOriginDock == pDock && _mouse_is_really_outside (pDock))  // ce test est la pour parer aux WM deficients mentaux comme KWin qui nous font sortir/rentrer lors d'un clic.
		{
			g_print (" on detache l'icone\n");
			pOriginDock->bIconIsFlyingAway = TRUE;
			gchar *cParentDockName = s_pIconClicked->cParentDockName;
			s_pIconClicked->cParentDockName = NULL;
			cairo_dock_detach_icon_from_dock (s_pIconClicked, pOriginDock, TRUE);
			s_pIconClicked->cParentDockName = cParentDockName;
			cairo_dock_update_dock_size (pOriginDock);
			cairo_dock_stop_icon_glide (pOriginDock);
			
			s_pFlyingContainer = cairo_dock_create_flying_container (s_pIconClicked, pOriginDock, TRUE);
			//g_print ("- s_pIconClicked <- NULL\n");
			s_pIconClicked = NULL;
			if (pDock->iRefCount > 0 || pDock->bAutoHide)  // pour garder le dock visible.
			{
				return;
			}
		}
	}
	else if (s_pFlyingContainer != NULL && s_pFlyingContainer->pIcon != NULL && pDock->iRefCount > 0)  // on evite les bouclages.
	{
		CairoDock *pOriginDock = cairo_dock_search_dock_from_name (s_pFlyingContainer->pIcon->cParentDockName);
		if (pOriginDock == pDock)
			return;
	}
	
	//\_______________ On lance l'animation du dock.
	if (pDock->iRefCount == 0)
	{
		if (pDock->bAutoHide)
		{
			pDock->fFoldingFactor = (mySystem.bAnimateOnAutoHide ? 0.001 : 0.);
			cairo_dock_start_hiding (pDock);
		}
	}
	else
	{
		pDock->fFoldingFactor = (mySystem.bAnimateSubDock ? 0.001 : 0.);
	}
	cairo_dock_start_shrinking (pDock);  // on commence a faire diminuer la taille des icones.
}
gboolean cairo_dock_on_leave_notify (GtkWidget* pWidget, GdkEventCrossing* pEvent, CairoDock *pDock)
{
	//g_print ("%s (bInside:%d; iState:%d; iRefCount:%d)\n", __func__, pDock->container.bInside, pDock->iInputState, pDock->iRefCount);
	//\_______________ On tire le dock => on ignore le signal.
	if (pEvent != NULL && (pEvent->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) && (pEvent->state & GDK_BUTTON1_MASK))
	{
		return FALSE;
	}
	
	//\_______________ On retarde la sortie.
	if (pDock->iSidLeaveDemand == 0 && pEvent != NULL)  // pas encore de demande de sortie et sortie naturelle.
	{
		if (pDock->iRefCount == 0)  // cas du main dock : on retarde si on pointe sur un sous-dock, pour laisser le temps au signal d'entree dans le sous-dock d'etre traite
		{
			Icon *pPointedIcon = cairo_dock_get_pointed_icon (pDock->icons);
			if (pPointedIcon != NULL && pPointedIcon->pSubDock != NULL && GTK_WIDGET_VISIBLE (pPointedIcon->pSubDock->container.pWidget))
			{
				//g_print ("  on retarde la sortie du dock de %dms\n", MAX (myAccessibility.iLeaveSubDockDelay, 330));
				pDock->iSidLeaveDemand = g_timeout_add (MAX (myAccessibility.iLeaveSubDockDelay, 330), (GSourceFunc) cairo_dock_emit_leave_signal, (gpointer) pDock);
				return TRUE;
			}
		}
		else if (myAccessibility.iLeaveSubDockDelay != 0)  // cas d'un sous-dock : on retarde le cachage.
		{
			//g_print ("  on retarde la sortie du sous-dock de %dms\n", myAccessibility.iLeaveSubDockDelay);
			pDock->iSidLeaveDemand = g_timeout_add (myAccessibility.iLeaveSubDockDelay, (GSourceFunc) cairo_dock_emit_leave_signal, (gpointer) pDock);
			return TRUE;
		}
	}
	pDock->iSidLeaveDemand = 0;
	
	//\_______________ Arrive ici, on est sorti du dock.
	pDock->container.bInside = FALSE;
	/**cd_debug (">>> on attend...");
	while (gtk_events_pending ())  // on laisse le temps au signal d'entree dans le sous-dock d'etre traite.
		gtk_main_iteration ();
	cd_debug (">>> pDock->container.bInside : %d", pDock->container.bInside);
	if (pDock->container.bInside)  // on est re-rentre dedans entre-temps.
		return TRUE;*/
	
	//\_______________ On cache ses sous-docks.
	if (! cairo_dock_hide_child_docks (pDock))  // on quitte si l'un des sous-docks reste visible (on est entre dedans), pour rester en position "haute".
		return TRUE;
	
	if (pEvent != NULL)
	{
		if (pDock->container.bIsHorizontal)
		{
			pDock->container.iMouseX = pEvent->x;
			pDock->container.iMouseY = pEvent->y;
		}
		else
		{
			pDock->container.iMouseX = pEvent->y;
			pDock->container.iMouseY = pEvent->x;
		}
	}
	
	cairo_dock_leave_from_main_dock (pDock);
	
	return TRUE;
}

gboolean cairo_dock_on_enter_notify (GtkWidget* pWidget, GdkEventCrossing* pEvent, CairoDock *pDock)
{
	//g_print ("%s (bIsMainDock : %d; bInside:%d; state:%d; iMagnitudeIndex:%d)\n", __func__, pDock->bIsMainDock, pDock->container.bInside, pDock->iInputState, pDock->iMagnitudeIndex);
	s_pLastPointedDock = NULL;  // ajoute le 04/10/07 pour permettre aux sous-docks d'apparaitre si on entre en pointant tout de suite sur l'icone.
	if (! cairo_dock_entrance_is_allowed (pDock))
	{
		cd_message ("* entree non autorisee");
		return FALSE;
	}
	
	// stop les timers.
	if (pDock->iSidLeaveDemand != 0)
	{
		g_source_remove (pDock->iSidLeaveDemand);
		pDock->iSidLeaveDemand = 0;
	}
	if (s_iSidShowSubDockDemand != 0)  // gere un cas tordu mais bien reel.
	{
		g_source_remove (s_iSidShowSubDockDemand);
		s_iSidShowSubDockDemand = 0;
	}
	
	// si on etait deja dedans, ou qu'on etait cense l'etre, on relance juste le grossissement.
	if (pDock->container.bInside || pDock->bIsHiding)
	{
		pDock->container.bInside = TRUE;
		cairo_dock_start_growing (pDock);
		if (pDock->bIsHiding || cairo_dock_is_hidden (pDock))  // on (re)monte.
		{
			g_print ("  on etait deja dedans\n");
			cairo_dock_start_showing (pDock);
		}
		return FALSE;
	}
	
	pDock->container.bInside = TRUE;
	// animation d'entree.
	gboolean bStartAnimation = FALSE;
	cairo_dock_notify (CAIRO_DOCK_ENTER_DOCK, pDock, &bStartAnimation);
	if (bStartAnimation)
		cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
	
	pDock->fDecorationsOffsetX = 0;
	cairo_dock_stop_quick_hide ();
	
	if (s_pIconClicked != NULL)  // on pourrait le faire a chaque motion aussi.
	{
		pDock->iAvoidingMouseIconType = s_pIconClicked->iType;
		pDock->fAvoidingMouseMargin = .5;  /// inutile il me semble ...
	}
	
	// si on rentre avec une icone volante, on la met dedans.
	if (s_pFlyingContainer != NULL)
	{
		Icon *pFlyingIcon = s_pFlyingContainer->pIcon;
		if (pDock != pFlyingIcon->pSubDock)  // on evite les boucles.
		{
			struct timeval tv;
			int r = gettimeofday (&tv, NULL);
			double t = tv.tv_sec + tv.tv_usec * 1e-6;
			if (t - s_pFlyingContainer->fCreationTime > 1)  // on empeche le cas ou enlever l'icone fait augmenter le ratio du dock, et donc sa hauteur, et nous fait rentrer dedans des qu'on sort l'icone.
			{
				g_print ("on remet l'icone volante dans un dock (dock d'origine : %s)\n", pFlyingIcon->cParentDockName);
				cairo_dock_free_flying_container (s_pFlyingContainer);
				cairo_dock_stop_icon_animation (pFlyingIcon);
				cairo_dock_insert_icon_in_dock (pFlyingIcon, pDock, CAIRO_DOCK_UPDATE_DOCK_SIZE, CAIRO_DOCK_ANIMATE_ICON);
				cairo_dock_start_icon_animation (pFlyingIcon, pDock);
				s_pFlyingContainer = NULL;
				pDock->bIconIsFlyingAway = FALSE;
			}
		}
	}
	
	// input shape desactivee, le dock devient actif.
	//g_print ("enter (%d) \n", pDock->iInputState);
	if ((pDock->pShapeBitmap || pDock->pHiddenShapeBitmap) && pDock->iInputState != CAIRO_DOCK_INPUT_ACTIVE)
	{
		gtk_widget_input_shape_combine_mask (pDock->container.pWidget,
			NULL,
			0,
			0);
	}
	pDock->iInputState = CAIRO_DOCK_INPUT_ACTIVE;
	
	// on repasse au premier plan.
	if (myAccessibility.bPopUp && pDock->iRefCount == 0)
	{
		cairo_dock_pop_up (pDock);
		//If the dock window is entered, and there is a pending drop below event then it should be cancelled
		if (pDock->iSidPopDown != 0)
		{
			g_source_remove(pDock->iSidPopDown);
			pDock->iSidPopDown = 0;
		}
	}
	
	// si on etait en auto-hide, on commence a monter.
	if (pDock->fHideOffset != 0 && pDock->iRefCount == 0)
	{
		//g_print ("  on commence a monter\n");
		cairo_dock_start_showing (pDock);
	}
	
	// cas special.
	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);
	if (icon != NULL)
	{
		//g_print (">>> we've just entered the dock, pointed icon becomes NULL\n");
		//if (s_pIconClicked != NULL)
		//	g_print (">>> on est rentre par un clic ! (KDE:%d)\n", g_iDesktopEnv == CAIRO_DOCK_KDE);
		if (_mouse_is_really_outside (pDock))  // ce test est la pour parer aux WM deficients mentaux comme KWin qui nous font sortir/rentrer lors d'un clic.
			icon->bPointed = FALSE;  // sinon on ne detecte pas l'arrive sur l'icone, c'est genant si elle a un sous-dock.
		//else
		//	g_print (">>> we already are inside the dock, why does this stupid WM make us enter one more time ???\n");
	}
	
	// on lance le grossissement.
	cairo_dock_start_growing (pDock);
	
	return TRUE;
}

/// This function checks for the mouse cursor's position. If the mouse
/// cursor touches the edge of the screen upon which the dock is resting,
/// then the dock will pop up over other windows...
gboolean cairo_dock_poll_screen_edge (CairoDock *pDock)  // thanks to Smidgey for the pop-up patch !
{
	static int iPrevPointerX = -1, iPrevPointerY = -1;
	gint iMousePosX, iMousePosY;
	
	if (pDock->iSidPopUp == 0 && !pDock->bPopped)
	{
		gdk_display_get_pointer(gdk_display_get_default(), NULL, &iMousePosX, &iMousePosY, NULL);
		if (iPrevPointerX == iMousePosX && iPrevPointerY == iMousePosY)
			return myAccessibility.bPopUp;
		
		iPrevPointerX = iMousePosX;
		iPrevPointerY = iMousePosY;
		
		CairoDockPositionType iScreenBorder1 = CAIRO_DOCK_INSIDE_SCREEN, iScreenBorder2 = CAIRO_DOCK_INSIDE_SCREEN;
		if (iMousePosY == 0)
		{
			iScreenBorder1 = CAIRO_DOCK_TOP;
		}
		else if (iMousePosY + 1 == g_iXScreenHeight[CAIRO_DOCK_HORIZONTAL])
		{
			iScreenBorder1 = CAIRO_DOCK_BOTTOM;
		}
		if (iMousePosX == 0)
		{
			iScreenBorder2 = CAIRO_DOCK_LEFT;
		}
		else if (iMousePosX + 1 == g_iXScreenWidth[CAIRO_DOCK_HORIZONTAL])
		{
			iScreenBorder2 = CAIRO_DOCK_RIGHT;
		}
		if (iScreenBorder1 == CAIRO_DOCK_INSIDE_SCREEN && iScreenBorder2 == CAIRO_DOCK_INSIDE_SCREEN)
			return myAccessibility.bPopUp;
		if ((iScreenBorder1 != CAIRO_DOCK_INSIDE_SCREEN && iScreenBorder2 != CAIRO_DOCK_INSIDE_SCREEN) || myAccessibility.bPopUpOnScreenBorder)
		{
			if (iScreenBorder1 != CAIRO_DOCK_INSIDE_SCREEN)
				cairo_dock_pop_up_root_docks_on_screen_edge (iScreenBorder1);
			if (iScreenBorder2 != CAIRO_DOCK_INSIDE_SCREEN)
				cairo_dock_pop_up_root_docks_on_screen_edge (iScreenBorder2);
		}
	}
	
	return myAccessibility.bPopUp;
}

gboolean cairo_dock_on_key_release (GtkWidget *pWidget,
	GdkEventKey *pKey,
	CairoDock *pDock)
{
	g_print ("on a appuye sur une touche (%d)\n", pKey->keyval);
	if (pKey->type == GDK_KEY_PRESS)
	{
		cairo_dock_notify (CAIRO_DOCK_KEY_PRESSED, pDock, pKey->keyval, pKey->state, pKey->string);
	}
	else if (pKey->type == GDK_KEY_RELEASE)
	{
		//g_print ("release : pKey->keyval = %d\n", pKey->keyval);
		if ((pKey->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) && pKey->keyval == 0)  // On relache la touche ALT, typiquement apres avoir fait un ALT + clique gauche + deplacement.
		{
			if (pDock->iRefCount == 0)
				cairo_dock_write_root_dock_gaps (pDock);
		}
	}
	return TRUE;
}

gchar *cairo_dock_launch_command_sync (const gchar *cCommand)
{
	gchar *standard_output=NULL, *standard_error=NULL;
	gint exit_status=0;
	GError *erreur = NULL;
	gboolean r = g_spawn_command_line_sync (cCommand,
		&standard_output,
		&standard_error,
		&exit_status,
		&erreur);
	if (erreur != NULL)
	{
		cd_warning (erreur->message);
		g_error_free (erreur);
		g_free (standard_error);
		return NULL;
	}
	if (standard_error != NULL && *standard_error != '\0')
	{
		cd_warning (standard_error);
	}
	g_free (standard_error);
	if (standard_output != NULL && *standard_output == '\0')
	{
		g_free (standard_output);
		return NULL;
	}
	if (standard_output[strlen (standard_output) - 1] == '\n')
		standard_output[strlen (standard_output) - 1] ='\0';
	return standard_output;
}

static gpointer _cairo_dock_launch_threaded (gchar *cCommand)
{
	int r;
	r = system (cCommand);
	g_free (cCommand);
	return NULL;
}

gboolean cairo_dock_launch_command_printf (const gchar *cCommandFormat, gchar *cWorkingDirectory, ...)
{
	va_list args;
	va_start (args, cWorkingDirectory);
	gchar *cCommand = g_strdup_vprintf (cCommandFormat, args);
	va_end (args);
	
	gboolean r = cairo_dock_launch_command_full (cCommand, cWorkingDirectory);
	g_free (cCommand);
	
	return r;
}

gboolean cairo_dock_launch_command_full (const gchar *cCommand, gchar *cWorkingDirectory)
{
	g_return_val_if_fail (cCommand != NULL, FALSE);
	cd_debug ("%s (%s , %s)", __func__, cCommand, cWorkingDirectory);
	
	gchar *cBGCommand = NULL;
	if (cCommand[strlen (cCommand)-1] != '&')
		cBGCommand = g_strconcat (cCommand, " &", NULL);
	
	gchar *cCommandFull = NULL;
	if (cWorkingDirectory != NULL)
	{
		cCommandFull = g_strdup_printf ("cd \"%s\" && %s", cWorkingDirectory, cBGCommand ? cBGCommand : cCommand);
		g_free (cBGCommand);
		cBGCommand = NULL;
	}
	else if (cBGCommand != NULL)
	{
		cCommandFull = cBGCommand;
		cBGCommand = NULL;
	}
	
	if (cCommandFull == NULL)
		cCommandFull = g_strdup (cCommand);
	
	GError *erreur = NULL;
	GThread* pThread = g_thread_create ((GThreadFunc) _cairo_dock_launch_threaded, cCommandFull, FALSE, &erreur);
	if (erreur != NULL)
	{
		cd_warning ("couldn't launch this command (%s : %s)", cCommandFull, erreur->message);
		g_error_free (erreur);
		g_free (cCommandFull);
		return FALSE;
	}
	return TRUE;
}

static int _compare_zorder (Icon *icon1, Icon *icon2)  // classe par z-order decroissant.
{
	if (icon1->iStackOrder < icon2->iStackOrder)
		return -1;
	else if (icon1->iStackOrder > icon2->iStackOrder)
		return 1;
	else
		return 0;
}
static void _cairo_dock_hide_show_in_class_subdock (Icon *icon)
{
	Icon *pIcon;
	GList *ic;
	for (ic = icon->pSubDock->icons; ic != NULL; ic = ic->next)
	{
		pIcon = ic->data;
		if (pIcon->Xid != 0 && ! pIcon->bIsHidden)  // par defaut on cache tout.
		{
			break;
		}
	}
	
	if (ic != NULL)  // au moins une fenetre est visible, on cache tout.
	{
		for (ic = icon->pSubDock->icons; ic != NULL; ic = ic->next)
		{
			pIcon = ic->data;
			if (pIcon->Xid != 0 && ! pIcon->bIsHidden)
			{
				cairo_dock_minimize_xwindow (pIcon->Xid);
			}
		}
	}
	else  // on montre tout, dans l'ordre du z-order.
	{
		GList *pZOrderList = NULL;
		for (ic = icon->pSubDock->icons; ic != NULL; ic = ic->next)
		{
			pIcon = ic->data;
			if (pIcon->Xid != 0)
				pZOrderList = g_list_insert_sorted (pZOrderList, pIcon, (GCompareFunc) _compare_zorder);
		}
		for (ic = pZOrderList; ic != NULL; ic = ic->next)
		{
			pIcon = ic->data;
			cairo_dock_show_xwindow (pIcon->Xid);
		}
		g_list_free (pZOrderList);
	}
}

static void _cairo_dock_show_prev_next_in_class_subdock (Icon *icon, gboolean bNext)
{
	Icon *pActiveIcon = cairo_dock_get_current_active_icon ();
	Icon *pNextIcon;
	if (pActiveIcon != NULL)
	{
		if (bNext)
		{
			pNextIcon = cairo_dock_get_next_icon (icon->pSubDock->icons, pActiveIcon);
			if (pNextIcon == NULL)  // pas trouvee ou derniere de la liste.
				pNextIcon = cairo_dock_get_first_icon (icon->pSubDock->icons);
		}
		else
		{
			pNextIcon = cairo_dock_get_previous_icon (icon->pSubDock->icons, pActiveIcon);
			if (pNextIcon == NULL)  // pas trouvee ou premiere de la liste.
				pNextIcon = cairo_dock_get_last_icon (icon->pSubDock->icons);
		}
	}
	else
	{
		pNextIcon = cairo_dock_get_first_icon (icon->pSubDock->icons);
	}
	if (pNextIcon != NULL)
		cairo_dock_show_xwindow (pNextIcon->Xid);
}

static void _cairo_dock_close_all_in_class_subdock (Icon *icon)
{
	Icon *pIcon;
	GList *ic;
	for (ic = icon->pSubDock->icons; ic != NULL; ic = ic->next)
	{
		pIcon = ic->data;
		if (pIcon->Xid != 0)
		{
			cairo_dock_close_xwindow (pIcon->Xid);
		}
	}
}


gboolean cairo_dock_notification_click_icon (gpointer pUserData, Icon *icon, CairoDock *pDock, guint iButtonState)
{
	//g_print ("+ %s (%s)\n", __func__, icon ? icon->cName : "no icon");
	if (icon == NULL)
		return CAIRO_DOCK_LET_PASS_NOTIFICATION;
	if (icon->pSubDock != NULL && (myAccessibility.bShowSubDockOnClick || !GTK_WIDGET_VISIBLE (pDock->container.pWidget)) && ! (iButtonState & GDK_SHIFT_MASK))  // icone de sous-dock a montrer au clic.
	{
		cairo_dock_show_subdock (icon, pDock, FALSE);
		return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
	}
	else if (CAIRO_DOCK_IS_URI_LAUNCHER (icon))  // URI : on lance ou on monte.
	{
		cd_debug (" uri launcher");
		gboolean bIsMounted = FALSE;
		if (icon->iVolumeID > 0)
		{
			gchar *cActivationURI = cairo_dock_fm_is_mounted (icon->cBaseURI, &bIsMounted);
			g_free (cActivationURI);
		}
		if (icon->iVolumeID > 0 && ! bIsMounted)
		{
			int answer = cairo_dock_ask_question_and_wait (_("Do you want to mount this point ?"), icon, CAIRO_CONTAINER (pDock));
			if (answer != GTK_RESPONSE_YES)
			{
				return CAIRO_DOCK_LET_PASS_NOTIFICATION;
			}
			cairo_dock_fm_mount (icon, CAIRO_CONTAINER (pDock));
		}
		else
			cairo_dock_fm_launch_uri (icon->cCommand);
		return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
	}
	else if (CAIRO_DOCK_IS_APPLI (icon) && ! ((iButtonState & GDK_SHIFT_MASK) && CAIRO_DOCK_IS_LAUNCHER (icon)) && ! CAIRO_DOCK_IS_APPLET (icon))  // une icone d'appli ou d'inhibiteur (hors applet) mais sans le shift+clic : on cache ou on montre.
	{
		cd_debug (" appli");
		if (cairo_dock_get_current_active_window () == icon->Xid && myTaskBar.bMinimizeOnClick)  // ne marche que si le dock est une fenêtre de type 'dock', sinon il prend le focus.
			cairo_dock_minimize_xwindow (icon->Xid);
		else
			cairo_dock_show_xwindow (icon->Xid);
		return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
	}
	else if (CAIRO_DOCK_IS_LAUNCHER (icon))
	{
		//g_print ("+ launcher\n");
		if (CAIRO_DOCK_IS_MULTI_APPLI (icon) && ! (iButtonState & GDK_SHIFT_MASK))  // un lanceur ayant un sous-dock de classe ou une icone de paille : on cache ou on montre.
		{
			if (! myAccessibility.bShowSubDockOnClick)
			{
				_cairo_dock_hide_show_in_class_subdock (icon);
			}
			return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
		}
		else if (icon->cCommand != NULL && strcmp (icon->cCommand, "none") != 0)  // finalement, on lance la commande.
		{
			if (pDock->iRefCount != 0)
			{
				Icon *pMainIcon = cairo_dock_search_icon_pointing_on_dock (pDock, NULL);
				if (CAIRO_DOCK_IS_APPLET (pMainIcon))
					return CAIRO_DOCK_LET_PASS_NOTIFICATION;
			}
			
			gboolean bSuccess = FALSE;
			if (*icon->cCommand == '<')
			{
				bSuccess = cairo_dock_simulate_key_sequence (icon->cCommand);
				if (!bSuccess)
					bSuccess = cairo_dock_launch_command_full (icon->cCommand, icon->cWorkingDirectory);
			}
			else
			{
				bSuccess = cairo_dock_launch_command_full (icon->cCommand, icon->cWorkingDirectory);
				if (! bSuccess)
					bSuccess = cairo_dock_simulate_key_sequence (icon->cCommand);
			}
			if (! bSuccess)
			{
				cairo_dock_request_icon_animation (icon, pDock, "blink", 1);  // 1 clignotement si echec
			}
			return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
		}
	}
	else
		cd_debug ("no action here");
	return CAIRO_DOCK_LET_PASS_NOTIFICATION;
}


gboolean cairo_dock_notification_middle_click_icon (gpointer pUserData, Icon *icon, CairoDock *pDock)
{
	if (CAIRO_DOCK_IS_APPLI (icon) && myTaskBar.bCloseAppliOnMiddleClick && ! CAIRO_DOCK_IS_APPLET (icon))
	{
		cairo_dock_close_xwindow (icon->Xid);
		return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
	}
	if (CAIRO_DOCK_IS_URI_LAUNCHER (icon) && icon->pSubDock != NULL)  // icone de repertoire.
	{
		cairo_dock_fm_launch_uri (icon->cCommand);
		return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
	}
	if (CAIRO_DOCK_IS_MULTI_APPLI (icon))
	{
		// On ferme tout.
		_cairo_dock_close_all_in_class_subdock (icon);
		
		return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
	}
	return CAIRO_DOCK_LET_PASS_NOTIFICATION;
}

gboolean cairo_dock_on_button_press (GtkWidget* pWidget, GdkEventButton* pButton, CairoDock *pDock)
{
	//g_print ("+ %s (%d/%d)\n", __func__, pButton->type, pButton->button);
	if (pDock->container.bIsHorizontal)  // utile ?
	{
		pDock->container.iMouseX = (int) pButton->x;
		pDock->container.iMouseY = (int) pButton->y;
	}
	else
	{
		pDock->container.iMouseX = (int) pButton->y;
		pDock->container.iMouseY = (int) pButton->x;
	}

	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);
	if (pButton->button == 1)  // clic gauche.
	{
		//g_print ("+ left click\n");
		switch (pButton->type)
		{
			case GDK_BUTTON_RELEASE :
				//g_print ("+ GDK_BUTTON_RELEASE (%d/%d sur %s/%s)\n", pButton->state, GDK_CONTROL_MASK | GDK_MOD1_MASK, icon ? icon->cName : "personne", icon ? icon->cCommand : "");  // 272 = 100010000
				if ( ! (pButton->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
				{
					if (s_pIconClicked != NULL)
					{
						cd_debug ("activate %s (%s)", s_pIconClicked->cName, icon ? icon->cName : "none");
						s_pIconClicked->iAnimationState = CAIRO_DOCK_STATE_REST;  // stoppe les animations de suivi du curseur.
						pDock->iAvoidingMouseIconType = -1;
						cairo_dock_stop_icon_glide (pDock);
					}
					if (icon != NULL && ! CAIRO_DOCK_IS_SEPARATOR (icon) && icon == s_pIconClicked)
					{
						s_pIconClicked = NULL;  // il faut le faire ici au cas ou le clic induirait un dialogue bloquant qui nous ferait sortir du dock par exemple.
						//g_print ("+ click on '%s' (%s)\n", icon->cName, icon->cCommand);
						if (! s_bIconDragged)  // on ignore le drag'n'drop sur elle-meme.
						{
							cairo_dock_notify (CAIRO_DOCK_CLICK_ICON, icon, pDock, pButton->state);
							if (myAccessibility.cRaiseDockShortcut != NULL)
								s_bHideAfterShortcut = TRUE;
							
							cairo_dock_start_icon_animation (icon, pDock);
						}
					}
					else if (s_pIconClicked != NULL && icon != NULL && icon != s_pIconClicked && ! g_bLocked && ! myAccessibility.bLockIcons && ! myAccessibility.bLockAll)  //  && icon->iType == s_pIconClicked->iType
					{
						//g_print ("deplacement de %s\n", s_pIconClicked->cName);
						CairoDock *pOriginDock = CAIRO_DOCK (cairo_dock_search_container_from_icon (s_pIconClicked));
						if (pOriginDock != NULL && pDock != pOriginDock)
						{
							cairo_dock_detach_icon_from_dock (s_pIconClicked, pOriginDock, TRUE);
							cairo_dock_update_dock_size (pOriginDock);
							
							cairo_dock_update_icon_s_container_name (s_pIconClicked, icon->cParentDockName);
							if (pOriginDock->iRefCount > 0 && ! myViews.bSameHorizontality)
							{
								cairo_t* pSourceContext = cairo_dock_create_context_from_window (CAIRO_CONTAINER (pDock));
								cairo_dock_fill_one_text_buffer (s_pIconClicked, pSourceContext, &myLabels.iconTextDescription);
								cairo_destroy (pSourceContext);
							}

							cairo_dock_insert_icon_in_dock (s_pIconClicked, pDock, ! CAIRO_DOCK_UPDATE_DOCK_SIZE, CAIRO_DOCK_ANIMATE_ICON);
							cairo_dock_start_icon_animation (s_pIconClicked, pDock);
						}

						Icon *prev_icon, *next_icon;
						if (icon->fXAtRest > s_pIconClicked->fXAtRest)
						{
							prev_icon = icon;
							next_icon = cairo_dock_get_next_icon (pDock->icons, icon);
						}
						else
						{
							prev_icon = cairo_dock_get_previous_icon (pDock->icons, icon);
							next_icon = icon;
						}
						if ((prev_icon == NULL || cairo_dock_get_icon_order (prev_icon) != cairo_dock_get_icon_order (s_pIconClicked)) && (next_icon == NULL || cairo_dock_get_icon_order (next_icon) != cairo_dock_get_icon_order (s_pIconClicked)))
						{
							s_pIconClicked = NULL;
							return FALSE;
						}
						//g_print ("deplacement de %s\n", s_pIconClicked->cName);
						if (prev_icon != NULL && cairo_dock_get_icon_order (prev_icon) != cairo_dock_get_icon_order (s_pIconClicked))
							prev_icon = NULL;
						cairo_dock_move_icon_after_icon (pDock, s_pIconClicked, prev_icon);

						pDock->pRenderer->calculate_icons (pDock);

						if (! CAIRO_DOCK_IS_SEPARATOR (s_pIconClicked))
						{
							cairo_dock_request_icon_animation (s_pIconClicked, pDock, "bounce", 2);
						}
						if (pDock->container.iSidGLAnimation == 0 || ! CAIRO_CONTAINER_IS_OPENGL (CAIRO_CONTAINER (pDock)))
							gtk_widget_queue_draw (pDock->container.pWidget);
					}
					
					if (s_pFlyingContainer != NULL)
					{
						g_print ("on relache l'icone volante\n");
						if (pDock->container.bInside)
						{
							//g_print ("  on la remet dans son dock d'origine\n");
							Icon *pFlyingIcon = s_pFlyingContainer->pIcon;
							cairo_dock_free_flying_container (s_pFlyingContainer);
							cairo_dock_stop_marking_icon_as_following_mouse (pFlyingIcon);
							cairo_dock_stop_icon_animation (pFlyingIcon);
							cairo_dock_insert_icon_in_dock (pFlyingIcon, pDock, CAIRO_DOCK_UPDATE_DOCK_SIZE, CAIRO_DOCK_ANIMATE_ICON);
							cairo_dock_start_icon_animation (pFlyingIcon, pDock);
						}
						else
						{
							cairo_dock_terminate_flying_container (s_pFlyingContainer);  // supprime ou detache l'icone, l'animation se terminera toute seule.
						}
						s_pFlyingContainer = NULL;
						pDock->bIconIsFlyingAway = FALSE;
						cairo_dock_stop_icon_glide (pDock);
					}
				}
				else
				{
					if (pDock->iRefCount == 0)
						cairo_dock_write_root_dock_gaps (pDock);
				}
				//g_print ("- apres clic : s_pIconClicked <- NULL\n");
				s_pIconClicked = NULL;
				s_bIconDragged = FALSE;
			break ;
			
			case GDK_BUTTON_PRESS :
				if ( ! (pButton->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
				{
					//g_print ("+ clic sur %s (%.2f)!\n", icon ? icon->cName : "rien", icon ? icon->fPersonnalScale : 0.);
					s_iClickX = pButton->x;
					s_iClickY = pButton->y;
					if (icon && icon->fPersonnalScale <= 0)
					{
						s_pIconClicked = icon;  // on ne definit pas l'animation FOLLOW_MOUSE ici , on le fera apres le 1er mouvement, pour eviter que l'icone soit dessinee comme tel quand on clique dessus alors que le dock est en train de jouer une animation (ca provoque un flash desagreable).
						cd_debug ("clicked on %s", icon->cName);
					}
					else
						s_pIconClicked = NULL;
				}
			break ;

			case GDK_2BUTTON_PRESS :
				{
					if (icon && icon->fPersonnalScale <= 0)
						cairo_dock_notify (CAIRO_DOCK_DOUBLE_CLICK_ICON, icon, pDock);
				}
			break ;

			default :
			break ;
		}
	}
	else if (pButton->button == 3 && pButton->type == GDK_BUTTON_PRESS)  // clique droit.
	{
		GtkWidget *menu = cairo_dock_build_menu (icon, CAIRO_CONTAINER (pDock));  // genere un CAIRO_DOCK_BUILD_MENU.
		
		cairo_dock_popup_menu_on_container (menu, CAIRO_CONTAINER (pDock));
	}
	else if (pButton->button == 2 && pButton->type == GDK_BUTTON_PRESS)  // clique milieu.
	{
		if (icon && icon->fPersonnalScale <= 0)
			cairo_dock_notify (CAIRO_DOCK_MIDDLE_CLICK_ICON, icon, pDock);
	}

	return FALSE;
}


gboolean cairo_dock_notification_scroll_icon (gpointer pUserData, Icon *icon, CairoDock *pDock, int iDirection)
{
	if (CAIRO_DOCK_IS_MULTI_APPLI (icon))  // on emule un alt+tab sur la liste des applis du sous-dock.
	{
		_cairo_dock_show_prev_next_in_class_subdock (icon, iDirection == GDK_SCROLL_DOWN);
	}
	else if (CAIRO_DOCK_IS_APPLI (icon) && icon->cClass != NULL)
	{
		Icon *pNextIcon = cairo_dock_get_prev_next_classmate_icon (icon, iDirection == GDK_SCROLL_DOWN);
		if (pNextIcon != NULL)
			cairo_dock_show_xwindow (pNextIcon->Xid);
	}
	return CAIRO_DOCK_LET_PASS_NOTIFICATION;
}
gboolean cairo_dock_on_scroll (GtkWidget* pWidget, GdkEventScroll* pScroll, CairoDock *pDock)
{
	if (pScroll->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))
	{
		if (myAccessibility.bLockIcons || myAccessibility.bLockAll)
			return FALSE;
		
		int iScrollAmount = 0;
		Icon *pLastPointedIcon = cairo_dock_get_pointed_icon (pDock->icons);
		Icon *pNeighborIcon;
		if (pScroll->direction == GDK_SCROLL_UP)
		{
			pNeighborIcon = cairo_dock_get_previous_icon (pDock->icons, pLastPointedIcon);
			if (pNeighborIcon == NULL)
				pNeighborIcon = cairo_dock_get_last_icon (pDock->icons);
			iScrollAmount = (pNeighborIcon->fWidth + (pLastPointedIcon != NULL ? pLastPointedIcon->fWidth : 0)) / 2;
		}
		else if (pScroll->direction == GDK_SCROLL_DOWN)
		{
			pNeighborIcon = cairo_dock_get_next_icon (pDock->icons, pLastPointedIcon);
			if (pNeighborIcon == NULL)
				pNeighborIcon = cairo_dock_get_first_icon (pDock->icons);
			iScrollAmount = - (pNeighborIcon->fWidth + (pLastPointedIcon != NULL ? pLastPointedIcon->fWidth : 0)) / 2;
		}
		
		cairo_dock_scroll_dock_icons (pDock, iScrollAmount);
		return FALSE;
	}
	if (pScroll->direction != GDK_SCROLL_UP && pScroll->direction != GDK_SCROLL_DOWN)  // on degage les scrolls horizontaux.
	{
		return FALSE;
	}
	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);
	if (icon != NULL)
	{
		cairo_dock_notify (CAIRO_DOCK_SCROLL_ICON, icon, pDock, pScroll->direction);
	}

	return FALSE;
}


gboolean cairo_dock_on_configure (GtkWidget* pWidget, GdkEventConfigure* pEvent, CairoDock *pDock)
{
	//g_print ("%s (main dock : %d) : (%d;%d) (%dx%d)\n", __func__, pDock->bIsMainDock, pEvent->x, pEvent->y, pEvent->width, pEvent->height);
	gint iNewWidth, iNewHeight, iNewX, iNewY;
	if (pDock->container.bIsHorizontal)
	{
		iNewWidth = pEvent->width;
		iNewHeight = pEvent->height;
		
		iNewX = pEvent->x;
		iNewY = pEvent->y;
	}
	else
	{
		iNewWidth = pEvent->height;
		iNewHeight = pEvent->width;
		
		iNewX = pEvent->y;
		iNewY = pEvent->x;
	}
	
	if ((iNewWidth != pDock->container.iWidth || iNewHeight != pDock->container.iHeight) && iNewWidth > 1)  // changement de taille
	{
		//g_print ("-> %dx%d\n", iNewWidth, iNewHeight);
		pDock->container.iWidth = iNewWidth;
		pDock->container.iHeight = iNewHeight;
		pDock->container.iWindowPositionX = iNewX;
		pDock->container.iWindowPositionY = iNewY;
		
		if (pDock->container.bIsHorizontal)
			gdk_window_get_pointer (pWidget->window, &pDock->container.iMouseX, &pDock->container.iMouseY, NULL);
		else
			gdk_window_get_pointer (pWidget->window, &pDock->container.iMouseY, &pDock->container.iMouseX, NULL);
		if (pDock->container.iMouseX < 0 || pDock->container.iMouseX > pDock->container.iWidth)  // utile ?
			pDock->container.iMouseX = 0;
		//g_print ("x,y : %d;%d\n", pDock->container.iMouseX, pDock->container.iMouseY);
		
		// les dimensions ont change, il faut remettre l'input shape a la bonne place.
		if (pDock->pHiddenShapeBitmap != NULL && pDock->iInputState == CAIRO_DOCK_INPUT_HIDDEN)
		{
			gtk_widget_input_shape_combine_mask (pDock->container.pWidget,
				NULL,
				0,
				0);
			gtk_widget_input_shape_combine_mask (pDock->container.pWidget,
				pDock->pHiddenShapeBitmap,
				0,
				0);
		}
		else if (pDock->pShapeBitmap != NULL && pDock->iInputState == CAIRO_DOCK_INPUT_AT_REST)
		{
			gtk_widget_input_shape_combine_mask (pDock->container.pWidget,
				NULL,
				0,
				0);
			gtk_widget_input_shape_combine_mask (pDock->container.pWidget,
				pDock->pShapeBitmap,
				0,
				0);
		}
		
		if (g_bUseOpenGL)
		{
			GdkGLContext* pGlContext = gtk_widget_get_gl_context (pWidget);
			GdkGLDrawable* pGlDrawable = gtk_widget_get_gl_drawable (pWidget);
			GLsizei w = pEvent->width;
			GLsizei h = pEvent->height;
			if (!gdk_gl_drawable_gl_begin (pGlDrawable, pGlContext))
				return FALSE;
			
			glViewport(0, 0, w, h);
			
			/*glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, w, 0, h, 0.0, 500.0);
			
			glMatrixMode (GL_MODELVIEW);
			glLoadIdentity ();
			gluLookAt (w/2, h/2, 3.,
				w/2, h/2, 0.,
				0.0f, 1.0f, 0.0f);
			glTranslatef (0.0f, 0.0f, -3.);*/
			cairo_dock_set_ortho_view (w, h);
			
			glClearAccum (0., 0., 0., 0.);
			glClear (GL_ACCUM_BUFFER_BIT);
			
			gdk_gl_drawable_gl_end (pGlDrawable);
		}
		
		#ifdef HAVE_GLITZ
		if (pDock->container.pGlitzDrawable)
		{
			glitz_drawable_update_size (pDock->container.pGlitzDrawable,
				pEvent->width,
				pEvent->height);
		}
		#endif
		
		cairo_dock_calculate_dock_icons (pDock);
		g_print ("configure size\n");
		cairo_dock_set_icons_geometry_for_window_manager (pDock);  // changement de position ou de taille du dock => on replace les icones.
		
		cairo_dock_replace_all_dialogs ();
	}
	else if (pDock->container.iWindowPositionX != iNewX || pDock->container.iWindowPositionY != iNewY)  // changement de position.
	{
		pDock->container.iWindowPositionX = iNewX;
		pDock->container.iWindowPositionY = iNewY;
		g_print ("configure x,y\n");
		cairo_dock_set_icons_geometry_for_window_manager (pDock);  // changement de position ou de taille du dock => on replace les icones.
		
		cairo_dock_replace_all_dialogs ();
	}
	
	gtk_widget_queue_draw (pWidget);
	
	return FALSE;
}



static gboolean s_bWaitForData = FALSE;

void cairo_dock_on_drag_data_received (GtkWidget *pWidget, GdkDragContext *dc, gint x, gint y, GtkSelectionData *selection_data, guint info, guint time, CairoDock *pDock)
{
	g_print ("%s (%dx%d, %d)\n", __func__, x, y, time);
	//\_________________ On recupere l'URI.
	gchar *cReceivedData = (gchar *) selection_data->data;  // gtk_selection_data_get_text
	g_return_if_fail (cReceivedData != NULL);
	int length = strlen (cReceivedData);
	if (cReceivedData[length-1] == '\n')
		cReceivedData[--length] = '\0';  // on vire le retour chariot final.
	if (cReceivedData[length-1] == '\r')
		cReceivedData[--length] = '\0';
	
	if (s_bWaitForData)
	{
		s_bWaitForData = FALSE;
		gdk_drag_status (dc, GDK_ACTION_COPY, time);
		g_print ("drag info : <%s>\n", cReceivedData);
		pDock->iAvoidingMouseIconType = CAIRO_DOCK_LAUNCHER;
		if (g_str_has_suffix (cReceivedData, ".desktop")/** || g_str_has_suffix (cReceivedData, ".sh")*/)
			pDock->fAvoidingMouseMargin = .5;  // on ne sera jamais dessus.
		else
			pDock->fAvoidingMouseMargin = .25;
		return ;
	}
	
	//\_________________ On arrete l'animation.
	//cairo_dock_stop_marking_icons (pDock);
	pDock->iAvoidingMouseIconType = -1;
	pDock->fAvoidingMouseMargin = 0;
	
	//\_________________ On calcule la position a laquelle on l'a lache.
	cd_message (">>> cReceivedData : '%s'", cReceivedData);
	int iDropX = (pDock->container.bIsHorizontal ? x : y);
	double fOrder = CAIRO_DOCK_LAST_ORDER;
	Icon *pPointedIcon = NULL, *pNeighboorIcon = NULL;
	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		if (icon->bPointed)
		{
			//g_print ("On pointe sur %s\n", icon->cName);
			pPointedIcon = icon;
			double fMargin;  /// deviendra obsolete si le drag-received fonctionne.
			if (g_str_has_suffix (cReceivedData, ".desktop")/** || g_str_has_suffix (cReceivedData, ".sh")*/)  // si c'est un .desktop, on l'ajoute.
				fMargin = 0.5;  // on ne sera jamais dessus.
			else  // sinon on le lance si on est sur l'icone, et on l'ajoute autrement.
				fMargin = 0.25;

			if (iDropX > icon->fX + icon->fWidth * icon->fScale * (1 - fMargin))  // on est apres.
			{
				pNeighboorIcon = (ic->next != NULL ? ic->next->data : NULL);
				fOrder = (pNeighboorIcon != NULL ? (icon->fOrder + pNeighboorIcon->fOrder) / 2 : icon->fOrder + 1);
			}
			else if (iDropX < icon->fX + icon->fWidth * icon->fScale * fMargin)  // on est avant.
			{
				pNeighboorIcon = (ic->prev != NULL ? ic->prev->data : NULL);
				fOrder = (pNeighboorIcon != NULL ? (icon->fOrder + pNeighboorIcon->fOrder) / 2 : icon->fOrder - 1);
			}
			else  // on est dessus.
			{
				fOrder = CAIRO_DOCK_LAST_ORDER;
			}
		}
	}
	
	cairo_dock_notify_drop_data (cReceivedData, pPointedIcon, fOrder, CAIRO_CONTAINER (pDock));
	
	gtk_drag_finish (dc, TRUE, FALSE, time);
}

gboolean cairo_dock_on_drag_drop (GtkWidget *pWidget, GdkDragContext *dc, gint x, gint y, guint time, CairoDock *pDock)
{
	cd_message ("%s (%dx%d, %d)", __func__, x, y, time);
	GdkAtom target = gtk_drag_dest_find_target (pWidget, dc, NULL);
	gtk_drag_get_data (pWidget, dc, target, time);
	return TRUE;  // in a drop zone.
}

gboolean cairo_dock_notification_drop_data (gpointer pUserData, const gchar *cReceivedData, Icon *icon, double fOrder, CairoContainer *pContainer)
{
	if (! CAIRO_DOCK_IS_DOCK (pContainer))
		return CAIRO_DOCK_LET_PASS_NOTIFICATION;
	
	CairoDock *pDock = CAIRO_DOCK (pContainer);
	if (icon == NULL || CAIRO_DOCK_IS_LAUNCHER (icon) || CAIRO_DOCK_IS_SEPARATOR (icon))
	{
		CairoDock *pReceivingDock = pDock;
		if (g_str_has_suffix (cReceivedData, ".desktop") /**|| g_str_has_suffix (cReceivedData, ".sh")*/)  // c'est un fichier .desktop ou un script, on choisit de l'ajouter quoiqu'il arrive.
		{
			if (fOrder == CAIRO_DOCK_LAST_ORDER)  // on a lache dessus.
			{
				if (icon && icon->pSubDock != NULL)  // on l'ajoutera au sous-dock.
				{
					pReceivingDock = icon->pSubDock;
				}
			}
		}
		else  // c'est un fichier.
		{
			if (fOrder == CAIRO_DOCK_LAST_ORDER)  // on a lache dessus.
			{
				if (CAIRO_DOCK_IS_LAUNCHER (icon))
				{
					if (CAIRO_DOCK_IS_URI_LAUNCHER (icon))
					{
						if (icon->pSubDock != NULL || icon->iVolumeID != 0)  // on le lache sur un repertoire ou un point de montage.
						{
							cairo_dock_fm_move_into_directory (cReceivedData, icon, pContainer);
							return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
						}
						else  // on le lache sur un fichier.
						{
							return CAIRO_DOCK_LET_PASS_NOTIFICATION;
						}
					}
					else if (CAIRO_DOCK_IS_CONTAINER_LAUNCHER (icon))  // on le lache sur un sous-dock de lanceurs.
					{
						pReceivingDock = icon->pSubDock;
					}
					else  // on le lache sur un lanceur.
					{
						gchar *cCommand = g_strdup_printf ("%s \"%s\"", icon->cCommand, cReceivedData + (strncmp (cReceivedData, "file://", 7) == 0 ? 7 : 0));  // tous les programmes ne gerent pas les URI; pour parer au cas ou il ne le gererait pas, dans le cas d'un fichier local, on convertit en un chemin classique.
						cd_message ("will open the file with the command '%s'...\n", cCommand);
						g_spawn_command_line_async (cCommand, NULL);
						g_free (cCommand);
						cairo_dock_request_icon_animation (icon, pDock, "blink", 2);
						return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
					}
				}
				else  // on le lache sur autre chose qu'un lanceur.
				{
					return CAIRO_DOCK_LET_PASS_NOTIFICATION;
				}
			}
			else  // on a lache a cote.
			{
				Icon *pPointingIcon = cairo_dock_search_icon_pointing_on_dock (pDock, NULL);
				if (CAIRO_DOCK_IS_URI_LAUNCHER (pPointingIcon))  // on a lache dans un dock qui est un repertoire, on copie donc le fichier dedans.
				{
					cairo_dock_fm_move_into_directory (cReceivedData, icon, pContainer);
					return CAIRO_DOCK_INTERCEPT_NOTIFICATION;
				}
			}
		}

		if (g_bLocked)
			return CAIRO_DOCK_LET_PASS_NOTIFICATION;
		
		cairo_dock_add_new_launcher_by_uri (cReceivedData, pReceivingDock, fOrder);
	}
	return CAIRO_DOCK_LET_PASS_NOTIFICATION;
}


gboolean cairo_dock_on_drag_motion (GtkWidget *pWidget, GdkDragContext *dc, gint x, gint y, guint time, CairoDock *pDock)
{
	//g_print ("%s (%d;%d, %d)\n", __func__, x, y, time);
	int X, Y;
	if (pDock->container.bIsHorizontal)
	{
		X = x - pDock->container.iWidth/2;
		Y = y;
	}
	else
	{
		Y = x;
		X = y - pDock->container.iWidth/2;
	}
	int w, h;
	if (pDock->iInputState == CAIRO_DOCK_INPUT_AT_REST)
	{
		w = pDock->iMinDockWidth;
		h = pDock->iMinDockHeight;
		
		if (X <= -w/2 || X >= w/2)
			return FALSE;  // on n'accepte pas le drop.
		if (pDock->container.bDirectionUp)
		{
			if (Y <= pDock->container.iHeight - h || Y >= pDock->container.iHeight)
				return FALSE;  // on n'accepte pas le drop.
		}
		else
		{
			if (Y < 0 || Y > h)
				return FALSE;  // on n'accepte pas le drop.
		}
	}
	else if (pDock->iInputState == CAIRO_DOCK_INPUT_HIDDEN)
	{
		w = MIN (myAccessibility.iVisibleZoneWidth, pDock->iMaxDockWidth);
		h = MIN (myAccessibility.iVisibleZoneHeight, pDock->iMaxDockHeight);
		
		if (X <= -w/2 || X >= w/2)
			return FALSE;  // on n'accepte pas le drop.
		if (pDock->container.bDirectionUp)
		{
			if (Y <= pDock->container.iHeight - h || Y >= pDock->container.iHeight)
				return FALSE;  // on n'accepte pas le drop.
		}
		else
		{
			if (Y < 0 || Y > h)
				return FALSE;  // on n'accepte pas le drop.
		}
	}
	
	//\_________________ On simule les evenements souris habituels.
	if (! pDock->bIsDragging)
	{
		g_print ("start dragging\n");
		pDock->bIsDragging = TRUE;
		
		/*GdkAtom gdkAtom = gdk_drag_get_selection (dc);
		Atom xAtom = gdk_x11_atom_to_xatom (gdkAtom);
		Window Xid = GDK_WINDOW_XID (dc->source_window);
		g_print (" <%s>\n", cairo_dock_get_property_name_on_xwindow (Xid, xAtom));*/
		
		gboolean bStartAnimation = FALSE;
		cairo_dock_notify (CAIRO_DOCK_START_DRAG_DATA, pDock, &bStartAnimation);
		if (bStartAnimation)
			cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
		
		/*pDock->iAvoidingMouseIconType = -1;
		
		GdkAtom target = gtk_drag_dest_find_target (pWidget, dc, NULL);
		if (target == GDK_NONE)
			gdk_drag_status (dc, 0, time);
		else
		{
			gtk_drag_get_data (pWidget, dc, target, time);
			s_bWaitForData = TRUE;
			g_print ("get-data envoye\n");
		}*/
		
		///cairo_dock_on_enter_notify (pWidget, NULL, pDock);  // ne sera effectif que la 1ere fois a chaque entree dans un dock.
	}
	else
	{
		//g_print ("move dragging\n");
		cairo_dock_on_motion_notify (pWidget, NULL, pDock);
	}
	
	gdk_drag_status (dc, GDK_ACTION_COPY, time);
	return TRUE;  // on accepte le drop.
}

void cairo_dock_on_drag_leave (GtkWidget *pWidget, GdkDragContext *dc, guint time, CairoDock *pDock)
{
	g_print ("stop dragging\n");
	s_bWaitForData = FALSE;
	pDock->bIsDragging = FALSE;
	pDock->bCanDrop = FALSE;
	//cairo_dock_stop_marking_icons (pDock);
	pDock->iAvoidingMouseIconType = -1;
	cairo_dock_emit_leave_signal (pDock);
}



void cairo_dock_show_dock_at_mouse (CairoDock *pDock)
{
	g_return_if_fail (pDock != NULL);
	int iMouseX, iMouseY;
	if (pDock->container.bIsHorizontal)
		gdk_window_get_pointer (pDock->container.pWidget->window, &iMouseX, &iMouseY, NULL);
	else
		gdk_window_get_pointer (pDock->container.pWidget->window, &iMouseY, &iMouseX, NULL);
	//g_print (" %d;%d\n", iMouseX, iMouseY);
	
	pDock->iGapX = pDock->container.iWindowPositionX + iMouseX - g_iXScreenWidth[pDock->container.bIsHorizontal] * pDock->fAlign;
	pDock->iGapY = (pDock->container.bDirectionUp ? g_iXScreenHeight[pDock->container.bIsHorizontal] - (pDock->container.iWindowPositionY + iMouseY) : pDock->container.iWindowPositionY + iMouseY);
	//g_print (" => %d;%d\n", g_pMainDock->iGapX, g_pMainDock->iGapY);
	
	int iNewPositionX, iNewPositionY;
	cairo_dock_get_window_position_at_balance (pDock,
		pDock->container.iWidth, pDock->container.iHeight,
		&iNewPositionX, &iNewPositionY);
	
	gtk_window_move (GTK_WINDOW (pDock->container.pWidget),
		(pDock->container.bIsHorizontal ? iNewPositionX : iNewPositionY),
		(pDock->container.bIsHorizontal ? iNewPositionY : iNewPositionX));
	gtk_widget_show (pDock->container.pWidget);
}

void cairo_dock_raise_from_keyboard (const char *cKeyShortcut, gpointer data)
{
	if (GTK_WIDGET_VISIBLE (g_pMainDock->container.pWidget))
	{
		gtk_widget_hide (g_pMainDock->container.pWidget);
	}
	else
	{
		cairo_dock_show_dock_at_mouse (g_pMainDock);
	}
	s_bHideAfterShortcut = FALSE;
}

void cairo_dock_hide_dock_like_a_menu (void)
{
	if (s_bHideAfterShortcut && GTK_WIDGET_VISIBLE (g_pMainDock->container.pWidget))
	{
		gtk_widget_hide (g_pMainDock->container.pWidget);
		s_bHideAfterShortcut = FALSE;
	}
}
