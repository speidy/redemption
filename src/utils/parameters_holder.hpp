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
    Copyright (C) Wallix 2014
    Author(s): Christophe Grosjean, Raphael Zhou, Jonathan Poelen
*/

#ifndef _REDEMPTION_UTILS_PARAMETERSHOLDER_HPP_
#define _REDEMPTION_UTILS_PARAMETERSHOLDER_HPP_

struct parameters_holder {
    virtual ~parameters_holder() = default;

    virtual const char * get_crypto_key_0() {
        static const char key[] = {
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
            "\x10\x11\x12\x13\x14\x15\x16\x17"
            "\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
        };

        return key;
    }

    virtual const char * get_crypto_key_1() {
        static const char key[] = {
            "\x00\x01\x02\x03\x04\x05\x06\x07"
            "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
            "\x10\x11\x12\x13\x14\x15\x16\x17"
            "\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
        };

        return key;
    }

    virtual const char * get_agent_alternate_shell() const {
        return "cmd /k ";
    }
};

#endif  // #ifndef _REDEMPTION_UTILS_PARAMETERSHOLDER_HPP_

