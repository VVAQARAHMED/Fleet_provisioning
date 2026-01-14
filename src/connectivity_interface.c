
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <net/aws_iot.h>
#include <stdio.h>
#include <stdlib.h>
#include <hw_id.h>
#include <modem/modem_info.h>
#include <modem/modem_key_mgmt.h>
#include "shadow_payload.h"
#include <modem/lte_lc.h>
#include "fleet_provisioning.h"
#include "nvs_flash.h"
#include "mqtt_pub_sub.h"
#include "connectivity_interface.h"
#if defined(CONFIG_MQTT_HELPER_PROVISION_CERTIFICATES)
#include "mqtt-certs.h"
#endif
LOG_MODULE_REGISTER(connectivity_interface,CONFIG_AWS_IOT_SAMPLE_LOG_LEVEL);
/* System Workqueue handlers. */

 void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
			     struct net_if *iface)
{
	switch (event) {
	case NET_EVENT_L4_CONNECTED:
		LOG_INF("Network connectivity established");
		on_net_event_l4_connected();
		break;
	case NET_EVENT_L4_DISCONNECTED:
		LOG_INF("Network connectivity lost");
		on_net_event_l4_disconnected();
		break;
	default:
		/* Don't care */
		return;
	}
}

void connectivity_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
				       struct net_if *iface)
{
	if (event == NET_EVENT_CONN_IF_FATAL_ERROR) {
		LOG_ERR("NET_EVENT_CONN_IF_FATAL_ERROR");
		FATAL_ERROR();
		return;
	}
}
