/*
 * consumer_gtk2.c -- A consumer for GTK2 apps
 * Copyright (C) 2003-2004 Ushodaya Enterprises Limited
 * Author: Charles Yates
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "consumer_gtk2.h"

#include <stdlib.h>
#include <framework/mlt_consumer.h>
#include <framework/mlt_factory.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

mlt_consumer consumer_gtk2_preview_init( GtkWidget *widget )
{
	// Create an sdl preview consumer
	mlt_consumer consumer = NULL;

	// This is a nasty little hack which is required by SDL
	if ( widget != NULL )
	{
        Window xwin = GDK_WINDOW_XWINDOW( widget->window );
        char windowhack[ 32 ];
        sprintf( windowhack, "%ld", xwin );
        setenv( "SDL_WINDOWID", windowhack, 1 );
	}

	// Create an sdl preview consumer
	consumer = mlt_factory_consumer( "sdl_preview", NULL );

	// Now assign the lock/unlock callbacks
	if ( consumer != NULL )
	{
		mlt_properties properties = mlt_consumer_properties( consumer );
		mlt_properties_set_int( properties, "app_locked", 1 );
		mlt_properties_set_data( properties, "app_lock", gdk_threads_enter, 0, NULL, NULL );
		mlt_properties_set_data( properties, "app_unlock", gdk_threads_leave, 0, NULL, NULL );
	}

	return consumer;
}
