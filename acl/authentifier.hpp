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
   Copyright (C) Wallix 2010
   Author(s): Christophe Grosjean, Javier Caverni, Xavier Dunat, Raphael Zhou
*/

#ifndef _REDEMPTION_ACL_AUTHENTIFIER_HPP_
#define _REDEMPTION_ACL_AUTHENTIFIER_HPP_

TODO("Sesman is performing two largely unrelated tasks : finding out the next module to run (from context reading) and updating context dictionnary from incoming acl traffic. These tasks should be performed by two different modules")

#include <unistd.h>
#include <fcntl.h>

#include "stream.hpp"
#include "config.hpp"
#include "netutils.hpp"
#include "sockettransport.hpp"
#include "wait_obj.hpp"


typedef enum {
    INTERNAL_NONE,
    INTERNAL_DIALOG_DISPLAY_MESSAGE,
    INTERNAL_DIALOG_VALID_MESSAGE,
    INTERNAL_CLOSE,
    INTERNAL_BOUNCER2,
    INTERNAL_TEST,
    INTERNAL_CARD,
    INTERNAL_WIDGET2_SELECTOR,
    INTERNAL_WIDGET2_CLOSE,
    INTERNAL_WIDGET2_DIALOG,
    INTERNAL_WIDGET2_MESSAGE,
    INTERNAL_WIDGET2_LOGIN,
    INTERNAL_WIDGET2_RWL,
    INTERNAL_WIDGET2_RWL_LOGIN,
} submodule_t;

enum {
    MCTX_STATUS_EXIT,
    MCTX_STATUS_WAITING,
    MCTX_STATUS_VNC,
    MCTX_STATUS_RDP,
    MCTX_STATUS_XUP,
    MCTX_STATUS_INTERNAL,
    MCTX_STATUS_TRANSITORY,
    MCTX_STATUS_AUTH,
    MCTX_STATUS_CLI,
};

class SessionManager {
    enum {
        MOD_STATE_INIT,
        MOD_STATE_DONE_RECEIVED_CREDENTIALS,
        MOD_STATE_DONE_DISPLAY_MESSAGE,
        MOD_STATE_DONE_VALID_MESSAGE,
        MOD_STATE_DONE_LOGIN,
        MOD_STATE_DONE_SELECTOR,
        MOD_STATE_DONE_PASSWORD,
        MOD_STATE_DONE_CONNECTED,
        MOD_STATE_DONE_CLOSE,
        MOD_STATE_DONE_EXIT,
    } mod_state;

    Inifile * ini;

    int tick_count;

    public:
    Transport & auth_trans;
    int keepalive_grace_delay;
    int max_tick;
    bool internal_domain;
    uint32_t verbose;

    SessionManager(Inifile * ini, Transport & auth_trans, int keepalive_grace_delay, 
                   int max_tick, bool internal_domain, uint32_t verbose)
        : mod_state(MOD_STATE_INIT)
        , ini(ini)
        , tick_count(0)
        , auth_trans(auth_trans)
        , keepalive_grace_delay(keepalive_grace_delay)
        , max_tick(max_tick)
        , internal_domain(internal_domain)
        , verbose(verbose)
    {
        if (this->verbose & 0x10){
            LOG(LOG_INFO, "auth::SessionManager");
        }
    }

    ~SessionManager()
    {
        if (this->verbose & 0x10){
            LOG(LOG_INFO, "auth::~SessionManager");
        }
    }
    /*
    bool event(fd_set & rfds){
        if (this->verbose & 0x40){
            LOG(LOG_INFO, "auth::event?");
        }
        return this->auth_event?this->auth_event->is_set(rfds):false;
    }
    */
    void start_keep_alive(long & keepalive_time)
    {
        if (this->verbose & 0x10){
            LOG(LOG_INFO, "auth::start_keep_alive");
        }
        this->tick_count = 1;

        BStream stream(8192);
        
        stream.out_uint32_be(0); // skip length
        this->ini->context_ask(AUTHID_KEEPALIVE);
        this->out_item(stream, STRAUTHID_KEEPALIVE);
        stream.mark_end();
        // now set length
        int total_length = stream.get_offset();
        stream.set_out_uint32_be(total_length - 4, 0);
        this->auth_trans.send(stream.data, total_length);
        keepalive_time = ::time(NULL) + 30;

    }

    // Set AUTHCHANNEL_TARGET dict value and transmit request to sesman (then wabenginge)
    void ask_auth_channel_target(const char * target)
    {
        if (this->verbose) {
            LOG(LOG_INFO, "SessionManager::ask_auth_channel_target(%s)", target);
        }

        BStream stream(8192);

        this->ini->context_set_value(AUTHID_AUTHCHANNEL_TARGET, target);

        stream.out_uint32_be(0); // skip length
        this->out_item(stream, STRAUTHID_AUTHCHANNEL_TARGET);

        int total_length = stream.get_offset();
        stream.p = stream.data;
        stream.out_uint32_be(total_length - 4);
        this->auth_trans.send(stream.data, total_length);
    }

    // Set AUTHCHANNEL_RESULT dict value and transmit request to sesman (then wabenginge)
    void set_auth_channel_result(const char * result)
    {
        if (this->verbose) {
            LOG(LOG_INFO, "SessionManager::set_auth_channel_result(%s)", result);
        }
        BStream stream(8192);

        this->ini->context_set_value(AUTHID_AUTHCHANNEL_RESULT, result);

        stream.out_uint32_be(0);  // skip length
        this->out_item(stream, STRAUTHID_AUTHCHANNEL_RESULT);
        int total_length = stream.get_offset();
        stream.set_out_uint32_be(total_length - 4, 0);

        this->auth_trans.send(stream.data, total_length);
    }

    void in_items(Stream & stream)
    {
        if (this->verbose & 0x40){
            LOG(LOG_INFO, "auth::in_items");
        }
        for (; stream.p < stream.end ; this->in_item(stream)){
            ;
        }
    }

    void in_item(Stream & stream)
    {
        enum { STATE_KEYWORD, STATE_VALUE } state = STATE_KEYWORD;
        uint8_t * value = stream.p;
        uint8_t * keyword = stream.p;
        const uint8_t * start = stream.p;
        for ( ; stream.p < stream.end ; stream.p++){
            switch (state){
            case STATE_KEYWORD:
                if (*stream.p == '\n'){
                    *stream.p = 0;
                    value = stream.p+1;
                    state = STATE_VALUE;
                }
                break;
            case STATE_VALUE:
                if (*stream.p == '\n'){
                    *stream.p = 0;

                    if ((0 == strncasecmp((char*)value, "ask", 3))) {
                        this->ini->context_ask((char *)keyword);
                        LOG(LOG_INFO, "receiving %s '%s'\n", value, keyword);
                    }
                    else {
                        this->ini->context_set_value((char *)keyword,
                            (char *)value + (value[0] == '!' ? 1 : 0));

                        if (  (strncasecmp("password",        (char *)keyword, 9 ) == 0)
                           || (strncasecmp("target_password", (char *)keyword, 16) == 0)
                           ){
                            LOG(LOG_INFO, "receiving '%s'=<hidden>\n", (char *)keyword);
                        }
                        else{
                            char buffer[128];
                            LOG(LOG_INFO, "receiving '%s'='%s'\n", keyword,
                                this->ini->context_get_value((char *)keyword, buffer, sizeof(buffer)));
                        }
                    }

                    stream.p = stream.p+1;
                    return;
                }
                break;
            }
        }
        LOG(LOG_WARNING, "Unexpected exit while parsing ACL message");
        hexdump((char *)start, stream.p-start);
        throw Error(ERR_ACL_UNEXPECTED_IN_ITEM_OUT);
    }

    bool close_on_timestamp(long & timestamp)
    {
        bool res = false;
        if (MOD_STATE_DONE_CONNECTED == this->mod_state){
            long enddate = this->ini->context.end_date_cnx;
            if (enddate != 0 && (timestamp > enddate)) {
                if (this->verbose & 0x10){
                    LOG(LOG_INFO, "auth::close_on_timestamp");
                }
                LOG(LOG_INFO, "Session is out of allowed timeframe : stopping");
                this->mod_state = MOD_STATE_DONE_CLOSE;
                res = true;
            }
        }
        return res;
    }

    bool keep_alive_checking(long & keepalive_time, long & now, Transport & trans)
    {
        
        //        LOG(LOG_INFO, "keep_alive(%lu, %lu)", keepalive_time, now);
        if (MOD_STATE_DONE_CONNECTED == this->mod_state){
            long enddate = this->ini->context.end_date_cnx;
            //            LOG(LOG_INFO, "keep_alive(%lu, %lu, %lu)", keepalive_time, now, enddate));
            if (enddate != 0 && (now > enddate)) {
                LOG(LOG_INFO, "Session is out of allowed timeframe : closing");
                this->mod_state = MOD_STATE_DONE_CLOSE;
                return false;
            }
        }

        if (keepalive_time == 0){
            //            LOG(LOG_INFO, "keep_alive disabled");
            return true;
        }

        TODO("we should manage a mode to disconnect on inactivity when we are on login box or on selector")
            if (now > (keepalive_time + this->keepalive_grace_delay)){
                LOG(LOG_INFO, "auth::keep_alive_or_inactivity Connection closed by manager (timeout)");
                this->ini->context.auth_error_message.copy_c_str("Connection closed by manager (timeout)");
                return false;
            }


        if (now > keepalive_time){
            // ===================== check if no traffic =====================
            if (this->verbose & 8){
                LOG(LOG_INFO, "%llu bytes received in last quantum, total: %llu tick:%d",
                    trans.last_quantum_received, trans.total_received,
                    this->tick_count);
            }
            if (trans.last_quantum_received == 0){
                this->tick_count++;
                if (this->tick_count > this->max_tick){ // 15 minutes before closing on inactivity
                    this->ini->context.auth_error_message.copy_c_str("Connection closed on inactivity");
                    LOG(LOG_INFO, "Session ACL inactivity : closing");
                    this->mod_state = MOD_STATE_DONE_CLOSE;
                    return false;
                }

                keepalive_time = now + this->keepalive_grace_delay;
            }
            else {
                this->tick_count = 0;
            }
            trans.tick();

            // ===================== check if keepalive ======================
            try {
                BStream stream(8192);
                stream.out_uint32_be(0); // skip length
                // set data
                this->out_item(stream, STRAUTHID_KEEPALIVE);
                // now set length in header
                int total_length = stream.get_offset();
                stream.set_out_uint32_be(total_length - 4, 0); /* size */
                stream.mark_end();
                this->auth_trans.send(stream.data, total_length);
            }
            catch (...){
                this->ini->context.auth_error_message.copy_c_str("Connection closed by manager (ACL closed).");
                this->mod_state = MOD_STATE_DONE_CLOSE;
                return false;
            }
        }
        return true;
    }
    bool keep_alive_response(long & keepalive_time, long & now)
    {
        if (this->verbose & 0x10){
            LOG(LOG_INFO, "auth::keep_alive ACL incoming event");
        }
        try {
            this->incoming();
            if (this->ini->context_get_bool(AUTHID_KEEPALIVE)) {
                keepalive_time = now + this->keepalive_grace_delay;
                this->ini->context_ask(AUTHID_KEEPALIVE);
            }
        }
        catch (...){
            this->ini->context.auth_error_message.copy_c_str("Connection closed by manager (ACL closed)");
            this->mod_state = MOD_STATE_DONE_CLOSE;
            return false;
        }
        return true;
    }
    bool keep_alive(long & keepalive_time, long & now, Transport * trans, bool read_auth)
    {
//        LOG(LOG_INFO, "keep_alive(%lu, %lu)", keepalive_time, now);
        if (MOD_STATE_DONE_CONNECTED == this->mod_state){
            long enddate = this->ini->context.end_date_cnx;
//            LOG(LOG_INFO, "keep_alive(%lu, %lu, %lu)", keepalive_time, now, enddate));
            if (enddate != 0 && (now > enddate)) {
                LOG(LOG_INFO, "Session is out of allowed timeframe : closing");
                this->mod_state = MOD_STATE_DONE_CLOSE;
                return false;
            }
        }

        if (keepalive_time == 0){
//            LOG(LOG_INFO, "keep_alive disabled");
            return true;
        }

        TODO("we should manage a mode to disconnect on inactivity when we are on login box or on selector")
        if (now > (keepalive_time + this->keepalive_grace_delay)){
            LOG(LOG_INFO, "auth::keep_alive_or_inactivity Connection closed by manager (timeout)");
            this->ini->context.auth_error_message.copy_c_str("Connection closed by manager (timeout)");
            return false;
        }


        if (now > keepalive_time){
            // ===================== check if no traffic =====================
            if (this->verbose & 8){
                LOG(LOG_INFO, "%llu bytes received in last quantum, total: %llu tick:%d",
                          trans->last_quantum_received, trans->total_received,
                          this->tick_count);
            }
            if (trans->last_quantum_received == 0){
                this->tick_count++;
                if (this->tick_count > this->max_tick){ // 15 minutes before closing on inactivity
                    this->ini->context.auth_error_message.copy_c_str("Connection closed on inactivity");
                    LOG(LOG_INFO, "Session ACL inactivity : closing");
                    this->mod_state = MOD_STATE_DONE_CLOSE;
                    return false;
                }

                keepalive_time = now + this->keepalive_grace_delay;
            }
            else {
                this->tick_count = 0;
            }
            trans->tick();

            // ===================== check if keepalive ======================
            try {
                BStream stream(8192);
                stream.out_uint32_be(0); // skip length
                // set data
                this->out_item(stream, STRAUTHID_KEEPALIVE);
                // now set length in header
                int total_length = stream.get_offset();
                stream.set_out_uint32_be(total_length - 4, 0); /* size */
                stream.mark_end();
                this->auth_trans.send(stream.data, total_length);
            }
            catch (...){
                this->ini->context.auth_error_message.copy_c_str("Connection closed by manager (ACL closed).");
                this->mod_state = MOD_STATE_DONE_CLOSE;
                return false;
            }
        }

        if (read_auth) {
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::keep_alive ACL incoming event");
            }
            try {
                this->incoming();
                if (this->ini->context_get_bool(AUTHID_KEEPALIVE)) {
                    keepalive_time = now + this->keepalive_grace_delay;
                    this->ini->context_ask(AUTHID_KEEPALIVE);
                }
            }
            catch (...){
                this->ini->context.auth_error_message.copy_c_str("Connection closed by manager (ACL closed)");
                this->mod_state = MOD_STATE_DONE_CLOSE;
                return false;
            }
        }

        return true;
    }

    int get_mod_from_protocol(submodule_t & nextmod)
    {
        if (this->verbose & 0x10){
            LOG(LOG_INFO, "auth::get_mod_from_protocol");
        }
        const char * protocol = this->ini->context_get_value(AUTHID_TARGET_PROTOCOL, NULL, 0);
        if (this->internal_domain){
            const char * target = this->ini->context_get_value(AUTHID_TARGET_DEVICE, NULL, 0);
            if (0 == strncmp(target, "autotest", 8)){
                protocol = "INTERNAL";
            }
        }
        int res = MCTX_STATUS_EXIT;
        if (strncasecmp(protocol, "RDP", 4) == 0){
            if (this->verbose & 0x4){
                LOG(LOG_INFO, "auth::get_mod_from_protocol RDP");
            }
            res = MCTX_STATUS_RDP;
        }
        else if (strncasecmp(protocol, "VNC", 4) == 0){
            if (this->verbose & 0x4){
                LOG(LOG_INFO, "auth::get_mod_from_protocol VNC");
            }
            res = MCTX_STATUS_VNC;
        }
        else if (strncasecmp(protocol, "XUP", 4) == 0){
            if (this->verbose & 0x4){
                LOG(LOG_INFO, "auth::get_mod_from_protocol XUP");
            }
            res = MCTX_STATUS_XUP;
        }
        else if (strncasecmp(protocol, "INTERNAL", 8) == 0){
            const char * target = this->ini->context_get_value(AUTHID_TARGET_DEVICE, NULL, 0);
            if (this->verbose & 0x4){
                LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL");
            }
            res = MCTX_STATUS_INTERNAL;
            if (0 == strcmp(target, "bouncer2")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL bouncer2");
                }
                nextmod = INTERNAL_BOUNCER2;
            }
            else if (0 == strncmp(target, "autotest", 8)){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL test");
                }
                const char * user = this->ini->context_get_value(AUTHID_TARGET_USER, NULL, 0);
                size_t len_user = strlen(user);
                strncpy(this->ini->context.movie, user, sizeof(this->ini->context.movie));
                this->ini->context.movie[sizeof(this->ini->context.movie) - 1] = 0;
                if (0 != strcmp(".mwrm", user + len_user - 5)){
                    strcpy(this->ini->context.movie + len_user, ".mwrm");
                }
                nextmod = INTERNAL_TEST;
            }
            else if (0 == strcmp(target, "selector")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL selector");
                }
                nextmod = INTERNAL_WIDGET2_SELECTOR;
            }
            else if (0 == strcmp(target, "login")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL login");
                }
                nextmod = INTERNAL_WIDGET2_LOGIN;
            }
            else if (0 == strcmp(target, "rwl_login")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL login");
                }
                nextmod = INTERNAL_WIDGET2_RWL_LOGIN;
            }
            else if (0 == strcmp(target, "rwl")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL login");
                }
                nextmod = INTERNAL_WIDGET2_RWL;
            }
            else if (0 == strcmp(target, "close")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL close");
                }
                nextmod = INTERNAL_CLOSE;
            }
            else if (0 == strcmp(target, "widget2_close")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL widget2_close");
                }
                nextmod = INTERNAL_WIDGET2_CLOSE;
            }
            else if (0 == strcmp(target, "widget2_dialog")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL widget2_dialog");
                }
                nextmod = INTERNAL_WIDGET2_DIALOG;
            }
            else if (0 == strcmp(target, "widget2_message")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL widget2_message");
                }
                nextmod = INTERNAL_WIDGET2_MESSAGE;
            }
            else if (0 == strcmp(target, "widget2_login")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL widget2_login");
                }
                nextmod = INTERNAL_WIDGET2_LOGIN;
            }
            else if (0 == strcmp(target, "widget2_rwl")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL rwl_login");
                }
                nextmod = INTERNAL_WIDGET2_RWL;
            }
            else if (0 == strcmp(target, "widget2_rwl_login")){
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL widget2_rwl_login");
                }
                nextmod = INTERNAL_WIDGET2_RWL_LOGIN;
            }
            else {
                if (this->verbose & 0x4){
                    LOG(LOG_INFO, "auth::get_mod_from_protocol INTERNAL card");
                }
                nextmod = INTERNAL_CARD;
            }
        }
        else {
            LOG(LOG_WARNING, "Unsupported target protocol %c%c%c%c",
                protocol[0], protocol[1], protocol[2], protocol[3]);
            nextmod = INTERNAL_CARD;
//            assert(false);
        }
        return res;
    }

    int ask_next_module(long & keepalive_time,
                        bool & record_video, bool & keep_alive,
                        submodule_t & nextmod)
    {
        if (this->verbose & 0x10){
            LOG(LOG_INFO, "auth::ask_next_module");
        }
        switch (this->mod_state){
        default:
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::ask_next_module default state");
            }
            this->ask_next_module_remote();
            return MCTX_STATUS_WAITING;
        break;
        case MOD_STATE_DONE_SELECTOR:
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_SELECTOR state");
            }
            this->ask_next_module_remote();
            return MCTX_STATUS_WAITING;
        break;
        case MOD_STATE_DONE_LOGIN:
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_LOGIN state");
            }
            this->ask_next_module_remote();
            return MCTX_STATUS_WAITING;
        break;
        case MOD_STATE_DONE_PASSWORD:
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_PASSWORD state");
            }
            this->ask_next_module_remote();
            return MCTX_STATUS_WAITING;
        break;
        case MOD_STATE_DONE_RECEIVED_CREDENTIALS:
        if (this->verbose & 0x10){
            LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS state");
        }
        {
            if (this->verbose & 0x20){
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_AUTH_USER=%s",
                    (this->ini->context_is_asked(AUTHID_AUTH_USER) ? "True": "False"));
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_PASSWORD=%s",
                    (this->ini->context_is_asked(AUTHID_PASSWORD) ? "True": "False"));
            }

            if (this->ini->context_is_asked(AUTHID_AUTH_USER)){
                if (this->verbose & 0x20){
                    LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_AUTH_USER is asked");
                }
                this->mod_state = MOD_STATE_DONE_LOGIN;
                nextmod = INTERNAL_WIDGET2_LOGIN;
                return MCTX_STATUS_INTERNAL;
            }
            else if (this->ini->context_is_asked(AUTHID_PASSWORD)){
                if (this->verbose & 0x20){
                    LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_PASSWORD is asked");
                }
                this->mod_state = MOD_STATE_DONE_LOGIN;
                nextmod = INTERNAL_WIDGET2_LOGIN;
                return MCTX_STATUS_INTERNAL;
            }
            else if (!this->ini->context_is_asked(AUTHID_SELECTOR)
                 &&   this->ini->context_get_bool(AUTHID_SELECTOR)
                 &&  !this->ini->context_is_asked(AUTHID_TARGET_DEVICE)
                 &&  !this->ini->context_is_asked(AUTHID_TARGET_USER)){
                if (this->verbose & 0x20){
                    LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_SELECTOR is asked");
                }
                this->mod_state = MOD_STATE_DONE_SELECTOR;
                nextmod = INTERNAL_WIDGET2_SELECTOR;
                return MCTX_STATUS_INTERNAL;
            }
            else if (this->ini->context_is_asked(AUTHID_TARGET_DEVICE)
                 ||  this->ini->context_is_asked(AUTHID_TARGET_USER)){
                    if (this->verbose & 0x20){
                        LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_TARGET_DEVICE is asked");
                    }
                    this->mod_state = MOD_STATE_DONE_LOGIN;
                    nextmod = INTERNAL_WIDGET2_LOGIN;
                    return MCTX_STATUS_INTERNAL;
            }
            else if (this->ini->context_is_asked(AUTHID_DISPLAY_MESSAGE)){
                if (this->verbose & 0x20){
                    LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_DISPLAY_MESSAGE is asked");
                }
                nextmod = INTERNAL_DIALOG_DISPLAY_MESSAGE;
                this->mod_state = MOD_STATE_DONE_DISPLAY_MESSAGE;
                return MCTX_STATUS_INTERNAL;
            }
            else if (this->ini->context_is_asked(AUTHID_ACCEPT_MESSAGE)){
                if (this->verbose & 0x20){
                    LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS AUTHID_ACCEPT_MESSAGE is asked");
                }
                this->mod_state = MOD_STATE_DONE_VALID_MESSAGE;
                nextmod = INTERNAL_DIALOG_VALID_MESSAGE;
                return MCTX_STATUS_INTERNAL;
            }
            else if (this->ini->context.authenticated){
                if (this->verbose & 0x20){
                    LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS authenticated is True");
                }
                record_video = this->ini->globals.movie;
                keep_alive = true;
                if (this->ini->context.auth_error_message.is_empty()) {
                    this->ini->context.auth_error_message.copy_c_str("End of connection");
                }
                this->mod_state = MOD_STATE_DONE_CONNECTED;
                return this->get_mod_from_protocol(nextmod);
            }
            else {
                if (this->verbose & 0x20){
                    LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_RECEIVED_CREDENTIALS else");
                }

                if (!this->ini->context.rejected.is_empty()) {
                    this->ini->context.auth_error_message.copy_str(this->ini->context.rejected);
                }
                if (this->ini->context.auth_error_message.is_empty()) {
                    this->ini->context.auth_error_message.copy_c_str("Authentifier service failed");
                }
                this->mod_state = MOD_STATE_DONE_CONNECTED;
                nextmod = INTERNAL_CLOSE;
                return MCTX_STATUS_INTERNAL;
            }
        }
        break;
        case MOD_STATE_DONE_CONNECTED:
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_CONNECTED state");
            }
            this->mod_state = MOD_STATE_DONE_CLOSE;
            nextmod = INTERNAL_CLOSE;
            return MCTX_STATUS_INTERNAL;
        break;
        case MOD_STATE_DONE_CLOSE:
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_CONNECTED state");
            }
            this->mod_state = MOD_STATE_DONE_EXIT;
            nextmod = INTERNAL_CLOSE;
            return MCTX_STATUS_EXIT;
        break;
        case MOD_STATE_DONE_EXIT:
            if (this->verbose & 0x10){
                LOG(LOG_INFO, "auth::ask_next_module MOD_STATE_DONE_EXIT state");
            }
            // we should never goes here, the main loop should have stopped before
            LOG(LOG_WARNING, "unexpected forced exit");
            nextmod = INTERNAL_CLOSE;
            return MCTX_STATUS_EXIT;
        break;
        }
    }

    TODO("move that function to Inifile create specialized stream object InifileStream")
    void out_item(Stream & stream, const char * key)
    {
        if (this->ini->context_is_asked(key)){
            LOG(LOG_INFO, "sending %s=ASK\n", key);
            stream.out_copy_bytes(key, strlen(key));
            stream.out_copy_bytes("\nASK\n",5);
        }
        else {
            char temp_buffer[256];

            const char * tmp = this->ini->context_get_value(key, temp_buffer, sizeof(temp_buffer));

            if ((strncasecmp("password", (char*)key, 8) == 0)
            ||(strncasecmp("target_password", (char*)key, 15) == 0)){
                LOG(LOG_INFO, "sending %s=<hidden>\n", key);
            }
            else {
                LOG(LOG_INFO, "sending %s=%s\n", key, tmp);
            }
            stream.out_copy_bytes(key, strlen(key));
            stream.out_uint8('\n');
            stream.out_uint8('!');
            stream.out_copy_bytes(tmp, strlen(tmp));
            stream.out_uint8('\n');
        }
    }

    void ask_next_module_remote()
    {
        // if anything happen, like authentification socked closing, stop current connection
        try {
            BStream stream(8192);

            stream.out_uint32_be(0);

            this->out_item(stream, STRAUTHID_PROXY_TYPE);
            this->out_item(stream, STRAUTHID_DISPLAY_MESSAGE);
            this->out_item(stream, STRAUTHID_ACCEPT_MESSAGE);
            this->out_item(stream, STRAUTHID_HOST);
            this->out_item(stream, STRAUTHID_TARGET);
            this->out_item(stream, STRAUTHID_AUTH_USER);
            this->out_item(stream, STRAUTHID_PASSWORD);
            this->out_item(stream, STRAUTHID_TARGET_USER);
            this->out_item(stream, STRAUTHID_TARGET_DEVICE);
            this->out_item(stream, STRAUTHID_TARGET_PROTOCOL);
            this->out_item(stream, STRAUTHID_SELECTOR);
            this->out_item(stream, STRAUTHID_SELECTOR_GROUP_FILTER);
            this->out_item(stream, STRAUTHID_SELECTOR_DEVICE_FILTER);
            this->out_item(stream, STRAUTHID_SELECTOR_LINES_PER_PAGE);
            this->out_item(stream, STRAUTHID_SELECTOR_CURRENT_PAGE);
            this->out_item(stream, STRAUTHID_TARGET_PASSWORD);
            this->out_item(stream, STRAUTHID_OPT_WIDTH);
            this->out_item(stream, STRAUTHID_OPT_HEIGHT);
            this->out_item(stream, STRAUTHID_OPT_BPP);
            this->out_item(stream, STRAUTHID_REAL_TARGET_DEVICE);
            // send trace seal if and only if there is one
            if (strlen(this->ini->context_get_value(AUTHID_TRACE_SEAL, NULL, 0))) {
                this->out_item(stream, STRAUTHID_TRACE_SEAL);
            }
            stream.mark_end();

            int total_length = stream.get_offset();
            LOG(LOG_INFO, "Data size without header (send) %u", total_length - 4);
            stream.set_out_uint32_be(total_length - 4, 0); /* size in header */
            this->auth_trans.send(stream.data, total_length);

        } catch (Error e) {
            this->ini->context.authenticated = false;
            this->ini->context.rejected.copy_c_str("Authentifier service failed");
        }
    }

    void incoming()
    {
        BStream stream(4);
        this->auth_trans.recv(&stream.end, 4);
        size_t size = stream.in_uint32_be();
        if (size > 65536){
            LOG(LOG_WARNING, "Error: ACL message too big (got %u max 64 K)", size);
            throw Error(ERR_ACL_MESSAGE_TOO_BIG);
        }
        if (size > stream.capacity){
            stream.init(size);
        }
        this->auth_trans.recv(&stream.end, size);
        LOG(LOG_INFO, "Data size without header (receive) = %u", size);
        bool flag = this->ini->context.session_id.is_empty();
        this->in_items(stream);
        if (flag && !this->ini->context.session_id.is_empty()) {
            int child_pid = getpid();
            char old_session_file[256];
            sprintf(old_session_file, "%s/redemption/session_%d.pid", PID_PATH, child_pid);
            char new_session_file[256];
            sprintf(new_session_file, "%s/redemption/session_%s.pid", PID_PATH,
                this->ini->context.session_id.c_str());
            rename(old_session_file, new_session_file);
        }

        LOG(LOG_INFO, "SESSION_ID = %s", this->ini->context.session_id.c_str());
    }

    void receive_next_module()
    {
        try {
            this->incoming();
        } catch (...) {
            this->ini->context.authenticated = false;
            this->ini->context.rejected.copy_c_str("Authentifier service failed");
        }
        this->mod_state = MOD_STATE_DONE_RECEIVED_CREDENTIALS;
    }
};

#endif
