#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fleet_provision)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/nvs_flash.c)
target_sources(app PRIVATE src/connectivity_interface.c)
target_sources(app PRIVATE src/fleet_provisioning.c)
target_sources(app PRIVATE src/mqtt_pub_sub.c)
target_sources(app PRIVATE src/shadow/shadow_payload.c)
target_sources(app PRIVATE claim-certs/ca-cert.pem)
# NORDIC SDK APP END

if (CONFIG_MQTT_HELPER_PROVISION_CERTIFICATES)
	message(WARNING "Credentials are exposed in non-secure memory. This should be avoided in production.")

	# Define the directory where certificate include files will be stored.
	set(app_certs_binary_dir ${APPLICATION_BINARY_DIR}/claim-certs)
	zephyr_include_directories(claim-cert)

	# Function to process a certificate file and create a corresponding .inc file and compiler definition
	# used in certs/mqtt-certs.h to assign C variables that are used in mqtt_helper.c.
	function(process_certificate definition_name file_name)
		set(cert_file ${APPLICATION_SOURCE_DIR}/${CONFIG_MQTT_HELPER_CERTIFICATES_FOLDER}/${file_name})
		if(EXISTS ${cert_file})
			message(STATUS "${file_name} found")

			get_filename_component(file_base_name ${file_name} NAME_WE)
            set(FILE_NAME ${file_base_name}_1)
			set(inc_file_name ${FILE_NAME}.inc)

			set(inc_file_path ${app_certs_binary_dir}/${inc_file_name})
			generate_inc_file_for_target(app ${cert_file} ${inc_file_path})

			# Define a compiler macro with the path to the generated .inc file,
			# allowing it to be included in the source code.
			add_definitions(-D${definition_name}="${inc_file_path}")
		endif()
	endfunction()

	# Process each certificate file by generating a .inc file and defining a corresponding macro.
process_certificate("MQTT_HELPER_CA_CERT_1" "ca-cert.pem")
endif()
zephyr_include_directories(src/)
#zephyr_include_directories(claim-certs)
zephyr_include_directories(src/shadow)
add_subdirectory_ifdef(CONFIG_WATCHDOG_APPLICATION src/watchdog)
zephyr_include_directories_ifdef(CONFIG_AWS_IOT_PROVISION_CERTIFICATES claim-certs)
