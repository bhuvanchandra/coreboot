/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2019 Google LLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <device/pci_ops.h>
#include <bootstate.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/pci.h>
#include <intelblocks/cse.h>
#include <soc/me.h>
#include <soc/pci_devs.h>
#include <stdint.h>

/* Miscellaneous constants */
enum {
	MKHI_GEN_GROUP_ID	= 0xFF,
	MKHI_GET_FW_VERSION	= 0x02,
	ME_OPMODE_NORMAL	= 0x00,
	ME_WSTATE_NORMAL	= 0x05,
};

/* Host Firmware Status Register 2 */
union me_hfsts2 {
	uint32_t raw;
	struct {
		uint32_t nftp_load_failure	: 1;
		uint32_t icc_prog_status	: 2;
		uint32_t invoke_mebx		: 1;
		uint32_t cpu_replaced		: 1;
		uint32_t rsvd0			: 1;
		uint32_t mfs_failure		: 1;
		uint32_t warm_reset_rqst	: 1;
		uint32_t cpu_replaced_valid	: 1;
		uint32_t low_power_state	: 1;
		uint32_t me_power_gate		: 1;
		uint32_t ipu_needed		: 1;
		uint32_t forced_safe_boot	: 1;
		uint32_t rsvd1			: 2;
		uint32_t listener_change	: 1;
		uint32_t status_data		: 8;
		uint32_t current_pmevent	: 4;
		uint32_t phase			: 4;
	} __packed fields;
};

/* Host Firmware Status Register 4 */
union me_hfsts4 {
	uint32_t raw;
	struct {
		uint32_t rsvd0			: 9;
		uint32_t enforcement_flow	: 1;
		uint32_t sx_resume_type		: 1;
		uint32_t rsvd1			: 1;
		uint32_t tpms_disconnected	: 1;
		uint32_t rvsd2			: 1;
		uint32_t fwsts_valid		: 1;
		uint32_t boot_guard_self_test	: 1;
		uint32_t rsvd3			: 16;
	} __packed fields;
};

/* Host Firmware Status Register 5 */
union me_hfsts5 {
	uint32_t raw;
	struct {
		uint32_t acm_active		: 1;
		uint32_t valid			: 1;
		uint32_t result_code_source	: 1;
		uint32_t error_status_code	: 5;
		uint32_t acm_done_sts		: 1;
		uint32_t timeout_count		: 7;
		uint32_t scrtm_indicator	: 1;
		uint32_t inc_boot_guard_acm	: 4;
		uint32_t inc_key_manifest	: 4;
		uint32_t inc_boot_policy	: 4;
		uint32_t rsvd0			: 2;
		uint32_t start_enforcement	: 1;
	} __packed fields;
};

/* Host Firmware Status Register 6 */
union me_hfsts6 {
	uint32_t raw;
	struct {
		uint32_t force_boot_guard_acm	: 1;
		uint32_t cpu_debug_disable	: 1;
		uint32_t bsp_init_disable	: 1;
		uint32_t protect_bios_env	: 1;
		uint32_t rsvd0			: 2;
		uint32_t error_enforce_policy	: 2;
		uint32_t measured_boot		: 1;
		uint32_t verified_boot		: 1;
		uint32_t boot_guard_acmsvn	: 4;
		uint32_t kmsvn			: 4;
		uint32_t bpmsvn			: 4;
		uint32_t key_manifest_id	: 4;
		uint32_t boot_policy_status	: 1;
		uint32_t error			: 1;
		uint32_t boot_guard_disable	: 1;
		uint32_t fpf_disable		: 1;
		uint32_t fpf_soc_lock		: 1;
		uint32_t txt_support		: 1;
	} __packed fields;
};

/*
 * From reading the documentation, this should work for both WHL and CML
 * platforms.  Also, calling this function from dump_me_status() does not
 * work, as the ME does not respond and the command times out.
 */
static void print_me_version(void *unused)
{
	struct version {
		uint16_t minor;
		uint16_t major;
		uint16_t build;
		uint16_t hotfix;
	} __packed;

	struct fw_ver_resp {
		struct mkhi_hdr hdr;
		struct version code;
		struct version rec;
		struct version fitc;
	} __packed;

	union me_hfsts1 hfsts1;
	const struct mkhi_hdr fw_ver_msg = {
		.group_id = MKHI_GEN_GROUP_ID,
		.command = MKHI_GET_FW_VERSION,
	};
	struct fw_ver_resp resp;
	size_t resp_size = sizeof(resp);

	/* Ignore if UART debugging is disabled */
	if (!CONFIG(CONSOLE_SERIAL))
		return;

	if (!is_cse_enabled())
		return;

	hfsts1.data = me_read_config32(PCI_ME_HFSTS1);

	/*
	 * Prerequisites:
	 * 1) HFSTS1 Current Working State is Normal
	 * 2) HFSTS1 Current Operation Mode is Normal
	 * 3) It's after DRAM INIT DONE message (taken care of by calling it
	 *    during ramstage
	 */
	if ((hfsts1.fields.working_state != ME_WSTATE_NORMAL) ||
		(hfsts1.fields.operation_mode != ME_OPMODE_NORMAL))
		goto fail;

	heci_reset();

	if (!heci_send(&fw_ver_msg, sizeof(fw_ver_msg), BIOS_HOST_ADDR,
			HECI_MKHI_ADDR))
		goto fail;

	if (!heci_receive(&resp, &resp_size))
		goto fail;

	if (resp.hdr.result)
		goto fail;

	printk(BIOS_DEBUG, "ME: Version: %d.%d.%d.%d\n", resp.code.major,
		resp.code.minor, resp.code.hotfix, resp.code.build);
	return;

fail:
	printk(BIOS_DEBUG, "ME: Version: Unavailable\n");
}
BOOT_STATE_INIT_ENTRY(BS_DEV_ENABLE, BS_ON_EXIT, print_me_version, NULL);

void dump_me_status(void *unused)
{
	union me_hfsts1 hfsts1;
	union me_hfsts2 hfsts2;
	union me_hfsts3 hfsts3;
	union me_hfsts4 hfsts4;
	union me_hfsts5 hfsts5;
	union me_hfsts6 hfsts6;

	if (!is_cse_enabled())
		return;

	hfsts1.data = me_read_config32(PCI_ME_HFSTS1);
	hfsts2.raw = me_read_config32(PCI_ME_HFSTS2);
	hfsts3.data = me_read_config32(PCI_ME_HFSTS3);
	hfsts4.raw = me_read_config32(PCI_ME_HFSTS4);
	hfsts5.raw = me_read_config32(PCI_ME_HFSTS5);
	hfsts6.raw = me_read_config32(PCI_ME_HFSTS6);

	printk(BIOS_DEBUG, "ME: HFSTS1                  : 0x%08X\n",
		hfsts1.data);
	printk(BIOS_DEBUG, "ME: HFSTS2                  : 0x%08X\n",
		hfsts2.raw);
	printk(BIOS_DEBUG, "ME: HFSTS3                  : 0x%08X\n",
		hfsts3.data);
	printk(BIOS_DEBUG, "ME: HFSTS4                  : 0x%08X\n",
		hfsts4.raw);
	printk(BIOS_DEBUG, "ME: HFSTS5                  : 0x%08X\n",
		hfsts5.raw);
	printk(BIOS_DEBUG, "ME: HFSTS6                  : 0x%08X\n",
		hfsts6.raw);

	printk(BIOS_DEBUG, "ME: Manufacturing Mode      : %s\n",
		hfsts1.fields.mfg_mode ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: FW Partition Table      : %s\n",
		hfsts1.fields.fpt_bad ? "BAD" : "OK");
	printk(BIOS_DEBUG, "ME: Bringup Loader Failure  : %s\n",
		hfsts1.fields.ft_bup_ld_flr ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: Firmware Init Complete  : %s\n",
		hfsts1.fields.fw_init_complete ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: Boot Options Present    : %s\n",
		hfsts1.fields.boot_options_present ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: Update In Progress      : %s\n",
		hfsts1.fields.update_in_progress ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: D0i3 Support            : %s\n",
		hfsts1.fields.d0i3_support_valid ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: Low Power State Enabled : %s\n",
		hfsts2.fields.low_power_state ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: CPU Replaced            : %s\n",
		hfsts2.fields.cpu_replaced  ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: CPU Replacement Valid   : %s\n",
		hfsts2.fields.cpu_replaced_valid ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: Current Working State   : %u\n",
		hfsts1.fields.working_state);
	printk(BIOS_DEBUG, "ME: Current Operation State : %u\n",
		hfsts1.fields.operation_state);
	printk(BIOS_DEBUG, "ME: Current Operation Mode  : %u\n",
		hfsts1.fields.operation_mode);
	printk(BIOS_DEBUG, "ME: Error Code              : %u\n",
		hfsts1.fields.error_code);
	printk(BIOS_DEBUG, "ME: CPU Debug Disabled      : %s\n",
		hfsts6.fields.cpu_debug_disable ? "YES" : "NO");
	printk(BIOS_DEBUG, "ME: TXT Support             : %s\n",
		hfsts6.fields.txt_support ? "YES" : "NO");
}

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME_CHECK, BS_ON_EXIT, dump_me_status, NULL);
