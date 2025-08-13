#ifndef IO_MONITOR_V3_H
#define IO_MONITOR_V3_H

#include <linux/types.h>
#include <linux/bio.h>

#define MODULE_NAME "io_monitor_v3"
#define MAX_DEVICE_NAME 64

int init_io_history(void);
void cleanup_io_history(void);
void handle_bio_request(struct bio *bio);

dev_t get_target_dev(void);
const char *get_target_device_name(void);

#endif /* IO_MONITOR_V3_H */
