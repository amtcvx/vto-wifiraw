/*
gcc -g -O2 -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectestserv.c -o fectestserv.o

gcc fectestserv.o ../obj/zfex.o -g -o fectestserv


On 192.168.3.2
sudo ./fectestcli
gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

On 192.168.3.1
sudo ./fectestserv
gst-launch-1.0 videotestsrc ! video/x-raw,framerate=20/1 ! videoconvert ! x265enc ! rtph265pay config-interval=1 ! udpsink host=127.0.0.1 port=5600


apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools

*/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/uio.h>

#include "../src/zfex.h"

#define FEC_K   8
#define FEC_N   12

typedef enum { WFB_TIM, WFB_RAW, WFB_VID, WFB_NB } type_d;

typedef struct {
  uint8_t droneid;
  uint8_t msgcpt;
  uint16_t msglen;
  uint8_t seq;
  uint8_t fec;
  uint8_t num;
  uint8_t dum;
} __attribute__((packed)) wfb_utils_heads_pay_t;

typedef struct {
  uint16_t feclen;
} __attribute__((packed)) wfb_utils_fec_t;

#define PAY_MTU 1400

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fec_t)

#define IP_REMOTE_RAW "192.168.3.2"
#define IP_LOCAL "127.0.0.1"

#define PORT_NORAW  3000
#define PORT_VID  5600

/*****************************************************************************/
int main(void) {

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);
  uint8_t sequence=0;
  uint8_t num=0;

  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur=0;;
  ssize_t vidlen=0;

  uint8_t rawfd;
  ssize_t rawlen;

  if (-1 == (rawfd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(rawfd, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in norawoutaddr;
  norawoutaddr.sin_family = AF_INET;
  norawoutaddr.sin_port = htons(PORT_NORAW);
  norawoutaddr.sin_addr.s_addr = inet_addr(IP_REMOTE_RAW);

  uint8_t vidfd;
  if (-1 == (vidfd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(vidfd, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in vidinaddr;
  vidinaddr.sin_family = AF_INET;
  vidinaddr.sin_port = htons(PORT_VID);
  vidinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL);
  if (-1 == bind( vidfd, (const struct sockaddr *)&vidinaddr, sizeof(vidinaddr))) exit(-1);

  struct pollfd readsets;
  readsets.fd = vidfd;
  readsets.events = POLLIN;

  for(;;) {
    if (0 != poll(&readsets, 1, -1)) {
      if (readsets.revents == POLLIN) {
        memset(&vidbuf[vidcur][0],0,ONLINE_MTU);
        struct iovec iov;
	iov.iov_base = &vidbuf[vidcur][sizeof(wfb_utils_fec_t)];
	iov.iov_len = PAY_MTU;
	vidlen = readv(vidfd,&iov,1) + sizeof(wfb_utils_fec_t);
	((wfb_utils_fec_t *)&vidbuf[vidcur][0])->feclen = vidlen;
	vidcur++;
/*
	uint8_t *ptr = vidbuf[vidcur-1]; ssize_t tmp = ((wfb_utils_fec_t *)ptr)->feclen;
        printf(">>len(%ld)  ",tmp);
        for (uint8_t i=0;i<5;i++) printf("%x ",*(ptr+i));printf(" ... ");
        for (uint16_t i=tmp-5;i<tmp;i++) printf("%x ",*(ptr+i));printf("\n");
*/
      }

      if (vidcur == FEC_K) {
        vidcur=0;
        unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
        uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)vidbuf[f];
        uint8_t *fecblocks[FEC_N-FEC_K];
        for (uint8_t f=0; f<(FEC_N - FEC_K); f++) fecblocks[f] = (uint8_t *)&vidbuf[f + FEC_K];
        fec_encode(fec_p,
                    (const gf*restrict const*restrict const)datablocks,
                    (gf*restrict const*restrict const)fecblocks,
                    (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);
      }

      if (vidlen>0) {
        uint8_t kmin=vidcur-1; uint8_t kmax=kmin+1;
        if (vidcur==0) { kmin=(FEC_K-1); kmax=FEC_N; }

        for (uint8_t k=kmin;k<kmax;k++) {

	  if (k<FEC_K) vidlen=((wfb_utils_fec_t *)&vidbuf[k][0])->feclen; else vidlen=ONLINE_MTU;
 
	  wfb_utils_heads_pay_t headspay =
            { .droneid = 1, .msgcpt = WFB_VID, .msglen = vidlen, .seq = sequence, .fec = k, .num = num++ };
            struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
            struct iovec iovpay = { .iov_base = &vidbuf[k][0], .iov_len = vidlen };
            struct iovec iovtab[2] = {iovheadpay, iovpay};
            struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2, .msg_name = &norawoutaddr, .msg_namelen = sizeof(norawoutaddr) };
/*
            if ((k == 4)||(k == 5)||(k == 6)||(k == 7)) printf("missing (%d)(%d)\n",sequence,k);
            else 
*/
	    rawlen = sendmsg(rawfd, (const struct msghdr *)&msg, MSG_DONTWAIT);
/*
	  if (k<FEC_K) {
	    uint8_t *ptr = vidbuf[k]; ssize_t tmp = ((wfb_utils_fec_t *)ptr)->feclen;
            printf("len(%ld) ",tmp);
            for (uint8_t i=0;i<5;i++) printf("%x ",*(ptr+i));printf(" ... ");
            for (uint16_t i=tmp-5;i<tmp;i++) printf("%x ",*(ptr+i));printf("\n");
	  }
*/
	  vidlen = 0;
          if ((vidcur == 0)&&(k == (FEC_N-1))) { sequence++; } //printf("\n"); }
	}
      }
    }
  }
}
