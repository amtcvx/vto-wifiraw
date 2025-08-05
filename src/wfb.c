#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>

#include "wfb.h"
#include "wfb_utils.h"


/*****************************************************************************/
int main(void) {
  uint8_t devcpt;
  uint64_t exptime;
  ssize_t len;

  wfb_utils_init_t putils;
  wfb_utils_init(&putils);
  printf("(%d)\n",putils.rawlimit-1);

  wfb_utils_raw_t raw;

  for(;;) {	
    if (0 != poll(putils.readsets, putils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<putils.nbdev; cpt++) {
        if (putils.readsets[cpt].revents == POLLIN) {
          devcpt = putils.readtab[cpt];
          if (devcpt == 0) {
            len = read(putils.fd[devcpt], &exptime, sizeof(uint64_t));
	    wfb_utils_periodic(putils.stat);
	  } else {
            if ((devcpt > 0)&&(devcpt < putils.rawlimit)) {
  	      printf("RAW (%d)\n",devcpt);
              wfb_utils_presetrawmsg(&raw, true);
              len = recvmsg( putils.fd[devcpt], &raw.rawmsg, MSG_DONTWAIT);

	      if (!((len > 0)&&(raw.pay->droneid >= DRONEIDMIN)&&(raw.pay->droneid <= DRONEIDMAX))) printf("putils.stat.raws[devcpt-1].fails++\n");
	      else {
                printf("piutils.pnet->raws[devcpt-1].incoming++\n");
	      }
	    }
          }
	}
      }
    }
  }
  return(0);
}
