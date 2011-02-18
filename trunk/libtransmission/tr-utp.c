/*
Copyright (c) 2010 by Juliusz Chroboczek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <unistd.h>

#include <event.h>
#include <libutp/utp.h>

#include "transmission.h"
#include "net.h"
#include "session.h"
#include "crypto.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "tr-utp.h"

#ifndef WITH_UTP

int tr_utpPacket(const unsigned char *buf UNUSED, size_t buflen UNUSED,
                 const struct sockaddr *from UNUSED, socklen_t fromlen UNUSED,
                 tr_session *ss UNUSED) { return -1; }

void tr_utpClose( tr_session * ss UNUSED ) { }

void tr_utpSendTo(void *closure UNUSED,
                  const unsigned char *buf UNUSED, size_t buflen UNUSED,
                  const struct sockaddr *to UNUSED, socklen_t tolen UNUSED) { }

#else

/* Greg says 50ms works for them. */

#define UTP_INTERVAL_US 50000

static struct event *utp_timer = NULL;

static void
incoming(void *closure, struct UTPSocket *s)
{
    tr_session *ss = (tr_session*)closure;
    struct sockaddr_storage from_storage;
    struct sockaddr *from = (struct sockaddr*)&from_storage;
    socklen_t fromlen = sizeof(from_storage);
    tr_address addr;
    tr_port port;

    if( !tr_sessionIsUTPEnabled(ss) ) {
        UTP_Close(s);
        return;
    }
    
    UTP_GetPeerName(s, from, &fromlen);
    if(from->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in*)from;
        addr.type = TR_AF_INET;
        addr.addr.addr4.s_addr = sin->sin_addr.s_addr;
        port = sin->sin_port;
    } else if(from->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)from;
        addr.type = TR_AF_INET6;
        addr.addr.addr6 = sin6->sin6_addr;
        port = sin6->sin6_port;
    } else {
        tr_nerr("UTP", "Unknown socket family");
        UTP_Close(s);
        return;
    }

    tr_peerMgrAddIncoming(ss->peerMgr, &addr, port, -1, s);
}

void
tr_utpSendTo(void *closure, const unsigned char *buf, size_t buflen,
             const struct sockaddr *to, socklen_t tolen)
{
    tr_session *ss = (tr_session*)closure;

    if(to->sa_family == AF_INET && ss->udp_socket)
        sendto(ss->udp_socket, buf, buflen, 0, to, tolen);
    else if(to->sa_family == AF_INET6 && ss->udp_socket)
        sendto(ss->udp6_socket, buf, buflen, 0, to, tolen);
}

static void
reset_timer(tr_session *ss)
{
    int sec;
    int usec;
    if( tr_sessionIsUTPEnabled( ss ) ) {
        sec = 0;
        usec = UTP_INTERVAL_US / 2 + tr_cryptoWeakRandInt(UTP_INTERVAL_US);
    } else {
        /* If somebody has disabled uTP, then we still want to run
           UTP_CheckTimeouts, in order to let closed sockets finish
           gracefully and so on.  However, since we're not particularly
           interested in that happening in a timely manner, we might as
           well use a large timeout. */
        sec = 2;
        usec = tr_cryptoWeakRandInt( 1000000 );
    }
    tr_timerAdd( utp_timer, sec, usec );
}

static void
timer_callback(int s UNUSED, short type UNUSED, void *closure)
{
    tr_session *ss = closure;
    UTP_CheckTimeouts();
    reset_timer( ss );
}

int
tr_utpPacket(const unsigned char *buf, size_t buflen,
             const struct sockaddr *from, socklen_t fromlen,
             tr_session *ss)
{
    if(utp_timer == NULL)
    {
        utp_timer = evtimer_new( ss->event_base, timer_callback, ss );
        if(utp_timer == NULL)
            return -1;
        reset_timer( ss );
    }

    return UTP_IsIncomingUTP(incoming, tr_utpSendTo, ss,
                             buf, buflen, from, fromlen);
}

void
tr_utpClose( tr_session * session UNUSED )
{
    if( utp_timer )
    {
        evtimer_del( utp_timer );
        utp_timer = NULL;
    }
}

#endif /* #ifndef WITH_UTP ... else */
