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
   Author(s): Christophe Grosjean, Jonathan Poelen, Raphael Zhou

   RDPGraphicDevice is an abstract class that describe a device able to
   proceed RDP Drawing Orders. How the drawing will be actually done
   depends on the implementation.
   - It may be sent on the wire,
   - Used to draw on some internal bitmap,
   - etc.
*/

#ifndef _REDEMPTION_CAPTURE_GRAPHICTOFILE_HPP_
#define _REDEMPTION_CAPTURE_GRAPHICTOFILE_HPP_

#include "colors.hpp"
#include "compression_transport_wrapper.hpp"
#include "config.hpp"
#include "RDP/caches/bmpcache.hpp"
#include "RDP/RDPSerializer.hpp"
#include "RDP/share.hpp"
#include "RDP/RDPDrawable.hpp"
#include "wrm_label.hpp"

class WRMChunk_Send
{
    public:
    WRMChunk_Send(Stream & stream, uint16_t chunktype, uint16_t data_size, uint16_t count)
    {
        stream.out_uint16_le(chunktype);
        stream.out_uint32_le(8 + data_size);
        stream.out_uint16_le(count);
        stream.mark_end();
    }
};

template <size_t SZ>
class OutChunkedBufferingTransport : public Transport
{
    Transport & trans;
    size_t max;
    BStream stream;

public:
    OutChunkedBufferingTransport(Transport & trans)
        : trans(trans)
        , max(SZ-8)
        , stream(SZ)
    {
    }

    virtual void flush() {
        this->stream.mark_end();
        if (this->stream.size() > 0) {
            BStream header(8);
            WRMChunk_Send(header, LAST_IMAGE_CHUNK, this->stream.size(), 1);
            this->trans.send(header);
            this->trans.send(this->stream);
        }
    }

private:
    virtual void do_send(const char * const buffer, size_t len) {
        size_t to_buffer_len = len;
        while (this->stream.size() + to_buffer_len > max) {
            BStream header(8);
            WRMChunk_Send(header, PARTIAL_IMAGE_CHUNK, max, 1);
            this->trans.send(header);
            this->trans.send(this->stream);
            size_t to_send = max - this->stream.size();
            this->trans.send(buffer + len - to_buffer_len, to_send);
            to_buffer_len -= to_send;
            this->stream.reset();
        }
        this->stream.out_copy_bytes(buffer + len - to_buffer_len, to_buffer_len);
        REDOC("Marking end here is necessary for chunking")
        this->stream.mark_end();
    }
};

class GraphicToFile : public RDPSerializer, public RDPCaptureDevice
REDOC("To keep things easy all chunks have 8 bytes headers"
      " starting with chunk_type, chunk_size"
      " and order_count (whatever it means, depending on chunks")
{
    enum {
        GTF_SIZE_KEYBUF_REC = 1024
    };

    CompressionOutTransportWrapper compression_wrapper;
    Transport & trans_target;
    Transport & trans;
    BStream buffer_stream_orders;
    BStream buffer_stream_bitmaps;

    timeval last_sent_timer;
    timeval timer;
    const uint16_t width;
    const uint16_t height;
    const uint8_t  capture_bpp;
    uint16_t mouse_x;
    uint16_t mouse_y;
    const bool send_input;
    RDPDrawable & drawable;

    BStream keyboard_buffer_32;

    const Inifile & ini;

    const uint8_t wrm_format_version;

    //const uint32_t verbose;

public:
    enum class SendInput { NO, YES };

    GraphicToFile(const timeval & now
                , Transport * trans
                , const uint16_t width
                , const uint16_t height
                , const uint8_t  capture_bpp
                , BmpCache & bmp_cache
                , GlyphCache & gly_cache
                , PointerCache & ptr_cache
                , RDPDrawable & drawable
                , const Inifile & ini
                , SendInput send_input = SendInput::NO
                , uint32_t verbose = 0)
    : RDPSerializer( trans, this->buffer_stream_orders
                   , this->buffer_stream_bitmaps, capture_bpp, bmp_cache, gly_cache, ptr_cache,
                   0, 1, 1, 32 * 1024, ini)
    , RDPCaptureDevice()
    , compression_wrapper(*trans, ini.video.wrm_compression_algorithm)
    , trans_target(*trans)
    , trans(this->compression_wrapper.get())
    , buffer_stream_orders(65536)
    , buffer_stream_bitmaps(65536)
    , last_sent_timer()
    , timer(now)
    , width(width)
    , height(height)
    , capture_bpp(capture_bpp)
    , mouse_x(0)
    , mouse_y(0)
    , send_input(send_input == SendInput::YES)
    , drawable(drawable)
    , keyboard_buffer_32(GTF_SIZE_KEYBUF_REC * sizeof(uint32_t))
    , ini(ini)
    , wrm_format_version(this->compression_wrapper.get_index_algorithm() ? 4 : 3)
    //, verbose(verbose)
    {
        if (this->ini.video.wrm_compression_algorithm != this->compression_wrapper.get_index_algorithm()) {
            LOG( LOG_WARNING, "compression algorithm %u not fount. Compression disable."
               , this->ini.video.wrm_compression_algorithm);
        }

        last_sent_timer.tv_sec = 0;
        last_sent_timer.tv_usec = 0;
        this->order_count = 0;

        this->send_meta_chunk();
        this->send_image_chunk();
    }

    void dump_png24(Transport & trans, bool bgr) const {
        this->drawable.dump_png24(trans, bgr);
    }

    REDOC("Update timestamp but send nothing, the timestamp will be sent later with the next effective event");
    virtual void timestamp(const timeval& now)
    {
        uint64_t old_timer = this->timer.tv_sec * 1000000ULL + this->timer.tv_usec;
        uint64_t current_timer = now.tv_sec * 1000000ULL + now.tv_usec;
        if (old_timer < current_timer) {
            this->flush_orders();
            this->flush_bitmaps();
            this->timer = now;
            this->trans.timestamp(now);
        }
    }

    virtual void mouse(uint16_t mouse_x, uint16_t mouse_y)
    {
        this->mouse_x = mouse_x;
        this->mouse_y = mouse_y;
    }

    virtual bool input(const timeval & now, Stream & input_data_32) override {
        uint32_t count  = input_data_32.size() / sizeof(uint32_t);

        size_t c = min<size_t>(count, keyboard_buffer_32.tailroom() / sizeof(uint32_t));
        keyboard_buffer_32.out_copy_bytes(input_data_32.get_data(), c * sizeof(uint32_t));

        return true;
    }

    void send_meta_chunk(void)
    {
        BStream header(8);
        BStream payload(36);
        payload.out_uint16_le(this->wrm_format_version);
        payload.out_uint16_le(this->width);
        payload.out_uint16_le(this->height);
        payload.out_uint16_le(this->capture_bpp);

        const BmpCache::cache_ & c0 = this->bmp_cache.get_cache(0);
        const BmpCache::cache_ & c1 = this->bmp_cache.get_cache(1);
        const BmpCache::cache_ & c2 = this->bmp_cache.get_cache(2);

        payload.out_uint16_le(c0.entries());
        payload.out_uint16_le(c0.bmp_size());
        payload.out_uint16_le(c1.entries());
        payload.out_uint16_le(c1.bmp_size());
        payload.out_uint16_le(c2.entries());
        payload.out_uint16_le(c2.bmp_size());

        if (this->wrm_format_version > 3) {
            payload.out_uint8(this->bmp_cache.number_of_cache);
            payload.out_uint8(this->bmp_cache.use_waiting_list ? 1 : 0);

            payload.out_uint8(c0.persistent() ? 1 : 0);
            payload.out_uint8(c1.persistent() ? 1 : 0);
            payload.out_uint8(c2.persistent() ? 1 : 0);

            const BmpCache::cache_ & c3 = this->bmp_cache.get_cache(3);
            const BmpCache::cache_ & c4 = this->bmp_cache.get_cache(4);

            payload.out_uint16_le(c3.entries());
            payload.out_uint16_le(c3.bmp_size());
            payload.out_uint8(c3.persistent() ? 1 : 0);
            payload.out_uint16_le(c4.entries());
            payload.out_uint16_le(c4.bmp_size());
            payload.out_uint8(c4.persistent() ? 1 : 0);

            // Compression algorithm
            payload.out_uint8(this->compression_wrapper.get_index_algorithm());
        }

        payload.mark_end();

        WRMChunk_Send chunk(header, META_FILE, payload.size(), 1);

        this->trans_target.send(header);
        this->trans_target.send(payload);
    }

    // this one is used to store some embedded image inside WRM
    void send_image_chunk(void)
    {
        OutChunkedBufferingTransport<65536> png_trans(this->trans);
        this->drawable.dump_png24(png_trans, false);
    }

    void send_reset_chunk()
    {
        BStream header(8);
        WRMChunk_Send chunk(header, RESET_CHUNK, 0, 1);
        this->trans.send(header);
    }

    void send_timestamp_chunk(bool ignore_time_interval = false)
    {
        BStream payload(12 + GTF_SIZE_KEYBUF_REC * sizeof(uint32_t) + 1);
        payload.out_timeval_to_uint64le_usec(this->timer);
        if (this->send_input) {
            payload.out_uint16_le(this->mouse_x);
            payload.out_uint16_le(this->mouse_y);

            payload.out_uint8(ignore_time_interval ? 1 : 0);

            keyboard_buffer_32.mark_end();

/*
            for (uint32_t i = 0, c = keyboard_buffer_32.size() / sizeof(uint32_t);
                 i < c; i++) {
                LOG(LOG_INFO, "send_timestamp_chunk: '%c'(0x%X)",
                    (keyboard_buffer_32.data[i]<128)?(char)keyboard_buffer_32.data[i]:'?',
                    keyboard_buffer_32.data[i]);
            }
*/

            payload.out_copy_bytes(keyboard_buffer_32.get_data(), keyboard_buffer_32.size());
            keyboard_buffer_32.rewind();
        }
        payload.mark_end();

        BStream header(8);
        WRMChunk_Send chunk(header, TIMESTAMP, payload.size(), 1);
        this->trans.send(header);
        this->trans.send(payload);

        this->last_sent_timer = this->timer;
    }

    void send_save_state_chunk()
    {
        BStream payload(4096);
        // RDPOrderCommon common;
        payload.out_uint8(this->common.order);
        payload.out_uint16_le(this->common.clip.x);
        payload.out_uint16_le(this->common.clip.y);
        payload.out_uint16_le(this->common.clip.cx);
        payload.out_uint16_le(this->common.clip.cy);
        // RDPDestBlt destblt;
        payload.out_uint16_le(this->destblt.rect.x);
        payload.out_uint16_le(this->destblt.rect.y);
        payload.out_uint16_le(this->destblt.rect.cx);
        payload.out_uint16_le(this->destblt.rect.cy);
        payload.out_uint8(this->destblt.rop);
        // RDPPatBlt patblt;
        payload.out_uint16_le(this->patblt.rect.x);
        payload.out_uint16_le(this->patblt.rect.y);
        payload.out_uint16_le(this->patblt.rect.cx);
        payload.out_uint16_le(this->patblt.rect.cy);
        payload.out_uint8(this->patblt.rop);
        payload.out_uint32_le(this->patblt.back_color);
        payload.out_uint32_le(this->patblt.fore_color);
        payload.out_uint8(this->patblt.brush.org_x);
        payload.out_uint8(this->patblt.brush.org_y);
        payload.out_uint8(this->patblt.brush.style);
        payload.out_uint8(this->patblt.brush.hatch);
        payload.out_copy_bytes(this->patblt.brush.extra, 7);
        // RDPScrBlt scrblt;
        payload.out_uint16_le(this->scrblt.rect.x);
        payload.out_uint16_le(this->scrblt.rect.y);
        payload.out_uint16_le(this->scrblt.rect.cx);
        payload.out_uint16_le(this->scrblt.rect.cy);
        payload.out_uint8(this->scrblt.rop);
        payload.out_uint16_le(this->scrblt.srcx);
        payload.out_uint16_le(this->scrblt.srcy);
        // RDPOpaqueRect opaquerect;
        payload.out_uint16_le(this->opaquerect.rect.x);
        payload.out_uint16_le(this->opaquerect.rect.y);
        payload.out_uint16_le(this->opaquerect.rect.cx);
        payload.out_uint16_le(this->opaquerect.rect.cy);
        payload.out_uint8(this->opaquerect.color);
        payload.out_uint8(this->opaquerect.color >> 8);
        payload.out_uint8(this->opaquerect.color >> 16);
        // RDPMemBlt memblt;
        payload.out_uint16_le(this->memblt.cache_id);
        payload.out_uint16_le(this->memblt.rect.x);
        payload.out_uint16_le(this->memblt.rect.y);
        payload.out_uint16_le(this->memblt.rect.cx);
        payload.out_uint16_le(this->memblt.rect.cy);
        payload.out_uint8(this->memblt.rop);
        payload.out_uint8(this->memblt.srcx);
        payload.out_uint8(this->memblt.srcy);
        payload.out_uint16_le(this->memblt.cache_idx);
        // RDPMem3Blt memblt;
        payload.out_uint16_le (this->mem3blt.cache_id);
        payload.out_uint16_le (this->mem3blt.rect.x);
        payload.out_uint16_le (this->mem3blt.rect.y);
        payload.out_uint16_le (this->mem3blt.rect.cx);
        payload.out_uint16_le (this->mem3blt.rect.cy);
        payload.out_uint8     (this->mem3blt.rop);
        payload.out_uint8     (this->mem3blt.srcx);
        payload.out_uint8     (this->mem3blt.srcy);
        payload.out_uint32_le (this->mem3blt.back_color);
        payload.out_uint32_le (this->mem3blt.fore_color);
        payload.out_uint8     (this->mem3blt.brush.org_x);
        payload.out_uint8     (this->mem3blt.brush.org_y);
        payload.out_uint8     (this->mem3blt.brush.style);
        payload.out_uint8     (this->mem3blt.brush.hatch);
        payload.out_copy_bytes(this->mem3blt.brush.extra, 7);
        payload.out_uint16_le (this->mem3blt.cache_idx);
        // RDPLineTo lineto;
        payload.out_uint8(this->lineto.back_mode);
        payload.out_uint16_le(this->lineto.startx);
        payload.out_uint16_le(this->lineto.starty);
        payload.out_uint16_le(this->lineto.endx);
        payload.out_uint16_le(this->lineto.endy);
        payload.out_uint32_le(this->lineto.back_color);
        payload.out_uint8(this->lineto.rop2);
        payload.out_uint8(this->lineto.pen.style);
        payload.out_sint8(this->lineto.pen.width);
        payload.out_uint32_le(this->lineto.pen.color);
        // RDPGlyphIndex glyphindex;
        payload.out_uint8(this->glyphindex.cache_id);
        payload.out_sint16_le(this->glyphindex.fl_accel);
        payload.out_sint16_le(this->glyphindex.ui_charinc);
        payload.out_sint16_le(this->glyphindex.f_op_redundant);
        payload.out_uint32_le(this->glyphindex.back_color);
        payload.out_uint32_le(this->glyphindex.fore_color);
        payload.out_uint16_le(this->glyphindex.bk.x);
        payload.out_uint16_le(this->glyphindex.bk.y);
        payload.out_uint16_le(this->glyphindex.bk.cx);
        payload.out_uint16_le(this->glyphindex.bk.cy);
        payload.out_uint16_le(this->glyphindex.op.x);
        payload.out_uint16_le(this->glyphindex.op.y);
        payload.out_uint16_le(this->glyphindex.op.cx);
        payload.out_uint16_le(this->glyphindex.op.cy);
        payload.out_uint8(this->glyphindex.brush.org_x);
        payload.out_uint8(this->glyphindex.brush.org_y);
        payload.out_uint8(this->glyphindex.brush.style);
        payload.out_uint8(this->glyphindex.brush.hatch);
        payload.out_copy_bytes(this->glyphindex.brush.extra, 7);
        payload.out_sint16_le(this->glyphindex.glyph_x);
        payload.out_sint16_le(this->glyphindex.glyph_y);
        payload.out_uint8(this->glyphindex.data_len);
        memset(this->glyphindex.data
                + this->glyphindex.data_len, 0,
            sizeof(this->glyphindex.data)
                - this->glyphindex.data_len);
        payload.out_copy_bytes(this->glyphindex.data, 256);
        // RDPPolyline polyline;
        payload.out_sint16_le(this->polyline.xStart);
        payload.out_sint16_le(this->polyline.yStart);
        payload.out_uint8(this->polyline.bRop2);
        payload.out_uint16_le(this->polyline.BrushCacheEntry);
        payload.out_uint32_le(this->polyline.PenColor);
        payload.out_uint8(this->polyline.NumDeltaEntries);
        for (uint8_t i = 0; i < this->polyline.NumDeltaEntries; i++) {
            payload.out_sint16_le(this->polyline.deltaEncodedPoints[i].xDelta);
            payload.out_sint16_le(this->polyline.deltaEncodedPoints[i].yDelta);
        }
        // RDPMultiDstBlt multidstblt;
        payload.out_sint16_le(this->multidstblt.nLeftRect);
        payload.out_sint16_le(this->multidstblt.nTopRect);
        payload.out_sint16_le(this->multidstblt.nWidth);
        payload.out_sint16_le(this->multidstblt.nHeight);
        payload.out_uint8(this->multidstblt.bRop);
        payload.out_uint8(this->multidstblt.nDeltaEntries);
        for (uint8_t i = 0; i < this->multidstblt.nDeltaEntries; i++) {
            payload.out_sint16_le(this->multidstblt.deltaEncodedRectangles[i].leftDelta);
            payload.out_sint16_le(this->multidstblt.deltaEncodedRectangles[i].topDelta);
            payload.out_sint16_le(this->multidstblt.deltaEncodedRectangles[i].width);
            payload.out_sint16_le(this->multidstblt.deltaEncodedRectangles[i].height);
        }
        // RDPMultiOpaqueRect multiopaquerect;
        payload.out_sint16_le(this->multiopaquerect.nLeftRect);
        payload.out_sint16_le(this->multiopaquerect.nTopRect);
        payload.out_sint16_le(this->multiopaquerect.nWidth);
        payload.out_sint16_le(this->multiopaquerect.nHeight);
        payload.out_uint8(this->multiopaquerect._Color);
        payload.out_uint8(this->multiopaquerect._Color >> 8);
        payload.out_uint8(this->multiopaquerect._Color >> 16);
        payload.out_uint8(this->multiopaquerect.nDeltaEntries);
        for (uint8_t i = 0; i < this->multiopaquerect.nDeltaEntries; i++) {
            payload.out_sint16_le(this->multiopaquerect.deltaEncodedRectangles[i].leftDelta);
            payload.out_sint16_le(this->multiopaquerect.deltaEncodedRectangles[i].topDelta);
            payload.out_sint16_le(this->multiopaquerect.deltaEncodedRectangles[i].width);
            payload.out_sint16_le(this->multiopaquerect.deltaEncodedRectangles[i].height);
        }
        // RDPMultiPatBlt multipatblt;
        payload.out_sint16_le(this->multipatblt.nLeftRect);
        payload.out_sint16_le(this->multipatblt.nTopRect);
        payload.out_uint16_le(this->multipatblt.nWidth);
        payload.out_uint16_le(this->multipatblt.nHeight);
        payload.out_uint8(this->multipatblt.bRop);
        payload.out_uint32_le(this->multipatblt.BackColor);
        payload.out_uint32_le(this->multipatblt.ForeColor);
        payload.out_uint8(this->multipatblt.BrushOrgX);
        payload.out_uint8(this->multipatblt.BrushOrgY);
        payload.out_uint8(this->multipatblt.BrushStyle);
        payload.out_uint8(this->multipatblt.BrushHatch);
        payload.out_copy_bytes(this->multipatblt.BrushExtra, 7);
        payload.out_uint8(this->multipatblt.nDeltaEntries);
        for (uint8_t i = 0; i < this->multipatblt.nDeltaEntries; i++) {
            payload.out_sint16_le(this->multipatblt.deltaEncodedRectangles[i].leftDelta);
            payload.out_sint16_le(this->multipatblt.deltaEncodedRectangles[i].topDelta);
            payload.out_sint16_le(this->multipatblt.deltaEncodedRectangles[i].width);
            payload.out_sint16_le(this->multipatblt.deltaEncodedRectangles[i].height);
        }
        // RDPMultiScrBlt multiscrblt;
        payload.out_sint16_le(this->multiscrblt.nLeftRect);
        payload.out_sint16_le(this->multiscrblt.nTopRect);
        payload.out_uint16_le(this->multiscrblt.nWidth);
        payload.out_uint16_le(this->multiscrblt.nHeight);
        payload.out_uint8(this->multiscrblt.bRop);
        payload.out_sint16_le(this->multiscrblt.nXSrc);
        payload.out_sint16_le(this->multiscrblt.nYSrc);
        payload.out_uint8(this->multiscrblt.nDeltaEntries);
        for (uint8_t i = 0; i < this->multiscrblt.nDeltaEntries; i++) {
            payload.out_sint16_le(this->multiscrblt.deltaEncodedRectangles[i].leftDelta);
            payload.out_sint16_le(this->multiscrblt.deltaEncodedRectangles[i].topDelta);
            payload.out_sint16_le(this->multiscrblt.deltaEncodedRectangles[i].width);
            payload.out_sint16_le(this->multiscrblt.deltaEncodedRectangles[i].height);
        }

        //------------------------------ missing variable length ---------------
        payload.mark_end();

        BStream header(8);
        WRMChunk_Send chunk(header, SAVE_STATE, payload.size(), 1);
        this->trans.send(header);
        this->trans.send(payload);
    }

    void save_bmp_caches()
    {
        for (uint8_t cache_id = 0
        ; cache_id < this->bmp_cache.number_of_cache
        ; ++cache_id) {
            const size_t entries = this->bmp_cache.get_cache(cache_id).entries();
            for (size_t i = 0; i < entries; i++) {
                this->bmp_cache.set_cached(cache_id, i, false);
            }
        }
    }

    void save_glyph_caches()
    {
        for (uint8_t cacheId = 0; cacheId < NUMBER_OF_GLYPH_CACHES; ++cacheId) {
            for (uint8_t cacheIndex = 0; cacheIndex < NUMBER_OF_GLYPH_CACHE_ENTRIES; ++cacheIndex) {
                this->glyph_cache.set_cached(cacheId, cacheIndex, false);
            }
        }
    }

    void save_ptr_cache() {
        for (int index = 0; index < MAX_POINTER_COUNT; ++index) {
            this->pointer_cache.set_cached(index, false);
        }
    }

    void send_caches_chunk()
    {
        this->save_bmp_caches();
        this->save_glyph_caches();
        this->save_ptr_cache();
        if (this->order_count > 0) {
            this->send_orders_chunk();
        }
    }

    void breakpoint()
    {
        this->flush_orders();
        this->flush_bitmaps();
        this->send_timestamp_chunk();
        if (this->compression_wrapper.get_index_algorithm()) {
            this->send_reset_chunk();
        }
        this->trans.next();
        this->send_meta_chunk();
        this->send_timestamp_chunk();
        this->send_save_state_chunk();

        OutChunkedBufferingTransport<65536> png_trans(this->trans);

        this->drawable.dump_png24(png_trans, true);

        this->send_caches_chunk();
    }

protected:
    virtual void flush_orders()
    {
        if (this->order_count > 0) {
            if (this->timer.tv_sec - this->last_sent_timer.tv_sec > 0) {
                this->send_timestamp_chunk();
            }
            this->send_orders_chunk();
        }
    }

public:
    void send_orders_chunk()
    {
        this->stream_orders.mark_end();
        BStream header(8);
        WRMChunk_Send chunk(header, RDP_UPDATE_ORDERS, this->stream_orders.size(), this->order_count);
        this->trans.send(header);
        this->trans.send(this->stream_orders);
        this->order_count = 0;
        this->stream_orders.reset();
    }

    virtual void draw(const RDPOpaqueRect & cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPScrBlt & cmd, const Rect &clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPDestBlt & cmd, const Rect &clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPMultiDstBlt & cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPMultiOpaqueRect & cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDP::RDPMultiPatBlt & cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDP::RDPMultiScrBlt & cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPPatBlt & cmd, const Rect &clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPMemBlt & cmd, const Rect & clip, const Bitmap & bmp)
    {
        this->drawable.draw(cmd, clip, bmp);
        this->RDPSerializer::draw(cmd, clip, bmp);
    }

    virtual void draw(const RDPMem3Blt & cmd, const Rect & clip, const Bitmap & bmp)
    {
        this->drawable.draw(cmd, clip, bmp);
        this->RDPSerializer::draw(cmd, clip, bmp);
    }

    virtual void draw(const RDPLineTo& cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPGlyphIndex & cmd, const Rect & clip, const GlyphCache * gly_cache)
    {
        this->drawable.draw(cmd, clip, gly_cache);
        this->RDPSerializer::draw(cmd, clip, gly_cache);
    }

    virtual void draw(const RDPPolygonSC& cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPPolygonCB& cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPPolyline& cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPEllipseSC & cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDPEllipseCB & cmd, const Rect & clip)
    {
        this->drawable.draw(cmd, clip);
        this->RDPSerializer::draw(cmd, clip);
    }

    virtual void draw(const RDP::RAIL::NewOrExistingWindow & order) {}
    virtual void draw(const RDP::RAIL::WindowIcon          & order) {}
    virtual void draw(const RDP::RAIL::CachedIcon          & order) {}
    virtual void draw(const RDP::RAIL::DeletedWindow       & order) {}

    virtual void draw(const RDP::RAIL::ActivelyMonitoredDesktop	& order) {}
    virtual void draw(const RDP::RAIL::NonMonitoredDesktop	    & order) {}

protected:
    virtual void flush_bitmaps() {
        if (this->bitmap_count > 0) {
            if (this->timer.tv_sec - this->last_sent_timer.tv_sec > 0) {
                this->send_timestamp_chunk();
            }
            this->send_bitmaps_chunk();
        }
    }

public:
    virtual void flush() {
        this->flush_bitmaps();
        this->flush_orders();
    }

    virtual void draw(const RDPBitmapData & bitmap_data, const uint8_t * data, size_t size, const Bitmap & bmp) {
        this->drawable.draw(bitmap_data, data, size, bmp);
        this->RDPSerializer::draw(bitmap_data, data, size, bmp);
    }

    virtual void draw(const RDP::FrameMarker & order) {
        this->drawable.draw(order);
        this->RDPSerializer::draw(order);
    }

    using RDPSerializer::draw;

    void send_bitmaps_chunk()
    {
        this->stream_bitmaps.mark_end();
        BStream header(8);
        WRMChunk_Send chunk(header, RDP_UPDATE_BITMAP, this->stream_bitmaps.size(), this->bitmap_count);
        this->trans.send(header);
        this->trans.send(this->stream_bitmaps);
        this->bitmap_count = 0;
        this->stream_bitmaps.reset();
    }

    virtual void server_set_pointer(const Pointer & cursor) {
        this->drawable.server_set_pointer(cursor);
        this->RDPSerializer::server_set_pointer(cursor);
    }

protected:
    virtual void send_pointer(int cache_idx, const Pointer & cursor) {
        BStream header(8);
        size_t size =   2           // mouse x
                      + 2           // mouse y
                      + 1           // cache index
                      + 1           // hotspot x
                      + 1           // hotspot y
                      + 32 * 32 * 3 // data
                      + 128         // mask
                      ;
        WRMChunk_Send chunk(header, POINTER, size, 0);
        this->trans.send(header);

        BStream payload(16);
        payload.out_uint16_le(this->mouse_x);
        payload.out_uint16_le(this->mouse_y);
        payload.out_uint8(cache_idx);
        payload.out_uint8(cursor.x);
        payload.out_uint8(cursor.y);
        payload.mark_end();
        this->trans.send(payload);

        this->trans.send(cursor.data, cursor.data_size());
        this->trans.send(cursor.mask, cursor.mask_size());
    }

    virtual void set_pointer(int cache_idx) {
        BStream header(8);
        size_t size =   2                   // mouse x
                      + 2                   // mouse y
                      + 1                   // cache index
                      ;
        WRMChunk_Send chunk(header, POINTER, size, 0);
        this->trans.send(header);

        BStream payload(16);
        payload.out_uint16_le(this->mouse_x);
        payload.out_uint16_le(this->mouse_y);
        payload.out_uint8(cache_idx);
        payload.mark_end();
        this->trans.send(payload);
    }

public:
    virtual void session_update(const timeval & now, const char * message) override {
        uint16_t message_length = ::strlen(message) + 1;    // Null-terminator is included.

        BStream payload(16);
        payload.out_timeval_to_uint64le_usec(now);
        payload.out_uint16_le(message_length);

        payload.mark_end();

        BStream header(8);
        WRMChunk_Send chunk(header, SESSION_UPDATE, payload.size() + message_length, 1);
        this->trans.send(header);
        this->trans.send(payload);
        this->trans.send(message, message_length);

        this->last_sent_timer = this->timer;
    }
};  // struct GraphicToFile

#endif
