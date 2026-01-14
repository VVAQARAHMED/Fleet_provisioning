
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
#include <cJSON.h>
#include <modem/modem_info.h>
#include <modem/modem_key_mgmt.h>
#include "shadow_payload.h"
#include <modem/lte_lc.h>
#include "fleet_provisioning.h"
#include "nvs_flash.h"
#include "mqtt_pub_sub.h"
#if defined(CONFIG_MQTT_HELPER_PROVISION_CERTIFICATES)
#include "mqtt-certs.h"
#endif

LOG_MODULE_REGISTER(mqtt_pub_sub, CONFIG_AWS_IOT_SAMPLE_LOG_LEVEL);
/* Work items used to control some aspects of the sample. */
static K_WORK_DELAYABLE_DEFINE(shadow_update_work, shadow_update_work_fn);
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);
static char config_set_topic[32];
static char config_get_topic[32];
static char data_from_cloud_topic[32];
static char data_to_cloud_topic[64];
static char cred_topic[40];
static struct mqtt_topic topic_list[3];
extern struct k_msgq uart_msgq;
void uart_send(const char *msg);
bool add_reset_reason = true;
int convert_to_unformatted(
    cJSON **json, char *formatted_json,
    char **unformatted_json);

int app_topics_subscribe(void) {
  int err;
  snprintf(config_get_topic, sizeof(config_get_topic), "%s%s",
           prov_set.thing_name, CONFIG_GET_TOPIC);
  snprintf(config_set_topic, sizeof(config_set_topic), "%s%s",
           prov_set.thing_name, CONFIG_SET_TOPIC);
  snprintf(data_from_cloud_topic, sizeof(data_from_cloud_topic), "%s%s",
           prov_set.thing_name, DATA_FROM_CLOUD_TOPIC);
  int len = snprintf(data_to_cloud_topic, sizeof(data_to_cloud_topic), "%s%s",
                     prov_set.thing_name, DATA_TO_CLOUD_TOPIC);
  if (len >= sizeof(data_to_cloud_topic)) {
    printk("Warning: topic truncated!\n");
  }
  snprintf(config_set_topic, sizeof(config_set_topic), "%s%s",
           prov_set.thing_name, CONFIG_SET_TOPIC);
  uint8_t count = 0;
  snprintf(cred_topic, sizeof(cred_topic), "%s%s%s", prov_set.thing_name,
           CRED_GET_TOPIC, WILD_CARD);
  topic_list[count].topic.utf8 = cred_topic;
  topic_list[count].topic.size = strlen(cred_topic);
  topic_list[count].qos = MQTT_QOS_1_AT_LEAST_ONCE;

  printk("topic list %d entry %s\n", count + 1, topic_list[count].topic.utf8);
  printk("topic list %d entry size %d\n", count + 1,
         topic_list[count].topic.size);
  count++;

  topic_list[count].topic.utf8 = config_set_topic;
  topic_list[count].topic.size = strlen(config_set_topic);
  topic_list[count].qos = MQTT_QOS_0_AT_MOST_ONCE;

  printk("topic list %d entry %s\n", count + 1, topic_list[count].topic.utf8);
  printk("topic list %d entry size %d\n", count + 1,
         topic_list[count].topic.size);

  count++;
  topic_list[count].topic.utf8 = data_from_cloud_topic;
  topic_list[count].topic.size = strlen(data_from_cloud_topic);
  topic_list[count].qos = MQTT_QOS_1_AT_LEAST_ONCE;
  printk("topic list %d entry %s\n", count + 1, topic_list[count].topic.utf8);
  printk("topic list %d entry size %d\n", count + 1,
         topic_list[count].topic.size);

  err = aws_iot_application_topics_set(topic_list,
                                       count + 1); // ARRAY_SIZE(topic_list));
  if (err) {
    LOG_ERR("aws_iot_application_topics_set, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  return 0;
}

static int json_add_obj(cJSON *parent, const char *str, cJSON *item)
{
	cJSON_AddItemToObject(parent, str, item);

	return 0;
}

static int json_add_str(cJSON *parent, const char *str, const char *item)
{
	cJSON *json_str;

	json_str = cJSON_CreateString(item);
	if (json_str == NULL)
	{
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_str);
}

static int json_add_number(cJSON *parent, const char *str, double item)
{
	cJSON *json_num;

	json_num = cJSON_CreateNumber(item);
	if (json_num == NULL)
	{
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_num);
}
int aws_iot_client_init(void)
{
	int err;

	err = aws_iot_init(aws_iot_event_handler);
	if (err)
	{
		LOG_ERR("AWS IoT library could not be initialized, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	/* Add application specific non-shadow topics to the AWS IoT library.
	 * These topics will be subscribed to when connecting to the broker.
	 */
	// if (prov_set.device_provisioned == 0)

	//{
	err = app_topics_subscribe();
	if (err)
	{
		LOG_ERR("Adding application specific topics failed, error: %d", err);
		FATAL_ERROR();
		return err;
	}
        printk("topic config %s\n", config_get_topic);
        printk("topic config set %s\n", config_set_topic);
        printk("topic data from cloud %s\n", data_from_cloud_topic);
        printk("topic data to cloud %s\n", data_to_cloud_topic);
        return 0;
}

void mqtttopic_handler(const struct aws_iot_evt *const evt)

{
	int8_t err = 0;
	if (strncmp(evt->data.msg.topic.str, config_set_topic, strlen(config_set_topic)) == 0)
	{
		//printk("incoming message %s\n", evt->data.msg.ptr);
		LOG_INF("Incoming Message: \"%.*s\" on topic: \"%.*s\"", evt->data.msg.len,
				   evt->data.msg.ptr, evt->data.msg.topic.len, evt->data.msg.topic.str);
		cJSON *json = cJSON_Parse(evt->data.msg.ptr);
		if (json == NULL)
		{
			const char *error_ptr = cJSON_GetErrorPtr();
			if (error_ptr != NULL)
			{
				printf("Error: %s\n", error_ptr);
			}
			return;
		}
		cJSON *rotate_cert = cJSON_GetObjectItemCaseSensitive(json, "rotate_cert");
		if (rotate_cert != NULL && cJSON_IsNumber(rotate_cert))
		{
			printf("rotate_cert: %d\n", rotate_cert->valueint);
		}
		cJSON *sec_tag = cJSON_GetObjectItemCaseSensitive(json, "sec_tag");
		if (sec_tag != NULL && cJSON_IsNumber(sec_tag))
		{
			printf("sec_tag: %d\n", sec_tag->valueint);
		}
		if (rotate_cert != NULL && sec_tag != NULL)
		{
			prov_set.rotate_cert = rotate_cert->valueint;
			prov_set.old_sec_tag = prov_set.sec_tag;
			prov_set.sec_tag = sec_tag->valueint;
		}
		printk("rotate_cert status %d, new sec tag %d,old secTag %d\n", prov_set.rotate_cert, prov_set.sec_tag, prov_set.old_sec_tag);
		cJSON_Delete(json);
	}

	if (strstr(evt->data.msg.topic.str, CRED_GET_TOPIC) != NULL)
	{
		printk("Received message after changes: len of message %d:", evt->data.msg.len);

		if (prov_set.device_provisioned == 0 || prov_set.rotate_cert == 1 || prov_set.rotate_cert == 2)
		{
			assemble_credentials(evt->data.msg.ptr, evt->data.msg.len,
								 evt->data.msg.topic.str, evt->data.msg.topic.len);
			if (prov_set.device_provisioned == 0)
			{
				prov_set.sec_tag = CONFIG_MQTT_HELPER_SEC_TAG + 1; // device is come from factory reset will use deafult +1 sec tag
			}
			err = write_to_nvs(KEY_ID_SEC_TAG, &prov_set.sec_tag);
			if (err != 0)
			{
				printk("Failed to write sec tag NVS");
			}
			
		}
	}
}
void publish(char *topic, u_int8_t type, uint8_t qos)
{
	int err = 0;
	cJSON *json = cJSON_CreateObject();

	if (json == NULL)
	{
		LOG_ERR("Failed to create JSON object");
		return;
	}
	printk("reset reason %s\n", sys_info.last_reboot_reason);
	if (prov_set.rotate_cert == 1)
		cJSON_AddStringToObject(json, "cert_rotation", "start");
	if (prov_set.rotate_cert == 2)
		cJSON_AddStringToObject(json, "cert_rotation", "inprogress");
	if (prov_set.rotate_cert == 3)
		cJSON_AddStringToObject(json, "cert_rotation", "completed");
    if(sys_info.add_reset_reason)
	   	cJSON_AddStringToObject(json, "reset_reason", sys_info.last_reboot_reason);
			//add_reset_reason = 0;
		

	char *json_str = cJSON_Print(json);

	printk("json str %s\n", json_str);
	printk("topic to publish %s", topic);
	struct aws_iot_data tx_data = {.qos = qos,
								   .topic.type = type,
								   .topic.str = topic,
								   .topic.len = strlen(topic),
								   .ptr = json_str,
								   .len = strlen(json_str)};

	LOG_INF("Publishing to AWS IoT broker on %s", topic);

	err = aws_iot_send(&tx_data);
	if (err)
	{
		LOG_INF("aws_iot_send, error: %d", err);
	}
	cJSON_free(json_str);
	cJSON_Delete(json);
	// shadow_update(false);
}
struct get_config
{
	uint8_t rotate_cert;
} get_conf;
char tx_buf[256];

int validate_json(char *json_str) {
  cJSON *json = cJSON_Parse(json_str);
  if (json == NULL) {
    printf("Error parsing JSON: %s\n", cJSON_GetErrorPtr());
    return -1;
  }
  cJSON_Delete(json);
  return 0;
}


void shadow_update_work_fn(struct k_work *work)
{
	
	int err;

	if (prov_set.device_provisioned)
	{
		char message[CONFIG_AWS_IOT_SAMPLE_JSON_MESSAGE_SIZE_MAX] = {0};
		struct payload payload = {
			.state.reported.uptime = k_uptime_get(),
			.state.reported.app_version = CONFIG_AWS_IOT_SAMPLE_APP_VERSION,
			
		};
		struct aws_iot_data tx_data = {
			.qos = MQTT_QOS_0_AT_MOST_ONCE,
			.topic.type = AWS_IOT_SHADOW_TOPIC_UPDATE,
		};

		if (IS_ENABLED(CONFIG_MODEM_INFO))
		{
			char modem_version_temp[MODEM_FIRMWARE_VERSION_SIZE_MAX];

			err = modem_info_get_fw_version(modem_version_temp,
											ARRAY_SIZE(modem_version_temp));
			if (err)
			{
				LOG_ERR("modem_info_get_fw_version, error: %d", err);
				//FATAL_ERROR();
				return;
			}

			payload.state.reported.modem_version = modem_version_temp;
			
		}

		err = json_payload_construct(message, sizeof(message), &payload);
		if (err)
		{
			LOG_ERR("json_payload_construct, error: %d", err);
			FATAL_ERROR();
			return;
		}

		tx_data.ptr = message;
		tx_data.len = strlen(message);

		LOG_INF("Publishing message: %s to AWS IoT shadow", message);
        err = aws_iot_send(&tx_data);
		if (err)
		{
			LOG_ERR("aws_iot_send, error: %d", err);
			FATAL_ERROR();
			return;
		}


                if (k_msgq_get(&uart_msgq, &tx_buf, K_SECONDS(1)) == 0) {
                if (validate_json(tx_buf) == 0) {


				 cJSON *json;
				 char *unformatted_json = NULL;
				 printk("VALID json string %s\n", tx_buf);
				 if (convert_to_unformatted(&json,tx_buf,&unformatted_json) == 0) {

				  tx_data.ptr =unformatted_json;
                  tx_data.len = strlen(unformatted_json);
				//  printk("unformatted json %s\n", unformatted_json);
				 // printk("unformatted json length %d\n", strlen(unformatted_json));
				  tx_data.topic.str = data_to_cloud_topic;
				  tx_data.topic.len = strlen(data_to_cloud_topic);
                  tx_data.topic.type = AWS_IOT_SHADOW_TOPIC_NONE;


                  LOG_INF("Publishing message: %s to AWS "
                                              "to Topic: %s", tx_data.ptr,tx_data.topic.str);



                  err = aws_iot_send(&tx_data);
                                  if (err) {
                                    LOG_ERR("aws_iot_send, error: %d", err);
                                    // FATAL_ERROR();
									cJSON_Delete(json);
									cJSON_free(unformatted_json);
                                  return;
                                 }

					cJSON_Delete(json);
					cJSON_free(unformatted_json);
                             }

							}
					else{

						printk("Invalid JSON received from UART\n");
					}

						}


        }
	if (prov_set.device_provisioned == 0)
	{
		credentials_get();
	}
	
	if (prov_set.rotate_cert > 0  || sys_info.add_reset_reason)
	{

		publish(config_get_topic, AWS_IOT_SHADOW_TOPIC_NONE, MQTT_QOS_1_AT_LEAST_ONCE);
		sys_info.add_reset_reason = false;
		if (prov_set.sec_tag == 3)
		{

			// publish(config_get_topic,AWS_IOT_SHADOW_TOPIC_NONE,MQTT_QOS_1_AT_LEAST_ONCE);
		}
		if (prov_set.rotate_cert == 1)
			prov_set.rotate_cert = 2;
	}
       // uart_send("Publishing to AWS IoT broker from nrf9151\n");
       // uart_send("\n");
        (void)k_work_reschedule(&shadow_update_work,
							K_SECONDS(CONFIG_AWS_IOT_SAMPLE_PUBLICATION_INTERVAL_SECONDS));
}

void connect_work_fn(struct k_work *work)
{
	int err;
	const struct aws_iot_config config = {
		.client_id = prov_set.thing_name,
	};
	LOG_INF("Connecting to AWS IoT with client id %s", prov_set.thing_name);
	// LOG_INF("Connecting to AWS IoT with client id %s",config.client_id);

	err = aws_iot_connect(&config);
	if (err == -EAGAIN)
	{
		LOG_INF("Connection attempt timed out, "
				"Next connection retry in %d seconds",
				CONFIG_AWS_IOT_SAMPLE_CONNECTION_RETRY_TIMEOUT_SECONDS);

		(void)k_work_reschedule(
			&connect_work,
			K_SECONDS(CONFIG_AWS_IOT_SAMPLE_CONNECTION_RETRY_TIMEOUT_SECONDS));
	}
	else if (err)
	{
		LOG_ERR("aws_iot_connect, error: %d", err);
                (void)k_work_reschedule(
                    &connect_work,
                    K_SECONDS(
                        CONFIG_AWS_IOT_SAMPLE_CONNECTION_RETRY_TIMEOUT_SECONDS));
                //FATAL_ERROR();
	}
}

/* Functions that are executed on specific connection-related events. */

void on_aws_iot_evt_connected(const struct aws_iot_evt *const evt)
{
	(void)k_work_cancel_delayable(&connect_work);

	/* If persistent session is enabled, the AWS IoT library will not subscribe to any topics.
	 * Topics from the last session will be used.
	 */
	if (evt->data.persistent_session)
	{
		LOG_INF("Persistent session is enabled, using subscriptions "
				"from the previous session");
	}

	/* Mark image as working to avoid reverting to the former image after a reboot. */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	boot_write_img_confirmed();
#endif

	/* Start sequential updates to AWS IoT. */
	(void)k_work_reschedule(&shadow_update_work, K_NO_WAIT);
}

void on_aws_iot_evt_disconnected(void)
{
	(void)k_work_cancel_delayable(&shadow_update_work);
	(void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

void on_aws_iot_evt_fota_done(const struct aws_iot_evt *const evt)
{
	int err;

	/* Tear down MQTT connection. */
	(void)aws_iot_disconnect();
	(void)k_work_cancel_delayable(&connect_work);

	/* If modem FOTA has been carried out, the modem needs to be reinitialized.
	 * This is carried out by bringing the network interface down/up.
	 */
	if (evt->data.image & DFU_TARGET_IMAGE_TYPE_ANY_MODEM)
	{
		LOG_INF("Modem FOTA done, reinitializing the modem");

		err = conn_mgr_all_if_down(true);
		if (err)
		{
			LOG_ERR("conn_mgr_all_if_down, error: %d", err);
			FATAL_ERROR();
			return;
		}

		err = conn_mgr_all_if_up(true);
		if (err)
		{
			LOG_ERR("conn_mgr_all_if_up, error: %d", err);
			FATAL_ERROR();
			return;
		}

		err = conn_mgr_all_if_connect(true);
		if (err)
		{
			LOG_ERR("conn_mgr_all_if_connect, error: %d", err);
			FATAL_ERROR();
			return;
		}
	}
	else if (evt->data.image & DFU_TARGET_IMAGE_TYPE_ANY_APPLICATION)
	{
		LOG_INF("Application FOTA done, rebooting");
		IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)));
	}
	else
	{
		LOG_WRN("Unexpected FOTA image type");
	}
}

void on_net_event_l4_connected(void)
{
	(void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

void on_net_event_l4_disconnected(void)
{
	(void)aws_iot_disconnect();
	(void)k_work_cancel_delayable(&connect_work);
	(void)k_work_cancel_delayable(&shadow_update_work);
}

/* Event handlers */

void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
	switch (evt->type)
	{
	case AWS_IOT_EVT_CONNECTING:
		LOG_INF("AWS_IOT_EVT_CONNECTING");
		break;
	case AWS_IOT_EVT_CONNECTED:
		LOG_INF("AWS_IOT_EVT_CONNECTED");
		if (prov_set.rotate_cert == 2 && prov_set.verify_connectivity == 1)
		{

			prov_set.rotate_cert = 3;
			publish(config_get_topic, AWS_IOT_SHADOW_TOPIC_NONE, MQTT_QOS_1_AT_LEAST_ONCE);
			k_msleep(5000);
			if (prov_set.old_sec_tag != 0 && prov_set.old_sec_tag != prov_set.sec_tag)
			{
				printk("Deleting credentials from modem with sec_tag %d\n", prov_set.old_sec_tag);
				(void)aws_iot_disconnect();

				LOG_INF("AWS IoT Client disconnected");
				int err=mqtt_helper_deinit();
				  printk("mqtt helper de_init sattus %d\n",err);

				/* Go offline in order to remove credentials. */
				 err = lte_lc_offline();
				if (err == 0)
				{
					LOG_INF("Modem set in offline mode\n");
				}
				err = modem_key_mgmt_delete(prov_set.old_sec_tag, MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT);
				if (err == 0)
				printk("Private key with sec tag %d deleted from Modem\n", prov_set.old_sec_tag);
				else
                    printk("Failed to delete private key with sec tag %d from Modem\n", prov_set.old_sec_tag);

				err = modem_key_mgmt_delete(prov_set.old_sec_tag, MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT);
				if (err == 0)
				 printk("Client certificate with sec tag %d deleted from Modem\n", prov_set.old_sec_tag);
				 else
				 printk("Failed to delete client with sec tag %d from Modem\n", prov_set.old_sec_tag);


			}
			printk("Rebooting system in 3 sec\n");
			k_msleep(3000);
			sys_reboot(0);
		}
		on_aws_iot_evt_connected(evt);
		break;
	case AWS_IOT_EVT_DISCONNECTED:
		LOG_INF("AWS_IOT_EVT_DISCONNECTED");

		on_aws_iot_evt_disconnected();
		break;
	case AWS_IOT_EVT_DATA_RECEIVED:
		LOG_INF("AWS_IOT_EVT_DATA_RECEIVED");
	    LOG_INF("Received message on topic: \"%.*s\"",evt->data.msg.topic.len,evt->data.msg.topic.str);
		if (evt->data.msg.topic.str[0] != '$')
		{
			mqtttopic_handler(evt);
		}
		if (strncmp(evt->data.msg.topic.str, data_from_cloud_topic, strlen(data_from_cloud_topic)) == 0)
		{
			LOG_INF("Received message: \"%.*s\" on topic: \"%.*s\"", evt->data.msg.len,
				evt->data.msg.ptr, evt->data.msg.topic.len, evt->data.msg.topic.str);
			if (validate_json(evt->data.msg.ptr) == 0) {
				cJSON *json;
				char *unformatted_json = NULL;
				printk("VALID json string %s\n", evt->data.msg.ptr);
				if (convert_to_unformatted(&json, evt->data.msg.ptr, &unformatted_json) == 0) {
					uart_send(unformatted_json);
					uart_send("\n");
					cJSON_free(unformatted_json);
					cJSON_Delete(json);
				} else {
					if (json) {
						cJSON_Delete(json);
					}
				}
			}
		}
		break;
	case AWS_IOT_EVT_PUBACK:
		LOG_INF("AWS_IOT_EVT_PUBACK, message ID: %d", evt->data.message_id);
		break;
	case AWS_IOT_EVT_PINGRESP:
		LOG_INF("AWS_IOT_EVT_PINGRESP");
		break;
	case AWS_IOT_EVT_FOTA_START:
		LOG_INF("AWS_IOT_EVT_FOTA_START");
		break;
	case AWS_IOT_EVT_FOTA_ERASE_PENDING:
		LOG_INF("AWS_IOT_EVT_FOTA_ERASE_PENDING");
		break;
	case AWS_IOT_EVT_FOTA_ERASE_DONE:
		LOG_INF("AWS_FOTA_EVT_ERASE_DONE");
		break;
	case AWS_IOT_EVT_FOTA_DONE:
		LOG_INF("AWS_IOT_EVT_FOTA_DONE");
		on_aws_iot_evt_fota_done(evt);
		break;
	case AWS_IOT_EVT_FOTA_DL_PROGRESS:
		LOG_INF("AWS_IOT_EVT_FOTA_DL_PROGRESS, (%d%%)", evt->data.fota_progress);
		break;
	case AWS_IOT_EVT_ERROR:
		LOG_INF("AWS_IOT_EVT_ERROR, %d", evt->data.err);
		FATAL_ERROR();
		break;
	case AWS_IOT_EVT_FOTA_ERROR:
		LOG_INF("AWS_IOT_EVT_FOTA_ERROR");
		break;
	default:
		LOG_WRN("Unknown AWS IoT event type: %d", evt->type);
		break;
	}
}
