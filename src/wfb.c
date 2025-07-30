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

  wfb_utils_rawmsg_t rawmsg[putils.rawlimit];

  for(;;) {	
    if (0 != poll(putils.readsets, putils.nbdev, -1)) {
      for (uint8_t cpt=0; cpt<putils.nbdev; cpt++) {
        if (putils.readsets[cpt].revents == POLLIN) {
          devcpt = putils.readtab[cpt];
          if (devcpt == 0) {
            len = read(putils.dev[devcpt].fd, &exptime, sizeof(uint64_t));
	    wfb_utils_periodic();
	  } else {
            if ((devcpt > 0)&&(devcpt < putils.rawlimit)) {
  	      printf("RAW (%d)\n",devcpt);
              wfb_utils_presetrawmsg(&rawmsg[devcpt-1], true);
              len = recvmsg( putils.dev[devcpt].fd, &rawmsg[devcpt-1].msg, MSG_DONTWAIT);
            }
          }
	}
      }
    }
  }

  return(0);
}
