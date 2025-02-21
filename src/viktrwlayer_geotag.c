/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2011, Rob Norris <rw_norris@hotmail.com>
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
/*
 *  Similar to the track and trackpoint properties dialogs,
 *   this is made a separate file for ease of grouping related stuff together
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "viking.h"
#include "vikfilelist.h"
#include "geotag_exif.h"
#include "thumbnails.h"
#include "background.h"

// Function taken from GPSCorrelate 1.6.1
// ConvertToUnixTime Copyright 2005 Daniel Foote. GPL2+

#define EXIF_DATE_FORMAT "%d:%d:%d %d:%d:%d"

static time_t ConvertToUnixTime(char* StringTime, char* Format, int TZOffsetHours, int TZOffsetMinutes, gboolean time_is_local)
{
	/* Read the time using the specified format.
	 * The format and string being read from must
	 * have the most significant time on the left,
	 * and the least significant on the right:
	 * ie, Year on the left, seconds on the right. */

	/* Sanity check... */
	if (StringTime == NULL || Format == NULL)
	{
		return 0;
	}

	/* Define and set up our structure. */
	struct tm Time;
	Time.tm_wday = 0;
	Time.tm_yday = 0;

	/* Read out the time from the string using our format. */
	sscanf(StringTime, Format, &Time.tm_year, &Time.tm_mon,
			&Time.tm_mday, &Time.tm_hour,
			&Time.tm_min, &Time.tm_sec);

	/* Adjust the years & months for valid tm struct values. */
	Time.tm_year -= 1900;
	Time.tm_mon  -= 1;

	/* Calculate the unix time. */
	time_t thetime;
	if ( time_is_local ) {
		Time.tm_isdst = -1;
		// Time is apparently in local time, thus use mktime() which is locale dependent.
		// Hence this assumes the current timezone is the same as the timezone the image was taken in.
		thetime = mktime ( &Time );
	}
	else {
		Time.tm_isdst = 0; // there is no DST in UTC
		thetime = util_timegm ( &Time );
	}
	/* Apply specified timezone offset to the time. */
	/* Note also that we SUBTRACT these times */
	thetime -= TZOffsetHours * 60 * 60;
	thetime -= TZOffsetMinutes * 60;

	return thetime;
}

// GPSCorrelate END

typedef struct {
	GtkWidget *dialog;
	VikFileList *files;
	VikTrwLayer *vtl;    // to pass on
	VikWaypoint *wpt;    // Use specified waypoint or otherwise the track(s) if NULL
	VikTrack *track;     // Use specified track or all tracks if NULL
	GtkCheckButton *create_waypoints_b;
	GtkLabel *overwrite_waypoints_l; // Referenced so the sensitivity can be changed
	GtkCheckButton *overwrite_waypoints_b;
	GtkCheckButton *write_exif_b;
	GtkLabel *overwrite_gps_exif_l; // Referenced so the sensitivity can be changed
	GtkCheckButton *overwrite_gps_exif_b;
	GtkLabel *no_change_mtime_l; // Referenced so the sensitivity can be changed
	GtkCheckButton *auto_image_direction_b;
	GtkCheckButton *no_change_mtime_b;
	GtkCheckButton *interpolate_segments_b;
	GtkCheckButton *time_is_local_b;
	GtkEntry *time_zone_b; // TODO consider a more user friendly tz widget eg libtimezonemap or similar
	GtkEntry *time_offset_b;
} GeoTagWidgets;

static GeoTagWidgets *geotag_widgets_new()
{
	GeoTagWidgets *widgets = g_malloc0(sizeof(GeoTagWidgets));
	return widgets;
}

static void geotag_widgets_free ( GeoTagWidgets *widgets )
{
	// Need to free VikFileList??
	g_free(widgets);
}

typedef struct {
	gboolean create_waypoints;
	gboolean overwrite_waypoints;
	gboolean write_exif;
	gboolean overwrite_gps_exif;
	gboolean auto_image_direction;
	gboolean no_change_mtime;
	gboolean interpolate_segments;
	gint time_offset;
	gboolean time_is_local;
	gint TimeZoneHours;
	gint TimeZoneMins;
} option_values_t;

typedef struct {
	VikTrwLayer *vtl;
	gchar *image;
	VikWaypoint *wpt;    // Use specified waypoint or otherwise the track(s) if NULL
	VikTrack *track;     // Use specified track or all tracks if NULL
	// User options...
	option_values_t ov;
	GList *files;
	time_t PhotoTime;
	// Store answer from interpolation for an image
	gboolean found_match;
	VikCoord coord;
	gdouble altitude;
	gdouble image_direction;
	// If anything has changed
	gboolean redraw;
} geotag_options_t;

#define VIK_SETTINGS_GEOTAG_CREATE_WAYPOINT      "geotag_create_waypoints"
#define VIK_SETTINGS_GEOTAG_OVERWRITE_WAYPOINTS  "geotag_overwrite_waypoints"
#define VIK_SETTINGS_GEOTAG_WRITE_EXIF           "geotag_write_exif"
#define VIK_SETTINGS_GEOTAG_OVERWRITE_GPS_EXIF   "geotag_overwrite_gps"
#define VIK_SETTINGS_GEOTAG_AUTO_IMAGE_DIRECTION "geotag_auto_image_direction"
#define VIK_SETTINGS_GEOTAG_NO_CHANGE_MTIME      "geotag_no_change_mtime"
#define VIK_SETTINGS_GEOTAG_INTERPOLATE_SEGMENTS "geotag_interpolate_segments"
#define VIK_SETTINGS_GEOTAG_TIME_OFFSET          "geotag_time_offset"
#define VIK_SETTINGS_GEOTAG_TIME_OFFSET_HOURS    "geotag_time_offset_hours"
#define VIK_SETTINGS_GEOTAG_TIME_OFFSET_MINS     "geotag_time_offset_mins"
#define VIK_SETTINGS_GEOTAG_TIME_IS_LOCAL        "geotag_time_is_local"

static void save_default_values ( option_values_t default_values )
{
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_CREATE_WAYPOINT, default_values.create_waypoints );
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_OVERWRITE_WAYPOINTS, default_values.overwrite_waypoints );
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_WRITE_EXIF, default_values.write_exif );
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_OVERWRITE_GPS_EXIF, default_values.overwrite_gps_exif );
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_AUTO_IMAGE_DIRECTION, default_values.auto_image_direction );
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_NO_CHANGE_MTIME, default_values.no_change_mtime );
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_INTERPOLATE_SEGMENTS, default_values.interpolate_segments );
	a_settings_set_integer ( VIK_SETTINGS_GEOTAG_TIME_OFFSET, default_values.time_offset );
	a_settings_set_boolean ( VIK_SETTINGS_GEOTAG_TIME_IS_LOCAL, default_values.time_is_local );
	a_settings_set_integer ( VIK_SETTINGS_GEOTAG_TIME_OFFSET_HOURS, default_values.TimeZoneHours );
	a_settings_set_integer ( VIK_SETTINGS_GEOTAG_TIME_OFFSET_MINS, default_values.TimeZoneMins );
}

static option_values_t get_default_values ( )
{
	option_values_t default_values;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_CREATE_WAYPOINT, &default_values.create_waypoints ) )
		default_values.create_waypoints = TRUE;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_OVERWRITE_WAYPOINTS, &default_values.overwrite_waypoints ) )
		default_values.overwrite_waypoints = TRUE;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_WRITE_EXIF, &default_values.write_exif ) )
		default_values.write_exif = TRUE;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_OVERWRITE_GPS_EXIF, &default_values.overwrite_gps_exif ) )
		default_values.overwrite_gps_exif = FALSE;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_AUTO_IMAGE_DIRECTION, &default_values.auto_image_direction ) )
		default_values.auto_image_direction = FALSE;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_NO_CHANGE_MTIME, &default_values.no_change_mtime ) )
		default_values.no_change_mtime = TRUE;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_INTERPOLATE_SEGMENTS, &default_values.interpolate_segments ) )
		default_values.interpolate_segments = TRUE;
	if ( ! a_settings_get_integer ( VIK_SETTINGS_GEOTAG_TIME_OFFSET, &default_values.time_offset ) )
		default_values.time_offset = 0;
	if ( ! a_settings_get_boolean ( VIK_SETTINGS_GEOTAG_TIME_IS_LOCAL, &default_values.time_is_local ) )
		default_values.time_is_local = FALSE;
	if ( ! a_settings_get_integer ( VIK_SETTINGS_GEOTAG_TIME_OFFSET_HOURS, &default_values.TimeZoneHours ) )
		default_values.TimeZoneHours = 0;
	if ( ! a_settings_get_integer ( VIK_SETTINGS_GEOTAG_TIME_OFFSET_MINS, &default_values.TimeZoneMins ) )
		default_values.TimeZoneMins = 0;
	return default_values;
}

/**
 * Get a heading from a single trkpoint
 *
 * Try using the adjacent trkpoints to get a direction
 */
static gdouble get_heading_from_trackpoint ( GList *mytrkpt )
{
	VikTrackpoint *trkpt = VIK_TRACKPOINT(mytrkpt->data);

	if ( mytrkpt->prev )
		return vik_coord_angle ( &VIK_TRACKPOINT(mytrkpt->prev->data)->coord, &trkpt->coord );
	else if ( mytrkpt->next )
		return vik_coord_angle ( &trkpt->coord, &VIK_TRACKPOINT(mytrkpt->prev->data)->coord );

	// In the unlikely event of a single trackpoint track - can't guess a direction
	return NAN;
}

/**
 * Correlate the image against the specified track
 */
static void trw_layer_geotag_track ( const gpointer id, VikTrack *track, geotag_options_t *options )
{
	// If already found match then don't need to check this track
	if ( options->found_match )
		return;

	VikTrackpoint *trkpt;
	VikTrackpoint *trkpt_next;

	GList *mytrkpt;
	for ( mytrkpt = track->trackpoints; mytrkpt; mytrkpt = mytrkpt->next ) {

		// Do something for this trackpoint...

		trkpt = VIK_TRACKPOINT(mytrkpt->data);

		if ( isnan(trkpt->timestamp) ) {
			continue;
		}

		// is it exactly this point?
		if ( options->PhotoTime == trkpt->timestamp ) {
			options->coord = trkpt->coord;
			options->altitude = trkpt->altitude;
			options->found_match = TRUE;
			if ( options->ov.auto_image_direction )
				options->image_direction = get_heading_from_trackpoint ( mytrkpt );
			break;
		}

		// Now need two trackpoints, hence check next is available
		if ( !mytrkpt->next ) break;
		trkpt_next = VIK_TRACKPOINT(mytrkpt->next->data);

		if ( isnan(trkpt_next->timestamp) ) continue;
		if ( trkpt->timestamp == trkpt_next->timestamp ) continue;
		if ( trkpt->timestamp > trkpt_next->timestamp ) continue;

		// When interpolating between segments, no need for any special segment handling
		if ( !options->ov.interpolate_segments )
			// Don't check between segments
			if ( trkpt_next->newsegment )
				// Simply move on to consider next point
				continue;

		// Too far
		if ( trkpt->timestamp > options->PhotoTime ) break;

		// Is is between this and the next point?
		if ( (options->PhotoTime > trkpt->timestamp) && (options->PhotoTime < trkpt_next->timestamp) ) {
			options->found_match = TRUE;
			// Interpolate
			/* Calculate the "scale": a decimal giving the relative distance
			 * in time between the two points. Ie, a number between 0 and 1 -
			 * 0 is the first point, 1 is the next point, and 0.5 would be
			 * half way. */
			gdouble tdiff = (gdouble)trkpt_next->timestamp - (gdouble)trkpt->timestamp;
			gdouble scale = ((gdouble)options->PhotoTime - (gdouble)trkpt->timestamp) / tdiff;

			options->PhotoTime = options->PhotoTime + (time_t)(tdiff * scale);

			struct LatLon ll_result, ll1, ll2;

			vik_coord_to_latlon ( &(trkpt->coord), &ll1 );
			vik_coord_to_latlon ( &(trkpt_next->coord), &ll2 );

			ll_result.lat = ll1.lat + ((ll2.lat - ll1.lat) * scale);

			// NB This won't cope with going over the 180 degrees longitude boundary
			ll_result.lon = ll1.lon + ((ll2.lon - ll1.lon) * scale);

			// set coord
			vik_coord_load_from_latlon ( &(options->coord), VIK_COORD_LATLON, &ll_result );

			// Interpolate elevation
			options->altitude = trkpt->altitude + ((trkpt_next->altitude - trkpt->altitude) * scale);

			if ( options->ov.auto_image_direction )
				options->image_direction = vik_coord_angle ( &trkpt->coord, &trkpt_next->coord );

			break;
		}
	}
}

/**
 * Simply align the images the waypoint position
 */
static void trw_layer_geotag_waypoint ( geotag_options_t *options )
{
	// Write EXIF if specified - although a fairly useless process if you've turned it off!
	if ( options->ov.write_exif ) {
		gboolean has_gps_exif = FALSE;
		gchar* datetime = a_geotag_get_exif_date_from_file ( options->image, &has_gps_exif );
		// If image already has gps info - don't attempt to change it unless forced
		if ( options->ov.overwrite_gps_exif || !has_gps_exif ) {
			gint ans = a_geotag_write_exif_gps ( options->image, options->wpt->coord, options->wpt->altitude,
			                                     options->wpt->image_direction, options->wpt->image_direction_ref,
			                                     options->ov.no_change_mtime );
			if ( ans != 0 ) {
				gchar *message = g_strdup_printf ( _("Failed updating EXIF on %s"), options->image );
				vik_window_statusbar_update ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(options->vtl)), message, VIK_STATUSBAR_INFO );
				g_free ( message );
			}
		}
		g_free ( datetime );
	}
}

/**
 * Correlate the image to any track within the TrackWaypoint layer
 */
static void trw_layer_geotag_process ( geotag_options_t *options )
{
	if ( !options->vtl || !IS_VIK_LAYER(options->vtl) )
		return;

	if ( !options->image )
		return;

	if ( options->wpt ) {
		trw_layer_geotag_waypoint ( options );
		return;
	}

	gboolean has_gps_exif = FALSE;
	gchar* datetime = a_geotag_get_exif_date_from_file ( options->image, &has_gps_exif );

	if ( datetime ) {
	
		// If image already has gps info - don't attempt to change it.
		if ( !options->ov.overwrite_gps_exif && has_gps_exif ) {
			if ( options->ov.create_waypoints ) {
				// Create waypoint with file information
				gchar *name = NULL;
				VikWaypoint *wp = a_geotag_create_waypoint_from_file ( options->image, vik_trw_layer_get_coord_mode (options->vtl), &name );
				if ( !wp ) {
					// Couldn't create Waypoint
					g_free ( datetime );
					return;
				}
				if ( !name )
					name = g_strdup ( a_file_basename ( options->image ) );

				gboolean updated_waypoint = FALSE;

				if ( options->ov.overwrite_waypoints ) {
					VikWaypoint *current_wp = vik_trw_layer_get_waypoint ( options->vtl, name );
					if ( current_wp ) {
						// Existing wp found, so set new position, comment and image
						(void)a_geotag_waypoint_positioned ( options->image, wp->coord, wp->altitude, &name, current_wp );
						updated_waypoint = TRUE;
					}
				}

				if ( !updated_waypoint ) {
					vik_trw_layer_filein_add_waypoint ( options->vtl, name, wp );
				}

				g_free ( name );
				
				// Mark for redraw
				options->redraw = TRUE;
			}
			g_free ( datetime );
			return;
		}

		options->PhotoTime = ConvertToUnixTime ( datetime, EXIF_DATE_FORMAT, options->ov.TimeZoneHours, options->ov.TimeZoneMins, options->ov.time_is_local );
		g_free ( datetime );
		
		// Apply any offset
		options->PhotoTime = options->PhotoTime + options->ov.time_offset;

		options->found_match = FALSE;
		options->image_direction = NAN;

		if ( options->track ) {
			// Single specified track
			// NB Doesn't care about track id
			trw_layer_geotag_track ( NULL, options->track, options );
		}
		else {
			// Try all tracks
			GHashTable *tracks = vik_trw_layer_get_tracks ( options->vtl );
			if ( g_hash_table_size (tracks) > 0 ) {
				g_hash_table_foreach ( tracks, (GHFunc) trw_layer_geotag_track, options );
			}
		}

		// Match found ?
		if ( options->found_match ) {

			if ( options->ov.create_waypoints ) {

				gboolean updated_waypoint = FALSE;

				if ( options->ov.overwrite_waypoints ) {
				
					// Update existing WP
					// Find a WP with current name
					gchar *name = NULL;
					name = g_strdup ( a_file_basename ( options->image ) );
					VikWaypoint *wp = vik_trw_layer_get_waypoint ( options->vtl, name );
					if ( wp ) {
						// Found, so set new position, comment and image
						(void)a_geotag_waypoint_positioned ( options->image, options->coord, options->altitude, &name, wp );
						wp->image_direction_ref = WP_IMAGE_DIRECTION_REF_TRUE;
						wp->image_direction = options->image_direction;
						wp->timestamp = options->PhotoTime;
						updated_waypoint = TRUE;
					}
					g_free ( name );
				}

				if ( !updated_waypoint ) {
					// Create waypoint with found position
					gchar *name = NULL;
					VikWaypoint *wp = a_geotag_waypoint_positioned ( options->image, options->coord, options->altitude, &name, NULL );
					if ( !name )
						name = g_strdup ( a_file_basename ( options->image ) );
					wp->image_direction_ref = WP_IMAGE_DIRECTION_REF_TRUE;
					wp->image_direction = options->image_direction;
					wp->timestamp = options->PhotoTime;
					vik_trw_layer_filein_add_waypoint ( options->vtl, name, wp );
					g_free ( name );
				}

				// Mark for redraw
				options->redraw = TRUE;
			}

			// Write EXIF if specified
			if ( options->ov.write_exif ) {
				gint ans = a_geotag_write_exif_gps ( options->image, options->coord, options->altitude,
				                                     options->image_direction, WP_IMAGE_DIRECTION_REF_TRUE,
				                                     options->ov.no_change_mtime );
				if ( ans != 0 ) {
					gchar *message = g_strdup_printf ( _("Failed updating EXIF on %s"), options->image );
					vik_window_statusbar_update ( VIK_WINDOW(VIK_GTK_WINDOW_FROM_LAYER(options->vtl)), message, VIK_STATUSBAR_INFO );
					g_free ( message );
				}
			}
		}
	}
}

/*
 * Tidy up
 */
static void trw_layer_geotag_thread_free ( geotag_options_t *gtd )
{
	if ( gtd->files )
		g_list_free ( gtd->files );
	g_free ( gtd );
}

/**
 * Run geotagging process in a separate thread
 */
static int trw_layer_geotag_thread ( geotag_options_t *options, gpointer threaddata )
{
	guint total = g_list_length(options->files), done = 0;

	// TODO decide how to report any issues to the user ...

	// Foreach file attempt to geotag it
	while ( options->files ) {
		options->image = (gchar *) ( options->files->data );
		trw_layer_geotag_process ( options );
		options->files = options->files->next;

		// Update thread progress and detect stop requests
		int result = a_background_thread_progress ( threaddata, ((gdouble) ++done) / total );
		if ( result != 0 )
			return -1; /* Abort thread */
	}

	if ( options->redraw ) {
		if ( IS_VIK_LAYER(options->vtl) ) {
			trw_layer_calculate_bounds_waypoints ( options->vtl );
			// Ensure any new images get shown
			trw_layer_verify_thumbnails ( options->vtl );
			// Force redraw as verify only redraws if there are new thumbnails (they may already exist)
			vik_layer_emit_update ( VIK_LAYER(options->vtl) ); // NB Update from background
		}
	}

	return 0;
}

/**
 * Parse user input from dialog response
 */
static void trw_layer_geotag_response_cb ( GtkDialog *dialog, gint resp, GeoTagWidgets *widgets )
{
	switch (resp) {
    case GTK_RESPONSE_DELETE_EVENT: /* received delete event (not from buttons) */
    case GTK_RESPONSE_REJECT:
		break;
	default: {
		//GTK_RESPONSE_ACCEPT:
		// Get options
		geotag_options_t *options = g_malloc ( sizeof(geotag_options_t) );
		options->vtl = widgets->vtl;
		options->wpt = widgets->wpt;
		options->track = widgets->track;
		// Values extracted from the widgets:
		options->ov.create_waypoints = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->create_waypoints_b) );
		options->ov.overwrite_waypoints = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->overwrite_waypoints_b) );
		options->ov.write_exif = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->write_exif_b) );
		options->ov.overwrite_gps_exif = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->overwrite_gps_exif_b) );
		options->ov.auto_image_direction = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->auto_image_direction_b) );
		options->ov.no_change_mtime = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->no_change_mtime_b) );
		options->ov.interpolate_segments = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->interpolate_segments_b) );
		options->ov.time_is_local = gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(widgets->time_is_local_b) );
		options->ov.TimeZoneHours = 0;
		options->ov.TimeZoneMins = 0;
		const gchar* TZString = gtk_entry_get_text(GTK_ENTRY(widgets->time_zone_b));
		/* Check the string. If there is a colon, then (hopefully) it's a time in xx:xx format.
		 * If not, it's probably just a +/-xx format. In all other cases,
		 * it will be interpreted as +/-xx, which, if given a string, returns 0. */
		if (strstr(TZString, ":")) {
			/* Found colon. Split into two. */
			sscanf(TZString, "%d:%d", &options->ov.TimeZoneHours, &options->ov.TimeZoneMins);
			if (options->ov.TimeZoneHours < 0)
				options->ov.TimeZoneMins *= -1;
		} else {
			/* No colon. Just parse. */
			options->ov.TimeZoneHours = atoi(TZString);
		}
		options->ov.time_offset = atoi ( gtk_entry_get_text ( GTK_ENTRY(widgets->time_offset_b) ) );

		options->redraw = FALSE;

		// Save settings for reuse
		save_default_values ( options->ov );

		options->files = g_list_copy ( vik_file_list_get_files ( widgets->files ) );

		gint len = g_list_length ( options->files );
		gchar *tmp = g_strdup_printf ( _("Geotagging %d Images..."), len );

		// Processing lots of files can take time - so run a background effort
		a_background_thread ( BACKGROUND_POOL_LOCAL,
		                      VIK_GTK_WINDOW_FROM_LAYER(options->vtl),
		                      tmp,
		                      (vik_thr_func) trw_layer_geotag_thread,
		                      options,
		                      (vik_thr_free_func) trw_layer_geotag_thread_free,
		                      NULL,
		                      len );

		g_free ( tmp );

		break;
	}
	}
	geotag_widgets_free ( widgets );
	gtk_widget_destroy ( GTK_WIDGET(dialog) );
}

/**
 * Handle widget sensitivities
 */
static void write_exif_b_cb ( GtkWidget *gw, GeoTagWidgets *gtw )
{
	// Overwriting & file modification times are irrelevant if not going to write EXIF!
	if ( gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(gtw->write_exif_b) ) ) {
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_gps_exif_b), TRUE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_gps_exif_l), TRUE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->no_change_mtime_b), TRUE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->no_change_mtime_l), TRUE );
	}
	else {
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_gps_exif_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_gps_exif_l), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->no_change_mtime_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->no_change_mtime_l), FALSE );
	}
}

static void create_waypoints_b_cb ( GtkWidget *gw, GeoTagWidgets *gtw )
{
	// Overwriting waypoints are irrelevant if not going to create them!
	if ( gtk_toggle_button_get_active ( GTK_TOGGLE_BUTTON(gtw->create_waypoints_b) ) ) {
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_waypoints_b), TRUE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_waypoints_l), TRUE );
	}
	else {
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_waypoints_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(gtw->overwrite_waypoints_l), FALSE );
	}
}

/**
 * trw_layer_geotag_dialog:
 * @parent: The Window of the calling process
 * @vtl: The VikTrwLayer to use for correlating images to tracks
 * @track: Optional - The particular track to use (if specified) for correlating images
 * @track_name: Optional - The name of specified track to use
 */
void trw_layer_geotag_dialog ( GtkWindow *parent,
                               VikTrwLayer *vtl,
                               VikWaypoint *wpt,
                               VikTrack *track )
{
	GeoTagWidgets *widgets = geotag_widgets_new();

	widgets->dialog = gtk_dialog_new_with_buttons ( _("Geotag Images"),
													parent,
													GTK_DIALOG_DESTROY_WITH_PARENT,
													GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
													GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT,
													NULL );
	GtkFileFilter *filter = gtk_file_filter_new ();
	gtk_file_filter_set_name ( filter, _("JPG") );
	gtk_file_filter_add_mime_type ( filter, "image/jpeg");

	widgets->files = VIK_FILE_LIST(vik_file_list_new ( _("Images"), filter ));
	widgets->vtl = vtl;
	widgets->wpt = wpt;
	widgets->track = track;
	widgets->create_waypoints_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );
	widgets->overwrite_waypoints_l = GTK_LABEL ( gtk_label_new ( _("Overwrite Existing Waypoints:") ) );
	widgets->overwrite_waypoints_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );
	widgets->write_exif_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );
	widgets->overwrite_gps_exif_l = GTK_LABEL ( gtk_label_new ( _("Overwrite Existing GPS Information:") ) );
	widgets->overwrite_gps_exif_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );
	widgets->no_change_mtime_l = GTK_LABEL ( gtk_label_new ( _("Keep File Modification Timestamp:") ) );
	widgets->no_change_mtime_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );
	widgets->auto_image_direction_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );
	widgets->interpolate_segments_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );
	widgets->time_zone_b = GTK_ENTRY ( gtk_entry_new () );
	widgets->time_offset_b = GTK_ENTRY ( gtk_entry_new () );
	widgets->time_is_local_b = GTK_CHECK_BUTTON ( gtk_check_button_new () );

	gtk_entry_set_width_chars ( widgets->time_zone_b, 7);
	gtk_entry_set_width_chars ( widgets->time_offset_b, 7);

	// Defaults
	option_values_t default_values = get_default_values ();

	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->create_waypoints_b), default_values.create_waypoints );
	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->overwrite_waypoints_b), default_values.overwrite_waypoints );
	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->write_exif_b), default_values.write_exif );
	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->overwrite_gps_exif_b), default_values.overwrite_gps_exif );
	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->auto_image_direction_b), default_values.auto_image_direction );
	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->no_change_mtime_b), default_values.no_change_mtime );
	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->interpolate_segments_b), default_values.interpolate_segments );
	gchar tmp_string[7];
	snprintf (tmp_string, 7, "%+02d:%02d", default_values.TimeZoneHours, abs (default_values.TimeZoneMins) );
	gtk_entry_set_text ( widgets->time_zone_b, tmp_string );
	snprintf (tmp_string, 7, "%d", default_values.time_offset );
	gtk_entry_set_text ( widgets->time_offset_b, tmp_string );

	gtk_toggle_button_set_active ( GTK_TOGGLE_BUTTON(widgets->time_is_local_b), default_values.time_is_local );

	// Ensure sensitivities setup
	write_exif_b_cb ( GTK_WIDGET(widgets->write_exif_b), widgets );
	g_signal_connect ( G_OBJECT(widgets->write_exif_b), "toggled", G_CALLBACK(write_exif_b_cb), widgets );

	create_waypoints_b_cb ( GTK_WIDGET(widgets->create_waypoints_b), widgets );
	g_signal_connect ( G_OBJECT(widgets->create_waypoints_b), "toggled", G_CALLBACK(create_waypoints_b_cb), widgets );

	GtkWidget *cw_hbox = gtk_hbox_new ( FALSE, 0 );
	GtkWidget *create_waypoints_l = gtk_label_new ( _("Create Waypoints:") );
	gtk_box_pack_start ( GTK_BOX(cw_hbox), create_waypoints_l, FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(cw_hbox), GTK_WIDGET(widgets->create_waypoints_b), FALSE, FALSE, 5 );

	GtkWidget *ow_hbox = gtk_hbox_new ( FALSE, 0 );
	gtk_box_pack_start ( GTK_BOX(ow_hbox), GTK_WIDGET(widgets->overwrite_waypoints_l), FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(ow_hbox), GTK_WIDGET(widgets->overwrite_waypoints_b), FALSE, FALSE, 5 );

	GtkWidget *we_hbox = gtk_hbox_new ( FALSE, 0 );
	gtk_box_pack_start ( GTK_BOX(we_hbox), gtk_label_new ( _("Write EXIF:") ), FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(we_hbox), GTK_WIDGET(widgets->write_exif_b), FALSE, FALSE, 5 );

	GtkWidget *og_hbox = gtk_hbox_new ( FALSE, 0 );
	gtk_box_pack_start ( GTK_BOX(og_hbox), GTK_WIDGET(widgets->overwrite_gps_exif_l), FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(og_hbox), GTK_WIDGET(widgets->overwrite_gps_exif_b), FALSE, FALSE, 5 );

	GtkWidget *fm_hbox = gtk_hbox_new ( FALSE, 0 );
	gtk_box_pack_start ( GTK_BOX(fm_hbox), GTK_WIDGET(widgets->no_change_mtime_l), FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(fm_hbox), GTK_WIDGET(widgets->no_change_mtime_b), FALSE, FALSE, 5 );

	GtkWidget *aid_hbox = gtk_hbox_new ( FALSE, 0 );
	gtk_box_pack_start ( GTK_BOX(aid_hbox), gtk_label_new ( _("Automatic Image Direction:") ), FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(aid_hbox), GTK_WIDGET(widgets->auto_image_direction_b), FALSE, FALSE, 5 );
	gtk_widget_set_tooltip_text ( GTK_WIDGET(widgets->auto_image_direction_b), _("Set image direction based on a heading derived from the track") );

	GtkWidget *is_hbox = gtk_hbox_new ( FALSE, 0 );
	GtkWidget *interpolate_segments_l = gtk_label_new ( _("Interpolate Between Track Segments:") );
	gtk_box_pack_start ( GTK_BOX(is_hbox), interpolate_segments_l, FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(is_hbox), GTK_WIDGET(widgets->interpolate_segments_b), FALSE, FALSE, 5 );

	GtkWidget *to_hbox = gtk_hbox_new ( FALSE, 0 );
	GtkWidget *time_offset_l = gtk_label_new ( _("Image Time Offset (Seconds):") );
	gtk_box_pack_start ( GTK_BOX(to_hbox), time_offset_l, FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(to_hbox), GTK_WIDGET(widgets->time_offset_b), FALSE, FALSE, 5 );
	gtk_widget_set_tooltip_text ( GTK_WIDGET(widgets->time_offset_b), _("The number of seconds to ADD to the photos time to make it match the GPS data. Calculate this with (GPS - Photo). Can be negative or positive. Useful to adjust times when a camera's timestamp was incorrect.") );

	GtkWidget *tl_hbox = gtk_hbox_new ( FALSE, 0 );
	GtkWidget *time_is_local_l = gtk_label_new ( _("Image Time is Local:") );
	gtk_box_pack_start ( GTK_BOX(tl_hbox), time_is_local_l, FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(tl_hbox), GTK_WIDGET(widgets->time_is_local_b), FALSE, FALSE, 5 );
	gtk_widget_set_tooltip_text ( GTK_WIDGET(widgets->time_is_local_b), _("The timestamps in the images are in local time (rather than UTC) and so current local timezone adjustment will be automatically applied.") );

	GtkWidget *tz_hbox = gtk_hbox_new ( FALSE, 0 );
	GtkWidget *time_zone_l = gtk_label_new ( _("Image Timezone:") );
	gtk_box_pack_start ( GTK_BOX(tz_hbox), time_zone_l, FALSE, FALSE, 5 );
	gtk_box_pack_start ( GTK_BOX(tz_hbox), GTK_WIDGET(widgets->time_zone_b), FALSE, FALSE, 5 );
	gtk_widget_set_tooltip_text ( GTK_WIDGET(widgets->time_zone_b), _("The timezone adjustment factor. e.g. for AWST use +8:00. This should be unnecessary if using 'time is local'. However time stamps may be offset due to timezone issues.") );

	gchar *track_string = NULL;
	if ( widgets->wpt ) {
		track_string = g_strdup_printf ( _("Using waypoint: %s"), wpt->name );
		// Control sensitivities
		gtk_widget_set_sensitive ( GTK_WIDGET(widgets->create_waypoints_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(create_waypoints_l), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(widgets->overwrite_waypoints_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(widgets->overwrite_waypoints_l), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(widgets->interpolate_segments_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(interpolate_segments_l), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(widgets->time_offset_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(time_offset_l), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(widgets->time_zone_b), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(time_zone_l), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(time_is_local_l), FALSE );
		gtk_widget_set_sensitive ( GTK_WIDGET(widgets->time_is_local_b), FALSE );
	}
	else if ( widgets->track )
		track_string = g_strdup_printf ( _("Using track: %s"), track->name );
	else
		track_string = g_strdup_printf ( _("Using all tracks in: %s"), VIK_LAYER(widgets->vtl)->name );

	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), gtk_label_new ( track_string ), FALSE, FALSE, 5 );

	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), GTK_WIDGET(widgets->files), TRUE, TRUE, 0 );

	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), cw_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), ow_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), we_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), og_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), fm_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), aid_hbox, FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), is_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), to_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), tl_hbox,  FALSE, FALSE, 0);
	gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widgets->dialog))), tz_hbox,  FALSE, FALSE, 0);

	g_signal_connect ( widgets->dialog, "response", G_CALLBACK(trw_layer_geotag_response_cb), widgets );

	gtk_dialog_set_default_response ( GTK_DIALOG(widgets->dialog), GTK_RESPONSE_REJECT );

	gtk_widget_show_all ( widgets->dialog );

	g_free ( track_string );
}
