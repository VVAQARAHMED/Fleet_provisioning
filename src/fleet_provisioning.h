#ifndef FLEET_PROV__
#define FLEET_PROV__

#define FATAL_ERROR()                                                          \
  LOG_ERR("Fatal error! Rebooting the device.");                               \
  LOG_PANIC();                                                                 \
  IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))
struct provison_setting {
  uint8_t device_provisioned;
  uint8_t rotate_cert;
  uint8_t sec_tag;
  uint8_t verify_connectivity;
  uint8_t old_sec_tag;
  char thing_name[12];
};

struct Sys_info{
char last_reboot_reason[30];
bool add_reset_reason;

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
int credentials_get(void);
void assemble_credentials(const char *buf, size_t buf_len, const char *topic,
                          size_t topic_len);

enum topic_type { KEY_TOPIC, CERT_TOPIC, UNKNOWN };
#endif