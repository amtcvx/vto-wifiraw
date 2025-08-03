#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>

#include "wfb.h"
#include "wfb_utils.h"

extern wfb_utils_pay_t wfb_utils_pay;

/*****************************************************************************/
int main(void) {
  uint8_t devcpt;
  uint64_t exptime;
  ssize_t len;

  wfb_utils_init_t putils;
  wfb_utils_init(&putils);
  printf("(%d)\n",putils.rawlimit-1);

  wfb_utils_rawmsg_t rawmsg[putils.rawlimit - 1];

  for(;;) {	
    if (0 != poll(putils.readsets, putils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<putils.nbdev; cpt++) {
        if (putils.readsets[cpt].revents == POLLIN) {
          devcpt = putils.readtab[cpt];
          if (devcpt == 0) {
            len = read(putils.fd[devcpt], &exptime, sizeof(uint64_t));
	    wfb_utils_periodic();
	  } else {
            if ((devcpt > 0)&&(devcpt < putils.rawlimit)) {
  	      printf("RAW (%d)\n",devcpt);
              wfb_utils_presetrawmsg(&rawmsg[devcpt-1], true);
              len = recvmsg( putils.fd[devcpt], &rawmsg[devcpt-1].msg, MSG_DONTWAIT);

	      if (!((len > 0)&&(wfb_utils_pay.droneid >= DRONEIDMIN)&&(wfb_utils_pay.droneid <= DRONEIDMAX))) putils.pnet->raws[devcpt-1].fails++;
	      else {
                putils.pnet->raws[devcpt-1].incoming++;
	      }
            }
          }
	}
      }
    }
  }

  return(0);
}
