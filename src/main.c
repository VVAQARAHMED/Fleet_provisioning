/*
 * Copyright (c) 2025 CORE Labs
 */
#include "shadow_payload.h"
#include <hw_id.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <modem/modem_key_mgmt.h>
#include <net/aws_iot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>
#include "connectivity_interface.h"
#include "fleet_provisioning.h"
#include "mqtt_pub_sub.h"
#include "nvs_flash.h"
#include "watchdog_app.h"

/* Register log module */
LOG_MODULE_REGISTER(aws_iot_sample, CONFIG_AWS_IOT_SAMPLE_LOG_LEVEL);
struct provison_setting prov_set = {.device_provisioned = 0,
                                    .rotate_cert = 0,
                                    .sec_tag = CONFIG_MQTT_HELPER_SEC_TAG,
                                    .old_sec_tag = 0,
                                    .thing_name = "",
                                    .verfiy_connectivity = 0};

struct Sys_info sys_info = {
    .last_reboot_reason = "NONE",  
    .add_reset_reason = true
};                               
static char hw_id[HW_ID_LEN];
#define MAX_UART_MSG_SIZE 256
/* Forward declarations. */
#define UART_DEVICE_NODE DT_NODELABEL(externalsensor)
static const struct device *uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static char rx_buffer[MAX_UART_MSG_SIZE];
static int rx_index = 0;
K_MSGQ_DEFINE(uart_msgq, MAX_UART_MSG_SIZE, 2, 4);
void uart_send(const char *msg) {
  for (int i = 0; msg[i] != '\0'; i++) {
    uart_poll_out(uart_dev, msg[i]); // Send character
  }
}
// UART Interrupt Callback
void uart_callback(const struct device *dev, void *user_data) {
  uint8_t c;
  while (uart_fifo_read(dev, &c, 1) > 0) {
    if (c == '\n' || rx_index >= MAX_UART_MSG_SIZE- 1) {
      rx_buffer[rx_index] = '\0';
      printk("Recived data on RX channel: %s\n", rx_buffer);
      k_msgq_put(&uart_msgq, &rx_buffer, K_NO_WAIT);
      rx_index = 0;
    } else {
      rx_buffer[rx_index++] = c;
    }
  }
}

int convert_to_unformatted(cJSON **json, char *formatted_json,
                           char **unformatted_json) {
  *json = cJSON_Parse(formatted_json);
  if (*json == NULL) {
    printf("Error parsing JSON: %s\n", cJSON_GetErrorPtr());
    return 1;
  }

  *unformatted_json = cJSON_PrintUnformatted(*json);
  if (*unformatted_json) {
    //printf("Unformatted JSON: %s\n", *unformatted_json);
    return 0;
  }
  return 1;
}
void uart_init() {
  if (!device_is_ready(uart_dev)) {
    printk("UART1 not ready\n");
    return;
  }
  uart_irq_callback_user_data_set(uart_dev, uart_callback, NULL);
  uart_irq_rx_enable(uart_dev);
  printk("UART1 initialized\n");
}
//char reset_reason[50] = "NONE";
static void log_resetreason(uint32_t rr)
{
        /* Reset reason */
        LOG_DBG("Reset reasons:\n");
        if (0 == rr)
        {
                LOG_DBG("- NONE\n");
                strcpy(sys_info.last_reboot_reason, "NONE");
        }
        else if (0 != (rr & NRF_POWER_RESETREAS_RESETPIN_MASK))
        {
                LOG_DBG("- RESETPIN\n");
                strcpy(sys_info.last_reboot_reason, "RESETPIN");
        }
        else if (0 != (rr & NRF_POWER_RESETREAS_DOG_MASK))
        {
                LOG_DBG("- Watch DOG\n");
                strcpy(sys_info.last_reboot_reason, "Watch DOG");
        }
        else if (0 != (rr & NRF_POWER_RESETREAS_SREQ_MASK))
        {
                LOG_DBG("- SREQ\n");
                strcpy(sys_info.last_reboot_reason, "SREQ");
        }
        else if (0 != (rr & NRF_POWER_RESETREAS_LOCKUP_MASK))
        {
                LOG_DBG("- LOCKUP\n");
                strcpy(sys_info.last_reboot_reason, "LOCKUP");
        }
        else if (0 != (rr & NRF_POWER_RESETREAS_OFF_MASK))
        {
                LOG_DBG("- OFF\n");
                strcpy(sys_info.last_reboot_reason, "OFF");
        }

        else if (0 != (rr & NRF_POWER_RESETREAS_DIF_MASK))
        {
                LOG_DBG("- DIF\n");
                strcpy(sys_info.last_reboot_reason, "DIF");
        }
}
int main(void) {
  LOG_INF("Fleet Provisioning over Cellular, version: %s",
          CONFIG_AWS_IOT_SAMPLE_APP_VERSION);



    int rr = nrf_power_resetreas_get(NRF_POWER);
        printk("RESETREAS: 0x%08x\n", rr);
        log_resetreason(rr);
        printk("reset reason:%s\n",sys_info.last_reboot_reason);   
          if (IS_ENABLED(CONFIG_WATCHDOG))
        {
                if (watchdog_init_and_start() == 0)
                {
                        LOG_INF("WATCH DOG  init and Start");
                }
                else
                        LOG_INF("Failed to init and Start WATCH DOG");
        }
  int err;
  err = init_nvs_sys();
  if (err == 0) {
    printk("NVS init successfully \n");
  }
  // write_provision_status_to_nvs();
  err = read_from_nvs(KEY_ID_PROVISION_STATUS, &prov_set.device_provisioned);
  if (err == 0) { // prov_set.device_provisioned=0;
    if (prov_set.device_provisioned == 1) {
      prov_set.sec_tag = CONFIG_MQTT_HELPER_SEC_TAG + 1;
      printk("Device already provisioned \n");
    }
  } else {
    printk("Failed to read provision status from NVS %d\n", err);
  }
  err = read_from_nvs(KEY_ID_SEC_TAG, &prov_set.sec_tag);
  if (err == 0) {
    printk("Successfully read sec tag %d\n", prov_set.sec_tag);
  } else {
    printk("Failed to read sec tag from NVS %d\n", err);
  }
  err = read_from_nvs(KEY_CERT_ROTATION, &prov_set.rotate_cert);
  if (err == 0) {
    printk("Successfully read CERT ROTATION FLAG from NVS %d\n",
           prov_set.rotate_cert);
  } else {
    printk("Failed to read CERT ROTATION from NVS %d\n", err);
  }
  uart_init();/* initialized uart 1 for external sensor*/

  /* Setup handler for Zephyr NET Connection Manager events. */
  net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
  net_mgmt_add_event_callback(&l4_cb);

  /* Setup handler for Zephyr NET Connection Manager Connectivity layer. */
  net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler,
                               CONN_LAYER_EVENT_MASK);
  net_mgmt_add_event_callback(&conn_cb);

  /* Connecting to the configured connectivity layer.
   * Wi-Fi or LTE depending on the board that the sample was built for.
   */
  LOG_INF("Bringing network interface up and connecting to the network");

  err = conn_mgr_all_if_up(true);
  if (err) {
    LOG_ERR("conn_mgr_all_if_up, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  err = conn_mgr_all_if_connect(true);
  if (err) {
    LOG_ERR("conn_mgr_all_if_connect, error: %d", err);
    FATAL_ERROR();
    return err;
  }

#if defined(CONFIG_AWS_IOT_SAMPLE_DEVICE_ID_USE_HW_ID)
  /* Get unique hardware ID, can be used as AWS IoT MQTT broker device/client
   * ID. */
  err = hw_id_get(hw_id, ARRAY_SIZE(hw_id));
  if (err) {
    LOG_ERR("Failed to retrieve hardware ID, error: %d", err);
    FATAL_ERROR();
    return err;
  }
  snprintk(prov_set.thing_name, 12, "CORE_%s", &hw_id[HW_ID_LEN - 7]);
  LOG_INF("Hardware ID: %s", hw_id);
  LOG_INF("Thing name: %s", prov_set.thing_name);

#endif /* CONFIG_AWS_IOT_SAMPLE_DEVICE_ID_USE_HW_ID */

  err = aws_iot_client_init();
  if (err) {
    LOG_ERR("aws_iot_client_init, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  /* Resend connection status if the sample is built for QEMU x86.
   * This is necessary because the network interface is automatically brought up
   * at SYS_INIT() before main() is called.
   * This means that NET_EVENT_L4_CONNECTED fires before the
   * appropriate handler l4_event_handler() is registered.
   */
  if (IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)) {
    conn_mgr_mon_resend_status();
  }

  return 0;
}
