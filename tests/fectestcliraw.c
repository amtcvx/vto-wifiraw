/*
gcc -g -O2 -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectestcliraw.c -o fectestcliraw.o

gcc -g -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectestcliraw.c -o fectestcliraw.o

gcc fectestcliraw.o ../obj/zfex.o -g -o fectestcliraw

export DEVICE=wlx54c9ff02e7ae
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set channel 18


sudo ./fectestservraw $DEVICE

gst-launch-1.0 videotestsrc ! video/x-raw,framerate=20/1 ! videoconvert ! x265enc ! rtph265pay config-interval=1 ! udpsink host=127.0.0.1 port=5600


sudo ./fectestcliraw $DEVICE

gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false



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

#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/filter.h>

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

#define IP_LOCAL "127.0.0.1"

#define PORT_VID  5600

/************************************************************************************************/
#define IEEE80211_RADIOTAP_MCS_HAVE_BW    0x01
#define IEEE80211_RADIOTAP_MCS_HAVE_MCS   0x02
#define IEEE80211_RADIOTAP_MCS_HAVE_GI    0x04

#define IEEE80211_RADIOTAP_MCS_HAVE_STBC  0x20

#define IEEE80211_RADIOTAP_MCS_BW_20    0
#define IEEE80211_RADIOTAP_MCS_SGI      0x04

#define IEEE80211_RADIOTAP_MCS_STBC_1  1
#define IEEE80211_RADIOTAP_MCS_STBC_SHIFT 5

#define MCS_KNOWN (IEEE80211_RADIOTAP_MCS_HAVE_MCS | IEEE80211_RADIOTAP_MCS_HAVE_BW | IEEE80211_RADIOTAP_MCS_HAVE_GI | IEEE80211_RADIOTAP_MCS_HAVE_STBC )

#define MCS_FLAGS  (IEEE80211_RADIOTAP_MCS_BW_20 | IEEE80211_RADIOTAP_MCS_SGI | (IEEE80211_RADIOTAP_MCS_STBC_1 << IEEE80211_RADIOTAP_MCS_STBC_SHIFT))

#define MCS_INDEX  2

/************************************************************************************************/

uint8_t radiotaphd_rx[35];
uint8_t ieeehd_rx[24];
uint8_t llchd_rx[4];

uint8_t radiotaphd_tx[] = {
        0x00, 0x00, // <-- radiotap version
        0x0d, 0x00, // <- radiotap header length
        0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags:  RADIOTAP_TX_FLAGS + RADIOTAP_MCS
        0x08, 0x00,  // RADIOTAP_F_TX_NOACK
        MCS_KNOWN , MCS_FLAGS, MCS_INDEX // bitmap, flags, mcs_index
};
uint8_t ieeehd_tx[] = {
        0x08, 0x01,                         // Frame Control : Data frame from STA to DS
        0x00, 0x00,                         // Duration
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Receiver MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Transmitter MAC
        0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // Destination MAC
        0x10, 0x86                          // Sequence control
};
uint8_t llchd_tx[4] = {1,2,3,4};

struct iovec iov_radiotaphd_rx = { .iov_base = radiotaphd_rx, .iov_len = sizeof(radiotaphd_rx)};
struct iovec iov_ieeehd_rx =     { .iov_base = ieeehd_rx,     .iov_len = sizeof(ieeehd_rx)};
struct iovec iov_llchd_rx =      { .iov_base = llchd_rx,      .iov_len = sizeof(llchd_rx)};

struct iovec iov_radiotaphd_tx = { .iov_base = radiotaphd_tx, .iov_len = sizeof(radiotaphd_tx)};
struct iovec iov_ieeehd_tx =     { .iov_base = ieeehd_tx,     .iov_len = sizeof(ieeehd_tx)};
struct iovec iov_llchd_tx =      { .iov_base = llchd_tx,      .iov_len = sizeof(llchd_tx)};

/*****************************************************************************/
int main(int argc, char **argv) {

  fec_t *fec_p;
  fec_new(FEC_K, FEC_N, &fec_p);

  uint8_t sockfd;
  ssize_t rawlen;
  uint16_t protocol = htons(ETH_P_ALL);

   if (-1 == (sockfd = socket(AF_PACKET,SOCK_RAW,protocol))) exit(-1);
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(struct ifreq));
  strncpy( ifr.ifr_name, argv[1], sizeof( ifr.ifr_name ) - 1 );
  if (ioctl( sockfd, SIOCGIFINDEX, &ifr ) < 0 ) exit(-1);
  struct sockaddr_ll sll;
  memset( &sll, 0, sizeof( sll ) );
  sll.sll_family   = AF_PACKET;
  sll.sll_ifindex  = ifr.ifr_ifindex;
  sll.sll_protocol = protocol;
  if (-1 == bind(sockfd, (struct sockaddr *)&sll, sizeof(sll))) exit(-1);

  struct sock_filter zero_bytecode = BPF_STMT(BPF_RET | BPF_K, 0);
  struct sock_fprog zero_program = { 1, &zero_bytecode};
  setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &zero_program, sizeof(zero_program));
  char drain[1];
  while (recv(sockfd, drain, sizeof(drain), MSG_DONTWAIT) >= 0) printf("----\n");
  struct sock_filter full_bytecode = BPF_STMT(BPF_RET | BPF_K, (u_int)-1);
  struct sock_fprog full_program = { 1, &full_bytecode};
  setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &full_program, sizeof(full_program));

  const int32_t sock_qdisc_bypass = 1;
  if (-1 == setsockopt(sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &sock_qdisc_bypass, sizeof(sock_qdisc_bypass))) exit(-1);

  uint8_t vidfd;
  if (-1 == (vidfd = socket(AF_INET, SOCK_DGRAM, 0))) exit(-1);
  if (-1 == setsockopt(vidfd, SOL_SOCKET, SO_REUSEADDR , &(int){1}, sizeof(int))) exit(-1);
  struct sockaddr_in vidoutaddr;
  vidoutaddr.sin_family = AF_INET;
  vidoutaddr.sin_port = htons(PORT_VID);
  vidoutaddr.sin_addr.s_addr = inet_addr(IP_LOCAL);

  struct pollfd readsets;
  readsets.fd = sockfd;
  readsets.events = POLLIN;

  uint8_t rawbuf[MAXNBRAWBUF][ONLINE_MTU];
  uint8_t rawcur=0;

  unsigned index[FEC_K];
  uint8_t *inblocks[FEC_K+1];
  uint8_t *outblocks[FEC_N-FEC_K];
  uint8_t outblockrecov[FEC_N-FEC_K];
  uint8_t outblocksbuf[FEC_N-FEC_K][ONLINE_MTU];

  uint8_t inblocksnb=0;
  uint8_t recovcpt=0;

  int8_t inblockstofec=-1;

  int8_t failfec=-1;

  uint8_t msginnxtfec=0;

  int16_t msginnxtseq=-1;
  int16_t msgincurseq=-1;

  bool alldata = false;
  bool bypassflag = false;
  bool clearflag = false;

  ssize_t vidlen=0;


  for(;;) {
    if (0 != poll(&readsets, 1, -1)) {
      if (readsets.revents == POLLIN) {

        wfb_utils_heads_pay_t headspay;
        memset(&headspay,0,sizeof(wfb_utils_heads_pay_t));
        memset(&rawbuf[rawcur][0],0,ONLINE_MTU);

        struct iovec iovheadpay = { .iov_base = &headspay, .iov_len = sizeof(wfb_utils_heads_pay_t) };
        struct iovec iovpay = { .iov_base = &rawbuf[rawcur][0], .iov_len = ONLINE_MTU };

        struct iovec iovtab[5] = { iov_radiotaphd_rx, iov_ieeehd_rx, iov_llchd_rx, iovheadpay, iovpay };
        memset(iov_llchd_rx.iov_base, 0, sizeof(iov_llchd_rx));

        struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = 5 };
        rawlen = recvmsg(sockfd, &msg, MSG_DONTWAIT);

        if (rawlen > 0) {

          if(headspay.msgcpt == WFB_VID) {

            if (rawcur < (MAXNBRAWBUF-1)) rawcur++; else rawcur=0;

            if (headspay.fec < FEC_K) {

              if (msgincurseq < 0) msgincurseq = headspay.seq;

	            int16_t nextseqtmp = msginnxtseq; if (nextseqtmp < 255) nextseqtmp++ ; else nextseqtmp = 0;

              if ((inblockstofec >= 0) && (failfec < 0) &&
                (((msginnxtseq == headspay.seq) && (msginnxtfec != headspay.fec)) ||
	              ((nextseqtmp == headspay.seq) && (msginnxtfec == (FEC_K - 1))))) {

	              failfec = msginnxtfec;
	              if (failfec == 0) bypassflag = false;
	            }

              if (headspay.fec < (FEC_K-1)) msginnxtfec = headspay.fec+1;
              else { msginnxtfec = 0; if (headspay.seq < 255) msginnxtseq = headspay.seq+1; else msginnxtseq = 0; }
            }

            uint8_t imax=0, imin=0;
            if (msgincurseq == headspay.seq) {

              if (headspay.fec < FEC_K) {

                if ((failfec < 0) || ((failfec > 0) && (headspay.fec < failfec))) { imin = headspay.fec; imax = (imin+1); }
                inblocks[headspay.fec] = iovpay.iov_base; index[headspay.fec] = headspay.fec; inblocksnb++;

              } else {

                for (uint8_t k=0;k<FEC_K;k++) if (!(inblocks[k])) {
                  inblocks[k] = iovpay.iov_base; index[k] = headspay.fec;
                  outblocks[recovcpt]=&outblocksbuf[recovcpt][0]; outblockrecov[recovcpt] = k; recovcpt++;
                  break;
                }
              }

            } else {

//              printf("[%d] (%d)\n",headspay.seq,failfec);

              msgincurseq = headspay.seq;
              inblocks[FEC_K] = iovpay.iov_base;
              clearflag=true;

              imin = FEC_K; imax = (FEC_K+1);

              if (inblockstofec >= 0) {

                if ((failfec == 0) && (!(bypassflag))) { imin = 0; imax = 0; }

                if ((failfec > 0) || ((failfec == 0) && (bypassflag))) {

                  imin = failfec;

                  if ((recovcpt + inblocksnb) != (FEC_K-1))  { for (uint8_t k=0;k<recovcpt;k++) inblocks[ outblockrecov[k] ] = 0; }
                  else {

                    imin = outblockrecov[0];

                    alldata = true;
                    for (uint8_t k=0;k<FEC_K;k++) if (!(inblocks[k])) { printf("unset (%d)\n",k); alldata = false; break; }
                    if (alldata) {

                      for (uint8_t k=0;k<FEC_K;k++) printf("%d ",index[k]);
                      printf("\nDECODE (%d)\n",recovcpt);

                      fec_decode(fec_p,
                                 (const unsigned char **)inblocks,
                                 (unsigned char * const*)outblocks,
                                 (unsigned int *)index,
                                 ONLINE_MTU);

                      for (uint8_t k=0;k<recovcpt;k++) {
                        inblocks[ outblockrecov[k] ] = outblocks[k];

                        uint8_t *ptr=inblocks[ outblockrecov[k] ];
                        vidlen = ((wfb_utils_fec_t *)ptr)->feclen - sizeof(wfb_utils_fec_t);
                        if (vidlen <= PAY_MTU) {
                          ptr += sizeof(wfb_utils_fec_t);
//                        printf("recover len(%ld)  ", vidlen);
                          for (uint8_t i=0;i<5;i++) printf("%x ",*(ptr+i));printf(" ... ");
                          for (uint16_t i=vidlen-5;i<vidlen;i++) printf("%x ",*(ptr+i));printf("\n");
                        } else {
//                        printf("missed recovered (%d)(%d)\n",headspay.seq,failfec);
                        }
                      }
                    }
                  }
                }
              }
            }

            for (uint8_t i=imin;i<imax;i++) {
              uint8_t *ptr=inblocks[i];
              if (ptr) {
                vidlen = ((wfb_utils_fec_t *)ptr)->feclen - sizeof(wfb_utils_fec_t);
                if (vidlen <= PAY_MTU) {
                  ptr += sizeof(wfb_utils_fec_t);
/*
                  printf("len(%ld) ",vidlen);
                  for (uint8_t j=0;j<5;j++) printf("%x ",*(j + ptr));printf(" ... ");
                  for (uint16_t j=vidlen-5;j<vidlen;j++) printf("%x ",*(j + ptr));
                  printf("\n");
*/
                  vidlen = sendto(vidfd, ptr, vidlen, MSG_DONTWAIT, (struct sockaddr *)&vidoutaddr, sizeof(vidoutaddr));
                } else {
//                  printf("miss send\n");
                }
              }
            }

            if (clearflag) {

              if ((failfec == 0)&&(!(bypassflag))) bypassflag = true;
              else failfec = -1;

              clearflag=false;
              msginnxtseq = headspay.seq;
              inblockstofec = headspay.fec;

              memset(inblocks, 0, (FEC_K * sizeof(uint8_t *)));

              recovcpt=0;
              if (headspay.fec < FEC_K) { inblocks[headspay.fec] = inblocks[FEC_K]; inblocksnb=1; index[headspay.fec] = headspay.fec; }
              else inblocksnb=0;
            }
          }
        }
      }
    }
  }
}
