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
 *   Copyright (C) Wallix 2010-2013
 *   Author(s): Christophe Grosjean, Dominique Lafages, Jonathan Poelen
 */

#if !defined(REDEMPTION_MOD_INTERNAL_WIDGET2_PASSWORD_HPP)
#define REDEMPTION_MOD_INTERNAL_WIDGET2_PASSWORD_HPP

#include "label.hpp"

class WidgetPassword : public Widget
{
public:
    static const size_t buffer_size = 256;
    char buffer[buffer_size];
    char display_pass[buffer_size];
    size_t buf_size;
    size_t buf_pos;
    size_t num_chars;
    size_t edit_pos;
    int w_char;
    int h_char;
    int x_text;
    int y_text;
    int bg_color;
    int fg_color;
    bool auto_resize;

    WidgetPassword(ModApi* drawable, int16_t x, int16_t y, uint16_t cx,
                   Widget* parent, NotifyApi* notifier, const char * text,
                   int id = 0, int bgcolor = BLACK, int fgcolor = WHITE,
                   std::size_t edit_position = -1, int xtext = 0, int ytext = 0)
    : Widget(drawable, Rect(x,y,cx,1), parent, notifier, id)
    , buf_size(0)
    , buf_pos(0)
    , num_chars(0)
    , edit_pos(0)
    , w_char(0)
    , h_char(0)
    , x_text(xtext)
    , y_text(ytext)
    , bg_color(bgcolor)
    , fg_color(fgcolor)
    , auto_resize(auto_resize)
    {
        this->buffer[0] = 0;
        this->display_pass[0] = 0;
        if (text && *text) {
            this->buf_size = std::min(buffer_size - 1, strlen(text));
            this->buffer[this->num_chars] = 0;
            this->num_chars = UTF8Len(text);
            this->edit_pos = std::min(this->num_chars, edit_position);
            this->buf_pos = UTF8GetPos(reinterpret_cast<uint8_t *>(this->buffer), this->edit_pos);
            for (size_t n = 0; n < this->num_chars; ++n) {
                this->display_pass[n] = '*';
            }
            this->display_pass[this->num_chars] = 0;
        }
        this->rect.cy = 0;
        if (this->drawable) {
            this->drawable->text_metrics("*", this->w_char, this->h_char);
            this->rect.cy = this->y_text * 2 + this->h_char;
        }
    }

    virtual ~WidgetPassword()
    {}

    virtual void draw(const Rect& clip)
    {
        this->drawable->draw(RDPOpaqueRect(clip, this->bg_color), this->rect);
        this->drawable->server_draw_text(this->x_text + this->dx(),
                                         this->y_text + this->dy(),
                                         this->display_pass,
                                         this->fg_color,
                                         this->rect.intersect(clip)
                                         );
        this->draw_cursor(clip);
    }

    void draw_cursor(const Rect& clip)
    {
        Rect cursor_clip = clip.intersect(
            Rect(this->x_text + this->edit_pos * this->w_char + (this->edit_pos ? (this->edit_pos-1)*2 : 0) + this->dx(),
                 this->y_text + this->dy(),
                 1,
                 this->h_char)
        );
        if (!cursor_clip.isempty()) {
            this->drawable->draw(
                RDPOpaqueRect(
                    cursor_clip,
                    this->fg_color
                ), this->rect
            );
        }
    }

    virtual void rdp_input_mouse(int device_flags, int x, int y, Keymap2* keymap)
    {
        if (device_flags == CLIC_BUTTON1_DOWN) {
            if (x <= this->dx() + this->x_text) {
                this->edit_pos = 0;
                this->buf_pos = 0;
            }
            else if (x <= this->dx() + this->x_text + this->w_char) {
                if (this->num_chars) {
                    this->edit_pos = 1;
                    this->buf_pos = 0;
                    this->buf_pos = this->utf8len_current_char();
                }
            }
            else if (x >= int(this->num_chars * (this->w_char+2) + this->dx() + this->x_text)) {
                if (this->edit_pos < this->num_chars) {
                    this->edit_pos = this->num_chars;
                    this->buf_pos = this->buf_size-1;
                }
            } else {
                this->edit_pos = std::min<size_t>((x - this->dx() + this->x_text) / (this->w_char+2), this->num_chars-1);
                this->buf_pos = UTF8GetPos(reinterpret_cast<uint8_t*>(&this->buffer[0]), this->edit_pos);
            }
        } else {
            Widget::rdp_input_mouse(device_flags, x, y, keymap);
        }
    }

    size_t utf8len_current_char()
    {
        size_t len = 0;
        while (this->buffer[this->buf_pos + len] >> 6 == 2){
            ++len;
        }
        return len;
    }

    size_t utf8len_prevent_char()
    {
        size_t len = 1;
        while (this->buffer[this->buf_pos - len] >> 6 == 2){
            ++len;
        }
        return len-1;
    }

    virtual void rdp_input_scancode(long int param1, long int param2, long int param3, long int param4, Keymap2* keymap)
    {
        if (keymap->nb_kevent_available() > 0){
            switch (keymap->top_kevent()){
                case Keymap2::KEVENT_LEFT_ARROW:
                case Keymap2::KEVENT_UP_ARROW:
                    keymap->get_kevent();
                    if (this->edit_pos > 0) {
                        --this->edit_pos;
                        this->buf_pos -= this->utf8len_prevent_char();
                    }
                    break;
                case Keymap2::KEVENT_RIGHT_ARROW:
                case Keymap2::KEVENT_DOWN_ARROW:
                    keymap->get_kevent();
                    if (this->edit_pos < this->num_chars) {
                        this->buf_pos += this->utf8len_current_char();
                        ++this->edit_pos;
                    }
                    break;
                case Keymap2::KEVENT_BACKSPACE:
                    keymap->get_kevent();
                    if (this->edit_pos > 0) {
                        --this->edit_pos;
                        size_t d = this->utf8len_current_char();
                        std::memmove(&this->buffer[this->buf_pos-d],
                                     &this->buffer[this->buf_pos],
                                     this->buf_size - this->buf_pos);
                        this->buf_size -= d;
                        this->buf_pos -= d;
                        --this->num_chars;
                        this->display_pass[this->num_chars] = 0;
                    }
                    break;
                case Keymap2::KEVENT_DELETE:
                    keymap->get_kevent();
                    if (this->edit_pos < this->num_chars) {
                        size_t d = this->utf8len_current_char();
                        std::memmove(&this->buffer[this->buf_pos],
                                     &this->buffer[this->buf_pos+d],
                                     this->buf_size - this->buf_pos);
                        this->buf_size -= d;
                        --this->num_chars;
                        this->display_pass[this->num_chars] = 0;
                    }
                    break;
                case Keymap2::KEVENT_END:
                    keymap->get_kevent();
                    if (this->edit_pos < this->num_chars) {
                        this->edit_pos = this->num_chars;
                        this->buf_pos = this->buf_size-1;
                    }
                    break;
                case Keymap2::KEVENT_HOME:
                    keymap->get_kevent();
                    this->edit_pos = 0;
                    this->buf_pos = 0;
                    break;
                case Keymap2::KEVENT_KEY:
                    if (this->num_chars < this->buffer_size - 5) {
                        uint32_t c = keymap->get_char();
                        UTF8InsertOneAtPos(reinterpret_cast<uint8_t *>(this->buffer + this->buf_pos), 0, c, this->buffer_size - 1 - this->buf_pos);
                        ++this->edit_pos;
                        this->display_pass[this->num_chars] = '*';
                        ++this->num_chars;
                        this->display_pass[this->num_chars] = '\0';
                        size_t d = this->utf8len_current_char();
                        this->buf_size += d;
                        this->buf_pos += d;
                        this->send_notify(NOTIFY_TEXT_CHANGED);
                    }
                    keymap->get_kevent();
                    break;
                case Keymap2::KEVENT_ENTER:
                    keymap->get_kevent();
                    this->send_notify(NOTIFY_SUBMIT);
                    break;
                default:
                    break;
            }
        }
    }
};

#endif