/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <gtk/gtk.h>
#include "actions.h"
#include "tr_icon.h"
#include "util.h"

#ifndef STATUS_ICON_SUPPORTED

gpointer
tr_icon_new( void )
{
    return NULL;
}

#else

static void
activated ( GtkStatusIcon   * self        UNUSED,
            gpointer          user_data   UNUSED )
{
    action_activate ("toggle-main-window");
}

static void
popup ( GtkStatusIcon  * self,
        guint            button,
        guint            when,
        gpointer         data    UNUSED )
{
    GtkWidget * w = action_get_widget( "/icon-popup" );
    gtk_menu_popup (GTK_MENU(w), NULL, NULL,
                    gtk_status_icon_position_menu,
                    self, button, when );
}

gpointer
tr_icon_new( void )
{
    GtkStatusIcon * ret = gtk_status_icon_new_from_stock ("transmission-logo");
    g_signal_connect( ret, "activate", G_CALLBACK( activated ), NULL );
    g_signal_connect( ret, "popup-menu", G_CALLBACK( popup ), NULL );
    return ret;
}

#endif
