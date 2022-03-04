/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Beckhoff Automation GmbH & Co. KG
 * Author: Corvin Köhne <c.koehne@beckhoff.com>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/queue.h>

#include <machine/vmm.h>

#include <err.h>
#include <errno.h>
#include <vmmapi.h>

#include "acpi.h"
#include "acpi_device.h"

/**
 * List entry to enumerate all resources used by an ACPI device.
 *
 * @param chain Used to chain multiple elements together.
 * @param type  Type of the ACPI resource.
 * @param data  Data of the ACPI resource.
 */
struct acpi_resource_list_entry {
	SLIST_ENTRY(acpi_resource_list_entry) chain;
	UINT32 type;
	ACPI_RESOURCE_DATA data;
};

/**
 * Holds information about an ACPI device.
 *
 * @param vm_ctx VM context the ACPI device was created in.
 * @param name   Name of the ACPI device.
 * @param hid    Hardware ID of the ACPI device.
 * @param crs    Current resources used by the ACPI device.
 */
struct acpi_device {
	struct vmctx *vm_ctx;
	const char *name;
	const char *hid;
	SLIST_HEAD(acpi_resource_list, acpi_resource_list_entry) crs;
};

int
acpi_device_create(struct acpi_device **const new_dev,
    struct vmctx *const vm_ctx, const char *const name, const char *const hid)
{
	if (new_dev == NULL || vm_ctx == NULL || name == NULL || hid == NULL) {
		return (EINVAL);
	}

	struct acpi_device *const dev = calloc(1, sizeof(*dev));
	if (dev == NULL) {
		return (ENOMEM);
	}

	dev->vm_ctx = vm_ctx;
	dev->name = name;
	dev->hid = hid;
	SLIST_INIT(&dev->crs);

	/* current resources always contain an end tag */
	struct acpi_resource_list_entry *const crs_end_tag = calloc(1,
	    sizeof(*crs_end_tag));
	if (crs_end_tag == NULL) {
		acpi_device_destroy(dev);
		return (ENOMEM);
	}
	crs_end_tag->type = ACPI_RESOURCE_TYPE_END_TAG;
	SLIST_INSERT_HEAD(&dev->crs, crs_end_tag, chain);

	*new_dev = dev;

	return (0);
}

void
acpi_device_destroy(struct acpi_device *const dev)
{
	if (dev == NULL) {
		return;
	}

	struct acpi_resource_list_entry *res;
	while (!SLIST_EMPTY(&dev->crs)) {
		res = SLIST_FIRST(&dev->crs);
		SLIST_REMOVE_HEAD(&dev->crs, chain);
		free(res);
	}
}
