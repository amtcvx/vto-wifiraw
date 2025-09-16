/*
gcc -g -O2 -DZFEX_UNROLL_ADDMUL_SIMD=8 -DZFEX_USE_INTEL_SSSE3 -DZFEX_USE_ARM_NEON -DZFEX_INLINE_ADDMUL -DZFEX_INLINE_ADDMUL_SIMD -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -DBOARD=0 -c fectest.c -o fectest.o

gcc fectest.o ../obj/zfex.o -g -o fectest

*/
#include "../src/wfb_utils.h"
#include <string.h>

/*****************************************************************************/
int main(void) {

  wfb_utils_msgout_t utils_msgout;

  uint8_t curr = 0;

  struct iovec *piov = &utils_msgout.iov[WFB_VID][0][curr];

  memset(&utils_msgout.buf_vid[curr][0],0,ONLINE_MTU);
  piov->iov_base = &utils_msgout.buf_vid[curr][sizeof(wfb_utils_fec_t)];
  piov->iov_len = PAY_MTU;
  piov->iov_len = readv( utils.fd[cpt], piov, 1);
  piov->iov_base = &utils_msgout.buf_vid[curr][0];
  piov->iov_len += sizeof(wfb_utils_fec_t);
  ((wfb_utils_fec_t *)piov->iov_base)->feclen = piov->iov_len;

            printf("len(%ld)  ",piov->iov_len);
            for (uint8_t i=0;i<5;i++) printf("%x ",*(((uint8_t *)piov->iov_base)+i));printf(" ... ");
            for (uint16_t i=piov->iov_len-5;i<piov->iov_len;i++) printf("%x ",*(((uint8_t *)piov->iov_base)+i));printf("\n");

            if (utils.rawchan.mainraw == -1) piov->iov_len = 0;
            else if (curr < FEC_K) (utils.msgout.currvid)++;





          kmax = (FEC_N  - 1);
          unsigned blocknums[FEC_N-FEC_K]; for(uint8_t f=0; f<(FEC_N-FEC_K); f++) blocknums[f]=(f+FEC_K);
          uint8_t *datablocks[FEC_K];for (uint8_t f=0; f<FEC_K; f++) datablocks[f] = (uint8_t *)&utils.msgout.buf_vid[f];
          uint8_t *fecblocks[FEC_N-FEC_K];
          for (uint8_t f=0; f<(FEC_N - FEC_K); f++) {
            fecblocks[f] = (uint8_t *)&utils.msgout.buf_vid[f + FEC_K];
            utils.msgout.iov[WFB_VID][0][f + FEC_K].iov_len = ONLINE_MTU;
          }
          fec_encode(utils.fec_p,
                         (const gf*restrict const*restrict const)datablocks,
                         (gf*restrict const*restrict const)fecblocks,
                         (const unsigned*restrict const)blocknums, (FEC_N-FEC_K), ONLINE_MTU);
        }



                    unsigned index[FEC_K];
                    uint8_t *inblocks[FEC_K];
                    uint8_t  alldata=0;
                    uint8_t j=FEC_K;
                    uint8_t idx = 0;
                    for (uint8_t k=0;k<FEC_K;k++) {
                      index[k] = 0;
                      inblocks[k] = (uint8_t *)0;
                      if (k < (FEC_N - FEC_K)) outblocks[k] = (uint8_t *)0;
                      if ( pelt->iovfec[k] ) {
                        inblocks[k] = (uint8_t *)pelt->iovfec[k]->iov_base;
                        index[k] = k;
                        alldata |= (1 << k);
                      } else {
                        for(;j < FEC_N; j++) {
                          if ( pelt->iovfec[j] ) {
                            inblocks[k] = (uint8_t *)pelt->iovfec[j]->iov_base;
                            outblocks[idx] = &outblocksbuf[idx][0]; idx++;
                            index[k] = j;
                            j++;
                            alldata |= (1 << k);
                            break;
                          }
                        }
                      }
                    }
                    if ((alldata == 255)&&(idx > 0)&&(idx <= (FEC_N - FEC_K))) {
                      for (uint8_t k=0;k<FEC_K;k++) printf("%d ",index[k]);
                      printf("\nDECODE (%d)\n",idx);
                      fec_decode(utils.fec_p,
                               (const unsigned char **)inblocks,
                               (unsigned char * const*)outblocks,
                               (unsigned int *)index,
                               ONLINE_MTU);

                      printf("\nrestoring :\n");
                      uint8_t j=0;
                      for (uint8_t k=0;k<FEC_K;k++) {
                        if (!(pelt->iovfec[k])) {
                          iovrecover[k].iov_base = outblocks[j];
                          iovrecover[k].iov_len = ((wfb_utils_fec_t *)outblocks[j])->feclen;
                          pelt->iovfec[k] = &iovrecover[k];
                          struct iovec *ptmp = pelt->iovfec[k];
                          printf("[%d] len(%ld)  ",k,ptmp->iov_len);
//                        for (uint8_t i=0;i<5;i++) printf("%x ",*(((uint8_t *)ptmp->iov_base)+i));printf(" ... ");
//                        for (uint16_t i=ptmp->iov_len-5;i<ptmp->iov_len;i++) printf("%x ",*(((uint8_t *)ptmp->iov_base)+i));printf("\n");
                          for (uint16_t i=0;i<ptmp->iov_len;i++) dump2[i]=*(((uint8_t *)ptmp->iov_base)+i);
                          j++;
                        }
                      }

*/
}
