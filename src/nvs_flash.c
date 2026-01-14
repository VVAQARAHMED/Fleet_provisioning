
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include "nvs_flash.h"
#include "fleet_provisioning.h"

static struct nvs_fs fs;

#define NVS_PARTITION		storage_partition
#define NVS_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(NVS_PARTITION)

/* 1000 msec = 1 sec */
#define SLEEP_TIME      100
/* maximum reboot counts, make high enough to trigger sector change (buffer */
/* rotation). */
#define MAX_REBOOT 400

#define ADDRESS_ID 1
#define KEY_ID 2
#define RBT_CNT_ID 3
#define STRING_ID 4
#define LONG_ID 5





int8_t init_nvs_sys()
{
int rc;
struct flash_pages_info info;
fs.flash_device = NVS_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		printk("Flash device %s is not ready\n", fs.flash_device->name);
		return -1;
	}
    fs.offset = NVS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		printk("Unable to get page info\n");
		return -1;
	}
	fs.sector_size = info.size;
	fs.sector_count = 3U;
    rc = nvs_mount(&fs);
	if (rc) {
		printk("Flash init failed\n");
		return -1;
	}
	return 0;

}
int8_t write_to_nvs(uint8_t id,uint8_t *ptr)
{

	int err=0;
	err=nvs_write(&fs, id, ptr,
			  sizeof(uint8_t));
    if(err < 0)
	   {
		 printk("Failed to write in NVS%d\n",err);
		 return -1;
	   }
	//printk("Successfully write NVS write in NVS%d\n",*ptr);
	return 0;

}

int8_t read_from_nvs(uint8_t id,uint8_t *ptr)
{ int err;
    //uint8_t status[1];
    err = nvs_read(&fs,id,ptr, sizeof(u_int8_t));
	if(err!=sizeof(uint8_t))
	  {
		printk("Failed to read from NVS%d\n",err);
		return -1;
	  }
	 //printk("Successfully read status from NVS %d\n",*ptr);
	 return 0;


}

