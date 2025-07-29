#include <stdio.h>
#include <poll.h>
#include <unistd.h>


#include "wfb.h"
#include "wfb_utils.h"

/*****************************************************************************/
int main(void) {
  uint8_t devcpt;
  uint64_t exptime;
  ssize_t len;
  printf("HELLO\n");

  wfb_utils_init_t putils;
  wfb_utils_init(&putils);

  printf("(%d)(%d)\n",putils.readtabnb,putils.nbdev);

  int deb = 1;
  int fin = 3;

  for(;;) {	
    if (0 != poll(putils.readsets, putils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<putils.nbdev; cpt++) {
        if (putils.readsets[cpt].revents == POLLIN) {
          devcpt = putils.readtab[cpt];
          if (devcpt == TIME_FD) {
            len = read(putils.dev[devcpt].fd, &exptime, sizeof(uint64_t));
	    wfb_utils_periodic();
	  } else {
            if ((devcpt > TIME_FD)&&(devcpt < putils.rawlimit)) {
  	      printf("RAW (%d)\n",devcpt);
/*
              wfb_utils_presetrawmsg(&rawmsg[devcpt - RAW0_FD][rawcur], ONLINE_MTU, true);
              len = recvmsg( dev[devcpt].fd, &rawmsg[devcpt - RAW0_FD][rawcur].msg, MSG_DONTWAIT);

	      break;
*/
	    }
          }
	}
      }
    }
  }

  return(0);
}
