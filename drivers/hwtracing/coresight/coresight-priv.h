/* Copyright (c) 2011-2012, 2017-2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CORESIGHT_PRIV_H
#define _CORESIGHT_PRIV_H

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/coresight.h>
#include <linux/pm_runtime.h>

/*
 * Coresight management registers (0xf00-0xfcc)
 * 0xfa0 - 0xfa4: Management	registers in PFTv1.0
 *		  Trace		registers in PFTv1.1
 */
#define CORESIGHT_ITCTRL	0xf00
#define CORESIGHT_CLAIMSET	0xfa0
#define CORESIGHT_CLAIMCLR	0xfa4
#define CORESIGHT_LAR		0xfb0
#define CORESIGHT_LSR		0xfb4
#define CORESIGHT_AUTHSTATUS	0xfb8
#define CORESIGHT_DEVID		0xfc8
#define CORESIGHT_DEVTYPE	0xfcc

#define TIMEOUT_US		100
#define BM(lsb, msb)		((BIT(msb) - BIT(lsb)) + BIT(msb))
#define BMVAL(val, lsb, msb)	((val & GENMASK(msb, lsb)) >> lsb)
#define BVAL(val, n)            ((val & BIT(n)) >> n)

#define ETM_MODE_EXCL_KERN	BIT(30)
#define ETM_MODE_EXCL_USER	BIT(31)

typedef u32 (*coresight_read_fn)(const struct device *, u32 offset);
#define __coresight_simple_func(type, func, name, lo_off, hi_off)	\
static ssize_t name##_show(struct device *_dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	type *drvdata = dev_get_drvdata(_dev->parent);			\
	coresight_read_fn fn = func;					\
	u64 val;							\
	pm_runtime_get_sync(_dev->parent);				\
	if (fn)								\
		val = (u64)fn(_dev->parent, lo_off);			\
	else								\
		val = coresight_read_reg_pair(drvdata->base,		\
						 lo_off, hi_off);	\
	pm_runtime_put_sync(_dev->parent);				\
	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", val);		\
}									\
static DEVICE_ATTR_RO(name)

#define coresight_simple_func(type, func, name, offset)			\
	__coresight_simple_func(type, func, name, offset, -1)
#define coresight_simple_reg32(type, name, offset)			\
	__coresight_simple_func(type, NULL, name, offset, -1)
#define coresight_simple_reg64(type, name, lo_off, hi_off)		\
	__coresight_simple_func(type, NULL, name, lo_off, hi_off)

extern const u32 barrier_pkt[5];

enum etm_addr_type {
	ETM_ADDR_TYPE_NONE,
	ETM_ADDR_TYPE_SINGLE,
	ETM_ADDR_TYPE_RANGE,
	ETM_ADDR_TYPE_START,
	ETM_ADDR_TYPE_STOP,
};

enum cs_mode {
	CS_MODE_DISABLED,
	CS_MODE_SYSFS,
	CS_MODE_PERF,
};

struct coresight_csr {
	const char *name;
	struct list_head link;
};

/**
 * struct cs_buffer - keep track of a recording session' specifics
 * @cur:	index of the current buffer
 * @nr_pages:	max number of pages granted to us
 * @offset:	offset within the current buffer
 * @data_size:	how much we collected in this run
 * @snapshot:	is this run in snapshot mode
 * @data_pages:	a handle the ring buffer
 */
struct cs_buffers {
	unsigned int		cur;
	unsigned int		nr_pages;
	unsigned long		offset;
	local_t			data_size;
	bool			snapshot;
	void			**data_pages;
};

static inline void CS_LOCK(void __iomem *addr)
{
	do {
		/* Wait for things to settle */
		mb();
		writel_relaxed(0x0, addr + CORESIGHT_LAR);
	} while (0);
}

static inline void CS_UNLOCK(void __iomem *addr)
{
	do {
		writel_relaxed(CORESIGHT_UNLOCK, addr + CORESIGHT_LAR);
		/* Make sure everyone has seen this */
		mb();
	} while (0);
}

static inline u64
coresight_read_reg_pair(void __iomem *addr, s32 lo_offset, s32 hi_offset)
{
	u64 val;

	val = readl_relaxed(addr + lo_offset);
	val |= (hi_offset < 0) ? 0 :
	       (u64)readl_relaxed(addr + hi_offset) << 32;
	return val;
}

static inline void coresight_write_reg_pair(void __iomem *addr, u64 val,
						 s32 lo_offset, s32 hi_offset)
{
	writel_relaxed((u32)val, addr + lo_offset);
	if (hi_offset >= 0)
		writel_relaxed((u32)(val >> 32), addr + hi_offset);
}

static inline bool coresight_authstatus_enabled(void __iomem *addr)
{
	int ret;
	unsigned int auth_val;

	if (!addr)
		return false;

	auth_val = readl_relaxed(addr + CORESIGHT_AUTHSTATUS);

	if ((BMVAL(auth_val, 0, 1) == 0x2) ||
		(BMVAL(auth_val, 2, 3) == 0x2) ||
		(BMVAL(auth_val, 4, 5) == 0x2) ||
		(BMVAL(auth_val, 6, 7) == 0x2))
		ret = false;
	else
		ret = true;

	return ret;
}
void coresight_disable_path(struct list_head *path);
int coresight_enable_path(struct list_head *path, u32 mode);
struct coresight_device *coresight_get_sink(struct list_head *path);
struct coresight_device *coresight_get_enabled_sink(bool reset);
struct list_head *coresight_build_path(struct coresight_device *csdev,
				       struct coresight_device *sink);
struct coresight_device *coresight_get_source(struct list_head *path);
void coresight_release_path(struct list_head *path);

#ifdef CONFIG_CORESIGHT_SOURCE_ETM3X
extern int etm_readl_cp14(u32 off, unsigned int *val);
extern int etm_writel_cp14(u32 off, u32 val);
#else
static inline int etm_readl_cp14(u32 off, unsigned int *val) { return 0; }
static inline int etm_writel_cp14(u32 off, u32 val) { return 0; }
#endif

#ifdef CONFIG_CORESIGHT_CSR
extern void msm_qdss_csr_enable_bam_to_usb(struct coresight_csr *csr);
extern void msm_qdss_csr_enable_flush(struct coresight_csr *csr);
extern void msm_qdss_csr_disable_bam_to_usb(struct coresight_csr *csr);
extern void msm_qdss_csr_disable_flush(struct coresight_csr *csr);
extern int coresight_csr_hwctrl_set(struct coresight_csr *csr, uint64_t addr,
				 uint32_t val);
extern void coresight_csr_set_byte_cntr(struct coresight_csr *csr,
				 uint32_t count);
extern struct coresight_csr *coresight_csr_get(const char *name);
#else
static inline void msm_qdss_csr_enable_bam_to_usb(struct coresight_csr *csr) {}
extern void msm_qdss_csr_enable_flush(struct coresight_csr *csr) {}
static inline void msm_qdss_csr_disable_bam_to_usb(struct coresight_csr *csr) {}
static inline void msm_qdss_csr_disable_flush(struct coresight_csr *csr) {}
static inline int coresight_csr_hwctrl_set(struct coresight_csr *csr,
	uint64_t addr, uint32_t val) { return -EINVAL; }
static inline void coresight_csr_set_byte_cntr(struct coresight_csr *csr,
					   uint32_t count) {}
static inline struct coresight_csr *coresight_csr_get(const char *name)
					{ return NULL; }
#endif
#endif
