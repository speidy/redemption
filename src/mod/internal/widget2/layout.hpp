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
 *   Author(s): Christophe Grosjean, Raphael Zhou, Jonathan Poelen,
 *              Meng Tan
 *
 */

#ifndef _REDEMPTION_MOD_INTERNAL_WIDGET2_LAYOUT_HPP_
#define _REDEMPTION_MOD_INTERNAL_WIDGET2_LAYOUT_HPP_

#include "widget.hpp"

static const size_t LAYOUT_SIZE_MAX = 50;

struct WidgetLayout : public Widget2 {
    Widget2 * items[LAYOUT_SIZE_MAX];
    size_t    nb_items;
    int bgcolor;

    WidgetLayout(DrawApi & drawable, const Rect & rect, Widget2 & parent,
                 NotifyApi * notifier, int group_id = 0)
        : Widget2(drawable, rect, parent, notifier, group_id)
        , nb_items(0)
        , bgcolor(BLACK)
    {
    }

    virtual ~WidgetLayout() {
    }
    virtual void draw(const Rect& clip) {
        for (size_t i = 0; i < this->nb_items; ++i) {
            Widget2 *w = this->items[i];
            w->refresh(clip.intersect(this->rect));
        }
    }
    virtual void set_xy(int16_t x, int16_t y) {
        for (size_t i = 0, max = this->nb_items; i < max; ++i) {
            Widget2 * w = this->items[i];
            uint16_t dx = w->rect.x - this->rect.x;
            uint16_t dy = w->rect.y - this->rect.y;
            this->items[i]->set_xy(x + dx, y + dy);
        }
        this->rect.x = x;
        this->rect.y = y;
    }
    virtual Widget2 * widget_at_pos(int16_t x, int16_t y)
    {
        Widget2 * ret = 0;
        for(size_t i = 0; i < this->nb_items && ret == 0; ++i) {
            if (this->items[i]->rect.contains_pt(x, y)) {
                ret = this->items[i];
            }
        }
        return ret;
    }

    virtual void rearrange(size_t origin = 0) {
    }

    size_t get_size() {
        return this->nb_items;
    }

    void add_widget(Widget2 * w) {
        if (this->nb_items < LAYOUT_SIZE_MAX) {
            this->items[nb_items] = w;
            this->nb_items++;
            this->rearrange(this->nb_items - 1);
        }
    }

    void remove_widget(Widget2 *w) {
        bool found = false;
        size_t removed = this->nb_items;
        for (size_t i = 0; i < this->nb_items; ++i) {
            if (!found) {
                if (w == this->items[i]) {
                    found = true;
                    this->items[i] = nullptr;
                    removed = i;
                }
            }
            else {
                this->items[i-1] = this->items[i];
            }
        }
        if (found) {
            this->items[this->nb_items] = nullptr;
            this->nb_items--;
            this->rearrange(removed);
        }
    }

    void insert_widget(Widget2 * w, size_t position) {
        if (position < this->nb_items) {
            for (size_t i = this->nb_items; i > position; --i) {
                this->items[i] = this->items[i - 1];
            }
            this->items[position] = w;
            this->nb_items++;
            this->rearrange(position);
        }
        else {
            this->add_widget(w);
        }
    }

// protected:

    // void rearrange_hori(size_t origin = 0) {
    //     size_t index = origin;
    //     int pos_x = this->rect.x;
    //     if (index > 0) {
    //         pos_x = this->items[index - 1]->lx();
    //     }
    //     for (; index < this->nb_items; index++) {
    //         Widget2 * w = this->items[index];
    //         w->set_xy(pos_x, this->rect.y);
    //         pos_x += this->items[index]->cx();
    //         if (w->cy() > this->rect.cy) {
    //             this->rect.cy = w->cy();
    //         }
    //     }
    //     this->rect.cx = pos_x - this->rect.x;
    // }

    // void rearrange_verti(size_t origin = 0) {
    //     size_t index = origin;
    //     int pos_y = this->rect.y;
    //     if (index > 0) {
    //         pos_y = this->items[index - 1]->ly();
    //     }
    //     for (; index < this->nb_items; index++) {
    //         Widget2 * w = this->items[index];
    //         w->set_xy(this->rect.x, pos_y);
    //         pos_y += this->items[index]->cy();
    //         if (w->cx() > this->rect.cx) {
    //             this->rect.cx = w->cx();
    //         }
    //     }
    //     this->rect.cy = pos_y - this->rect.y;
    // }

    // void add_widget_hori(Widget2 * w) {
    //     int pos_x = this->rect.x;
    //     if (this->nb_items > 0) {
    //         pos_x = this->items[this->nb_items - 1]->lx();
    //     }
    //     w->set_xy(pos_x, this->rect.y);
    //     this->rect.cx += w->cx();
    //     if (w->cy() > this->rect.cy) {
    //         this->rect.cy = w->cy();
    //     }
    // }

    // void add_widget_verti(Widget2 * w) {
    //     int pos_y = this->rect.y;
    //     if (this->nb_items > 0) {
    //         pos_y = this->items[this->nb_items - 1]->ly();
    //     }
    //     w->set_xy(this->rect.x, pos_y);
    //     this->rect.cy += w->cy();
    //     if (w->cx() > this->rect.cx) {
    //         this->rect.cx = w->cx();
    //     }
    // }
};


#endif
