#ifndef FLASH_NVS__
#define FLASH_NVS__

#define KEY_ID_PROVISION_STATUS 1
#define KEY_ID_SEC_TAG 2
#define KEY_CERT_ROTATION 3
int8_t init_nvs_sys();
int8_t read_from_nvs(uint8_t id,uint8_t *ptr);
int8_t write_to_nvs(uint8_t id,uint8_t *ptr);
#endif