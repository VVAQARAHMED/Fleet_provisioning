#ifndef FLEET_PROV__
#define FLEET_PROV__

#define FATAL_ERROR()                                                          \
  LOG_ERR("Fatal error! Rebooting the device.");                               \
  LOG_PANIC();                                                                 \
  IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

static char client_certificate_new[2048];
static size_t client_certificate_len_new;
/* Private key */
static char private_key_new[2048];
static size_t private_key_len_new;
static char key_cert_topic[40];
static char key_cert_topic_get[75];
// char key_cert_topic_get[75];
struct provison_setting {
  uint8_t device_provisioned;
  uint8_t rotate_cert;
  uint8_t sec_tag;
  uint8_t verfiy_connectivity;
  uint8_t old_sec_tag;
  char thing_name[11];
};

struct Sys_info{
char last_reboot_reason[30];
bool add_reset_reason;

};
static const unsigned char ca[] = {
#if defined(MQTT_HELPER_CA_CERT_1)
#include MQTT_HELPER_CA_CERT_1
    /* Null terminate certificate */
    (0x00)
#else
    ""
#endif
};
#define KEY_TOPIC_FILTER "key"
#define WILD_CARD "+"
#define CERT_TOPIC_FILTER "cert"
#define CRED_TOPIC "certificates/"
#define CRED_TOPIC_LEN (sizeof(CRED_TOPIC))
#define CRED_CREATE_TOPIC "%s/create"
#define CRED_CREATE_TOPIC_LEN                                                  \
  (CRED_TOPIC_LEN + CONFIG_AWS_IOT_CLIENT_ID_MAX_LEN + 11)

#define CRED_GET_TOPIC "/create/accepted/"
#define GET_TOPIC_LEN (CRED_TOPIC_LEN + CONFIG_AWS_IOT_CLIENT_ID_MAX_LEN + 11)
extern struct provison_setting prov_set;
extern struct Sys_info sys_info;
void credentials_get();
void assemble_credentials(const char *buf, size_t buf_len, const char *topic,
                          size_t topic_len);

enum topic_type { KEY_TOPIC, CERT_TOPIC, UNKNOWN };
#endif