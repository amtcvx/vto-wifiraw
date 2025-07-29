#include <stdio.h>
#include <sys/timerfd.h>

#include "wfb_utils.h"
#include "wfb_net.h"


void wfb_utils_periodic(void) {
  printf("TIC\n");
}


void wfb_utils_init(wfb_utils_init_t *putils) {

  wfb_net_init_t pnet;
  wfb_net_init(&pnet);

  putils->nbdev = FD_NB;
  putils->readtabnb = 0;

  uint8_t devcpt = 0;
  putils->dev[devcpt].fd = timerfd_create(CLOCK_MONOTONIC, 0);
  putils->readsets[putils->readtabnb].fd = putils->dev[devcpt].fd;
  putils->readsets[putils->readtabnb].events = POLLIN;
  struct itimerspec period = { { PERIOD_DELAY_S, 0 }, { PERIOD_DELAY_S, 0 } };
  timerfd_settime(putils->dev[devcpt].fd, 0, &period, NULL);
  putils->readtab[putils->readtabnb] = devcpt;
  (putils->readtabnb) += 1;

  for(uint8_t i=0;i<(pnet.rawnb);i++) {
    devcpt = 1 + i;
    putils->dev[devcpt].fd = pnet.raw[i].fd;
    putils->readsets[putils->readtabnb].fd = putils->dev[devcpt].fd;
    putils->readsets[putils->readtabnb].events = POLLIN;
    putils->readtab[putils->readtabnb] = devcpt;
    (putils->readtabnb) += 1;
  }
}
