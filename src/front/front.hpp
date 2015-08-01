/*
    This program is free software; you can redistribute it and/or modify it
     under the terms of the GNU General Public License as published by the
     Free Software Foundation; either version 2 of the License, or (at your
     option) any later version.

    This program is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
     Public License for more details.

    You should have received a copy of the GNU General Public License along
     with this program; if not, write to the Free Software Foundation, Inc.,
     675 Mass Ave, Cambridge, MA 02139, USA.

    Product name: redemption, a FLOSS RDP proxy
    Copyright (C) Wallix 2013
    Author(s): Christophe Grosjean, Javier Caverni, Xavier Dunat,
               Dominique Lafages, Raphael Zhou, Meng Tan
    Based on xrdp Copyright (C) Jay Sorg 2004-2010

    Front object (server), used to communicate with RDP client
*/

#ifndef _REDEMPTION_FRONT_FRONT_HPP_
#define _REDEMPTION_FRONT_FRONT_HPP_

#include "log.hpp"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "openssl_tls.hpp"
#include "stream.hpp"
#include "transport.hpp"
#include "RDP/x224.hpp"
#include "RDP/nego.hpp"
#include "RDP/mcs.hpp"
#include "RDP/lic.hpp"
#include "channel_list.hpp"
#include "RDP/gcc.hpp"
#include "RDP/sec.hpp"
#include "colors.hpp"
#include "RDP/fastpath.hpp"
#include "RDP/slowpath.hpp"

#include "ssl_calls.hpp"
#include "bitfu.hpp"
#include "rect.hpp"
#include "region.hpp"
#include "capture.hpp"
#include "font.hpp"
#include "bitmap.hpp"
#include "RDP/caches/bmpcache.hpp"
#include "RDP/caches/bmpcachepersister.hpp"
#include "RDP/caches/glyphcache.hpp"
#include "RDP/caches/pointercache.hpp"
#include "RDP/caches/brushcache.hpp"
#include "channel_names.hpp"
#include "client_info.hpp"
#include "config.hpp"
#include "error.hpp"
#include "callback.hpp"
#include "colors.hpp"
#include "bitfu.hpp"
#include "confdescriptor.hpp"
#include "in_file_transport.hpp"
#include "out_file_transport.hpp"

#include "RDP/GraphicUpdatePDU.hpp"
#include "RDP/SaveSessionInfoPDU.hpp"
#include "RDP/PersistentKeyListPDU.hpp"
#include "RDP/remote_programs.hpp"

#include "RDP/compress_and_draw_bitmap_update.hpp"

#include "RDP/capabilities/bmpcache.hpp"
#include "RDP/capabilities/offscreencache.hpp"
#include "RDP/capabilities/bmpcache2.hpp"
#include "RDP/capabilities/bitmapcachehostsupport.hpp"
#include "RDP/capabilities/colcache.hpp"
#include "RDP/capabilities/pointer.hpp"
#include "RDP/capabilities/cap_share.hpp"
#include "RDP/capabilities/cap_brushcache.hpp"
#include "RDP/capabilities/input.hpp"
#include "RDP/capabilities/multifragmentupdate.hpp"
#include "RDP/capabilities/compdesk.hpp"
#include "RDP/capabilities/cap_font.hpp"
#include "RDP/capabilities/glyphcache.hpp"
#include "RDP/capabilities/rail.hpp"
#include "RDP/capabilities/window.hpp"

#include "front_api.hpp"
#include "activity_checker.hpp"
#include "genrandom.hpp"

#include "auth_api.hpp"

#include "keymap2.hpp"

#include "RDP/mppc_40.hpp"
#include "RDP/mppc_50.hpp"
#include "RDP/mppc_60.hpp"
#include "RDP/mppc_61.hpp"

#include <memory>


class Front : public FrontAPI, public ActivityChecker{
    using FrontAPI::draw;

    bool has_activity = true;

public:
    enum CaptureState {
          CAPTURE_STATE_UNKNOWN
        , CAPTURE_STATE_STARTED
        , CAPTURE_STATE_PAUSED
        , CAPTURE_STATE_STOPED
    } capture_state;
    Capture * capture;

private:
    struct Graphics
    {
        BmpCache bmp_cache;
        BmpCachePersister * bmp_cache_persister;
        BrushCache brush_cache;
        PointerCache pointer_cache;
        GlyphCache glyph_cache;
        GraphicsUpdatePDU graphics_update_pdu;

        Graphics(
            ClientInfo const & client_info
          , Transport * trans
          , uint16_t & userid
          , int & shareid
          , int & encryptionLevel
          , CryptContext & encrypt
          , const Inifile & ini
          , size_t max_bitmap_size
          , bool fastpath_support
          , rdp_mppc_enc * mppc_enc
          , uint32_t verbose
        )
        : bmp_cache(
            BmpCache::Front
          , client_info.bpp
          , client_info.number_of_cache
          , ((client_info.cache_flags & ALLOW_CACHE_WAITING_LIST_FLAG) && ini.client.cache_waiting_list),
            BmpCache::CacheOption(
                client_info.cache1_entries
              , client_info.cache1_size
              , client_info.cache1_persistent),
            BmpCache::CacheOption(
                client_info.cache2_entries
              , client_info.cache2_size
              , client_info.cache2_persistent),
            BmpCache::CacheOption(
                client_info.cache3_entries
              , client_info.cache3_size
              , client_info.cache3_persistent),
            BmpCache::CacheOption(
                client_info.cache4_entries
              , client_info.cache4_size
              , client_info.cache4_persistent),
            BmpCache::CacheOption(
                client_info.cache5_entries
              , client_info.cache5_size
              , client_info.cache5_persistent),
            ini.debug.cache
          )
        , bmp_cache_persister([&ini, verbose, this]() {
            BmpCachePersister * bmp_cache_persister = nullptr;

            if (ini.client.persistent_disk_bitmap_cache &&
                ini.client.persist_bitmap_cache_on_disk &&
                bmp_cache.has_cache_persistent()) {
                // Generates the name of file.
                char cache_filename[2048];
                ::snprintf(cache_filename, sizeof(cache_filename) - 1, "%s/PDBC-%s-%d",
                    PERSISTENT_PATH "/client", ini.globals.host.get_cstr(), this->bmp_cache.bpp);
                cache_filename[sizeof(cache_filename) - 1] = '\0';

                int fd = ::open(cache_filename, O_RDONLY);
                if (fd != -1) {
                    try {
                        InFileTransport ift(fd);

                        bmp_cache_persister = new BmpCachePersister(
                            this->bmp_cache, ift, cache_filename, verbose
                        );
                    }
                    catch (const Error & e) {
                        ::close(fd);
                        if (e.id != ERR_PDBC_LOAD) {
                            throw;
                        }
                    }
                }
            }

            return bmp_cache_persister;
        }())
        , pointer_cache(client_info.pointer_cache_entries)
        , glyph_cache(client_info.number_of_entries_in_glyph_cache)
        , graphics_update_pdu(
            trans
          , userid
          , shareid
          , encryptionLevel
          , encrypt
          , ini
          , client_info.bpp
          , this->bmp_cache
          , this->glyph_cache
          , this->pointer_cache
          , client_info.bitmap_cache_version
          , ini.client.bitmap_compression
          , client_info.use_compact_packets
          , max_bitmap_size
          , fastpath_support
          , mppc_enc
          , ini.client.rdp_compression ? client_info.rdp_compression : 0
          , verbose
          )
        {}

        ~Graphics() = default;
    };

    struct GraphicsPointer
    {
        Graphics * p = nullptr;
        bool is_initialized = false;

        ~GraphicsPointer() {
            if (this->is_initialized) {
                this->p->~Graphics();
            }
            ::operator delete(this->p);
        }

        void initialize(
            ClientInfo const & client_info
          , Transport * trans
          , uint16_t & userid
          , int & shareid
          , int & encryptionLevel
          , CryptContext & encrypt
          , const Inifile & ini
          , size_t max_bitmap_size
          , bool fastpath_support
          , rdp_mppc_enc * mppc_enc
          , uint32_t verbose
        ) {
            if (this->p) {
                this->is_initialized = false;
                this->p->~Graphics();
            }
            else {
                this->p = static_cast<decltype(this->p)>(::operator new(sizeof(decltype(*this->p))));
            }

            new (this->p) Graphics(
                client_info, trans, userid, shareid, encryptionLevel, encrypt,
                ini, max_bitmap_size, fastpath_support, mppc_enc, verbose
            );
            this->is_initialized = true;
        }

        void clear_bmp_cache_persister() {
            delete this->p->bmp_cache_persister;
            this->p->bmp_cache_persister = nullptr;
        }

        bool has_bmp_cache_persister() const {
            return this->p && this->p->bmp_cache_persister;
        }

        BmpCachePersister * bmp_cache_persister() const
        { return this->p->bmp_cache_persister; }

        uint8_t bpp() const { return this->p->bmp_cache.bpp; }

        int add_brush(uint8_t* brush_item_data, int& cache_idx)
        { return this->p->brush_cache.add_brush(brush_item_data, cache_idx); }

        brush_item const & brush_at(int cache_idx) const
        { return this->p->brush_cache.brush_items[cache_idx]; }

        GraphicsUpdatePDU & graphics_update_pdu()
        { return this->p->graphics_update_pdu; }

        GraphicsUpdatePDU * operator->() const { return &this->p->graphics_update_pdu; }
    } orders;

public:
    Keymap2 keymap;

private:
    CHANNELS::ChannelDefArray channel_list;

public:
    int up_and_running;

private:
    int share_id;
    int encryptionLevel; /* 1, 2, 3 = low, medium, high */

public:
    ClientInfo client_info;

private:
    Transport & trans;

    uint16_t userid;
    uint8_t pub_mod[512];
    uint8_t pri_exp[512];
    uint8_t server_random[32];
    CryptContext encrypt, decrypt;

    int order_level;

private:
    Inifile & ini;
    uint32_t verbose;

    Font font;

    bool palette_sent;
    bool palette_memblt_sent[6];

    BGRPalette palette332_rgb = BGRPalette::classic_332_rgb();

public:
    BGRPalette mod_palette_rgb {BGRPalette::no_init()};
    uint8_t mod_bpp;

private:
    uint8_t capture_bpp;

    enum {
        CONNECTION_INITIATION,
        WAITING_FOR_LOGON_INFO,
        WAITING_FOR_ANSWER_TO_LICENCE,
        ACTIVATE_AND_PROCESS_DATA
    } state;

    Random & gen;

    bool fastpath_support;                    // choice of programmer
    bool client_fastpath_input_event_support; // = choice of programmer
    bool server_fastpath_update_support;      // choice of programmer + capability of client
    bool tls_client_active;
    bool mem3blt_support;
    int clientRequestedProtocols;

    GeneralCaps        client_general_caps;
    BitmapCaps         client_bitmap_caps;
    OrderCaps          client_order_caps;
    BmpCacheCaps       client_bmpcache_caps;
    OffScreenCacheCaps client_offscreencache_caps;
    BmpCache2Caps      client_bmpcache2_caps;
    GlyphCacheCaps     client_glyphcache_caps;
    bool               use_bitmapcache_rev2;

    std::string server_capabilities_filename;

    Transport * persistent_key_list_transport;

    rdp_mppc_enc * mppc_enc;

    auth_api * authentifier;
    bool       auth_info_sent;

    uint16_t rail_channel_id = 0;

    size_t max_bitmap_size = 1024 * 64;

    bool focus_on_password_textbox = false;

    bool input_event_and_graphics_update_disabled = false;

public:
    Front ( Transport & trans
          , const char * default_font_name // SHARE_PATH "/" DEFAULT_FONT_NAME
          , Random & gen
          , Inifile & ini
          , bool fp_support // If true, fast-path must be supported
          , bool mem3blt_support
          , const char * server_capabilities_filename = ""
          , Transport * persistent_key_list_transport = nullptr
          )
    : FrontAPI(ini.globals.notimestamp, ini.globals.nomouse)
    , capture_state(CAPTURE_STATE_UNKNOWN)
    , capture(nullptr)
    , up_and_running(0)
    , share_id(65538)
    , encryptionLevel(ini.globals.encryptionLevel + 1)
    , trans(trans)
    , userid(0)
    , order_level(0)
    , ini(ini)
    , verbose(this->ini.debug.front)
    , font(default_font_name)
    , mod_bpp(0)
    , capture_bpp(0)
    , state(CONNECTION_INITIATION)
    , gen(gen)
    , fastpath_support(fp_support)
    , client_fastpath_input_event_support(fp_support)
    , server_fastpath_update_support(false)
    , tls_client_active(true)
    , mem3blt_support(mem3blt_support)
    , clientRequestedProtocols(X224::PROTOCOL_RDP)
    , use_bitmapcache_rev2(false)
    , server_capabilities_filename(server_capabilities_filename)
    , persistent_key_list_transport(persistent_key_list_transport)
    , mppc_enc(nullptr)
    , authentifier(nullptr)
    , auth_info_sent(false) {
        // init TLS
        // --------------------------------------------------------


        // -------- Start of system wide SSL_Ctx option ------------------------------

        // ERR_load_crypto_strings() registers the error strings for all libcrypto
        // functions. SSL_load_error_strings() does the same, but also registers the
        // libssl error strings.

        // One of these functions should be called before generating textual error
        // messages. However, this is not required when memory usage is an issue.

        // ERR_free_strings() frees all previously loaded error strings.

        SSL_load_error_strings();

        // SSL_library_init() registers the available SSL/TLS ciphers and digests.
        // OpenSSL_add_ssl_algorithms() and SSLeay_add_ssl_algorithms() are synonyms
        // for SSL_library_init().

        // - SSL_library_init() must be called before any other action takes place.
        // - SSL_library_init() is not reentrant.
        // - SSL_library_init() always returns "1", so it is safe to discard the return
        // value.

        // Note: OpenSSL 0.9.8o and 1.0.0a and later added SHA2 algorithms to
        // SSL_library_init(). Applications which need to use SHA2 in earlier versions
        // of OpenSSL should call OpenSSL_add_all_algorithms() as well.

        SSL_library_init();

        // --------------------------------------------------------

        this->palette_sent = false;
        for (size_t i = 0; i < 6 ; i++) {
            this->palette_memblt_sent[i] = false;
        }

        // from server_sec
        // CGR: see if init has influence for the 3 following fields
        memset(this->server_random, 0, 32);

        // shared
        memset(this->decrypt.key, 0, 16);
        memset(this->encrypt.key, 0, 16);
        memset(this->decrypt.update_key, 0, 16);
        memset(this->encrypt.update_key, 0, 16);

        switch (this->encryptionLevel) {
        case 1:
        case 2:
            this->decrypt.encryptionMethod = 1; /* 40 bits */
            this->encrypt.encryptionMethod = 1; /* 40 bits */
        break;
        default:
        case 3:
            this->decrypt.encryptionMethod = 2; /* 128 bits */
            this->encrypt.encryptionMethod = 2; /* 128 bits */
        break;
        }
    }

    ~Front() {
        ERR_free_strings();
        delete this->mppc_enc;

        if (this->orders.has_bmp_cache_persister()) {
            this->save_persistent_disk_bitmap_cache();
        }

        delete this->capture;
    }

    uint64_t get_total_received() const
    {
        return this->trans.get_total_received();
    }

    uint64_t get_total_sent() const
    {
        return this->trans.get_total_sent();
    }

    int server_resize(int width, int height, int bpp)
    {
        uint32_t res = 0;

        this->mod_bpp = bpp;
        if (bpp == 8) {
            this->palette_sent = false;
            for (size_t i = 0; i < 6 ; i++) {
                this->palette_memblt_sent[i] = false;
            }
        }

        if (this->client_info.width != width
        || this->client_info.height != height) {
            /* older client can't resize */
            if (client_info.build <= 419) {
                LOG(LOG_WARNING, "Resizing is not available on older RDP clients");
                // resizing needed but not available
                res = -1;
            }
            else {
                LOG(LOG_INFO, "Resizing client to : %d x %d x %d", width, height, this->client_info.bpp);

                if (this->capture)
                {
                    CaptureState original_capture_state = this->capture_state;

                    auth_api * authentifier = this->authentifier;
                    this->stop_capture();
                    this->start_capture(width, height, this->ini, authentifier);

                    this->capture_state = original_capture_state;
                }

                this->client_info.width = width;
                this->client_info.height = height;

                TODO("Why are we not calling this->flush() instead ? Looks dubious.")
                // send buffered orders
                this->orders->flush();

                // clear all pending orders, caches data, and so on and
                // start a send_deactive, send_deman_active process with
                // the new resolution setting
                /* shut down the rdp client */
                this->up_and_running = 0;
                this->send_deactive();
                /* this should do the actual resizing */
                this->send_demand_active();

                LOG(LOG_INFO, "Front::server_resize::ACTIVATED (resize)");
                state = ACTIVATE_AND_PROCESS_DATA;
                res = 1;
            }
        }

        // resizing not necessary
        return res;
    }

    void server_set_pointer(const Pointer & cursor)
    {
        this->orders->server_set_pointer(cursor);
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->server_set_pointer(cursor);
        }
    }

    virtual void update_pointer_position(uint16_t xPos, uint16_t yPos) override
    {
        this->orders->update_pointer_position(xPos, yPos);
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->update_pointer_position(xPos, yPos);
        }
    }

    virtual void text_metrics(Font const & font, const char * text, int & width, int & height)
    {
        REDASSERT(false);
    }

    virtual void server_draw_text(Font const & font, int16_t x, int16_t y, const char * text, uint32_t fgcolor,
                                  uint32_t bgcolor, const Rect & clip)
    {
        REDASSERT(false);
    }

    // ===========================================================================
    void start_capture(int width, int height, Inifile & ini, auth_api * authentifier)
    {
        // Recording or pattern dectection is enabled.
        if (!ini.globals.movie.get() && ini.context.pattern_kill.is_empty() && ini.context.pattern_notify.is_empty()) {
            return;
        }

        if (this->capture) {
            LOG(LOG_INFO, "Front::start_capture: session capture is already started");

            return;
        }

        if (!ini.globals.movie.get()) {
            ini.video.capture_flags = 8;
            ini.video.capture_wrm   = false;
            ini.video.capture_flv   = false;
            ini.video.capture_ocr   = true;
            ini.video.capture_png   = false;
            ini.video.png_limit     = 0;
        }

        LOG(LOG_INFO, "---<>  Front::start_capture  <>---");
        struct timeval now = tvtime();

        if (this->verbose & 1) {
            LOG(LOG_INFO, "movie_path    = %s\n", ini.globals.movie_path.get_cstr());
            LOG(LOG_INFO, "codec_id      = %s\n", ini.globals.codec_id.get_cstr());
            LOG(LOG_INFO, "video_quality = %s\n", ini.globals.video_quality.get_cstr());
            LOG(LOG_INFO, "auth_user     = %s\n", ini.globals.auth_user.get_cstr());
            LOG(LOG_INFO, "host          = %s\n", ini.globals.host.get_cstr());
            LOG(LOG_INFO, "target_device = %s\n", ini.globals.target_device.get().c_str());
            LOG(LOG_INFO, "target_user   = %s\n", ini.globals.target_user.get_cstr());
        }

        char path[1024];
        char basename[1024];
        char extension[128];
        strcpy(path, WRM_PATH "/");     // default value, actual one should come from movie_path
        strcpy(basename, "redemption"); // default value actual one should come from movie_path
        strcpy(extension, "");          // extension is currently ignored
        const bool res = canonical_path(ini.globals.movie_path.get_cstr(), path,
                                        sizeof(path), basename, sizeof(basename), extension,
                                        sizeof(extension));
        if (!res) {
            LOG(LOG_ERR, "Buffer Overflowed: Path too long");
            throw Error(ERR_RECORDER_FAILED_TO_FOUND_PATH);
        }
        this->capture_bpp = ((ini.video.wrm_color_depth_selection_strategy == 1) ? 16 : 24);
        this->capture = new Capture( now, width, height, this->capture_bpp, this->capture_bpp
                                   , ini.video.record_path
                                   , ini.video.record_tmp_path
                                   , ini.video.hash_path, basename
                                   , true
                                   , false
                                   , authentifier
                                   , ini
                                   );
        if (this->nomouse) {
            this->capture->set_pointer_display();
        }
        this->capture->capture_event.set();
        this->capture_state = CAPTURE_STATE_STARTED;

        this->authentifier = authentifier;
    }

    void pause_capture() {
        LOG(LOG_INFO, "---<>  Front::pause_capture  <>---");
        if (this->capture_state != CAPTURE_STATE_STARTED) {
            return;
        }

        this->capture->pause();
        this->capture->capture_event.reset();
        this->capture_state = CAPTURE_STATE_PAUSED;
    }

    void resume_capture() {
        LOG(LOG_INFO, "---<>  Front::resume_capture <>---");
        if (this->capture_state != CAPTURE_STATE_PAUSED) {
            return;
        }

        this->capture->resume();
        this->capture->capture_event.set();
        this->capture_state = CAPTURE_STATE_STARTED;
    }

    void stop_capture()
    {
        if (this->capture) {
            LOG(LOG_INFO, "---<>   Front::stop_capture  <>---");
            this->authentifier = nullptr;
            delete this->capture;
            this->capture = nullptr;

            this->capture_state = CAPTURE_STATE_STOPED;
        }
    }

    void update_config(Inifile & ini) {
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->update_config(ini);
        }
    }

    void periodic_snapshot()
    {
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            struct timeval now = tvtime();
            bool requested_to_stop = false;
            this->capture->snapshot( now, this->mouse_x, this->mouse_y
                                   , false  // ignore frame in time interval
                                   , requested_to_stop
                                   );
        }
    }
    // ===========================================================================

    static int get_appropriate_compression_type(int client_supported_type, int front_supported_type)
    {
        if (((client_supported_type < PACKET_COMPR_TYPE_8K) || (client_supported_type > PACKET_COMPR_TYPE_RDP61)) ||
            ((front_supported_type  < PACKET_COMPR_TYPE_8K) || (front_supported_type  > PACKET_COMPR_TYPE_RDP61)))
            return -1;

        static int compress_type_selector[4][4] = {
            { PACKET_COMPR_TYPE_8K, PACKET_COMPR_TYPE_8K,  PACKET_COMPR_TYPE_8K,   PACKET_COMPR_TYPE_8K    },
            { PACKET_COMPR_TYPE_8K, PACKET_COMPR_TYPE_64K, PACKET_COMPR_TYPE_64K,  PACKET_COMPR_TYPE_64K   },
            { PACKET_COMPR_TYPE_8K, PACKET_COMPR_TYPE_64K, PACKET_COMPR_TYPE_RDP6, PACKET_COMPR_TYPE_RDP6  },
            { PACKET_COMPR_TYPE_8K, PACKET_COMPR_TYPE_64K, PACKET_COMPR_TYPE_RDP6, PACKET_COMPR_TYPE_RDP61 }
        };

        return compress_type_selector[client_supported_type][front_supported_type];
    }

    void save_persistent_disk_bitmap_cache() const {
        if (!this->ini.client.persistent_disk_bitmap_cache || !this->ini.client.persist_bitmap_cache_on_disk)
            return;

        const char * persistent_path = PERSISTENT_PATH "/client";

        // Ensures that the directory exists.
        if (::recursive_create_directory(persistent_path, S_IRWXU | S_IRWXG, 0) != 0) {
            LOG( LOG_ERR
               , "front::save_persistent_disk_bitmap_cache: failed to create directory \"%s\"."
               , persistent_path);
            throw Error(ERR_BITMAP_CACHE_PERSISTENT, 0);
        }

        // Generates the name of file.
        char filename[2048];
        ::snprintf(filename, sizeof(filename) - 1, "%s/PDBC-%s-%d",
            persistent_path, this->ini.globals.host.get_cstr(), this->orders.bpp());
        filename[sizeof(filename) - 1] = '\0';

        char filename_temporary[2048];
        ::snprintf(filename_temporary, sizeof(filename_temporary) - 1, "%s/PDBC-%s-%d-XXXXXX.tmp",
            persistent_path, this->ini.globals.host.get_cstr(), this->orders.bpp());
        filename_temporary[sizeof(filename_temporary) - 1] = '\0';

        int fd = ::mkostemps(filename_temporary, 4, O_CREAT | O_WRONLY);
        if (fd == -1) {
            LOG( LOG_ERR
               , "front::save_persistent_disk_bitmap_cache: "
                 "failed to open (temporary) file for writing. filename=\"%s\""
               , filename_temporary);
            throw Error(ERR_PDBC_SAVE);
        }

        try {
            OutFileTransport oft(fd);

            BmpCachePersister::save_all_to_disk(this->orders.p->bmp_cache, oft, this->verbose);

            ::close(fd);

            if (::rename(filename_temporary, filename) == -1) {
                LOG( LOG_WARNING
                   , "front::save_persistent_disk_bitmap_cache: failed to rename the (temporary) file. "
                     "old_filename=\"%s\" new_filename=\"%s\""
                   , filename_temporary, filename);
                ::unlink(filename_temporary);
            }
        }
        catch (...) {
            ::close(fd);
            ::unlink(filename_temporary);
        }
    }

private:
    void reset() {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "Front::reset::use_bitmap_comp=%u", this->ini.client.bitmap_compression ? 1 : 0);
            LOG(LOG_INFO, "Front::reset::use_compact_packets=%u", this->client_info.use_compact_packets);
            LOG(LOG_INFO, "Front::reset::bitmap_cache_version=%u", this->client_info.bitmap_cache_version);
        }

        if (this->mppc_enc) {
            delete this->mppc_enc;
            this->mppc_enc = nullptr;
        }

        this->max_bitmap_size = 1024 * 64;

        switch (Front::get_appropriate_compression_type(this->client_info.rdp_compression_type, this->ini.client.rdp_compression - 1))
        {
        case PACKET_COMPR_TYPE_RDP61:
            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front: Use RDP 6.1 Bulk compression");
            }
            //this->mppc_enc_match_finder = new rdp_mppc_61_enc_sequential_search_match_finder();
            this->mppc_enc = new rdp_mppc_61_enc_hash_based(this->ini.debug.compression);
            break;
        case PACKET_COMPR_TYPE_RDP6:
            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front: Use RDP 6.0 Bulk compression");
            }
            this->mppc_enc = new rdp_mppc_60_enc(this->ini.debug.compression);
            break;
        case PACKET_COMPR_TYPE_64K:
            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front: Use RDP 5.0 Bulk compression");
            }
            this->mppc_enc = new rdp_mppc_50_enc(this->ini.debug.compression);
            break;
        case PACKET_COMPR_TYPE_8K:
            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front: Use RDP 4.0 Bulk compression");
            }
            this->mppc_enc = new rdp_mppc_40_enc(this->ini.debug.compression);
            this->max_bitmap_size = 1024 * 8;
            break;
        }

        if (this->orders.has_bmp_cache_persister()) {
            this->save_persistent_disk_bitmap_cache();
        }
        this->orders.initialize(
            this->client_info
          , &this->trans
          , this->userid
          , this->share_id
          , this->encryptionLevel
          , this->encrypt
          , this->ini
          , this->max_bitmap_size
          , this->server_fastpath_update_support
          , this->mppc_enc
          , this->verbose
        );
    }

public:
    virtual void begin_update()
    {
        if (this->verbose & 64) {
            LOG(LOG_INFO, "Front::begin_update");
        }
        this->order_level++;
    }

    virtual void end_update()
    {
        if (this->verbose & 64) {
            LOG(LOG_INFO, "Front::end_update");
        }
        this->order_level--;
        if (!this->up_and_running) {
            LOG(LOG_ERR, "Front is not up and running.");
            throw Error(ERR_RDP_EXPECTING_CONFIRMACTIVEPDU);
        }
        if (this->order_level == 0) {
            this->flush();
        }
    }

    void disconnect() throw(Error)
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "Front::disconnect");
        }

        BStream x224_header(256);
        HStream mcs_data(256, 512);
        MCS::DisconnectProviderUltimatum_Send(mcs_data, 3, MCS::PER_ENCODING);
        X224::DT_TPDU_Send(x224_header,  mcs_data.size());

        this->trans.send(x224_header, mcs_data);
    }

    virtual const CHANNELS::ChannelDefArray & get_channel_list(void) const
    {
        return this->channel_list;
    }

    virtual void send_to_channel( const CHANNELS::ChannelDef & channel
                                , uint8_t * chunk
                                , size_t length
                                , size_t chunk_size
                                , int flags) {
        if (this->verbose & 16) {
            LOG( LOG_INFO
               , "Front::send_to_channel(channel='%s'(%d), data=%p, length=%u, chunk_size=%u, flags=%x)"
               , channel.name, channel.chanid, chunk, length, chunk_size, flags);
        }

        if ((channel.flags & GCC::UserData::CSNet::CHANNEL_OPTION_SHOW_PROTOCOL) &&
            (channel.chanid != this->rail_channel_id)) {
            flags |= CHANNELS::CHANNEL_FLAG_SHOW_PROTOCOL;
        }

        CHANNELS::VirtualChannelPDU virtual_channel_pdu(this->verbose);

        virtual_channel_pdu.send_to_client( this->trans, this->encrypt
                                          , this->encryptionLevel, userid, channel.chanid
                                          , length, flags, chunk, chunk_size);
    }

    // Global palette cf [MS-RDPCGR] 2.2.9.1.1.3.1.1.1 Palette Update Data
    // -------------------------------------------------------------------

    // updateType (2 bytes): A 16-bit, unsigned integer. The graphics update type.
    // This field MUST be set to UPDATETYPE_PALETTE (0x0002).

    // pad2Octets (2 bytes): A 16-bit, unsigned integer. Padding.
    // Values in this field are ignored.

    // numberColors (4 bytes): A 32-bit, unsigned integer.
    // The number of RGB triplets in the paletteData field.
    // This field MUST be set to NUM_8BPP_PAL_ENTRIES (256).

    void GeneratePaletteUpdateData(Stream & stream) {
        const BGRPalette & palette = (this->mod_bpp == 8) ? this->mod_palette_rgb : this->palette332_rgb;

        // Payload
        stream.out_uint16_le(RDP_UPDATE_PALETTE);
        stream.out_uint16_le(0);

        stream.out_uint32_le(256); /* # of colors */
        for (int i = 0; i < 256; i++) {
            int color = palette[i];
            // Palette entries is in BGR triplet format.
            uint8_t r = color >> 16;
            uint8_t g = color >> 8;
            uint8_t b = color;
            stream.out_uint8(r);
            stream.out_uint8(g);
            stream.out_uint8(b);
        }
        stream.mark_end();
    }

    //void SendLogonInfo(const uint8_t * user_name)
    //{
    //    HStream stream(1024, 2048);
    //
    //    // Payload
    //    RDP::SaveSessionInfoPDUData_Send ssipdu(stream, RDP::INFOTYPE_LOGON);
    //    RDP::LogonInfoVersion1_Send      liv1(stream,
    //                                          reinterpret_cast<const uint8_t *>(""),
    //                                          user_name, getpid());
    //    stream.mark_end();
    //
    //    const uint32_t log_condition = (128 | 8);
    //    ::send_share_data_ex( this->trans
    //                        , PDUTYPE2_SAVE_SESSION_INFO
    //                        , (this->ini.client.rdp_compression ? this->client_info.rdp_compression : 0)
    //                        , this->mppc_enc
    //                        , this->share_id
    //                        , this->encryptionLevel
    //                        , this->encrypt
    //                        , this->userid
    //                        , stream
    //                        , log_condition
    //                        , this->verbose
    //                        );
    //}

    void send_global_palette()
    {
        if (!this->palette_sent && (this->client_info.bpp == 8)) {
            if (this->verbose & 4) {
                LOG(LOG_INFO, "Front::send_global_palette");
            }

            this->flush();

            HStream stream(1024, 65536);
            GeneratePaletteUpdateData(stream);

            ::send_server_update( this->trans
                                , this->server_fastpath_update_support
                                , (this->ini.client.rdp_compression ? this->client_info.rdp_compression : 0)
                                , this->mppc_enc
                                , this->share_id
                                , this->encryptionLevel
                                , this->encrypt
                                , this->userid
                                , SERVER_UPDATE_GRAPHICS_PALETTE
                                , 0
                                , stream
                                , this->verbose
                                );

            this->palette_sent = true;
        }
    }

    void incoming(Callback & cb) throw(Error)
    {
        unsigned expected;

        if (this->verbose & 4) {
            LOG(LOG_INFO, "Front::incoming");
        }

        switch (this->state) {
        case CONNECTION_INITIATION:
        {
            // Connection Initiation
            // ---------------------

            // The client initiates the connection by sending the server an X.224 Connection
            //  Request PDU (class 0). The server responds with an X.224 Connection Confirm
            // PDU (class 0). From this point, all subsequent data sent between client and
            // server is wrapped in an X.224 Data Protocol Data Unit (PDU).

            // Client                                                     Server
            //    |------------X224 Connection Request PDU----------------> |
            //    | <----------X224 Connection Confirm PDU----------------- |

            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front::incoming:CONNECTION_INITIATION");
                LOG(LOG_INFO, "Front::incoming::receiving x224 request PDU");
            }

            {
                Array array(65536);
                uint8_t * end = array.get_data();
                X224::RecvFactory fx224(this->trans, &end, array.size());
                InStream stream(array, 0, 0, end - array.get_data());

                X224::CR_TPDU_Recv x224(stream, this->ini.client.bogus_neg_request);
                if (x224._header_size != stream.size()) {
                    LOG(LOG_ERR, "Front::incoming::connection request : all data should have been consumed,"
                                 " %d bytes remains", stream.size() - x224._header_size);
                }
                this->clientRequestedProtocols = x224.rdp_neg_requestedProtocols;

                if (// Proxy doesnt supports TLS or RDP client doesn't support TLS
                    (!this->ini.client.tls_support || 0 == (this->clientRequestedProtocols & X224::PROTOCOL_TLS))
                    // Fallback to legacy security protocol (RDP) is allowed.
                    && this->ini.client.tls_fallback_legacy) {
                    LOG(LOG_INFO, "Fallback to legacy security protocol");
                    this->tls_client_active = false;
                }
            }

            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front::incoming::sending x224 connection confirm PDU");
            }
            {
                BStream stream(256);
                uint8_t rdp_neg_type = 0;
                uint8_t rdp_neg_flags = /*0*/RdpNego::EXTENDED_CLIENT_DATA_SUPPORTED;
                uint32_t rdp_neg_code = 0;
                if (this->tls_client_active) {
                    LOG(LOG_INFO, "-----------------> Front::TLS Support Enabled");
                    if (this->clientRequestedProtocols & X224::PROTOCOL_TLS) {
                        rdp_neg_type = X224::RDP_NEG_RSP;
                        rdp_neg_code = X224::PROTOCOL_TLS;
                        this->encryptionLevel = 0;
                    }
                    else {
                        rdp_neg_type = X224::RDP_NEG_FAILURE;
                        rdp_neg_code = X224::SSL_REQUIRED_BY_SERVER;
                    }
                }
                else {
                    LOG(LOG_INFO, "-----------------> Front::TLS Support not Enabled");
                }

                X224::CC_TPDU_Send x224(stream, rdp_neg_type, rdp_neg_flags, rdp_neg_code);
                this->trans.send(stream);

                if (this->tls_client_active) {
                    this->trans.enable_server_tls(this->ini.globals.certificate_password);

            // 2.2.10.2 Early User Authorization Result PDU
            // ============================================

            // The Early User Authorization Result PDU is sent from server to client and is used
            // to convey authorization information to the client. This PDU is only sent by the server
            // if the client advertised support for it by specifying the PROTOCOL_HYBRID_EX (0x00000008)
            // flag in the requestedProtocols field of the RDP Negotiation Request (section 2.2.1.1.1)
            // structure and it MUST be sent immediately after the CredSSP handshake (section 5.4.5.2) has completed.

            // authorizationResult (4 bytes): A 32-bit unsigned integer. Specifies the authorization result.

            // +---------------------------------+--------------------------------------------------------+
            // | AUTHZ_SUCCESS 0x00000000        | The user has permission to access the server.          |
            // +---------------------------------+--------------------------------------------------------+
            // | AUTHZ _ACCESS_DENIED 0x0000052E | The user does not have permission to access the server.|
            // +---------------------------------+--------------------------------------------------------+

                }
            }
            // Basic Settings Exchange
            // -----------------------

            // Basic Settings Exchange: Basic settings are exchanged between the client and
            // server by using the MCS Connect Initial and MCS Connect Response PDUs. The
            // Connect Initial PDU contains a GCC Conference Create Request, while the
            // Connect Response PDU contains a GCC Conference Create Response.

            // These two Generic Conference Control (GCC) packets contain concatenated
            // blocks of settings data (such as core data, security data and network data)
            // which are read by client and server

            // Client                                                     Server
            //    |--------------MCS Connect Initial PDU with-------------> |
            //                   GCC Conference Create Request
            //    | <------------MCS Connect Response PDU with------------- |
            //                   GCC conference Create Response

            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front::incoming::Basic Settings Exchange");
            }


            Array array(65536);
            uint8_t * end = array.get_data();
            X224::RecvFactory fx224(this->trans, &end, array.size());
            InStream x224_data(array, 0, 0, end - array.get_data());

            X224::DT_TPDU_Recv x224(x224_data);
            MCS::CONNECT_INITIAL_PDU_Recv mcs_ci(x224.payload, MCS::BER_ENCODING);

            // GCC User Data
            // -------------
            SubStream & gcc_data = mcs_ci.payload;
            GCC::Create_Request_Recv gcc_cr(gcc_data);
            TODO("ensure gcc_data substream is fully consumed")

            while (gcc_cr.payload.in_check_rem(4)) {
                GCC::UserData::RecvFactory f(gcc_cr.payload);
                switch (f.tag) {
                    case CS_CORE:
                    {
                        GCC::UserData::CSCore cs_core;
                        cs_core.recv(f.payload);
                        if (this->verbose & 1) {
                            cs_core.log("Received from Client");
                        }

                        this->client_info.width     = cs_core.desktopWidth;
                        this->client_info.height    = cs_core.desktopHeight;
                        this->client_info.keylayout = cs_core.keyboardLayout;
                        this->client_info.build     = cs_core.clientBuild;
                        for (size_t i = 0; i < 16 ; i++) {
                            this->client_info.hostname[i] = cs_core.clientName[i];
                        }
                        //LOG(LOG_INFO, "hostname=\"%s\"", this->client_info.hostname);
                        this->client_info.bpp = 8;
                        switch (cs_core.postBeta2ColorDepth) {
                        case 0xca01:
                            /*
                            this->client_info.bpp =
                                (cs_core.highColorDepth <= 24)?cs_core.highColorDepth:24;
                            */
                            this->client_info.bpp = (
                                      (cs_core.earlyCapabilityFlags & GCC::UserData::RNS_UD_CS_WANT_32BPP_SESSION)
                                    ? 32
                                    : cs_core.highColorDepth
                                );
                        break;
                        case 0xca02:
                            this->client_info.bpp = 15;
                        break;
                        case 0xca03:
                            this->client_info.bpp = 16;
                        break;
                        case 0xca04:
                            this->client_info.bpp = 24;
                        break;
                        default:
                        break;
                        }
                        if (this->ini.client.max_color_depth) {
                            this->client_info.bpp = std::min<int>(
                                this->client_info.bpp, this->ini.client.max_color_depth);
                        }
                    }
                    break;
                    case CS_SECURITY:
                    {
                        GCC::UserData::CSSecurity cs_sec;
                        cs_sec.recv(f.payload);
                        if (this->verbose & 1) {
                            cs_sec.log("Received from Client");
                        }
                    }
                    break;
                    case CS_NET:
                    {
                        GCC::UserData::CSNet cs_net;
                        cs_net.recv(f.payload);
                        for (uint32_t index = 0; index < cs_net.channelCount; index++) {
                            const auto & channel_def = cs_net.channelDefArray[index];
                            CHANNELS::ChannelDef channel_item;
                            memcpy(channel_item.name, channel_def.name, 8);
                            channel_item.flags = channel_def.options;
                            channel_item.chanid = GCC::MCS_GLOBAL_CHANNEL + (index + 1);
                            this->channel_list.push_back(channel_item);

                            if (!this->rail_channel_id &&
                                !strcmp(channel_item.name, channel_names::rail)) {
                                this->rail_channel_id = channel_item.chanid;
                            }
                        }
                        if (this->verbose & 1) {
                            cs_net.log("Received from Client");
                        }
                    }
                    break;
                    case CS_CLUSTER:
                    {
                        GCC::UserData::CSCluster cs_cluster;
                        cs_cluster.recv(f.payload);
                        this->client_info.console_session =
                            (0 != (cs_cluster.flags & GCC::UserData::CSCluster::REDIRECTED_SESSIONID_FIELD_VALID));
                        if (this->verbose & 1) {
                            cs_cluster.log("Receiving from Client");
                        }
                    }
                    break;
                    case CS_MONITOR:
                    {
                        GCC::UserData::CSMonitor cs_monitor;
                        cs_monitor.recv(f.payload);
                        if (this->verbose & 1) {
                            cs_monitor.log("Receiving from Client");
                        }
                    }
                    break;
                    default:
                        LOG(LOG_WARNING, "Unexpected data block tag %x\n", f.tag);
                    break;
                }
            }
            if (gcc_cr.payload.in_check_rem(1)) {
                LOG(LOG_ERR, "recv connect request parsing gcc data : short header");
                throw Error(ERR_MCS_DATA_SHORT_HEADER);
            }

            // ------------------------------------------------------------------
            HStream stream(1024, 65536);
            // ------------------------------------------------------------------
//            GCC::UserData::ServerToClient_Send(stream, this->clientRequestedProtocols, this->channel_list.size());

            GCC::UserData::SCCore sc_core;
            sc_core.version = 0x00080004;
            if (this->tls_client_active) {
                sc_core.length = 12;
                sc_core.clientRequestedProtocols = this->clientRequestedProtocols;
            }
            if (this->verbose & 1) {
                sc_core.log("Sending to client");
            }
            sc_core.emit(stream);
            // ------------------------------------------------------------------
            GCC::UserData::SCNet sc_net;
            sc_net.MCSChannelId = GCC::MCS_GLOBAL_CHANNEL;
            sc_net.channelCount = this->channel_list.size();
            for (size_t index = 0; index < this->channel_list.size(); ++index) {
                sc_net.channelDefArray[index].id = this->channel_list[index].chanid;
            }
            if (this->verbose & 1) {
                sc_net.log("Sending to client");
            }
            sc_net.emit(stream);
            // ------------------------------------------------------------------
            if (this->tls_client_active) {
                GCC::UserData::SCSecurity sc_sec1;
                sc_sec1.encryptionMethod = 0;
                sc_sec1.encryptionLevel = 0;
                sc_sec1.length = 12;
                sc_sec1.serverRandomLen = 0;
                sc_sec1.serverCertLen = 0;
                if (this->verbose & 1) {
                    sc_sec1.log("Sending to client");
                }
                sc_sec1.emit(stream);
            }
            else {
                GCC::UserData::SCSecurity sc_sec1;
                /*
                   For now rsa_keys are not in a configuration file any more, but as we were not changing keys
                   the values have been embedded in code and the key generator file removed from source code.

                   It will be put back at some later time using a clean parser/writer module and sll calls
                   coherent with the remaining of ReDemPtion code. For reference to historical key generator code
                   look for utils/keygen.cpp in old repository code.

                   references for RSA Keys: http://www.securiteam.com/windowsntfocus/5EP010KG0G.html
                */
                uint8_t rsa_keys_pub_mod[64] = {
                    0x67, 0xab, 0x0e, 0x6a, 0x9f, 0xd6, 0x2b, 0xa3, 0x32, 0x2f, 0x41, 0xd1, 0xce, 0xee, 0x61, 0xc3,
                    0x76, 0x0b, 0x26, 0x11, 0x70, 0x48, 0x8a, 0x8d, 0x23, 0x81, 0x95, 0xa0, 0x39, 0xf7, 0x5b, 0xaa,
                    0x3e, 0xf1, 0xed, 0xb8, 0xc4, 0xee, 0xce, 0x5f, 0x6a, 0xf5, 0x43, 0xce, 0x5f, 0x60, 0xca, 0x6c,
                    0x06, 0x75, 0xae, 0xc0, 0xd6, 0xa4, 0x0c, 0x92, 0xa4, 0xc6, 0x75, 0xea, 0x64, 0xb2, 0x50, 0x5b
                };
                memcpy(this->pub_mod, rsa_keys_pub_mod, 64);

                uint8_t rsa_keys_pri_exp[64] = {
                    0x41, 0x93, 0x05, 0xB1, 0xF4, 0x38, 0xFC, 0x47, 0x88, 0xC4, 0x7F, 0x83, 0x8C, 0xEC, 0x90, 0xDA,
                    0x0C, 0x8A, 0xB5, 0xAE, 0x61, 0x32, 0x72, 0xF5, 0x2B, 0xD1, 0x7B, 0x5F, 0x44, 0xC0, 0x7C, 0xBD,
                    0x8A, 0x35, 0xFA, 0xAE, 0x30, 0xF6, 0xC4, 0x6B, 0x55, 0xA7, 0x65, 0xEF, 0xF4, 0xB2, 0xAB, 0x18,
                    0x4E, 0xAA, 0xE6, 0xDC, 0x71, 0x17, 0x3B, 0x4C, 0xC2, 0x15, 0x4C, 0xF7, 0x81, 0xBB, 0xF0, 0x03
                };
                memcpy(sc_sec1.pri_exp, rsa_keys_pri_exp, 64);
                memcpy(this->pri_exp, sc_sec1.pri_exp, 64);

                uint8_t rsa_keys_pub_sig[64] = {
                    0x6a, 0x41, 0xb1, 0x43, 0xcf, 0x47, 0x6f, 0xf1, 0xe6, 0xcc, 0xa1, 0x72, 0x97, 0xd9, 0xe1, 0x85,
                    0x15, 0xb3, 0xc2, 0x39, 0xa0, 0xa6, 0x26, 0x1a, 0xb6, 0x49, 0x01, 0xfa, 0xa6, 0xda, 0x60, 0xd7,
                    0x45, 0xf7, 0x2c, 0xee, 0xe4, 0x8e, 0x64, 0x2e, 0x37, 0x49, 0xf0, 0x4c, 0x94, 0x6f, 0x08, 0xf5,
                    0x63, 0x4c, 0x56, 0x29, 0x55, 0x5a, 0x63, 0x41, 0x2c, 0x20, 0x65, 0x95, 0x99, 0xb1, 0x15, 0x7c
                };

                uint8_t rsa_keys_pub_exp[4] = { 0x01, 0x00, 0x01, 0x00 };

                sc_sec1.encryptionMethod = this->encrypt.encryptionMethod;
                sc_sec1.encryptionLevel = this->encryptionLevel;
                sc_sec1.serverRandomLen = 32;
                this->gen.random(this->server_random, 32);
                memcpy(sc_sec1.serverRandom, this->server_random, 32);
                sc_sec1.dwVersion = GCC::UserData::SCSecurity::CERT_CHAIN_VERSION_1;
                sc_sec1.temporary = false;
                memcpy(sc_sec1.proprietaryCertificate.RSAPK.pubExp, rsa_keys_pub_exp, SEC_EXPONENT_SIZE);
                memcpy(sc_sec1.proprietaryCertificate.RSAPK.modulus, this->pub_mod, 64);
                memcpy(sc_sec1.proprietaryCertificate.RSAPK.modulus + 64,
                    "\x00\x00\x00\x00\x00\x00\x00\x00", SEC_PADDING_SIZE);
                memcpy(sc_sec1.proprietaryCertificate.wSignatureBlob, rsa_keys_pub_sig, 64);
                memcpy(sc_sec1.proprietaryCertificate.wSignatureBlob + 64,
                    "\x00\x00\x00\x00\x00\x00\x00\x00", SEC_PADDING_SIZE);

                if (this->verbose & 1) {
                    sc_sec1.log("Sending to client");
                }
                sc_sec1.emit(stream);
            }
            stream.mark_end();

            // ------------------------------------------------------------------
            BStream gcc_header(256);
            BStream mcs_header(256);
            BStream x224_header(256);

            GCC::Create_Response_Send(gcc_header, stream.size());
            MCS::CONNECT_RESPONSE_Send mcs_cr(mcs_header, gcc_header.size() + stream.size(), MCS::BER_ENCODING);
            X224::DT_TPDU_Send(x224_header, mcs_header.size() + gcc_header.size() + stream.size());
            this->trans.send(x224_header, mcs_header, gcc_header, stream);

            // Channel Connection
            // ------------------

            // Channel Connection: The client sends an MCS Erect Domain Request PDU,
            // followed by an MCS Attach User Request PDU to attach the primary user
            // identity to the MCS domain.

            // The server responds with an MCS Attach User Response PDU containing the user
            // channel ID.

            // The client then proceeds to join the :
            // - user channel,
            // - the input/output (I/O) channel
            // - and all of the static virtual channels

            // (the I/O and static virtual channel IDs are obtained from the data embedded
            //  in the GCC packets) by using multiple MCS Channel Join Request PDUs.

            // The server confirms each channel with an MCS Channel Join Confirm PDU.
            // (The client only sends a Channel Join Request after it has received the
            // Channel Join Confirm for the previously sent request.)

            // From this point, all subsequent data sent from the client to the server is
            // wrapped in an MCS Send Data Request PDU, while data sent from the server to
            //  the client is wrapped in an MCS Send Data Indication PDU. This is in
            // addition to the data being wrapped by an X.224 Data PDU.

            // Client                                                     Server
            //    |-------MCS Erect Domain Request PDU--------------------> |
            //    |-------MCS Attach User Request PDU---------------------> |

            //    | <-----MCS Attach User Confirm PDU---------------------- |

            //    |-------MCS Channel Join Request PDU--------------------> |
            //    | <-----MCS Channel Join Confirm PDU--------------------- |

            if (this->verbose & 16) {
                LOG(LOG_INFO, "Front::incoming::Channel Connection");
            }

            if (this->verbose) {
                LOG(LOG_INFO, "Front::incoming::Recv MCS::ErectDomainRequest");
            }
            {
                Array array(256);
                uint8_t * end = array.get_data();
                X224::RecvFactory fx224(this->trans, &end, array.size());
                REDASSERT(fx224.type == X224::DT_TPDU);
                InStream x224_data(array, 0, 0, end - array.get_data());

                X224::DT_TPDU_Recv x224(x224_data);
                MCS::ErectDomainRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
            }
            if (this->verbose) {
                LOG(LOG_INFO, "Front::incoming::Recv MCS::AttachUserRequest");
            }
            {
                Array array(256);
                uint8_t * end = array.get_data();
                X224::RecvFactory fx224(this->trans, &end, array.size());
                REDASSERT(fx224.type == X224::DT_TPDU);
                InStream x224_data(array, 0, 0, end - array.get_data());
                X224::DT_TPDU_Recv x224(x224_data);
                MCS::AttachUserRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
            }

            if (this->ini.client.bogus_user_id) {
                // To avoid bug in freerdp 0.7.x and Remmina 0.8.x that causes client disconnection
                //  when unexpected channel id is received.
                this->userid = 32;
            }

            if (this->verbose) {
                LOG(LOG_INFO, "Front::incoming::Send MCS::AttachUserConfirm userid=%u", this->userid);
            }
            {
                BStream x224_header(256);
                HStream mcs_data(256, 512);
                MCS::AttachUserConfirm_Send(mcs_data, MCS::RT_SUCCESSFUL, true, this->userid, MCS::PER_ENCODING);
                X224::DT_TPDU_Send(x224_header, mcs_data.size());
                this->trans.send(x224_header, mcs_data);
            }

            {
                // read tpktHeader (4 bytes = 3 0 len)
                // TPDU class 0    (3 bytes = LI F0 PDU_DT)
                Array array(256);
                uint8_t * end = array.get_data();
                X224::RecvFactory fx224(this->trans, &end, array.size());
                REDASSERT(fx224.type == X224::DT_TPDU);
                InStream x224_data(array, 0, 0, end - array.get_data());
                X224::DT_TPDU_Recv x224(x224_data);
                MCS::ChannelJoinRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
                this->userid = mcs.initiator;

                BStream x224_header(256);
                HStream mcs_cjcf_data(256, 512);

                MCS::ChannelJoinConfirm_Send(mcs_cjcf_data, MCS::RT_SUCCESSFUL,
                                             mcs.initiator,
                                             mcs.channelId,
                                             true, mcs.channelId,
                                             MCS::PER_ENCODING);
                X224::DT_TPDU_Send(x224_header, mcs_cjcf_data.size());
                this->trans.send(x224_header, mcs_cjcf_data);
            }

            {
                Array array(256);
                uint8_t * end = array.get_data();
                X224::RecvFactory fx224(this->trans, &end, array.size());
                REDASSERT(fx224.type == X224::DT_TPDU);
                InStream x224_data(array, 0, 0, end - array.get_data());
                X224::DT_TPDU_Recv x224(x224_data);
                MCS::ChannelJoinRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
                if (mcs.initiator != this->userid) {
                    LOG(LOG_ERR, "MCS error bad userid, expecting %u got %u", this->userid, mcs.initiator);
                    throw Error(ERR_MCS_BAD_USERID);
                }

                BStream x224_header(256);
                HStream mcs_cjcf_data(256, 512);

                MCS::ChannelJoinConfirm_Send(mcs_cjcf_data, MCS::RT_SUCCESSFUL,
                                             mcs.initiator,
                                             mcs.channelId,
                                             true, mcs.channelId,
                                             MCS::PER_ENCODING);
                X224::DT_TPDU_Send(x224_header, mcs_cjcf_data.size());
                this->trans.send(x224_header, mcs_cjcf_data);
            }

            for (size_t i = 0 ; i < this->channel_list.size(); ++i) {
                Array array(256);
                uint8_t * end = array.get_data();
                X224::RecvFactory fx224(this->trans, &end, array.size());
                InStream x224_data(array, 0, 0, end - array.get_data());
                X224::DT_TPDU_Recv x224(x224_data);
                MCS::ChannelJoinRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);

                if (this->verbose & 16) {
                    LOG(LOG_INFO, "cjrq[%u] = %u -> cjcf", i, mcs.channelId);
                }

                if (mcs.initiator != this->userid) {
                    LOG(LOG_ERR, "MCS error bad userid, expecting %u got %u", this->userid, mcs.initiator);
                    throw Error(ERR_MCS_BAD_USERID);
                }

                BStream x224_header(256);
                HStream mcs_cjcf_data(256, 512);

                MCS::ChannelJoinConfirm_Send(mcs_cjcf_data, MCS::RT_SUCCESSFUL,
                                             mcs.initiator,
                                             mcs.channelId,
                                             true, mcs.channelId,
                                             MCS::PER_ENCODING);
                X224::DT_TPDU_Send(x224_header, mcs_cjcf_data.size());
                this->trans.send(x224_header, mcs_cjcf_data);

                this->channel_list.set_chanid(i, mcs.channelId);
            }

            if (this->verbose & 1) {
                LOG(LOG_INFO, "Front::incoming::RDP Security Commencement");
            }

            // RDP Security Commencement
            // -------------------------

            // RDP Security Commencement: If standard RDP security methods are being
            // employed and encryption is in force (this is determined by examining the data
            // embedded in the GCC Conference Create Response packet) then the client sends
            // a Security Exchange PDU containing an encrypted 32-byte random number to the
            // server. This random number is encrypted with the public key of the server
            // (the server's public key, as well as a 32-byte server-generated random
            // number, are both obtained from the data embedded in the GCC Conference Create
            //  Response packet).

            // The client and server then utilize the two 32-byte random numbers to generate
            // session keys which are used to encrypt and validate the integrity of
            // subsequent RDP traffic.

            // From this point, all subsequent RDP traffic can be encrypted and a security
            // header is include " with the data if encryption is in force (the Client Info
            // and licensing PDUs are an exception in that they always have a security
            // header). The Security Header follows the X.224 and MCS Headers and indicates
            // whether the attached data is encrypted.

            // Even if encryption is in force server-to-client traffic may not always be
            // encrypted, while client-to-server traffic will always be encrypted by
            // Microsoft RDP implementations (encryption of licensing PDUs is optional,
            // however).

            // Client                                                     Server
            //    |------Security Exchange PDU ---------------------------> |
            if (this->tls_client_active) {
                LOG(LOG_INFO, "TLS mode: exchange packet disabled");
            }
            else
            {
                LOG(LOG_INFO, "Legacy RDP mode: expecting exchange packet");
                Array array(256);
                uint8_t * end = array.get_data();
                X224::RecvFactory fx224(this->trans, &end, array.size());
                InStream pdu(array, 0, 0, end - array.get_data());
                X224::DT_TPDU_Recv x224(pdu);

                int mcs_type = MCS::peekPerEncodedMCSType(x224.payload);
                if (mcs_type == MCS::MCSPDU_DisconnectProviderUltimatum) {
                    LOG(LOG_INFO, "Front::incoming::DisconnectProviderUltimatum received");
                    MCS::DisconnectProviderUltimatum_Recv mcs(x224.payload, MCS::PER_ENCODING);
                    const char * reason = MCS::get_reason(mcs.reason);
                    LOG(LOG_INFO, "Front DisconnectProviderUltimatum: reason=%s [%d]", reason, mcs.reason);
                    throw Error(ERR_MCS);
                }

                MCS::SendDataRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
                SEC::SecExchangePacket_Recv sec(mcs.payload);

                TODO("see possible factorisation with ssl_calls.hpp/ssllib::rsa_encrypt")
                uint8_t client_random[64];
                memset(client_random, 0, 64);
                {
                    uint8_t l_out[64]; memset(l_out, 0, 64);
                    uint8_t l_in[64];  rmemcpy(l_in, sec.payload.get_data(), 64);
                    uint8_t l_mod[64]; rmemcpy(l_mod, this->pub_mod, 64);
                    uint8_t l_exp[64]; rmemcpy(l_exp, this->pri_exp, 64);

                    BN_CTX* ctx = BN_CTX_new();
                    BIGNUM lmod; BN_init(&lmod); BN_bin2bn(l_mod, 64, &lmod);
                    BIGNUM lexp; BN_init(&lexp); BN_bin2bn(l_exp, 64, &lexp);
                    BIGNUM lin; BN_init(&lin);  BN_bin2bn(l_in, 64, &lin);
                    BIGNUM lout; BN_init(&lout); BN_mod_exp(&lout, &lin, &lexp, &lmod, ctx);

                    int rv = BN_bn2bin(&lout, l_out);
                    if (rv <= 64) {
                        reverseit(l_out, rv);
                        memcpy(client_random, l_out, 64);
                    }
                    BN_free(&lin);
                    BN_free(&lout);
                    BN_free(&lexp);
                    BN_free(&lmod);
                    BN_CTX_free(ctx);
                }

                // beware order of parameters for key generation (decrypt/encrypt) is inversed between server and client
                SEC::KeyBlock key_block(client_random, this->server_random);
                memcpy(this->encrypt.sign_key, key_block.blob0, 16);
                ssllib ssl;
                if (this->encrypt.encryptionMethod == 1) {
                    ssl.sec_make_40bit(this->encrypt.sign_key);
                }

                this->encrypt.generate_key(key_block.key1, this->encrypt.encryptionMethod);
                this->decrypt.generate_key(key_block.key2, this->encrypt.encryptionMethod);
            }
            this->state = WAITING_FOR_LOGON_INFO;
        }
        break;

        case WAITING_FOR_LOGON_INFO:
        // Secure Settings Exchange
        // ------------------------

        // Secure Settings Exchange: Secure client data (such as the username,
        // password and auto-reconnect cookie) is sent to the server using the Client
        // Info PDU.

        // Client                                                     Server
        //    |------ Client Info PDU      ---------------------------> |
        {
            LOG(LOG_INFO, "Front::incoming::Secure Settings Exchange");

            Array array(65536);
            uint8_t * end = array.get_data();
            X224::RecvFactory fx224(this->trans, &end, array.size());
            InStream stream(array, 0, 0, end - array.get_data());
            X224::DT_TPDU_Recv x224(stream);

            int mcs_type = MCS::peekPerEncodedMCSType(x224.payload);
            if (mcs_type == MCS::MCSPDU_DisconnectProviderUltimatum) {
                LOG(LOG_INFO, "Front::incoming::DisconnectProviderUltimatum received");
                MCS::DisconnectProviderUltimatum_Recv mcs(x224.payload, MCS::PER_ENCODING);
                const char * reason = MCS::get_reason(mcs.reason);
                LOG(LOG_INFO, "Front DisconnectProviderUltimatum: reason=%s [%d]", reason, mcs.reason);
                throw Error(ERR_MCS);
            }

            MCS::SendDataRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
            SEC::SecSpecialPacket_Recv sec(mcs.payload, this->decrypt, this->encryptionLevel);
            if (this->verbose & 128) {
                LOG(LOG_INFO, "sec decrypted payload:");
                hexdump_d(sec.payload.get_data(), sec.payload.size());
            }

            if (!(sec.flags & SEC::SEC_INFO_PKT)) {
                throw Error(ERR_SEC_EXPECTED_LOGON_INFO);
            }

            /* this is the first test that the decrypt is working */
            this->client_info.process_logon_info( sec.payload
                                                , ini.client.ignore_logon_password
                                                , ini.client.performance_flags_default
                                                , ini.client.performance_flags_force_present
                                                , ini.client.performance_flags_force_not_present
                                                , ini.debug.password
                                                , (this->verbose & 128)
                                                );

            if (sec.payload.in_remain()) {
                LOG(LOG_ERR, "Front::incoming::process_logon all data should have been consumed %u bytes trailing",
                    (unsigned)sec.payload.in_remain());
            }

            this->keymap.init_layout(this->client_info.keylayout);
            LOG(LOG_INFO, "Front Keyboard Layout = 0x%x", this->client_info.keylayout);
            this->ini.client.keyboard_layout.set(this->client_info.keylayout);
            if (this->client_info.is_mce) {
                if (this->verbose & 2) {
                    LOG(LOG_INFO, "Front::incoming::licencing client_info.is_mce");
                    LOG(LOG_INFO, "Front::incoming::licencing send_media_lic_response");
                }

                {
                    HStream stream(1024, 65535);

                    /* mce */
                    /* some compilers need unsigned char to avoid warnings */
                    static uint8_t lic3[16] = { 0xff, 0x03, 0x10, 0x00,
                                             0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
                                             0xf3, 0x99, 0x00, 0x00
                                             };

                    stream.out_copy_bytes((char*)lic3, 16);
                    stream.mark_end();

                    BStream sec_header(65536);

                    if ((this->verbose & (128 | 2)) == (128 | 2)) {
                        LOG(LOG_INFO, "Sec clear payload to send:");
                        hexdump_d(stream.get_data(), stream.size());
                    }

                    SEC::Sec_Send sec(sec_header, stream, SEC::SEC_LICENSE_PKT | 0x00100200, this->encrypt, 0);
                    stream.copy_to_head(sec_header.get_data(), sec_header.size());

                    this->send_data_indication(GCC::MCS_GLOBAL_CHANNEL, stream);
                }
                // proceed with capabilities exchange

                // Capabilities Exchange
                // ---------------------

                // Capabilities Negotiation: The server sends the set of capabilities it
                // supports to the client in a Demand Active PDU. The client responds with its
                // capabilities by sending a Confirm Active PDU.

                // Client                                                     Server
                //    | <------- Demand Active PDU ---------------------------- |
                //    |--------- Confirm Active PDU --------------------------> |

                if (this->verbose & 1) {
                    LOG(LOG_INFO, "Front::incoming::send_demand_active");
                }
                this->send_demand_active();

                LOG(LOG_INFO, "Front::incoming::ACTIVATED (mce)");
                this->state = ACTIVATE_AND_PROCESS_DATA;
            }
            else {
                if (this->verbose & 16) {
                    LOG(LOG_INFO, "Front::incoming::licencing not client_info.is_mce");
                    LOG(LOG_INFO, "Front::incoming::licencing send_lic_initial");
                }

                HStream stream(1024, 65535);

                stream.out_uint8(LIC::LICENSE_REQUEST);
                stream.out_uint8(2); // preamble flags : PREAMBLE_VERSION_2_0 (RDP 4.0)
                stream.out_uint16_le(318); // wMsgSize = 318 including preamble

                /* some compilers need unsigned char to avoid warnings */
                static uint8_t lic1[314] = {
                    // SEC_RANDOM ?
                    0x7b, 0x3c, 0x31, 0xa6, 0xae, 0xe8, 0x74, 0xf6,
                    0xb4, 0xa5, 0x03, 0x90, 0xe7, 0xc2, 0xc7, 0x39,
                    0xba, 0x53, 0x1c, 0x30, 0x54, 0x6e, 0x90, 0x05,
                    0xd0, 0x05, 0xce, 0x44, 0x18, 0x91, 0x83, 0x81,
                    //
                    0x00, 0x00, 0x04, 0x00, 0x2c, 0x00, 0x00, 0x00,
                    0x4d, 0x00, 0x69, 0x00, 0x63, 0x00, 0x72, 0x00,
                    0x6f, 0x00, 0x73, 0x00, 0x6f, 0x00, 0x66, 0x00,
                    0x74, 0x00, 0x20, 0x00, 0x43, 0x00, 0x6f, 0x00,
                    0x72, 0x00, 0x70, 0x00, 0x6f, 0x00, 0x72, 0x00,
                    0x61, 0x00, 0x74, 0x00, 0x69, 0x00, 0x6f, 0x00,
                    0x6e, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
                    0x32, 0x00, 0x33, 0x00, 0x36, 0x00, 0x00, 0x00,
                    0x0d, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
                    0x03, 0x00, 0xb8, 0x00, 0x01, 0x00, 0x00, 0x00,
                    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                    0x06, 0x00, 0x5c, 0x00, 0x52, 0x53, 0x41, 0x31,
                    0x48, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
                    0x3f, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
                    0x01, 0xc7, 0xc9, 0xf7, 0x8e, 0x5a, 0x38, 0xe4,
                    0x29, 0xc3, 0x00, 0x95, 0x2d, 0xdd, 0x4c, 0x3e,
                    0x50, 0x45, 0x0b, 0x0d, 0x9e, 0x2a, 0x5d, 0x18,
                    0x63, 0x64, 0xc4, 0x2c, 0xf7, 0x8f, 0x29, 0xd5,
                    0x3f, 0xc5, 0x35, 0x22, 0x34, 0xff, 0xad, 0x3a,
                    0xe6, 0xe3, 0x95, 0x06, 0xae, 0x55, 0x82, 0xe3,
                    0xc8, 0xc7, 0xb4, 0xa8, 0x47, 0xc8, 0x50, 0x71,
                    0x74, 0x29, 0x53, 0x89, 0x6d, 0x9c, 0xed, 0x70,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x08, 0x00, 0x48, 0x00, 0xa8, 0xf4, 0x31, 0xb9,
                    0xab, 0x4b, 0xe6, 0xb4, 0xf4, 0x39, 0x89, 0xd6,
                    0xb1, 0xda, 0xf6, 0x1e, 0xec, 0xb1, 0xf0, 0x54,
                    0x3b, 0x5e, 0x3e, 0x6a, 0x71, 0xb4, 0xf7, 0x75,
                    0xc8, 0x16, 0x2f, 0x24, 0x00, 0xde, 0xe9, 0x82,
                    0x99, 0x5f, 0x33, 0x0b, 0xa9, 0xa6, 0x94, 0xaf,
                    0xcb, 0x11, 0xc3, 0xf2, 0xdb, 0x09, 0x42, 0x68,
                    0x29, 0x56, 0x58, 0x01, 0x56, 0xdb, 0x59, 0x03,
                    0x69, 0xdb, 0x7d, 0x37, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                    0x0e, 0x00, 0x0e, 0x00, 0x6d, 0x69, 0x63, 0x72,
                    0x6f, 0x73, 0x6f, 0x66, 0x74, 0x2e, 0x63, 0x6f,
                    0x6d, 0x00
                };

                stream.out_copy_bytes((char*)lic1, 314);
                stream.mark_end();

                BStream sec_header(65536);

                if ((this->verbose & (128 | 2)) == (128 | 2)) {
                    LOG(LOG_INFO, "Sec clear payload to send:");
                    hexdump_d(stream.get_data(), stream.size());
                }

                SEC::Sec_Send sec(sec_header, stream, SEC::SEC_LICENSE_PKT,
                    this->encrypt, 0);
                stream.copy_to_head(sec_header.get_data(), sec_header.size());

                this->send_data_indication(GCC::MCS_GLOBAL_CHANNEL, stream);

                if (this->verbose & 2) {
                    LOG(LOG_INFO, "Front::incoming::waiting for answer to lic_initial");
                }
                this->state = WAITING_FOR_ANSWER_TO_LICENCE;
            }
        }
        break;

        case WAITING_FOR_ANSWER_TO_LICENCE:
        {
            if (this->verbose & 2) {
                LOG(LOG_INFO, "Front::incoming::WAITING_FOR_ANSWER_TO_LICENCE");
            }
            Array array(65536);
            uint8_t * end = array.get_data();
            X224::RecvFactory fx224(this->trans, &end, array.size());
            InStream stream(array, 0, 0, end - array.get_data());
            X224::DT_TPDU_Recv x224(stream);

            int mcs_type = MCS::peekPerEncodedMCSType(x224.payload);

            if (mcs_type == MCS::MCSPDU_DisconnectProviderUltimatum) {
                MCS::DisconnectProviderUltimatum_Recv mcs(x224.payload, MCS::PER_ENCODING);
                const char * reason = MCS::get_reason(mcs.reason);
                LOG(LOG_INFO, "Front DisconnectProviderUltimatum: reason=%s [%d]", reason, mcs.reason);
                throw Error(ERR_MCS);
            }

            MCS::SendDataRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
            SEC::SecSpecialPacket_Recv sec(mcs.payload, this->decrypt, this->encryptionLevel);
            if ((this->verbose & (128 | 2)) == (128 | 2)) {
                LOG(LOG_INFO, "sec decrypted payload:");
                hexdump_d(sec.payload.get_data(), sec.payload.size());
            }

            // Licensing
            // ---------

            // Licensing: The goal of the licensing exchange is to transfer a
            // license from the server to the client.

            // The client should store this license and on subsequent
            // connections send the license to the server for validation.
            // However, in some situations the client may not be issued a
            // license to store. In effect, the packets exchanged during this
            // phase of the protocol depend on the licensing mechanisms
            // employed by the server. Within the context of this document
            // we will assume that the client will not be issued a license to
            // store. For details regarding more advanced licensing scenarios
            // that take place during the Licensing Phase, see [MS-RDPELE].

            // Client                                                     Server
            //    | <------ Licence Error PDU Valid Client ---------------- |

            if (sec.flags & SEC::SEC_LICENSE_PKT) {
                LIC::RecvFactory flic(sec.payload);
                switch (flic.tag) {
                case LIC::ERROR_ALERT:
                {
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "Front::ERROR_ALERT");
                    }
                    TODO("We should check what is actually returned by this message, as it may be an error")
                    LIC::ErrorAlert_Recv lic(sec.payload);
                    LOG(LOG_ERR, "Front::License Alert: error=%u transition=%u",
                        lic.validClientMessage.dwErrorCode, lic.validClientMessage.dwStateTransition);

                }
                break;
                case LIC::NEW_LICENSE_REQUEST:
                {
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "Front::NEW_LICENSE_REQUEST");
                    }
                    LIC::NewLicenseRequest_Recv lic(sec.payload);
                    TODO("Instead of returning a license we return a message saying that no license is OK")
                    this->send_valid_client_license_data();
/*
                    HStream stream(1024, 65535);
                    // Valid Client License Data (LICENSE_VALID_CLIENT_DATA)

                    // some compilers need unsigned char to avoid warnings
                    static uint8_t lic2[16] = {
                        0xff,       // bMsgType : ERROR_ALERT
                        0x02,       // NOT EXTENDED_ERROR_MSG_SUPPORTED, PREAMBLE_VERSION_2_0
                        0x10, 0x00, // wMsgSize: 16 bytes including preamble
                        0x07, 0x00, 0x00, 0x00, // dwErrorCode : STATUS_VALID_CLIENT
                        0x02, 0x00, 0x00, 0x00, // dwStateTransition ST_NO_TRANSITION
                        0x28, 0x14, // wBlobType : ignored because wBlobLen is 0
                        0x00, 0x00  // wBlobLen  : 0
                    };
                    stream.out_copy_bytes((char*)lic2, 16);
                    stream.mark_end();

                    BStream sec_header(256);

                    if ((this->verbose & (128 | 2)) == (128 | 2)) {
                        LOG(LOG_INFO, "Sec clear payload to send:");
                        hexdump_d(stream.get_data(), stream.size());
                    }

                    SEC::Sec_Send sec(sec_header, stream, SEC::SEC_LICENSE_PKT | 0x00100000, this->encrypt, 0);
                    stream.copy_to_head(sec_header.get_data(), sec_header.size());

                    this->send_data_indication(GCC::MCS_GLOBAL_CHANNEL, stream);
*/
                }
                break;
                case LIC::PLATFORM_CHALLENGE_RESPONSE:
                    TODO("As we never send a platform challenge, it is unlikely we ever receive a PLATFORM_CHALLENGE_RESPONSE")
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "Front::PLATFORM_CHALLENGE_RESPONSE");
                    }
                    break;
                case LIC::LICENSE_INFO:
                    TODO("As we never send a server license request, it is unlikely we ever receive a LICENSE_INFO")
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "Front::LICENSE_INFO");
                    }
                    TODO("Instead of returning a license we return a message saying that no license is OK")
                    this->send_valid_client_license_data();
                    break;
                default:
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "Front::LICENCE_TAG %u unknown or unsupported by server", flic.tag);
                    }
                    break;
                }
                // licence received, proceed with capabilities exchange

                // Capabilities Exchange
                // ---------------------

                // Capabilities Negotiation: The server sends the set of capabilities it
                // supports to the client in a Demand Active PDU. The client responds with its
                // capabilities by sending a Confirm Active PDU.

                // Client                                                     Server
                //    | <------- Demand Active PDU ---------------------------- |
                //    |--------- Confirm Active PDU --------------------------> |

                if (this->verbose & 1) {
                    LOG(LOG_INFO, "Front::incoming::send_demand_active");
                }
                this->send_demand_active();

                LOG(LOG_INFO, "Front::incoming::ACTIVATED (new license request)");
                this->state = ACTIVATE_AND_PROCESS_DATA;
            }
            else {
                if (this->verbose & 2) {
                    LOG(LOG_INFO, "non licence packet: still waiting for licence");
                }
                ShareControl_Recv sctrl(sec.payload);

                switch (sctrl.pduType) {
                case PDUTYPE_DEMANDACTIVEPDU: /* 1 */
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "unexpected DEMANDACTIVE PDU while in licence negociation");
                    }
                    break;
                case PDUTYPE_CONFIRMACTIVEPDU:
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "Unexpected CONFIRMACTIVE PDU");
                    }
                    {
                        expected = 6; /* shareId(4) + originatorId(2) */
                        if (!sctrl.payload.in_check_rem(expected)) {
                            LOG(LOG_ERR, "Truncated CONFIRMACTIVE PDU, need=%u remains=%u",
                                expected, sctrl.payload.in_remain());
                            throw Error(ERR_MCS_PDU_TRUNCATED);
                        }
                        uint32_t share_id = sctrl.payload.in_uint32_le();
                        uint16_t originatorId = sctrl.payload.in_uint16_le();
                        this->process_confirm_active(sctrl.payload);
                        (void)share_id;
                        (void)originatorId;
                    }
                    if (!sctrl.payload.check_end()) {
                        LOG(LOG_ERR, "Trailing data after CONFIRMACTIVE PDU remains=%u",
                            sctrl.payload.in_remain());
                        throw Error(ERR_MCS_PDU_TRAILINGDATA);
                    }
                    break;
                case PDUTYPE_DATAPDU: /* 7 */
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "unexpected DATA PDU while in licence negociation");
                    }
                    // at this point licence negociation is still ongoing
                    // most data packets should not be received
                    // actually even input is dubious,
                    // but rdesktop actually sends input data
                    // also processing this is a problem because input data packets are broken
//                    this->process_data(sctrl.payload, cb);

                    TODO("check all payload data is consumed")
                    break;
                case PDUTYPE_DEACTIVATEALLPDU:
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "unexpected DEACTIVATEALL PDU while in licence negociation");
                    }
                    TODO("check all payload data is consumed")
                    break;
                case PDUTYPE_SERVER_REDIR_PKT:
                    if (this->verbose & 2) {
                        LOG(LOG_INFO, "unsupported SERVER_REDIR_PKT while in licence negociation");
                    }
                    TODO("check all payload data is consumed")
                    break;
                default:
                    LOG(LOG_WARNING, "unknown PDU type received while in licence negociation (%d)\n", sctrl.pduType);
                    break;
                }
                TODO("Check why this is necessary when using loop connection ?")
            }
            sec.payload.p = sec.payload.end;
        }
        break;

        case ACTIVATE_AND_PROCESS_DATA:
        if (this->verbose & 8) {
            LOG(LOG_INFO, "Front::incoming::ACTIVATE_AND_PROCESS_DATA");
        }
        // Connection Finalization
        // -----------------------

        // Connection Finalization: The client and server send PDUs to finalize the
        // connection details. The client-to-server and server-to-client PDUs exchanged
        // during this phase may be sent concurrently as long as the sequencing in
        // either direction is maintained (there are no cross-dependencies between any
        // of the client-to-server and server-to-client PDUs). After the client receives
        // the Font Map PDU it can start sending mouse and keyboard input to the server,
        // and upon receipt of the Font List PDU the server can start sending graphics
        // output to the client.

        // Client                                                     Server
        //    |----------Synchronize PDU------------------------------> |
        //    |----------Control PDU Cooperate------------------------> |
        //    |----------Control PDU Request Control------------------> |
        //    |----------Persistent Key List PDU(s)-------------------> |
        //    |----------Font List PDU--------------------------------> |

        //    | <--------Synchronize PDU------------------------------- |
        //    | <--------Control PDU Cooperate------------------------- |
        //    | <--------Control PDU Granted Control------------------- |
        //    | <--------Font Map PDU---------------------------------- |

        // All PDU's in the client-to-server direction must be sent in the specified
        // order and all PDU's in the server to client direction must be sent in the
        // specified order. However, there is no requirement that client to server PDU's
        // be sent before server-to-client PDU's. PDU's may be sent concurrently as long
        // as the sequencing in either direction is maintained.


        // Besides input and graphics data, other data that can be exchanged between
        // client and server after the connection has been finalized include "
        // connection management information and virtual channel messages (exchanged
        // between client-side plug-ins and server-side applications).
        {
            Array array(65536);
            uint8_t * end = array.get_data();
            X224::RecvFactory fx224(this->trans, &end, array.size(), true);
            InStream stream(array, 0, 0, end - array.get_data());

            if (fx224.fast_path) {
                FastPath::ClientInputEventPDU_Recv cfpie(stream, this->decrypt);

                for (uint8_t i = 0; i < cfpie.numEvents; i++) {
                    if (!cfpie.payload.in_check_rem(1)) {
                        LOG(LOG_ERR, "Truncated Fast-Path input event PDU, need=1 remains=%u",
                            cfpie.payload.in_remain());
                        throw Error(ERR_RDP_DATA_TRUNCATED);
                    }

                    uint8_t byte = cfpie.payload.in_uint8();
                    uint8_t eventCode  = (byte & 0xE0) >> 5;

                    switch (eventCode) {
                        case FastPath::FASTPATH_INPUT_EVENT_SCANCODE:
                        {
                            FastPath::KeyboardEvent_Recv ke(cfpie.payload, byte);

                            if (this->verbose & 4) {
                                LOG(LOG_INFO,
                                    "Front::Received fast-path PUD, scancode keyboardFlags=0x%X, keyCode=0x%X",
                                    ke.spKeyboardFlags, ke.keyCode);
                            }

                            BStream decoded_data(256);
                            bool    tsk_switch_shortcuts;

                            this->keymap.event(ke.spKeyboardFlags, ke.keyCode, decoded_data, tsk_switch_shortcuts);
                            decoded_data.mark_end();
                            //LOG(LOG_INFO, "Decoded keyboard input data:");
                            //hexdump_d(decoded_data.get_data(), decoded_data.size());

                            bool send_to_mod = true;

                            if (  this->capture
                               && (this->capture_state == CAPTURE_STATE_STARTED)
                               && decoded_data.size()) {
                                if (this->focus_on_password_textbox) {
                                    unsigned char_count = decoded_data.size() / sizeof(uint32_t);
                                    decoded_data.reset();
                                    for (; char_count > 0; char_count--) {
                                        // Unicode Character 'BLACK CIRCLE' (U+25CF).
                                        decoded_data.out_uint32_le(0x25CF);
                                    }
                                    decoded_data.mark_end();
                                }

                                struct timeval now = tvtime();

                                send_to_mod = this->capture->input(now, decoded_data);
                            }

                            if (this->up_and_running) {
                                if (tsk_switch_shortcuts && this->ini.client.disable_tsk_switch_shortcuts.get()) {
                                    LOG(LOG_INFO, "Ctrl+Alt+Del and Ctrl+Shift+Esc keyboard sequences ignored.");
                                }
                                else {
                                    if (!this->input_event_and_graphics_update_disabled &&
                                        send_to_mod) {
                                        cb.rdp_input_scancode(ke.keyCode, 0, ke.spKeyboardFlags, 0, &this->keymap);
                                    }
                                    this->has_activity = true;
                                }
                            }
                        }
                        break;

                        case FastPath::FASTPATH_INPUT_EVENT_MOUSE:
                        {
                            FastPath::MouseEvent_Recv me(cfpie.payload, byte);

                            if (this->verbose & 4) {
                                LOG(LOG_INFO,
                                    "Front::Received fast-path PUD, mouse pointerFlags=0x%X, xPos=0x%X, yPos=0x%X",
                                    me.pointerFlags, me.xPos, me.yPos);
                            }

                            this->mouse_x = me.xPos;
                            this->mouse_y = me.yPos;
                            if (this->up_and_running) {
                                if (!this->input_event_and_graphics_update_disabled) {
                                    cb.rdp_input_mouse(me.pointerFlags, me.xPos, me.yPos, &this->keymap);
                                }
                                this->has_activity = true;
                            }
                        }
                        break;

                        //case FastPath::FASTPATH_INPUT_EVENT_MOUSEX:
                        //break;

                        case FastPath::FASTPATH_INPUT_EVENT_SYNC:
                        {
                            FastPath::SynchronizeEvent_Recv se(cfpie.payload, byte);

                            if (this->verbose & 4) {
                                LOG(LOG_INFO, "Front::Received fast-path PUD, sync eventFlags=0x%X",
                                    se.eventFlags);
                            }

                            this->keymap.synchronize(se.eventFlags & 0xFFFF);
                            if (this->up_and_running) {
                                cb.rdp_input_synchronize(0, 0, se.eventFlags & 0xFFFF, 0);
                                this->has_activity = true;
                            }
                        }
                        break;

                        //case FastPath::FASTPATH_INPUT_EVENT_UNICODE:
                        //break;

                        default:
                            LOG(LOG_INFO,
                                "Front::Received unexpected fast-path PUD, eventCode = %u",
                                eventCode);
                            throw Error(ERR_RDP_FASTPATH);
                    }
                    if (this->verbose & 4) {
                        LOG(LOG_INFO, "Front::Received fast-path PUD done");
                    }
                }

                if (cfpie.payload.in_remain() != 0) {
                    LOG(LOG_WARNING, "Front::Received fast-path PUD, remains=%u",
                        cfpie.payload.in_remain());
                }
                break;
            }
            else {
//                X224::RecvFactory fx224(this->trans, stream);
                TODO("We shall put a specific case when we get Disconnect Request")
                if (fx224.type == X224::DR_TPDU) {
                    TODO("What is the clean way to actually disconnect ?")
                    X224::DR_TPDU_Recv x224(stream);
                    LOG(LOG_INFO, "Front::Received Disconnect Request from RDP client");
                    throw Error(ERR_X224_EXPECTED_DATA_PDU);
                }
                else if (fx224.type != X224::DT_TPDU) {
                    LOG(LOG_ERR, "Front::Unexpected non data PDU (got %u)", fx224.type);
                    throw Error(ERR_X224_EXPECTED_DATA_PDU);
                }

                X224::DT_TPDU_Recv x224(stream);

                int mcs_type = MCS::peekPerEncodedMCSType(x224.payload);
                if (mcs_type == MCS::MCSPDU_DisconnectProviderUltimatum) {
                    LOG(LOG_INFO, "Front::incoming::DisconnectProviderUltimatum received");
                    MCS::DisconnectProviderUltimatum_Recv mcs(x224.payload, MCS::PER_ENCODING);
                    const char * reason = MCS::get_reason(mcs.reason);
                    LOG(LOG_INFO, "Front DisconnectProviderUltimatum: reason=%s [%d]", reason, mcs.reason);
                    throw Error(ERR_MCS);
                }

                MCS::SendDataRequest_Recv mcs(x224.payload, MCS::PER_ENCODING);
                SEC::Sec_Recv sec(mcs.payload, this->decrypt, this->encryptionLevel);
                if (this->verbose & 128) {
                    LOG(LOG_INFO, "sec decrypted payload:");
                    hexdump_d(sec.payload.get_data(), sec.payload.size());
                }

                if (this->verbose & 8) {
                    LOG(LOG_INFO, "Front::incoming::sec_flags=%x", sec.flags);
                }

                if (mcs.channelId != GCC::MCS_GLOBAL_CHANNEL) {
                    if (this->verbose & 16) {
                        LOG(LOG_INFO, "Front::incoming::channel_data channelId=%u", mcs.channelId);
                    }

                    size_t num_channel_src = this->channel_list.size();
                    for (size_t index = 0; index < this->channel_list.size(); index++) {
                        if (this->channel_list[index].chanid == mcs.channelId) {
                            num_channel_src = index;
                            break;
                        }
                    }

                    if (num_channel_src >= this->channel_list.size()) {
                        LOG(LOG_ERR, "Front::incoming::Unknown Channel");
                        throw Error(ERR_CHANNEL_UNKNOWN_CHANNEL);
                    }

                    const CHANNELS::ChannelDef & channel = this->channel_list[num_channel_src];
                    if (this->verbose & 16) {
                        channel.log(mcs.channelId);
                    }

                    expected = 8; /* length(4) + flags(4) */
                    if (!sec.payload.in_check_rem(expected)) {
                        LOG(LOG_ERR, "Front::incoming::data truncated, need=%u remains=%u",
                            expected, sec.payload.in_remain());
                        throw Error(ERR_MCS);
                    }

                    uint32_t length = sec.payload.in_uint32_le();
                    uint32_t flags  = sec.payload.in_uint32_le();

                    size_t chunk_size = sec.payload.in_remain();

                    if (this->up_and_running) {
                        if (this->verbose & 16) {
                            LOG(LOG_INFO, "Front::send_to_mod_channel");
                        }

                        SubStream chunk(sec.payload, sec.payload.get_offset(), chunk_size);

                        cb.send_to_mod_channel(channel.name, chunk, length, flags);
                    }
                    else {
                        if (this->verbose & 16) {
                            LOG(LOG_INFO, "Front::not up_and_running send_to_mod_channel dropped");
                        }
                    }
                    sec.payload.p += chunk_size;
                }
                else {
                    while (sec.payload.p < sec.payload.end) {
                        ShareControl_Recv sctrl(sec.payload);

                        switch (sctrl.pduType) {
                        case PDUTYPE_DEMANDACTIVEPDU:
                            if (this->verbose & 1) {
                                LOG(LOG_INFO, "Front received DEMANDACTIVEPDU (unsupported)");
                            }
                            break;
                        case PDUTYPE_CONFIRMACTIVEPDU:
                            if (this->verbose & 1) {
                                LOG(LOG_INFO, "Front received CONFIRMACTIVEPDU");
                            }
                            {
                                expected = 6;   /* shareId(4) + originatorId(2) */
                                if (!sctrl.payload.in_check_rem(expected)) {
                                    LOG(LOG_ERR,
                                        "Truncated Confirm active PDU data, need=%u remains=%u",
                                        expected, sctrl.payload.in_remain());
                                    throw Error(ERR_RDP_DATA_TRUNCATED);
                                }

                                uint32_t share_id = sctrl.payload.in_uint32_le();
                                uint16_t originatorId = sctrl.payload.in_uint16_le();
                                this->process_confirm_active(sctrl.payload);
                                (void)share_id;
                                (void)originatorId;
                            }
                            // reset caches, etc.
                            this->reset();
                            // resizing done
                            {
                                RDPColCache cmd(0, BGRPalette::classic_332());
                                this->orders->draw(cmd);
                            }
                            if (this->verbose & 1) {
                                LOG(LOG_INFO, "Front received CONFIRMACTIVEPDU done");
                            }

                            break;
                        case PDUTYPE_DATAPDU: /* 7 */
                            if (this->verbose & 8) {
                                LOG(LOG_INFO, "Front received DATAPDU");
                            }
                            // this is rdp_process_data that will set up_and_running to 1
                            // when fonts have been received
                            // we will not exit this loop until we are in this state.
                            //LOG(LOG_INFO, "sctrl.payload.len= %u sctrl.len = %u", sctrl.payload.size(), sctrl.len);
                            this->process_data(sctrl.payload, cb);
                            if (this->verbose & 8) {
                                LOG(LOG_INFO, "Front received DATAPDU done");
                            }

                            if (!sctrl.payload.check_end())
                            {
                                LOG(LOG_ERR,
                                    "Trailing data after DATAPDU: remains=%u",
                                    sctrl.payload.in_remain());
                                throw Error(ERR_MCS_PDU_TRAILINGDATA);
                            }
                            break;
                        case PDUTYPE_DEACTIVATEALLPDU:
                            if (this->verbose & 1) {
                                LOG(LOG_INFO, "Front received DEACTIVATEALLPDU (unsupported)");
                            }
                            break;
                        case PDUTYPE_SERVER_REDIR_PKT:
                            if (this->verbose & 1) {
                                LOG(LOG_INFO, "Front received SERVER_REDIR_PKT (unsupported)");
                            }
                            break;
                        default:
                            LOG(LOG_WARNING, "Front received unknown PDU type in session_data (%d)\n", sctrl.pduType);
                            break;
                        }

                        TODO("check all sctrl.payload data is consumed")
                        sec.payload.p = sctrl.payload.p;
                    }
                }

                if (!sec.payload.check_end())
                {
                    LOG(LOG_ERR,
                        "Trailing data after SEC: remains=%u",
                        sec.payload.in_remain());
                    throw Error(ERR_SEC_TRAILINGDATA);
                }
            }
        }
        break;
        }
    }

    void send_valid_client_license_data() {
        HStream stream(1024, 65535);

        // Valid Client License Data (LICENSE_VALID_CLIENT_DATA)

        /* some compilers need unsigned char to avoid warnings */
        static uint8_t lic2[16] = {
            0xff,                   // bMsgType : ERROR_ALERT
            0x02,                   // NOT EXTENDED_ERROR_MSG_SUPPORTED, PREAMBLE_VERSION_2_0
            0x10, 0x00,             // wMsgSize: 16 bytes including preamble
            0x07, 0x00, 0x00, 0x00, // dwErrorCode : STATUS_VALID_CLIENT
            0x02, 0x00, 0x00, 0x00, // dwStateTransition ST_NO_TRANSITION
            0x28, 0x14,             // wBlobType : ignored because wBlobLen is 0
            0x00, 0x00              // wBlobLen  : 0
        };
        stream.out_copy_bytes((char *)lic2, 16);
        stream.mark_end();

        BStream sec_header(256);

        if ((this->verbose & (128 | 2)) == (128 | 2)) {
            LOG(LOG_INFO, "Sec clear payload to send:");
            hexdump_d(stream.get_data(), stream.size());
        }

        SEC::Sec_Send sec(sec_header, stream, SEC::SEC_LICENSE_PKT | 0x00100000, this->encrypt, 0);
        stream.copy_to_head(sec_header.get_data(), sec_header.size());

        this->send_data_indication(GCC::MCS_GLOBAL_CHANNEL, stream);
    }

    void send_data_indication(uint16_t channelId, HStream & stream)
    {
        BStream x224_header(256);
        OutPerBStream mcs_header(256);

        MCS::SendDataIndication_Send mcs(mcs_header, this->userid, channelId,
                                         1, 3, stream.size(),
                                         MCS::PER_ENCODING);

        X224::DT_TPDU_Send(x224_header, stream.size() + mcs_header.size());
        this->trans.send(x224_header, mcs_header, stream);
    }

    inline void send_data_indication_ex(uint16_t channelId, HStream & stream)
    {
        ::send_data_indication_ex(this->trans, this->encryptionLevel, this->encrypt, this->userid, stream);
    }

    virtual void send_fastpath_data(InStream & data) {
        HStream stream(1024, 1024 + 65536);

        stream.out_copy_bytes(data.get_data(), data.size());
        stream.mark_end();

        if (this->verbose & 4) {
            LOG(LOG_INFO, "Front::send_data: fast-path");
        }

        BStream fastpath_header(256);

        FastPath::ServerUpdatePDU_Send SvrUpdPDU(
            fastpath_header,
            stream,
            ((this->encryptionLevel > 1) ?
             FastPath::FASTPATH_OUTPUT_ENCRYPTED : 0),
            this->encrypt);
        this->trans.send(fastpath_header, stream);
    }

    virtual bool retrieve_client_capability_set(Capability & caps) {
#ifdef __clang__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdynamic-class-memaccess"
# endif
        switch (caps.capabilityType) {
            case CAPSTYPE_GENERAL:
                ::memcpy(&caps, &this->client_general_caps, sizeof(this->client_general_caps));
            break;

            case CAPSTYPE_BITMAP:
                ::memcpy(&caps, &this->client_bitmap_caps, sizeof(this->client_bitmap_caps));
            break;

            case CAPSTYPE_ORDER:
                ::memcpy(&caps, &this->client_order_caps, sizeof(this->client_order_caps));
            break;

            case CAPSTYPE_BITMAPCACHE:
                if (use_bitmapcache_rev2) {
                    return false;
                }
                ::memcpy(&caps, &this->client_bmpcache_caps, sizeof(this->client_bmpcache_caps));
            break;

            case CAPSTYPE_OFFSCREENCACHE:
                ::memcpy(&caps, &this->client_offscreencache_caps, sizeof(this->client_offscreencache_caps));
            break;

            case CAPSTYPE_BITMAPCACHE_REV2:
                if (!use_bitmapcache_rev2) {
                    return false;
                }
                ::memcpy(&caps, &this->client_bmpcache2_caps, sizeof(this->client_bmpcache2_caps));
            break;

            case CAPSTYPE_GLYPHCACHE:
                ::memcpy(&caps, &this->client_glyphcache_caps, sizeof(this->client_glyphcache_caps));
            break;
        }
#ifdef __clang__
    #pragma GCC diagnostic pop
# endif
        return true;
    }

    virtual void set_keylayout(int LCID) override {
        this->keymap.init_layout(LCID);
    }

    virtual void focus_changed(bool on_password_textbox) override {
        this->focus_on_password_textbox = on_password_textbox;
    }

    virtual void session_update(const char * message) override {
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            struct timeval now = tvtime();

            this->capture->session_update(now, message);
        }
    }

    virtual bool disable_input_event_and_graphics_update(bool disable) override {
        bool need_full_screen_update =
            (this->input_event_and_graphics_update_disabled && !disable);

            if (this->input_event_and_graphics_update_disabled != disable) {
                LOG(LOG_INFO, "Front: %s graphics update.",
                    (disable ? "Disable" : "Enable"));
            }

        this->input_event_and_graphics_update_disabled = disable;

        return need_full_screen_update;
    }

    /*****************************************************************************/
    void send_data_update_sync() throw(Error)
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "Front::send_data_update_sync");
        }

        HStream stream(1024, 65536);

        ::send_server_update( this->trans
                            , this->server_fastpath_update_support
                            , (this->ini.client.rdp_compression ? this->client_info.rdp_compression : 0)
                            , this->mppc_enc
                            , this->share_id
                            , this->encryptionLevel
                            , this->encrypt
                            , this->userid
                            , SERVER_UPDATE_GRAPHICS_SYNCHRONIZE
                            , 0
                            , stream
                            , this->verbose
                            );
    }

    /*****************************************************************************/
    void send_demand_active() throw(Error)
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "Front::send_demand_active");
        }

        size_t caps_count = 0;

        HStream stream(1024, 65536);

        // Payload
        stream.out_uint32_le(this->share_id);
        stream.out_uint16_le(4); /* 4 chars for RDP\0 */

        /* 2 bytes size after num caps, set later */
        uint32_t caps_size_offset = stream.get_offset();
        stream.out_clear_bytes(2);

        stream.out_copy_bytes("RDP", 4);

        /* 4 byte num caps, set later */
        uint32_t caps_count_offset = stream.get_offset();
        stream.out_clear_bytes(4);

        GeneralCaps general_caps;

        if (this->fastpath_support) {
            general_caps.extraflags |= FASTPATH_OUTPUT_SUPPORTED;
        }
        if (!this->server_capabilities_filename.empty()) {
            GeneralCapsLoader generalcaps_loader(general_caps);

            ConfigurationLoader cfg_loader(generalcaps_loader, this->server_capabilities_filename.c_str());
        }
        if (this->verbose) {
            general_caps.log("Sending to client");
        }
        general_caps.emit(stream);
        caps_count++;

        BitmapCaps bitmap_caps;
        bitmap_caps.preferredBitsPerPixel = this->client_info.bpp;
        bitmap_caps.desktopWidth = this->client_info.width;
        bitmap_caps.desktopHeight = this->client_info.height;
        bitmap_caps.drawingFlags = DRAW_ALLOW_SKIP_ALPHA;
        if (!this->server_capabilities_filename.empty()) {
            BitmapCapsLoader bitmapcaps_loader(bitmap_caps);

            ConfigurationLoader cfg_loader(bitmapcaps_loader, this->server_capabilities_filename.c_str());
        }
        if (this->verbose) {
            bitmap_caps.log("Sending to client");
        }
        bitmap_caps.emit(stream);
        caps_count++;

        FontCaps font_caps;
        if (this->verbose) {
            font_caps.log("Sending to client");
        }
        font_caps.emit(stream);
        caps_count++;

        OrderCaps order_caps;
        order_caps.pad4octetsA = 0x40420f00;
        order_caps.numberFonts = 0x2f;
        order_caps.orderFlags = 0x22;
        order_caps.orderSupport[TS_NEG_DSTBLT_INDEX]             = 1;
        order_caps.orderSupport[TS_NEG_PATBLT_INDEX]             = 1;
        order_caps.orderSupport[TS_NEG_SCRBLT_INDEX]             = 1;
        order_caps.orderSupport[TS_NEG_MEMBLT_INDEX]             = 1;
        order_caps.orderSupport[TS_NEG_MEM3BLT_INDEX]            = (this->mem3blt_support ? 1 : 0);
        order_caps.orderSupport[TS_NEG_LINETO_INDEX]             = 1;
        order_caps.orderSupport[TS_NEG_MULTI_DRAWNINEGRID_INDEX] = 1;
        order_caps.orderSupport[TS_NEG_POLYLINE_INDEX]           = 1;
        order_caps.orderSupport[TS_NEG_ELLIPSE_SC_INDEX]         = 1;
        order_caps.orderSupport[TS_NEG_INDEX_INDEX]              = 1;
        order_caps.orderSupport[UnusedIndex3] = 1;
        order_caps.textFlags = 0x06a1;
        order_caps.pad4octetsB = 0x0f4240;
        order_caps.desktopSaveSize = 0x0f4240;
        order_caps.pad2octetsC = 1;
        if (!this->server_capabilities_filename.empty()) {
            OrderCapsLoader ordercaps_loader(order_caps);

            ConfigurationLoader cfg_loader(ordercaps_loader, this->server_capabilities_filename.c_str());
        }
        if (this->verbose) {
            order_caps.log("Sending to client");
        }
        order_caps.emit(stream);
        caps_count++;

        if (this->ini.client.persistent_disk_bitmap_cache) {
            BitmapCacheHostSupportCaps bitmap_cache_host_support_caps;
            if (this->verbose) {
                bitmap_cache_host_support_caps.log("Sending to client");
            }
            bitmap_cache_host_support_caps.emit(stream);
            caps_count++;
        }

        ColorCacheCaps colorcache_caps;
        if (this->verbose) {
            colorcache_caps.log("Sending to client");
        }
        colorcache_caps.emit(stream);
        caps_count++;

        PointerCaps pointer_caps;
        pointer_caps.colorPointerCacheSize = 0x19;
        pointer_caps.pointerCacheSize = 0x19;
        if (this->verbose) {
            pointer_caps.log("Sending to client");
        }
        pointer_caps.emit(stream);
        caps_count++;

        ShareCaps share_caps;
        share_caps.nodeId = this->userid + GCC::MCS_USERCHANNEL_BASE;
        share_caps.pad2octets = 0xb5e2; /* 0x73e1 */
        if (this->verbose) {
            share_caps.log("Sending to client");
        }
        share_caps.emit(stream);
        caps_count++;

        InputCaps input_caps;

        // Slow/Fast-path
        input_caps.inputFlags          =
              INPUT_FLAG_SCANCODES
            | (  this->client_fastpath_input_event_support
               ? (INPUT_FLAG_FASTPATH_INPUT | INPUT_FLAG_FASTPATH_INPUT2) : 0);
        input_caps.keyboardLayout      = 0;
        input_caps.keyboardType        = 0;
        input_caps.keyboardSubType     = 0;
        input_caps.keyboardFunctionKey = 0;
        if (this->verbose) {
            input_caps.log("Sending to client");
        }
        input_caps.emit(stream);
        caps_count++;

        if (this->client_info.remote_program) {
            RailCaps rail_caps;
            rail_caps.RailSupportLevel = TS_RAIL_LEVEL_SUPPORTED;
            if (this->verbose) {
                rail_caps.log("Sending to client");
            }
            rail_caps.emit(stream);
            caps_count++;

            WindowListCaps window_list_caps;
            window_list_caps.WndSupportLevel = TS_WINDOW_LEVEL_SUPPORTED;
            window_list_caps.NumIconCaches = 3;
            window_list_caps.NumIconCacheEntries = 12;
            if (this->verbose) {
                window_list_caps.log("Sending to client");
            }
            window_list_caps.emit(stream);
            caps_count++;
        }

        size_t caps_size = stream.get_offset() - caps_count_offset;
        stream.set_out_uint16_le(caps_size, caps_size_offset);
        stream.set_out_uint32_le(caps_count, caps_count_offset);

        stream.out_clear_bytes(4); /* sessionId(4). This field is ignored by the client. */
        stream.mark_end();

        BStream sctrl_header(256);
        ShareControl_Send(sctrl_header, PDUTYPE_DEMANDACTIVEPDU, this->userid + GCC::MCS_USERCHANNEL_BASE, stream.size());

        stream.copy_to_head(sctrl_header.get_data(), sctrl_header.size());

        if ((this->verbose & (128 | 1)) == (128 | 1)) {
            LOG(LOG_INFO, "Sec clear payload to send:");
            hexdump_d(stream.get_data(), stream.size());
        }

        this->send_data_indication_ex(GCC::MCS_GLOBAL_CHANNEL, stream);
    }   // send_demand_active

    void process_confirm_active(Stream & stream)
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "process_confirm_active");
        }
        TODO("We should separate the parts relevant to caps processing and the part relevant to actual confirm active")
        TODO("Server Caps management should go to RDP layer and be unified between client (mod/rdp.hpp and server code front.hpp)")

        unsigned expected = 4; /* lengthSourceDescriptor(2) + lengthCombinedCapabilities(2) */
        if (!stream.in_check_rem(expected)) {
            LOG(LOG_ERR, "Truncated CONFIRMACTIVE PDU, need=%u remains=%u",
                expected, stream.in_remain());
            throw Error(ERR_MCS_PDU_TRUNCATED);
        }

        uint16_t lengthSourceDescriptor = stream.in_uint16_le(); /* sizeof RDP_SOURCE */
        uint16_t lengthCombinedCapabilities = stream.in_uint16_le();

        if (!stream.in_check_rem(lengthSourceDescriptor)) {
            LOG(LOG_ERR, "Truncated CONFIRMACTIVE PDU lengthSourceDescriptor, need=%u remains=%u",
                lengthSourceDescriptor, stream.in_remain());
            throw Error(ERR_MCS_PDU_TRUNCATED);
        }

        stream.in_skip_bytes(lengthSourceDescriptor);

        if (this->verbose & 1) {
            LOG(LOG_INFO, "lengthSourceDescriptor = %u", lengthSourceDescriptor);
            LOG(LOG_INFO, "lengthCombinedCapabilities = %u", lengthCombinedCapabilities);
        }

        uint8_t * start = stream.p;
        uint8_t* theoricCapabilitiesEnd = start + lengthCombinedCapabilities;
        uint8_t* actualCapabilitiesEnd = stream.end;

        expected = 4; /* numberCapabilities(2) + pad(2) */
        if (!stream.in_check_rem(expected)) {
            LOG(LOG_ERR, "Truncated CONFIRMACTIVE PDU numberCapabilities, need=%u remains=%u",
                expected, stream.in_remain());
            throw Error(ERR_MCS_PDU_TRUNCATED);
        }

        int numberCapabilities = stream.in_uint16_le();
        stream.in_skip_bytes(2); /* pad */

        for (int n = 0; n < numberCapabilities; n++) {
            if (this->verbose & 32) {
                LOG(LOG_INFO, "Front::capability %u / %u", n, numberCapabilities );
            }
            if (stream.p + 4 > theoricCapabilitiesEnd) {
                LOG(LOG_ERR, "Incomplete capabilities received (bad length): expected length=%d need=%d available=%d",
                    lengthCombinedCapabilities,
                    stream.p-start,
                    stream.in_remain());
            }
            if (stream.p + 4 > actualCapabilitiesEnd) {
                LOG(LOG_ERR, "Incomplete capabilities received (need more data): expected length=%d need=%d available=%d",
                    lengthCombinedCapabilities,
                    stream.p-start,
                    stream.in_remain());
                return;
            }

            uint16_t capset_type = stream.in_uint16_le();
            uint16_t capset_length = stream.in_uint16_le();
            uint8_t * next = (stream.p + capset_length) - 4;

            switch (capset_type) {
            case CAPSTYPE_GENERAL: {
                    this->client_general_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        this->client_general_caps.log("Receiving from client");
                    }
                    this->client_info.use_compact_packets =
                        (this->client_general_caps.extraflags & NO_BITMAP_COMPRESSION_HDR) ?
                        1 : 0;

                    this->server_fastpath_update_support =
                        (   this->fastpath_support
                         && ((this->client_general_caps.extraflags & FASTPATH_OUTPUT_SUPPORTED) != 0)
                        );
                }
                break;
            case CAPSTYPE_BITMAP: {
                    this->client_bitmap_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        this->client_bitmap_caps.log("Receiving from client");
                    }
/*
                    this->client_info.bpp    =
                          (this->client_bitmap_caps.preferredBitsPerPixel >= 24)
                        ? 24 : this->client_bitmap_caps.preferredBitsPerPixel;
*/
                    // Fixed bug in rdesktop
                    // Desktop size in Client Core Data != Desktop size in Bitmap Capability Set
                    if (!this->client_info.width || !this->client_info.height)
                    {
                        this->client_info.width  = this->client_bitmap_caps.desktopWidth;
                        this->client_info.height = this->client_bitmap_caps.desktopHeight;
                    }
                }
                break;
            case CAPSTYPE_ORDER: { /* 3 */
                    this->client_order_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        this->client_order_caps.log("Receiving from client");
                    }
                }
                break;
            case CAPSTYPE_BITMAPCACHE: {
                    this->client_bmpcache_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        this->client_bmpcache_caps.log("Receiving from client");
                    }
                    this->client_info.number_of_cache      = 3;
                    this->client_info.cache1_entries       = this->client_bmpcache_caps.cache0Entries;
                    this->client_info.cache1_persistent    = false;
                    this->client_info.cache1_size          = this->client_bmpcache_caps.cache0MaximumCellSize;
                    this->client_info.cache2_entries       = this->client_bmpcache_caps.cache1Entries;
                    this->client_info.cache2_persistent    = false;
                    this->client_info.cache2_size          = this->client_bmpcache_caps.cache1MaximumCellSize;
                    this->client_info.cache3_entries       = this->client_bmpcache_caps.cache2Entries;
                    this->client_info.cache3_persistent    = false;
                    this->client_info.cache3_size          = this->client_bmpcache_caps.cache2MaximumCellSize;
                    this->client_info.cache4_entries       = 0;
                    this->client_info.cache5_entries       = 0;
                    this->client_info.cache_flags          = 0;
                    this->client_info.bitmap_cache_version = 0;
                }
                break;
            case CAPSTYPE_CONTROL: /* 5 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_CONTROL");
                }
                break;
            case CAPSTYPE_ACTIVATION: /* 7 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_ACTIVATION");
                }
                break;
            case CAPSTYPE_POINTER: {  /* 8 */
                    PointerCaps pointer_caps;
                    pointer_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        pointer_caps.log("Receiving from client");
                    }

                    this->client_info.pointer_cache_entries =
                        std::min<int>(pointer_caps.colorPointerCacheSize, MAX_POINTER_COUNT);
                }
                break;
            case CAPSTYPE_SHARE: /* 9 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_SHARE");
                }
                break;
            case CAPSTYPE_COLORCACHE: /* 10 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_COLORCACHE");
                }
                break;
            case CAPSTYPE_SOUND:
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_SOUND");
                }
                break;
            case CAPSTYPE_INPUT: /* 13 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_INPUT");
                }
                break;
            case CAPSTYPE_FONT: /* 14 */
                break;
            case CAPSTYPE_BRUSH: { /* 15 */
                    if (this->verbose) {
                        LOG(LOG_INFO, "Receiving from client CAPSTYPE_BRUSH");
                    }
                    BrushCacheCaps brushcache_caps;
                    brushcache_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        brushcache_caps.log("Receiving from client");
                    }
                    this->client_info.brush_cache_code = brushcache_caps.brushSupportLevel;
                }
                break;
            case CAPSTYPE_GLYPHCACHE: { /* 16 */
                    if (this->verbose) {
                        LOG(LOG_INFO, "Receiving from client CAPSTYPE_GLYPHCACHE");
                    }
                    this->client_glyphcache_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        this->client_glyphcache_caps.log("Receiving from client");
                    }
                    for (uint8_t i = 0; i < NUMBER_OF_GLYPH_CACHES; ++i) {
                        this->client_info.number_of_entries_in_glyph_cache[i] =
                            this->client_glyphcache_caps.GlyphCache[i].CacheEntries;
                    }
                }
                break;
            case CAPSTYPE_OFFSCREENCACHE: /* 17 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_OFFSCREENCACHE");
                }
                this->client_offscreencache_caps.recv(stream, capset_length);
                if (this->verbose) {
                    this->client_offscreencache_caps.log("Receiving from client");
                }
                break;
            case CAPSTYPE_BITMAPCACHE_HOSTSUPPORT: /* 18 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_BITMAPCACHE_HOSTSUPPORT");
                }
                break;
            case CAPSTYPE_BITMAPCACHE_REV2: {
                    this->use_bitmapcache_rev2 = true;

                    this->client_bmpcache2_caps.recv(stream, capset_length);
                    if (this->verbose) {
                        this->client_bmpcache2_caps.log("Receiving from client");
                    }

                    TODO("We only use the first 3 caches (those existing in Rev1), we should have 2 more caches for rev2")
                    this->client_info.number_of_cache = this->client_bmpcache2_caps.numCellCaches;
                    int Bpp = nbbytes(this->client_info.bpp);
                    if (this->client_bmpcache2_caps.numCellCaches > 0) {
                        this->client_info.cache1_entries    = (this->client_bmpcache2_caps.bitmapCache0CellInfo & 0x7fffffff);
                        this->client_info.cache1_persistent = (this->client_bmpcache2_caps.bitmapCache0CellInfo & 0x80000000);
                        this->client_info.cache1_size       = 256 * Bpp;
                    }
                    else {
                        this->client_info.cache1_entries = 0;
                    }
                    if (this->client_bmpcache2_caps.numCellCaches > 1) {
                        this->client_info.cache2_entries    = (this->client_bmpcache2_caps.bitmapCache1CellInfo & 0x7fffffff);
                        this->client_info.cache2_persistent = (this->client_bmpcache2_caps.bitmapCache1CellInfo & 0x80000000);
                        this->client_info.cache2_size       = 1024 * Bpp;
                    }
                    else {
                        this->client_info.cache2_entries = 0;
                    }
                    if (this->client_bmpcache2_caps.numCellCaches > 2) {
                        this->client_info.cache3_entries    = (this->client_bmpcache2_caps.bitmapCache2CellInfo & 0x7fffffff);
                        this->client_info.cache3_persistent = (this->client_bmpcache2_caps.bitmapCache2CellInfo & 0x80000000);
                        this->client_info.cache3_size       = 4096 * Bpp;
                    }
                    else {
                        this->client_info.cache3_entries = 0;
                    }
                    if (this->client_bmpcache2_caps.numCellCaches > 3) {
                        this->client_info.cache4_entries    = (this->client_bmpcache2_caps.bitmapCache3CellInfo & 0x7fffffff);
                        this->client_info.cache4_persistent = (this->client_bmpcache2_caps.bitmapCache3CellInfo & 0x80000000);
                        this->client_info.cache4_size       = 6144 * Bpp;
                    }
                    else {
                        this->client_info.cache4_entries = 0;
                    }
                    if (this->client_bmpcache2_caps.numCellCaches > 4) {
                        this->client_info.cache5_entries    = (this->client_bmpcache2_caps.bitmapCache4CellInfo & 0x7fffffff);
                        this->client_info.cache5_persistent = (this->client_bmpcache2_caps.bitmapCache4CellInfo & 0x80000000);
                        this->client_info.cache5_size       = 8192 * Bpp;
                    }
                    else {
                        this->client_info.cache5_entries = 0;
                    }
                    this->client_info.cache_flags          = this->client_bmpcache2_caps.cacheFlags;
                    this->client_info.bitmap_cache_version = 2;
                }
                break;
            case CAPSTYPE_VIRTUALCHANNEL: /* 20 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_VIRTUALCHANNEL");
                }
                break;
            case CAPSTYPE_DRAWNINEGRIDCACHE: /* 21 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_DRAWNINEGRIDCACHE");
                }
                break;
            case CAPSTYPE_DRAWGDIPLUS: /* 22 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSTYPE_DRAWGDIPLUS");
                }
                break;
            case CAPSTYPE_RAIL: { /* 23 */
                    RailCaps cap;
                    cap.recv(stream, capset_length);
                    if (this->verbose) {
                        cap.log("Receiving from client");
                    }
                }
                break;
            case CAPSTYPE_WINDOW: { /* 24 */
                    WindowListCaps cap;
                    cap.recv(stream, capset_length);
                    if (this->verbose) {
                        cap.log("Receiving from client");
                    }
                }
                break;
            case CAPSETTYPE_COMPDESK: { /* 25 */
                    CompDeskCaps cap;
                    cap.recv(stream, capset_length);
                    if (this->verbose) {
                        cap.log("Receiving from client");
                    }
                }
                break;
            case CAPSETTYPE_MULTIFRAGMENTUPDATE: { /* 26 */
                    MultiFragmentUpdateCaps cap;
                    cap.recv(stream, capset_length);
                    if (this->verbose) {
                        cap.log("Receiving from client");
                    }
                }
                break;
            case CAPSETTYPE_LARGE_POINTER: /* 27 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSETTYPE_LARGE_POINTER");
                }
                break;
            case CAPSETTYPE_SURFACE_COMMANDS: /* 28 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSETTYPE_SURFACE_COMMANDS");
                }
                break;
            case CAPSETTYPE_BITMAP_CODECS: /* 29 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSETTYPE_BITMAP_CODECS");
                }
                break;
            case CAPSETTYPE_FRAME_ACKNOWLEDGE: /* 30 */
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client CAPSETTYPE_FRAME_ACKNOWLEDGE");
                }
                break;
            default:
                if (this->verbose) {
                    LOG(LOG_INFO, "Receiving from client unknown caps %u", capset_type);
                }
                break;
            }
            if (stream.p > next) {
                LOG(LOG_ERR, "read out of bound detected");
                throw Error(ERR_MCS);
            }
            stream.p = next;
        }
        // After Capabilities read optional SessionId
        if (stream.in_remain() >= 4) {
            // From the documentation SessionId is ignored by client.
            stream.in_skip_bytes(4); /* Session Id */
        }
        if (this->verbose & 1) {
            LOG(LOG_INFO, "process_confirm_active done p=%p end=%p", stream.p, stream.end);
        }
    }

// 2.2.1.19 Server Synchronize PDU
// ===============================

// The Server Synchronize PDU is an RDP Connection Sequence PDU sent from server
// to client during the Connection Finalization phase (see section 1.3.1.1). It
// is sent after receiving the Confirm Active PDU (section 2.2.1.13.2).

// tpktHeader (4 bytes): A TPKT Header, as specified in [T123] section 8.

// x224Data (3 bytes): An X.224 Class 0 Data TPDU, as specified in [X224] section 13.7.

// mcsSDin (variable): Variable-length PER-encoded MCS Domain PDU which
//   encapsulates an MCS Send Data Indication structure, as specified in [T125]
//   (the ASN.1 structure definitions are given in section 7, parts 7 and 10 of
//   [T125]). The userData field of the MCS Send Data Indication contains a
//   Security Header and the Synchronize PDU Data (section 2.2.1.14.1).

// securityHeader (variable): Optional security header. If the Encryption Level
//   (sections 5.3.2 and 2.2.1.4.3) selected by the server is greater than
//   ENCRYPTION_LEVEL_NONE (0) and the Encryption Method (sections 5.3.2 and
//   2.2.1.4.3) selected by the server is greater than ENCRYPTION_METHOD_NONE
//   (0) then this field will contain one of the following headers:

//   - Basic Security Header (section 2.2.8.1.1.2.1) if the Encryption Level
//     selected by the server (see sections 5.3.2 and 2.2.1.4.3) is
//     ENCRYPTION_LEVEL_LOW (1).

//  - Non-FIPS Security Header (section 2.2.8.1.1.2.2) if the Encryption Level
//    selected by the server (see sections 5.3.2 and 2.2.1.4.3) is
//    ENCRYPTION_LEVEL_CLIENT_COMPATIBLE (2), or ENCRYPTION_LEVEL_HIGH (3).

//  - FIPS Security Header (section 2.2.8.1.1.2.3) if the Encryption Level
//    selected by the server (see sections 5.3.2 and 2.2.1.4.3) is
//    ENCRYPTION_LEVEL_FIPS (4).

// If the Encryption Level (sections 5.3.2 and 2.2.1.4.3) selected by the server
// is ENCRYPTION_LEVEL_NONE (0) and the Encryption Method (sections 5.3.2 and
// 2.2.1.4.3) selected by the server is ENCRYPTION_METHOD_NONE (0), then this
// header is not include " in the PDU.

// synchronizePduData (22 bytes): The contents of the Synchronize PDU as
// described in section 2.2.1.14.1.

// 2.2.1.14.1 Synchronize PDU Data (TS_SYNCHRONIZE_PDU)
// ====================================================
// The TS_SYNCHRONIZE_PDU structure is a standard T.128 Synchronize PDU (see
// [T128] section 8.6.1).

// shareDataHeader (18 bytes): Share Control Header (section 2.2.8.1.1.1.1)
//   containing information about the packet. The type subfield of the pduType
//   field of the Share Control Header MUST be set to PDUTYPE_DATAPDU (7). The
//   pduType2 field of the Share Data Header MUST be set to PDUTYPE2_SYNCHRONIZE
//   (31).

// messageType (2 bytes): A 16-bit, unsigned integer. The message type. This
//   field MUST be set to SYNCMSGTYPE_SYNC (1).

// targetUser (2 bytes): A 16-bit, unsigned integer. The MCS channel ID of the
//   target user.

    TODO(" duplicated code in mod/rdp")
    void send_synchronize()
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_synchronize");
        }

        HStream stream(1024, 2048);
        // Payload
        stream.out_uint16_le(1);    // messageType = SYNCMSGTYPE_SYNC(1)
        stream.out_uint16_le(1002); // targetUser (MCS channel ID of the target user.)
        stream.mark_end();

        const uint32_t log_condition = (128 | 1);
        ::send_share_data_ex( this->trans
                            , PDUTYPE2_SYNCHRONIZE
                            , false
                            , this->mppc_enc
                            , this->share_id
                            , this->encryptionLevel
                            , this->encrypt
                            , this->userid
                            , stream
                            , log_condition
                            , this->verbose
                            );

        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_synchronize done");
        }
    }

// 2.2.1.15.1 Control PDU Data (TS_CONTROL_PDU)
// ============================================

// The TS_CONTROL_PDU structure is a standard T.128 Synchronize PDU (see [T128]
// section 8.12).

// shareDataHeader (18 bytes): Share Data Header (section 2.2.8.1.1.1.2)
//   containing information about the packet. The type subfield of the pduType
//   field of the Share Control Header (section 2.2.8.1.1.1.1) MUST be set to
//   PDUTYPE_DATAPDU (7). The pduType2 field of the Share Data Header MUST be set
//   to PDUTYPE2_CONTROL (20).

// action (2 bytes): A 16-bit, unsigned integer. The action code.
// 0x0001 CTRLACTION_REQUEST_CONTROL Request control
// 0x0002 CTRLACTION_GRANTED_CONTROL Granted control
// 0x0003 CTRLACTION_DETACH Detach
// 0x0004 CTRLACTION_COOPERATE Cooperate

// grantId (2 bytes): A 16-bit, unsigned integer. The grant identifier.

// controlId (4 bytes): A 32-bit, unsigned integer. The control identifier.

    void send_control(int action)
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_control action=%u", action);
        }

        HStream stream(1024, 2048);

        // Payload
        stream.out_uint16_le(action);
        stream.out_uint16_le(0); // userid
        stream.out_uint32_le(1002); // control id
        stream.mark_end();

        const uint32_t log_condition = (128 | 1);
        ::send_share_data_ex( this->trans
                            , PDUTYPE2_CONTROL
                            , false
                            , this->mppc_enc
                            , this->share_id
                            , this->encryptionLevel
                            , this->encrypt
                            , this->userid
                            , stream
                            , log_condition
                            , this->verbose
                            );

        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_control done. action=%u", action);
        }
    }

    /*****************************************************************************/
    void send_fontmap() throw(Error)
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_fontmap");
        }

        static uint8_t g_fontmap[172] = { 0xff, 0x02, 0xb6, 0x00, 0x28, 0x00, 0x00, 0x00,
                                          0x27, 0x00, 0x27, 0x00, 0x03, 0x00, 0x04, 0x00,
                                          0x00, 0x00, 0x26, 0x00, 0x01, 0x00, 0x1e, 0x00,
                                          0x02, 0x00, 0x1f, 0x00, 0x03, 0x00, 0x1d, 0x00,
                                          0x04, 0x00, 0x27, 0x00, 0x05, 0x00, 0x0b, 0x00,
                                          0x06, 0x00, 0x28, 0x00, 0x08, 0x00, 0x21, 0x00,
                                          0x09, 0x00, 0x20, 0x00, 0x0a, 0x00, 0x22, 0x00,
                                          0x0b, 0x00, 0x25, 0x00, 0x0c, 0x00, 0x24, 0x00,
                                          0x0d, 0x00, 0x23, 0x00, 0x0e, 0x00, 0x19, 0x00,
                                          0x0f, 0x00, 0x16, 0x00, 0x10, 0x00, 0x15, 0x00,
                                          0x11, 0x00, 0x1c, 0x00, 0x12, 0x00, 0x1b, 0x00,
                                          0x13, 0x00, 0x1a, 0x00, 0x14, 0x00, 0x17, 0x00,
                                          0x15, 0x00, 0x18, 0x00, 0x16, 0x00, 0x0e, 0x00,
                                          0x18, 0x00, 0x0c, 0x00, 0x19, 0x00, 0x0d, 0x00,
                                          0x1a, 0x00, 0x12, 0x00, 0x1b, 0x00, 0x14, 0x00,
                                          0x1f, 0x00, 0x13, 0x00, 0x20, 0x00, 0x00, 0x00,
                                          0x21, 0x00, 0x0a, 0x00, 0x22, 0x00, 0x06, 0x00,
                                          0x23, 0x00, 0x07, 0x00, 0x24, 0x00, 0x08, 0x00,
                                          0x25, 0x00, 0x09, 0x00, 0x26, 0x00, 0x04, 0x00,
                                          0x27, 0x00, 0x03, 0x00, 0x28, 0x00, 0x02, 0x00,
                                          0x29, 0x00, 0x01, 0x00, 0x2a, 0x00, 0x05, 0x00,
                                          0x2b, 0x00, 0x2a, 0x00
                                        };

        HStream stream(1024, 4096);

        // Payload
        stream.out_copy_bytes((char*)g_fontmap, 172);
        stream.mark_end();

        const uint32_t log_condition = (128 | 1);
        ::send_share_data_ex( this->trans
                            , PDUTYPE2_FONTMAP
                            , false
                            , this->mppc_enc
                            , this->share_id
                            , this->encryptionLevel
                            , this->encrypt
                            , this->userid
                            , stream
                            , log_condition
                            , this->verbose
                            );

        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_fontmap done");
        }
    }

    /* PDUTYPE_DATAPDU */
    void process_data(Stream & stream, Callback & cb) throw(Error)
    {
        unsigned expected;
        if (this->verbose & 8) {
            LOG(LOG_INFO, "Front::process_data(...)");
        }
        ShareData_Recv sdata_in(stream, nullptr);
        if (this->verbose & 8) {
            LOG(LOG_INFO, "sdata_in.pdutype2=%u"
                          " sdata_in.len=%u"
                          " sdata_in.compressedLen=%u"
                          " remains=%u"
                          " payload_len=%u",
                (unsigned)sdata_in.pdutype2,
                (unsigned)sdata_in.len,
                (unsigned)sdata_in.compressedLen,
                (unsigned)(stream.in_remain()),
                (unsigned)(sdata_in.payload.size())
            );
        }

        switch (sdata_in.pdutype2) {
        case PDUTYPE2_UPDATE:  // Update PDU (section 2.2.9.1.1.3)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_UPDATE");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_CONTROL: // 20(0x14) Control PDU (section 2.2.1.15.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_CONTROL");
            }
            {
                expected = 8;   /* action(2) + grantId(2) + controlId(4) */
                if (!sdata_in.payload.in_check_rem(expected)) {
                    LOG(LOG_ERR, "Truncated Control PDU data, need=%u remains=%u",
                        expected, sdata_in.payload.in_remain());
                    throw Error(ERR_RDP_DATA_TRUNCATED);
                }

                int action = sdata_in.payload.in_uint16_le();
                sdata_in.payload.in_skip_bytes(2); /* user id */
                sdata_in.payload.in_skip_bytes(4); /* control id */
                switch (action) {
                    case RDP_CTL_REQUEST_CONTROL:
                        this->send_control(RDP_CTL_GRANT_CONTROL);
                    break;
                    case RDP_CTL_COOPERATE:
                        this->send_control(RDP_CTL_COOPERATE);
                    break;
                    default:
                        LOG(LOG_WARNING, "process DATA_PDU_CONTROL unknown action (%d)\n", action);
                }
            }
            break;
        case PDUTYPE2_POINTER: // Pointer Update PDU (section 2.2.9.1.1.4)
            if (this->verbose & 4) {
                LOG(LOG_INFO, "PDUTYPE2_POINTER");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_INPUT:   // 28(0x1c) Input PDU (section 2.2.8.1.1.3)
            {
                SlowPath::ClientInputEventPDU_Recv cie(sdata_in.payload);

                if (this->verbose & 4) {
                    LOG(LOG_INFO, "PDUTYPE2_INPUT num_events=%u", cie.numEvents);
                }

                for (int index = 0; index < cie.numEvents; index++) {
                    SlowPath::InputEvent_Recv ie(cie.payload);

                    TODO(" we should always call send_input with original data  if the other side is rdp it will merely transmit it to the other end without change. If the other side is some internal module it will be it's own responsibility to decode it")
                    TODO(" with the scheme above  any kind of keymap management is only necessary for internal modules or if we convert mapping. But only the back-end module really knows what the target mapping should be.")
                    switch (ie.messageType) {
                        case SlowPath::INPUT_EVENT_SYNC:
                        {
                            SlowPath::SynchronizeEvent_Recv se(ie.payload);

                            if (this->verbose & 4) {
                                LOG(LOG_INFO, "SlowPath INPUT_EVENT_SYNC eventTime=%u toggleFlags=0x%04X",
                                    ie.eventTime, se.toggleFlags);
                            }
                            // happens when client gets focus and sends key modifier info
                            this->keymap.synchronize(se.toggleFlags & 0xFFFF);
                            if (this->up_and_running) {
                                cb.rdp_input_synchronize(ie.eventTime, 0, se.toggleFlags & 0xFFFF, (se.toggleFlags & 0xFFFF0000) >> 16);
                                this->has_activity = true;
                            }
                        }
                        break;

                        case SlowPath::INPUT_EVENT_MOUSE:
                        {
                            SlowPath::MouseEvent_Recv me(ie.payload);

                            if (this->verbose & 4) {
                                LOG(LOG_INFO, "Slow-path INPUT_EVENT_MOUSE eventTime=%u pointerFlags=0x%04X, xPos=%u, yPos=%u)",
                                    ie.eventTime, me.pointerFlags, me.xPos, me.yPos);
                            }
                            this->mouse_x = me.xPos;
                            this->mouse_y = me.yPos;
                            if (this->up_and_running) {
                                if (!this->input_event_and_graphics_update_disabled) {
                                    cb.rdp_input_mouse(me.pointerFlags, me.xPos, me.yPos, &this->keymap);
                                }
                                this->has_activity = true;
                            }
                        }
                        break;

                        case SlowPath::INPUT_EVENT_SCANCODE:
                        {
                            SlowPath::KeyboardEvent_Recv ke(ie.payload);

                            if (this->verbose & 4) {
                                LOG(LOG_INFO, "Slow-path INPUT_EVENT_SYNC eventTime=%u keyboardFlags=0x%04X keyCode=0x%04X",
                                    ie.eventTime, ke.keyboardFlags, ke.keyCode);
                            }

                            BStream decoded_data(256);
                            bool    tsk_switch_shortcuts;

                            this->keymap.event(ke.keyboardFlags, ke.keyCode, decoded_data, tsk_switch_shortcuts);
                            decoded_data.mark_end();
                            //LOG(LOG_INFO, "Decoded keyboard input data:");
                            //hexdump_d(decoded_data.get_data(), decoded_data.size());

                            bool send_to_mod = true;

                            if (  this->capture
                               && (this->capture_state == CAPTURE_STATE_STARTED)
                               && decoded_data.size()) {
                                if (this->focus_on_password_textbox) {
                                    unsigned char_count = decoded_data.size() / sizeof(uint32_t);
                                    decoded_data.reset();
                                    for (; char_count > 0; char_count--) {
                                        // Unicode Character 'BLACK CIRCLE' (U+25CF).
                                        decoded_data.out_uint32_le(0x25CF);
                                    }
                                    decoded_data.mark_end();
                                }

                                struct timeval now = tvtime();

                                send_to_mod = this->capture->input(now, decoded_data);
                            }

                            if (this->up_and_running) {
                                if (tsk_switch_shortcuts && this->ini.client.disable_tsk_switch_shortcuts.get()) {
                                    LOG(LOG_INFO, "Ctrl+Alt+Del and Ctrl+Shift+Esc keyboard sequences ignored.");
                                }
                                else {
                                    if (!this->input_event_and_graphics_update_disabled &&
                                        send_to_mod) {
                                        cb.rdp_input_scancode(ke.keyCode, 0, ke.keyboardFlags, ie.eventTime, &this->keymap);
                                    }
                                    this->has_activity = true;
                                }
                            }
                        }
                        break;

                        default:
                            LOG(LOG_WARNING, "unsupported PDUTYPE2_INPUT message type %u", ie.messageType);
                        break;
                    }
                }
                if (this->verbose & 4) {
                    LOG(LOG_INFO, "PDUTYPE2_INPUT done");
                }
            }
        break;
        case PDUTYPE2_SYNCHRONIZE:  // Synchronize PDU (section 2.2.1.14.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_SYNCHRONIZE");
            }
            {
                expected = 4;   /* messageType(2) + targetUser(4) */
                if (!sdata_in.payload.in_check_rem(expected)) {
                    LOG(LOG_ERR, "Truncated Synchronize PDU data, need=%u remains=%u",
                        expected, sdata_in.payload.in_remain());
                    throw Error(ERR_RDP_DATA_TRUNCATED);
                }

                uint16_t messageType = sdata_in.payload.in_uint16_le();
                uint16_t controlId = sdata_in.payload.in_uint16_le();
                if (this->verbose & 8) {
                    LOG(LOG_INFO, "PDUTYPE2_SYNCHRONIZE"
                                  " messageType=%u controlId=%u",
                                  (unsigned)messageType,
                                  static_cast<unsigned>(controlId));
                }
                this->send_synchronize();
            }
        break;
        case PDUTYPE2_REFRESH_RECT: // Refresh Rect PDU (section 2.2.11.2.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_REFRESH_RECT");
            }
            // numberOfAreas (1 byte): An 8-bit, unsigned integer. The number of Inclusive Rectangle
            // (section 2.2.11.1) structures in the areasToRefresh field.

            // pad3Octects (3 bytes): A 3-element array of 8-bit, unsigned integer values. Padding.
            // Values in this field MUST be ignored.

            // areasToRefresh (variable): An array of TS_RECTANGLE16 structures (variable number of
            // bytes). Array of screen area Inclusive Rectangles to redraw. The number of rectangles
            // is given by the numberOfAreas field.

            // 2.2.11.1 Inclusive Rectangle (TS_RECTANGLE16)
            // =============================================
            // The TS_RECTANGLE16 structure describes a rectangle expressed in inclusive coordinates
            // (the right and bottom coordinates are include " in the rectangle bounds).
            // left (2 bytes): A 16-bit, unsigned integer. The leftmost bound of the rectangle.
            // top (2 bytes): A 16-bit, unsigned integer. The upper bound of the rectangle.
            // right (2 bytes): A 16-bit, unsigned integer. The rightmost bound of the rectangle.
            // bottom (2 bytes): A 16-bit, unsigned integer. The lower bound of the rectangle.

            {
                expected = 4;   /* numberOfAreas(1) + pad3Octects(3) */
                if (!sdata_in.payload.in_check_rem(expected)) {
                    LOG(LOG_ERR, "Truncated Refresh rect PDU data, need=%u remains=%u",
                        expected, sdata_in.payload.in_remain());
                    throw Error(ERR_RDP_DATA_TRUNCATED);
                }

                size_t numberOfAreas = sdata_in.payload.in_uint8();
                sdata_in.payload.in_skip_bytes(3);

                expected = numberOfAreas * 8;   /* numberOfAreas * (left(2) + top(2) + right(2) + bottom(2)) */
                if (!sdata_in.payload.in_check_rem(expected)) {
                    LOG(LOG_ERR, "Truncated Refresh rect PDU data, need=%u remains=%u",
                        expected, sdata_in.payload.in_remain());
                    throw Error(ERR_RDP_DATA_TRUNCATED);
                }

                DArray<Rect> rects(numberOfAreas);
                for (size_t i = 0; i < numberOfAreas ; i++) {

                    int left = sdata_in.payload.in_uint16_le();
                    int top = sdata_in.payload.in_uint16_le();
                    int right = sdata_in.payload.in_uint16_le();
                    int bottom = sdata_in.payload.in_uint16_le();
                    Rect rect(left, top, (right - left) + 1, (bottom - top) + 1);
                    rects[i] = rect;
                    if (this->verbose & (64|4)) {
                        LOG(LOG_INFO, "PDUTYPE2_REFRESH_RECT"
                            " left=%u top=%u right=%u bottom=%u cx=%u cy=%u",
                            left, top, right, bottom, rect.x, rect.cy);
                    }
                    // TODO("we should consider adding to API some function to refresh several rects at once")
                    // if (this->up_and_running) {
                    //     cb.rdp_input_invalidate(rect);
                    // }
                }
                cb.rdp_input_invalidate2(rects);
            }
        break;
        case PDUTYPE2_PLAY_SOUND:   // Play Sound PDU (section 2.2.9.1.1.5.1):w
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_PLAY_SOUND");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_SUPPRESS_OUTPUT:  // Suppress Output PDU (section 2.2.11.3.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_SUPPRESS_OUTPUT");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;

            // PDUTYPE2_SUPPRESS_OUTPUT comes when minimizing a full screen
            // mstsc.exe 2600. I think this is saying the client no longer wants
            // screen updates and it will issue a PDUTYPE2_REFRESH_RECT above
            // to catch up so minimized apps don't take bandwidth
            break;

        break;
        case PDUTYPE2_SHUTDOWN_REQUEST: // Shutdown Request PDU (section 2.2.2.2.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_SHUTDOWN_REQUEST");
            }
            {
                // when this message comes, send a PDUTYPE2_SHUTDOWN_DENIED back
                // so the client is sure the connection is alive and it can ask
                // if user really wants to disconnect

                HStream stream(1024, 2048);

                // Payload
                stream.mark_end();

                const uint32_t log_condition = (128 | 8);
                ::send_share_data_ex( this->trans
                                    , PDUTYPE2_SHUTDOWN_DENIED
                                    , (this->ini.client.rdp_compression ? this->client_info.rdp_compression : 0)
                                    , this->mppc_enc
                                    , this->share_id
                                    , this->encryptionLevel
                                    , this->encrypt
                                    , this->userid
                                    , stream
                                    , log_condition
                                    , this->verbose
                                    );
            }
        break;
        case PDUTYPE2_SHUTDOWN_DENIED:  // Shutdown Request Denied PDU (section 2.2.2.3.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_SHUTDOWN_DENIED");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_SAVE_SESSION_INFO: // Save Session Info PDU (section 2.2.10.1.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_SAVE_SESSION_INFO");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_FONTLIST: // 39(0x27) Font List PDU (section 2.2.1.18.1)
        {
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_FONTLIST");
            }
        // 2.2.1.18.1 Font List PDU Data (TS_FONT_LIST_PDU)
        // ================================================
        // The TS_FONT_LIST_PDU structure contains the contents of the Font
        // List PDU, which is a Share Data Header (section 2.2.8.1.1.1.2) and
        // four fields.

        // shareDataHeader (18 bytes): Share Data Header (section 2.2.8.1.1.1.2)
        // containing information about the packet. The type subfield of the
        // pduType field of the Share Control Header (section 2.2.8.1.1.1.1)
        // MUST be set to PDUTYPE_DATAPDU (7). The pduType2 field of the Share
        // Data Header MUST be set to PDUTYPE2_FONTLIST (39).

        // numberFonts (2 bytes): A 16-bit, unsigned integer. The number of
        // fonts. This field SHOULD be set to 0.

        // totalNumFonts (2 bytes): A 16-bit, unsigned integer. The total number
        // of fonts. This field SHOULD be set to 0.

        // listFlags (2 bytes): A 16-bit, unsigned integer. The sequence flags.
        // This field SHOULD be set to 0x0003, which is the logical OR'ed value
        // of FONTLIST_FIRST (0x0001) and FONTLIST_LAST (0x0002).

        // entrySize (2 bytes): A 16-bit, unsigned integer. The entry size. This
        // field SHOULD be set to 0x0032 (50 bytes).

            expected = 8;   /* numberFonts(2) + totalNumFonts(2) + listFlags(2) + entrySize(2) */
            if (!sdata_in.payload.in_check_rem(expected)) {
                LOG(LOG_ERR, "Truncated Font list PDU data, need=%u remains=%u",
                    expected, sdata_in.payload.in_remain());
                throw Error(ERR_RDP_DATA_TRUNCATED);
            }

            sdata_in.payload.in_uint16_le(); /* numberFont -> 0*/
            sdata_in.payload.in_uint16_le(); /* totalNumFonts -> 0 */
            int seq = sdata_in.payload.in_uint16_le();
            sdata_in.payload.in_uint16_le(); /* entrySize -> 50 */

            /* 419 client sends Seq 1, then 2 */
            /* 2600 clients sends only Seq 3 */
            /* after second font message, we are up and running */
            if (seq == 2 || seq == 3)
            {
                this->send_fontmap();
                this->send_data_update_sync();

                if (this->client_info.bpp == 8) {
                    RDPColCache cmd(0, BGRPalette::classic_332());
                    this->orders->draw(cmd);
                }

                if (this->verbose & (8|1)) {
                    LOG(LOG_INFO, "--------------> UP AND RUNNING <----------------");
                }
                this->up_and_running = 1;
                cb.rdp_input_up_and_running();
                TODO("we should use accessors to set that, also not sure it's the right place to set it")
                this->ini.context.opt_width.set(this->client_info.width);
                this->ini.context.opt_height.set(this->client_info.height);
                this->ini.context.opt_bpp.set(this->client_info.bpp);

                if (!this->auth_info_sent) {
                    char         username_a_domain[512];
                    const char * username;
                    if (this->client_info.domain[0] &&
                        !strchr(this->client_info.username, '@') &&
                        !strchr(this->client_info.username, '\\')) {
                        snprintf(username_a_domain, sizeof(username_a_domain), "%s@%s", this->client_info.username, this->client_info.domain);
                        username = username_a_domain;
                    }
                    else {
                        username = this->client_info.username;
                    }
                    this->ini.parse_username(username);
                    if (this->client_info.password[0]) {
                        this->ini.context_set_value(AUTHID_PASSWORD, this->client_info.password);
                    }

                    this->auth_info_sent = true;
                }
            }
        }
        break;
        case PDUTYPE2_FONTMAP:  // Font Map PDU (section 2.2.1.22.1)
            if (this->verbose & (8 | 1)) {
                LOG(LOG_INFO, "PDUTYPE2_FONTMAP");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_SET_KEYBOARD_INDICATORS: // Set Keyboard Indicators PDU (section 2.2.8.2.1.1)
            if (this->verbose & (4 | 8)) {
                LOG(LOG_INFO, "PDUTYPE2_SET_KEYBOARD_INDICATORS");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_BITMAPCACHE_PERSISTENT_LIST: // Persistent Key List PDU (section 2.2.1.17.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_BITMAPCACHE_PERSISTENT_LIST");
            }

            if (this->ini.client.persistent_disk_bitmap_cache &&
                this->orders.bmp_cache_persister()) {
                RDP::PersistentKeyListPDUData pklpdud;

                pklpdud.receive(sdata_in.payload);
                if (this->verbose & 8) {
                    pklpdud.log(LOG_INFO, "Receiving from client");
                }

                static uint16_t cache_entry_index[BmpCache::MAXIMUM_NUMBER_OF_CACHES] = { 0, 0, 0, 0, 0 };

                RDP::BitmapCachePersistentListEntry * entries = pklpdud.entries;

                for (unsigned i = 0; i < BmpCache::MAXIMUM_NUMBER_OF_CACHES; ++i) {
                    if (pklpdud.numEntriesCache[i]) {
                        this->orders.bmp_cache_persister()->process_key_list(
                            i, entries, pklpdud.numEntriesCache[i] , cache_entry_index[i]
                        );
                        entries              += pklpdud.numEntriesCache[i];
                        cache_entry_index[i] += pklpdud.numEntriesCache[i];
                    }
                }

                if (this->persistent_key_list_transport) {
                    BStream persistent_key_list_stream(65535);

                    uint16_t pdu_size_offset = persistent_key_list_stream.get_offset();
                    persistent_key_list_stream.out_clear_bytes(2);  // Size of Persistent Key List PDU.

                    pklpdud.emit(persistent_key_list_stream);
                    persistent_key_list_stream.mark_end();

                    persistent_key_list_stream.set_out_uint16_le(
                          persistent_key_list_stream.size() - 2 /* Size of Persistent Key List PDU(2) */
                        , pdu_size_offset);

                    this->persistent_key_list_transport->send(persistent_key_list_stream);
                }

                if (pklpdud.bBitMask & RDP::PERSIST_LAST_PDU) {
                    this->orders.clear_bmp_cache_persister();
                }
            }

            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_BITMAPCACHE_ERROR_PDU: // Bitmap Cache Error PDU (see [MS-RDPEGDI] section 2.2.2.3.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_BITMAPCACHE_ERROR_PDU");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_SET_KEYBOARD_IME_STATUS: // Set Keyboard IME Status PDU (section 2.2.8.2.2.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_SET_KEYBOARD_IME_STATUS");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_OFFSCRCACHE_ERROR_PDU: // Offscreen Bitmap Cache Error PDU (see [MS-RDPEGDI] section 2.2.2.3.2)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_OFFSCRCACHE_ERROR_PDU");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_SET_ERROR_INFO_PDU: // Set Error Info PDU (section 2.2.5.1.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_SET_ERROR_INFO_PDU");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_DRAWNINEGRID_ERROR_PDU: // DrawNineGrid Cache Error PDU (see [MS-RDPEGDI] section 2.2.2.3.3)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_DRAWNINEGRID_ERROR_PDU");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_DRAWGDIPLUS_ERROR_PDU: // GDI+ Error PDU (see [MS-RDPEGDI] section 2.2.2.3.4)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_DRAWGDIPLUS_ERROR_PDU");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;
        case PDUTYPE2_ARC_STATUS_PDU: // Auto-Reconnect Status PDU (section 2.2.4.1.1)
            if (this->verbose & 8) {
                LOG(LOG_INFO, "PDUTYPE2_ARC_STATUS_PDU");
            }
            TODO("this quickfix prevents a tech crash, but consuming the data should be a better behaviour")
            sdata_in.payload.p = sdata_in.payload.end;
        break;

        default:
            LOG(LOG_WARNING, "unsupported PDUTYPE in process_data %d\n", sdata_in.pdutype2);
            break;
        }

        stream.p = sdata_in.payload.p;

        if (this->verbose & (4 | 8)) {
            LOG(LOG_INFO, "process_data done");
        }
    }

    void send_deactive() throw(Error)
    {
        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_deactive");
        }

        HStream stream(1024, 1024 + 256);
        ShareControl_Send(stream, PDUTYPE_DEACTIVATEALLPDU, this->userid + GCC::MCS_USERCHANNEL_BASE, 0);

        if ((this->verbose & (128 | 1)) == (128 | 1)) {
            LOG(LOG_INFO, "Sec clear payload to send:");
            hexdump_d(stream.get_data(), stream.size());
        }

        this->send_data_indication_ex(GCC::MCS_GLOBAL_CHANNEL, stream);

        if (this->verbose & 1) {
            LOG(LOG_INFO, "send_deactive done");
        }
    }

    void draw(const RDPOpaqueRect & cmd, const Rect & clip) {
        if (!clip.isempty() && !clip.intersect(cmd.rect).isempty()) {
            this->send_global_palette();

            if (!this->input_event_and_graphics_update_disabled) {
                if (this->client_info.bpp != this->mod_bpp) {
                    RDPOpaqueRect new_cmd = cmd;

                    const BGRColor color24 = color_decode_opaquerect(cmd.color, this->mod_bpp, this->mod_palette_rgb);
                    new_cmd.color = color_encode(color24, this->client_info.bpp);

                    this->orders->draw(new_cmd, clip);
                }
                else {
                    this->orders->draw(cmd, clip);
                }
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPOpaqueRect capture_cmd = cmd;

                    const BGRColor color24 = color_decode_opaquerect(cmd.color, this->mod_bpp, this->mod_palette_rgb);
                    capture_cmd.color = color_encode(color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDPScrBlt & cmd, const Rect & clip)
    {
        if (!clip.isempty() && !clip.intersect(cmd.rect).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                this->orders->draw(cmd, clip);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                this->capture->draw(cmd, clip);
            }
        }
    }

    void draw(const RDPDestBlt & cmd, const Rect & clip)
    {
        if (!clip.isempty() && !clip.intersect(cmd.rect).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                this->orders->draw(cmd, clip);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                this->capture->draw(cmd, clip);
            }
        }
    }

    void draw(const RDPMultiDstBlt & cmd, const Rect & clip) {
        if (!clip.isempty() &&
            !clip.intersect(Rect(cmd.nLeftRect, cmd.nTopRect, cmd.nWidth, cmd.nHeight)).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                this->orders->draw(cmd, clip);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                this->capture->draw(cmd, clip);
            }
        }
    }

    void draw(const RDPMultiOpaqueRect & cmd, const Rect & clip) {
        if (!clip.isempty() &&
            !clip.intersect(Rect(cmd.nLeftRect, cmd.nTopRect, cmd.nWidth, cmd.nHeight)).isempty()) {
            this->send_global_palette();

            if (!this->input_event_and_graphics_update_disabled) {
                if (this->client_info.bpp != this->mod_bpp) {
                    RDPMultiOpaqueRect new_cmd = cmd;

                    const BGRColor color24 = color_decode_opaquerect(cmd._Color, this->mod_bpp, this->mod_palette_rgb);
                    new_cmd._Color = color_encode(color24, this->client_info.bpp);

                    this->orders->draw(new_cmd, clip);
                }
                else {
                    this->orders->draw(cmd, clip);
                }
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPMultiOpaqueRect capture_cmd = cmd;

                    const BGRColor color24 = color_decode_opaquerect(cmd._Color, this->mod_bpp, this->mod_palette_rgb);
                    capture_cmd._Color = color_encode(color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDP::RDPMultiPatBlt & cmd, const Rect & clip) {
        if (!clip.isempty() && !clip.intersect(cmd.rect).isempty()) {
            this->send_global_palette();

            if (!this->input_event_and_graphics_update_disabled) {
                RDP::RDPMultiPatBlt new_cmd = cmd;
                if (this->client_info.bpp != this->mod_bpp) {
                    const BGRColor back_color24 = color_decode_opaquerect(cmd.BackColor, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor fore_color24 = color_decode_opaquerect(cmd.ForeColor, this->mod_bpp, this->mod_palette_rgb);

                    new_cmd.BackColor = color_encode(back_color24, this->client_info.bpp);
                    new_cmd.ForeColor = color_encode(fore_color24, this->client_info.bpp);
                    // this may change the brush add send it to to remote cache
                }
                this->cache_brush(new_cmd.brush);
                this->orders->draw(new_cmd, clip);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDP::RDPMultiPatBlt capture_cmd = cmd;

                    const BGRColor back_color24 = color_decode_opaquerect(cmd.BackColor, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor fore_color24 = color_decode_opaquerect(cmd.ForeColor, this->mod_bpp, this->mod_palette_rgb);

                    capture_cmd.BackColor = color_encode(back_color24, this->capture_bpp);
                    capture_cmd.ForeColor = color_encode(fore_color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDP::RDPMultiScrBlt & cmd, const Rect & clip) {
        if (!clip.isempty() && !clip.intersect(cmd.rect).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                this->orders->draw(cmd, clip);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                this->capture->draw(cmd, clip);
            }
        }
    }

    void draw(const RDPPatBlt & cmd, const Rect & clip) {
        if (!clip.isempty() && !clip.intersect(cmd.rect).isempty()) {
            this->send_global_palette();

            if (!this->input_event_and_graphics_update_disabled) {
                RDPPatBlt new_cmd = cmd;
                if (this->client_info.bpp != this->mod_bpp) {
                    const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);

                    new_cmd.back_color = color_encode(back_color24, this->client_info.bpp);
                    new_cmd.fore_color = color_encode(fore_color24, this->client_info.bpp);
                    // this may change the brush add send it to to remote cache
                }
                this->cache_brush(new_cmd.brush);
                this->orders->draw(new_cmd, clip);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPPatBlt capture_cmd = cmd;

                    const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);

                    capture_cmd.back_color = color_encode(back_color24, this->capture_bpp);
                    capture_cmd.fore_color = color_encode(fore_color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw_tile(const Rect & dst_tile, const Rect & src_tile, const RDPMemBlt & cmd, const Bitmap & bitmap, const Rect & clip)
    {
        if (this->verbose & 64) {
            LOG(LOG_INFO, "front::draw:draw_tile((%u, %u, %u, %u) (%u, %u, %u, %u))",
                 dst_tile.x, dst_tile.y, dst_tile.cx, dst_tile.cy,
                 src_tile.x, src_tile.y, src_tile.cx, src_tile.cy);
        }

        const Bitmap tiled_bmp(bitmap, src_tile);
        const RDPMemBlt cmd2(0, dst_tile, cmd.rop, 0, 0, 0);

        if (!this->input_event_and_graphics_update_disabled) {
            this->orders->draw(cmd2, clip, tiled_bmp);
        }

        if (  this->capture
            && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(cmd2, clip, Bitmap(this->capture_bpp, tiled_bmp));
        }
    }

private:
    void priv_draw_tile(const Rect & dst_tile, const Rect & src_tile, const RDPMemBlt & cmd, const Bitmap & bitmap, const Rect & clip)
    {
        this->draw_tile(dst_tile, src_tile, cmd, bitmap, clip);
    }

    void priv_draw_tile(const Rect & dst_tile, const Rect & src_tile, const RDPMem3Blt & cmd, const Bitmap & bitmap, const Rect & clip)
    {
        this->draw_tile3(dst_tile, src_tile, cmd, bitmap, clip);
    }

    template<class MemBlt>
    void priv_draw_memblt(const MemBlt & cmd, const Rect & clip, const Bitmap & bitmap)
    {
        if (bitmap.cx() < cmd.srcx || bitmap.cy() < cmd.srcy) {
            return;
        }

        this->send_global_palette();

        const uint8_t palette_id = 0;
        if (this->client_info.bpp == 8) {
            if (!this->palette_memblt_sent[palette_id]) {
                RDPColCache cmd(palette_id, bitmap.palette());
                this->orders->draw(cmd);
                this->palette_memblt_sent[palette_id] = true;
            }
        }

        const uint16_t dst_x = cmd.rect.x;
        const uint16_t dst_y = cmd.rect.y;
        // clip dst as it can be larger than source bitmap
        const uint16_t dst_cx = std::min<uint16_t>(bitmap.cx() - cmd.srcx, cmd.rect.cx);
        const uint16_t dst_cy = std::min<uint16_t>(bitmap.cy() - cmd.srcy, cmd.rect.cy);

        // check if target bitmap can be fully stored inside one front cache entry
        // if so no need to tile it.
        uint32_t front_bitmap_size = ::nbbytes(this->client_info.bpp) * align4(dst_cx) * dst_cy;
        // even if cache seems to be large enough, cache entries cant be used
        // for values whose width is larger or equal to 256 after alignment
        // hence, we check for this case. There does not seem to exist any
        // similar restriction on cy actual reason of this is unclear
        // (I don't even know if it's related to redemption code or client code).
//        LOG(LOG_INFO, "cache1=%u cache2=%u cache3=%u bmp_size==%u",
//            this->client_info.cache1_size,
//            this->client_info.cache2_size,
//            this->client_info.cache3_size,
//            front_bitmap_size);
        if (front_bitmap_size <= this->client_info.cache3_size
            && align4(dst_cx) < 128 && dst_cy < 128) {
            // clip dst as it can be larger than source bitmap
            const Rect dst_tile(dst_x, dst_y, dst_cx, dst_cy);
            const Rect src_tile(cmd.srcx, cmd.srcy, dst_cx, dst_cy);
            this->priv_draw_tile(dst_tile, src_tile, cmd, bitmap, clip);
        }
        else {
            // if not we have to split it
            const uint16_t TILE_CX = ((::nbbytes(this->client_info.bpp) * 64 * 64 < RDPSerializer::MAX_ORDERS_SIZE) ? 64 : 32);
            const uint16_t TILE_CY = TILE_CX;

            for (int y = 0; y < dst_cy ; y += TILE_CY) {
                int cy = std::min(TILE_CY, (uint16_t)(dst_cy - y));

                for (int x = 0; x < dst_cx ; x += TILE_CX) {
                    int cx = std::min(TILE_CX, (uint16_t)(dst_cx - x));

                    const Rect dst_tile(dst_x + x, dst_y + y, cx, cy);
                    const Rect src_tile(cmd.srcx + x, cmd.srcy + y, cx, cy);
                    this->priv_draw_tile(dst_tile, src_tile, cmd, bitmap, clip);
                }
            }
        }
    }

public:
    void draw(const RDPMemBlt & cmd, const Rect & clip, const Bitmap & bitmap)
    {
        this->priv_draw_memblt(cmd, clip, bitmap);
    }

    void draw_tile3(const Rect & dst_tile, const Rect & src_tile, const RDPMem3Blt & cmd, const Bitmap & bitmap, const Rect & clip)
    {
        if (this->verbose & 64) {
            LOG(LOG_INFO, "front::draw:draw_tile3((%u, %u, %u, %u) (%u, %u, %u, %u)",
                 dst_tile.x, dst_tile.y, dst_tile.cx, dst_tile.cy,
                 src_tile.x, src_tile.y, src_tile.cx, src_tile.cy);
        }

        const Bitmap tiled_bmp(bitmap, src_tile);
        RDPMem3Blt cmd2(0, dst_tile, cmd.rop, 0, 0, cmd.back_color, cmd.fore_color, cmd.brush, 0);

        if (this->client_info.bpp != this->mod_bpp) {
            const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
            const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);

            cmd2.back_color= color_encode(back_color24, this->client_info.bpp);
            cmd2.fore_color= color_encode(fore_color24, this->client_info.bpp);
            // this may change the brush add send it to to remote cache
        }

        if (!this->input_event_and_graphics_update_disabled) {
            this->orders->draw(cmd2, clip, tiled_bmp);
        }

        if (  this->capture
            && (this->capture_state == CAPTURE_STATE_STARTED)) {
            if (this->capture_bpp != this->mod_bpp) {
                const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);

                cmd2.back_color= color_encode(back_color24, this->capture_bpp);
                cmd2.fore_color= color_encode(fore_color24, this->capture_bpp);
                // this may change the brush add send it to to remote cache
            }
            else {
                cmd2.back_color= cmd.back_color;
                cmd2.fore_color= cmd.fore_color;
            }
            this->capture->draw(cmd2, clip, tiled_bmp);
        }
    }

    void draw(const RDPMem3Blt & cmd, const Rect & clip, const Bitmap & bitmap)
    {
        this->priv_draw_memblt(cmd, clip, bitmap);
    }

    void draw(const RDPLineTo & cmd, const Rect & clip) {
        const uint16_t minx = std::min(cmd.startx, cmd.endx);
        const uint16_t miny = std::min(cmd.starty, cmd.endy);
        const Rect rect(minx, miny,
                        std::max(cmd.startx, cmd.endx) - minx + 1,
                        std::max(cmd.starty, cmd.endy) - miny + 1);

        if (!clip.isempty() && !clip.intersect(rect).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                if (this->client_info.bpp != this->mod_bpp) {
                    RDPLineTo new_cmd = cmd;

                    const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor pen_color24  = color_decode_opaquerect(cmd.pen.color,  this->mod_bpp, this->mod_palette_rgb);

                    new_cmd.back_color = color_encode(back_color24, this->client_info.bpp);
                    new_cmd.pen.color  = color_encode(pen_color24,  this->client_info.bpp);

                    this->orders->draw(new_cmd, clip);
                }
                else {
                    this->orders->draw(cmd, clip);
                }
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPLineTo capture_cmd = cmd;

                    const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor pen_color24 = color_decode_opaquerect(cmd.pen.color, this->mod_bpp, this->mod_palette_rgb);

                    capture_cmd.back_color = color_encode(back_color24, this->capture_bpp);
                    capture_cmd.pen.color = color_encode(pen_color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDPGlyphIndex & cmd, const Rect & clip, const GlyphCache * gly_cache)
    {
        if (!clip.isempty() && !clip.intersect(cmd.bk).isempty()) {
            this->send_global_palette();

            RDPGlyphIndex new_cmd = cmd;
            if (this->client_info.bpp != this->mod_bpp) {
                const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);
                new_cmd.back_color = color_encode(back_color24, this->client_info.bpp);
                new_cmd.fore_color = color_encode(fore_color24, this->client_info.bpp);
            }

            // this may change the brush and send it to to remote cache
            this->cache_brush(new_cmd.brush);

            if (!this->input_event_and_graphics_update_disabled) {
                this->orders->draw(new_cmd, clip, gly_cache);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);
                    new_cmd.back_color = color_encode(back_color24, this->capture_bpp);
                    new_cmd.fore_color = color_encode(fore_color24, this->capture_bpp);
                }
                else {
                    new_cmd.back_color = cmd.back_color;
                    new_cmd.fore_color = cmd.fore_color;
                }
                this->capture->draw(new_cmd, clip, gly_cache);
            }
        }
    }

    void draw(const RDPPolygonSC & cmd, const Rect & clip) {
        int16_t minx, miny, maxx, maxy, previousx, previousy;

        minx = maxx = previousx = cmd.xStart;
        miny = maxy = previousy = cmd.yStart;

        for (uint8_t i = 0; i < cmd.NumDeltaEntries; i++) {
            previousx += cmd.deltaPoints[i].xDelta;
            previousy += cmd.deltaPoints[i].yDelta;

            minx = std::min(minx, previousx);
            miny = std::min(miny, previousy);

            maxx = std::max(maxx, previousx);
            maxy = std::max(maxy, previousy);
        }
        const Rect rect(minx, miny, maxx-minx+1, maxy-miny+1);

        if (!clip.isempty() && !clip.intersect(rect).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                RDPPolygonSC new_cmd = cmd;
                if (this->client_info.bpp != this->mod_bpp) {
                    const BGRColor pen_color24 = color_decode_opaquerect(cmd.BrushColor, this->mod_bpp, this->mod_palette_rgb);
                    new_cmd.BrushColor = color_encode(pen_color24, this->client_info.bpp);
                }

                this->orders->draw(new_cmd, clip);
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPPolygonSC capture_cmd = cmd;
                    const BGRColor pen_color24 = color_decode_opaquerect(cmd.BrushColor, this->mod_bpp, this->mod_palette_rgb);
                    capture_cmd.BrushColor = color_encode(pen_color24, this->capture_bpp);
                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDPPolygonCB & cmd, const Rect & clip) {
        int16_t minx, miny, maxx, maxy, previousx, previousy;

        minx = maxx = previousx = cmd.xStart;
        miny = maxy = previousy = cmd.yStart;

        for (uint8_t i = 0; i < cmd.NumDeltaEntries; i++) {
            previousx += cmd.deltaPoints[i].xDelta;
            previousy += cmd.deltaPoints[i].yDelta;

            minx = std::min(minx, previousx);
            miny = std::min(miny, previousy);

            maxx = std::max(maxx, previousx);
            maxy = std::max(maxy, previousy);
        }
        const Rect rect(minx, miny, maxx-minx+1, maxy-miny+1);

        if (!clip.isempty() && !clip.intersect(rect).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                if (this->client_info.bpp != this->mod_bpp) {
                    RDPPolygonCB new_cmd = cmd;

                    const BGRColor fore_pen_color24 = color_decode_opaquerect(cmd.foreColor, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor back_pen_color24 = color_decode_opaquerect(cmd.backColor, this->mod_bpp, this->mod_palette_rgb);

                    new_cmd.foreColor = color_encode(fore_pen_color24, this->client_info.bpp);
                    new_cmd.backColor = color_encode(back_pen_color24, this->client_info.bpp);

                    this->orders->draw(new_cmd, clip);
                }
                else {
                    this->orders->draw(cmd, clip);
                }
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPPolygonCB capture_cmd = cmd;

                    const BGRColor fore_pen_color24 = color_decode_opaquerect(cmd.foreColor, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor back_pen_color24 = color_decode_opaquerect(cmd.backColor, this->mod_bpp, this->mod_palette_rgb);

                    capture_cmd.foreColor = color_encode(fore_pen_color24, this->capture_bpp);
                    capture_cmd.backColor = color_encode(back_pen_color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDPPolyline & cmd, const Rect & clip) {
        int16_t minx, miny, maxx, maxy, previousx, previousy;

        minx = maxx = previousx = cmd.xStart;
        miny = maxy = previousy = cmd.yStart;

        for (uint8_t i = 0; i < cmd.NumDeltaEntries; i++) {
            previousx += cmd.deltaEncodedPoints[i].xDelta;
            previousy += cmd.deltaEncodedPoints[i].yDelta;

            minx = std::min(minx, previousx);
            miny = std::min(miny, previousy);

            maxx = std::max(maxx, previousx);
            maxy = std::max(maxy, previousy);
        }
        const Rect rect(minx, miny, maxx-minx+1, maxy-miny+1);

        if (!clip.isempty() && !clip.intersect(rect).isempty()) {
            if (!this->input_event_and_graphics_update_disabled) {
                if (this->client_info.bpp != this->mod_bpp) {
                    RDPPolyline new_cmd = cmd;

                    const BGRColor pen_color24 = color_decode_opaquerect(cmd.PenColor, this->mod_bpp, this->mod_palette_rgb);
                    new_cmd.PenColor = color_encode(pen_color24, this->client_info.bpp);

                    this->orders->draw(new_cmd, clip);
                }
                else {
                    this->orders->draw(cmd, clip);
                }
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPPolyline capture_cmd = cmd;

                    const BGRColor pen_color24 = color_decode_opaquerect(cmd.PenColor, this->mod_bpp, this->mod_palette_rgb);
                    capture_cmd.PenColor = color_encode(pen_color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDPEllipseSC & cmd, const Rect & clip)
    {
        if (!clip.isempty() && !clip.intersect(cmd.el.get_rect()).isempty()) {
            this->send_global_palette();

            if (!this->input_event_and_graphics_update_disabled) {
                if (this->client_info.bpp != this->mod_bpp) {
                    RDPEllipseSC new_cmd = cmd;
                    const BGRColor color24 = color_decode_opaquerect(cmd.color, this->mod_bpp, this->mod_palette_rgb);
                    new_cmd.color = color_encode(color24, this->client_info.bpp);
                    this->orders->draw(new_cmd, clip);
                }
                else {
                    this->orders->draw(cmd, clip);
                }
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPEllipseSC capture_cmd = cmd;

                    const BGRColor color24 = color_decode_opaquerect(cmd.color, this->mod_bpp, this->mod_palette_rgb);
                    capture_cmd.color = color_encode(color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    void draw(const RDPEllipseCB & cmd, const Rect & clip)
    {
        if (!clip.isempty() && !clip.intersect(cmd.el.get_rect()).isempty()) {
            this->send_global_palette();

            if (!this->input_event_and_graphics_update_disabled) {
                if (this->client_info.bpp != this->mod_bpp) {
                    RDPEllipseCB new_cmd = cmd;
                    const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);

                    new_cmd.fore_color = color_encode(fore_color24, this->client_info.bpp);
                    new_cmd.back_color = color_encode(back_color24, this->client_info.bpp);

                    this->orders->draw(new_cmd, clip);
                }
                else {
                    this->orders->draw(cmd, clip);
                }
            }

            if (  this->capture
               && (this->capture_state == CAPTURE_STATE_STARTED)) {
                if (this->capture_bpp != this->mod_bpp) {
                    RDPEllipseCB capture_cmd = cmd;

                    const BGRColor back_color24 = color_decode_opaquerect(cmd.back_color, this->mod_bpp, this->mod_palette_rgb);
                    const BGRColor fore_color24 = color_decode_opaquerect(cmd.fore_color, this->mod_bpp, this->mod_palette_rgb);

                    capture_cmd.back_color = color_encode(back_color24, this->capture_bpp);
                    capture_cmd.fore_color = color_encode(fore_color24, this->capture_bpp);

                    this->capture->draw(capture_cmd, clip);
                }
                else {
                    this->capture->draw(cmd, clip);
                }
            }
        }
    }

    virtual void flush() {
        this->orders->flush();
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->flush();
        }
    }

    void cache_brush(RDPBrush & brush)
    {
        if ((brush.style == 3) && (this->client_info.brush_cache_code == 1)) {
            uint8_t pattern[8];
            pattern[0] = brush.hatch;
            memcpy(pattern+1, brush.extra, 7);
            int cache_idx = 0;
            if (BRUSH_TO_SEND == this->orders.add_brush(pattern, cache_idx)) {
                RDPBrushCache cmd(cache_idx, 1, 8, 8, 0x81,
                    sizeof(this->orders.brush_at(cache_idx).pattern),
                    this->orders.brush_at(cache_idx).pattern);
                this->orders->draw(cmd);
            }
            brush.hatch = cache_idx;
            brush.style = 0x81;
        }
    }

    virtual void draw(const RDPColCache & cmd)
    {
        this->orders->draw(cmd);
    }

    void set_mod_palette(const BGRPalette & palette)
    {
        this->mod_palette_rgb = palette;
        this->palette_sent = false;

        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->set_mod_palette(palette);
        }
    }

    virtual void draw(const RDP::FrameMarker & order) {
        if (this->client_order_caps.orderSupportExFlags & ORDERFLAGS_EX_ALTSEC_FRAME_MARKER_SUPPORT) {
            this->orders->draw(order);
        }
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(order);
        }
    }

    virtual void draw(const RDP::RAIL::NewOrExistingWindow & order) {
        this->orders->draw(order);

        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(order);
        }
    }

    virtual void draw(const RDP::RAIL::WindowIcon & order) {
        this->orders->draw(order);

        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(order);
        }
    }

    virtual void draw(const RDP::RAIL::CachedIcon & order) {
        this->orders->draw(order);

        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(order);
        }
    }

    virtual void draw(const RDP::RAIL::DeletedWindow & order) {
        this->orders->draw(order);

        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(order);
        }
    }

    virtual void draw(const RDP::RAIL::ActivelyMonitoredDesktop & order) {
        this->orders->draw(order);

        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(order);
        }
    }

    virtual void draw(const RDP::RAIL::NonMonitoredDesktop & order) {
        this->orders->draw(order);

        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            this->capture->draw(order);
        }
    }

    virtual void intersect_order_caps(int idx, uint8_t * proxy_order_caps) const {
        proxy_order_caps[idx] &= this->client_order_caps.orderSupport[idx];
    }

    virtual void intersect_order_caps_ex(OrderCaps & order_caps) const {
        order_caps.orderSupportExFlags &= this->client_order_caps.orderSupportExFlags;
    }

    virtual void draw(const RDPBitmapData & bitmap_data, const uint8_t * data
                     , size_t size, const Bitmap & bmp) {
        //LOG(LOG_INFO, "Front::draw(BitmapUpdate)");

        if (   !this->ini.globals.enable_bitmap_update
            // This is to protect rdesktop different color depth works with mstsc and xfreerdp.
            || (bitmap_data.bits_per_pixel != this->client_info.bpp)
            || (bitmap_data.bitmap_size() > this->max_bitmap_size)
           ) {
            Rect boundary(bitmap_data.dest_left,
                          bitmap_data.dest_top,
                          bitmap_data.dest_right - bitmap_data.dest_left + 1,
                          bitmap_data.dest_bottom - bitmap_data.dest_top + 1
                         );

            this->draw(RDPMemBlt(0, boundary, 0xCC, 0, 0, 0), boundary, bmp);
            return;
        }

        if (!this->input_event_and_graphics_update_disabled) {
            ::compress_and_draw_bitmap_update(bitmap_data,
                                              Bitmap(this->client_info.bpp, bmp),
                                              this->client_info.bpp,
                                              this->orders.p->graphics_update_pdu);
        }
        //bitmap_data.log(LOG_INFO, "Front");
        //hexdump_d(data, size);
        if (  this->capture
           && (this->capture_state == CAPTURE_STATE_STARTED)) {
            if ((bmp.bpp() > this->capture_bpp) || (bmp.bpp() == 8)) {
                Bitmap capture_bmp(this->capture_bpp, bmp);

                ::compress_and_draw_bitmap_update(bitmap_data, capture_bmp, this->capture_bpp, *this->capture);
            }
            else {
                if (!(bitmap_data.flags & BITMAP_COMPRESSION)) {
                    ::compress_and_draw_bitmap_update(bitmap_data, bmp, this->capture_bpp, *this->capture);
                }
                else {
                    this->capture->draw(bitmap_data, data, size, bmp);
                }
            }
        }
    }

    virtual bool check_and_reset_activity()
    {
        const bool res = this->has_activity;
        this->has_activity = false;
        return res;
    }
};

#endif
