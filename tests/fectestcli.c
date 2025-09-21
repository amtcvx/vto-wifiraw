/*
gcc -g -O2 -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectestcli.c -o fectestcli.o

gcc fectestcli.o ../obj/zfex.o -g -o fectestcli


On 192.168.3.2
sudo ./fectestcli
gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false

On 192.168.3.1
sudo ./fectestserv
gst-launch-1.0 videotestsrc ! video/x-raw,framerate=20/1 ! videoconvert ! x265enc ! rtph265pay config-interval=1 ! udpsink host=127.0.0.1 port=5600


apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools

*/
#include <stdbool.h>
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

#define MAXNBRAWBUF 2*FEC_N

#define IP_LOCAL_RAW "192.168.3.2"

/*****************************************************************************/
int main(void) {

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);

  ssize_t rawlen;
  uint8_t rawbuf[MAXNBRAWBUF][ONLINE_MTU];
  uint8_t rawcur=0;

  uint8_t *inblocks[FEC_N];
  ssize_t vidlen=0;

  uint8_t rawfd; 
  if (-1 == (rawfd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(rawfd, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in norawinaddr;
  norawinaddr.sin_family = AF_INET;
  norawinaddr.sin_port = htons(3000);
  norawinaddr.sin_addr.s_addr =inet_addr(IP_LOCAL_RAW);
  if (-1 == bind( rawfd, (const struct sockaddr *)&norawinaddr, sizeof(norawinaddr))) exit(-1);

  struct pollfd readsets;
  readsets.fd = rawfd;
  readsets.events = POLLIN;

  uint8_t msgincurseq=0;
  uint8_t msginnxtseq=0;
  uint8_t msginnxtfec=0;
  bool msginfails = false;
  uint8_t *inblocksto;
  int8_t fecsto=-1;


  for(;;) {
    if (0 != poll(&readsets, 1, -1)) {
      if (readsets.revents == POLLIN) {

        wfb_utils_heads_pay_t headspay;
        memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));

        struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
        struct iovec iovpay = { .iov_base = &rawbuf[rawcur][0], .iov_len = ONLINE_MTU };
        struct iovec iovtab[2] = {iovheadpay, iovpay};

        struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 2 };
        rawlen = recvmsg(rawfd, &msg, MSG_DONTWAIT);

	if (rawlen > 0) {
          if(headspay.msgcpt == WFB_VID) {


//	    if (headspay.fec == 0) { printf("Missing (%d)\n",headspay.fec); inblocks[headspay.fec]=(uint8_t *)0; break; }


            if (rawcur < (MAXNBRAWBUF-1)) rawcur++; else rawcur=0;

	    printf("(%d)   (%d)\n",headspay.seq,headspay.fec);

            uint8_t imax=0, imin=0;
            if ((msginnxtseq != headspay.seq)||(msginnxtfec != headspay.fec)) {
              msginfails = true; printf("KO (%d)(%d)  (%d)(%d)\n",msginnxtseq,headspay.seq,msginnxtfec,headspay.fec);
              if (headspay.fec < (FEC_N-1)) { msginnxtfec=(headspay.fec+1); msginnxtseq=headspay.seq; }
              else {
                msginnxtfec=0;
                if (headspay.seq < 254) msginnxtseq=(headspay.seq+1); else msginnxtseq=0;
              }
            } else {
              printf("OK\n");
              if (msginnxtfec < (FEC_N-1)) msginnxtfec++;
              else { msginnxtfec=0; if (msginnxtseq < 255) msginnxtseq++; else msginnxtseq=0; }
              if (headspay.fec < FEC_K) {imin=headspay.fec; imax=(1+imin); }
            }


            if (msgincurseq == headspay.seq) { inblocks[headspay.fec]=iovpay.iov_base; printf("in|%d)\n",headspay.fec); }
            else {
              inblocksto=iovpay.iov_base; fecsto=headspay.fec; printf("sso(%d)\n",headspay.fec);
              msgincurseq = headspay.seq;
	      exit(-1);
	    }
/*

	    if (headspay.fec==(FEC_N-1)) {
              unsigned index[FEC_K];
              uint8_t recov[FEC_K];
              uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];
              uint8_t *outblocks[FEC_N-FEC_K];
              uint8_t outblocksidx=0;
              for (uint8_t i=0;i<FEC_K;i++) {
                if (inblocks[i]) index[i]=i;
                else {
                  for (uint8_t j=FEC_K;j<FEC_N;j++) {
                    if (inblocks[j]) {
                      inblocks[i]=inblocks[j];
                      index[i]=j;
                      outblocks[outblocksidx]=&outblocksbuf[outblocksidx][0];
                      recov[outblocksidx]=i;
                      outblocksidx++;
                      break;
                    }
                  }
                }
              }
    

              for (uint8_t k=0;k<FEC_K;k++) printf("%d ",index[k]);
              printf("\nDECODE (%d)\n",outblocksidx);
              fec_decode(fec_p,
                           (const unsigned char **)inblocks,
                           (unsigned char * const*)outblocks,
                           (unsigned int *)index,
                           ONLINE_MTU);
      
              printf("RESTORING\n");
              for (uint8_t k=0;k<outblocksidx;k++) {
                inblocks[recov[k]] = outblocks[k];
      
                uint8_t *ptr=inblocks[recov[k]];
                vidlen = ((wfb_utils_fec_t *)ptr)->feclen;
      
                printf("(%d)len(%ld)  ",recov[k],vidlen);
                for (uint8_t i=0;i<5;i++) printf("%x ",*(ptr+i));printf(" ... ");
                for (uint16_t i=vidlen-5;i<vidlen;i++) printf("%x ",*(ptr+i));printf("\n");
              }
              printf("\n");
	    }
*/
	  }
	}
      }
    }
  }
}
