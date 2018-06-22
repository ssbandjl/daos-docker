/**
 * (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * vos/vos_io.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include "vos_internal.h"

/** I/O context */
struct vos_io_context {
	daos_epoch_t		 ic_epoch;
	/** number DAOS IO descriptors */
	unsigned int		 ic_iod_nr;
	daos_iod_t		*ic_iods;
	/** reference on the object */
	struct vos_object	*ic_obj;
	/** EIO descriptor, has ic_iod_nr SGLs */
	struct eio_desc		*ic_eiod;
	/** cursor of SGL & IOV in EIO descriptor */
	unsigned int		 ic_sgl_at;
	unsigned int		 ic_iov_at;
	/** reserved SCM extents */
	unsigned int		 ic_actv_cnt;
	unsigned int		 ic_actv_at;
	struct pobj_action	*ic_actv;
	/** reserved mmids for SCM update */
	umem_id_t		*ic_mmids;
	unsigned int		 ic_mmids_cnt;
	unsigned int		 ic_mmids_at;
	/** reserved NVMe extents */
	d_list_t		 ic_blk_exts;
	/** TODO: remove this once vos_obj_zc_sgl() is replaced */
	daos_sg_list_t		*ic_sgls;
	/** flags */
	unsigned int		 ic_update:1,
				 ic_size_fetch:1;
};

static struct vos_io_context *
vos_ioh2ioc(daos_handle_t ioh)
{
	return (struct vos_io_context *)ioh.cookie;
}

static daos_handle_t
vos_ioc2ioh(struct vos_io_context *ioc)
{
	daos_handle_t ioh;

	ioh.cookie = (uint64_t)ioc;
	return ioh;
}

static void
iod_empty_sgl(struct vos_io_context *ioc, unsigned int sgl_at)
{
	struct eio_sglist *esgl;

	D_ASSERT(sgl_at < ioc->ic_iod_nr);
	ioc->ic_iods[sgl_at].iod_size = 0;
	esgl = eio_iod_sgl(ioc->ic_eiod, sgl_at);
	esgl->es_nr_out = 0;
}

static void
vos_ioc_reserve_fini(struct vos_io_context *ioc)
{
	D_ASSERT(d_list_empty(&ioc->ic_blk_exts));
	D_ASSERT(ioc->ic_actv_at == 0);

	if (ioc->ic_actv_cnt != 0) {
		D_FREE(ioc->ic_actv);
		ioc->ic_actv = NULL;
	}

	if (ioc->ic_mmids != NULL) {
		D_FREE(ioc->ic_mmids);
		ioc->ic_mmids = NULL;
	}
}

static int
vos_ioc_reserve_init(struct vos_io_context *ioc)
{
	int i, total_acts = 0;

	ioc->ic_actv = NULL;
	ioc->ic_actv_cnt = ioc->ic_actv_at = 0;
	ioc->ic_mmids_cnt = ioc->ic_mmids_at = 0;
	D_INIT_LIST_HEAD(&ioc->ic_blk_exts);

	if (!ioc->ic_update)
		return 0;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		daos_iod_t *iod = &ioc->ic_iods[i];

		total_acts += iod->iod_nr;
	}

	D_ALLOC(ioc->ic_mmids, total_acts * sizeof(*ioc->ic_mmids));
	if (ioc->ic_mmids == NULL)
		return -DER_NOMEM;

	if (vos_obj2umm(ioc->ic_obj)->umm_ops->mo_reserve == NULL)
		return 0;

	/*
	 * Too many IODs in one update RPC which exceeds the maximum reserve
	 * count that PMDK can handle in single transaction, fallback to
	 * persistent allocation (instead of reserve) in update_begin phase.
	 */
	if (total_acts > POBJ_MAX_ACTIONS) {
		D_WARN("Too many DAOS IODs (%d)!\n", total_acts);
		return 0;
	}

	D_ALLOC(ioc->ic_actv, total_acts * sizeof(*ioc->ic_actv));
	if (ioc->ic_actv == NULL)
		return -DER_NOMEM;

	ioc->ic_actv_cnt = total_acts;
	return 0;
}

static void
vos_ioc_destroy(struct vos_io_context *ioc)
{
	/* TODO: Remove this once vos_obj_zc_sgl_at() is replaced */
	if (ioc->ic_sgls != NULL) {
		int i;

		eio_iod_post(ioc->ic_eiod);
		for (i = 0; i < ioc->ic_iod_nr; i++)
			daos_sgl_fini(&ioc->ic_sgls[i], false);
		D_FREE(ioc->ic_sgls);
	}

	if (ioc->ic_eiod != NULL)
		eio_iod_free(ioc->ic_eiod);

	if (ioc->ic_obj)
		vos_obj_release(vos_obj_cache_current(), ioc->ic_obj);

	vos_ioc_reserve_fini(ioc);
	D_FREE_PTR(ioc);
}

static int
vos_ioc_create(daos_handle_t coh, daos_unit_oid_t oid, bool read_only,
	       daos_epoch_t epoch, unsigned int iod_nr, daos_iod_t *iods,
	       bool size_fetch, struct vos_io_context **ioc_pp)
{
	struct vos_io_context *ioc;
	struct eio_io_context *eioc;
	int i, rc;

	D_ALLOC_PTR(ioc);
	if (ioc == NULL)
		return -DER_NOMEM;

	ioc->ic_iod_nr = iod_nr;
	ioc->ic_iods = iods;
	ioc->ic_epoch = epoch;
	ioc->ic_update = !read_only;
	ioc->ic_size_fetch = size_fetch;

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, read_only,
			  &ioc->ic_obj);
	if (rc != 0)
		goto error;

	rc = vos_ioc_reserve_init(ioc);
	if (rc != 0)
		goto error;

	eioc = ioc->ic_obj->obj_cont->vc_pool->vp_io_ctxt;
	D_ASSERT(eioc != NULL);
	ioc->ic_eiod = eio_iod_alloc(eioc, iod_nr, !read_only);
	if (ioc->ic_eiod == NULL) {
		rc = -DER_NOMEM;
		goto error;
	}

	for (i = 0; i < iod_nr; i++) {
		int iov_nr = iods[i].iod_nr;
		struct eio_sglist *esgl;

		if (iods[i].iod_type == DAOS_IOD_SINGLE && iov_nr != 1) {
			D_ERROR("Invalid sv iod_nr=%d\n", iov_nr);
			rc = -DER_IO_INVAL;
			goto error;
		}

		/* Don't bother to initialize SGLs for size fetch */
		if (ioc->ic_size_fetch)
			continue;

		esgl = eio_iod_sgl(ioc->ic_eiod, i);
		rc = eio_sgl_init(esgl, iov_nr);
		if (rc != 0)
			goto error;
	}

	*ioc_pp = ioc;
	return 0;
error:
	vos_ioc_destroy(ioc);
	return rc;
}

static int
iod_fetch(struct vos_io_context *ioc, daos_iov_t *iov)
{
	struct eio_sglist *esgl;
	struct eio_iov *eiovs, eiov;
	int iov_nr, iov_at;

	if (ioc->ic_size_fetch)
		return 0;

	esgl = eio_iod_sgl(ioc->ic_eiod, ioc->ic_sgl_at);
	D_ASSERT(esgl != NULL);
	iov_nr = esgl->es_nr;
	iov_at = ioc->ic_iov_at;

	D_ASSERT(iov_nr > iov_at);
	D_ASSERT(iov_nr >= esgl->es_nr_out);

	if (iov_at == iov_nr - 1) {
		D_ALLOC(eiovs, iov_nr * 2 * sizeof(*eiovs));
		if (eiovs == NULL)
			return -DER_NOMEM;

		memcpy(eiovs, &esgl->es_iovs[0], iov_nr * sizeof(*eiovs));
		D_FREE(esgl->es_iovs);

		esgl->es_iovs = eiovs;
		esgl->es_nr = iov_nr * 2;
	}

	/*
	 * TODO: We temporarily forge an eio_iov from daos_iov_t, this will
	 *	 be fixed once the tree interfaces being adjusted.
	 */
	memset(&eiov, 0, sizeof(eiov));
	eiov.ei_type = EIO_ADDR_SCM;
	eiov.ei_data_len = iov->iov_len;
	if (iov->iov_buf != NULL) {
		umem_id_t umem_id;

		D_ASSERT(iov->iov_len != 0);
		umem_id = pmemobj_oid(iov->iov_buf);
		eiov.ei_off = umem_id.off;
	} else {
		eio_iov_set_hole(&eiov, 1);
	}

	esgl->es_iovs[iov_at] = eiov;
	esgl->es_nr_out++;
	ioc->ic_iov_at++;
	return 0;
}

/** Fetch the single value within the specified epoch range of an key */
static int
akey_fetch_single(daos_handle_t toh, daos_epoch_range_t *epr,
		  daos_size_t *rsize, struct vos_io_context *ioc)
{
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_csum_buf_t		 csum;
	daos_iov_t		 kiov; /* iov to carry key bundle */
	daos_iov_t		 riov; /* iov to carray record bundle */
	daos_iov_t		 diov; /* iov to return data buffer */
	int			 rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= epr;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_iov	= &diov;
	rbund.rb_csum	= &csum;
	daos_iov_set(&diov, NULL, 0);
	daos_csum_set(&csum, NULL, 0);

	rc = dbtree_fetch(toh, BTR_PROBE_LE, &kiov, &kiov, &riov);
	if (rc == -DER_NONEXIST) {
		rbund.rb_rsize = 0;
		rc = 0;
	} else if (rc != 0) {
		D_GOTO(out, rc);
	}

	rc = iod_fetch(ioc, &diov);
	if (rc != 0)
		D_GOTO(out, rc);

	*rsize = rbund.rb_rsize;
 out:
	return rc;
}

/** Fetch an extent from an akey */
static int
akey_fetch_recx(daos_handle_t toh, daos_epoch_range_t *epr, daos_recx_t *recx,
		daos_size_t *rsize_p, struct vos_io_context *ioc)
{
	struct evt_entry	*ent;
	/* At present, this is not exposed in interface but passing it toggles
	 * sorting and clipping of rectangles
	 */
	d_list_t		 covered;
	struct evt_entry_list	 ent_list;
	struct evt_rect		 rect;
	daos_iov_t		 iov;
	daos_size_t		 holes; /* hole width */
	daos_off_t		 index;
	daos_off_t		 end;
	unsigned int		 rsize;
	int			 rc;

	index = recx->rx_idx;
	end   = recx->rx_idx + recx->rx_nr;

	rect.rc_off_lo = index;
	rect.rc_off_hi = end - 1;
	rect.rc_epc_lo = epr->epr_lo;

	evt_ent_list_init(&ent_list);
	rc = evt_find(toh, &rect, &ent_list, &covered);
	if (rc != 0)
		D_GOTO(failed, rc);

	rsize = 0;
	holes = 0;
	evt_ent_list_for_each(ent, &ent_list) {
		daos_off_t	lo = ent->en_sel_rect.rc_off_lo;
		daos_off_t	hi = ent->en_sel_rect.rc_off_hi;
		daos_size_t	nr;

		D_ASSERT(hi >= lo);
		nr = hi - lo + 1;

		if (lo != index) {
			D_ASSERTF(lo > index,
				  DF_U64"/"DF_U64", "DF_RECT", "DF_RECT"\n",
				  lo, index, DP_RECT(&rect),
				  DP_RECT(&ent->en_sel_rect));
			holes += lo - index;
		}

		if (ent->en_inob == 0) { /* hole extent */
			index = lo + nr;
			holes += nr;
			continue;
		}

		if (rsize == 0)
			rsize = ent->en_inob;

		if (rsize != ent->en_inob) {
			D_ERROR("Record sizes of all indices must be "
				"the same: %u/%u\n", rsize, ent->en_inob);
			D_GOTO(failed, rc = -DER_IO_INVAL);
		}

		if (holes != 0) {
			daos_iov_set(&iov, NULL, holes * rsize);
			/* skip the hole */
			rc = iod_fetch(ioc, &iov);
			if (rc != 0)
				D_GOTO(failed, rc);
			holes = 0;
		}

		daos_iov_set(&iov, ent->en_addr, nr * rsize);
		rc = iod_fetch(ioc, &iov);
		if (rc != 0)
			D_GOTO(failed, rc);

		index = lo + nr;
	}

	D_ASSERT(index <= end);
	if (index < end)
		holes += end - index;

	if (holes != 0) { /* trailing holes */
		if (rsize == 0) { /* nothing but holes */
			iod_empty_sgl(ioc, ioc->ic_sgl_at);
		} else {
			daos_iov_set(&iov, NULL, holes * rsize);
			rc = iod_fetch(ioc, &iov);
			if (rc != 0)
				D_GOTO(failed, rc);
		}
	}
	*rsize_p = rsize;
 failed:
	evt_ent_list_fini(&ent_list);
	return rc;
}

static int
akey_fetch(struct vos_io_context *ioc, daos_handle_t ak_toh)
{
	daos_epoch_range_t epr;
	daos_handle_t toh;
	int i, rc, flags = 0;
	daos_iod_t *iod = &ioc->ic_iods[ioc->ic_sgl_at];
	bool is_array = (iod->iod_type == DAOS_IOD_ARRAY);

	D_DEBUG(DB_TRACE, "Fetch %s value\n", is_array ? "array" : "single");
	if (is_array)
		flags |= SUBTR_EVT;

	epr.epr_lo = epr.epr_hi = ioc->ic_epoch;
	rc = tree_prepare(ioc->ic_obj, &epr, ak_toh, VOS_BTR_AKEY,
			  &iod->iod_name, flags, &toh);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_IO, "Nonexistent akey\n");
		iod_empty_sgl(ioc, ioc->ic_sgl_at);
		return 0;
	}

	if (rc != 0) {
		D_ERROR("Failed to open tree root: %d\n", rc);
		return rc;
	}

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = akey_fetch_single(toh, &epr, &iod->iod_size, ioc);
		goto out;
	} /* else: array */

	for (i = 0; i < iod->iod_nr; i++) {
		daos_epoch_range_t *etmp;
		daos_size_t rsize;

		etmp = iod->iod_eprs ? &iod->iod_eprs[i] : &epr;
		rc = akey_fetch_recx(toh, etmp, &iod->iod_recxs[i], &rsize,
				     ioc);
		if (rc != 0) {
			D_DEBUG(DB_IO, "Failed to fetch index %d: %d\n", i, rc);
			goto out;
		}

		if (rsize == 0) /* nothing but hole */
			continue;

		if (iod->iod_size == 0)
			iod->iod_size = rsize;

		if (iod->iod_size != rsize) {
			D_ERROR("Cannot support mixed record size "
				DF_U64"/"DF_U64"\n", iod->iod_size, rsize);
			D_GOTO(out, rc);
		}
	}
out:
	tree_release(toh, is_array);
	return rc;
}

static void
iod_set_cursor(struct vos_io_context *ioc, unsigned int sgl_at)
{
	D_ASSERT(sgl_at < ioc->ic_iod_nr);
	D_ASSERT(ioc->ic_iods != NULL);

	ioc->ic_sgl_at = sgl_at;
	ioc->ic_iov_at = 0;
}

static int
dkey_fetch(struct vos_io_context *ioc, daos_key_t *dkey)
{
	daos_handle_t toh;
	daos_epoch_range_t epr;
	struct vos_object *obj = ioc->ic_obj;
	int i, rc;

	rc = vos_obj_tree_init(obj);
	if (rc != 0)
		return rc;

	epr.epr_lo = epr.epr_hi = ioc->ic_epoch;
	rc = tree_prepare(obj, &epr, obj->obj_toh, VOS_BTR_DKEY, dkey, 0, &toh);
	if (rc == -DER_NONEXIST) {
		for (i = 0; i < ioc->ic_iod_nr; i++)
			iod_empty_sgl(ioc, i);
		D_DEBUG(DB_IO, "Nonexistent dkey\n");
		return 0;
	}

	if (rc != 0) {
		D_ERROR("Failed to prepare subtree: %d\n", rc);
		return rc;
	}

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_fetch(ioc, toh);
		if (rc != 0)
			break;
	}

	tree_release(toh, false);
	return rc;
}

int
vos_fetch_end(daos_handle_t ioh, int err)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	/* NB: it's OK to use the stale ioc->ic_obj for fetch_end */
	D_ASSERT(!ioc->ic_update);
	vos_ioc_destroy(ioc);
	return err;
}

int
vos_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		bool size_fetch, daos_handle_t *ioh)
{
	struct vos_io_context *ioc;
	int i, rc;

	rc = vos_ioc_create(coh, oid, true, epoch, iod_nr, iods, size_fetch,
			    &ioc);
	if (rc != 0)
		return rc;

	if (vos_obj_is_empty(ioc->ic_obj)) {
		for (i = 0; i < iod_nr; i++)
			iod_empty_sgl(ioc, i);
	} else {
		rc = dkey_fetch(ioc, dkey);
		if (rc != 0)
			goto error;
	}

	D_DEBUG(DB_IO, "Prepared io context for fetching %d iods\n", iod_nr);
	*ioh = vos_ioc2ioh(ioc);
	return 0;
error:
	return vos_fetch_end(vos_ioc2ioh(ioc), rc);
}

/* Caveat: EIO APIs may yield! */
static int
vos_obj_copy(struct vos_io_context *ioc, daos_sg_list_t *sgls,
	     unsigned int sgl_nr)
{
	int rc, err;

	D_ASSERT(sgl_nr == ioc->ic_iod_nr);
	rc = eio_iod_prep(ioc->ic_eiod);
	if (rc)
		return rc;

	err = eio_iod_copy(ioc->ic_eiod, sgls, sgl_nr);
	rc = eio_iod_post(ioc->ic_eiod);

	/* TODO: add back checksum */
	return err ? err : rc;
}

/**
 * Fetch an array of records from the specified object.
 * TODO: Deprecated, should be replaced by ds_obj_fetch()
 */
int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
	      daos_sg_list_t *sgls)
{
	daos_handle_t ioh;
	bool size_fetch = (sgls == NULL);
	int rc;

	D_DEBUG(DB_TRACE, "Fetch "DF_UOID", desc_nr %d, epoch "DF_U64"\n",
		DP_UOID(oid), iod_nr, epoch);

	rc = vos_fetch_begin(coh, oid, epoch, dkey, iod_nr, iods, size_fetch,
			     &ioh);
	if (rc) {
		D_ERROR("Fetch "DF_UOID" failed %d\n", DP_UOID(oid), rc);
		return rc;
	}

	if (!size_fetch) {
		struct vos_io_context *ioc = vos_ioh2ioc(ioh);
		int i, j;

		for (i = 0; i < iod_nr; i++) {
			struct eio_sglist *esgl = eio_iod_sgl(ioc->ic_eiod, i);
			daos_sg_list_t *sgl = &sgls[i];

			/* Inform caller the nonexistent of object/key */
			if (esgl->es_nr_out == 0) {
				for (j = 0; j < sgl->sg_nr; j++)
					sgl->sg_iovs[j].iov_len = 0;
			}
		}

		rc = vos_obj_copy(ioc, sgls, iod_nr);
		if (rc)
			D_ERROR("Copy "DF_UOID" failed %d\n",
				DP_UOID(oid), rc);
	}

	rc = vos_fetch_end(ioh, rc);
	return rc;
}

static umem_id_t
iod_update_mmid(struct vos_io_context *ioc)
{
	umem_id_t mmid;

	D_ASSERT(ioc->ic_mmids_at < ioc->ic_mmids_cnt);
	mmid = ioc->ic_mmids[ioc->ic_mmids_at];
	ioc->ic_mmids_at++;

	return mmid;
}

static int
akey_update_single(daos_handle_t toh, daos_epoch_range_t *epr,
		   uuid_t cookie, uint32_t pm_ver, daos_size_t rsize,
		   struct vos_io_context *ioc)
{
	struct vos_key_bundle kbund;
	struct vos_rec_bundle rbund;
	daos_csum_buf_t csum;
	daos_iov_t kiov, riov;
	daos_iov_t iov; /* iov for the sink buffer */
	umem_id_t mmid;
	int rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epr	= epr;

	daos_csum_set(&csum, NULL, 0);
	daos_iov_set(&iov, NULL, rsize);

	D_ASSERT(ioc->ic_iov_at == 0);
	/* TODO support NVMe record */
	mmid = iod_update_mmid(ioc);

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_csum	= &csum;
	rbund.rb_iov	= &iov;
	rbund.rb_rsize	= rsize;
	rbund.rb_mmid	= mmid;
	uuid_copy(rbund.rb_cookie, cookie);
	rbund.rb_ver	= pm_ver;

	rc = dbtree_update(toh, &kiov, &riov);
	if (rc != 0)
		D_ERROR("Failed to update subtree: %d\n", rc);

	return rc;
}

/**
 * Update a record extent.
 * See comment of vos_recx_fetch for explanation of @off_p.
 */
static int
akey_update_recx(daos_handle_t toh, daos_epoch_range_t *epr, uuid_t cookie,
		 uint32_t pm_ver, daos_recx_t *recx, daos_size_t rsize,
		 struct vos_io_context *ioc)
{
	struct evt_rect rect;
	umem_id_t mmid;
	int rc;

	rect.rc_epc_lo = epr->epr_lo;
	rect.rc_off_lo = recx->rx_idx;
	rect.rc_off_hi = recx->rx_idx + recx->rx_nr - 1;

	/* TODO support NVMe record */
	mmid = iod_update_mmid(ioc);
	rc = evt_insert(toh, cookie, pm_ver, &rect, rsize, mmid);

	return rc;
}

static int
akey_update(struct vos_io_context *ioc, uuid_t cookie, uint32_t pm_ver,
	    daos_handle_t ak_toh)
{
	struct vos_object *obj = ioc->ic_obj;
	daos_epoch_range_t epr;
	daos_handle_t toh;
	int i, rc, flags = SUBTR_CREATE;
	daos_iod_t *iod = &ioc->ic_iods[ioc->ic_sgl_at];
	bool is_array = (iod->iod_type == DAOS_IOD_ARRAY);

	if (is_array)
		flags |= SUBTR_EVT;

	D_DEBUG(DB_TRACE, "Update %s value\n", is_array ? "array" : "single");

	epr.epr_lo = ioc->ic_epoch;
	epr.epr_hi = DAOS_EPOCH_MAX;
	rc = tree_prepare(obj, &epr, ak_toh, VOS_BTR_AKEY, &iod->iod_name,
			  flags, &toh);
	if (rc != 0)
		return rc;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		rc = akey_update_single(toh, &epr, cookie, pm_ver,
					iod->iod_size, ioc);
		goto out;
	} /* else: array */

	for (i = 0; i < iod->iod_nr; i++) {
		daos_epoch_range_t *etmp;

		etmp = iod->iod_eprs ? &iod->iod_eprs[i] : &epr;
		rc = akey_update_recx(toh, etmp, cookie, pm_ver,
				      &iod->iod_recxs[i], iod->iod_size, ioc);
		if (rc != 0)
			goto out;
	}
out:
	tree_release(toh, is_array);
	return rc;
}

static int
dkey_update(struct vos_io_context *ioc, uuid_t cookie, uint32_t pm_ver,
	    daos_key_t *dkey)
{
	struct vos_object *obj = ioc->ic_obj;
	daos_epoch_range_t epr;
	daos_handle_t ak_toh, ck_toh;
	int i, rc;

	rc = vos_obj_tree_init(obj);
	if (rc != 0)
		return rc;

	epr.epr_lo = ioc->ic_epoch;
	epr.epr_hi = DAOS_EPOCH_MAX;
	rc = tree_prepare(obj, &epr, obj->obj_toh, VOS_BTR_DKEY, dkey,
			  SUBTR_CREATE, &ak_toh);
	if (rc != 0)
		return rc;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_update(ioc, cookie, pm_ver, ak_toh);
		if (rc != 0)
			goto out;
	}

	/** If dkey update is successful update the cookie tree */
	ck_toh = vos_obj2cookie_hdl(obj);
	rc = vos_cookie_find_update(ck_toh, cookie, ioc->ic_epoch, true, NULL);
	if (rc) {
		D_ERROR("Failed to record cookie: %d\n", rc);
		goto out;
	}
out:
	tree_release(ak_toh, false);
	return rc;
}

static daos_size_t
vos_recx2irec_size(daos_size_t rsize, daos_csum_buf_t *csum)
{
	struct vos_rec_bundle	rbund;

	rbund.rb_csum	= csum;
	rbund.rb_rsize	= rsize;

	return vos_irec_size(&rbund);
}

static int
vos_reserve(struct vos_io_context *ioc, uint16_t media, daos_size_t size,
	    uint64_t *off)
{
	struct vos_object *obj = ioc->ic_obj;
	umem_id_t mmid;

	if (media == EIO_ADDR_SCM) {
		if (ioc->ic_actv_cnt > 0) {
			struct pobj_action *act;

			D_ASSERT(ioc->ic_actv_cnt > ioc->ic_actv_at);
			D_ASSERT(ioc->ic_actv != NULL);
			act = &ioc->ic_actv[ioc->ic_actv_at];

			mmid = umem_reserve(vos_obj2umm(obj), act, size);
			if (!UMMID_IS_NULL(mmid))
				ioc->ic_actv_at++;
		} else {
			mmid = umem_alloc(vos_obj2umm(obj), size);
		}

		if (!UMMID_IS_NULL(mmid)) {
			ioc->ic_mmids[ioc->ic_mmids_cnt] = mmid;
			ioc->ic_mmids_cnt++;
			*off = mmid.off;
		}

		return UMMID_IS_NULL(mmid) ? -DER_NOSPACE : 0;
	}

	/* TODO: reserve NVMe blocks */
	return -DER_INVAL;
}

/*
 * A simple media selection policy embedded in VOS, which select media by
 * akey type and record size.
 */
static uint16_t
akey_media_select(daos_iod_type_t type, daos_size_t size)
{
	/* TODO: support NVMe */
	return EIO_ADDR_SCM;
}

static int
akey_update_begin(struct vos_io_context *ioc)
{
	struct vos_object *obj = ioc->ic_obj;
	daos_iod_t *iod = &ioc->ic_iods[ioc->ic_sgl_at];
	struct eio_sglist *esgl;
	int i, rc;

	if (iod->iod_type == DAOS_IOD_SINGLE && iod->iod_nr != 1) {
		D_ERROR("Invalid sv iod_nr=%d\n", iod->iod_nr);
		return -DER_IO_INVAL;
	}

	esgl = eio_iod_sgl(ioc->ic_eiod, ioc->ic_sgl_at);
	D_ASSERT(esgl->es_nr != 0 && esgl->es_nr_out == 0);

	for (i = 0; i < iod->iod_nr; i++) {
		daos_size_t size;
		uint64_t off = 0;
		uint16_t media;
		struct eio_iov *eiov;

		D_ASSERTF(i < esgl->es_nr, "%d >= %d\n", i, esgl->es_nr);
		eiov = &esgl->es_iovs[i];

		size = (iod->iod_type == DAOS_IOD_SINGLE) ?
		       vos_recx2irec_size(iod->iod_size, NULL) :
		       iod->iod_recxs[i].rx_nr * iod->iod_size;

		media = akey_media_select(iod->iod_type, size);

		/* recx punch */
		if (size == 0) {
			ioc->ic_mmids[ioc->ic_mmids_cnt] = UMMID_NULL;
			ioc->ic_mmids_cnt++;
			goto next;
		}

		rc = vos_reserve(ioc, media, size, &off);
		if (rc)
			return rc;

		if (iod->iod_type == DAOS_IOD_SINGLE) {
			umem_id_t mmid;
			struct vos_irec_df *irec;
			char *payload_addr;

			D_ASSERT(media == EIO_ADDR_SCM);
			D_ASSERT(ioc->ic_mmids_cnt > 0);
			mmid = ioc->ic_mmids[ioc->ic_mmids_cnt - 1];
			irec = (struct vos_irec_df *)
				umem_id2ptr(vos_obj2umm(obj), mmid);
			irec->ir_cs_size = 0;
			irec->ir_cs_type = 0;

			/* Get the record payload offset */
			payload_addr = vos_irec2data(irec);
			D_ASSERT(payload_addr >= (char *)irec);
			off = mmid.off + (payload_addr - (char *)irec);
			size = iod->iod_size;
		}
next:
		eiov->ei_type = media;
		eiov->ei_off = off;
		eiov->ei_data_len = size;
		D_DEBUG(DB_IO, "media %hu offset "DF_X64" size %zd\n",
			media, off, size);
		esgl->es_nr_out++;
		D_ASSERT(esgl->es_nr_out <= esgl->es_nr);
	}
	return 0;
}

static int
dkey_update_begin(struct vos_io_context *ioc, daos_key_t *dkey)
{
	int i, rc = 0;

	for (i = 0; i < ioc->ic_iod_nr; i++) {
		iod_set_cursor(ioc, i);
		rc = akey_update_begin(ioc);
		if (rc != 0)
			break;
	}

	return rc;
}

static void
update_cancel(struct vos_io_context *ioc)
{
	/* Cancel SCM reservations or free persistent allocations */
	if (ioc->ic_actv_at != 0) {
		D_ASSERT(ioc->ic_actv != NULL);
		umem_cancel(vos_obj2umm(ioc->ic_obj), ioc->ic_actv,
			    ioc->ic_actv_at);
		ioc->ic_actv_at = 0;
	} else if (ioc->ic_mmids_cnt != 0) {
		struct umem_instance *umem = vos_obj2umm(ioc->ic_obj);
		int i, rc;

		rc = umem_tx_begin(umem);
		if (rc) {
			D_ERROR("TX start for update rollback: %d\n", rc);
			return;
		}

		for (i = 0; i < ioc->ic_mmids_cnt; i++) {
			if (UMMID_IS_NULL(ioc->ic_mmids[i]))
				continue;
			umem_free(umem, ioc->ic_mmids[i]);
		}

		rc =  umem_tx_commit(umem);
		if (rc) {
			D_ERROR("TX commit for update rollback: %d\n", rc);
			return;
		}
	}

	/* TODO: Cancel NVMe reservations */
}

int
vos_update_end(daos_handle_t ioh, uuid_t cookie, uint32_t pm_ver,
	       daos_key_t *dkey, int err)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);
	struct umem_instance *umem;

	D_ASSERT(ioc->ic_update);
	D_ASSERT(ioc->ic_obj != NULL);

	if (err != 0)
		goto out;

	err = vos_obj_revalidate(vos_obj_cache_current(), ioc->ic_epoch,
				 &ioc->ic_obj);
	if (err)
		goto out;

	umem = vos_obj2umm(ioc->ic_obj);
	err = umem_tx_begin(umem);
	if (err)
		goto out;

	/* Publish SCM reservations */
	if (ioc->ic_actv_at != 0) {
		err = umem_tx_publish(umem, ioc->ic_actv, ioc->ic_actv_at);
		ioc->ic_actv_at = 0;
		D_DEBUG(DB_TRACE, "publish ioc %p actv_at %d rc %d\n",
			ioc, ioc->ic_actv_cnt, err);
		if (err)
			goto abort;
	}

	/* Update tree index */
	err = dkey_update(ioc, cookie, pm_ver, dkey);
	if (err) {
		D_ERROR("Failed to update tree index: %d\n", err);
		goto abort;
	}

	/* TODO Publish NVMe reservations */

abort:
	err = err ? umem_tx_abort(umem, err) : umem_tx_commit(umem);
out:
	if (err != 0)
		update_cancel(ioc);
	vos_ioc_destroy(ioc);

	return err;
}

int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
		 daos_handle_t *ioh)
{
	struct vos_io_context *ioc;
	int rc;

	rc = vos_ioc_create(coh, oid, false, epoch, iod_nr, iods, false, &ioc);
	if (rc != 0)
		return rc;

	if (ioc->ic_actv_cnt != 0) {
		rc = dkey_update_begin(ioc, dkey);
		if (rc)
			goto error;
	} else {
		struct umem_instance *umem = vos_obj2umm(ioc->ic_obj);

		rc = umem_tx_begin(umem);
		if (rc)
			goto error;

		rc = dkey_update_begin(ioc, dkey);
		if (rc)
			D_ERROR(DF_UOID"dkey update begin failed. %d\n",
				DP_UOID(oid), rc);

		rc = rc ? umem_tx_abort(umem, rc) : umem_tx_commit(umem);
		if (rc)
			goto error;
	}

	D_DEBUG(DB_IO, "Prepared io context for updating %d iods\n", iod_nr);
	*ioh = vos_ioc2ioh(ioc);
	return 0;
error:
	vos_update_end(vos_ioc2ioh(ioc), 0, 0, dkey, rc);
	return rc;
}

/**
 * Update an array of records for the specified object.
 * TODO: Deprecated, should be replaced by ds_obj_update()
 */
int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       uuid_t cookie, uint32_t pm_ver, daos_key_t *dkey,
	       unsigned int iod_nr, daos_iod_t *iods, daos_sg_list_t *sgls)
{
	daos_handle_t ioh;
	int rc;

	D_DEBUG(DB_IO, "Update "DF_UOID", desc_nr %d, cookie "DF_UUID" epoch "
		DF_U64"\n", DP_UOID(oid), iod_nr, DP_UUID(cookie), epoch);

	rc = vos_update_begin(coh, oid, epoch, dkey, iod_nr, iods, &ioh);
	if (rc) {
		D_ERROR("Update "DF_UOID" failed %d\n", DP_UOID(oid), rc);
		return rc;
	}

	rc = vos_obj_copy(vos_ioh2ioc(ioh), sgls, iod_nr);
	if (rc)
		D_ERROR("Copy "DF_UOID" failed %d\n", DP_UOID(oid), rc);

	rc = vos_update_end(ioh, cookie, pm_ver, dkey, rc);
	return rc;
}

struct eio_desc *
vos_ioh2desc(daos_handle_t ioh)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	D_ASSERT(ioc->ic_eiod != NULL);
	return ioc->ic_eiod;
}

struct eio_sglist *
vos_iod_sgl_at(daos_handle_t ioh, unsigned int idx)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	if (idx > ioc->ic_iod_nr) {
		D_ERROR("Invalid SGL index %d >= %d\n",
			idx, ioc->ic_iod_nr);
		return NULL;
	}
	return eio_iod_sgl(ioc->ic_eiod, idx);
}

/* TODO: Deprecated, will be replaced by vos_iod_sgl_at(). */
int
vos_obj_zc_sgl_at(daos_handle_t ioh, unsigned int idx, daos_sg_list_t **sgl_pp)
{
	struct vos_io_context *ioc = vos_ioh2ioc(ioh);

	/* Convert array of eio_sglist into array of daos_sg_list_t */
	if (ioc->ic_sgls == NULL) {
		int i, j, rc;

		D_ALLOC(ioc->ic_sgls, sizeof(*ioc->ic_sgls) * ioc->ic_iod_nr);
		if (ioc->ic_sgls == NULL)
			return -DER_NOMEM;

		rc = eio_iod_prep(ioc->ic_eiod);
		if (rc)
			return rc;

		for (i = 0; i < ioc->ic_iod_nr; i++) {
			struct eio_sglist *esgl;
			daos_sg_list_t *sgl;

			esgl = eio_iod_sgl(ioc->ic_eiod, i);
			D_ASSERT(esgl != NULL);
			sgl = &ioc->ic_sgls[i];

			rc = daos_sgl_init(sgl, esgl->es_nr_out);
			if (rc != 0)
				return -DER_NOMEM;
			sgl->sg_nr_out = esgl->es_nr_out;

			for (j = 0; j < sgl->sg_nr_out; j++) {
				struct eio_iov *eiov = &esgl->es_iovs[j];
				daos_iov_t *iov = &sgl->sg_iovs[j];

				iov->iov_buf = eiov->ei_buf;
				iov->iov_len = eiov->ei_data_len;
				iov->iov_buf_len = eiov->ei_data_len;
			}
		}
	}

	if (idx >= ioc->ic_iod_nr) {
		*sgl_pp = NULL;
		D_DEBUG(DB_IO, "Invalid iod index %d/%d.\n",
			idx, ioc->ic_iod_nr);
		return -DER_NONEXIST;
	}

	*sgl_pp = &ioc->ic_sgls[idx];
	return 0;
}
