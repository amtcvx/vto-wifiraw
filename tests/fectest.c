/*
gcc -g -O2 -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectest.c -o fectest.o

gcc fectest.o ../obj/zfex.o -g -o exe_fectest

sudo ./fectest

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


typedef struct {
  uint16_t feclen;
} __attribute__((packed)) wfb_utils_fec_t;

#define PAY_MTU 1400

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fec_t)


/*****************************************************************************/
int main(void) {

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);

  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur = 0;
  ssize_t vidlen=0;


  uint8_t vidfd;
  if (-1 == (vidfd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(vidfd, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in vidinaddr;
  vidinaddr.sin_family = AF_INET;
  vidinaddr.sin_port = htons(5600);
  vidinaddr.sin_addr.s_addr =inet_addr("127.0.0.1");
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
        vidlen = readv(vidfd, &iov, 1) + sizeof(wfb_utils_fec_t);
        ((wfb_utils_fec_t *)&vidbuf[vidcur][0])->feclen = vidlen;
        vidcur++;

        printf("(%d)len(%ld)  ",vidcur-1,vidlen);
        for (uint8_t i=0;i<5;i++) printf("%x ",vidbuf[vidcur-1][i]);printf(" ... ");
        for (uint16_t i=vidlen-5;i<vidlen;i++) printf("%x ",vidbuf[vidcur-1][i]);printf("\n");
      }

      if (vidcur == FEC_K) {
        printf("\n");

        vidcur=0;
        unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
        uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)&vidbuf[f];
        uint8_t *fecblocks[FEC_N-FEC_K];
        for (uint8_t f=0; f<(FEC_N - FEC_K); f++) {
          fecblocks[f] = (uint8_t *)&vidbuf[f + FEC_K];
        }
        fec_encode(fec_p,
                    (const gf*restrict const*restrict const)datablocks,
                    (gf*restrict const*restrict const)fecblocks,
                    (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);


        uint8_t *inblocks[FEC_N];
        for (uint8_t k=0;k<FEC_N;k++) if(k<FEC_K) inblocks[k]=&vidbuf[k][0]; else inblocks[k]=fecblocks[k-FEC_K];

        uint8_t misnb=3; printf("MISSING (%d)\n",misnb);inblocks[misnb]=(int8_t *)0;


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
    }
  }
}
