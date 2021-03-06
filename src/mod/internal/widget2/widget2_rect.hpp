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
 *   Copyright (C) Wallix 2010-2012
 *   Author(s): Christophe Grosjean, Dominique Lafages, Jonathan Poelen,
 *              Meng Tan
 */

#if !defined(REDEMPTION_MOD_INTERNAL_WIDGET2_WIDGET_RECT_HPP)
#define REDEMPTION_MOD_INTERNAL_WIDGET2_WIDGET_RECT_HPP

#include "widget.hpp"
#include "colors.hpp"
#include "RDP/orders/RDPOrdersPrimaryOpaqueRect.hpp"

class WidgetRect : public Widget2
{
public:
    int color;

public:
    WidgetRect(DrawApi & drawable, const Rect& rect, Widget2 & parent, NotifyApi * notifier, int group_id = 0, int color = BLACK)
    : Widget2(drawable, rect, parent, notifier, group_id)
    , color(color)
    {
        this->tab_flag = IGNORE_TAB;
        this->focus_flag = IGNORE_FOCUS;
    }

    virtual void draw(const Rect& clip)
    {
        this->drawable.draw(
            RDPOpaqueRect(
                clip,
                this->color
            ), this->rect
        );
    }
};

#endif

