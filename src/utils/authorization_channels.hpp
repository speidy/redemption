/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   Product name: redemption, a FLOSS RDP proxy
 *   Copyright (C) Wallix 2010-2014
 *   Author(s): Christophe Grosjean, Raphael Zhou, Jonathan Poelen, Meng Tan
 */

#ifndef REDEMPTION_UTILS_AUTHORIZATION_CHANNELS_HPP
#define REDEMPTION_UTILS_AUTHORIZATION_CHANNELS_HPP

#include "movable_noncopyable.hpp"
#include "apply_for_delim.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <array>
#include <iosfwd>

#include "RDP/channels/rdpdr.hpp"

struct AuthorizationChannels
: public movable_noncopyable
{
    AuthorizationChannels() = default;

    AuthorizationChannels(std::string allow, std::string deny)
    : allow_(std::move(allow))
    , deny_(std::move(deny))
    {
        apply_for_delim(this->allow_.c_str(), ',', [this](char const * & s) {
            if (*s == '*') {
                this->all_allow_ = true;
                this->rdpdr_restriction_.fill(true);
                this->cliprdr_restriction_.fill(true);
                s = "";
            }
        });

        apply_for_delim(this->deny_.c_str(), ',', [this](char const * & s) {
            if (*s == '*') {
                this->all_deny_ = true;
                if (this->all_allow_) {
                    this->rdpdr_restriction_.fill(false);
                    this->cliprdr_restriction_.fill(false);
                }
                s = "";
            }
        });

        this->normalize(this->allow_);
        this->normalize(this->deny_);
    }

    bool is_authorized(const char * s) const noexcept {
        if (this->all_deny_) {
            return contains(this->allow_, s);
        }
        if (this->all_allow_) {
            return !contains(this->deny_, s);
        }
        return !contains(this->deny_, s)
            &&  contains(this->allow_, s);
    }

    bool rdpdr_type_all_is_authorized() const noexcept {
        return (this->rdpdr_type_is_authorized(rdpdr::RDPDR_DTYP_SERIAL) &&
                this->rdpdr_type_is_authorized(rdpdr::RDPDR_DTYP_PRINT) &&
                this->rdpdr_type_is_authorized(rdpdr::RDPDR_DTYP_FILESYSTEM) &&
                this->rdpdr_type_is_authorized(rdpdr::RDPDR_DTYP_SMARTCARD)
               );
    }

    bool rdpdr_type_is_authorized(uint32_t DeviceType) const noexcept {
        switch (DeviceType) {
            case rdpdr::RDPDR_DTYP_SERIAL:
            case rdpdr::RDPDR_DTYP_PARALLEL:
                return this->rdpdr_restriction_[1];

            case rdpdr::RDPDR_DTYP_PRINT:
                return this->rdpdr_restriction_[0];

            case rdpdr::RDPDR_DTYP_FILESYSTEM:
                return (this->rdpdr_restriction_[2] || this->rdpdr_restriction_[3]);

            case rdpdr::RDPDR_DTYP_SMARTCARD:
                return this->rdpdr_restriction_[4];

            default:;
        }

        assert(true && "Unknown RDPDR DeviceType");
        return false;
    }

    inline bool rdpdr_drive_read_is_authorized() const noexcept {
        return this->rdpdr_restriction_[2];
    }

    inline bool rdpdr_drive_write_is_authorized() const noexcept {
        return this->rdpdr_restriction_[3];
    }

    inline bool cliprdr_up_is_authorized() const noexcept {
        return this->cliprdr_restriction_[0];
    }

    inline bool cliprdr_down_is_authorized() const noexcept {
        return this->cliprdr_restriction_[1];
    }

    inline bool cliprdr_file_is_authorized() const noexcept {
        return this->cliprdr_restriction_[2];
    }


    template<class CharT, class Traits>
    friend std::basic_ostream<CharT, Traits> &
    operator<<(std::basic_ostream<CharT, Traits> & os, AuthorizationChannels const & auth) {
        auto p = [&](std::string const & s, bool all, bool val_ok, const char * name) {
            if (all) {
                os << name << "=*\n";
            }
            else {
                os << name << '=';
                for (size_t i = 0; i < auth.cliprdr_restriction_.size(); ++i) {
                    if (auth.cliprdr_restriction_[i] == val_ok) {
                        os << cliprde_list[i];
                    }
                }
                for (size_t i = 0; i < auth.rdpdr_restriction_.size(); ++i) {
                    if (auth.rdpdr_restriction_[i] == val_ok) {
                        os << rdpdr_list[i];
                    }
                }
                os << s << '\n';
            }
        };
        p(auth.allow_, auth.all_allow_, true, "allow");
        p(auth.deny_, auth.all_deny_, false, "deny");
        return os;
    }

    static constexpr const std::array<const char *, 3> cliprde_list {{
        "cliprdr_up,", "cliprdr_down,", "cliprdr_file,"
    }};
    static constexpr const std::array<const char *, 5> rdpdr_list {{
        "rdpdr_printer,", "rdpdr_port,", "rdpdr_drive_read,", "rdpdr_drive_write,", "rdpdr_smartcard,"
    }};

private:
    template<class Cont>
    static bool contains_true(Cont const & cont) {
        for (bool x : cont) {
            if (x) {
                return true;
            }
        }
        return false;
    }

    template<std::size_t N>
    void normalize(
      std::string & s, bool set, std::array<bool, N> & values,
      const char * channel_name, std::array<char const *, N> restriction_names
    ) {
        auto pos = s.find(channel_name);
        if (pos != std::string::npos) {
            s.erase(pos, strlen(channel_name));
            values.fill(set);
            return;
        }

        auto first = values.begin();
        for (auto name : restriction_names) {
            pos = s.find(name);
            if (pos != std::string::npos) {
                s.erase(pos, strlen(name));
                *first = set;
            }
            ++first;
        }
    }

    void normalize(std::string & s) {
        const bool set = (&s == &this->allow_);
        if (!s.empty()) {
            s += ',';
            this->normalize(s, set, this->cliprdr_restriction_, "cliprdr,", cliprde_list);
            this->normalize(s, set, this->rdpdr_restriction_, "rdpdr,", rdpdr_list);
            if (!s.empty() && s.front() == ',') {
                s.erase(0, 1);
            }
        }
        if (set) {
            if (contains_true(this->cliprdr_restriction_)) {
                s += "cliprdr,";
            }
            if (contains_true(this->rdpdr_restriction_)) {
                s += "rdpdr";
            }
        }
        if (!s.empty() && s.back() == ',') {
            s.pop_back();
        }
    }

    static bool contains(std::string const & s, const char * search) noexcept {
        bool ret = false;
        apply_for_delim(s.c_str(), ',', [&ret, search](const char * & s) {
            char const * s2 = search;
            while (*s2 == *s && *s2) {
                ++s;
                ++s2;
            }
            if (!*s2 && (!*s || *s == ',' || is_blanck_fn()(*s))) {
                ret = true;
                s = "";
            }
        });
        return ret;
    }

    std::string allow_;
    std::string deny_;
    bool all_allow_ = false;
    bool all_deny_ = false;
    std::array<bool, 5> rdpdr_restriction_ {{}};
    std::array<bool, 3> cliprdr_restriction_ {{}};
};
constexpr decltype(AuthorizationChannels::cliprde_list) AuthorizationChannels::cliprde_list;
constexpr decltype(AuthorizationChannels::rdpdr_list) AuthorizationChannels::rdpdr_list;


void update_authorized_channels(std::string & allow,
                                std::string & deny,
                                const std::string & proxy_opt) {
    auto remove = [] (std::string & str, const char * pattern) -> bool {
        bool removed = false;
        size_t pos = 0;
        while ((pos = str.find(pattern, pos)) != std::string::npos) {
            str.erase(pos, strlen(pattern));
            removed = true;
        }

        return removed;
    };

    std::string expanded_proxy_opt = proxy_opt;
    while (!expanded_proxy_opt.empty() && expanded_proxy_opt.back() == ',') {
        expanded_proxy_opt.pop_back();
    }
    expanded_proxy_opt += ',';
    if (remove(expanded_proxy_opt, "RDP_DRIVE,")) {
        expanded_proxy_opt += "RDP_DRIVE_READ,RDP_DRIVE_WRITE";
    }
    if (!expanded_proxy_opt.empty() && expanded_proxy_opt.back() == ',') {
        expanded_proxy_opt.pop_back();
    }

    allow += ',';
    deny += ',';

    struct ref_string {
        std::string & s;
        std::string & get() { return this->s; };
        operator std::string & () { return this->s; };
    };

    std::array<ref_string, 2> ret{{{allow}, {deny}}};

    for (std::string & s : ret) {
        remove(s, "cliprdr,");
        remove(s, "rdpdr,");
        for (auto str : AuthorizationChannels::cliprde_list) {
            remove(s, str);
        }
        for (auto str : AuthorizationChannels::rdpdr_list) {
            remove(s, str);
        }
        if (!s.empty() && s.back() == ',') {
            s.pop_back();
        }
    }

    constexpr struct {
        const char * opt;
        const char * channel;
    } opts_channels[] {
        {"RDP_CLIPBOARD_UP",   ",cliprdr_up"       },
        {"RDP_CLIPBOARD_DOWN", ",cliprdr_down"     },
        {"RDP_CLIPBOARD_FILE", ",cliprdr_file"     },

        {"RDP_PRINTER",        ",rdpdr_printer"    },
        {"RDP_COM_PORT",       ",rdpdr_port"       },
        {"RDP_DRIVE_READ",     ",rdpdr_drive_read" },
        {"RDP_DRIVE_WRITE",    ",rdpdr_drive_write"},
        {"RDP_SMARTCARD",      ",rdpdr_smartcard"  }
    };

    static_assert(
        AuthorizationChannels::cliprde_list.size()
      + AuthorizationChannels::rdpdr_list.size()
     == std::extent<decltype(opts_channels)>::value
    , "opts_channels.size() error");

    for (auto & x : opts_channels) {
        ret[(expanded_proxy_opt.find(x.opt) != std::string::npos) ? 0 : 1].get() += x.channel;
    }

    for (std::string & s : ret) {
        if (!s.empty() && s.front() == ',') {
            s.erase(0,1);
        }
    }
}

#endif
