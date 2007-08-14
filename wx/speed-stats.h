/*
 * Xmission - a cross-platform bittorrent client
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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

#ifndef __XMISSION_SPEED_STATS_H__
#define __XMISSION_SPEED_STATS_H__

#include <wx/panel.h>
#include <libtransmission/transmission.h>

class SpeedStats: public wxPanel
{
    public:

        SpeedStats( wxWindow * parent,
                      wxWindowID id = wxID_ANY,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize,
                      long style = wxTAB_TRAVERSAL,
                      const wxString& name = _T("panel"));

        virtual ~SpeedStats() {}

        void Update( tr_handle_t * handle );

    public:

        void OnPaint( wxPaintEvent& );

    private:

       DECLARE_EVENT_TABLE()
};

#endif
