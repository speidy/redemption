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
   Copyright (C) Wallix 2011
   Author(s): Martin Potier, Jonathan Poelen

   Usecond Real (redemption) Time
   wrapper for timeval

*/

#if !defined(__URT_HPP__)
#define __URT_HPP__

#include <sys/time.h>
#include <stdint.h>

/*
 * Keep in mind that the resolution of tv_usec is at best 0,1 sec
 * this if FAR from a µsec...
 */
struct URT
{
//    typedef time_t sec_type;
//    typedef suseconds_t usec_type;

    timeval tv;

    URT(const timeval& tv)
    : tv(tv)
    {}

//    URT(uint64_t usec) {
//        this->tv.tv_sec  = usec / 1000000; // usec to sec
//        this->tv.tv_usec = usec % 1000000; // rest of usec
//    }

//    // Default constructor sets to time of the day
//    URT() {
//        gettimeofday(&(this->tv), NULL);
//    }

//    URT(const URT& other)
//    : tv(other.tv)
//    {}

//    URT(sec_type __sec, usec_type __usec)
//    {
//        this->tv.tv_sec  = __sec;
//        this->tv.tv_usec = __usec;
//    }

//    URT& operator=(const URT& other)
//    {
//        this->tv = other.tv;
//        return *this;
//    }

//    URT& operator=(const timeval& other)
//    {
//        this->tv = other;
//        return *this;
//    }

//    ~URT(){}

};
#endif
