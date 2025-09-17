/*
gcc -g -O2 -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectest.c -o fectest.o

gcc fectest.o ../obj/zfex.o -g -o fectest

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

#define PAY_MTU 1400

typedef struct {
  uint16_t feclen;
} __attribute__((packed)) wfb_utils_fec_t;

#define ONLINE_MTU PAY_MTU + sizeof(wfb_utils_fec_t)


/*****************************************************************************/
int main(void) {

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);

  uint8_t buf_vid[FEC_N][ONLINE_MTU];
  struct iovec iov[FEC_N], *iovfec[FEC_N];
  uint8_t curr = 0;
  for (uint8_t k=0;k<FEC_N;k++) iov[k].iov_len = 0;

  uint8_t fd;
  if (-1 == (fd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(5600);
  addr.sin_addr.s_addr =inet_addr("127.0.0.1");
  if (-1 == bind( fd, (const struct sockaddr *)&addr, sizeof(addr))) exit(-1);
  struct pollfd readsets;
  readsets.fd = fd;
  readsets.events = POLLIN;

  struct iovec *piov;
  for(;;) {
    if (0 != poll(&readsets, 1, -1)) {
      for (uint8_t cpt=0; cpt<1; cpt++) {
        if (readsets.revents == POLLIN) {
          if (cpt == 0) {
            if (curr < (FEC_K+1)) {
              memset(&buf_vid[curr][0],0,ONLINE_MTU);
	      piov = &iov[curr];
              piov->iov_base = &buf_vid[curr][sizeof(wfb_utils_fec_t)];
              piov->iov_len = PAY_MTU;
              piov->iov_len = readv( fd, piov, 1);
              piov->iov_base = &buf_vid[curr][0];
              piov->iov_len += sizeof(wfb_utils_fec_t);
              ((wfb_utils_fec_t *)piov->iov_base)->feclen = piov->iov_len;
  	      curr++;
	    
	      printf("len(%ld)  ",piov->iov_len - sizeof(wfb_utils_fec_t));
	      for (uint8_t i=0;i<5;i++) printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf(" ... ");
	      for (uint16_t i=piov->iov_len-5-sizeof(wfb_utils_fec_t);i<piov->iov_len-sizeof(wfb_utils_fec_t);i++) 
	        printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf("\n");
	    }
	  }
	}
      }

      if (curr == FEC_K) {
        printf("\n");

        curr=0;
        unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
        uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)&buf_vid[f];
        uint8_t *fecblocks[FEC_N-FEC_K];
        for (uint8_t f=0; f<(FEC_N - FEC_K); f++) {
          fecblocks[f] = (uint8_t *)&buf_vid[f + FEC_K];
        }
        fec_encode(fec_p,
                    (const gf*restrict const*restrict const)datablocks,
                    (gf*restrict const*restrict const)fecblocks,
                    (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);

        for (uint8_t k=0;k<FEC_N;k++) {
          iovfec[k] = &iov[k];
          if (k>=FEC_K) { iov[k].iov_base = &buf_vid[k][0]; iov[k].iov_len = ONLINE_MTU; }
	}

	uint8_t mis = 3;
	printf("\nMISSING (%d)\n",mis);
	piov = &iov[mis];
        printf("len(%ld)  ",piov->iov_len - sizeof(wfb_utils_fec_t));
        for (uint8_t i=0;i<5;i++) printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf(" ... ");
        for (uint16_t i=piov->iov_len-5-sizeof(wfb_utils_fec_t);i<piov->iov_len-sizeof(wfb_utils_fec_t);i++)
          printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf("\n");

	iovfec[mis] = (struct iovec *)0;
        printf("\n");

	uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];
        uint8_t *outblocks[FEC_N-FEC_K];
        unsigned index[FEC_K];
        uint8_t *inblocks[FEC_K];
        uint8_t  alldata=0;
        uint8_t j=FEC_K;
        uint8_t idx = 0;
        for (uint8_t k=0;k<FEC_K;k++) {
          index[k] = 0;
          inblocks[k] = (uint8_t *)0;
          if (k < (FEC_N - FEC_K)) outblocks[k] = (uint8_t *)0;
          if ( iovfec[k] ) {
            inblocks[k] = (uint8_t *)iovfec[k]->iov_base;
            index[k] = k;
            alldata |= (1 << k);
          } else {
            for(;j < FEC_N; j++) {
              if ( iovfec[j] ) {
                inblocks[k] = (uint8_t *)iovfec[j]->iov_base;
                outblocks[idx] = &outblocksbuf[idx][0]; idx++;
                index[k] = j;
                j++;
                alldata |= (1 << k);
                break;
              }
            }
          }
        }

	printf("\n");
        if ((alldata == 255)&&(idx > 0)&&(idx <= (FEC_N - FEC_K))) {
          for (uint8_t k=0;k<FEC_K;k++) printf("%d ",index[k]);
          printf("\nDECODE (%d)\n",idx);
          fec_decode(fec_p,
                     (const unsigned char **)inblocks,
                     (unsigned char * const*)outblocks,
                     (unsigned int *)index,
                     ONLINE_MTU);
	}

        printf("\nRESTORING\n");
        uint8_t x=0;
	struct iovec recover[FEC_N-FEC_K];
        for (uint8_t k=0;k<FEC_K;k++) {
          if (!(iovfec[k])) {
            recover[x].iov_base = outblocks[x];
            recover[x].iov_len = ((wfb_utils_fec_t *)outblocks[x])->feclen;
	    iovfec[k] = &recover[x];
            x++;
	    piov = iovfec[k];
            printf("len(%ld)  ",piov->iov_len - sizeof(wfb_utils_fec_t));
            for (uint8_t i=0;i<5;i++) printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf(" ... ");
            for (uint16_t i=piov->iov_len-5-sizeof(wfb_utils_fec_t);i<piov->iov_len-sizeof(wfb_utils_fec_t);i++)
              printf("%x ",*((uint8_t *)(piov->iov_base + i + sizeof(wfb_utils_fec_t))));printf("\n");
	  }
	}
	printf("---------------------------------------------------------------------\n");
      }
    }
  }
}
