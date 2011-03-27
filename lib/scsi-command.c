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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "iscsi.h"
#include "iscsi-private.h"
#include "scsi-lowlevel.h"
#include "slist.h"

struct iscsi_scsi_cbdata {
	struct iscsi_scsi_cbdata *prev, *next;
	iscsi_command_cb          callback;
	void                     *private_data;
	struct scsi_task         *task;
};

void
iscsi_free_scsi_cbdata(struct iscsi_scsi_cbdata *scsi_cbdata)
{
	if (scsi_cbdata == NULL) {
		return;
	}
	if (scsi_cbdata->task != NULL) {
		scsi_cbdata->task = NULL;
	}
	free(scsi_cbdata);
}

static void
iscsi_scsi_response_cb(struct iscsi_context *iscsi, int status,
		       void *command_data, void *private_data)
{
	struct iscsi_scsi_cbdata *scsi_cbdata =
	  (struct iscsi_scsi_cbdata *)private_data;
	struct scsi_task *task = command_data;

	switch (status) {
	case SCSI_STATUS_GOOD:
		scsi_cbdata->callback(iscsi, SCSI_STATUS_GOOD, task,
				      scsi_cbdata->private_data);
		return;
	case SCSI_STATUS_CHECK_CONDITION:
		scsi_cbdata->callback(iscsi, SCSI_STATUS_CHECK_CONDITION, task,
				      scsi_cbdata->private_data);
		return;
	default:
		iscsi_set_error(iscsi, "Cant handle  scsi status %d yet.",
				status);
		scsi_cbdata->callback(iscsi, SCSI_STATUS_ERROR, task,
				      scsi_cbdata->private_data);
	}
}

static int
iscsi_send_data_out(struct iscsi_context *iscsi, struct iscsi_pdu *cmd_pdu,
			   uint32_t ttt, uint32_t offset, uint32_t tot_len)
{
	while (tot_len > 0) {
		uint32_t len = tot_len;
		struct iscsi_pdu *pdu;
		int flags;

		if (len > iscsi->target_max_recv_data_segment_length) {
			len = iscsi->target_max_recv_data_segment_length;
		}

		pdu = iscsi_allocate_pdu_with_itt_flags(iscsi, ISCSI_PDU_DATA_OUT,
				 ISCSI_PDU_NO_PDU,
				 cmd_pdu->itt,
				 ISCSI_PDU_DELETE_WHEN_SENT|ISCSI_PDU_NO_CALLBACK);
		if (pdu == NULL) {
			iscsi_set_error(iscsi, "Out-of-memory, Failed to allocate "
				"scsi data out pdu.");
			SLIST_REMOVE(&iscsi->outqueue, cmd_pdu);
			SLIST_REMOVE(&iscsi->waitpdu, cmd_pdu);
			cmd_pdu->callback(iscsi, SCSI_STATUS_ERROR, NULL,
				     cmd_pdu->private_data);
			iscsi_free_pdu(iscsi, cmd_pdu);
			return -1;

		}

		if (tot_len == len) {
			flags = ISCSI_PDU_SCSI_FINAL;
		} else {
			flags = 0;
		}

		/* flags */
		iscsi_pdu_set_pduflags(pdu, flags);

		/* lun */
		iscsi_pdu_set_lun(pdu, cmd_pdu->lun);

		/* ttt */
		iscsi_pdu_set_ttt(pdu, ttt);

		/* exp statsn */
		iscsi_pdu_set_expstatsn(pdu, iscsi->statsn+1);

		/* data sn */
		iscsi_pdu_set_datasn(pdu, 0);

		/* buffer offset */
		iscsi_pdu_set_bufferoffset(pdu, offset);

		if (iscsi_pdu_add_data(iscsi, pdu, cmd_pdu->nidata.data + offset, len)
		    != 0) {
		    	iscsi_set_error(iscsi, "Out-of-memory: Failed to "
				"add outdata to the pdu.");
			SLIST_REMOVE(&iscsi->outqueue, cmd_pdu);
			SLIST_REMOVE(&iscsi->waitpdu, cmd_pdu);
			cmd_pdu->callback(iscsi, SCSI_STATUS_ERROR, NULL,
				     cmd_pdu->private_data);
			iscsi_free_pdu(iscsi, cmd_pdu);
			iscsi_free_pdu(iscsi, pdu);
			return -1;
		}

		pdu->callback     = cmd_pdu->callback;
		pdu->private_data = cmd_pdu->private_data;;

		if (iscsi_queue_pdu(iscsi, pdu) != 0) {
			iscsi_set_error(iscsi, "Out-of-memory: failed to queue iscsi "
				"scsi pdu.");
			SLIST_REMOVE(&iscsi->outqueue, cmd_pdu);
			SLIST_REMOVE(&iscsi->waitpdu, cmd_pdu);
			cmd_pdu->callback(iscsi, SCSI_STATUS_ERROR, NULL,
				     cmd_pdu->private_data);
			iscsi_free_pdu(iscsi, cmd_pdu);
			iscsi_free_pdu(iscsi, pdu);
			return -1;
		}

		tot_len -= len;
		offset  += len;
	}
	return 0;
}

int
iscsi_scsi_command_async(struct iscsi_context *iscsi, int lun,
			    struct scsi_task *task, iscsi_command_cb cb,
			    struct iscsi_data *d, void *private_data)
{
	struct iscsi_pdu *pdu;
	struct iscsi_scsi_cbdata *scsi_cbdata;
	struct iscsi_data data;
	uint32_t offset = 0;

	int flags;

	data.data = (d != NULL) ? d->data : NULL;
	data.size = (d != NULL) ? d->size : 0;

	if (iscsi->session_type != ISCSI_SESSION_NORMAL) {
		iscsi_set_error(iscsi, "Trying to send command on "
				"discovery session.");
		return -1;
	}

	if (iscsi->is_loggedin == 0) {
		iscsi_set_error(iscsi, "Trying to send command while "
				"not logged in.");
		return -1;
	}

	scsi_cbdata = malloc(sizeof(struct iscsi_scsi_cbdata));
	if (scsi_cbdata == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: failed to allocate "
				"scsi cbdata.");
		return -1;
	}
	memset(scsi_cbdata, 0, sizeof(struct iscsi_scsi_cbdata));
	scsi_cbdata->task         = task;
	scsi_cbdata->callback     = cb;
	scsi_cbdata->private_data = private_data;

	scsi_set_task_private_ptr(task, scsi_cbdata);

	pdu = iscsi_allocate_pdu(iscsi, ISCSI_PDU_SCSI_REQUEST,
				 ISCSI_PDU_SCSI_RESPONSE);
	if (pdu == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory, Failed to allocate "
				"scsi pdu.");
		iscsi_free_scsi_cbdata(scsi_cbdata);
		return -1;
	}
	pdu->scsi_cbdata = scsi_cbdata;

	/* flags */
	flags = ISCSI_PDU_SCSI_FINAL|ISCSI_PDU_SCSI_ATTR_SIMPLE;
	switch (task->xfer_dir) {
	case SCSI_XFER_NONE:
		break;
	case SCSI_XFER_READ:
		flags |= ISCSI_PDU_SCSI_READ;
		break;
	case SCSI_XFER_WRITE:
		flags |= ISCSI_PDU_SCSI_WRITE;
		if (data.size == 0) {
			iscsi_set_error(iscsi, "DATA-OUT command but data "
					"== NULL.");
			iscsi_free_pdu(iscsi, pdu);
			return -1;
		}
		if (data.size != task->expxferlen) {
			iscsi_set_error(iscsi, "Data size:%d is not same as "
					"expected data transfer "
					"length:%d.", data.size,
					task->expxferlen);
			iscsi_free_pdu(iscsi, pdu);
			return -1;
		}

		/* Assume all data is non-immediate data */
		pdu->nidata.data = data.data;
		pdu->nidata.size = data.size;

		/* Are we allowed to send immediate data ? */
		if (iscsi->use_immediate_data == ISCSI_IMMEDIATE_DATA_YES) {
			uint32_t len = data.size;

			if (len > iscsi->target_max_recv_data_segment_length) {
				len = iscsi->target_max_recv_data_segment_length;
			}

			if (iscsi_pdu_add_data(iscsi, pdu, data.data, len)
			    != 0) {
			    	iscsi_set_error(iscsi, "Out-of-memory: Failed to "
						"add outdata to the pdu.");
				iscsi_free_pdu(iscsi, pdu);
				return -1;
			}
			offset = len;

			if (len == (uint32_t)data.size) {
				/* We managed to send it all as immediate data, so there is no non-immediate data left */
				pdu->nidata.data = NULL;
				pdu->nidata.size = 0;

			}
		}

		if (pdu->nidata.size > 0 && iscsi->use_initial_r2t == ISCSI_INITIAL_R2T_NO) {
			/* We have more data to send, and we are allowed to send
			 * unsolicited data, so dont flag this PDU as final.
			 */
			flags &= ~ISCSI_PDU_SCSI_FINAL;
		}
		break;
	}
	iscsi_pdu_set_pduflags(pdu, flags);

	/* lun */
	iscsi_pdu_set_lun(pdu, lun);
	pdu->lun = lun;

	/* expxferlen */
	iscsi_pdu_set_expxferlen(pdu, task->expxferlen);

	/* cmdsn */
	iscsi_pdu_set_cmdsn(pdu, iscsi->cmdsn);
	pdu->cmdsn = iscsi->cmdsn;
	iscsi->cmdsn++;

	/* exp statsn */
	iscsi_pdu_set_expstatsn(pdu, iscsi->statsn+1);
	
	/* cdb */
	iscsi_pdu_set_cdb(pdu, task);

	pdu->callback     = iscsi_scsi_response_cb;
	pdu->private_data = scsi_cbdata;

	if (iscsi_queue_pdu(iscsi, pdu) != 0) {
		iscsi_set_error(iscsi, "Out-of-memory: failed to queue iscsi "
				"scsi pdu.");
		iscsi_free_pdu(iscsi, pdu);
		return -1;
	}

	/* Can we send some unsolicited data ? */
	if (pdu->nidata.size != 0 && iscsi->use_initial_r2t == ISCSI_INITIAL_R2T_NO) {
		uint32_t len = pdu->nidata.size - offset;

		if (len > iscsi->first_burst_length) {
			len = iscsi->first_burst_length;
		}
		iscsi_send_data_out(iscsi, pdu, 0xffffffff, offset, len);
	}

	/* remember cmdsn and itt so we can use task management */
	task->cmdsn = pdu->cmdsn;
	task->itt   = pdu->itt;
	task->lun   = lun;

	return 0;
}


int
iscsi_process_scsi_reply(struct iscsi_context *iscsi, struct iscsi_pdu *pdu,
			 struct iscsi_in_pdu *in)
{
	int statsn, flags, response, status;
	struct iscsi_scsi_cbdata *scsi_cbdata = pdu->scsi_cbdata;
	struct scsi_task *task = scsi_cbdata->task;

	statsn = ntohl(*(uint32_t *)&in->hdr[24]);
	if (statsn > (int)iscsi->statsn) {
		iscsi->statsn = statsn;
	}

	flags = in->hdr[1];
	if ((flags&ISCSI_PDU_DATA_FINAL) == 0) {
		iscsi_set_error(iscsi, "scsi response pdu but Final bit is "
				"not set: 0x%02x.", flags);
		pdu->callback(iscsi, SCSI_STATUS_ERROR, task,
			      pdu->private_data);
		return -1;
	}
	if ((flags&ISCSI_PDU_DATA_ACK_REQUESTED) != 0) {
		iscsi_set_error(iscsi, "scsi response asked for ACK "
				"0x%02x.", flags);
		pdu->callback(iscsi, SCSI_STATUS_ERROR, task,
			      pdu->private_data);
		return -1;
	}

	response = in->hdr[2];

	status = in->hdr[3];

	switch (status) {
	case SCSI_STATUS_GOOD:
		task->datain.data = pdu->indata.data;
		task->datain.size = pdu->indata.size;

		pdu->indata.data = NULL;
		pdu->indata.size = 0;

		pdu->callback(iscsi, SCSI_STATUS_GOOD, task,
			      pdu->private_data);
		break;
	case SCSI_STATUS_CHECK_CONDITION:
		task->datain.size = in->data_pos;
		task->datain.data = malloc(task->datain.size);
		if (task->datain.data == NULL) {
			iscsi_set_error(iscsi, "failed to allocate blob for "
					"sense data");
		}
		memcpy(task->datain.data, in->data, task->datain.size);

		task->sense.error_type = task->datain.data[2] & 0x7f;
		task->sense.key        = task->datain.data[4] & 0x0f;
		task->sense.ascq       = ntohs(*(uint16_t *)
					       &(task->datain.data[14]));

		iscsi_set_error(iscsi, "SENSE KEY:%s(%d) ASCQ:%s(0x%04x)",
				scsi_sense_key_str(task->sense.key),
				task->sense.key,
				scsi_sense_ascq_str(task->sense.ascq),
				task->sense.ascq);
		pdu->callback(iscsi, SCSI_STATUS_CHECK_CONDITION, task,
			      pdu->private_data);
		break;
	default:
		iscsi_set_error(iscsi, "Unknown SCSI status :%d.", status);

		pdu->callback(iscsi, SCSI_STATUS_ERROR, task,
			      pdu->private_data);
		return -1;
	}

	return 0;
}

int
iscsi_process_scsi_data_in(struct iscsi_context *iscsi, struct iscsi_pdu *pdu,
			   struct iscsi_in_pdu *in, int *is_finished)
{
	int statsn, flags, status;
	struct iscsi_scsi_cbdata *scsi_cbdata = pdu->scsi_cbdata;
	struct scsi_task *task = scsi_cbdata->task;
	int dsl;

	statsn = ntohl(*(uint32_t *)&in->hdr[24]);
	if (statsn > (int)iscsi->statsn) {
		iscsi->statsn = statsn;
	}

	flags = in->hdr[1];
	if ((flags&ISCSI_PDU_DATA_ACK_REQUESTED) != 0) {
		iscsi_set_error(iscsi, "scsi response asked for ACK "
				"0x%02x.", flags);
		pdu->callback(iscsi, SCSI_STATUS_ERROR, task,
			      pdu->private_data);
		return -1;
	}
	dsl = ntohl(*(uint32_t *)&in->hdr[4])&0x00ffffff;

	if (iscsi_add_data(iscsi, &pdu->indata,
			   in->data, dsl, 0)
	    != 0) {
		iscsi_set_error(iscsi, "Out-of-memory: failed to add data "
				"to pdu in buffer.");
		return -1;
	}


	if ((flags&ISCSI_PDU_DATA_FINAL) == 0) {
		*is_finished = 0;
	}
	if ((flags&ISCSI_PDU_DATA_CONTAINS_STATUS) == 0) {
		*is_finished = 0;
	}

	if (*is_finished == 0) {
		return 0;
	}


	/* this was the final data-in packet in the sequence and it has
	 * the s-bit set, so invoke the callback.
	 */
	status = in->hdr[3];
	task->datain.data = pdu->indata.data;
	task->datain.size = pdu->indata.size;

	pdu->indata.data = NULL;
	pdu->indata.size = 0;

	pdu->callback(iscsi, status, task, pdu->private_data);

	return 0;
}

int
iscsi_process_r2t(struct iscsi_context *iscsi, struct iscsi_pdu *pdu,
			 struct iscsi_in_pdu *in)
{
	uint32_t ttt, offset, len;

	ttt    = ntohl(*(uint32_t *)&in->hdr[20]);
	offset = ntohl(*(uint32_t *)&in->hdr[40]);
	len    = ntohl(*(uint32_t *)&in->hdr[44]);

	iscsi_send_data_out(iscsi, pdu, ttt, offset, len);
	return 0;
}

/*
 * SCSI commands
 */

struct scsi_task *
iscsi_testunitready_task(struct iscsi_context *iscsi, int lun,
			  iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;

	task = scsi_cdb_testunitready();
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"testunitready cdb.");
		return NULL;
	}
	if (iscsi_scsi_command_async(iscsi, lun, task, cb, NULL,
				       private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_testunitready_async(struct iscsi_context *iscsi, int lun,
			  iscsi_command_cb cb, void *private_data)
{
	return iscsi_testunitready_task(iscsi, lun,
			  cb, private_data) == NULL ? -1 : 0;
}

struct scsi_task *
iscsi_reportluns_task(struct iscsi_context *iscsi, int report_type,
		       int alloc_len, iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;

	if (alloc_len < 16) {
		iscsi_set_error(iscsi, "Minimum allowed alloc len for "
				"reportluns is 16. You specified %d.",
				alloc_len);
		return NULL;
	}

	task = scsi_reportluns_cdb(report_type, alloc_len);
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"reportluns cdb.");
		return NULL;
	}
	/* report luns are always sent to lun 0 */
	if (iscsi_scsi_command_async(iscsi, 0, task, cb, NULL,
				     private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_reportluns_async(struct iscsi_context *iscsi, int report_type,
		       int alloc_len, iscsi_command_cb cb, void *private_data)
{
	return iscsi_reportluns_task(iscsi, report_type,
		       alloc_len, cb, private_data) == NULL ? -1 : 0;
}

struct scsi_task *
iscsi_inquiry_task(struct iscsi_context *iscsi, int lun, int evpd,
		    int page_code, int maxsize,
		    iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;

	task = scsi_cdb_inquiry(evpd, page_code, maxsize);
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"inquiry cdb.");
		return NULL;
	}
	if (iscsi_scsi_command_async(iscsi, lun, task, cb, NULL,
				       private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_inquiry_async(struct iscsi_context *iscsi, int lun, int evpd,
		    int page_code, int maxsize,
		    iscsi_command_cb cb, void *private_data)
{
	return iscsi_inquiry_task(iscsi, lun, evpd,
		    page_code, maxsize,
		    cb, private_data) == NULL ? -1 : 0;
}

struct scsi_task *
iscsi_readcapacity10_task(struct iscsi_context *iscsi, int lun, int lba,
			   int pmi, iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;

	task = scsi_cdb_readcapacity10(lba, pmi);
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"readcapacity10 cdb.");
		return NULL;
	}
	if (iscsi_scsi_command_async(iscsi, lun, task, cb, NULL,
				       private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_readcapacity10_async(struct iscsi_context *iscsi, int lun, int lba,
			   int pmi, iscsi_command_cb cb, void *private_data)
{
	return iscsi_readcapacity10_task(iscsi, lun, lba,
			   pmi, cb, private_data) == NULL ? -1 : 0;
}

struct scsi_task *
iscsi_read10_task(struct iscsi_context *iscsi, int lun, uint32_t lba,
		   uint32_t datalen, int blocksize,
		   iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;

	if (datalen % blocksize != 0) {
		iscsi_set_error(iscsi, "Datalen:%d is not a multiple of "
				"the blocksize:%d.", datalen, blocksize);
		return NULL;
	}

	task = scsi_cdb_read10(lba, datalen, blocksize);
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"read10 cdb.");
		return NULL;
	}
	if (iscsi_scsi_command_async(iscsi, lun, task, cb, NULL,
				       private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_read10_async(struct iscsi_context *iscsi, int lun, uint32_t lba,
		   uint32_t datalen, int blocksize,
		   iscsi_command_cb cb, void *private_data)
{
	return iscsi_read10_task(iscsi, lun, lba,
		   datalen, blocksize,
		   cb, private_data) == NULL ? -1 : 0;
}

struct scsi_task *
iscsi_write10_task(struct iscsi_context *iscsi, int lun, unsigned char *data,
		    uint32_t datalen, uint32_t lba, int fua, int fuanv, int blocksize,
		    iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;
	struct iscsi_data outdata;

	if (datalen % blocksize != 0) {
		iscsi_set_error(iscsi, "Datalen:%d is not a multiple of the "
				"blocksize:%d.", datalen, blocksize);
		return NULL;
	}

	task = scsi_cdb_write10(lba, datalen, fua, fuanv, blocksize);
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"read10 cdb.");
		return NULL;
	}

	outdata.data = data;
	outdata.size = datalen;

	if (iscsi_scsi_command_async(iscsi, lun, task, cb, &outdata,
				       private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_write10_async(struct iscsi_context *iscsi, int lun, unsigned char *data,
		    uint32_t datalen, uint32_t lba, int fua, int fuanv, int blocksize,
		    iscsi_command_cb cb, void *private_data)
{
	return iscsi_write10_task(iscsi, lun, data,
		    datalen, lba, fua, fuanv, blocksize,
		    cb, private_data) == NULL ? -1 : 0;
}

struct scsi_task *
iscsi_modesense6_task(struct iscsi_context *iscsi, int lun, int dbd, int pc,
		       int page_code, int sub_page_code,
		       unsigned char alloc_len,
		       iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;

	task = scsi_cdb_modesense6(dbd, pc, page_code, sub_page_code,
				   alloc_len);
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"modesense6 cdb.");
		return NULL;
	}
	if (iscsi_scsi_command_async(iscsi, lun, task, cb, NULL,
				       private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_modesense6_async(struct iscsi_context *iscsi, int lun, int dbd, int pc,
		       int page_code, int sub_page_code,
		       unsigned char alloc_len,
		       iscsi_command_cb cb, void *private_data)
{
	return iscsi_modesense6_task(iscsi, lun, dbd, pc,
		       page_code, sub_page_code,
		       alloc_len,
		       cb, private_data) == NULL ? -1 : 0;
}

struct scsi_task *
iscsi_synchronizecache10_task(struct iscsi_context *iscsi, int lun, int lba,
			       int num_blocks, int syncnv, int immed,
			       iscsi_command_cb cb, void *private_data)
{
	struct scsi_task *task;

	task = scsi_cdb_synchronizecache10(lba, num_blocks, syncnv,
					   immed);
	if (task == NULL) {
		iscsi_set_error(iscsi, "Out-of-memory: Failed to create "
				"synchronizecache10 cdb.");
		return NULL;
	}
	if (iscsi_scsi_command_async(iscsi, lun, task, cb, NULL,
				       private_data) != 0) {
		scsi_free_scsi_task(task);
		return NULL;
	}

	return task;
}

int
iscsi_synchronizecache10_async(struct iscsi_context *iscsi, int lun, int lba,
			       int num_blocks, int syncnv, int immed,
			       iscsi_command_cb cb, void *private_data)
{
	return iscsi_synchronizecache10_task(iscsi, lun, lba,
			       num_blocks, syncnv, immed,
			       cb, private_data) == NULL ? -1 : 0;
}
