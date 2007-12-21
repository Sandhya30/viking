/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifdef HAVE_CONFIG
#include "config.h"
#endif /* HAVE_CONFIG */

#include "viking.h"
#include "icons/viking_icon.png_h"
#include "mapcache.h"
#include "background.h"
#include "dems.h"
#include "curl_download.h"
#include "preferences.h"

#include <stdlib.h>
#include <string.h>

#include <glib/gprintf.h>
#include <glib/gi18n.h>

#include "modules.h"

#define MAX_WINDOWS 1024

static guint window_count = 0;

static VikWindow *new_window ();
static void open_window ( VikWindow *vw, const gchar **files );
static void destroy( GtkWidget *widget,
                     gpointer   data );


/* Another callback */
static void destroy( GtkWidget *widget,
                     gpointer   data )
{
    if ( ! --window_count )
      gtk_main_quit ();
}

static VikWindow *new_window ()
{
  if ( window_count < MAX_WINDOWS )
  {
    VikWindow *vw = vik_window_new ();

    g_signal_connect (G_OBJECT (vw), "destroy",
		      G_CALLBACK (destroy), NULL);
    g_signal_connect (G_OBJECT (vw), "newwindow",
		      G_CALLBACK (new_window), NULL);
    g_signal_connect (G_OBJECT (vw), "openwindow",
		      G_CALLBACK (open_window), NULL);

    gtk_widget_show_all ( GTK_WIDGET(vw) );

    window_count++;

    return vw;
  }
  return NULL;
}

static void open_window ( VikWindow *vw, const gchar **files )
{
  VikWindow *newvw = new_window();
  gboolean change_fn = (!files[1]); /* only change fn if one file */
  if ( newvw )
    while ( *files ) {
      vik_window_open_file ( newvw, *(files++), change_fn );
    }
}

/* Options */
static gboolean version = FALSE;

static GOptionEntry entries[] = 
{
  { "version", 'v', 0, G_OPTION_ARG_NONE, &version, "Show version", NULL },
  { NULL }
};

int main( int argc, char *argv[] )
{
  VikWindow *first_window;
  GdkPixbuf *main_icon;
  gboolean dashdash_already = FALSE;
  int i = 0;
  GError *error = NULL;
  gboolean gui_initialized;
	
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);  
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_thread_init ( NULL );
  gdk_threads_init ();

  gui_initialized = gtk_init_with_args (&argc, &argv, "files+", entries, NULL, &error);
  if (!gui_initialized)
  {
    /* check if we have an error message */
    if (error == NULL)
    {
      /* no error message, the GUI initialization failed */
      const gchar *display_name = gdk_get_display_arg_name ();
      g_fprintf (stderr, "Failed to open display: %s\n", (display_name != NULL) ? display_name : " ");
    }
    else
    {
      /* yep, there's an error, so print it */
      g_fprintf (stderr, "Parsing command line options failed: %s\n", error->message);
      g_error_free (error);
      g_fprintf (stderr, "Run \"%s --help\" to see the list of recognized options.\n",argv[0]);
    }
    return EXIT_FAILURE;
  }
   
  if (version)
  {
    g_printf ("%s %s, Copyright (c) 2003-2007 Evan Battaglia\n", PACKAGE_NAME, PACKAGE_VERSION);
    return EXIT_SUCCESS;
  }

  curl_download_init();

  /* Init modules/plugins */
  modules_init();

  a_mapcache_init ();
  a_background_init ();
  a_preferences_init ();
  vik_layer_cursors_init ();
  vik_window_cursors_init ();

  /* Set the icon */
  main_icon = gdk_pixbuf_from_pixdata(&viking_icon, FALSE, NULL);
  gtk_window_set_default_icon(main_icon);

  /* Create the first window */
  first_window = new_window();

  gdk_threads_enter ();
  while ( ++i < argc ) {
    if ( strcmp(argv[i],"--") == 0 && !dashdash_already )
      dashdash_already = TRUE; /* hack to open '-' */
    else
      vik_window_open_file ( first_window, argv[i], argc == 2 );
  }

  gtk_main ();
  gdk_threads_leave ();

  a_mapcache_uninit ();
  a_dems_uninit ();
  a_preferences_uninit ();
  vik_layer_cursors_uninit ();
  vik_window_cursors_uninit ();

  return 0;
}
