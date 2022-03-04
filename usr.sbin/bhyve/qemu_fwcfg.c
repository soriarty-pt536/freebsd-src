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
#include <sys/endian.h>
#include <sys/queue.h>

#include <machine/vmm.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "acpi_device.h"
#include "inout.h"
#include "qemu_fwcfg.h"

#define QEMU_FWCFG_ACPI_DEVICE_NAME "FWCF"
#define QEMU_FWCFG_ACPI_HARDWARE_ID "QEMU0002"

#define QEMU_FWCFG_SELECTOR_PORT_NUMBER 0x510
#define QEMU_FWCFG_SELECTOR_PORT_SIZE 1
#define QEMU_FWCFG_SELECTOR_PORT_FLAGS IOPORT_F_INOUT
#define QEMU_FWCFG_DATA_PORT_NUMBER 0x511
#define QEMU_FWCFG_DATA_PORT_SIZE 1
#define QEMU_FWCFG_DATA_PORT_FLAGS \
	IOPORT_F_INOUT /* QEMU v2.4+ ignores writes */

#define QEMU_FWCFG_ARCHITECTURE_MASK 0x0001
#define QEMU_FWCFG_INDEX_MASK 0x3FFF

#define QEMU_FWCFG_SELECT_READ 0
#define QEMU_FWCFG_SELECT_WRITE 1

#define QEMU_FWCFG_ARCHITECTURE_GENERIC 0
#define QEMU_FWCFG_ARCHITECTURE_SPECIFIC 1

#define QEMU_FWCFG_INDEX_SIGNATURE 0x00
#define QEMU_FWCFG_INDEX_ID 0x01
#define QEMU_FWCFG_INDEX_FILE_DIR 0x19

#define QEMU_FWCFG_FIRST_FILE_INDEX 0x20

#define QEMU_FWCFG_MIN_FILES 10

#pragma pack(1)

union qemu_fwcfg_selector {
	struct {
		uint16_t index : 14;
		uint16_t writeable : 1;
		/*
		 * 0 = generic  | for all architectures
		 * 1 = specific | only for current architecture
		 */
		uint16_t architecture : 1;
	};
	uint16_t bits;
};

struct qemu_fwcfg_signature {
	uint8_t signature[4];
};

struct qemu_fwcfg_id {
	uint32_t interface : 1; /* always set */
	uint32_t DMA : 1;
	uint32_t reserved : 30;
};

struct qemu_fwcfg_file {
	uint32_t be_size;
	uint16_t be_selector;
	uint16_t reserved;
	uint8_t name[QEMU_FWCFG_MAX_NAME];
};

struct qemu_fwcfg_directory {
	uint32_t be_count;
	struct qemu_fwcfg_file files[0];
};

struct qemu_fwcfg_softc {
	struct acpi_device *acpi_dev;

	uint32_t data_offset;
	union qemu_fwcfg_selector selector;
	struct qemu_fwcfg_item items[QEMU_FWCFG_MAX_ARCHS]
				    [QEMU_FWCFG_MAX_ENTRIES];
	struct qemu_fwcfg_directory *directory;
};

#pragma pack()

static struct qemu_fwcfg_softc sc;

struct qemu_fwcfg_user_file {
	STAILQ_ENTRY(qemu_fwcfg_user_file) chain;
	uint8_t name[QEMU_FWCFG_MAX_NAME];
	uint32_t size;
	void *data;
};
STAILQ_HEAD(qemu_fwcfg_user_file_list,
    qemu_fwcfg_user_file) user_files = STAILQ_HEAD_INITIALIZER(user_files);

static int
qemu_fwcfg_selector_port_handler(struct vmctx *const ctx, const int vcpu,
    const int in, const int port, const int bytes, uint32_t *const eax,
    void *const arg)
{
	if (in) {
		*eax = *(uint16_t *)&sc.selector;
		return (0);
	}

	sc.data_offset = 0;
	sc.selector.bits = *eax;

	return (0);
}

static int
qemu_fwcfg_data_port_handler(struct vmctx *const ctx, const int vcpu,
    const int in, const int port, const int bytes, uint32_t *const eax,
    void *const arg)
{
	if (!in) {
		warnx("%s: Writes to qemu fwcfg data port aren't allowed",
		    __func__);
		return (-1);
	}

	/* get fwcfg item */
	struct qemu_fwcfg_item *const item =
	    &sc.items[sc.selector.architecture][sc.selector.index];
	if (item->data == NULL) {
		warnx(
		    "%s: qemu fwcfg item doesn't exist (architecture %s index 0x%x)",
		    __func__, sc.selector.architecture ? "specific" : "generic",
		    sc.selector.index);
		*eax = 0x00;
		return (0);
	} else if (sc.data_offset >= item->size) {
		warnx(
		    "%s: qemu fwcfg item read exceeds size (architecture %s index 0x%x size 0x%x offset 0x%x)",
		    __func__, sc.selector.architecture ? "specific" : "generic",
		    sc.selector.index, item->size, sc.data_offset);
		*eax = 0x00;
		return (0);
	}

	/* return item data */
	*eax = item->data[sc.data_offset];
	sc.data_offset++;

	return (0);
}

static int
qemu_fwcfg_add_item(const uint16_t architecture, const uint16_t index,
    const uint32_t size, void *const data)
{
	/* truncate architecture and index to their desired size */
	const uint16_t arch = architecture & QEMU_FWCFG_ARCHITECTURE_MASK;
	const uint16_t idx = index & QEMU_FWCFG_INDEX_MASK;

	/* get pointer to item specified by selector */
	struct qemu_fwcfg_item *const fwcfg_item = &sc.items[arch][idx];

	/* check if item is already used */
	if (fwcfg_item->data != NULL) {
		warnx("%s: qemu fwcfg item exists (architecture %s index 0x%x)",
		    __func__, arch ? "specific" : "generic", idx);
		return (-1);
	}

	/* save data of the item */
	fwcfg_item->size = size;
	fwcfg_item->data = data;

	return (0);
}

static int
qemu_fwcfg_add_item_file_dir()
{
	/* alloc directory */
	const size_t size = sizeof(struct qemu_fwcfg_directory) +
	    QEMU_FWCFG_MIN_FILES * sizeof(struct qemu_fwcfg_file);
	struct qemu_fwcfg_directory *const fwcfg_directory = calloc(1, size);
	if (fwcfg_directory == NULL) {
		return (-ENOMEM);
	}

	/* init directory */
	sc.directory = fwcfg_directory;

	/* add directory */
	return qemu_fwcfg_add_item(QEMU_FWCFG_ARCHITECTURE_GENERIC,
	    QEMU_FWCFG_INDEX_FILE_DIR, sizeof(struct qemu_fwcfg_directory), (uint8_t *)sc.directory);
}

static int
qemu_fwcfg_add_item_id()
{
	/* alloc id */
	struct qemu_fwcfg_id *const fwcfg_id = calloc(1,
	    sizeof(struct qemu_fwcfg_id));
	if (fwcfg_id == NULL) {
		return (-ENOMEM);
	}

	/* init id */
	fwcfg_id->interface = 1;
	fwcfg_id->DMA = 0;

	/*
	 * QEMU specifies ID as little endian.
	 * Convert fwcfg_id to little endian.
	 */
	uint32_t *const le_fwcfg_id_ptr = (uint32_t *)fwcfg_id;
	*le_fwcfg_id_ptr = htole32(*le_fwcfg_id_ptr);

	/* add id */
	return qemu_fwcfg_add_item(QEMU_FWCFG_ARCHITECTURE_GENERIC,
	    QEMU_FWCFG_INDEX_ID, sizeof(struct qemu_fwcfg_id),
	    (uint8_t *)fwcfg_id);
}

static int
qemu_fwcfg_add_item_signature()
{
	/* alloc signature */
	struct qemu_fwcfg_signature *const fwcfg_signature = calloc(1,
	    sizeof(struct qemu_fwcfg_signature));
	if (fwcfg_signature == NULL) {
		return (-ENOMEM);
	}

	/* init signature */
	fwcfg_signature->signature[0] = 'Q';
	fwcfg_signature->signature[1] = 'E';
	fwcfg_signature->signature[2] = 'M';
	fwcfg_signature->signature[3] = 'U';

	/* add signature */
	return qemu_fwcfg_add_item(QEMU_FWCFG_ARCHITECTURE_GENERIC,
	    QEMU_FWCFG_INDEX_SIGNATURE, sizeof(struct qemu_fwcfg_signature),
	    (uint8_t *)fwcfg_signature);
}

static int
qemu_fwcfg_register_port(const char *const name, const int port, const int size,
    const int flags, const inout_func_t handler)
{
	struct inout_port iop;

	bzero(&iop, sizeof(iop));
	iop.name = name;
	iop.port = port;
	iop.size = size;
	iop.flags = flags;
	iop.handler = handler;

	return register_inout(&iop);
}

int
qemu_fwcfg_add_file(const uint8_t name[QEMU_FWCFG_MAX_NAME], const uint32_t size,
    void *const data)
{
	/*
	 * QEMU specifies count as big endian.
	 * Convert it to host endian to work with it.
	 */
	const uint32_t count = be32toh(sc.directory->be_count) + 1;

	/* add file to items list */
	const uint32_t index = QEMU_FWCFG_FIRST_FILE_INDEX + count - 1;
	const int error = qemu_fwcfg_add_item(QEMU_FWCFG_ARCHITECTURE_GENERIC,
	    index, size, data);
	if (error != 0) {
		return (error);
	}

	/*
	 * files should be sorted alphabetical, get index for new file
	 */
	uint32_t file_index;
	for (file_index = 0; file_index < count - 1; ++file_index) {
		if (strcmp(name, sc.directory->files[file_index].name) < 0)
			break;
	}

	if (count > QEMU_FWCFG_MIN_FILES) {
		/* alloc new file directory */
		const uint64_t new_size = sizeof(struct qemu_fwcfg_directory) +
		    count * sizeof(struct qemu_fwcfg_file);
		struct qemu_fwcfg_directory *const new_directory = calloc(1,
		    new_size);
		if (new_directory == NULL) {
			warnx(
			    "%s: Unable to allocate a new qemu fwcfg files directory (count %d)",
			    __func__, count);
			return (-ENOMEM);
		}

		/* copy files below file_index to new directory */
		memcpy(new_directory->files, sc.directory->files,
		    file_index * sizeof(struct qemu_fwcfg_file));

		/* copy files behind file_index to directory */
		memcpy(&new_directory->files[file_index + 1],
		    &sc.directory->files[file_index],
		    (count - file_index) * sizeof(struct qemu_fwcfg_file));

		/* free old directory */
		free(sc.directory);

		/* set directory pointer to new directory */
		sc.directory = new_directory;

		/* adjust directory pointer */
		sc.items[0][QEMU_FWCFG_INDEX_FILE_DIR].data = (uint8_t *)
								  sc.directory;
	} else {
		/* shift files behind file_index */
		for (uint32_t i = QEMU_FWCFG_MIN_FILES - 1; i > file_index; --i) {
			memcpy(&sc.directory->files[i],
			    &sc.directory->files[i - 1],
			    sizeof(struct qemu_fwcfg_file));
		}
	}

	/*
	 * QEMU specifies count, size and index as big endian.
	 * Save these values in big endian to simplify guest reads of these
	 * values.
	 */
	sc.directory->be_count = htobe32(count);
	sc.directory->files[file_index].be_size = htobe32(size);
	sc.directory->files[file_index].be_selector = htobe16(index);
	strcpy(sc.directory->files[file_index].name, name);

	/* set new size for the fwcfg_file_directory */
	sc.items[0][QEMU_FWCFG_INDEX_FILE_DIR].size =
	    sizeof(struct qemu_fwcfg_directory) +
	    count * sizeof(struct qemu_fwcfg_file);

	return (0);
}

static int
qemu_fwcfg_add_user_files()
{
	const struct qemu_fwcfg_user_file *fwcfg_file;
	STAILQ_FOREACH (fwcfg_file, &user_files, chain) {
		const int error = qemu_fwcfg_add_file(fwcfg_file->name,
		    fwcfg_file->size, fwcfg_file->data);
		if (error)
			return (error);
	}

	return (0);
}

int
qemu_fwcfg_init(struct vmctx *const ctx)
{
	int error;

	error = acpi_device_create(&sc.acpi_dev, ctx,
	    QEMU_FWCFG_ACPI_DEVICE_NAME, QEMU_FWCFG_ACPI_HARDWARE_ID);
	if (error) {
		warnx("%s: failed to create ACPI device for QEMU FwCfg",
		    __func__);
		goto done;
	}

	error = acpi_device_add_res_fixed_ioport(sc.acpi_dev,
	    QEMU_FWCFG_SELECTOR_PORT_NUMBER, 2);
	if (error) {
		warnx("%s: failed to add fixed IO port for QEMU FwCfg",
		    __func__);
		goto done;
	}

	/* add common fwcfg items */
	if ((error = qemu_fwcfg_add_item_signature()) != 0) {
		warnx("%s: Unable to add signature item", __func__);
		goto done;
	}
	if ((error = qemu_fwcfg_add_item_id()) != 0) {
		warnx("%s: Unable to add id item", __func__);
		goto done;
	}
	if ((error = qemu_fwcfg_add_item_file_dir()) != 0) {
		warnx("%s: Unable to add file_dir item", __func__);
		goto done;
	}

	/* add handlers for fwcfg ports */
	if ((error = qemu_fwcfg_register_port("qemu_fwcfg_selector",
		 QEMU_FWCFG_SELECTOR_PORT_NUMBER, QEMU_FWCFG_SELECTOR_PORT_SIZE,
		 QEMU_FWCFG_SELECTOR_PORT_FLAGS,
		 qemu_fwcfg_selector_port_handler)) != 0) {
		warnx("%s: Unable to register qemu fwcfg selector port 0x%x",
		    __func__, QEMU_FWCFG_SELECTOR_PORT_NUMBER);
		goto done;
	}
	if ((error = qemu_fwcfg_register_port("qemu_fwcfg_data",
		 QEMU_FWCFG_DATA_PORT_NUMBER, QEMU_FWCFG_DATA_PORT_SIZE,
		 QEMU_FWCFG_DATA_PORT_FLAGS, qemu_fwcfg_data_port_handler)) !=
	    0) {
		warnx("%s: Unable to register qemu fwcfg data port 0x%x",
		    __func__, QEMU_FWCFG_DATA_PORT_NUMBER);
		goto done;
	}

	if ((error = qemu_fwcfg_add_user_files()) != 0) {
		warnx("%s: Unable to add user files", __func__);
		goto done;
	}

done:
	if (error) {
		acpi_device_destroy(sc.acpi_dev);
	}

	return (error);
}

static void
qemu_fwcfg_usage(const char *opt)
{
	warnx("Invalid fw_cfg option \"%s\"", opt);
	warnx("-f [name=]<name>,(string|file)=<value>");
}

/*
 * Parses the cmdline argument for user defined fw_cfg items. The cmdline
 * argument has the format:
 * "-f [name=]<name>,(string|file)=<value>"
 *
 * E.g.: "-f opt/com.page/example,string=Hello"
 */
int
qemu_fwcfg_parse_cmdline_arg(const char *opt)
{
	struct qemu_fwcfg_user_file *const fwcfg_file = malloc(sizeof(*fwcfg_file));
	if (fwcfg_file == NULL) {
		warnx("Unable to allocate fw_cfg_user_file");
		return (-ENOMEM);
	}

	/* get pointer to <name> */
	const char *opt_ptr = opt;
	/* If [name=] is specified, skip it */
	if (strncmp(opt_ptr, "name=", sizeof("name=") - 1) == 0) {
		opt_ptr += sizeof("name=") - 1;
	}

	/* get the end of <name> */
	const char *opt_end = strchr(opt_ptr, ',');
	if (opt_end == NULL) {
		qemu_fwcfg_usage(opt);
		return (-1);
	}

	/* check if <name> is too long */
	if (opt_end - opt_ptr > QEMU_FWCFG_MAX_NAME) {
		warnx("fw_cfg name too long: \"%s\"", opt);
		return (-1);
	}

	/* save <name> */
	strncpy(fwcfg_file->name, opt_ptr, opt_end - opt_ptr);

	/* set opt_ptr and opt_end to <value> */
	opt_ptr = opt_end + 1;
	opt_end = opt_ptr + strlen(opt_ptr);

	if (strncmp(opt_ptr, "string=", sizeof("string=") - 1) == 0) {
		opt_ptr += sizeof("string=") - 1;
		fwcfg_file->data = strdup(opt_ptr);
		if (fwcfg_file->data == NULL) {
			warnx(" Can't duplicate fw_cfg_user_file string \"%s\"",
			    opt_ptr);
			return (-ENOMEM);
		}
		fwcfg_file->size = strlen(opt_ptr) + 1;

	} else if (strncmp(opt_ptr, "file=", sizeof("file=") - 1) == 0) {
		opt_ptr += sizeof("file=") - 1;

		/* open file */
		const int fd = open(opt_ptr, O_RDONLY);
		if (fd < 0) {
			warnx("Can't open fw_cfg_user_file file \"%s\"",
			    opt_ptr);
			return (-1);
		}

		/* get file size */
		const uint64_t size = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);

		/* read file */
		fwcfg_file->data = malloc(size);
		if (fwcfg_file->data == NULL) {
			warnx(
			    "Can't allocate fw_cfg_user_file file \"%s\" (size: 0x%16lx)",
			    opt_ptr, size);
			close(fd);
			return (-ENOMEM);
		}
		fwcfg_file->size = read(fd, fwcfg_file->data, size);

		close(fd);

	} else {
		qemu_fwcfg_usage(opt);
		return (-1);
	}

	STAILQ_INSERT_TAIL(&user_files, fwcfg_file, chain);

	return (0);
}
