/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Beckhoff Automation GmbH & Co. KG
 * Author: Corvin Köhne <c.koehne@beckhoff.com>
 */

#pragma once

#include <contrib/dev/acpica/include/acpi.h>

struct vmctx;

struct acpi_device;

/**
 * Creates an ACPI device.
 *
 * @param[out] new_dev Returns the newly create ACPI device.
 * @param[in]  vm_ctx  VM context the ACPI device is created in.
 * @param[in]  name    Name of the ACPI device. Should always be a NULL
 *                     terminated string.
 * @param[in]  hid     Hardware ID of the ACPI device. Should always be a NULL
 *                     terminated string.
 */
int acpi_device_create(struct acpi_device **const new_dev,
    struct vmctx *const vm_ctx, const char *const name, const char *const hid);
void acpi_device_destroy(struct acpi_device *const dev);
