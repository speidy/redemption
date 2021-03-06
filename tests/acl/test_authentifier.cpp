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
  Copyright (C) Wallix 2013
  Author(s): Christophe Grosjean, Meng Tan
*/

#define BOOST_AUTO_TEST_MAIN
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE TestAuthentifierNew
#include <boost/test/auto_unit_test.hpp>

#undef SHARE_PATH
#define SHARE_PATH FIXTURES_PATH

#define LOGNULL
//#define LOGPRINT

#include "authentifier.hpp"
#include "module_manager.hpp"
#include "count_transport.hpp"
#include "test_transport.hpp"

struct ActivityAlwaysTrue : ActivityChecker {
    virtual bool check_and_reset_activity() { return true; };
};
struct ActivityAlwaysFalse : ActivityChecker {
    virtual bool check_and_reset_activity() { return false; };
};


BOOST_AUTO_TEST_CASE(TestAuthentifierNoKeepalive)
{
    BackEvent_t signal = BACK_EVENT_NONE;

    Inifile ini;

    ini.globals.keepalive_grace_delay = 30;
    ini.globals.session_timeout = 900;
    ini.debug.auth = 255;

    MMIni mm(ini);

    char outdata[] =
        // Time: 10011
           "\x00\x00\x01\xA1"
           "login\nASK\n"
           "ip_client\n!\n"
           "ip_target\n!\n"
           "target_device\nASK\n"
           "target_login\nASK\n"
           "bpp\n!24\n"
           "height\n!600\n"
           "width\n!800\n"
           "selector\n!False\n"
           "selector_current_page\n!1\n"
           "selector_device_filter\n!\n"
           "selector_group_filter\n!\n"
           "selector_proto_filter\n!\n"
           "selector_lines_per_page\n!0\n"
           "target_password\nASK\n"
           "target_host\nASK\n"
           "proto_dest\nASK\n"
           "password\nASK\n"
           "reporting\n!\n"
           "auth_channel_result\n!\n"
           "auth_channel_target\n!\n"
           "accept_message\n!\n"
           "display_message\n!\n"
           "real_target_device\n!\n"

        // Time: 10043
           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

    ;

//    printf("len=%x\n",
//        (unsigned)strlen(
//        "keepalive\nASK\n"
//        "password\ntotopass\n"
//        "target_device\nwin\n"
//        "target_login\nuser\n"
//        "target_password\nwhoknows\n"
//        "proto_dest\nRDP\n"
//        "authenticated\nTrue\n"
//        ));

//    exit(0);

    char indata[] =
        "\x00\x00\x00\x87"
        "login\ntoto\n"
        "password\ntotopass\n"
        "target_device\nwin\n"
        "target_login\nuser\n"
        "target_password\nwhoknows\n"
        "proto_dest\nRDP\n"
        "module\nRDP\n"
        "authenticated\nTrue\n"

    ;

    TestTransport acl_trans("test", indata, sizeof(indata)-1, outdata, sizeof(outdata)-1);
    ActivityAlwaysTrue activity_checker;
    SessionManager sesman(mm.ini, activity_checker, acl_trans, 10000, 10010);
    signal = BACK_EVENT_NEXT;

    // Ask next_module, send inital data to ACL
    sesman.check(mm, 10011, signal);
    // Receive answer, OK to connect
    sesman.receive();
    // instanciate new mod, start keepalive (proxy ASK keepalive and should receive result in less than keepalive_grace_delay)
    sesman.check(mm, 10012, signal);
    sesman.check(mm, 10042, signal);
    // Send keepalive=ASK
    sesman.check(mm, 10043, signal);
    sesman.check(mm, 10072, signal);
    // still connected
    BOOST_CHECK_EQUAL(mm.last_module, false);
    // If no keepalive is received after 30 seconds => disconnection
    sesman.check(mm, 10073, signal);
    BOOST_CHECK_EQUAL(mm.last_module, true);
}


BOOST_AUTO_TEST_CASE(TestAuthentifierKeepalive)
{

    BackEvent_t signal = BACK_EVENT_NONE;

    Inifile ini;

    ini.globals.keepalive_grace_delay = 30;
    ini.globals.session_timeout = 900;
    ini.debug.auth = 255;

    MMIni mm(ini);

    char outdata[] =
        // Time 10011
           "\x00\x00\x01\xA1"
           "login\nASK\n"
           "ip_client\n!\n"
           "ip_target\n!\n"
           "target_device\nASK\n"
           "target_login\nASK\n"
           "bpp\n!24\n"
           "height\n!600\n"
           "width\n!800\n"
           "selector\n!False\n"
           "selector_current_page\n!1\n"
           "selector_device_filter\n!\n"
           "selector_group_filter\n!\n"
           "selector_proto_filter\n!\n"
           "selector_lines_per_page\n!0\n"
           "target_password\nASK\n"
           "target_host\nASK\n"
           "proto_dest\nASK\n"
           "password\nASK\n"
           "reporting\n!\n"
           "auth_channel_result\n!\n"
           "auth_channel_target\n!\n"
           "accept_message\n!\n"
           "display_message\n!\n"
           "real_target_device\n!\n"

        // Time 10043
           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

    ;

//    printf("len=%x\n",
//        (unsigned)strlen(
//        "keepalive\nASK\n"
//        "password\ntotopass\n"
//        "target_device\nwin\n"
//        "target_login\nuser\n"
//        "target_password\nwhoknows\n"
//        "proto_dest\nRDP\n"
//        "authenticated\nTrue\n"
//        ));

//    exit(0);

    char indata[] =
        "\x00\x00\x00\x87"
        "login\ntoto\n"
        "password\ntotopass\n"
        "target_device\nwin\n"
        "target_login\nuser\n"
        "target_password\nwhoknows\n"
        "proto_dest\nRDP\n"
        "module\nRDP\n"
        "authenticated\nTrue\n"

        // Time 10045
        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        // Time 10072 : bad message
        "\x00\x00\x00\x10"
        "koopalive\n!True\n"

    ;

    TestTransport acl_trans("test", indata, sizeof(indata)-1, outdata, sizeof(outdata)-1);
    ActivityAlwaysTrue activity_checker;
    SessionManager sesman(mm.ini, activity_checker, acl_trans, 10000, 10010);
    signal = BACK_EVENT_NEXT;

    CountTransport keepalivetrans;
    // Ask next_module, send inital data to ACL
    sesman.check(mm, 10011, signal);
    // Receive answer, OK to connect
    sesman.receive();
    // instanciate new mod, start keepalive (proxy ASK keepalive and should receive result in less than keepalive_grace_delay)
    sesman.check(mm, 10012, signal);
    sesman.check(mm, 10042, signal);
    // Send keepalive=ASK
    sesman.check(mm, 10043, signal);

    sesman.receive();
    //  keepalive=True
    sesman.check(mm, 10045, signal);

    // koopalive=True => unknown var...
    sesman.receive();
    sesman.check(mm, 10072, signal);
    sesman.check(mm, 10075, signal);
    BOOST_CHECK_EQUAL(mm.last_module, false);  // still connected

    // Renew Keepalive time:
    // Send keepalive=ASK
    sesman.check(mm, 10076, signal);
    sesman.check(mm, 10105, signal);
    BOOST_CHECK_EQUAL(mm.last_module, false); // still connected

    // Keep alive not received, disconnection
    sesman.check(mm, 10106, signal);
    BOOST_CHECK_EQUAL(mm.last_module, true);  // close box
}

BOOST_AUTO_TEST_CASE(TestAuthentifierInactivity)
{

    BackEvent_t signal = BACK_EVENT_NONE;

    Inifile ini;
    ini.globals.keepalive_grace_delay = 30;
    ini.globals.session_timeout = 240; // => 8*30 = 240secs inactivity
    ini.debug.auth = 255;
    MMIni mm(ini);

    char outdata[] =
        // Time 10011
           "\x00\x00\x01\xA1"
           "login\nASK\n"
           "ip_client\n!\n"
           "ip_target\n!\n"
           "target_device\nASK\n"
           "target_login\nASK\n"
           "bpp\n!24\n"
           "height\n!600\n"
           "width\n!800\n"
           "selector\n!False\n"
           "selector_current_page\n!1\n"
           "selector_device_filter\n!\n"
           "selector_group_filter\n!\n"
           "selector_proto_filter\n!\n"
           "selector_lines_per_page\n!0\n"
           "target_password\nASK\n"
           "target_host\nASK\n"
           "proto_dest\nASK\n"
           "password\nASK\n"
           "reporting\n!\n"
           "auth_channel_result\n!\n"
           "auth_channel_target\n!\n"
           "accept_message\n!\n"
           "display_message\n!\n"
           "real_target_device\n!\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"

           "\x00\x00\x00\x0E"
            "keepalive\nASK\n"
    ;

//    printf("len=%x\n",
//        (unsigned)strlen(
//        "keepalive\nASK\n"
//        "password\ntotopass\n"
//        "target_device\nwin\n"
//        "target_login\nuser\n"
//        "target_password\nwhoknows\n"
//        "proto_dest\nRDP\n"
//        "authenticated\nTrue\n"
//        ));

//    exit(0);

    char indata[] =
        "\x00\x00\x00\x7c"
        "login\ntoto\n"
        "password\ntotopass\n"
        "target_device\nwin\n"
        "target_login\nuser\n"
        "target_password\nwhoknows\n"
        "proto_dest\nRDP\n"
        "authenticated\nTrue\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"

        "\x00\x00\x00\x10"
        "keepalive\n!True\n"
    ;

    TestTransport acl_trans("test", indata, sizeof(indata)-1, outdata, sizeof(outdata)-1);
    CountTransport keepalivetrans;
    ActivityAlwaysFalse activity_checker;
    SessionManager sesman(ini, activity_checker, acl_trans, 10000, 10010);
    signal = BACK_EVENT_NEXT;


    // Ask next_module, send inital data to ACL
    sesman.check(mm, 10011, signal);
    // Receive answer, OK to connect
    sesman.receive();
    // instanciate new mod, start keepalive (proxy ASK keepalive and should receive result in less than keepalive_grace_delay)
    sesman.check(mm, 10012, signal);
    sesman.check(mm, 10042, signal);
    // Send keepalive=ASK
    sesman.check(mm, 10043, signal);

    sesman.receive();
    //  keepalive=True
    sesman.check(mm, 10045, signal);

    // keepalive=True
    sesman.receive();
    sesman.check(mm, 10072, signal);
    sesman.check(mm, 10075, signal);
    BOOST_CHECK_EQUAL(mm.last_module, false);  // still connected

    // Renew Keepalive time:
    // Send keepalive=ASK
    sesman.check(mm, 10076, signal);
    sesman.receive();
    sesman.check(mm, 10079, signal);
    BOOST_CHECK_EQUAL(mm.last_module, false); // still connected


    // Send keepalive=ASK
    sesman.check(mm, 10106, signal);
    sesman.check(mm, 10135, signal);
    BOOST_CHECK_EQUAL(mm.last_module, false); // still connected

    sesman.receive();
    sesman.check(mm, 10136, signal);
    sesman.check(mm, 10165, signal);

    BOOST_CHECK_EQUAL(mm.last_module, false); // still connected


    sesman.check(mm, 10166, signal);
    sesman.receive();
    sesman.check(mm, 10195, signal);
    BOOST_CHECK_EQUAL(mm.last_module, false); // still connected

    sesman.receive();
    sesman.check(mm, 10196, signal);
    sesman.check(mm, 10225, signal);
    BOOST_CHECK_EQUAL(mm.last_module, false); // still connected

    sesman.receive();
    sesman.check(mm, 10227, signal);
    sesman.check(mm, 10255, signal);
    BOOST_CHECK_EQUAL(mm.last_module, true); // disconnected on inactivity
}
