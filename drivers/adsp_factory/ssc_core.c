/*
 *  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/adsp/slpi_motor.h>
#include <linux/adsp/ssc_ssr_reason.h>
#include <linux/string.h>

#ifdef CONFIG_SUPPORT_DEVICE_MODE
#include <linux/hall.h>
#endif
#include "adsp.h"

#define SSR_REASON_LEN	128
#ifdef CONFIG_SEC_FACTORY
#define SLPI_STUCK "SLPI_STUCK"
#define SLPI_PASS "SLPI_PASS"
#define PROBE_FAIL "PROBE_FAIL"
#endif

static int pid;
static char panic_msg[SSR_REASON_LEN];
#ifdef CONFIG_SUPPORT_DEVICE_MODE
static int32_t curr_fstate;
#endif

#if defined(CONFIG_SEC_WINNERLTE_PROJECT) && defined(CONFIG_SEC_FACTORY)
extern bool is_pretest(void);
#endif
/*
 * send ssr notice to ois mcu
 */
#define NO_OIS_STRUCT (-1)

struct ois_sensor_interface{
	void *core;
	void (*ois_func)(void *);
};

static struct ois_sensor_interface ois_reset;

int ois_reset_register(struct ois_sensor_interface *ois)
{
	if (ois->core)
		ois_reset.core = ois->core;

	if (ois->ois_func)
		ois_reset.ois_func = ois->ois_func;

	if (!ois->core || !ois->ois_func) {
		pr_info("[FACTORY] %s - no ois struct\n", __func__);
		return NO_OIS_STRUCT;
	}

	return 0;
}
EXPORT_SYMBOL(ois_reset_register);

void ois_reset_unregister(void)
{
	ois_reset.core = NULL;
	ois_reset.ois_func = NULL;
}
EXPORT_SYMBOL(ois_reset_unregister);

/*************************************************************************/
/* factory Sysfs							 */
/*************************************************************************/

static char operation_mode_flag[11];
#ifdef CONFIG_SEC_WINNERLTE_PROJECT
static int get_prox_sidx(struct adsp_data *data)
{
	int ret = MSG_PROX;
#ifdef CONFIG_SUPPORT_DUAL_OPTIC
	switch (data->fac_fstate) {
	case FSTATE_INACTIVE:
	case FSTATE_FAC_INACTIVE:
		ret = MSG_PROX;
		break;
	case FSTATE_ACTIVE:
	case FSTATE_FAC_ACTIVE:
		ret = MSG_PROX_SUB;
		break;
	default:
		break;
	}
#endif
	return ret;
}
#endif
static ssize_t dumpstate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	uint8_t cnt = 0;
	int32_t type[1];
	if (pid != 0) {
		adsp_unicast(NULL, 0, MSG_ACCEL, 0, MSG_TYPE_GET_DHR_INFO);
		while (!(data->ready_flag[MSG_TYPE_GET_DHR_INFO] & 1 << MSG_ACCEL) &&
			cnt++ < TIMEOUT_CNT)
			usleep_range(500, 550);

		data->ready_flag[MSG_TYPE_GET_DHR_INFO] &= ~(1 << MSG_ACCEL);

		if (cnt >= TIMEOUT_CNT)
			pr_err("[FACTORY] %s: accel dhr_sensor_info Timeout!!!\n",
			__func__);

#ifdef CONFIG_SEC_WINNERLTE_PROJECT
		adsp_unicast(NULL, 0, MSG_ACCEL_SUB, 0, MSG_TYPE_GET_DHR_INFO);
		while (!(data->ready_flag[MSG_TYPE_GET_DHR_INFO] & 1 << MSG_ACCEL_SUB) &&
			cnt++ < TIMEOUT_CNT)
			usleep_range(500, 550);

		data->ready_flag[MSG_TYPE_GET_DHR_INFO] &= ~(1 << MSG_ACCEL_SUB);

		if (cnt >= TIMEOUT_CNT)
			pr_err("[FACTORY] %s: sub_accel dhr_sensor_info Timeout!!!\n",
			__func__);

		adsp_unicast(NULL, 0, get_prox_sidx(data), 0, MSG_TYPE_GET_DHR_INFO);
#else
		adsp_unicast(NULL, 0, MSG_LIGHT, 0, MSG_TYPE_GET_DHR_INFO);
#endif
		
		while (!(data->ready_flag[MSG_TYPE_GET_DHR_INFO] & 1 << MSG_LIGHT) &&
			cnt++ < TIMEOUT_CNT)
			usleep_range(500, 550);

		data->ready_flag[MSG_TYPE_GET_DHR_INFO] &= ~(1 << MSG_LIGHT);

		if (cnt >= TIMEOUT_CNT)
			pr_err("[FACTORY] %s: light dhr_sensor_info Timeout!!!\n",
				__func__);

		pr_info("[FACTORY] to take the logs\n");
		type[0] = 0;
	} else {
		type[0] = 2;
		pr_info("[FACTORY] logging service was stopped %d\n", pid);
	}
	adsp_unicast(type, sizeof(type), MSG_SSC_CORE, 0, MSG_TYPE_DUMPSTATE);
	pr_info("[FACTORY] %s support_algo = %u\n", __func__, data->support_algo);
	return snprintf(buf, PAGE_SIZE, "SSC_CORE\n");
}

static ssize_t operation_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s", operation_mode_flag);
}

static ssize_t operation_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < 10 && buf[i] != '\0'; i++)
		operation_mode_flag[i] = buf[i];
	operation_mode_flag[i] = '\0';

	if (!strncmp(operation_mode_flag, "restrict", 8)) {
		pr_info("[FACTORY] %s: set restrict_mode\n", __func__);
		data->restrict_mode = true;
	} else {
		pr_info("[FACTORY] %s: set normal_mode\n", __func__);
		data->restrict_mode = false;
	}
	pr_info("[FACTORY] %s: operation_mode_flag = %s\n", __func__,
		operation_mode_flag);
	return size;
}

static ssize_t mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned long timeout;
	unsigned long timeout_2;
	int32_t type[1];

	if (pid != 0) {
		timeout = jiffies + (2 * HZ);
		timeout_2 = jiffies + (10 * HZ);
		type[0] = 1;
		pr_info("[FACTORY] To stop logging %d\n", pid);
		adsp_unicast(type, sizeof(type), MSG_SSC_CORE, 0, MSG_TYPE_DUMPSTATE);

		while (pid != 0) {
			msleep(25);
			if (time_after(jiffies, timeout))
				pr_info("[FACTORY] %s: Timeout!!!\n", __func__);
			if (time_after(jiffies, timeout_2)) {
//				panic("force crash : ssc core\n");
				pr_info("[FACTORY] pid %d\n", pid);
				return snprintf(buf, PAGE_SIZE, "1\n");
			}
		}
	}

	return snprintf(buf, PAGE_SIZE, "0\n");
}

static ssize_t mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int data = 0;
	int32_t type[1];
	if (kstrtoint(buf, 10, &data)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}

	if (data != 1) {
		pr_err("[FACTORY] %s: data was wrong\n", __func__);
		return -EINVAL;
	}

	if (pid != 0) {
		type[0] = 1;
		adsp_unicast(type, sizeof(type), MSG_SSC_CORE, 0, MSG_TYPE_DUMPSTATE);
	}

	return size;
}

static ssize_t pid_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", pid);
}

static ssize_t pid_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int data = 0;

	if (kstrtoint(buf, 10, &data)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}

	pid = data;
	pr_info("[FACTORY] %s: pid %d\n", __func__, pid);

	return size;
}

static ssize_t remove_sensor_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int type = MSG_SENSOR_MAX;
	struct adsp_data *data = dev_get_drvdata(dev);

	if (kstrtouint(buf, 10, &type)) {
		pr_err("[FACTORY] %s: kstrtouint fail\n", __func__);
		return -EINVAL;
	}

	if (type > PHYSICAL_SENSOR_SYSFS) {
		pr_err("[FACTORY] %s: Invalid type %u\n", __func__, type);
		return size;
	}

	pr_info("[FACTORY] %s: type = %u\n", __func__, type);

	mutex_lock(&data->remove_sysfs_mutex);
	adsp_factory_unregister(type);
	mutex_unlock(&data->remove_sysfs_mutex);

	return size;
}

extern unsigned int sec_hw_rev(void);
int ssc_system_rev_test;
static ssize_t ssc_hw_rev_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_info("[FACTORY] %s: ssc_rev:%d\n", __func__, sec_hw_rev());

	if (!ssc_system_rev_test)
		return snprintf(buf, PAGE_SIZE, "%d\n", sec_hw_rev());
	else
		return snprintf(buf, PAGE_SIZE, "%d\n", ssc_system_rev_test);
}

static ssize_t ssc_hw_rev_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	if (kstrtoint(buf, 10, &ssc_system_rev_test)) {
		pr_err("[FACTORY] %s: kstrtoint fail\n", __func__);
		return -EINVAL;
	}

	pr_info("[FACTORY] %s: system_rev_ssc %d\n", __func__, ssc_system_rev_test);

	return size;
}

void ssr_reason_call_back(char reason[], int len)
{
	if (len <= 0) {
		pr_info("[FACTORY] ssr %d\n", len);
		return;
	}
	memset(panic_msg, 0, SSR_REASON_LEN);
	strlcpy(panic_msg, reason, min(len, (int)(SSR_REASON_LEN - 1)));

	pr_info("[FACTORY] ssr %s\n", panic_msg);

	if (ois_reset.ois_func != NULL && ois_reset.core != NULL) {
		ois_reset.ois_func(ois_reset.core);
		pr_info("[FACTORY] %s - send ssr notice to ois mcu\n", __func__);
	} else {
		pr_info("[FACTORY] %s - no ois struct\n", __func__);
	}
}

static ssize_t ssr_msg_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
#ifdef CONFIG_SEC_FACTORY
	struct adsp_data *data = dev_get_drvdata(dev);
	int32_t raw_data_acc[3] = {0, };
	int32_t raw_data_mag[3] = {0, };
	int ret_acc = 0;
	int ret_mag = 0;
#ifdef CONFIG_SEC_WINNERLTE_PROJECT
	if (is_pretest())
		return snprintf(buf, PAGE_SIZE, "%s\n", panic_msg);
#endif
	if (!data->sysfs_created[MSG_ACCEL] || !data->sysfs_created[MSG_MAG]) {
		pr_err("[FACTORY] %s: sensor probe fail %d, %d\n",
			__func__, data->sysfs_created[MSG_ACCEL],
			data->sysfs_created[MSG_MAG]);
		return snprintf(buf, PAGE_SIZE, "%s\n", PROBE_FAIL);
	}

	mutex_lock(&data->accel_factory_mutex);
	ret_acc = get_accel_raw_data(raw_data_acc);
	mutex_unlock(&data->accel_factory_mutex);

	ret_mag = get_mag_raw_data(raw_data_mag);

	pr_info("[FACTORY] %s: accel(%d, %d, %d), mag(%d, %d, %d)\n", __func__,
		raw_data_acc[0], raw_data_acc[1], raw_data_acc[2],
		raw_data_mag[0], raw_data_mag[1], raw_data_mag[2]);

	if (ret_acc == -1 && ret_mag == -1) {
		pr_err("[FACTORY] %s: SLPI stuck, check hal log\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s\n", SLPI_STUCK);
	} else {
		return snprintf(buf, PAGE_SIZE, "%s\n", SLPI_PASS);
	}
#else
	return snprintf(buf, PAGE_SIZE, "%s\n", panic_msg);
#endif
}

static ssize_t ssr_reset_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int32_t type[1];

	type[0] = 0xff;

	adsp_unicast(type, sizeof(type), MSG_SSC_CORE, 0, MSG_TYPE_DUMPSTATE);
	pr_info("[FACTORY] %s\n", __func__);

	return snprintf(buf, PAGE_SIZE, "%s\n", "Success");
}

static ssize_t support_algo_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int support_algo = 0;
	struct adsp_data *data = dev_get_drvdata(dev);

	if (kstrtouint(buf, 10, &support_algo)) {
		pr_err("[FACTORY] %s: kstrtouint fail\n", __func__);
		return -EINVAL;
	}

        data->support_algo = (uint32_t)support_algo;
	pr_info("[FACTORY] %s support_algo = %u\n", __func__, support_algo);

	return size;
}

#ifdef CONFIG_SUPPORT_DEVICE_MODE
int sns_device_mode_notify(struct notifier_block *nb,
	unsigned long flip_state, void *v)
{
	struct adsp_data *data = container_of(nb, struct adsp_data, adsp_nb);
	
	pr_info("[FACTORY] %s - before device mode curr:%d, fstate:%d",
		__func__, curr_fstate, data->fac_fstate);

	data->fac_fstate = curr_fstate = (int32_t)flip_state;
	pr_info("[FACTORY] %s - after device mode curr:%d, fstate:%d",
		__func__, curr_fstate, data->fac_fstate);

	if(curr_fstate == 0)
		adsp_unicast(NULL, 0, MSG_SSC_CORE, 0, MSG_TYPE_FACTORY_ENABLE);
	else
		adsp_unicast(NULL, 0, MSG_SSC_CORE, 0, MSG_TYPE_FACTORY_DISABLE);

	return 0;
}

void sns_device_mode_init_work(void)
{
	if(curr_fstate == 0)
		adsp_unicast(NULL, 0, MSG_SSC_CORE, 0, MSG_TYPE_FACTORY_ENABLE);
	else
		adsp_unicast(NULL, 0, MSG_SSC_CORE, 0, MSG_TYPE_FACTORY_DISABLE);
}
#endif

#ifdef CONFIG_SUPPORT_VIRTUAL_OPTIC
static ssize_t fac_fstate_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct adsp_data *data = dev_get_drvdata(dev);
	int32_t fstate[1] = {0};
	
	if (sysfs_streq(buf, "0"))
		data->fac_fstate = fstate[0] = curr_fstate;
	else if (sysfs_streq(buf, "1"))
		data->fac_fstate = fstate[0] = FSTATE_FAC_INACTIVE;
	else if (sysfs_streq(buf, "2"))
		data->fac_fstate = fstate[0] = FSTATE_FAC_ACTIVE;
	else
		data->fac_fstate = fstate[0] = curr_fstate;

	adsp_unicast(fstate, sizeof(fstate),
		MSG_VIR_OPTIC, 0, MSG_TYPE_OPTION_DEFINE);
	pr_info("[FACTORY] %s - Factory flip state:%d",
		__func__, fstate[0]);

	return size;
}
#endif

static ssize_t abs_lcd_onoff_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int32_t msg_buf[1];
	int new_value;

	if (sysfs_streq(buf, "0"))
		new_value = 0;
	else if (sysfs_streq(buf, "1"))
		new_value = 1;
	else
		return size;

	pr_info("[FACTORY] %s: new_value %d\n", __func__, new_value);
	msg_buf[0] = new_value;

	adsp_unicast(msg_buf, sizeof(msg_buf),
		MSG_SSC_CORE, 0, MSG_TYPE_OPTION_DEFINE);

	return size;
}

static DEVICE_ATTR(dumpstate, 0440, dumpstate_show, NULL);
static DEVICE_ATTR(operation_mode, 0664,
	operation_mode_show, operation_mode_store);
static DEVICE_ATTR(mode, 0660, mode_show, mode_store);
static DEVICE_ATTR(ssc_pid, 0660, pid_show, pid_store);
static DEVICE_ATTR(remove_sysfs, 0220, NULL, remove_sensor_sysfs_store);
static DEVICE_ATTR(ssc_hw_rev, 0664, ssc_hw_rev_show, ssc_hw_rev_store);
static DEVICE_ATTR(ssr_msg, 0440, ssr_msg_show, NULL);
static DEVICE_ATTR(ssr_reset, 0440, ssr_reset_show, NULL);
static DEVICE_ATTR(support_algo, 0220, NULL, support_algo_store);
#ifdef CONFIG_SUPPORT_VIRTUAL_OPTIC
static DEVICE_ATTR(fac_fstate, 0220, NULL, fac_fstate_store);
#endif
static DEVICE_ATTR(abs_lcd_onoff, 0220, NULL, abs_lcd_onoff_store);

static struct device_attribute *core_attrs[] = {
	&dev_attr_dumpstate,
	&dev_attr_operation_mode,
	&dev_attr_mode,
	&dev_attr_ssc_pid,
	&dev_attr_remove_sysfs,
	&dev_attr_ssc_hw_rev,
	&dev_attr_ssr_msg,
	&dev_attr_ssr_reset,
	&dev_attr_support_algo,
#ifdef CONFIG_SUPPORT_VIRTUAL_OPTIC	
	&dev_attr_fac_fstate,
#endif
	&dev_attr_abs_lcd_onoff,
	NULL,
};

static int __init core_factory_init(void)
{
#ifdef CONFIG_SUPPORT_DEVICE_MODE
	struct adsp_data *data = adsp_ssc_core_register(MSG_SSC_CORE, core_attrs);
	
	pr_info("[FACTORY] %s\n", __func__);
	data->adsp_nb.notifier_call = sns_device_mode_notify,
	data->adsp_nb.priority = 1,
        hall_ic_register_notify(&data->adsp_nb);
#else
	adsp_factory_register(MSG_SSC_CORE, core_attrs);
#endif
	pr_info("[FACTORY] %s\n", __func__);

	return 0;
}

static void __exit core_factory_exit(void)
{

#ifdef CONFIG_SUPPORT_DEVICE_MODE
	struct adsp_data *data = adsp_ssc_core_unregister(MSG_SSC_CORE);;

	hall_ic_unregister_notify(&data->adsp_nb);
#else
	adsp_factory_unregister(MSG_SSC_CORE);
#endif
	pr_info("[FACTORY] %s\n", __func__);
}
module_init(core_factory_init);
module_exit(core_factory_exit);
