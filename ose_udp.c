/*
  Copyright (c) 2019-22 John MacCallum Permission is hereby granted,
  free of charge, to any person obtaining a copy of this software
  and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the
  rights to use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell copies of the Software, and to permit
  persons to whom the Software is furnished to do so, subject to the
  following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
*/

/* memset */
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "ose.h"
#include "ose_stackops.h"
#include "ose_assert.h"
#include "ose_vm.h"
#include "ose_print.h"

static int getsockargs(ose_bundle bundle,
                       char **addr,
                       uint16_t *port)
{
    int n = ose_getBundleElemCount(bundle);
    if(n < 2)
    {
        /* error */
        return 1;
    }
    if(ose_peekType(bundle) != OSETT_MESSAGE)
    {
        /* error */
        return 1;
    }
    if(ose_peekMessageArgType(bundle) != OSETT_INT32)
    {
        /* error */
        return 1;
    }
    uint16_t p = ose_popInt32(bundle);
    if(p > 65535)
    {
        /* error */
        return 1;
    }
    if(ose_getBundleElemCount(bundle) == n)
    {
        ose_drop(bundle);
    }
    if(ose_peekType(bundle) != OSETT_MESSAGE)
    {
        /* error */
        return 1;
    }
    if(ose_peekMessageArgType(bundle) != OSETT_STRING)
    {
        /* error */
        return 1;
    }
    char *a = ose_peekString(bundle);
    if(!a)
    {
        return 1;
    }
    *port = p;
    *addr = a;
    return 0;
}

/* static struct sockaddr_in makesockaddr(const char * const addr, */
/*                                        uint16_t port); */
static int makesockaddr(ose_bundle bundle,
                        struct sockaddr_in *sa)
{
    uint16_t port = 0;
    char *addr = NULL;
    int r = getsockargs(bundle, &addr, &port);
    if(r)
    {
        ose_pushInt32(bundle, port);
        return 1;
    }
    memset(sa, 0, sizeof(struct sockaddr_in));
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = inet_addr(addr);
    sa->sin_port = htons((uint16_t)port);
    return 0;
}

static int32_t makesock(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    return sock;
}

static void ose_udp_sockCreate(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    int32_t sock = makesock();
    ose_pushInt32(vm_s, sock);
}

static void ose_udp_sockBind(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    
    struct sockaddr_in sa;
    int r = makesockaddr(vm_s, &sa);
    if(r)
    {
        return;
    }
    int32_t sock = makesock();
    bind(sock, (struct sockaddr *)&sa,
         sizeof(struct sockaddr_in));
    ose_drop(vm_s);             /* address */
    ose_pushInt32(vm_s, sock);
}

static void ose_udp_sockConnect(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    
    struct sockaddr_in sa;
    int r = makesockaddr(vm_s, &sa);
    if(r)
    {
        return;
    }
    int32_t sock = makesock();
    connect(sock, (struct sockaddr *)&sa,
            sizeof(struct sockaddr_in));
    ose_drop(vm_s);             /* address */
    ose_pushInt32(vm_s, sock);
}

static void ose_udp_recv(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    uint16_t fd = (uint16_t)ose_popInt32(vm_s);
    struct sockaddr_in ca;
    socklen_t ca_len = sizeof(struct sockaddr_in);
    memset(&ca, 0, sizeof(struct sockaddr_in));
    char buf[65536];
    int len = recvfrom(fd,
                       buf, 65536, 0,
                       (struct sockaddr *)&ca,
                       &ca_len);
    ose_pushBlob(vm_s, len, buf);
    ose_blobToElem(vm_s);
    int32_t addr = ca.sin_addr.s_addr;
    addr = ose_ntohl(addr);
    snprintf(buf, 16, "%d.%d.%d.%d",
             (addr & 0xff000000) >> 24,
             (addr & 0x00ff0000) >> 16,
             (addr & 0x0000ff00) >> 8,
             addr & 0x000000ff);
    ose_pushMessage(vm_s, "/udp/sender/addr",
                    strlen("/udp/sender/addr"), 1,
                    OSETT_STRING, buf);
}

static void ose_udp_send(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    /* arg check */
    int32_t sock = ose_popInt32(vm_s);
    int32_t o = ose_getLastBundleElemOffset(vm_s);
    const char * const b = ose_getBundlePtr(vm_s);
    send(sock, b + o + 4, ose_readInt32(vm_s, o), 0);
    ose_drop(vm_s);
}

static void ose_udp_sendto(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    /* arg check */
    int32_t sock = ose_popInt32(vm_s);
    int32_t port = ose_popInt32(vm_s);
    char addr[16];
    memset(addr, 0, 16);
    ose_popString(vm_s, addr);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(addr);
    sa.sin_port = htons((uint16_t)port);
    int32_t o = ose_getLastBundleElemOffset(vm_s);
    const char * const b = ose_getBundlePtr(vm_s);
    sendto(sock, b + o + 4, ose_readInt32(vm_s, o), 0,
           (struct sockaddr *)&sa,
           sizeof(struct sockaddr_in));
    ose_drop(vm_s);
}

void ose_main(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_pushBundle(vm_s);
    ose_pushMessage(vm_s, "/udp/sock/create", strlen("/udp/sock/create"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_sockCreate);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/sock/bind", strlen("/udp/sock/bind"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_sockBind);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/sock/connect", strlen("/udp/sock/connect"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_sockConnect);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/recv", strlen("/udp/recv"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_recv);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/sendto", strlen("/udp/sendto"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_sendto);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/send", strlen("/udp/send"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_send);
    ose_push(vm_s);
}
