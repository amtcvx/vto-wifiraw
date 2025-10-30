/*
gcc -g -O2 -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectestservraw.c -o fectestservraw.o

cc fectestservraw.o ../obj/zfex.o -g -o fectestservraw

sudo rfkill unblock ...

export DEVICE=wlxfc349725a319
sudo ip link set $DEVICE down
sudo iw dev $DEVICE set type monitor
sudo ip link set $DEVICE up
sudo iw dev $DEVICE set channel 18

sudo ./fectestservraw  $DEVICE

gst-launch-1.0 videotestsrc ! video/x-raw,framerate=20/1 ! videoconvert ! x265enc ! rtph265pay config-interval=1 ! udpsink host=127.0.0.1 port=5600


sudo ./fectestcliraw $DEVICE

gst-launch-1.0 udpsrc port=5600 ! application/x-rtp, encoding-name=H265, payload=96 ! rtph265depay ! h265parse ! queue ! avdec_h265 !  videoconvert ! autovideosink sync=false


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
  uint8_t sequence=0;
  uint8_t num=0;

  uint8_t vidbuf[FEC_N][ONLINE_MTU];
  uint8_t vidcur=0;
  ssize_t vidlen=0;

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
        uint8_t *ptr = vidbuf[vidcur-1]; ssize_t tmp = ((wfb_utils_fec_t *)ptr)->feclen - sizeof(wfb_utils_fec_t);
        ptr += sizeof(wfb_utils_fec_t);
        printf("len(%ld) ",tmp);
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

          struct iovec iovtab[5] = { iov_radiotaphd_tx, iov_ieeehd_tx, iov_llchd_tx, iovheadpay, iovpay }; uint8_t msglen = 5;
          struct msghdr msg = { .msg_iov = iovtab, .msg_iovlen = msglen };

          rawlen = sendmsg(sockfd, (const struct msghdr *)&msg, MSG_DONTWAIT);
/*
          if (k<FEC_K) {
            uint8_t *ptr = vidbuf[k]; ssize_t tmp;
            if (k<FEC_K) tmp = ((wfb_utils_fec_t *)ptr)->feclen; else tmp = ONLINE_MTU;
            printf("(%d] len(%ld) ",k,tmp);
            for (uint8_t i=0;i<5;i++) printf("%x ",*(ptr+i));printf(" ... ");
            for (uint16_t i=tmp-5;i<tmp;i++) printf("%x ",*(ptr+i));printf("\n");
          }
*/
          vidlen = 0;
          if ((vidcur == 0)&&(k == (FEC_N-1))) { sequence++; printf("\n"); }

        }
      }
    }
  }
}
