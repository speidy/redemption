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
 *   Author(s): Christophe Grosjean, Dominique Lafages, Jonathan Poelen,
 *              Meng Tan
 */

#if !defined(REDEMPTION_MOD_INTERNAL_WIDGET2_WINDOW_WAB_CLOSE_HPP)
#define REDEMPTION_MOD_INTERNAL_WIDGET2_WINDOW_WAB_CLOSE_HPP

#include "widget2_window.hpp"
#include "button.hpp"
#include "image.hpp"
#include "edit.hpp"
#include "label.hpp"
#include "multiline.hpp"

class WindowWabClose : public Window
{
public:
    WidgetImage img;
    WidgetLabel username_label;
    WidgetLabel username_label_value;
    WidgetLabel target_label;
    WidgetLabel target_label_value;
    WidgetLabel connection_closed_label;
    WidgetButton cancel;
    WidgetLabel diagnostic;
    WidgetMultiLine diagnostic_lines;
    WidgetLabel timeleft_label;
    WidgetLabel timeleft_value;

private:
    long prev_time;

public:
    WindowWabClose(DrawApi& drawable, int16_t x, int16_t y, Widget2& parent,
                   NotifyApi* notifier, const char * diagnostic_text, int group_id = 0,
                   const char * username = 0, const char * target = 0,
                   int fgcolor = BLACK, int bgcolor = DARK_WABGREEN, bool showtimer = false)
    : Window(drawable, Rect(x,y,600,1), parent, notifier, "Close", bgcolor, group_id)
    , img(drawable, 0, 0, SHARE_PATH "/" LOGIN_LOGO24, *this, NULL, -10)
    , username_label(drawable, this->img.cx() + 20, 0, *this, NULL,
                     "Username:", true, -11, fgcolor, bgcolor)
    , username_label_value(drawable, 0, 0, *this, NULL, username, true, -11, fgcolor, bgcolor)
    , target_label(drawable, this->img.cx() + 20, 0, *this, NULL,
                   "Target:", true, -12, fgcolor, bgcolor)
    , target_label_value(drawable, 0, 0, *this, NULL, target, true, -12, fgcolor, bgcolor)
    , connection_closed_label(drawable, 0, 0, *this, NULL, "Connection closed", true, -13, fgcolor, bgcolor)
    , cancel(drawable, 0, 0, *this, this, "Close", true, -14, BLACK, GREY, 6, 2)
    , diagnostic(drawable, this->img.cx() + 20, 0, *this, NULL,
                 "Diagnostic:", true, -15, fgcolor, bgcolor)
    , diagnostic_lines(drawable, this->img.cx() + 20, 0, *this, NULL,
                       diagnostic_text, true, -16, fgcolor, bgcolor)
    , timeleft_label(drawable, this->img.cx() + 20, 0, *this, NULL,
                     "Time left:", true, -12, fgcolor, bgcolor)
    , timeleft_value(drawable, this->img.cx() + 95, 0, *this, NULL, NULL, true, -12, fgcolor, bgcolor)
    , prev_time(0)
    {
        this->cancel.border_top_left_color = WHITE;

        this->img.rect.x = this->dx() + 10;
        this->cancel.set_button_x((this->cx() - this->cancel.cx()) / 2);
        this->connection_closed_label.rect.x = (this->cx() - this->connection_closed_label.cx()) / 2;

        this->resize_titlebar();

        this->add_widget(&this->img);
        this->add_widget(&this->connection_closed_label);
        this->add_widget(&this->cancel);
        this->add_widget(&this->diagnostic);
        this->add_widget(&this->diagnostic_lines);

        uint16_t px = this->diagnostic.cx() + 10;

        y = this->dy() + this->titlebar.cy() + 10;
        this->img.rect.y = y;
        y += 10;

        if (username && *username) {
            this->add_widget(&this->username_label);
            this->add_widget(&this->username_label_value);
            this->add_widget(&this->target_label);
            this->add_widget(&this->target_label_value);

            px = std::max(this->username_label.cx(), this->diagnostic.cx()) + 10;
            this->username_label_value.rect.x = this->username_label.dx() + px;
            this->target_label_value.rect.x = this->username_label.dx() + px;

            this->username_label.rect.y = y;
            this->username_label_value.rect.y = y;
            y += this->username_label.cy() + 10;
            this->target_label.rect.y = y;
            this->target_label_value.rect.y = y;
            y += this->target_label.cy() + 20;
        }

        this->connection_closed_label.rect.y = y;
        y += this->connection_closed_label.cy() + 20;

        this->diagnostic.rect.y = y;
        if (this->diagnostic.cx() > this->cx() - (px + 10)) {
            y += this->diagnostic.cy() + 10;
            this->diagnostic_lines.rect.y = y;
            y += this->diagnostic_lines.cy() + 20;
        }
        else {
            this->diagnostic_lines.rect.y = y;
            y += std::max(this->diagnostic_lines.cy(), this->diagnostic.cy()) + 20;
            this->diagnostic_lines.rect.x = this->username_label.dx() + px;
        }

        if (showtimer) {
            y -= 10;
            this->add_widget(&this->timeleft_label);
            this->add_widget(&this->timeleft_value);
            this->timeleft_label.rect.y = y;
            this->timeleft_value.rect.y = y;
            y += this->timeleft_label.cy() + 10;
        }

        this->cancel.set_button_y(y);
        y += this->cancel.cy() + 20;
        this->set_window_cy(y - this->dy());
    }

    virtual ~WindowWabClose()
    {
        this->clear();
    }

    void refresh_timeleft(long tl) {
        bool seconds = true;
        if (tl > 60) {
            seconds = false;
            tl = tl / 60;
        }
        if (this->prev_time != tl) {
            char buff[128];
            snprintf(buff, sizeof(buff), "%2ld %s%s before closing.",
                     tl,
                     seconds?"second":"minute",
                     (tl <= 1)?"":"s");

            Rect old = this->timeleft_value.rect;
            this->drawable.begin_update();
            this->timeleft_value.set_text(NULL);
            this->draw(old);
            this->timeleft_value.set_text(buff);
            this->draw(this->timeleft_value.rect);
            this->drawable.end_update();

            this->prev_time = tl;
        }
    }

    virtual void notify(Widget2 * widget, NotifyApi::notify_event_t event)
    {
        if (widget == &this->cancel && event == NOTIFY_SUBMIT) {
            this->send_notify(NOTIFY_CANCEL);
        }
        else {
            Window::notify(widget, event);
        }
    }
};

#endif
