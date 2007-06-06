/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <gtk/gtk.h>
#include "hig.h"

GtkWidget*
hig_workarea_create (void)
{
	GtkWidget * t = gtk_table_new (4, 100, FALSE);
	gtk_table_set_row_spacings (GTK_TABLE(t), 6);
	gtk_container_set_border_width (GTK_CONTAINER(t), 12);
	return t;
}

void
hig_workarea_add_section_divider  (GtkWidget   * table,
                                   int         * row)
{
	GtkWidget * w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
	gtk_widget_set_usize (w, 0u, 6u);
	gtk_table_attach (GTK_TABLE(table), w, 0, 4, *row, *row+1, 0, 0, 0, 0);
	++*row;
}

void
hig_workarea_add_section_title    (GtkWidget   * table,
                                   int         * row,
                                   const char  * section_title)
{
	char buf[512];
	GtkWidget * l;

	g_snprintf (buf, sizeof(buf), "<b>%s</b>", section_title);
	l = gtk_label_new (buf);
	gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
	gtk_label_set_use_markup (GTK_LABEL(l), TRUE);
	gtk_table_attach (GTK_TABLE(table), l, 0, 4, *row, *row+1, GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);
	++*row;
}

void
hig_workarea_add_section_spacer  (GtkWidget   * table,
                                  int           row,
                                  int           items_in_section)
{
	GtkWidget * w;

	/* spacer to move the fields a little to the right of the name header */
	w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
	gtk_widget_set_usize (w, 18u, 0u);
	gtk_table_attach (GTK_TABLE(table), w, 0, 1, row, row+items_in_section, 0, 0, 0, 0);

	/* spacer between the controls and their labels */
	w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
	gtk_widget_set_usize (w, 12u, 0u);
	gtk_table_attach (GTK_TABLE(table), w, 2, 3, row, row+items_in_section, 0, 0, 0, 0);
}

void
hig_workarea_add_wide_control (GtkWidget * table,
                               int       * row,
                               GtkWidget * w)
{
  gtk_table_attach (GTK_TABLE(table), w,
                    1, 4, *row, *row+1,
                    GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);
  ++*row;
}

GtkWidget *
hig_workarea_add_wide_checkbutton   (GtkWidget   * table,
                                     int         * row,
                                     const char  * mnemonic_string,
                                     gboolean      is_active)
{
  GtkWidget * w = gtk_check_button_new_with_mnemonic (mnemonic_string);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), is_active);
  hig_workarea_add_wide_control (table, row, w);
  return w;
}

void
hig_workarea_add_label_w (GtkWidget   * table,
                          int           row,
                          GtkWidget   * l)
{
  if (GTK_IS_MISC(l))
    gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.0f);
  if (GTK_IS_LABEL(l))
    gtk_label_set_use_markup (GTK_LABEL(l), TRUE);
  gtk_table_attach (GTK_TABLE(table), l, 1, 2, row, row+1, GTK_FILL, GTK_FILL, 0, 0);
}

GtkWidget*
hig_workarea_add_label   (GtkWidget   * table,
                          int           row,
                          const char  * mnemonic_string)
{
  GtkWidget * l = gtk_label_new_with_mnemonic (mnemonic_string);
  hig_workarea_add_label_w (table, row, l);
  return l;
}

void
hig_workarea_add_control (GtkWidget   * table,
                          int           row,
                          GtkWidget   * control)
{
  gtk_table_attach (GTK_TABLE(table), control,
                    3, 4, row, row+1,
                    GTK_EXPAND|GTK_SHRINK|GTK_FILL, 0, 0, 0);
}

void
hig_workarea_add_row_w (GtkWidget * table,
                       int        * row,
                       GtkWidget  * label,
                       GtkWidget  * control,
                       GtkWidget  * mnemonic)
{
  hig_workarea_add_label_w (table, *row, label);
  hig_workarea_add_control (table, *row, control);
  if (GTK_IS_LABEL(label))
    gtk_label_set_mnemonic_widget (GTK_LABEL(label),
                                   mnemonic ? mnemonic : control);
  ++*row;
}

GtkWidget*
hig_workarea_add_row  (GtkWidget   * table,
                       int         * row,
                       const char  * mnemonic_string,
                       GtkWidget   * control,
                       GtkWidget   * mnemonic)
{
  GtkWidget * l = gtk_label_new_with_mnemonic (mnemonic_string);
  hig_workarea_add_row_w (table, row, l, control, mnemonic);
  return l;
}

void
hig_workarea_finish (GtkWidget   * table,
                        int         * row)
{
  GtkWidget * w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 0u, 6u);
  gtk_table_attach_defaults (GTK_TABLE(table), w, 0, 4, *row, *row+1);
}

