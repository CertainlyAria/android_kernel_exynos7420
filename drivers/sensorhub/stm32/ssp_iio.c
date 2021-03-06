/*
 *  Copyright (C) 2015, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include "ssp_iio.h"
#include "ssp_sensorhub.h"

static void init_sensorlist(struct ssp_data *data)
{
	struct sensor_info sensorinfo[SENSOR_TYPE_MAX] = {
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_ACCELEROMETER,
		SENSOR_INFO_GEOMAGNETIC_FIELD,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_LIGHT,
		SENSOR_INFO_PRESSURE,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_PROXIMITY,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_ROTATION_VECTOR,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_MAGNETIC_FIELD_UNCALIBRATED,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_SIGNIFICANT_MOTION,
		SENSOR_INFO_STEP_DETECTOR,
		SENSOR_INFO_STEP_COUNTER,
		SENSOR_INFO_GEOMAGNETIC_ROTATION_VECTOR, 
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_TILT_DETECTOR,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_PICK_UP_GESTURE,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_PROXIMITY_RAW,
		SENSOR_INFO_GEOMAGNETIC_POWER,
#if ANDROID_VERSION >= 80000
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_SCONTEXT,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_LIGHT_CCT,
#endif
};	

	memcpy(&data->info, sensorinfo, sizeof(data->info));
}

static int ssp_preenable(struct iio_dev *indio_dev)
{
	return iio_sw_buffer_preenable(indio_dev);
}

static int ssp_predisable(struct iio_dev *indio_dev)
{
	return 0;
}

static const struct iio_buffer_setup_ops ssp_iio_ring_setup_ops = {
	.preenable = &ssp_preenable,
	.predisable = &ssp_predisable,
};

static int ssp_iio_configure_ring(struct iio_dev *indio_dev)
{
	struct iio_buffer *ring;

	ring = iio_kfifo_allocate(indio_dev);
	if (!ring)
		return -ENOMEM;

	ring->scan_timestamp = true;
	ring->bytes_per_datum = 8;
	indio_dev->buffer = ring;
	indio_dev->setup_ops = &ssp_iio_ring_setup_ops;
	indio_dev->modes |= INDIO_BUFFER_HARDWARE;

	return 0;
}

static void ssp_iio_push_buffers(struct iio_dev *indio_dev, u64 timestamp,
				char *data, int data_len)
{
	char buf[data_len + sizeof(timestamp)];

	if (!indio_dev || !data)
		return;

	memcpy(buf, data, data_len);
	memcpy(buf + data_len, &timestamp, sizeof(timestamp));
	mutex_lock(&indio_dev->mlock);
	iio_push_to_buffers(indio_dev, buf);
	mutex_unlock(&indio_dev->mlock);
}

static void report_prox_raw_data(struct ssp_data *data, int type,
	struct sensor_value *proxrawdata)
{
	if (data->uFactoryProxAvg[0]++ >= PROX_AVG_READ_NUM) {
		data->uFactoryProxAvg[2] /= PROX_AVG_READ_NUM;
		data->buf[type].prox_raw[1] = (u16)data->uFactoryProxAvg[1];
		data->buf[type].prox_raw[2] = (u16)data->uFactoryProxAvg[2];
		data->buf[type].prox_raw[3] = (u16)data->uFactoryProxAvg[3];

		data->uFactoryProxAvg[0] = 0;
		data->uFactoryProxAvg[1] = 0;
		data->uFactoryProxAvg[2] = 0;
		data->uFactoryProxAvg[3] = 0;
	} else {
		data->uFactoryProxAvg[2] += proxrawdata->prox_raw[0];

		if (data->uFactoryProxAvg[0] == 1)
			data->uFactoryProxAvg[1] = proxrawdata->prox_raw[0];
		else if (proxrawdata->prox_raw[0] < data->uFactoryProxAvg[1])
			data->uFactoryProxAvg[1] = proxrawdata->prox_raw[0];

		if (proxrawdata->prox_raw[0] > data->uFactoryProxAvg[3])
			data->uFactoryProxAvg[3] = proxrawdata->prox_raw[0];
	}

	data->buf[type].prox_raw[0] = proxrawdata->prox_raw[0];
}

void report_sensor_data(struct ssp_data *data, int type,
			struct sensor_value *event)
{
	if (type == SENSOR_TYPE_PROXIMITY) {
		ssp_info("Proximity Sensor Detect : %u, raw : %u",
			event->prox, event->prox_ex);

	} else if (type == SENSOR_TYPE_PROXIMITY_RAW) {
		report_prox_raw_data(data, type, event);
		return;

	} else if (type == SENSOR_TYPE_LIGHT) {
		event->a_gain &= 0x03;
		if (data->light_log_cnt < 3) {
			ssp_info("Light Sensor : lux=%d r=%d g=%d b=%d c=%d atime=%d again=%d",
			data->buf[SENSOR_TYPE_LIGHT].lux,
			data->buf[SENSOR_TYPE_LIGHT].r, data->buf[SENSOR_TYPE_LIGHT].g,
			data->buf[SENSOR_TYPE_LIGHT].b, data->buf[SENSOR_TYPE_LIGHT].w,
			data->buf[SENSOR_TYPE_LIGHT].a_time, data->buf[SENSOR_TYPE_LIGHT].a_gain);
			data->light_log_cnt++;
		}
	}
#if ANDROID_VERSION >= 80000
	else if (type == SENSOR_TYPE_LIGHT_CCT) {
		event->a_gain &= 0x03;
		if (data->light_log_cnt < 3) {
			ssp_info("Light cct Sensor : lux=%d r=%d g=%d b=%d c=%d atime=%d again=%d",
			data->buf[SENSOR_TYPE_LIGHT_CCT].lux,
			data->buf[SENSOR_TYPE_LIGHT_CCT].r, data->buf[SENSOR_TYPE_LIGHT_CCT].g,
			data->buf[SENSOR_TYPE_LIGHT_CCT].b, data->buf[SENSOR_TYPE_LIGHT_CCT].w,
			data->buf[SENSOR_TYPE_LIGHT_CCT].a_time, data->buf[SENSOR_TYPE_LIGHT_CCT].a_gain);
			data->light_log_cnt++;
		}
	}
#endif
	else if (type == SENSOR_TYPE_STEP_COUNTER) {
		data->buf[type].step_total += event->step_diff;
	}

	memcpy(&data->buf[type], (char *)event, data->info[type].get_data_len);
	ssp_iio_push_buffers(data->indio_devs[type], event->timestamp,
			(char *)&data->buf[type], data->info[type].report_data_len);

	/* wake-up sensor */
	if (type == SENSOR_TYPE_PROXIMITY || type == SENSOR_TYPE_SIGNIFICANT_MOTION
		|| type == SENSOR_TYPE_TILT_DETECTOR || type == SENSOR_TYPE_PICK_UP_GESTURE) {
		wake_lock_timeout(&data->ssp_wake_lock, 0.3 * HZ);
	}
}

void report_meta_data(struct ssp_data *data, struct sensor_value *s)
{
	char *meta_event
	        = kzalloc(data->info[s->meta_data.sensor].report_data_len,
	                  GFP_KERNEL);

	ssp_infof("what: %d, sensor: %d",
	          s->meta_data.what, s->meta_data.sensor);

	if (!meta_event) {
		ssp_errf("fail to allocate memory for meta event");
		return;
	}

	memset(meta_event, META_EVENT,
	       data->info[s->meta_data.sensor].report_data_len);
	ssp_iio_push_buffers(data->indio_devs[s->meta_data.sensor],
	                     META_TIMESTAMP, meta_event,
	                     data->info[s->meta_data.sensor].report_data_len);
	kfree(meta_event);
}

#if ANDROID_VERSION >= 80000
void report_scontext_data(struct ssp_data *data, char *data_buf, u32 length)
{
	u64 timestamp;
	char buf[SCONTEXT_HEADER_LEN + SCONTEXT_DATA_LEN + sizeof(timestamp)] = {0, };
	char send_buf[1 + SCONTEXT_DATA_ONCE_LEN] = {0,};
	u16 start, end;

	ssp_sensorhub_log(__func__, data_buf, length);

	start = 0;
	memcpy(buf, &length, sizeof(length));

	timestamp = get_current_timestamp();
	memcpy(buf + SCONTEXT_HEADER_LEN + SCONTEXT_DATA_LEN, &timestamp, sizeof(timestamp));

	while (start < length) {
		int i;
		if (start + SCONTEXT_DATA_LEN < length) {
			end = start + SCONTEXT_DATA_LEN - 1;
		} else {
			memset(buf + SCONTEXT_HEADER_LEN, 0, SCONTEXT_DATA_LEN);
			end = length - 1;
		}
		memcpy(buf + sizeof(length), &start, sizeof(u16));
		memcpy(buf + sizeof(length) + sizeof(start), &end, sizeof(u16));
		memcpy(buf + SCONTEXT_HEADER_LEN, data_buf + start, end - start + 1);

		for (i = 0 ; i < sizeof(buf) ; i = i + SCONTEXT_DATA_ONCE_LEN) {
			send_buf[0] = (char)(i / SCONTEXT_DATA_ONCE_LEN);
			memcpy(&send_buf[1], &buf[i], SCONTEXT_DATA_ONCE_LEN);

/*
			ssp_infof("[%d] 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
			          send_buf[0], send_buf[1], send_buf[2], send_buf[3], send_buf[4], send_buf[5], send_buf[6],
			          send_buf[7], send_buf[8]);


			ssp_infof("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
			          send_buf[9], send_buf[10], send_buf[11], send_buf[12], send_buf[13], send_buf[14], send_buf[15],
			          send_buf[16]);


			ssp_infof("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
			          send_buf[17], send_buf[18], send_buf[19], send_buf[20], send_buf[21], send_buf[22], send_buf[23],
			          send_buf[24]);
*/
			mutex_lock(&data->indio_devs[SENSOR_TYPE_SCONTEXT]->mlock);
			iio_push_to_buffers(data->indio_devs[SENSOR_TYPE_SCONTEXT], send_buf);
			mutex_unlock(&data->indio_devs[SENSOR_TYPE_SCONTEXT]->mlock);
		}

		start = end + 1;
	}
}

void report_scontext_notice_data(struct ssp_data *data, char notice)
{
	char notice_buf[4] = {0x02, 0x01, 0x00, 0x00};
	int len = 3;

	notice_buf[2] = notice;

	if (notice == MSG2SSP_AP_STATUS_WAKEUP) {
		ssp_infof("wake up");
	} else if (notice == MSG2SSP_AP_STATUS_SLEEP) {
		ssp_infof("sleep");
	} else if (notice == MSG2SSP_AP_STATUS_RESET) {
		ssp_infof("reset");
	} else {
		ssp_errf("invalid notice(0x%x)", notice);
	}

	report_scontext_data(data, notice_buf, len);
}
#endif

static void *init_indio_device(struct ssp_data *data,
			const struct iio_info *info,
			const struct iio_chan_spec *channels,
			const char *device_name)
{
	struct iio_dev *indio_dev;
	int ret = 0;

	indio_dev = iio_device_alloc(0);
	if (!indio_dev)
		goto err_alloc;

	indio_dev->name = device_name;
	indio_dev->dev.parent = &data->spi->dev;
	indio_dev->info = info;
	indio_dev->channels = channels;
	indio_dev->num_channels = 1;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->currentmode = INDIO_DIRECT_MODE;

	ret = ssp_iio_configure_ring(indio_dev);
	if (ret)
		goto err_config_ring;

	ret = iio_buffer_register(indio_dev,
			indio_dev->channels, indio_dev->num_channels);
	if (ret)
		goto err_register_buffer;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_register_device;

	return indio_dev;

err_register_device:
	ssp_err("fail to register %s device", device_name);
	iio_buffer_unregister(indio_dev);
err_register_buffer:
	ssp_err("fail to register %s buffer", device_name);
	iio_kfifo_free(indio_dev->buffer);
err_config_ring:
	ssp_err("fail to configure %s ring buffer", device_name);
	iio_device_free(indio_dev);
err_alloc:
	ssp_err("fail to allocate memory for iio %s device", device_name);
	return NULL;
}

static const struct iio_info indio_info = {
	.driver_module = THIS_MODULE,
};

int initialize_indio_dev(struct ssp_data *data)
{
	int timestamp_len = 0;
	int type;

	init_sensorlist(data);

	for (type = 0; type < SENSOR_TYPE_MAX; type++) {
		if (!data->info[type].enable || (data->info[type].report_data_len == 0))
			continue;

		timestamp_len = sizeof(data->buf[type].timestamp);

		data->indio_channels[type].type = IIO_TIMESTAMP;
		data->indio_channels[type].channel = IIO_CHANNEL;
		data->indio_channels[type].scan_index = IIO_SCAN_INDEX;
		data->indio_channels[type].scan_type.sign = IIO_SIGN;
#ifdef SENSOR_TYPE_SCONTEXT
		if (type == SENSOR_TYPE_SCONTEXT) {
			data->indio_channels[type].scan_type.realbits = (data->info[type].report_data_len) * BITS_PER_BYTE;
			data->indio_channels[type].scan_type.storagebits = (data->info[type].report_data_len) *
			                                                   BITS_PER_BYTE;
		} else 
#endif
                {
			data->indio_channels[type].scan_type.realbits = (data->info[type].report_data_len + timestamp_len) *
			                                                BITS_PER_BYTE;
			data->indio_channels[type].scan_type.storagebits = (data->info[type].report_data_len +
			                                                    timestamp_len) * BITS_PER_BYTE;
		}
		data->indio_channels[type].scan_type.shift = IIO_SHIFT;

		data->indio_devs[type]
			= (struct iio_dev *)init_indio_device(data,
				&indio_info, &data->indio_channels[type],
				data->info[type].name);
		
		ssp_err("init %s iio dev, type = %d", data->info[type].name, type);
		
		if (!data->indio_devs[type]) {
			ssp_err("fail to init %s iio dev", data->info[type].name);
			remove_indio_dev(data);
			return ERROR;
		}
	}

	return SUCCESS;
}

void remove_indio_dev(struct ssp_data *data)
{
	int type;

	for (type = SENSOR_TYPE_MAX-1; type >= 0; type--) {
		if (data->indio_devs[type])
			iio_device_unregister(data->indio_devs[type]);
	}
}
