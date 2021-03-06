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
   Author(s): Christophe Grosjean

   Unit test to RDP Orders coder/decoder
   Using lib boost functions for testing
*/

#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE TestOrderColCache
#include <boost/test/auto_unit_test.hpp>

#include "RDP/orders/RDPOrdersSecondaryBmpCache.hpp"
#include "client_info.hpp"

#include "test_orders.hpp"

BOOST_AUTO_TEST_CASE(TestBmpCacheV1NoCompressionLargeHeaders)
{
    using namespace RDP;

    {
        BStream stream;
        ClientInfo ci;
        ci.bitmap_cache_version = 1;
        const int use_bitmap_comp = 0;

        const uint8_t data[] = {
            /* line 0 */
            0x00, 0x00, 0xFF,  0x00, 0x00, 0xFF,  0x00, 0x00, 0xFF,  0x00, 0x00, 0xFF,
            0x00, 0x00, 0xFF,  0x00, 0x00, 0xFF,  0x00, 0x00, 0xFF,  0x00, 0x00, 0xFF,
        };
        Bitmap bmp(24, 24, nullptr, 8, 1, data, sizeof(data), false);
        RDPBmpCache newcmd(bmp, 1, 10, false, false);
        newcmd.emit(24, stream, ci.bitmap_cache_version, use_bitmap_comp, ci.use_compact_packets);

        uint8_t datas[] = {
            STANDARD | SECONDARY,       // control = 0x03
            0x1a, 0x00, // 26: little-endian, length after orderType - 7
            0x08, 0x00, // extra Flags, nothing special, I don't know if using 8 here is of any significance
            0x00, // type = TS_CACHE_BITMAP_UNCOMPRESSED
            0x01, // cache_id = 1
            0x00, // 1 byte padding
            0x08, // cx = 8
            0x01, // cy = 1
            0x18, // 24 bits color depth
            0x18, 0x00, // cy * row_size = 8 * 3 Bytes per pixel
            0x0a, 0x00, // cache index = 10
            // bitmap data uncompressed
            0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff,
            0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff,
        };

        check_datas(stream.p-stream.get_data(), stream.get_data(), sizeof(datas), datas, "Bmp Cache 1");
        stream.mark_end();
        stream.p = stream.get_data();

        uint8_t control = stream.in_uint8();
        BOOST_CHECK_EQUAL(true, !!(control & (STANDARD|SECONDARY)));
        RDPSecondaryOrderHeader header(stream);
        BOOST_CHECK_EQUAL(static_cast<uint16_t>(33) - 7, header.order_length); // length after type - 7
        BOOST_CHECK_EQUAL((unsigned)0x08, header.flags);
        BOOST_CHECK_EQUAL((unsigned)TS_CACHE_BITMAP_UNCOMPRESSED, header.type);

        RDPBmpCache cmd;
//        cmd.receive(stream, control, header);

//        check<RDPColCache>(cmd, newcmd, "Color Cache 1");
    }
}
