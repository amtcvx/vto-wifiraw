#ifndef WFB_UTILS_H
#define WFB_UTILS_H

#include <stdint.h>
#include <poll.h>

#include "wfb.h"

typedef struct {
  uint8_t fd;
} dev_t;

typedef struct {
  uint8_t readtabnb;
  uint8_t readtab[FD_NB];
  struct pollfd readsets[FD_NB];
  uint8_t nbdev;
  dev_t dev[FD_NB];
  uint8_t rawlimit;
} wfb_utils_init_t;

void wfb_utils_periodic(void);
void wfb_utils_init(wfb_utils_init_t *putils);

#endif // WFB_UTILS_H
