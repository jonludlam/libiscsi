/*
   Copyright (C) 2010 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __scsi_lowlevel_h__
#define __scsi_lowlevel_h__

#define SCSI_CDB_MAX_SIZE			16

enum scsi_opcode {
	SCSI_OPCODE_TESTUNITREADY      = 0x00,
	SCSI_OPCODE_READ6              = 0x08,
	SCSI_OPCODE_INQUIRY            = 0x12,
	SCSI_OPCODE_MODESENSE6         = 0x1a,
	SCSI_OPCODE_READCAPACITY10     = 0x25,
	SCSI_OPCODE_READ10             = 0x28,
	SCSI_OPCODE_WRITE10            = 0x2A,
	SCSI_OPCODE_SYNCHRONIZECACHE10 = 0x35,
	SCSI_OPCODE_REPORTLUNS         = 0xA0
};

/* sense keys */
enum scsi_sense_key {
	SCSI_SENSE_NO_SENSE            = 0x00,
	SCSI_SENSE_RECOVERED_ERROR     = 0x01,
	SCSI_SENSE_NOT_READY           = 0x02,
	SCSI_SENSE_MEDIUM_ERROR        = 0x03,
	SCSI_SENSE_HARDWARE_ERROR      = 0x04,
	SCSI_SENSE_ILLEGAL_REQUEST     = 0x05,
	SCSI_SENSE_UNIT_ATTENTION      = 0x06,
	SCSI_SENSE_DATA_PROTECTION     = 0x07,
	SCSI_SENSE_BLANK_CHECK         = 0x08,
	SCSI_SENSE_VENDOR_SPECIFIC     = 0x09,
	SCSI_SENSE_COPY_ABORTED        = 0x0a,
	SCSI_SENSE_COMMAND_ABORTED     = 0x0b,
	SCSI_SENSE_OBSOLETE_ERROR_CODE = 0x0c,
	SCSI_SENSE_OVERFLOW_COMMAND    = 0x0d,
	SCSI_SENSE_MISCOMPARE          = 0x0e
};

const char *scsi_sense_key_str(int key);

/* ascq */
#define SCSI_SENSE_ASCQ_INVALID_OPERATION_CODE		0x2000
#define SCSI_SENSE_ASCQ_LBA_OUT_OF_RANGE		0x2100
#define SCSI_SENSE_ASCQ_INVALID_FIELD_IN_CDB		0x2400
#define SCSI_SENSE_ASCQ_LOGICAL_UNIT_NOT_SUPPORTED	0x2500
#define SCSI_SENSE_ASCQ_BUS_RESET			0x2900

const char *scsi_sense_ascq_str(int ascq);


enum scsi_xfer_dir {
	SCSI_XFER_NONE  = 0,
	SCSI_XFER_READ  = 1,
	SCSI_XFER_WRITE = 2
};

struct scsi_reportluns_params {
	int report_type;
};
struct scsi_read6_params {
	uint32_t lba;
	uint32_t num_blocks;
};
struct scsi_read10_params {
	uint32_t lba;
	uint32_t num_blocks;
};
struct scsi_write10_params {
	uint32_t lba;
	uint32_t num_blocks;
};
struct scsi_readcapacity10_params {
	int lba;
	int pmi;
};
struct scsi_inquiry_params {
	int evpd;
	int page_code;
};
struct scsi_modesense6_params {
	int dbd;
	int pc;
	int page_code;
	int sub_page_code;
};

struct scsi_sense {
	unsigned char       error_type;
	enum scsi_sense_key key;
	int                 ascq;
};

struct scsi_data {
	int            size;
	unsigned char *data;
};

struct scsi_allocated_memory {
	struct scsi_allocated_memory *next;
	void                         *ptr;
};

enum scsi_residual {
	SCSI_RESIDUAL_NO_RESIDUAL = 0,
	SCSI_RESIDUAL_UNDERFLOW,
	SCSI_RESIDUAL_OVERFLOW
};

struct scsi_task {
	int status;

	int cdb_size;
	int xfer_dir;
	int expxferlen;
	unsigned char cdb[SCSI_CDB_MAX_SIZE];
	union {
		struct scsi_read6_params          read6;
		struct scsi_read10_params         read10;
		struct scsi_write10_params        write10;
		struct scsi_readcapacity10_params readcapacity10;
		struct scsi_reportluns_params     reportluns;
		struct scsi_inquiry_params        inquiry;
		struct scsi_modesense6_params     modesense6;
	} params;

	enum scsi_residual residual_status;
	int residual;
	struct scsi_sense sense;
	struct scsi_data datain;
	struct scsi_allocated_memory *mem;

	void *ptr;

	uint32_t itt;
	uint32_t cmdsn;
	uint32_t lun;

	struct scsi_data_buffer *in_buffers;
};

void scsi_free_scsi_task(struct scsi_task *task);
void scsi_set_task_private_ptr(struct scsi_task *task, void *ptr);
void *scsi_get_task_private_ptr(struct scsi_task *task);

/*
 * TESTUNITREADY
 */
struct scsi_task *scsi_cdb_testunitready(void);


/*
 * REPORTLUNS
 */
#define SCSI_REPORTLUNS_REPORT_ALL_LUNS				0x00
#define SCSI_REPORTLUNS_REPORT_WELL_KNOWN_ONLY			0x01
#define SCSI_REPORTLUNS_REPORT_AVAILABLE_LUNS_ONLY		0x02

struct scsi_reportluns_list {
	uint32_t num;
	uint16_t luns[0];
};

struct scsi_task *scsi_reportluns_cdb(int report_type, int alloc_len);

/*
 * READCAPACITY10
 */
struct scsi_readcapacity10 {
	uint32_t lba;
	uint32_t block_size;
};
struct scsi_task *scsi_cdb_readcapacity10(int lba, int pmi);


/*
 * INQUIRY
 */
enum scsi_inquiry_peripheral_qualifier {
	SCSI_INQUIRY_PERIPHERAL_QUALIFIER_CONNECTED     = 0x00,
	SCSI_INQUIRY_PERIPHERAL_QUALIFIER_DISCONNECTED  = 0x01,
	SCSI_INQUIRY_PERIPHERAL_QUALIFIER_NOT_SUPPORTED = 0x03
};

const char *scsi_devqualifier_to_str(
			enum scsi_inquiry_peripheral_qualifier qualifier);

enum scsi_inquiry_peripheral_device_type {
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS            = 0x00,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SEQUENTIAL_ACCESS        = 0x01,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_PRINTER                  = 0x02,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_PROCESSOR                = 0x03,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_WRITE_ONCE               = 0x04,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_MMC                      = 0x05,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SCANNER                  = 0x06,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_OPTICAL_MEMORY           = 0x07,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_MEDIA_CHANGER            = 0x08,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_COMMUNICATIONS           = 0x09,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_STORAGE_ARRAY_CONTROLLER = 0x0c,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_ENCLOSURE_SERVICES       = 0x0d,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SIMPLIFIED_DIRECT_ACCESS = 0x0e,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_OPTICAL_CARD_READER      = 0x0f,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_BRIDGE_CONTROLLER        = 0x10,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_OSD                      = 0x11,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_AUTOMATION               = 0x12,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_SEQURITY_MANAGER         = 0x13,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_WELL_KNOWN_LUN           = 0x1e,
	SCSI_INQUIRY_PERIPHERAL_DEVICE_TYPE_UNKNOWN                  = 0x1f
};

const char *scsi_devtype_to_str(enum scsi_inquiry_peripheral_device_type type);

enum scsi_version {
	SCSI_VERSION_SPC  = 0x03,
	SCSI_VERSION_SPC2 = 0x04,
	SCSI_VERSION_SPC3 = 0x05
};

const char *scsi_version_to_str(enum scsi_version version);

enum scsi_inquiry_tpgs {
	SCSI_INQUIRY_TPGS_NO_SUPPORT            = 0x00,
	SCSI_INQUIRY_TPGS_IMPLICIT              = 0x01,
	SCSI_INQUIRY_TPGS_EXPLICIT              = 0x02,
	SCSI_INQUIRY_TPGS_IMPLICIT_AND_EXPLICIT = 0x03
};

struct scsi_inquiry_standard {
	enum scsi_inquiry_peripheral_qualifier periperal_qualifier;
	enum scsi_inquiry_peripheral_device_type periperal_device_type;
	int rmb;
	int version;
	int normaca;
	int hisup;
	int response_data_format;

	int sccs;
	int acc;
	int tpgs;
	int threepc;
	int protect;

	int encserv;
	int multip;
	int addr16;
	int wbus16;
	int sync;
	int cmdque;

	int clocking;
	int qas;
	int ius;

	char vendor_identification[8+1];
	char product_identification[16+1];
	char product_revision_level[4+1];
};

enum scsi_inquiry_pagecode {
	SCSI_INQUIRY_PAGECODE_SUPPORTED_VPD_PAGES          = 0x00,
	SCSI_INQUIRY_PAGECODE_UNIT_SERIAL_NUMBER           = 0x80,
	SCSI_INQUIRY_PAGECODE_DEVICE_IDENTIFICATION        = 0x83,
	SCSI_INQUIRY_PAGECODE_BLOCK_DEVICE_CHARACTERISTICS = 0xB1
};

const char *scsi_inquiry_pagecode_to_str(int pagecode);

struct scsi_inquiry_supported_pages {
	enum scsi_inquiry_peripheral_qualifier periperal_qualifier;
	enum scsi_inquiry_peripheral_device_type periperal_device_type;
	enum scsi_inquiry_pagecode pagecode;

	int num_pages;
	unsigned char *pages;
};

struct scsi_inquiry_block_device_characteristics {
	enum scsi_inquiry_peripheral_qualifier periperal_qualifier;
	enum scsi_inquiry_peripheral_device_type periperal_device_type;
	enum scsi_inquiry_pagecode pagecode;

	int medium_rotation_rate;
};

struct scsi_task *scsi_cdb_inquiry(int evpd, int page_code, int alloc_len);

struct scsi_inquiry_unit_serial_number {
	enum scsi_inquiry_peripheral_qualifier periperal_qualifier;
	enum scsi_inquiry_peripheral_device_type periperal_device_type;
	enum scsi_inquiry_pagecode pagecode;

	char *usn;
};

enum scsi_protocol_identifier {
	SCSI_PROTOCOL_IDENTIFIER_FIBRE_CHANNEL = 0x00,
	SCSI_PROTOCOL_IDENTIFIER_PARALLEL_SCSI = 0x01,
	SCSI_PROTOCOL_IDENTIFIER_SSA           = 0x02,
	SCSI_PROTOCOL_IDENTIFIER_IEEE_1394     = 0x03,
	SCSI_PROTOCOL_IDENTIFIER_RDMA          = 0x04,
	SCSI_PROTOCOL_IDENTIFIER_ISCSI         = 0x05,
	SCSI_PROTOCOL_IDENTIFIER_SAS           = 0x06,
	SCSI_PROTOCOL_IDENTIFIER_ADT           = 0x07,
	SCSI_PROTOCOL_IDENTIFIER_ATA           = 0x08
};

const char *scsi_protocol_identifier_to_str(int identifier);

enum scsi_codeset {
	SCSI_CODESET_BINARY = 0x01,
	SCSI_CODESET_ASCII  = 0x02,
	SCSI_CODESET_UTF8   = 0x03
};

const char *scsi_codeset_to_str(int codeset);

enum scsi_association {
	SCSI_ASSOCIATION_LOGICAL_UNIT  = 0x00,
	SCSI_ASSOCIATION_TARGET_PORT   = 0x01,
	SCSI_ASSOCIATION_TARGET_DEVICE = 0x02
};

const char *scsi_association_to_str(int association);

enum scsi_designator_type {
	SCSI_DESIGNATOR_TYPE_VENDOR_SPECIFIC             = 0x00,
	SCSI_DESIGNATOR_TYPE_T10_VENDORT_ID              = 0x01,
	SCSI_DESIGNATOR_TYPE_EUI_64                      = 0x02,
	SCSI_DESIGNATOR_TYPE_NAA                         = 0x03,
	SCSI_DESIGNATOR_TYPE_RELATIVE_TARGET_PORT        = 0x04,
	SCSI_DESIGNATOR_TYPE_TARGET_PORT_GROUP           = 0x05,
	SCSI_DESIGNATOR_TYPE_LOGICAL_UNIT_GROUP          = 0x06,
	SCSI_DESIGNATOR_TYPE_MD5_LOGICAL_UNIT_IDENTIFIER = 0x07,
	SCSI_DESIGNATOR_TYPE_SCSI_NAME_STRING            = 0x08
};

const char *scsi_designator_type_to_str(int association);

struct scsi_inquiry_device_designator {
	struct scsi_inquiry_device_designator *next;

	enum scsi_protocol_identifier protocol_identifier;
	enum scsi_codeset code_set;
	int piv;
	enum scsi_association association;
	enum scsi_designator_type designator_type;
	int designator_length;
	char *designator;
};

struct scsi_inquiry_device_identification {
	enum scsi_inquiry_peripheral_qualifier periperal_qualifier;
	enum scsi_inquiry_peripheral_device_type periperal_device_type;
	enum scsi_inquiry_pagecode pagecode;

	struct scsi_inquiry_device_designator *designators;
};

/*
 * MODESENSE6
 */
enum scsi_modesense_page_control {
	SCSI_MODESENSE_PC_CURRENT    = 0x00,
	SCSI_MODESENSE_PC_CHANGEABLE = 0x01,
	SCSI_MODESENSE_PC_DEFAULT    = 0x02,
	SCSI_MODESENSE_PC_SAVED      = 0x03
};

enum scsi_modesense_page_code {
	SCSI_MODESENSE_PAGECODE_RETURN_ALL_PAGES = 0x3f
};

struct scsi_task *scsi_cdb_modesense6(int dbd,
			enum scsi_modesense_page_control pc,
			enum scsi_modesense_page_code page_code,
			int sub_page_code,
			unsigned char alloc_len);




int scsi_datain_getfullsize(struct scsi_task *task);
void *scsi_datain_unmarshall(struct scsi_task *task);

struct scsi_task *scsi_cdb_read6(uint32_t lba, uint32_t xferlen, int blocksize);
struct scsi_task *scsi_cdb_read10(uint32_t lba, uint32_t xferlen, int blocksize);
struct scsi_task *scsi_cdb_write10(uint32_t lba, uint32_t xferlen, int fua, int fuanv,
			int blocksize);

struct scsi_task *scsi_cdb_synchronizecache10(int lba, int num_blocks,
			int syncnv, int immed);

#endif /* __scsi_lowlevel_h__ */

