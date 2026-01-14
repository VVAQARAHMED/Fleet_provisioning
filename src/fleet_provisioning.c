
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
#if defined(CONFIG_MQTT_HELPER_PROVISION_CERTIFICATES)
#include "mqtt-certs.h"
#endif

#include "mqtt_pub_sub.h"

LOG_MODULE_REGISTER(fleet_provision,CONFIG_AWS_IOT_SAMPLE_LOG_LEVEL);


static enum topic_type topic_filter(const char *topic, size_t topic_len)
{
	if (strstr(topic, KEY_TOPIC_FILTER) != NULL) {
		return KEY_TOPIC;
	} else if (strstr(topic, CERT_TOPIC_FILTER) != NULL) {
		return CERT_TOPIC;
	}

	return UNKNOWN;
}


 void assemble_credentials(const char *buf, size_t buf_len, const char *topic,
				 size_t topic_len)
{
	int err;

	LOG_INF("%d Bytes received from AWS IoT console: Topic: %.*s:", buf_len, topic_len, topic);
	//LOG_INF("\n\n%.*s", buf_len, buf);

	if (prov_set.device_provisioned && !prov_set.rotate_cert) {
		LOG_WRN("Device already provisioned");
		return;
	}

	enum topic_type type = topic_filter(topic, topic_len);

	switch (type) {
	case KEY_TOPIC:
		LOG_INF("KEY_TOPIC");
		__ASSERT_NO_MSG(sizeof(private_key_new) > buf_len);
		memcpy(&private_key_new, buf, buf_len);
		private_key_len_new = buf_len;
		LOG_INF("Private key copied");
		break;
	case CERT_TOPIC:
		LOG_INF("CERT_TOPIC");
		__ASSERT_NO_MSG(sizeof(client_certificate_new) > buf_len);
		memcpy(&client_certificate_new, buf, buf_len);
		client_certificate_len_new = buf_len;
		LOG_INF("Certificate copied");
		break;
	default:
		LOG_ERR("Unknown incoming topic!");
		return;
	}

	if (client_certificate_len_new == 0 || private_key_len_new == 0) {
		LOG_INF("Not all credentials has been received, abort provisioning");
		return;
	}

	LOG_INF("Provision credentials");

	/* Disconnect client before shutting down the modem. */
	(void)aws_iot_disconnect();

	LOG_INF("AWS IoT Client disconnected");

	/* Go offline in order to provision credentials. */
	err = lte_lc_offline();
	if (err == 0) {
		LOG_INF("Modem set in offline mode");

		/* Write Private key */
		//prov_set.sec_tag = CONFIG_MQTT_HELPER_SEC_TAG + 1;
		err = modem_key_mgmt_write(prov_set.sec_tag, MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
					   &private_key_new, private_key_len_new);
		if (err) {
			LOG_ERR("Failed writing private key to the modem");
		}
		printk("Private key written on modem successfully,size of private key %d\n",private_key_len_new);
		/* Write client certificate */
		err = modem_key_mgmt_write(prov_set.sec_tag, MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
					   &client_certificate_new, client_certificate_len_new);
		if (err) {
			LOG_ERR("Failed writing client certificate to the modem");
		}
		printk("Client certificate written on modem successfully,,size of client certificate %d\n",client_certificate_len_new);
		/* Write CA certificate */
		err = modem_key_mgmt_write(prov_set.sec_tag, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, ca,
					   sizeof(ca));
		if (err) {
			LOG_ERR("Failed writing client certificate to the modem");
		}
		printk("CA certificate written on modem successfully,size of CA %d\n",sizeof(ca));
		LOG_INF("Credentials written to the modem!");
		prov_set.device_provisioned = 1;
		err=write_to_nvs(KEY_ID_PROVISION_STATUS,&prov_set.device_provisioned);
		if(err!=0)
		  printk("Failed to write provision status to NVS %d ",err);
		k_msleep(3000);
		err=read_from_nvs(KEY_ID_PROVISION_STATUS,&prov_set.device_provisioned);
		if(err!=0)
		   printk("Failed to read provision status frm NVS %d ",err);
		if(prov_set.device_provisioned==1 && prov_set.rotate_cert==0)
		{printk("Rebooting system\n");
		sys_reboot(0);
		}

		/* Connect to LTE.*/
		prov_set.verfiy_connectivity=1;
		lte_lc_connect();


		/* Schedule a new connection. */
		// k_work_schedule(&connect_after_provisioning_work, K_SECONDS(1));
		prov_set.device_provisioned = 1;
	} else

	{
		printk("Failed to put modem in offline mode \n");
	}
}


 void credentials_get()
{
	int err = 0;
	err = snprintk(key_cert_topic_get, sizeof(key_cert_topic_get), CRED_CREATE_TOPIC,
		       prov_set.thing_name);
	if ((err < 0) && (err >= CRED_CREATE_TOPIC_LEN)) {
		return -ENOMEM;
	}

	/* Publish blank message to certificate/${deviceId}/create to get credentials. */
	printk("Topic to publish %s", key_cert_topic_get);
	struct aws_iot_data tx_data = {.qos = MQTT_QOS_1_AT_LEAST_ONCE,
				       .topic.type = AWS_IOT_SHADOW_TOPIC_NONE,
				       .topic.str = key_cert_topic_get,
				       .topic.len = strlen(key_cert_topic_get),
				       .ptr = "",
				       .len = strlen("")};

	LOG_INF("Publishing blank message to AWS IoT broker");

	err = aws_iot_send(&tx_data);
	if (err) {
		LOG_INF("aws_iot_send, error: %d", err);
	}

	// shadow_update(false);
}