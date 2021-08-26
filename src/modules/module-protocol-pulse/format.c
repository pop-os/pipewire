/* PipeWire
 *
 * Copyright © 2020 Wim Taymans
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

#include <spa/utils/string.h>
#include <spa/debug/types.h>
#include <spa/param/audio/format.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/utils/json.h>

#include "format.h"

static const struct format audio_formats[] = {
	[SAMPLE_U8] = { SAMPLE_U8, SPA_AUDIO_FORMAT_U8, "u8", 1 },
	[SAMPLE_ALAW] = { SAMPLE_ALAW, SPA_AUDIO_FORMAT_ALAW, "aLaw", 1 },
	[SAMPLE_ULAW] = { SAMPLE_ULAW, SPA_AUDIO_FORMAT_ULAW, "uLaw", 1 },
	[SAMPLE_S16LE] = { SAMPLE_S16LE, SPA_AUDIO_FORMAT_S16_LE, "s16le", 2 },
	[SAMPLE_S16BE] = { SAMPLE_S16BE, SPA_AUDIO_FORMAT_S16_BE, "s16be", 2 },
	[SAMPLE_FLOAT32LE] = { SAMPLE_FLOAT32LE, SPA_AUDIO_FORMAT_F32_LE, "float32le", 4 },
	[SAMPLE_FLOAT32BE] = { SAMPLE_FLOAT32BE, SPA_AUDIO_FORMAT_F32_BE, "float32be", 4 },
	[SAMPLE_S32LE] = { SAMPLE_S32LE, SPA_AUDIO_FORMAT_S32_LE, "s32le", 4 },
	[SAMPLE_S32BE] = { SAMPLE_S32BE, SPA_AUDIO_FORMAT_S32_BE, "s32be", 4 },
	[SAMPLE_S24LE] = { SAMPLE_S24LE, SPA_AUDIO_FORMAT_S24_LE, "s24le", 3 },
	[SAMPLE_S24BE] = { SAMPLE_S24BE, SPA_AUDIO_FORMAT_S24_BE, "s24be", 3 },
	[SAMPLE_S24_32LE] = { SAMPLE_S24_32LE, SPA_AUDIO_FORMAT_S24_32_LE, "s24-32le", 4 },
	[SAMPLE_S24_32BE] = { SAMPLE_S24_32BE, SPA_AUDIO_FORMAT_S24_32_BE, "s24-32be", 4 },

#if __BYTE_ORDER == __BIG_ENDIAN
	{ SAMPLE_S16BE, SPA_AUDIO_FORMAT_S16_BE, "s16ne", 2 },
	{ SAMPLE_FLOAT32BE, SPA_AUDIO_FORMAT_F32_BE, "float32ne", 4 },
	{ SAMPLE_S32BE, SPA_AUDIO_FORMAT_S32_BE, "s32ne", 4 },
	{ SAMPLE_S24BE, SPA_AUDIO_FORMAT_S24_BE, "s24ne", 3 },
	{ SAMPLE_S24_32BE, SPA_AUDIO_FORMAT_S24_32_BE, "s24-32ne", 4 },
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	{ SAMPLE_S16LE, SPA_AUDIO_FORMAT_S16_LE, "s16ne", 2 },
	{ SAMPLE_FLOAT32LE, SPA_AUDIO_FORMAT_F32_LE, "float32ne", 4 },
	{ SAMPLE_S32LE, SPA_AUDIO_FORMAT_S32_LE, "s32ne", 4 },
	{ SAMPLE_S24LE, SPA_AUDIO_FORMAT_S24_LE, "s24ne", 3 },
	{ SAMPLE_S24_32LE, SPA_AUDIO_FORMAT_S24_32_LE, "s24-32ne", 4 },
#endif
	/* planar formats, we just report them as interleaved */
	{ SAMPLE_U8, SPA_AUDIO_FORMAT_U8P, "u8ne", 1 },
	{ SAMPLE_S16NE, SPA_AUDIO_FORMAT_S16P, "s16ne", 2 },
	{ SAMPLE_S24_32NE, SPA_AUDIO_FORMAT_S24_32P, "s24-32ne", 4 },
	{ SAMPLE_S32NE, SPA_AUDIO_FORMAT_S32P, "s32ne", 4 },
	{ SAMPLE_S24NE, SPA_AUDIO_FORMAT_S24P, "s24ne", 3 },
	{ SAMPLE_FLOAT32NE, SPA_AUDIO_FORMAT_F32P, "float32ne", 4 },
};

static const struct channel audio_channels[] = {
	[CHANNEL_POSITION_MONO] = { SPA_AUDIO_CHANNEL_MONO, "mono", },

	[CHANNEL_POSITION_FRONT_LEFT] = { SPA_AUDIO_CHANNEL_FL, "front-left", },
	[CHANNEL_POSITION_FRONT_RIGHT] = { SPA_AUDIO_CHANNEL_FR, "front-right", },
	[CHANNEL_POSITION_FRONT_CENTER] = { SPA_AUDIO_CHANNEL_FC, "front-center", },

	[CHANNEL_POSITION_REAR_CENTER] = { SPA_AUDIO_CHANNEL_RC, "rear-center", },
	[CHANNEL_POSITION_REAR_LEFT] = { SPA_AUDIO_CHANNEL_RL, "rear-left", },
	[CHANNEL_POSITION_REAR_RIGHT] = { SPA_AUDIO_CHANNEL_RR, "rear-right", },

	[CHANNEL_POSITION_LFE] = { SPA_AUDIO_CHANNEL_LFE, "lfe", },
	[CHANNEL_POSITION_FRONT_LEFT_OF_CENTER] = { SPA_AUDIO_CHANNEL_FLC, "front-left-of-center", },
	[CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER] = { SPA_AUDIO_CHANNEL_FRC, "front-right-of-center", },

	[CHANNEL_POSITION_SIDE_LEFT] = { SPA_AUDIO_CHANNEL_SL, "side-left", },
	[CHANNEL_POSITION_SIDE_RIGHT] = { SPA_AUDIO_CHANNEL_SR, "side-right", },

	[CHANNEL_POSITION_AUX0] = { SPA_AUDIO_CHANNEL_AUX0, "aux0", },
	[CHANNEL_POSITION_AUX1] = { SPA_AUDIO_CHANNEL_AUX1, "aux1", },
	[CHANNEL_POSITION_AUX2] = { SPA_AUDIO_CHANNEL_AUX2, "aux2", },
	[CHANNEL_POSITION_AUX3] = { SPA_AUDIO_CHANNEL_AUX3, "aux3", },
	[CHANNEL_POSITION_AUX4] = { SPA_AUDIO_CHANNEL_AUX4, "aux4", },
	[CHANNEL_POSITION_AUX5] = { SPA_AUDIO_CHANNEL_AUX5, "aux5", },
	[CHANNEL_POSITION_AUX6] = { SPA_AUDIO_CHANNEL_AUX6, "aux6", },
	[CHANNEL_POSITION_AUX7] = { SPA_AUDIO_CHANNEL_AUX7, "aux7", },
	[CHANNEL_POSITION_AUX8] = { SPA_AUDIO_CHANNEL_AUX8, "aux8", },
	[CHANNEL_POSITION_AUX9] = { SPA_AUDIO_CHANNEL_AUX9, "aux9", },
	[CHANNEL_POSITION_AUX10] = { SPA_AUDIO_CHANNEL_AUX10, "aux10", },
	[CHANNEL_POSITION_AUX11] = { SPA_AUDIO_CHANNEL_AUX11, "aux11", },
	[CHANNEL_POSITION_AUX12] = { SPA_AUDIO_CHANNEL_AUX12, "aux12", },
	[CHANNEL_POSITION_AUX13] = { SPA_AUDIO_CHANNEL_AUX13, "aux13", },
	[CHANNEL_POSITION_AUX14] = { SPA_AUDIO_CHANNEL_AUX14, "aux14", },
	[CHANNEL_POSITION_AUX15] = { SPA_AUDIO_CHANNEL_AUX15, "aux15", },
	[CHANNEL_POSITION_AUX16] = { SPA_AUDIO_CHANNEL_AUX16, "aux16", },
	[CHANNEL_POSITION_AUX17] = { SPA_AUDIO_CHANNEL_AUX17, "aux17", },
	[CHANNEL_POSITION_AUX18] = { SPA_AUDIO_CHANNEL_AUX18, "aux18", },
	[CHANNEL_POSITION_AUX19] = { SPA_AUDIO_CHANNEL_AUX19, "aux19", },
	[CHANNEL_POSITION_AUX20] = { SPA_AUDIO_CHANNEL_AUX20, "aux20", },
	[CHANNEL_POSITION_AUX21] = { SPA_AUDIO_CHANNEL_AUX21, "aux21", },
	[CHANNEL_POSITION_AUX22] = { SPA_AUDIO_CHANNEL_AUX22, "aux22", },
	[CHANNEL_POSITION_AUX23] = { SPA_AUDIO_CHANNEL_AUX23, "aux23", },
	[CHANNEL_POSITION_AUX24] = { SPA_AUDIO_CHANNEL_AUX24, "aux24", },
	[CHANNEL_POSITION_AUX25] = { SPA_AUDIO_CHANNEL_AUX25, "aux25", },
	[CHANNEL_POSITION_AUX26] = { SPA_AUDIO_CHANNEL_AUX26, "aux26", },
	[CHANNEL_POSITION_AUX27] = { SPA_AUDIO_CHANNEL_AUX27, "aux27", },
	[CHANNEL_POSITION_AUX28] = { SPA_AUDIO_CHANNEL_AUX28, "aux28", },
	[CHANNEL_POSITION_AUX29] = { SPA_AUDIO_CHANNEL_AUX29, "aux29", },
	[CHANNEL_POSITION_AUX30] = { SPA_AUDIO_CHANNEL_AUX30, "aux30", },
	[CHANNEL_POSITION_AUX31] = { SPA_AUDIO_CHANNEL_AUX31, "aux31", },

	[CHANNEL_POSITION_TOP_CENTER] = { SPA_AUDIO_CHANNEL_TC, "top-center", },

	[CHANNEL_POSITION_TOP_FRONT_LEFT] = { SPA_AUDIO_CHANNEL_TFL, "top-front-left", },
	[CHANNEL_POSITION_TOP_FRONT_RIGHT] = { SPA_AUDIO_CHANNEL_TFR, "top-front-right", },
	[CHANNEL_POSITION_TOP_FRONT_CENTER] = { SPA_AUDIO_CHANNEL_TFC, "top-front-center", },

	[CHANNEL_POSITION_TOP_REAR_LEFT] = { SPA_AUDIO_CHANNEL_TRL, "top-rear-left", },
	[CHANNEL_POSITION_TOP_REAR_RIGHT] = { SPA_AUDIO_CHANNEL_TRR, "top-rear-right", },
	[CHANNEL_POSITION_TOP_REAR_CENTER] = { SPA_AUDIO_CHANNEL_TRC, "top-rear-center", },
};

uint32_t format_pa2id(enum sample_format format)
{
	if (format < 0 || format >= SAMPLE_MAX)
		return SPA_AUDIO_FORMAT_UNKNOWN;
	return audio_formats[format].id;
}

const char *format_id2name(uint32_t format)
{
	int i;
	for (i = 0; spa_type_audio_format[i].name; i++) {
		if (spa_type_audio_format[i].type == format)
			return spa_debug_type_short_name(spa_type_audio_format[i].name);
	}
	return "UNKNOWN";
}

uint32_t format_name2id(const char *name)
{
	int i;
	for (i = 0; spa_type_audio_format[i].name; i++) {
		if (spa_streq(name, spa_debug_type_short_name(spa_type_audio_format[i].name)))
			return spa_type_audio_format[i].type;
	}
	return SPA_AUDIO_CHANNEL_UNKNOWN;
}

uint32_t format_paname2id(const char *name, size_t size)
{
	size_t i;
	for (i = 0; i < SPA_N_ELEMENTS(audio_formats); i++) {
		if (audio_formats[i].name != NULL &&
		    strncmp(name, audio_formats[i].name, size) == 0)
			return audio_formats[i].id;
	}
	return SPA_AUDIO_FORMAT_UNKNOWN;
}

enum sample_format format_id2pa(uint32_t id)
{
	size_t i;
	for (i = 0; i < SPA_N_ELEMENTS(audio_formats); i++) {
		if (id == audio_formats[i].id)
			return audio_formats[i].pa;
	}
	return SAMPLE_INVALID;
}

const char *format_id2paname(uint32_t id)
{
	size_t i;
	for (i = 0; i < SPA_N_ELEMENTS(audio_formats); i++) {
		if (id == audio_formats[i].id &&
		    audio_formats[i].name != NULL)
			return audio_formats[i].name;
	}
	return "invalid";
}

uint32_t sample_spec_frame_size(const struct sample_spec *ss)
{
	switch (ss->format) {
	case SPA_AUDIO_FORMAT_U8:
	case SPA_AUDIO_FORMAT_ULAW:
	case SPA_AUDIO_FORMAT_ALAW:
		return ss->channels;
	case SPA_AUDIO_FORMAT_S16_LE:
	case SPA_AUDIO_FORMAT_S16_BE:
	case SPA_AUDIO_FORMAT_S16P:
		return 2 * ss->channels;
	case SPA_AUDIO_FORMAT_S24_LE:
	case SPA_AUDIO_FORMAT_S24_BE:
	case SPA_AUDIO_FORMAT_S24P:
		return 3 * ss->channels;
	case SPA_AUDIO_FORMAT_F32_LE:
	case SPA_AUDIO_FORMAT_F32_BE:
	case SPA_AUDIO_FORMAT_F32P:
	case SPA_AUDIO_FORMAT_S32_LE:
	case SPA_AUDIO_FORMAT_S32_BE:
	case SPA_AUDIO_FORMAT_S32P:
	case SPA_AUDIO_FORMAT_S24_32_LE:
	case SPA_AUDIO_FORMAT_S24_32_BE:
	case SPA_AUDIO_FORMAT_S24_32P:
		return 4 * ss->channels;
	default:
		return 0;
	}
}

bool sample_spec_valid(const struct sample_spec *ss)
{
	return (sample_spec_frame_size(ss) > 0 &&
	    ss->rate > 0 && ss->rate <= RATE_MAX &&
	    ss->channels > 0 && ss->channels <= CHANNELS_MAX);
}

uint32_t channel_pa2id(enum channel_position channel)
{
	if (channel < 0 || (size_t)channel >= SPA_N_ELEMENTS(audio_channels))
		return SPA_AUDIO_CHANNEL_UNKNOWN;

	return audio_channels[channel].channel;
}

const char *channel_id2name(uint32_t channel)
{
	int i;
	for (i = 0; spa_type_audio_channel[i].name; i++) {
		if (spa_type_audio_channel[i].type == channel)
			return spa_debug_type_short_name(spa_type_audio_channel[i].name);
	}
	return "UNK";
}

uint32_t channel_name2id(const char *name)
{
	int i;
	for (i = 0; spa_type_audio_channel[i].name; i++) {
		if (strcmp(name, spa_debug_type_short_name(spa_type_audio_channel[i].name)) == 0)
			return spa_type_audio_channel[i].type;
	}
	return SPA_AUDIO_CHANNEL_UNKNOWN;
}

enum channel_position channel_id2pa(uint32_t id, uint32_t *aux)
{
	size_t i;
	for (i = 0; i < SPA_N_ELEMENTS(audio_channels); i++) {
		if (id == audio_channels[i].channel)
			return i;
	}
	return CHANNEL_POSITION_AUX0 + ((*aux)++ & 31);
}

const char *channel_id2paname(uint32_t id, uint32_t *aux)
{
	size_t i;
	for (i = 0; i < SPA_N_ELEMENTS(audio_channels); i++) {
		if (id == audio_channels[i].channel &&
		    audio_channels[i].name != NULL)
			return audio_channels[i].name;
	}
	return audio_channels[CHANNEL_POSITION_AUX0 + ((*aux)++ & 31)].name;
}

uint32_t channel_paname2id(const char *name, size_t size)
{
	size_t i;
	for (i = 0; i < SPA_N_ELEMENTS(audio_channels); i++) {
		if (strncmp(name, audio_channels[i].name, size) == 0)
			return audio_channels[i].channel;
	}
	return SPA_AUDIO_CHANNEL_UNKNOWN;
}


void channel_map_to_positions(const struct channel_map *map, uint32_t *pos)
{
	int i;
	for (i = 0; i < map->channels; i++)
		pos[i] = map->map[i];
}

void channel_map_parse(const char *str, struct channel_map *map)
{
	const char *p = str;
	size_t len;

	if (spa_streq(p, "stereo")) {
		*map = (struct channel_map) {
			.channels = 2,
			.map[0] = SPA_AUDIO_CHANNEL_FL,
			.map[1] = SPA_AUDIO_CHANNEL_FR,
		};
	} else if (spa_streq(p, "surround-21")) {
		*map = (struct channel_map) {
			.channels = 3,
			.map[0] = SPA_AUDIO_CHANNEL_FL,
			.map[1] = SPA_AUDIO_CHANNEL_FR,
			.map[2] = SPA_AUDIO_CHANNEL_LFE,
		};
	} else if (spa_streq(p, "surround-40")) {
		*map = (struct channel_map) {
			.channels = 4,
			.map[0] = SPA_AUDIO_CHANNEL_FL,
			.map[1] = SPA_AUDIO_CHANNEL_FR,
			.map[2] = SPA_AUDIO_CHANNEL_RL,
			.map[3] = SPA_AUDIO_CHANNEL_RR,
		};
	} else if (spa_streq(p, "surround-41")) {
		*map = (struct channel_map) {
			.channels = 5,
			.map[0] = SPA_AUDIO_CHANNEL_FL,
			.map[1] = SPA_AUDIO_CHANNEL_FR,
			.map[2] = SPA_AUDIO_CHANNEL_RL,
			.map[3] = SPA_AUDIO_CHANNEL_RR,
			.map[4] = SPA_AUDIO_CHANNEL_LFE,
		};
	} else if (spa_streq(p, "surround-50")) {
		*map = (struct channel_map) {
			.channels = 5,
			.map[0] = SPA_AUDIO_CHANNEL_FL,
			.map[1] = SPA_AUDIO_CHANNEL_FR,
			.map[2] = SPA_AUDIO_CHANNEL_RL,
			.map[3] = SPA_AUDIO_CHANNEL_RR,
			.map[4] = SPA_AUDIO_CHANNEL_FC,
		};
	} else if (spa_streq(p, "surround-51")) {
		*map = (struct channel_map) {
			.channels = 6,
			.map[0] = SPA_AUDIO_CHANNEL_FL,
			.map[1] = SPA_AUDIO_CHANNEL_FR,
			.map[2] = SPA_AUDIO_CHANNEL_RL,
			.map[3] = SPA_AUDIO_CHANNEL_RR,
			.map[4] = SPA_AUDIO_CHANNEL_FC,
			.map[5] = SPA_AUDIO_CHANNEL_LFE,
		};
	} else if (spa_streq(p, "surround-71")) {
		*map = (struct channel_map) {
			.channels = 8,
			.map[0] = SPA_AUDIO_CHANNEL_FL,
			.map[1] = SPA_AUDIO_CHANNEL_FR,
			.map[2] = SPA_AUDIO_CHANNEL_RL,
			.map[3] = SPA_AUDIO_CHANNEL_RR,
			.map[4] = SPA_AUDIO_CHANNEL_FC,
			.map[5] = SPA_AUDIO_CHANNEL_LFE,
			.map[6] = SPA_AUDIO_CHANNEL_SL,
			.map[7] = SPA_AUDIO_CHANNEL_SR,
		};
	} else {
		map->channels = 0;
		while (*p && map->channels < SPA_AUDIO_MAX_CHANNELS) {
			if ((len = strcspn(p, ",")) == 0)
				break;
			map->map[map->channels++] = channel_paname2id(p, len);
			p += len + strspn(p+len, ",");
		}
	}
}

bool channel_map_valid(const struct channel_map *map)
{
	uint8_t i;
	uint32_t aux = 0;
	if (map->channels == 0 || map->channels > CHANNELS_MAX)
		return false;
	for (i = 0; i < map->channels; i++)
		if (channel_id2pa(map->map[i], &aux) >= CHANNEL_POSITION_MAX)
			return false;
	return true;
}

struct encoding_info {
	const char *name;
	uint32_t id;
};

static const struct encoding_info encoding_names[] = {
	[ENCODING_ANY] = { "ANY", 0 },
	[ENCODING_PCM] = { "PCM", 0 },
	[ENCODING_AC3_IEC61937] = { "AC3-IEC61937", SPA_AUDIO_IEC958_CODEC_AC3 },
	[ENCODING_EAC3_IEC61937] = { "EAC3-IEC61937", SPA_AUDIO_IEC958_CODEC_EAC3 },
	[ENCODING_MPEG_IEC61937] = { "MPEG-IEC61937", SPA_AUDIO_IEC958_CODEC_MPEG },
	[ENCODING_DTS_IEC61937] = { "DTS-IEC61937", SPA_AUDIO_IEC958_CODEC_DTS },
	[ENCODING_MPEG2_AAC_IEC61937] = { "MPEG2-AAC-IEC61937", SPA_AUDIO_IEC958_CODEC_MPEG2_AAC },
	[ENCODING_TRUEHD_IEC61937] = { "TRUEHD-IEC61937", SPA_AUDIO_IEC958_CODEC_TRUEHD },
	[ENCODING_DTSHD_IEC61937] = { "DTSHD-IEC61937", SPA_AUDIO_IEC958_CODEC_DTSHD },
};

const char *format_encoding2name(enum encoding enc)
{
	if (enc >= 0 && enc < (int)SPA_N_ELEMENTS(encoding_names) &&
	    encoding_names[enc].name != NULL)
		return encoding_names[enc].name;
	return "INVALID";
}
static uint32_t format_encoding2id(enum encoding enc)
{
	if (enc >= 0 && enc < (int)SPA_N_ELEMENTS(encoding_names) &&
	    encoding_names[enc].name != NULL)
		return encoding_names[enc].id;
	return SPA_ID_INVALID;
}

static enum encoding format_encoding_from_id(uint32_t id)
{
	int i;
	for (i = 0; i < (int)SPA_N_ELEMENTS(encoding_names); i++) {
		if (encoding_names[i].id == id)
			return i;
	}
	return ENCODING_ANY;
}

static inline int
audio_raw_parse_opt(const struct spa_pod *format, struct spa_audio_info_raw *info)
{
	struct spa_pod *position = NULL;
	int res;
	info->flags = 0;
	res = spa_pod_parse_object(format,
			SPA_TYPE_OBJECT_Format, NULL,
			SPA_FORMAT_AUDIO_format,        SPA_POD_OPT_Id(&info->format),
			SPA_FORMAT_AUDIO_rate,          SPA_POD_OPT_Int(&info->rate),
			SPA_FORMAT_AUDIO_channels,      SPA_POD_OPT_Int(&info->channels),
			SPA_FORMAT_AUDIO_position,      SPA_POD_OPT_Pod(&position));
	if (position == NULL ||
	    !spa_pod_copy_array(position, SPA_TYPE_Id, info->position, SPA_AUDIO_MAX_CHANNELS))
		SPA_FLAG_SET(info->flags, SPA_AUDIO_FLAG_UNPOSITIONED);

	return res;
}

int format_parse_param(const struct spa_pod *param, struct sample_spec *ss, struct channel_map *map,
		const struct sample_spec *def_ss, const struct channel_map *def_map)
{
	struct spa_audio_info info = { 0 };
	uint32_t i;

	if (spa_format_parse(param, &info.media_type, &info.media_subtype) < 0)
		return -ENOTSUP;

	if (info.media_type != SPA_MEDIA_TYPE_audio)
                return -ENOTSUP;

	switch (info.media_subtype) {
	case SPA_MEDIA_SUBTYPE_raw:
		if (def_ss != NULL) {
			if (ss != NULL)
				*ss = *def_ss;
			if (audio_raw_parse_opt(param, &info.info.raw) < 0)
		                return -ENOTSUP;
		} else {
			if (spa_format_audio_raw_parse(param, &info.info.raw) < 0)
		                return -ENOTSUP;
		}
		break;
	case SPA_MEDIA_SUBTYPE_iec958:
	{
		struct spa_audio_info_iec958 iec;

		if (spa_format_audio_iec958_parse(param, &iec) < 0)
			return -ENOTSUP;

		info.info.raw.format = SPA_AUDIO_FORMAT_S16;
		info.info.raw.channels = 2;
		info.info.raw.rate = iec.rate;
		info.info.raw.position[0] = SPA_AUDIO_CHANNEL_FL;
		info.info.raw.position[1] = SPA_AUDIO_CHANNEL_FR;
		break;
	}
	default:
		return -ENOTSUP;
        }
	if (ss) {
		if (info.info.raw.format)
		        ss->format = info.info.raw.format;
		if (info.info.raw.rate)
		        ss->rate = info.info.raw.rate;
		if (info.info.raw.channels)
		        ss->channels = info.info.raw.channels;
	}
	if (map) {
		map->channels = info.info.raw.channels;
		for (i = 0; i < map->channels; i++)
			map->map[i] = info.info.raw.position[i];
	}
	return 0;
}

const struct spa_pod *format_build_param(struct spa_pod_builder *b, uint32_t id,
		const struct sample_spec *spec, const struct channel_map *map)
{
	struct spa_audio_info_raw info;

	info = SPA_AUDIO_INFO_RAW_INIT(
			.format = spec->format,
			.channels = spec->channels,
			.rate = spec->rate);
	if (map)
		channel_map_to_positions(map, info.position);

	return spa_format_audio_raw_build(b, id, &info);
}

int format_info_from_spec(struct format_info *info, const struct sample_spec *ss,
			  const struct channel_map *map)
{
	spa_zero(*info);
	info->encoding = ENCODING_PCM;
	if ((info->props = pw_properties_new(NULL, NULL)) == NULL)
		return -errno;

	pw_properties_setf(info->props, "format.sample_format", "\"%s\"",
			format_id2paname(ss->format));
	pw_properties_setf(info->props, "format.rate", "%d", ss->rate);
	pw_properties_setf(info->props, "format.channels", "%d", ss->channels);
	if (map && map->channels == ss->channels) {
		char chmap[1024] = "";
		int i, o, r;
		uint32_t aux = 0;

		for (i = 0, o = 0; i < map->channels; i++) {
			r = snprintf(chmap+o, sizeof(chmap)-o, "%s%s", i == 0 ? "" : ",",
					channel_id2paname(map->map[i], &aux));
			if (r < 0 || o + r >= (int)sizeof(chmap))
				return -ENOSPC;
			o += r;
		}
		pw_properties_setf(info->props, "format.channel_map", "\"%s\"", chmap);
	}
	return 0;
}

static int add_int(struct format_info *info, const char *k, struct spa_pod *param,
		uint32_t key)
{
	const struct spa_pod_prop *prop;
	struct spa_pod *val;
	uint32_t i, n_values, choice;
	int32_t *values;

	prop = spa_pod_find_prop(param, NULL, key);
	if (prop == NULL)
		return -ENOENT;

	val = spa_pod_get_values(&prop->value, &n_values, &choice);
	if (val->type != SPA_TYPE_Int)
		return -ENOTSUP;

	values = SPA_POD_BODY(val);

	switch (choice) {
	case SPA_CHOICE_None:
		pw_properties_setf(info->props, k, "%d", values[0]);
		break;
	case SPA_CHOICE_Range:
		pw_properties_setf(info->props, k, "{ \"min\": %d, \"max\": %d }",
				values[1], values[2]);
		break;
	case SPA_CHOICE_Enum:
	{
		char *ptr;
		size_t size;
		FILE *f;

		f = open_memstream(&ptr, &size);
		fprintf(f, "[");
		for (i = 1; i < n_values; i++)
			fprintf(f, "%s %d", i == 1 ? "" : ",", values[i]);
		fprintf(f, " ]");
		fclose(f);

		pw_properties_set(info->props, k, ptr);
		free(ptr);
		break;
	}
	default:
		return -ENOTSUP;
	}
	return 0;
}

static int format_info_pcm_from_param(struct format_info *info, struct spa_pod *param, uint32_t index)
{
	if (index > 0)
		return -ENOENT;

	info->encoding = ENCODING_PCM;
	/* don't add params here yet, pulseaudio doesn't do that either */
	return 0;
}

static int format_info_iec958_from_param(struct format_info *info, struct spa_pod *param, uint32_t index)
{
	const struct spa_pod_prop *prop;
	struct spa_pod *val;
	uint32_t n_values, *values, choice;

	prop = spa_pod_find_prop(param, NULL, SPA_FORMAT_AUDIO_iec958Codec);
	if (prop == NULL)
		return -ENOENT;

	val = spa_pod_get_values(&prop->value, &n_values, &choice);
	if (val->type != SPA_TYPE_Id)
		return -ENOTSUP;

	if (index >= n_values)
		return -ENOENT;

	values = SPA_POD_BODY(val);

	switch (choice) {
	case SPA_CHOICE_None:
		info->encoding = format_encoding_from_id(values[index]);
		break;
	case SPA_CHOICE_Enum:
		info->encoding = format_encoding_from_id(values[index+1]);
		break;
	default:
		return -ENOTSUP;
	}
	if ((info->props = pw_properties_new(NULL, NULL)) == NULL)
		return -errno;

	add_int(info, "format.rate", param, SPA_FORMAT_AUDIO_rate);

	return 0;
}

int format_info_from_param(struct format_info *info, struct spa_pod *param, uint32_t index)
{
	uint32_t media_type, media_subtype;
	int res;

	if (spa_format_parse(param, &media_type, &media_subtype) < 0)
		return -ENOTSUP;

	if (media_type != SPA_MEDIA_TYPE_audio)
                return -ENOTSUP;

	switch(media_subtype) {
	case SPA_MEDIA_SUBTYPE_raw:
		res = format_info_pcm_from_param(info, param, index);
		break;
	case SPA_MEDIA_SUBTYPE_iec958:
		res = format_info_iec958_from_param(info, param, index);
		break;
	default:
		return -ENOTSUP;
	}
	return res;
}

static uint32_t format_info_get_format(const struct format_info *info)
{
	const char *str, *val;
	struct spa_json it[2];
	int len;

	if ((str = pw_properties_get(info->props, "format.sample_format")) == NULL)
		return SPA_AUDIO_FORMAT_UNKNOWN;

	spa_json_init(&it[0], str, strlen(str));
	if ((len = spa_json_next(&it[0], &val)) <= 0)
		return SPA_AUDIO_FORMAT_UNKNOWN;

	if (spa_json_is_string(val, len))
		return format_paname2id(val+1, len-2);

	return SPA_AUDIO_FORMAT_UNKNOWN;
}

static int format_info_get_rate(const struct format_info *info)
{
	const char *str, *val;
	struct spa_json it[2];
	int len, v;

	if ((str = pw_properties_get(info->props, "format.rate")) == NULL)
		return -ENOENT;

	spa_json_init(&it[0], str, strlen(str));
	if ((len = spa_json_next(&it[0], &val)) <= 0)
		return -EINVAL;
	if (spa_json_is_int(val, len)) {
		if (spa_json_parse_int(val, len, &v) <= 0)
			return -EINVAL;
		return v;
	}
	return -ENOTSUP;
}

int format_info_to_spec(const struct format_info *info, struct sample_spec *ss,
			  struct channel_map *map)
{
	const char *str, *val;
	struct spa_json it[2];
	float f;
	int res, len;

	spa_zero(*ss);
	spa_zero(*map);

	if (info->encoding != ENCODING_PCM)
		return -ENOTSUP;
	if (info->props == NULL)
		return -ENOENT;

	if ((ss->format = format_info_get_format(info)) == SPA_AUDIO_FORMAT_UNKNOWN)
		return -ENOTSUP;

	if ((res = format_info_get_rate(info)) < 0)
		return res;
	ss->rate = res;

	if ((str = pw_properties_get(info->props, "format.channels")) == NULL)
		return -ENOENT;

	spa_json_init(&it[0], str, strlen(str));
	if ((len = spa_json_next(&it[0], &val)) <= 0)
		return -EINVAL;
	if (spa_json_is_float(val, len)) {
		if (spa_json_parse_float(val, len, &f) <= 0)
			return -EINVAL;
		ss->channels = f;
	} else if (spa_json_is_array(val, len)) {
		return -ENOTSUP;
	} else if (spa_json_is_object(val, len)) {
		return -ENOTSUP;
	} else
		return -ENOTSUP;

	if ((str = pw_properties_get(info->props, "format.channel_map")) != NULL) {
		spa_json_init(&it[0], str, strlen(str));
		if ((len = spa_json_next(&it[0], &val)) <= 0)
			return -EINVAL;
		if (!spa_json_is_string(val, len))
			return -EINVAL;
		while ((*str == '\"' || *str == ',') &&
		    (len = strcspn(++str, "\",")) > 0) {
			map->map[map->channels++] = channel_paname2id(str, len);
			str += len;
		}
	}
	return 0;
}

const struct spa_pod *format_info_build_param(struct spa_pod_builder *b, uint32_t id,
		const struct format_info *info, uint32_t *rate)
{
	struct sample_spec ss;
	struct channel_map map;
	const struct spa_pod *param = NULL;
	int res;

	switch (info->encoding) {
	case ENCODING_PCM:
		if ((res = format_info_to_spec(info, &ss, &map)) < 0) {
			errno = -res;
			return NULL;
		}
		*rate = ss.rate;
		param = format_build_param(b, id, &ss, &map);
		break;
	case ENCODING_AC3_IEC61937:
	case ENCODING_EAC3_IEC61937:
	case ENCODING_MPEG_IEC61937:
	case ENCODING_DTS_IEC61937:
	case ENCODING_MPEG2_AAC_IEC61937:
	case ENCODING_TRUEHD_IEC61937:
	case ENCODING_DTSHD_IEC61937:
	{
		struct spa_audio_info_iec958 i = { 0 };
		i.codec = format_encoding2id(info->encoding);
		if ((res = format_info_get_rate(info)) <= 0) {
			errno = -res;
			return NULL;
		}
		i.rate = res;
		param = spa_format_audio_iec958_build(b, id, &i);
		break;
	}
	default:
		errno = ENOTSUP;
		break;
	}
	return param;
}
