/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2012
   Author(s): Christophe Grosjean

*/

#ifndef _REDEMPTION_MOD_NULL_NULL_HPP_
#define _REDEMPTION_MOD_NULL_NULL_HPP_

#include "mod_api.hpp"

// Null module receive every event and does nothing. It allow session code to always have a receiving module active, thus avoidind to test that so back_end is available.

class FrontAPI;

struct null_mod : public mod_api {
    null_mod(FrontAPI & front) : mod_api(0, 0) {}

    virtual void rdp_input_mouse(int device_flags, int x, int y, Keymap2 * keymap) {}

    virtual void rdp_input_scancode(long param1, long param2, long param3, long param4, Keymap2 * keymap) {}

    virtual void rdp_input_synchronize(uint32_t time, uint16_t device_flags, int16_t param1, int16_t param2) {}

    virtual void rdp_input_invalidate(const Rect & r) {}

    // management of module originated event ("data received from server")
    // return non zero if module is "finished", 0 if it's still running
    // the null module never finish and accept any incoming event
    virtual void draw_event(time_t now) {}

    virtual void begin_update() {}
    virtual void end_update() {}
    using mod_api::draw;
    virtual void draw(const RDPOpaqueRect      & cmd, const Rect & clip) {}
    virtual void draw(const RDPScrBlt          & cmd, const Rect & clip) {}
    virtual void draw(const RDPDestBlt         & cmd, const Rect & clip) {}
    virtual void draw(const RDPMultiDstBlt     & cmd, const Rect & clip) {}
    virtual void draw(const RDPMultiOpaqueRect & cmd, const Rect & clip) {}
    virtual void draw(const RDP::RDPMultiPatBlt & cmd, const Rect & clip) {}
    virtual void draw(const RDP::RDPMultiScrBlt & cmd, const Rect & clip) {}
    virtual void draw(const RDPPatBlt          & cmd, const Rect & clip) {}
    virtual void draw(const RDPMemBlt          & cmd, const Rect & clip, const Bitmap & bmp) {}
    virtual void draw(const RDPMem3Blt         & cmd, const Rect & clip, const Bitmap & bmp) {}
    virtual void draw(const RDPLineTo          & cmd, const Rect & clip) {}
    virtual void draw(const RDPGlyphIndex      & cmd, const Rect & clip, const GlyphCache * gly_cache) {}
    virtual void draw(const RDPPolygonSC       & cmd, const Rect & clip) {}
    virtual void draw(const RDPPolygonCB       & cmd, const Rect & clip) {}
    virtual void draw(const RDPPolyline        & cmd, const Rect & clip) {}
    virtual void draw(const RDPEllipseSC       & cmd, const Rect & clip) {}
    virtual void draw(const RDPEllipseCB       & cmd, const Rect & clip) {}

    virtual void draw(const RDP::FrameMarker & order) {}

    virtual void draw(const RDP::RAIL::NewOrExistingWindow & order) {}
    virtual void draw(const RDP::RAIL::WindowIcon          & order) {}
    virtual void draw(const RDP::RAIL::CachedIcon          & order) {}
    virtual void draw(const RDP::RAIL::DeletedWindow       & order) {}

    virtual void draw(const RDP::RAIL::ActivelyMonitoredDesktop & order) {}
    virtual void draw(const RDP::RAIL::NonMonitoredDesktop 		& order) {}

    virtual void draw(const RDPBitmapData & bitmap_data, const uint8_t * data,
        size_t size, const Bitmap & bmp) {}

    virtual void server_set_pointer(const Pointer & cursor) {}

    virtual void send_to_front_channel(const char * const mod_channel_name, uint8_t* data, size_t length, size_t chunk_size, int flags) {}
};

#endif
