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

static void ose_udp_sock(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    int n = ose_getBundleElemCount(vm_s);
    if(n < 2)
    {
        /* error */
        return;
    }
    if(ose_peekType(vm_s) != OSETT_MESSAGE)
    {
        /* error */
        return;
    }
    if(ose_peekMessageArgType(vm_s) != OSETT_INT32)
    {
        /* error */
        return;
    }
    uint32_t port = ose_popInt32(vm_s);
    if(port > 65535)
    {
        /* error */
        return;
    }
    if(ose_getBundleElemCount(vm_s) == n)
    {
        ose_drop(vm_s);
    }
    if(ose_peekType(vm_s) != OSETT_MESSAGE)
    {
        /* error */
        return;
    }
    if(ose_peekMessageArgType(vm_s) != OSETT_STRING)
    {
        /* error */
        return;
    }
    const char * const addr = ose_peekString(vm_s);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(addr);
    sa.sin_port = htons((uint16_t)port);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    bind(sock, (struct sockaddr *)&sa, sizeof(struct sockaddr_in));
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

static void ose_udp_print(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    char buf[8192];
    memset(buf, 0, 8192);
    int32_t n = ose_pprintBundle(vm_s, buf, 8192);
    buf[n++] = '\n';
    buf[n++] = '\r';
    ose_pushString(vm_s, buf);
    
}

void ose_main(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_pushBundle(vm_s);
    ose_pushMessage(vm_s, "/udp/sock", strlen("/udp/sock"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_sock);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/recv", strlen("/udp/recv"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_recv);
    ose_push(vm_s);
    {
        ose_pushMessage(vm_s, "/udp/recv/exec",
                        strlen("/udp/recv/exec"), 0);
        ose_pushString(vm_s, "/!/udp/recv");
        ose_pushString(vm_s, "/>/_e");
        ose_pushString(vm_s, "/!/swap");
        ose_pushString(vm_s, "/s//udp/sender/addr");
        ose_pushString(vm_s, "/!/assign");
        ose_pushString(vm_s, "/!/swap");
        ose_pushString(vm_s, "/!/exec");
        ose_pushString(vm_s, "/</_e");
        ose_pushInt32(vm_s, 8);
        ose_bundleFromTop(vm_s);
    	ose_push(vm_s);
    }
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/print", strlen("/udp/print"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_print);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/udp/sendto", strlen("/udp/sendto"), 1,
                    OSETT_ALIGNEDPTR, ose_udp_sendto);
    ose_push(vm_s);
}
