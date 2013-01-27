/* 
 * Copyright (C) 2013 Simon Richter
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "messagepump.h"

extern "C" {
        static AvahiWatch *avahi_watch_new(AvahiPoll const *api, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata);
        static void avahi_watch_update(AvahiWatch *w, AvahiWatchEvent event);
        static AvahiWatchEvent avahi_watch_get_events(AvahiWatch *w);
        static void avahi_watch_free(AvahiWatch *w);
        static AvahiTimeout* avahi_timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback callback, void *userdata);
        static void avahi_timeout_update(AvahiTimeout *, const struct timeval *tv);
        static void avahi_timeout_free(AvahiTimeout *t);
}

struct AvahiWatch
{
        librevisa::messagepump *messagepump;
        int fd;
        AvahiWatchEvent event;
        AvahiWatchCallback callback;
        void *userdata;
};

struct AvahiTimeout
{
        librevisa::messagepump *messagepump;
        timeval tv;
        AvahiTimeoutCallback callback;
        void *userdata;
};

static AvahiWatch *avahi_watch_new(AvahiPoll const *api, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata)
{
        return reinterpret_cast<librevisa::messagepump *>(api->userdata)->watch_new(fd, event, callback, userdata);
}

static void avahi_watch_update(AvahiWatch *w, AvahiWatchEvent event)
{
        w->messagepump->watch_update(w, event);
}

static AvahiWatchEvent avahi_watch_get_events(AvahiWatch *w)
{
        return w->messagepump->watch_get_events(w);
}

static void avahi_watch_free(AvahiWatch *w)
{
        w->messagepump->watch_free(w);
}

static AvahiTimeout* avahi_timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback callback, void *userdata)
{
        return reinterpret_cast<librevisa::messagepump *>(api->userdata)->timeout_new(tv, callback, userdata);
}

static void avahi_timeout_update(AvahiTimeout *t, const struct timeval *tv)
{
        t->messagepump->timeout_update(t, tv);
}

static void avahi_timeout_free(AvahiTimeout *t)
{
        t->messagepump->timeout_free(t);
}

inline AvahiWatchEvent &operator|=(AvahiWatchEvent &lhs, AvahiWatchEvent rhs)
{
        return (lhs = AvahiWatchEvent(unsigned(lhs) | unsigned(rhs)));
}

inline timeval operator+(timeval const &lhs, unsigned int microseconds)
{
        timeval ret = lhs;
        ret.tv_usec += microseconds % 1000000;
        ret.tv_sec += microseconds / 1000000;
        if(ret.tv_usec > 1000000)
        {
                ret.tv_usec -= 1000000;
                ret.tv_sec += 1;
        }
        return ret;
}

inline timeval &operator-=(timeval &lhs, timeval const &rhs)
{
        if(lhs.tv_usec > rhs.tv_usec)
        {
                lhs.tv_sec -= rhs.tv_sec;
                lhs.tv_usec -= rhs.tv_usec;
        }
        else
        {
                lhs.tv_sec = lhs.tv_sec - 1 - rhs.tv_sec;
                lhs.tv_usec = lhs.tv_usec + 1000000 - rhs.tv_usec;
        }
        return lhs;
}

inline bool operator<(timeval const &lhs, timeval const &rhs)
{
        return lhs.tv_sec < rhs.tv_sec ||
                (lhs.tv_sec == rhs.tv_sec && lhs.tv_usec < rhs.tv_usec);
}

namespace librevisa {

struct messagepump::watch
{
        watch(AvahiWatch const &avahi) : interface(AVAHI), avahi(avahi) { }
        watch(watch const &w) : interface(w.interface)
        {
                switch(interface)
                {
                case AVAHI:
                        this->avahi = w.avahi;
                        break;
                }
        }

        enum
        {
                AVAHI
        } interface;
        union
        {
                AvahiWatch avahi;
        };
};

struct messagepump::timeout
{
        timeout(AvahiTimeout const &avahi) : interface(AVAHI), avahi(avahi) { }

        enum
        {
                AVAHI
        } interface;

        union
        {
                AvahiTimeout avahi;
        };
};

messagepump::messagepump() throw()
{
        AvahiPoll::userdata = reinterpret_cast<void *>(this);
        AvahiPoll::watch_new = &avahi_watch_new;
        AvahiPoll::watch_update = &avahi_watch_update;
        AvahiPoll::watch_get_events = &avahi_watch_get_events;
        AvahiPoll::watch_free = &avahi_watch_free;
        AvahiPoll::timeout_new = &avahi_timeout_new;
        AvahiPoll::timeout_update = &avahi_timeout_update;
        AvahiPoll::timeout_free = &avahi_timeout_free;
        return;
}

void messagepump::run(unsigned int stopafter)
{
        timeval now;
        ::gettimeofday(&now, 0);

        timeval const limit = now + stopafter * 1000;

        for(;;)
        {
                timeval next = limit;
                for(std::list<timeout>::iterator i = timeouts.begin(); i != timeouts.end(); ++i)
                {
                        while(i->avahi.tv.tv_sec == -1 && i != timeouts.end())
                                i = timeouts.erase(i);
                        if(i->avahi.tv.tv_sec == 0)
                                continue;
                        if(i->avahi.tv < now)
                        {
                                i->avahi.tv = null_timeout;
                                i->avahi.callback(&i->avahi, i->avahi.userdata);
                                continue;
                        }
                }

                next -= now;

                FD_ZERO(&readfds);
                FD_ZERO(&writefds);
                FD_ZERO(&exceptfds);

                int maxfd = -1;

                for(std::list<watch>::iterator i = watches.begin(); i != watches.end(); ++i)
                {
                        while(i->avahi.fd == -1 && i != watches.end())
                                i = watches.erase(i);

                        if(i->avahi.event & AVAHI_WATCH_IN)
                                FD_SET(i->avahi.fd, &readfds);
                        if(i->avahi.event & AVAHI_WATCH_OUT)
                                FD_SET(i->avahi.fd, &writefds);
                        if(i->avahi.event && i->avahi.fd > maxfd)
                                maxfd = i->avahi.fd;
                }

                int rc = ::select(maxfd + 1, &readfds, &writefds, &exceptfds, &next);
                if(rc == -1)
                        return;
                if(rc > 0)
                {
                        for(std::list<watch>::iterator i = watches.begin(); i != watches.end(); ++i)
                        {
                                AvahiWatchEvent ev = watch_get_events(&i->avahi);
                                if(ev)
                                        i->avahi.callback(&i->avahi, i->avahi.fd, ev, i->avahi.userdata);
                        }
                }

                ::gettimeofday(&now, 0);
                if(limit < now)
                        return;
        }
}

AvahiWatch *messagepump::watch_new(int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata)
{
        AvahiWatch avahi = { this, fd, event, callback, userdata };
        return &watches.insert(watches.end(), avahi)->avahi;
}

void messagepump::watch_update(AvahiWatch *w, AvahiWatchEvent event)
{
        w->event = event;
}

AvahiWatchEvent messagepump::watch_get_events(AvahiWatch *w)
{
        AvahiWatchEvent ret = AvahiWatchEvent(0);
        if(FD_ISSET(w->fd, &readfds))
                ret |= AVAHI_WATCH_IN;
        if(FD_ISSET(w->fd, &writefds))
                ret |= AVAHI_WATCH_OUT;
        return ret;
}

void messagepump::watch_free(AvahiWatch *w)
{
        w->fd = -1;
}

AvahiTimeout *messagepump::timeout_new(timeval const *tv, AvahiTimeoutCallback callback, void *userdata)
{
        if(!tv)
                tv = &null_timeout;
        AvahiTimeout avahi = { this, *tv, callback, userdata };
        return &timeouts.insert(timeouts.end(), avahi)->avahi;
}

void messagepump::timeout_update(AvahiTimeout *t, timeval const *tv)
{
        if(!tv)
                tv = &null_timeout;
        t->tv = *tv;
}

void messagepump::timeout_free(AvahiTimeout *t)
{
        t->tv.tv_sec = -1;
}

timeval const messagepump::null_timeout = { 0, 0 };

messagepump main;

}