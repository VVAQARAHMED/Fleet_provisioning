#ifndef MQTT_PUBSUB__
#define MQTT_PUBSUB__

#define MODEM_FIRMWARE_VERSION_SIZE_MAX 50

int aws_iot_client_init(void);
void on_net_event_l4_connected(void);
void on_net_event_l4_disconnected(void);
void shadow_update_work_fn(struct k_work *work);
void connect_work_fn(struct k_work *work);
void aws_iot_event_handler(const struct aws_iot_evt *const evt);
int app_topics_subscribe(void);
void mqtttopic_handler(const struct aws_iot_evt *const evt);

#define CONFIG_SET_TOPIC "/set_config"
#define CONFIG_GET_TOPIC "/get_config"
#define DATA_FROM_CLOUD_TOPIC "/data_from_cloud"
#define DATA_TO_CLOUD_TOPIC "/data_to_cloud"

#endif