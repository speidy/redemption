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
   Copyright (C) Wallix 2012
   Author(s): Christophe Grosjean

   Transport layer abstraction
*/

#ifndef REDEMPTION_TRANSPORT_INFILETRANSPORT_HPP
#define REDEMPTION_TRANSPORT_INFILETRANSPORT_HPP

#include "transport.hpp"
#include "fdbub.hpp"

#include <cerrno>


class InFileTransport
: public Transport
{
    int fd;
    //uint32_t verbose;

public:
    InFileTransport(int fd, unsigned verbose = 0)
    : fd(fd)
    //, verbose(verbose)
    {}

    using Transport::recv;
    virtual void recv(char ** pbuffer, size_t len) throw (Error)
    {
        const ssize_t res = io::posix::read_all(this->fd, *pbuffer, len);
        if (res <= 0){
            throw Error(ERR_TRANSPORT_READ_FAILED, errno);
        }
        *pbuffer += res;
        if (static_cast<size_t>(res) != len){
            throw Error(ERR_TRANSPORT_NO_MORE_DATA, errno);
        }
    }

    using Transport::send;
    virtual void send(const char * const buffer, size_t len) throw (Error) {
        throw Error(ERR_TRANSPORT_INPUT_ONLY_USED_FOR_RECV, 0);
    }

    virtual void seek(int64_t offset, int whence) throw (Error) {
        throw Error(ERR_TRANSPORT_SEEK_NOT_AVAILABLE);
    }
};

#endif
