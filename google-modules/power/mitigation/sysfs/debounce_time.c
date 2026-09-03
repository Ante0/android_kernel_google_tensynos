// SPDX-License-Identifier: GPL-2.0 only

#include "debounce_time.h"
#include <max77779.h>
#include "bcl.h"

static int read_debounce(struct device *dev, int idx, enum DEBOUNCE_TYPE type,
						 bool is_int)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u8 val, regval, reg_addr;

	if (!bcl_dev->intf_pmic_dev)
		return -EBUSY;

	switch (idx) {
	case UVLO1:
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO1_CNFG_1, &regval) < 0)
			return -EIO;
		if (type == deglitch) {
			val = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_det_get(regval);
			bcl_dev->batt_irq_conf1.uvlo_det = val;
		} else {
			val = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_rel_get(regval);
			bcl_dev->batt_irq_conf1.uvlo_rel = val;
		}
		break;
	case UVLO2:
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO2_CNFG_1, &regval) < 0)
			return -EIO;
		if (type == deglitch) {
			val = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_det_get(regval);
			bcl_dev->batt_irq_conf2.uvlo_det = val;
		} else {
			val = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_rel_get(regval);
			bcl_dev->batt_irq_conf2.uvlo_rel = val;
		}
		break;
	case BATOILO1:
		reg_addr = is_int ? MAX77779_BAT_OILO1_CNFG_2 : MAX77779_BAT_OILO1_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		if (is_int) {
			if (type == deglitch) {
				val = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_det_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_int_det = val;
			} else {
				val = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_rel_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_int_rel = val;
			}
		} else {
			if (type == deglitch) {
				val = _max77779_bat_oilo1_cnfg_1_bat_oilo1_det_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_det = val;
			} else {
				val = _max77779_bat_oilo1_cnfg_1_bat_oilo1_rel_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_rel = val;
			}
		}
		break;
	case BATOILO2:
		reg_addr = is_int ? MAX77779_BAT_OILO2_CNFG_2 : MAX77779_BAT_OILO2_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		if (is_int) {
			if (type == deglitch) {
				val = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_det_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_int_det = val;
			} else {
				val = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_rel_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_int_rel = val;
			}
		} else {
			if (type == deglitch) {
				val = _max77779_bat_oilo2_cnfg_1_bat_oilo2_det_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_det = val;
			} else {
				val = _max77779_bat_oilo2_cnfg_1_bat_oilo2_rel_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_rel = val;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	return val;
}

static int write_debounce(struct device *dev, const char *buf, int idx,
						  enum DEBOUNCE_TYPE type, bool is_int)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u8 val, regval, *irq_conf_val, reg_addr;
	int ret;

	if (!bcl_dev->intf_pmic_dev)
		return -EBUSY;

	ret = kstrtou8(buf, 16, &val);
	if (ret)
		return ret;

	switch (idx) {
	case UVLO1:
		if ((type == deglitch && val > UVLO_DET_MAX) ||
		    (type == release && val > UVLO_REL_MAX))
			return -EINVAL;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO1_CNFG_1, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[UVLO1]->bcl_irq);
		if (type == deglitch) {
			regval = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_det_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf1.uvlo_det;
		} else {
			regval = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_rel_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf1.uvlo_rel;
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							MAX77779_SYS_UVLO1_CNFG_1, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[UVLO1]->bcl_irq);
		break;
	case UVLO2:
		if ((type == deglitch && val > UVLO_DET_MAX) ||
		    (type == release && val > UVLO_REL_MAX))
			return -EINVAL;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO2_CNFG_1, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[UVLO2]->bcl_irq);
		if (type == deglitch) {
			regval = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_det_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf2.uvlo_det;
		} else {
			regval = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_rel_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf2.uvlo_rel;
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							MAX77779_SYS_UVLO2_CNFG_1, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[UVLO2]->bcl_irq);
		break;
	case BATOILO1:
		if ((type == deglitch && val > OILO_DET_MAX) ||
		    (type == release && val > OILO_REL_MAX))
			return -EINVAL;
		reg_addr = is_int ? MAX77779_BAT_OILO1_CNFG_2 : MAX77779_BAT_OILO1_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[BATOILO1]->bcl_irq);
		if (is_int) {
			if (type == deglitch) {
				regval = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_det_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_int_det;
			} else {
				regval = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_rel_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_int_rel;
			}
		} else {
			if (type == deglitch) {
				regval = _max77779_bat_oilo1_cnfg_1_bat_oilo1_det_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_det;
			} else {
				regval = _max77779_bat_oilo1_cnfg_1_bat_oilo1_rel_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_rel;
			}
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							reg_addr, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[BATOILO1]->bcl_irq);
		break;
	case BATOILO2:
		if ((type == deglitch && val > OILO_DET_MAX) ||
		    (type == release && val > OILO_REL_MAX))
			return -EINVAL;
		reg_addr = is_int ? MAX77779_BAT_OILO2_CNFG_2 : MAX77779_BAT_OILO2_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[BATOILO2]->bcl_irq);
		if (is_int) {
			if (type == deglitch) {
				regval = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_det_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_int_det;
			} else {
				regval = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_rel_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_int_rel;
			}
		} else {
			if (type == deglitch) {
				regval = _max77779_bat_oilo2_cnfg_1_bat_oilo2_det_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_det;
			} else {
				regval = _max77779_bat_oilo2_cnfg_1_bat_oilo2_rel_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_rel;
			}
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							reg_addr, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[BATOILO2]->bcl_irq);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static ssize_t uvlo1_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO1, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t uvlo1_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO1, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(uvlo1_det);

static ssize_t uvlo2_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO2, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);

}

static ssize_t uvlo2_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO2, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(uvlo2_det);

static ssize_t batoilo1_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo1_det);

static ssize_t batoilo2_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo2_det);

static ssize_t batoilo1_int_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, deglitch, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_int_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, deglitch, true);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo1_int_det);

static ssize_t batoilo2_int_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, deglitch, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_int_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, deglitch, true);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo2_int_det);

static ssize_t uvlo1_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO1, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t uvlo1_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO1, release, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(uvlo1_rel);

static ssize_t uvlo2_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO2, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t uvlo2_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO2, release, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(uvlo2_rel);

static ssize_t batoilo1_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, release, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo1_rel);

static ssize_t batoilo2_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, release, false);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo2_rel);

static ssize_t batoilo1_int_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, release, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_int_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, release, true);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo1_int_rel);

static ssize_t batoilo2_int_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, release, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_int_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, release, true);

	if (ret < 0)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo2_int_rel);
