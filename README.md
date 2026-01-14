# nRF9151 AWS IoT Fleet Provisioning Application

A production-grade IoT firmware application for Nordic nRF9151/nRF9160 cellular modules implementing AWS IoT Fleet Provisioning by Claim.

## Overview

This project enables IoT devices to securely obtain device-specific certificates during initial provisioning without pre-provisioning credentials on each device. Once provisioned, devices can:

- Publish telemetry data to AWS IoT Core via MQTT
- Receive configuration updates from the cloud
- Update device shadow with system information (app version, modem version, uptime)
- Perform Firmware Over-The-Air (FOTA) updates
- Manage certificate rotation

## Features

- **AWS IoT Fleet Provisioning by Claim** - Automatic device certificate provisioning
- **Secure MQTT Communication** - TLS 1.2/1.3 encrypted connections to AWS IoT Core
- **Device Shadow Updates** - Regular status reporting (every 60 seconds)
- **FOTA Support** - Over-the-air firmware updates via AWS Jobs
- **Certificate Rotation** - Periodic certificate refresh capability
- **Watchdog Timer** - Automatic reboot on system hang (360-420 second timeout)
- **NVS Storage** - Persistent storage for provisioning state and configuration
- **UART Interface** - External sensor data communication

## Prerequisites

### Hardware

- **Development Board** (one of the following):
  - nRF9151-DK (Nordic Development Kit)
  - nRF9160-DK (Nordic Development Kit)
  - Thingy:91 X
  - C91S board
- **SIM Card** - Active LTE/NB-IoT SIM with data plan
- **USB Cable** - For programming and debugging

### Software

- **nRF Connect SDK v2.7.0** - [Installation Guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.7.0/nrf/installation.html)
- **CMake 3.20.0+** - Build system
- **Python 3.8+** - Required by Zephyr build tools
- **VS Code** (recommended) - With nRF Connect extension pack

### AWS Setup

> **Reference:** [AWS IoT Device Provisioning Documentation](https://docs.aws.amazon.com/iot/latest/developerguide/iot-provision.html)

1. **AWS Account** with IoT Core access
2. **Fleet Provisioning Template** - Must be configured in AWS IoT Core
   - Defines how devices are provisioned
   - Specifies which policy to attach to new devices
   - Required by Lambda function for device registration
3. **Device Policy** - IoT policy defining device permissions
   - Publish/Subscribe permissions for device topics
   - Shadow access permissions
   - Must be referenced in provisioning template
4. **Claim Certificate** - Temporary certificate for initial provisioning
5. **AWS Root CA Certificate** - Pre-loaded on device firmware
6. **Lambda Function** - Certificate provisioning function (required due to nRF91x 2KB packet limit)
   - Subscribes to `+/create` topic
   - Registers device in AWS IoT using serial number as thing_name (e.g., `CORE_123456`)
   - Uses provisioning template for device creation
   - Stores certificates in MongoDB
   - Publishes private key and device certificate as separate messages
7. **MongoDB** - Database for storing device certificates

## Project Structure

```
fleet/
├── src/
│   ├── main.c                    # Application entry point
│   ├── fleet_provisioning.c/h    # Fleet provisioning implementation
│   ├── mqtt_pub_sub.c/h          # MQTT publish/subscribe handling
│   ├── nvs_flash.c/h             # Non-volatile storage operations
│   ├── connectivity_interface.c/h # Network event handlers
│   ├── shadow/
│   │   └── shadow_payload.c/h    # Device shadow JSON construction
│   └── watchdog/
│       └── watchdog_app.c/h      # Watchdog management
├── boards/                       # Board-specific configurations
├── claim-certs/                  # Certificate storage
│   ├── ca-cert.pem               # AWS Root CA
│   ├── client-cert.pem           # Claim certificate
│   └── private-key.pem           # Claim private key
├── prj.conf                      # Main project configuration
├── CMakeLists.txt                # Build configuration
└── Kconfig                       # Application configuration menu
```

## Configuration

### AWS IoT Settings

Edit `prj.conf` to configure your AWS IoT endpoint:

```
CONFIG_AWS_IOT_BROKER_HOST_NAME="your-iot-endpoint.iot.region.amazonaws.com"
```

### Certificates

Place your certificates in the `claim-certs/` directory:

| File | Description |
|------|-------------|
| `ca-cert.pem` | AWS Root CA certificate (pre-loaded, used for all connections) |
| `client-cert.pem` | Fleet provisioning claim certificate (temporary, for initial provisioning) |
| `private-key.pem` | Private key for claim certificate (temporary, for initial provisioning) |

> **Note:** The Root CA certificate is compiled into the firmware and used for both claim certificate connections and device certificate connections. The claim certificate is only used during initial provisioning; after successful provisioning, the device uses the certificate received from Lambda.

## Building

### Using nRF Connect for VS Code

1. Open the project in VS Code
2. Select your target board from the nRF Connect extension
3. Click "Build" in the Actions panel

### Using Command Line

```bash
# For nRF9151-DK
west build -b nrf9151dk/nrf9151/ns

# For nRF9160-DK
west build -b nrf9160dk/nrf9160/ns

# For Thingy:91 X
west build -b thingy91x/nrf9151/ns
```

## Flashing

```bash
west flash
```

Or use the "Flash" action in VS Code nRF Connect extension.

## nRF91x Packet Size Limitation & Lambda Workaround

### The Problem

The nRF91x series modems have a **2KB maximum MQTT packet size limitation**. However, AWS IoT Fleet Provisioning returns device certificates that are approximately **4KB in size**, which exceeds this limit and cannot be received directly by the device.

### The Solution: AWS Lambda Certificate Provisioning

To work around this limitation, an **AWS Lambda function** is deployed that handles device registration and certificate distribution:

```
┌─────────────┐  1. Send Serial No.  ┌─────────────┐  2. Create Thing     ┌─────────────┐
│   Device    │ ───────────────────► │   Lambda    │ ───────────────────► │   AWS IoT   │
│  (nRF91x)   │                      │  Function   │ ◄─────────────────── │    Core     │
└─────────────┘                      └─────────────┘  3. Return Cert      └─────────────┘
       ▲                                   │
       │                                   │ 4. Store in MongoDB
       │                                   ▼
       │                             ┌─────────────┐
       │  5. Send Key + Cert         │   MongoDB   │
       └──────────────────────────── └─────────────┘
```

**Flow:**

1. **Unprovisioned device** connects using the claim certificate
2. Device derives its **serial number** from IMEI (e.g., `CORE_123456`)
3. Device publishes to `{serial_number}/create` to request credentials
4. Lambda receives the request and registers the device in **AWS IoT Core**:
   - Uses serial number as the **thing_name** (e.g., `CORE_123456`)
   - Applies the **provisioning template**
   - Attaches the **device policy**
5. Lambda stores the certificate in **MongoDB** for record keeping
6. Lambda sends credentials back to device in separate messages:
   - **Private key** (~1.6KB)
   - **Device certificate** (~1.2KB)
7. **Root CA** is already pre-loaded on the device (no need to send)

### Provisioning MQTT Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `{thing_name}/create` | Device → Lambda | Request credentials (sends serial number) |
| `{thing_name}/create/accepted/key` | Lambda → Device | Private key response |
| `{thing_name}/create/accepted/cert` | Lambda → Device | Device certificate response |

The device subscribes to `{thing_name}/create/accepted/+` (wildcard) to receive both key and certificate messages.

## Application MQTT Topics

The device uses the following topic structure for normal operation (where `{thing_name}` is derived from device IMEI, e.g., `CORE_123456`):

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `{thing_name}/data_to_cloud` | Device → Cloud | Telemetry data |
| `{thing_name}/data_from_cloud` | Cloud → Device | Cloud messages |
| `{thing_name}/set_config` | Cloud → Device | Configuration updates |
| `{thing_name}/get_config` | Device → Cloud | Request current config |

## Provisioning Flow

1. Device boots and connects to AWS IoT using **claim certificate**
2. Device derives **serial number** from IMEI (e.g., `CORE_123456`)
   - This serial number becomes the device's **thing_name** in AWS IoT
3. Subscribes to `{serial_number}/create/accepted/+` topic
4. Publishes to `{serial_number}/create` to request credentials
5. **Lambda function** receives request:
   - Registers device in AWS IoT Core with serial number as thing_name
   - Applies **provisioning template** and attaches **device policy**
   - Stores certificate in MongoDB
6. Lambda sends **private key** on `{serial_number}/create/accepted/key`
7. Lambda sends **device certificate** on `{serial_number}/create/accepted/cert`
8. Device receives both messages and stores credentials in modem secure storage
   - Root CA is already pre-loaded on device
9. Updates provisioning status in NVS flash
10. Reboots and reconnects using new device certificate
11. Begins normal operation (shadow updates, telemetry)

## Troubleshooting

### Device not connecting to network
- Verify SIM card is properly inserted and activated
- Check antenna connection
- Review LTE/NB-IoT coverage in your area

### Certificate errors
- Ensure all certificates are in PEM format
- Verify AWS IoT endpoint URL is correct
- Check Fleet Provisioning template permissions in AWS

### Watchdog resets
- Review logs for system hangs
- Increase watchdog timeout if legitimate operations are taking too long

## License

See LICENSE file for details.

## Version

Current version: **v1.0.1**
