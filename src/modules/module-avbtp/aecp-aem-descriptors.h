/* AVB support
 *
 * Copyright © 2022 Wim Taymans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef AVBTP_AECP_AEM_DESCRIPTORS_H
#define AVBTP_AECP_AEM_DESCRIPTORS_H

#include "internal.h"

#define AVBTP_AEM_DESC_ENTITY			0x0000
#define AVBTP_AEM_DESC_CONFIGURATION		0x0001
#define AVBTP_AEM_DESC_AUDIO_UNIT		0x0002
#define AVBTP_AEM_DESC_VIDEO_UNIT		0x0003
#define AVBTP_AEM_DESC_SENSOR_UNIT		0x0004
#define AVBTP_AEM_DESC_STREAM_INPUT		0x0005
#define AVBTP_AEM_DESC_STREAM_OUTPUT		0x0006
#define AVBTP_AEM_DESC_JACK_INPUT		0x0007
#define AVBTP_AEM_DESC_JACK_OUTPUT		0x0008
#define AVBTP_AEM_DESC_AVB_INTERFACE		0x0009
#define AVBTP_AEM_DESC_CLOCK_SOURCE		0x000a
#define AVBTP_AEM_DESC_MEMORY_OBJECT		0x000b
#define AVBTP_AEM_DESC_LOCALE			0x000c
#define AVBTP_AEM_DESC_STRINGS			0x000d
#define AVBTP_AEM_DESC_STREAM_PORT_INPUT	0x000e
#define AVBTP_AEM_DESC_STREAM_PORT_OUTPUT	0x000f
#define AVBTP_AEM_DESC_EXTERNAL_PORT_INPUT	0x0010
#define AVBTP_AEM_DESC_EXTERNAL_PORT_OUTPUT	0x0011
#define AVBTP_AEM_DESC_INTERNAL_PORT_INPUT	0x0012
#define AVBTP_AEM_DESC_INTERNAL_PORT_OUTPUT	0x0013
#define AVBTP_AEM_DESC_AUDIO_CLUSTER		0x0014
#define AVBTP_AEM_DESC_VIDEO_CLUSTER		0x0015
#define AVBTP_AEM_DESC_SENSOR_CLUSTER		0x0016
#define AVBTP_AEM_DESC_AUDIO_MAP		0x0017
#define AVBTP_AEM_DESC_VIDEO_MAP		0x0018
#define AVBTP_AEM_DESC_SENSOR_MAP		0x0019
#define AVBTP_AEM_DESC_CONTROL			0x001a
#define AVBTP_AEM_DESC_SIGNAL_SELECTOR		0x001b
#define AVBTP_AEM_DESC_MIXER			0x001c
#define AVBTP_AEM_DESC_MATRIX			0x001d
#define AVBTP_AEM_DESC_MATRIX_SIGNAL		0x001e
#define AVBTP_AEM_DESC_SIGNAL_SPLITTER		0x001f
#define AVBTP_AEM_DESC_SIGNAL_COMBINER		0x0020
#define AVBTP_AEM_DESC_SIGNAL_DEMULTIPLEXER	0x0021
#define AVBTP_AEM_DESC_SIGNAL_MULTIPLEXER	0x0022
#define AVBTP_AEM_DESC_SIGNAL_TRANSCODER	0x0023
#define AVBTP_AEM_DESC_CLOCK_DOMAIN		0x0024
#define AVBTP_AEM_DESC_CONTROL_BLOCK		0x0025
#define AVBTP_AEM_DESC_INVALID			0xffff

struct avbtp_aem_desc_entity {
	uint64_t entity_id;
	uint64_t entity_model_id;
	uint32_t entity_capabilities;
	uint16_t talker_stream_sources;
	uint16_t talker_capabilities;
	uint16_t listener_stream_sinks;
	uint16_t listener_capabilities;
	uint32_t controller_capabilities;
	uint32_t available_index;
	uint64_t association_id;
	char entity_name[64];
	uint16_t vendor_name_string;
	uint16_t model_name_string;
	char firmware_version[64];
	char group_name[64];
	char serial_number[64];
	uint16_t configurations_count;
	uint16_t current_configuration;
} __attribute__ ((__packed__));

struct avbtp_aem_desc_descriptor_count {
	uint16_t descriptor_type;
	uint16_t descriptor_count;
} __attribute__ ((__packed__));

struct avbtp_aem_desc_configuration {
	char object_name[64];
	uint16_t localized_description;
	uint16_t descriptor_counts_count;
	uint16_t descriptor_counts_offset;
	struct avbtp_aem_desc_descriptor_count descriptor_counts[0];
} __attribute__ ((__packed__));

struct avbtp_aem_desc_sampling_rate {
	uint32_t pull_frequency;
} __attribute__ ((__packed__));

struct avbtp_aem_desc_audio_unit {
	char object_name[64];
	uint16_t localized_description;
	uint16_t clock_domain_index;
	uint16_t number_of_stream_input_ports;
	uint16_t base_stream_input_port;
	uint16_t number_of_stream_output_ports;
	uint16_t base_stream_output_port;
	uint16_t number_of_external_input_ports;
	uint16_t base_external_input_port;
	uint16_t number_of_external_output_ports;
	uint16_t base_external_output_port;
	uint16_t number_of_internal_input_ports;
	uint16_t base_internal_input_port;
	uint16_t number_of_internal_output_ports;
	uint16_t base_internal_output_port;
	uint16_t number_of_controls;
	uint16_t base_control;
	uint16_t number_of_signal_selectors;
	uint16_t base_signal_selector;
	uint16_t number_of_mixers;
	uint16_t base_mixer;
	uint16_t number_of_matrices;
	uint16_t base_matrix;
	uint16_t number_of_splitters;
	uint16_t base_splitter;
	uint16_t number_of_combiners;
	uint16_t base_combiner;
	uint16_t number_of_demultiplexers;
	uint16_t base_demultiplexer;
	uint16_t number_of_multiplexers;
	uint16_t base_multiplexer;
	uint16_t number_of_transcoders;
	uint16_t base_transcoder;
	uint16_t number_of_control_blocks;
	uint16_t base_control_block;
	uint32_t current_sampling_rate;
	uint16_t sampling_rates_offset;
	uint16_t sampling_rates_count;
	struct avbtp_aem_desc_sampling_rate sampling_rates[0];
} __attribute__ ((__packed__));

#define AVBTP_AEM_DESC_STREAM_FLAG_SYNC_SOURCE			(1u<<0)
#define AVBTP_AEM_DESC_STREAM_FLAG_CLASS_A			(1u<<1)
#define AVBTP_AEM_DESC_STREAM_FLAG_CLASS_B			(1u<<2)
#define AVBTP_AEM_DESC_STREAM_FLAG_SUPPORTS_ENCRYPTED		(1u<<3)
#define AVBTP_AEM_DESC_STREAM_FLAG_PRIMARY_BACKUP_SUPPORTED	(1u<<4)
#define AVBTP_AEM_DESC_STREAM_FLAG_PRIMARY_BACKUP_VALID		(1u<<5)
#define AVBTP_AEM_DESC_STREAM_FLAG_SECONDARY_BACKUP_SUPPORTED	(1u<<6)
#define AVBTP_AEM_DESC_STREAM_FLAG_SECONDARY_BACKUP_VALID	(1u<<7)
#define AVBTP_AEM_DESC_STREAM_FLAG_TERTIARY_BACKUP_SUPPORTED	(1u<<8)
#define AVBTP_AEM_DESC_STREAM_FLAG_TERTIARY_BACKUP_VALID	(1u<<9)

struct avbtp_aem_desc_stream {
	char object_name[64];
	uint16_t localized_description;
	uint16_t clock_domain_index;
	uint16_t stream_flags;
	uint64_t current_format;
	uint16_t formats_offset;
	uint16_t number_of_formats;
	uint64_t backup_talker_entity_id_0;
	uint16_t backup_talker_unique_id_0;
	uint64_t backup_talker_entity_id_1;
	uint16_t backup_talker_unique_id_1;
	uint64_t backup_talker_entity_id_2;
	uint16_t backup_talker_unique_id_2;
	uint64_t backedup_talker_entity_id;
	uint16_t backedup_talker_unique;
	uint16_t avb_interface_index;
	uint32_t buffer_length;
	uint64_t stream_formats[0];
} __attribute__ ((__packed__));

#define AVBTP_AEM_DESC_AVB_INTERFACE_FLAG_GPTP_GRANDMASTER_SUPPORTED	(1<<0)
#define AVBTP_AEM_DESC_AVB_INTERFACE_FLAG_GPTP_SUPPORTED		(1<<1)
#define AVBTP_AEM_DESC_AVB_INTERFACE_FLAG_SRP_SUPPORTED			(1<<2)

struct avbtp_aem_desc_avb_interface {
	char object_name[64];
	uint16_t localized_description;
	uint8_t mac_address[6];
	uint16_t interface_flags;
	uint64_t clock_identity;
	uint8_t priority1;
	uint8_t clock_class;
	uint16_t offset_scaled_log_variance;
	uint8_t clock_accuracy;
	uint8_t priority2;
	uint8_t domain_number;
	int8_t log_sync_interval;
	int8_t log_announce_interval;
	int8_t log_pdelay_interval;
	uint16_t port_number;
} __attribute__ ((__packed__));

#define AVBTP_AEM_DESC_CLOCK_SOURCE_TYPE_INTERNAL		0x0000
#define AVBTP_AEM_DESC_CLOCK_SOURCE_TYPE_EXTERNAL		0x0001
#define AVBTP_AEM_DESC_CLOCK_SOURCE_TYPE_INPUT_STREAM		0x0002
#define AVBTP_AEM_DESC_CLOCK_SOURCE_TYPE_MEDIA_CLOCK_STREAM	0x0003
#define AVBTP_AEM_DESC_CLOCK_SOURCE_TYPE_EXPANSION		0xffff

struct avbtp_aem_desc_clock_source {
	char object_name[64];
	uint16_t localized_description;
	uint16_t clock_source_flags;
	uint16_t clock_source_type;
	uint64_t clock_source_identifier;
	uint16_t clock_source_location_type;
	uint16_t clock_source_location_index;
} __attribute__ ((__packed__));

struct avbtp_aem_desc_locale {
	char locale_identifier[64];
	uint16_t number_of_strings;
	uint16_t base_strings;
} __attribute__ ((__packed__));

struct avbtp_aem_desc_strings {
	char string_0[64];
	char string_1[64];
	char string_2[64];
	char string_3[64];
	char string_4[64];
	char string_5[64];
	char string_6[64];
} __attribute__ ((__packed__));

struct avbtp_aem_desc_stream_port {
	uint16_t clock_domain_index;
	uint16_t port_flags;
	uint16_t number_of_controls;
	uint16_t base_control;
	uint16_t number_of_clusters;
	uint16_t base_cluster;
	uint16_t number_of_maps;
	uint16_t base_map;
} __attribute__ ((__packed__));

#endif /* AVBTP_AECP_AEM_DESCRIPTORS_H */