/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2011
   Author(s): Christophe Grosjean, Raphael Zhou

   RDPSerializer is an implementation of RDPGraphicDevice that know how to serialize RDP Orders
   and send them on the wire or store them in a file (actual storage will be provided as a Transport class).
   Serialized RDP orders are put in a chunk and sent when flush is called (either explicit call or because
   the provided buffer is full).
*/

#ifndef _REDEMPTION_CORE_RDP_RDPSERIALIZER_HPP_
#define _REDEMPTION_CORE_RDP_RDPSERIALIZER_HPP_

// MS-RDPECGI 2.2.2.2 Fast-Path Orders Update (TS_FP_UPDATE_ORDERS)
// ================================================================
// The TS_FP_UPDATE_ORDERS structure contains primary, secondary, and alternate
// secondary drawing orders aligned on byte boundaries. This structure conforms
// to the layout of a Fast-Path Update (see [MS-RDPBCGR] section 2.2.9.1.2.1)
// and is encapsulated within a Fast-Path Update PDU (see [MS-RDPBCGR] section
// 2.2.9.1.2.1.1).

// updateHeader (1 byte): An 8-bit, unsigned integer. The format of this field
//   is the same as the updateHeader byte field described in the Fast-Path
//   Update structure (see [MS-RDPBCGR] section 2.2.9.1.2.1). The updateCode
//   bitfield (4 bits in size) MUST be set to FASTPATH_UPDATETYPE_ORDERS (0x0).

// compressionFlags (1 byte): An 8-bit, unsigned integer. The format of this
//   optional field (as well as the possible values) is the same as the
//   compressionFlags field described in the Fast-Path Update structure
//   specified in [MS-RDPBCGR] section 2.2.9.1.2.1.

// size (2 bytes): A 16-bit, unsigned integer. The format of this field (as well
//   as the possible values) is the same as the size field described in the
//   Fast-Path Update structure specified in [MS-RDPBCGR] section 2.2.9.1.2.1.

// numberOrders (2 bytes): A 16-bit, unsigned integer. The number of Drawing
//   Order (section 2.2.2.1.1) structures contained in the orderData field.

// orderData (variable): A variable-sized array of Drawing Order (section
//   2.2.2.1.1) structures packed on byte boundaries. Each structure contains a
//   primary, secondary, or alternate secondary drawing order. The controlFlags
//   field of the Drawing Order identifies the type of drawing order.


// MS-RDPECGI 2.2.2.1 Orders Update (TS_UPDATE_ORDERS_PDU_DATA)
// ============================================================
// The TS_UPDATE_ORDERS_PDU_DATA structure contains primary, secondary, and
// alternate secondary drawing orders aligned on byte boundaries. This structure
// conforms to the layout of a Slow Path Graphics Update (see [MS-RDPBCGR]
// section 2.2.9.1.1.3.1) and is encapsulated within a Graphics Update PDU (see
// [MS-RDPBCGR] section 2.2.9.1.1.3.1.1).

// shareDataHeader (18 bytes): Share Data Header (see [MS-RDPBCGR], section
//   2.2.8.1.1.1.2) containing information about the packet. The type subfield
//   of the pduType field of the Share Control Header (section 2.2.8.1.1.1.1)
//   MUST be set to PDUTYPE_DATAPDU (7). The pduType2 field of the Share Data
//   Header MUST be set to PDUTYPE2_UPDATE (2).

// updateType (2 bytes): A 16-bit, unsigned integer. The field contains the
//   graphics update type. This field MUST be set to UPDATETYPE_ORDERS (0x0000).

// pad2OctetsA (2 bytes): A 16-bit, unsigned integer used as a padding field.
//   Values in this field are arbitrary and MUST be ignored.

// numberOrders (2 bytes): A 16-bit, unsigned integer. The number of Drawing
//   Order (section 2.2.2.1.1) structures contained in the orderData field.

// pad2OctetsB (2 bytes): A 16-bit, unsigned integer used as a padding field.
//   Values in this field are arbitrary and MUST be ignored.

// orderData (variable): A variable-sized array of Drawing Order (section
//   2.2.2.1.1) structures packed on byte boundaries. Each structure contains a
//   primary, secondary, or alternate secondary drawing order. The controlFlags
//   field of the Drawing Order identifies the type of drawing order.

#include "config.hpp"

#include "RDPGraphicDevice.hpp"
#include "bitmapupdate.hpp"

#include "RDP/caches/bmpcache.hpp"
#include "RDP/caches/pointercache.hpp"
#include "caches/glyphcache.hpp"

#include "orders/RDPOrdersPrimaryDestBlt.hpp"
#include "orders/RDPOrdersPrimaryMultiDstBlt.hpp"
#include "orders/RDPOrdersPrimaryPatBlt.hpp"
#include "orders/RDPOrdersPrimaryMultiPatBlt.hpp"
#include "orders/RDPOrdersPrimaryOpaqueRect.hpp"
#include "orders/RDPOrdersPrimaryMultiOpaqueRect.hpp"
#include "orders/RDPOrdersPrimaryScrBlt.hpp"
#include "orders/RDPOrdersPrimaryMultiScrBlt.hpp"
#include "orders/RDPOrdersPrimaryMemBlt.hpp"
#include "orders/RDPOrdersPrimaryMem3Blt.hpp"
#include "orders/RDPOrdersPrimaryLineTo.hpp"
#include "orders/RDPOrdersPrimaryGlyphIndex.hpp"
#include "orders/RDPOrdersPrimaryPolygonSC.hpp"
#include "orders/RDPOrdersPrimaryPolygonCB.hpp"
#include "orders/RDPOrdersPrimaryPolyline.hpp"
#include "orders/RDPOrdersPrimaryEllipseSC.hpp"
#include "orders/RDPOrdersPrimaryEllipseCB.hpp"
#include "orders/RDPOrdersSecondaryColorCache.hpp"
#include "orders/RDPOrdersSecondaryGlyphCache.hpp"
#include "orders/RDPOrdersSecondaryBrushCache.hpp"
#include "orders/RDPOrdersSecondaryFrameMarker.hpp"
#include "orders/AlternateSecondaryWindowing.hpp"

#include "transport.hpp"

#include "finally.hpp"
#include "stream.hpp"

struct RDPSerializer : public RDPGraphicDevice
{
    // Packet more than 16384 bytes can cause MSTSC to crash.
    enum { MAX_ORDERS_SIZE = 16384,
           MAX_BITMAP_SIZE_8K = 8192,
           MAX_BITMAP_SIZE_64K = 65536
    };

    using RDPGraphicDevice::draw;

protected:
    Stream & stream_orders;
    Stream & stream_bitmaps;

private:
    uint8_t bpp;

protected:
    Transport * trans;

protected:
    const Inifile & ini;

private:
    const int bitmap_cache_version;
    const int use_bitmap_comp;
    const int op2;
    const size_t max_bitmap_size;

protected:
    // Internal state of orders
    RDPOrderCommon     common;
    RDPDestBlt         destblt;
    RDPMultiDstBlt     multidstblt;
    RDPMultiOpaqueRect multiopaquerect;
    RDP::RDPMultiPatBlt multipatblt;
    RDP::RDPMultiScrBlt multiscrblt;
    RDPPatBlt          patblt;
    RDPScrBlt          scrblt;
    RDPOpaqueRect      opaquerect;
    RDPMemBlt          memblt;
    RDPMem3Blt         mem3blt;
    RDPLineTo          lineto;
    RDPGlyphIndex      glyphindex;
    RDPPolygonSC       polygonSC;
    RDPPolygonCB       polygonCB;
    RDPPolyline        polyline;
    RDPEllipseSC       ellipseSC;
    RDPEllipseCB       ellipseCB;

    // state variables for gathering batch of orders
    size_t order_count;
    size_t bitmap_count;

    BmpCache      & bmp_cache;
    GlyphCache    & glyph_cache;
    PointerCache  & pointer_cache;

    const uint32_t verbose;

public:
    RDPSerializer( Transport * trans
                 , Stream & stream_orders
                 , Stream & stream_bitmaps
                 , const uint8_t bpp
                 , BmpCache & bmp_cache
                 , GlyphCache & glyph_cache
                 , PointerCache & pointer_cache
                 , const int bitmap_cache_version
                 , const int use_bitmap_comp
                 , const int op2
                 , size_t max_bitmap_size
                 , const Inifile & ini
                 , uint32_t verbose = 0)
    : RDPGraphicDevice()
    , stream_orders(stream_orders)
    , stream_bitmaps(stream_bitmaps)
    , bpp(bpp)
    , trans(trans)
    , ini(ini)
    , bitmap_cache_version(bitmap_cache_version)
    , use_bitmap_comp(use_bitmap_comp)
    , op2(op2)
    , max_bitmap_size(max_bitmap_size)
    // Internal state of orders
    , common(RDP::PATBLT, Rect(0, 0, 1, 1))
    , destblt(Rect(), 0)
    , multidstblt()
    , patblt(Rect(), 0, 0, 0, RDPBrush())
    , scrblt(Rect(), 0, 0, 0)
    , opaquerect(Rect(), 0)
    , memblt(0, Rect(), 0, 0, 0, 0)
    , mem3blt(0, Rect(), 0, 0, 0, 0, 0, RDPBrush(), 0)
    , lineto(0, 0, 0, 0, 0, 0, 0, RDPPen(0, 0, 0))
    , glyphindex( 0, 0, 0, 0, 0, 0
                , Rect(0, 0, 1, 1), Rect(0, 0, 1, 1), RDPBrush(), 0, 0, 0, (const uint8_t *)"")
    , polygonSC()
    , polygonCB()
    , polyline()
    , ellipseSC()
    , ellipseCB(Rect(), 0, 0, 0, 0, RDPBrush())
    // state variables for a batch of orders
    , order_count(0)
    , bitmap_count(0)
    , bmp_cache(bmp_cache)
    , glyph_cache(glyph_cache)
    , pointer_cache(pointer_cache)
    , verbose(verbose) {}

    ~RDPSerializer() {}

protected:
    virtual void flush_orders() = 0;
    virtual void flush_bitmaps() = 0;

    virtual void send_pointer(int cache_idx, const Pointer & cursor) = 0;
    virtual void set_pointer(int cache_idx) = 0;

public:
    /*****************************************************************************/
    // check if the next order will fit in available packet size
    // if not send previous orders we got and init a new packet
    void reserve_order(size_t asked_size)
    {
        //LOG(LOG_INFO, "RDPSerializer::reserve_order %u (avail=%u)", asked_size, this->stream_orders.size());
        // To support 64x64 32-bit bitmap.
        size_t max_packet_size = std::min(this->stream_orders.get_capacity(), static_cast<size_t>(MAX_ORDERS_SIZE));
        size_t used_size = this->stream_orders.get_offset();
        if (this->ini.debug.primary_orders > 3) {
            LOG( LOG_INFO
               , "<Serializer %p> RDPSerializer::reserve_order[%u](%u) used=%u free=%u"
               , this
               , this->order_count
               , asked_size, used_size
               , max_packet_size - used_size - 106
               );
        }
        if (asked_size + 106 > max_packet_size) {
            LOG( LOG_ERR
               , "(asked size (%u) + 106 = %d) > order batch capacity (%u)"
               , asked_size
               , asked_size + 106
               , max_packet_size);
            throw Error(ERR_STREAM_MEMORY_TOO_SMALL);
        }
        REDASSERT(!this->bitmap_count);
        if (this->bitmap_count) { this->flush_bitmaps(); }
        const size_t max_order_batch = 4096;
        if (   (this->order_count >= max_order_batch)
            || ((used_size + asked_size + 106) > max_packet_size)) {
            this->flush_orders();
        }
        this->order_count++;
        //LOG(LOG_INFO, "RDPSerializer::reserve_order done");
    }

    virtual void draw(const RDPOpaqueRect & cmd, const Rect & clip)
    {
        //LOG(LOG_INFO, "RDPSerializer::draw::RDPOpaqueRect");
        this->reserve_order(23);
        RDPOrderCommon newcommon(RDP::RECT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->opaquerect);
        this->common = newcommon;
        this->opaquerect = cmd;

        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
        //LOG(LOG_INFO, "RDPSerializer::draw::RDPOpaqueRect done");
    }

    virtual void draw(const RDPScrBlt & cmd, const Rect &clip)
    {
        this->reserve_order(25);
        RDPOrderCommon newcommon(RDP::SCREENBLT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->scrblt);
        this->common = newcommon;
        this->scrblt = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDPDestBlt & cmd, const Rect &clip)
    {
        this->reserve_order(21);
        RDPOrderCommon newcommon(RDP::DESTBLT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->destblt);
        this->common = newcommon;
        this->destblt = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDPMultiDstBlt & cmd, const Rect & clip) {
        this->reserve_order(395 * 2);
        RDPOrderCommon newcommon(RDP::MULTIDSTBLT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->multidstblt);
        this->common      = newcommon;
        this->multidstblt = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDPMultiOpaqueRect & cmd, const Rect & clip) {
        this->reserve_order(397 * 2);
        RDPOrderCommon newcommon(RDP::MULTIOPAQUERECT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->multiopaquerect);
        this->common          = newcommon;
        this->multiopaquerect = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDP::RDPMultiPatBlt & cmd, const Rect & clip) {
        this->reserve_order(412 * 2);
        RDPOrderCommon newcommon(RDP::MULTIPATBLT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->multipatblt);
        this->common      = newcommon;
        this->multipatblt = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDP::RDPMultiScrBlt & cmd, const Rect & clip) {
        this->reserve_order(399 * 2);
        RDPOrderCommon newcommon(RDP::MULTISCRBLT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->multiscrblt);
        this->common      = newcommon;
        this->multiscrblt = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDPPatBlt & cmd, const Rect &clip)
    {
        this->reserve_order(29);
        using namespace RDP;
        RDPOrderCommon newcommon(RDP::PATBLT, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->patblt);
        this->common = newcommon;
        this->patblt = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

protected:
    void emit_bmp_cache(uint8_t cache_id, uint16_t cache_idx, bool in_wait_list)
    {
        const Bitmap & bmp = this->bmp_cache.get(
            (in_wait_list ? BmpCache::MAXIMUM_NUMBER_OF_CACHES : cache_id), cache_idx);
        if (!bmp.is_valid()) {
            //LOG(LOG_INFO, "skipping RDPSerializer::emit_bmp_cache for %u:%u (entry not used)",
            //    cache_id, cache_idx);
            return;
        }
        RDPBmpCache cmd_cache(bmp, cache_id, cache_idx,
            this->bmp_cache.is_cache_persistent(cache_id), in_wait_list,
            this->ini.debug.secondary_orders);
        this->reserve_order(cmd_cache.bmp.bmp_size() + 16);
        cmd_cache.emit( this->bpp, this->stream_orders, this->bitmap_cache_version, this->use_bitmap_comp
                      , this->op2);

        if (this->ini.debug.secondary_orders) {
            cmd_cache.log(LOG_INFO);
        }
    }

    void emit_glyph_cache(uint8_t cacheId, uint8_t cacheIndex) {
        FontChar & fc = this->glyph_cache.glyphs[cacheId][cacheIndex].font_item;
        RDPGlyphCache cmd(
            cacheId, /*1, */cacheIndex, fc.offset, fc.baseline, fc.width, fc.height, std::move(fc.data));
        // always restored fc.data
        auto finally_ = finally([&]{ fc.data = std::move(cmd.aj); });

        this->reserve_order(cmd.total_order_size());
        cmd.emit(this->stream_orders);

        if (this->ini.debug.secondary_orders) {
            cmd.log(LOG_INFO);
        }
    }

public:
    template<class MemBlt>
    void draw_memblt(const MemBlt & cmd, MemBlt & this_memblt, const Rect & clip, const Bitmap & oldbmp)
    {
        uint32_t res          = this->bmp_cache.cache_bitmap(oldbmp);
        bool     in_wait_list = (res >> 16) & BmpCache::IN_WAIT_LIST;
        uint8_t  cache_id     = (res >> 16) & 0x7;
        uint16_t cache_idx    = res;

        using is_RDPMemBlt = std::is_same<RDPMemBlt, MemBlt>;

        if (this->verbose & 512) {
            LOG(LOG_INFO,
                is_RDPMemBlt()
                ? "RDPSerializer: draw MemBlt, cache_id=%u cache_index=%u in_wait_list=%s"
                : "RDPSerializer: draw Mem3Blt, cache_id=%u cache_index=%u in_wait_list=%s",
                cache_id, cache_idx, (in_wait_list ? "true" : "false"));
        }

        if ((res >> 24) == BmpCache::ADDED_TO_CACHE) {
            this->emit_bmp_cache(cache_id, cache_idx, in_wait_list);
        }
        else if ((this->bmp_cache.owner == BmpCache::Recorder) && !this->bmp_cache.is_cached(cache_id, cache_idx)) {
            this->emit_bmp_cache(cache_id, cache_idx, in_wait_list);
            this->bmp_cache.set_cached(cache_id, cache_idx, true);
        }

        MemBlt newcmd = cmd;
        newcmd.cache_id = cache_id;
        newcmd.cache_idx = (in_wait_list ? uint16_t(RDPBmpCache::BITMAPCACHE_WAITING_LIST_INDEX) : cache_idx);

        this->reserve_order(is_RDPMemBlt() ? 30 : 60);
        RDPOrderCommon newcommon(is_RDPMemBlt() ? RDP::MEMBLT : RDP::MEM3BLT, clip);
        newcmd.emit(this->stream_orders, newcommon, this->common, this_memblt);
        this->common = newcommon;
        this_memblt = newcmd;
        if (this->ini.debug.primary_orders) {
            newcmd.log(LOG_INFO, common.clip);
        }
    }

public:
    virtual void draw(const RDPMemBlt & cmd, const Rect & clip, const Bitmap & oldbmp)
    {
        this->draw_memblt(cmd, this->memblt, clip, oldbmp);
    }

    virtual void draw(const RDPMem3Blt & cmd, const Rect & clip, const Bitmap & oldbmp)
    {
        this->draw_memblt(cmd, this->mem3blt, clip, oldbmp);
    }

    virtual void draw(const RDPLineTo & cmd, const Rect & clip)
    {
        this->reserve_order(32);
        RDPOrderCommon newcommon(RDP::LINE, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->lineto);
        this->common = newcommon;
        this->lineto = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDPGlyphIndex & cmd, const Rect & clip,
        const GlyphCache * gly_cache) {
        REDASSERT(gly_cache);

        auto get_delta = [] (RDPGlyphIndex & cmd, uint8_t & i) -> uint16_t {
            uint16_t delta = cmd.data[i++];
            if (delta == 0x80) {
                StaticStream stream(cmd.data + i, sizeof(uint16_t));
                i += stream.get_capacity();

                delta = stream.in_uint16_le();
            }
            return delta;
        };

        RDPGlyphIndex new_cmd = cmd;
        bool has_delta_byte = (!new_cmd.ui_charinc && !(new_cmd.fl_accel & SO_CHAR_INC_EQUAL_BM_BASE));
        for (uint8_t i = 0; i < new_cmd.data_len; ) {
            if (new_cmd.data[i] <= 0xFD) {
                //LOG(LOG_INFO, "Index in the fragment cache=%u", new_cmd.data[i]);
                FontChar const & fc = gly_cache->glyphs[new_cmd.cache_id][new_cmd.data[i]].font_item;
                REDASSERT(fc);

                int cacheIndex;
                if (this->glyph_cache.add_glyph(fc, new_cmd.cache_id, cacheIndex) ==
                    GlyphCache::GLYPH_ADDED_TO_CACHE) {
                    this->emit_glyph_cache(new_cmd.cache_id, cacheIndex);
                }
                else if ((this->bmp_cache.owner == BmpCache::Recorder) &&
                         !this->glyph_cache.is_cached(new_cmd.cache_id, cacheIndex)) {
                    this->emit_glyph_cache(new_cmd.cache_id, cacheIndex);
                    this->glyph_cache.set_cached(new_cmd.cache_id, cacheIndex, true);
                }

                REDASSERT(cacheIndex >= 0);
                new_cmd.data[i++] = static_cast<uint8_t>(cacheIndex);
                if (has_delta_byte) {
                    const uint16_t delta = get_delta(new_cmd, i);

                    if (this->ini.debug.primary_orders & 0x80) {
                        LOG(LOG_INFO,
                            "RDPSerializer::draw(RDPGlyphIndex, ...): "
                                "Experimental support of "
                                "the distance between two consecutive glyphs "
                                "indicated by delta bytes in "
                                "GlyphIndex Primary Drawing Order. "
                                "delta=%u",
                            delta);
                    }
                }
            }
            else if (new_cmd.data[i] == 0xFE) {
                i++;

                const uint8_t fragment_index = new_cmd.data[i++];

                if (has_delta_byte) {
                    const uint16_t delta = get_delta(new_cmd, i);

                    if (this->ini.debug.primary_orders & 0x80) {
                        LOG(LOG_INFO,
                            "RDPSerializer::draw(RDPGlyphIndex, ...): "
                                "Experimental support of "
                                "the distance between two consecutive fragments "
                                "indicated by delta bytes in "
                                "GlyphIndex Primary Drawing Order. "
                                "delta=%u",
                            delta);
                    }
                }

                if (this->ini.debug.primary_orders & 0x80) {
                    LOG(LOG_INFO,
                        "RDPSerializer::draw(RDPGlyphIndex, ...): "
                            "Experimental support of USE (0xFE) operation byte in "
                            "GlyphIndex Primary Drawing Order. "
                            "fragment_index=%u",
                        fragment_index);
                }
            }
            else if (new_cmd.data[i] == 0xFF) {
                i++;

                const uint8_t fragment_index = new_cmd.data[i++];
                const uint8_t fragment_size  = new_cmd.data[i++];

                if (this->ini.debug.primary_orders & 0x80) {
                    LOG(LOG_INFO,
                        "RDPSerializer::draw(RDPGlyphIndex, ...): "
                            "Experimental support of ADD (0xFF) operation byte in "
                            "GlyphIndex Primary Drawing Order. "
                            "fragment_index=%u fragment_size=%u",
                        fragment_index, fragment_size);
                }

                REDASSERT(i == new_cmd.data_len);
            }
        }

        this->reserve_order(297);
        RDPOrderCommon newcommon(RDP::GLYPHINDEX, clip);
        new_cmd.emit(this->stream_orders, newcommon, this->common, this->glyphindex);
        this->common = newcommon;
        this->glyphindex = new_cmd;
        if (this->ini.debug.primary_orders) {
            new_cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDPBrushCache & cmd)
    {
        this->reserve_order(cmd.size + 12);
        cmd.emit(this->stream_orders);
    }

    virtual void draw(const RDPColCache & cmd)
    {
        this->reserve_order(2000);
        cmd.emit(this->stream_orders);
    }

    virtual void draw(const RDPPolygonSC & cmd, const Rect & clip) {
        this->reserve_order(256);
        RDPOrderCommon newcommon(RDP::POLYGONSC, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->polygonSC);
        this->common    = newcommon;
        this->polygonSC = cmd;
    }

    virtual void draw(const RDPPolygonCB & cmd, const Rect & clip) {
        this->reserve_order(256);
        RDPOrderCommon newcommon(RDP::POLYGONCB, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->polygonCB);
        this->common    = newcommon;
        this->polygonCB = cmd;
    }

    virtual void draw(const RDPPolyline & cmd, const Rect & clip) {
        this->reserve_order(256);
        RDPOrderCommon newcommon(RDP::POLYLINE, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->polyline);
        this->common   = newcommon;
        this->polyline = cmd;
        if (this->ini.debug.primary_orders) {
            cmd.log(LOG_INFO, common.clip);
        }
    }

    virtual void draw(const RDPEllipseSC & cmd, const Rect & clip)
    {
        this->reserve_order(26);
        RDPOrderCommon newcommon(RDP::ELLIPSESC, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->ellipseSC);
        this->common = newcommon;
        this->ellipseSC = cmd;
    }

    virtual void draw(const RDPEllipseCB & cmd, const Rect & clip)
    {
        this->reserve_order(54);
        RDPOrderCommon newcommon(RDP::ELLIPSECB, clip);
        cmd.emit(this->stream_orders, newcommon, this->common, this->ellipseCB);
        this->common = newcommon;
        this->ellipseCB = cmd;
    }

    virtual void draw(const RDP::FrameMarker & order) {
        this->reserve_order(5);
        order.emit(this->stream_orders);
        if (this->ini.debug.secondary_orders) {
            order.log(LOG_INFO);
        }
    }

    virtual void draw(const RDP::RAIL::NewOrExistingWindow & order) {
        this->reserve_order(order.size());
        order.emit(this->stream_orders);
        if (this->ini.debug.secondary_orders) {
            order.log(LOG_INFO);
        }
    }

    virtual void draw(const RDP::RAIL::WindowIcon & order) {
        this->reserve_order(order.size());
        order.emit(this->stream_orders);
        if (this->ini.debug.secondary_orders) {
            order.log(LOG_INFO);
        }
    }

    virtual void draw(const RDP::RAIL::CachedIcon & order) {
        this->reserve_order(order.size());
        order.emit(this->stream_orders);
        if (this->ini.debug.secondary_orders) {
            order.log(LOG_INFO);
        }
    }

    virtual void draw(const RDP::RAIL::DeletedWindow & order) {
        this->reserve_order(order.size());
        order.emit(this->stream_orders);
        if (this->ini.debug.secondary_orders) {
            order.log(LOG_INFO);
        }
    }

    virtual void draw(const RDP::RAIL::ActivelyMonitoredDesktop & order) {
        this->reserve_order(order.size());
        order.emit(this->stream_orders);
        if (this->ini.debug.secondary_orders) {
            order.log(LOG_INFO);
        }
    }

    virtual void draw(const RDP::RAIL::NonMonitoredDesktop & order) {
        this->reserve_order(order.size());
        order.emit(this->stream_orders);
        if (this->ini.debug.secondary_orders) {
            order.log(LOG_INFO);
        }
    }

    // check if the next bitmap will fit in available packet size
    // if not send previous bitmaps we got and init a new packet
    void reserve_bitmap(size_t asked_size) {
        size_t max_packet_size = std::min(this->stream_bitmaps.get_capacity(), this->max_bitmap_size + 300u);
        TODO("QuickFix, should set a max packet size according to RDP compression version of client, proxy and server");
        size_t used_size       = this->stream_bitmaps.get_offset();
        if (this->ini.debug.primary_orders > 3) {
            LOG( LOG_INFO
               , "<Serializer %p> RDPSerializer::reserve_bitmap[%u](%u) used=%u free=%u"
               , this
               , this->bitmap_count
               , asked_size
               , used_size
               , max_packet_size - used_size - 106
               );
        }
        if (asked_size + 106 > max_packet_size) {
            LOG( LOG_ERR
               , "asked size (%u) > image batch capacity (%u)"
               , asked_size + 106
               , max_packet_size
               );
            throw Error(ERR_STREAM_MEMORY_TOO_SMALL);
        }
        REDASSERT(!this->order_count);
        if (this->order_count) { this->flush_orders(); }
        const size_t max_image_batch = 4096;
        if (   (this->bitmap_count >= max_image_batch)
            || ((used_size + asked_size + 106) > max_packet_size)) {
            this->flush_bitmaps();
        }
        this->bitmap_count++;
    }

    virtual void draw( const RDPBitmapData & bitmap_data, const uint8_t * data
                     , size_t size, const Bitmap & bmp) {
        this->reserve_bitmap(bitmap_data.struct_size() + size);

        bitmap_data.emit(this->stream_bitmaps);
        this->stream_bitmaps.out_copy_bytes(data, size);
        if (this->ini.debug.bitmap_update) {
            bitmap_data.log(LOG_INFO, "RDPSerializer");
        }
    }

    virtual void server_set_pointer(const Pointer & cursor) {
        int cache_idx = 0;
        switch (this->pointer_cache.add_pointer(cursor, cache_idx)) {
        case POINTER_TO_SEND:
            this->send_pointer(cache_idx, cursor);
        break;
        default:
        case POINTER_ALLREADY_SENT:
            if ((this->bmp_cache.owner == BmpCache::Recorder) && !this->pointer_cache.is_cached(cache_idx)) {
                this->send_pointer(cache_idx, cursor);
                this->pointer_cache.set_cached(cache_idx, true);
                break;
            }

            this->set_pointer(cache_idx);
        break;
        }
    }
};

#endif
