/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   Product name: redemption, a FLOSS RDP proxy
   Copyright (C) Wallix 2010
   Author(s): Christophe Grosjean, Javier Caverni
   Based on xrdp Copyright (C) Jay Sorg 2004-2010

   Use (implemented) basic RDP orders to draw some known test pattern

*/

#if !defined(__TEST_CARD_HPP__)
#define __TEST_CARD_HPP__

struct test_card_mod : public internal_mod {
    test_card_mod(
        wait_obj * event,
        ModContext & context, FrontAPI & front):
            internal_mod(front)
    {
        this->event = event;
        this->event->set();
    }

    virtual ~test_card_mod()
    {
    }

    virtual void rdp_input_invalidate(const Rect & rect)
    {
    }

    virtual void rdp_input_mouse(int device_flags, int x, int y, const Keymap * keymap)
    {
    }

    virtual void rdp_input_scancode(long param1, long param2, long param3, long param4, const Keymap * keymap, const key_info* ki){
    }

    virtual void rdp_input_synchronize(uint32_t time, uint16_t device_flags, int16_t param1, int16_t param2)
    {
    }

    // event from back end (draw event from remote or internal server)
    // returns module continuation status, 0 if module want to continue
    // non 0 if it wants to stop (to run another module)
    virtual BackEvent_t draw_event()
    {
        this->draw();
        this->event->reset();
        return BACK_EVENT_NONE;
    }


    void draw()
    {
        BGRPalette palette;
        this->gd.server_begin_update();

        const Rect & clip = this->get_screen_rect();

        this->gd.draw(RDPOpaqueRect(this->get_screen_rect(), WHITE), clip);
        this->gd.draw(RDPOpaqueRect(this->get_screen_rect().shrink(5), RED), clip);
        this->gd.draw(RDPOpaqueRect(this->get_screen_rect().shrink(10), GREEN), clip);
        this->gd.draw(RDPOpaqueRect(this->get_screen_rect().shrink(15), BLUE), clip);
        this->gd.draw(RDPOpaqueRect(this->get_screen_rect().shrink(20), BLACK), clip);

        Rect winrect = this->get_screen_rect().shrink(30);
        this->gd.draw(RDPOpaqueRect(winrect, WINBLUE), clip);

        Bitmap bitmap(24, SHARE_PATH "/" "Philips_PM5544_640.bmp");
        this->gd.bitmap_update(bitmap,
            Rect(winrect.x + (winrect.cx - bitmap.cx)/2,
                 winrect.y + (winrect.cy - bitmap.cy)/2,
                 bitmap.cx, bitmap.cy),
             0, 0, 0xCC, palette, clip);

        //  lineTo mix_mode=1 startx=200 starty=1198 endx=200 endy=145 bg_color=0 rop2=13 clip=(200, 145, 1, 110)
        this->gd.draw(
            RDPLineTo(1, 200, 1198, 200, 145, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(200, 145, 1, 110));

        this->gd.draw(
            RDPLineTo(1, 200, 145, 200, 1198, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(200, 145, 1, 110));

        this->gd.draw(
            RDPLineTo(1, 201, 1198, 200, 145, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(200, 145, 1, 110));

        this->gd.draw(
            RDPLineTo(1, 200, 145, 201, 1198, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(200, 145, 1, 110));

        this->gd.draw(
            RDPLineTo(1, 1198, 200, 145, 200, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(145, 200, 110, 1));

        this->gd.draw(
            RDPLineTo(1, 145, 200, 1198, 200, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(145, 200, 110, 1));

        this->gd.draw(
            RDPLineTo(1, 1198, 201, 145, 200, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(145, 200, 110, 1));

        this->gd.draw(
            RDPLineTo(1, 145, 200, 1198, 201, 0, 13, RDPPen(0, 1, 0x0000FF)),
            Rect(145, 200, 110, 1));

        this->gd.server_draw_text(30, 30, "White", BLACK, WHITE, clip);
        this->gd.server_draw_text(30, 50, "Red  ", BLACK, RED, clip);
        this->gd.server_draw_text(30, 70, "Green", BLACK, GREEN, clip);
        this->gd.server_draw_text(30, 90, "Blue ", BLACK, BLUE, clip);
        this->gd.server_draw_text(30, 110, "Black", WHITE, BLACK, clip);

        Bitmap card(24, SHARE_PATH "/" REDEMPTION_LOGO24);
        this->gd.bitmap_update(card,
            Rect(this->get_screen_rect().cx - card.cx - 30,
                 this->get_screen_rect().cy - card.cy - 30, card.cx, card.cy),
             0, 0, 0xCC, this->gd.palette332, clip);

        // Bogus square generating zero width/height tiles if not properly guarded
        TODO(" find a better fix than the current one in bitmap_update and mem_blt for the case occuring when drawing square below or similar.")
        uint8_t comp64x64RED[] = {
            0xc0, 0x30, 0x00, 0x00, 0xFF,
            0xf0, 0xc0, 0x0f,
        };

        Bitmap bloc64x64(24, &this->gd.palette332, 64, 64, comp64x64RED, sizeof(comp64x64RED), true );
        this->gd.bitmap_update(bloc64x64,
            Rect(0, this->get_screen_rect().cy - 64, bloc64x64.cx, bloc64x64.cy),
             32, 32, 0xCC, this->gd.palette332, clip);

        Bitmap logo(24, SHARE_PATH "/ad24b.bmp");
        this->gd.bitmap_update(logo,
            Rect(100, 100, logo.cx, logo.cy),
             0, 0, 0xCC, this->gd.palette332, clip);

        this->gd.server_end_update();
    }

};

#endif
