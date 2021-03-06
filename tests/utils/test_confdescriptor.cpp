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
#define BOOST_TEST_MODULE TestConfDescriptor
#include <boost/test/auto_unit_test.hpp>

#define LOGNULL

using namespace std;

#include "confdescriptor.hpp"

BOOST_AUTO_TEST_CASE(TestConfigDescriptorFromFile)
{
    GeneralCaps       generalcaps;
    GeneralCapsLoader generalcaps_loader(generalcaps);

    ConfigurationLoader cfg_loader(generalcaps_loader, FIXTURES_PATH "/capsset.ini");

    BOOST_CHECK_EQUAL(generalcaps.os_major,              1);
    BOOST_CHECK_EQUAL(generalcaps.os_minor,              3);
    BOOST_CHECK_EQUAL(generalcaps.protocolVersion,       512);
    BOOST_CHECK_EQUAL(generalcaps.compressionType,       0);
    BOOST_CHECK_EQUAL(generalcaps.extraflags,            1025);
    BOOST_CHECK_EQUAL(generalcaps.updateCapability,      0);
    BOOST_CHECK_EQUAL(generalcaps.remoteUnshare,         0);
    BOOST_CHECK_EQUAL(generalcaps.compressionLevel,      0);
    BOOST_CHECK_EQUAL(generalcaps.refreshRectSupport,    1);
    BOOST_CHECK_EQUAL(generalcaps.suppressOutputSupport, 1);
}

BOOST_AUTO_TEST_CASE(TestConfigDescriptorFromFile1)
{
    BitmapCaps       bitmapcaps;
    BitmapCapsLoader bitmapcaps_loader(bitmapcaps);

    ConfigurationLoader cfg_loader(bitmapcaps_loader, FIXTURES_PATH "/capsset.ini");

    BOOST_CHECK_EQUAL(bitmapcaps.preferredBitsPerPixel,    8);
    BOOST_CHECK_EQUAL(bitmapcaps.receive1BitPerPixel,      1);
    BOOST_CHECK_EQUAL(bitmapcaps.receive4BitsPerPixel,     1);
    BOOST_CHECK_EQUAL(bitmapcaps.receive8BitsPerPixel,     1);
    BOOST_CHECK_EQUAL(bitmapcaps.desktopWidth,             1024);
    BOOST_CHECK_EQUAL(bitmapcaps.desktopHeight,            768);
    BOOST_CHECK_EQUAL(bitmapcaps.desktopResizeFlag,        1);
    BOOST_CHECK_EQUAL(bitmapcaps.bitmapCompressionFlag,    1);
    BOOST_CHECK_EQUAL(bitmapcaps.highColorFlags,           0);
    BOOST_CHECK_EQUAL(bitmapcaps.drawingFlags,             0);
    BOOST_CHECK_EQUAL(bitmapcaps.multipleRectangleSupport, 1);
}

BOOST_AUTO_TEST_CASE(TestConfigDescriptorFromFile2)
{
    OrderCaps       ordercaps;
    OrderCapsLoader ordercaps_loader(ordercaps);

    ConfigurationLoader cfg_loader(ordercaps_loader, FIXTURES_PATH "/capsset.ini");

    BOOST_CHECK_EQUAL(ordercaps.desktopSaveXGranularity,                       1);
    BOOST_CHECK_EQUAL(ordercaps.desktopSaveYGranularity,                       20);
    BOOST_CHECK_EQUAL(ordercaps.maximumOrderLevel,                             1);
    BOOST_CHECK_EQUAL(ordercaps.numberFonts,                                   0);
    BOOST_CHECK_EQUAL(ordercaps.orderFlags,                                    34);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_DSTBLT_INDEX],             1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_PATBLT_INDEX],             1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_SCRBLT_INDEX],             1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MEMBLT_INDEX],             1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MEM3BLT_INDEX],            1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_DRAWNINEGRID_INDEX],       0);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_LINETO_INDEX],             1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTI_DRAWNINEGRID_INDEX], 0);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_SAVEBITMAP_INDEX],         1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTIDSTBLT_INDEX],        1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTIPATBLT_INDEX],        1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTISCRBLT_INDEX],        1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTIOPAQUERECT_INDEX],    1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_FAST_INDEX_INDEX],         1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_POLYGON_SC_INDEX],         1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_POLYGON_CB_INDEX],         1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_POLYLINE_INDEX],           1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_FAST_GLYPH_INDEX],         1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_ELLIPSE_SC_INDEX],         1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_ELLIPSE_CB_INDEX],         1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_INDEX_INDEX],              1);
    BOOST_CHECK_EQUAL(ordercaps.orderSupportExFlags,                           0);
    BOOST_CHECK_EQUAL(ordercaps.desktopSaveSize,                               1000000);
    BOOST_CHECK_EQUAL(ordercaps.textANSICodePage,                              0);
}



BOOST_AUTO_TEST_CASE(TestConfigDescriptorFromFile3)
{
    GeneralCaps       generalcaps;
    GeneralCapsLoader generalcaps_loader(generalcaps);

    ConfigurationLoader cfg_loader(generalcaps_loader, FIXTURES_PATH "/capsset1.ini");

    BOOST_CHECK_EQUAL(generalcaps.os_major,              2);
    BOOST_CHECK_EQUAL(generalcaps.os_minor,              7);
    BOOST_CHECK_EQUAL(generalcaps.protocolVersion,       256);
    BOOST_CHECK_EQUAL(generalcaps.compressionType,       3);
    BOOST_CHECK_EQUAL(generalcaps.extraflags,            768);
    BOOST_CHECK_EQUAL(generalcaps.updateCapability,      5);
    BOOST_CHECK_EQUAL(generalcaps.remoteUnshare,         4);
    BOOST_CHECK_EQUAL(generalcaps.compressionLevel,      3);
    BOOST_CHECK_EQUAL(generalcaps.refreshRectSupport,    2);
    BOOST_CHECK_EQUAL(generalcaps.suppressOutputSupport, 6);
}

BOOST_AUTO_TEST_CASE(TestConfigDescriptorFromFile4)
{
    BitmapCaps       bitmapcaps;
    BitmapCapsLoader bitmapcaps_loader(bitmapcaps);

    ConfigurationLoader cfg_loader(bitmapcaps_loader, FIXTURES_PATH "/capsset1.ini");

    BOOST_CHECK_EQUAL(bitmapcaps.preferredBitsPerPixel,    9);
    BOOST_CHECK_EQUAL(bitmapcaps.receive1BitPerPixel,      8);
    BOOST_CHECK_EQUAL(bitmapcaps.receive4BitsPerPixel,     7);
    BOOST_CHECK_EQUAL(bitmapcaps.receive8BitsPerPixel,     6);
    BOOST_CHECK_EQUAL(bitmapcaps.desktopWidth,             1440);
    BOOST_CHECK_EQUAL(bitmapcaps.desktopHeight,            900);
    BOOST_CHECK_EQUAL(bitmapcaps.desktopResizeFlag,        5);
    BOOST_CHECK_EQUAL(bitmapcaps.bitmapCompressionFlag,    4);
    BOOST_CHECK_EQUAL(bitmapcaps.highColorFlags,           3);
    BOOST_CHECK_EQUAL(bitmapcaps.drawingFlags,             2);
    BOOST_CHECK_EQUAL(bitmapcaps.multipleRectangleSupport, 0);
}

BOOST_AUTO_TEST_CASE(TestConfigDescriptorFromFile5)
{
    OrderCaps       ordercaps;
    OrderCapsLoader ordercaps_loader(ordercaps);

    ConfigurationLoader cfg_loader(ordercaps_loader, FIXTURES_PATH "/capsset1.ini");

    BOOST_CHECK_EQUAL(ordercaps.desktopSaveXGranularity,                       9);
    BOOST_CHECK_EQUAL(ordercaps.desktopSaveYGranularity,                       8);
    BOOST_CHECK_EQUAL(ordercaps.maximumOrderLevel,                             7);
    BOOST_CHECK_EQUAL(ordercaps.numberFonts,                                   6);
    BOOST_CHECK_EQUAL(ordercaps.orderFlags,                                    5);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_DSTBLT_INDEX],             9);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_PATBLT_INDEX],             8);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_SCRBLT_INDEX],             7);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MEMBLT_INDEX],             6);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MEM3BLT_INDEX],            5);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_DRAWNINEGRID_INDEX],       4);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_LINETO_INDEX],             3);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTI_DRAWNINEGRID_INDEX], 2);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_SAVEBITMAP_INDEX],         9);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTIDSTBLT_INDEX],        8);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTIPATBLT_INDEX],        7);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTISCRBLT_INDEX],        6);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_MULTIOPAQUERECT_INDEX],    5);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_FAST_INDEX_INDEX],         4);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_POLYGON_SC_INDEX],         3);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_POLYGON_CB_INDEX],         2);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_POLYLINE_INDEX],           9);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_FAST_GLYPH_INDEX],         8);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_ELLIPSE_SC_INDEX],         7);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_ELLIPSE_CB_INDEX],         6);
    BOOST_CHECK_EQUAL(ordercaps.orderSupport[TS_NEG_INDEX_INDEX],              5);
    BOOST_CHECK_EQUAL(ordercaps.orderSupportExFlags,                           9);
    BOOST_CHECK_EQUAL(ordercaps.desktopSaveSize,                               20000);
    BOOST_CHECK_EQUAL(ordercaps.textANSICodePage,                              8);
}
