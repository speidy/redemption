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
    Author(s): Christophe Grosjean, Raphael Zhou
*/

#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE TestMPPC61
#include <boost/test/auto_unit_test.hpp>

//#define LOGNULL
#define LOGPRINT
#include "log.hpp"

#include "RDP/mppc.hpp"

BOOST_AUTO_TEST_CASE(TestRDP61BlukCompression)
{
    rdp_mppc_enc_match_finder * mppc_enc_match_finder = new rdp_mppc_61_enc_hash_based_match_finder();
    rdp_mppc_61_enc * mppc_61_enc = new rdp_mppc_61_enc(mppc_enc_match_finder);

    delete mppc_61_enc;
    delete mppc_enc_match_finder;
}