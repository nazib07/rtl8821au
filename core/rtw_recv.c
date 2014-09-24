/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_RECV_C_

#include <drv_types.h>



#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
void rtw_signal_stat_timer_hdl(RTW_TIMER_HDL_ARGS);
#endif


void _rtw_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv)
{
	memset((uint8_t *)psta_recvpriv, 0, sizeof (struct sta_recv_priv));

	_rtw_spinlock_init(&psta_recvpriv->lock);

	/*
	 * for(i=0; i<MAX_RX_NUMBLKS; i++)
	 * 	_rtw_init_queue(&psta_recvpriv->blk_strms[i]);
	 */

	_rtw_init_queue(&psta_recvpriv->defrag_q);

}

sint _rtw_init_recv_priv(struct recv_priv *precvpriv, struct _ADAPTER *padapter)
{
	sint i;

	union recv_frame *precvframe;

	sint	res=_SUCCESS;


	/*
	 * We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc().
	 * memset((unsigned char *)precvpriv, 0, sizeof (struct  recv_priv));
	 */

	_rtw_spinlock_init(&precvpriv->lock);

	_rtw_init_queue(&precvpriv->free_recv_queue);
	_rtw_init_queue(&precvpriv->recv_pending_queue);
	_rtw_init_queue(&precvpriv->uc_swdec_pending_queue);

	precvpriv->adapter = padapter;

	precvpriv->free_recvframe_cnt = NR_RECVFRAME;

	rtw_os_recv_resource_init(precvpriv, padapter);

	precvpriv->pallocated_frame_buf = rtw_zvmalloc(NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);

	if (precvpriv->pallocated_frame_buf==NULL){
		res= _FAIL;
		goto exit;
	}

	/*
	 * memset(precvpriv->pallocated_frame_buf, 0, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);
	 */

	precvpriv->precv_frame_buf = (uint8_t *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_frame_buf), RXFRAME_ALIGN_SZ);

	/*
	 * precvpriv->precv_frame_buf = precvpriv->pallocated_frame_buf + RXFRAME_ALIGN_SZ -
	 * 						((SIZE_PTR) (precvpriv->pallocated_frame_buf) &(RXFRAME_ALIGN_SZ-1));
	 */

	precvframe = (union recv_frame*) precvpriv->precv_frame_buf;

	for (i = 0; i < NR_RECVFRAME ; i++) {
		_rtw_init_listhead(&(precvframe->u.list));

		rtw_list_insert_tail(&(precvframe->u.list), &(precvpriv->free_recv_queue.queue));

		res = rtw_os_recv_resource_alloc(padapter, precvframe);

		precvframe->u.hdr.len = 0;

		precvframe->u.hdr.adapter =padapter;
		precvframe++;

	}


	precvpriv->rx_pending_cnt=1;

	sema_init(&precvpriv->allrxreturnevt, 0);


	res = rtw_hal_init_recv_priv(padapter);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_init_timer(&precvpriv->signal_stat_timer, padapter, RTW_TIMER_HDL_NAME(signal_stat));

	precvpriv->signal_stat_sampling_interval = 1000; /* ms */
	/* precvpriv->signal_stat_converging_constant = 5000; ms */

	rtw_set_signal_stat_timer(precvpriv);
#endif

exit:

_func_exit_;

	return res;

}

void rtw_mfree_recv_priv_lock(struct recv_priv *precvpriv);
void rtw_mfree_recv_priv_lock(struct recv_priv *precvpriv)
{
	_rtw_spinlock_free(&precvpriv->lock);
#ifdef CONFIG_RECV_THREAD_MODE
#endif

	_rtw_spinlock_free(&precvpriv->free_recv_queue.lock);
	_rtw_spinlock_free(&precvpriv->recv_pending_queue.lock);

	_rtw_spinlock_free(&precvpriv->free_recv_buf_queue.lock);

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	_rtw_spinlock_free(&precvpriv->recv_buf_pending_queue.lock);
#endif
}

void _rtw_free_recv_priv (struct recv_priv *precvpriv)
{
	struct _ADAPTER	*padapter = precvpriv->adapter;

	rtw_free_uc_swdec_pending_queue(padapter);

	rtw_mfree_recv_priv_lock(precvpriv);

	rtw_os_recv_resource_free(precvpriv);

	if(precvpriv->pallocated_frame_buf) {
		rtw_vmfree(precvpriv->pallocated_frame_buf);
	}

	rtw_hal_free_recv_priv(padapter);
}

union recv_frame *_rtw_alloc_recvframe (_queue *pfree_recv_queue)
{

	union recv_frame  *precvframe;
	struct list_head	*plist, *phead;
	struct _ADAPTER *padapter;
	struct recv_priv *precvpriv;

	if (_rtw_queue_empty(pfree_recv_queue) == _TRUE) {
		precvframe = NULL;
	} else {
		phead = get_list_head(pfree_recv_queue);

		plist = get_next(phead);

		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);

		rtw_list_delete(&precvframe->u.hdr.list);
		padapter=precvframe->u.hdr.adapter;
		if(padapter !=NULL){
			precvpriv=&padapter->recvpriv;
			if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt--;
		}
	}

	return precvframe;
}

union recv_frame *rtw_alloc_recvframe (_queue *pfree_recv_queue)
{
	_irqL irqL;
	union recv_frame  *precvframe;

	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

	precvframe = _rtw_alloc_recvframe(pfree_recv_queue);

	_exit_critical_bh(&pfree_recv_queue->lock, &irqL);

	return precvframe;
}

void rtw_init_recvframe(union recv_frame *precvframe, struct recv_priv *precvpriv)
{
	/* Perry: This can be removed */
	_rtw_init_listhead(&precvframe->u.hdr.list);

	precvframe->u.hdr.len=0;
}

int rtw_free_recvframe(union recv_frame *precvframe, _queue *pfree_recv_queue)
{
	_irqL irqL;
	struct _ADAPTER *padapter=precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

_func_enter_;

	rtw_os_free_recvframe(precvframe);

	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

	rtw_list_delete(&(precvframe->u.hdr.list));

	precvframe->u.hdr.len = 0;

	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(pfree_recv_queue));

	if (padapter !=NULL){
		if(pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt++;
	}

	_exit_critical_bh(&pfree_recv_queue->lock, &irqL);

	return _SUCCESS;
}




sint _rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{

	struct _ADAPTER *padapter=precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	/*_rtw_init_listhead(&(precvframe->u.hdr.list)); */
	rtw_list_delete(&(precvframe->u.hdr.list));


	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(queue));

	if (padapter != NULL) {
		if (queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}

	return _SUCCESS;
}

sint rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	sint ret;
	_irqL irqL;

	/* _spinlock(&pfree_recv_queue->lock); */
	_enter_critical_bh(&queue->lock, &irqL);
	ret = _rtw_enqueue_recvframe(precvframe, queue);
	/* _rtw_spinunlock(&pfree_recv_queue->lock); */
	_exit_critical_bh(&queue->lock, &irqL);

	return ret;
}

/*
sint	rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	return rtw_free_recvframe(precvframe, queue);
}
*/




/*
caller : defrag ; recvframe_chk_defrag in recv_thread  (passive)
pframequeue: defrag_queue : will be accessed in recv_thread  (passive)

using spinlock to protect

*/

void rtw_free_recvframe_queue(_queue *pframequeue,  _queue *pfree_recv_queue)
{
	union	recv_frame 	*precvframe;
	struct list_head	*plist, *phead;

	_rtw_spinlock(&pframequeue->lock);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);

		plist = get_next(plist);

		/*
		 * rtw_list_delete(&precvframe->u.hdr.list);
		 * will do this in rtw_free_recvframe()
		 */

		rtw_free_recvframe(precvframe, pfree_recv_queue);
	}

	_rtw_spinunlock(&pframequeue->lock);


}

uint32_t rtw_free_uc_swdec_pending_queue(struct _ADAPTER *adapter)
{
	uint32_t cnt = 0;
	union recv_frame *pending_frame;

	while ((pending_frame=rtw_alloc_recvframe(&adapter->recvpriv.uc_swdec_pending_queue))) {
		rtw_free_recvframe(pending_frame, &adapter->recvpriv.free_recv_queue);
		DBG_871X("%s: dequeue uc_swdec_pending_queue\n", __func__);
		cnt++;
	}

	return cnt;
}


sint rtw_enqueue_recvbuf_to_head(struct recv_buf *precvbuf, _queue *queue)
{
	_irqL irqL;

	_enter_critical_bh(&queue->lock, &irqL);

	rtw_list_delete(&precvbuf->list);
	rtw_list_insert_head(&precvbuf->list, get_list_head(queue));

	_exit_critical_bh(&queue->lock, &irqL);

	return _SUCCESS;
}

sint rtw_enqueue_recvbuf(struct recv_buf *precvbuf, _queue *queue)
{
	_irqL irqL;

	_enter_critical_ex(&queue->lock, &irqL);

	rtw_list_delete(&precvbuf->list);

	rtw_list_insert_tail(&precvbuf->list, get_list_head(queue));
	_exit_critical_ex(&queue->lock, &irqL);
	return _SUCCESS;
}

struct recv_buf *rtw_dequeue_recvbuf (_queue *queue)
{
	_irqL irqL;
	struct recv_buf *precvbuf;
	struct list_head	*plist, *phead;

	_enter_critical_ex(&queue->lock, &irqL);

	if(_rtw_queue_empty(queue) == _TRUE) {
		precvbuf = NULL;
	} else {
		phead = get_list_head(queue);

		plist = get_next(phead);

		precvbuf = LIST_CONTAINOR(plist, struct recv_buf, list);

		rtw_list_delete(&precvbuf->list);

	}

	_exit_critical_ex(&queue->lock, &irqL);

	return precvbuf;

}

sint recvframe_chkmic(struct _ADAPTER *adapter,  union recv_frame *precvframe);
sint recvframe_chkmic(struct _ADAPTER *adapter,  union recv_frame *precvframe){

	sint	i,res=_SUCCESS;
	uint32_t	datalen;
	uint8_t	miccode[8];
	uint8_t	bmic_err=_FALSE,brpt_micerror = _TRUE;
	uint8_t	*pframe, *payload,*pframemic;
	uint8_t	*mickey;
	/* uint8_t	*iv,rxdata_key_idx=0; */
	struct	sta_info		*stainfo;
	struct	rx_pkt_attrib	*prxattrib=&precvframe->u.hdr.attrib;
	struct 	security_priv	*psecuritypriv=&adapter->securitypriv;

	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	stainfo=rtw_get_stainfo(&adapter->stapriv ,&prxattrib->ta[0]);

	if (prxattrib->encrypt == _TKIP_) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n recvframe_chkmic:prxattrib->encrypt ==_TKIP_\n"));
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n recvframe_chkmic:da=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
			prxattrib->ra[0],prxattrib->ra[1],prxattrib->ra[2],prxattrib->ra[3],prxattrib->ra[4],prxattrib->ra[5]));

		/* calculate mic code */
		if (stainfo != NULL) {
			if (IS_MCAST(prxattrib->ra)) {
				/*
				 * mickey=&psecuritypriv->dot118021XGrprxmickey.skey[0];
				 * iv = precvframe->u.hdr.rx_data+prxattrib->hdrlen;
				 * rxdata_key_idx =( ((iv[3])>>6)&0x3) ;
				 * mickey=&psecuritypriv->dot118021XGrprxmickey[prxattrib->key_index].skey[0];
				 */

				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n recvframe_chkmic: bcmc key \n"));
				/*
				 * DBG_871X("\n recvframe_chkmic: bcmc key psecuritypriv->dot118021XGrpKeyid(%d),pmlmeinfo->key_index(%d) ,recv key_id(%d)\n",
				 * psecuritypriv->dot118021XGrpKeyid,pmlmeinfo->key_index,rxdata_key_idx);
				 */

				if (psecuritypriv->binstallGrpkey == _FALSE) {
					res=_FAIL;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n"));
					DBG_871X("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n");
					goto exit;
				}
			} else{
				mickey=&stainfo->dot11tkiprxmickey.skey[0];
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n recvframe_chkmic: unicast key \n"));
			}

			datalen=precvframe->u.hdr.len-prxattrib->hdrlen-prxattrib->iv_len-prxattrib->icv_len-8;//icv_len included the mic code
			pframe=precvframe->u.hdr.rx_data;
			payload=pframe+prxattrib->hdrlen+prxattrib->iv_len;

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n prxattrib->iv_len=%d prxattrib->icv_len=%d\n",prxattrib->iv_len,prxattrib->icv_len));

			/*
			 * rtw_seccalctkipmic(&stainfo->dot11tkiprxmickey.skey[0],pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data
			 */

			rtw_seccalctkipmic(mickey,pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); //care the length of the data

			pframemic=payload+datalen;

			bmic_err=_FALSE;

			for (i = 0; i < 8; i++) {
				if (miccode[i] != *(pframemic+i)){
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chkmic:miccode[%d](%02x) != *(pframemic+%d)(%02x) ",i,miccode[i],i,*(pframemic+i)));
					bmic_err=_TRUE;
				}
			}


			if (bmic_err == _TRUE) {
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n *(pframemic-8)-*(pframemic-1)=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
					*(pframemic-8),*(pframemic-7),*(pframemic-6),*(pframemic-5),*(pframemic-4),*(pframemic-3),*(pframemic-2),*(pframemic-1)));
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n *(pframemic-16)-*(pframemic-9)=0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
					*(pframemic-16),*(pframemic-15),*(pframemic-14),*(pframemic-13),*(pframemic-12),*(pframemic-11),*(pframemic-10),*(pframemic-9)));

				{
					uint i;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n ======demp packet (len=%d)======\n",precvframe->u.hdr.len));
					for(i=0;i<precvframe->u.hdr.len;i=i+8){
						RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x",
							*(precvframe->u.hdr.rx_data+i),*(precvframe->u.hdr.rx_data+i+1),
							*(precvframe->u.hdr.rx_data+i+2),*(precvframe->u.hdr.rx_data+i+3),
							*(precvframe->u.hdr.rx_data+i+4),*(precvframe->u.hdr.rx_data+i+5),
							*(precvframe->u.hdr.rx_data+i+6),*(precvframe->u.hdr.rx_data+i+7)));
					}
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n ======demp packet end [len=%d]======\n",precvframe->u.hdr.len));
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("\n hrdlen=%d, \n",prxattrib->hdrlen));
				}

				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("ra=0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x psecuritypriv->binstallGrpkey=%d ",
					prxattrib->ra[0],prxattrib->ra[1],prxattrib->ra[2],
					prxattrib->ra[3],prxattrib->ra[4],prxattrib->ra[5],psecuritypriv->binstallGrpkey));

				/*
				 * double check key_index for some timing issue ,
				 * cannot compare with psecuritypriv->dot118021XGrpKeyid also cause timing issue
				 */

				if((IS_MCAST(prxattrib->ra)==_TRUE)  && (prxattrib->key_index != pmlmeinfo->key_index ))
					brpt_micerror = _FALSE;

				if((prxattrib->bdecrypted ==_TRUE)&& (brpt_micerror == _TRUE)) {
					rtw_handle_tkip_mic_err(adapter,(uint8_t)IS_MCAST(prxattrib->ra));
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
					DBG_871X(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
				} else {
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" mic error :prxattrib->bdecrypted=%d ",prxattrib->bdecrypted));
					DBG_871X(" mic error :prxattrib->bdecrypted=%d\n",prxattrib->bdecrypted);
				}

				res=_FAIL;

			} else {
				/* mic checked ok */
				if((psecuritypriv->bcheck_grpkey ==_FALSE)&&(IS_MCAST(prxattrib->ra)==_TRUE)){
					psecuritypriv->bcheck_grpkey =_TRUE;
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("psecuritypriv->bcheck_grpkey =_TRUE"));
				}
			}

		} else 	{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chkmic: rtw_get_stainfo==NULL!!!\n"));
		}

		recvframe_pull_tail(precvframe, 8);

	}

exit:

	return res;

}

//decrypt and set the ivlen,icvlen of the recv_frame
union recv_frame * decryptor(struct _ADAPTER *padapter,union recv_frame *precv_frame);
union recv_frame * decryptor(struct _ADAPTER *padapter,union recv_frame *precv_frame)
{

	struct rx_pkt_attrib *prxattrib = &precv_frame->u.hdr.attrib;
	struct security_priv *psecuritypriv=&padapter->securitypriv;
	union recv_frame *return_packet=precv_frame;
	uint32_t	 res=_SUCCESS;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("prxstat->decrypted=%x prxattrib->encrypt = 0x%03x\n",prxattrib->bdecrypted,prxattrib->encrypt));

	if (prxattrib->encrypt>0) {
		uint8_t *iv = precv_frame->u.hdr.rx_data+prxattrib->hdrlen;
		prxattrib->key_index = ( ((iv[3])>>6)&0x3) ;

		if(prxattrib->key_index > WEP_KEYS) {
			DBG_871X("prxattrib->key_index(%d) > WEP_KEYS \n", prxattrib->key_index);

			switch(prxattrib->encrypt){
			case _WEP40_:
			case _WEP104_:
				prxattrib->key_index = psecuritypriv->dot11PrivacyKeyIndex;
				break;
			case _TKIP_:
			case _AES_:
			default:
				prxattrib->key_index = psecuritypriv->dot118021XGrpKeyid;
				break;
			}
		}
	}

	if ((prxattrib->encrypt>0) && ((prxattrib->bdecrypted==0) ||(psecuritypriv->sw_decrypt==_TRUE))) {

		psecuritypriv->hw_decrypted=_FALSE;

#ifdef DBG_RX_DECRYPTOR
		DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n"
			, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
#endif

		switch (prxattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
			rtw_wep_decrypt(padapter, (uint8_t *)precv_frame);
			break;
		case _TKIP_:
			res = rtw_tkip_decrypt(padapter, (uint8_t *)precv_frame);
			break;
		case _AES_:
			res = rtw_aes_decrypt(padapter, (uint8_t * )precv_frame);
			break;
		default:
				break;
		}
	} else if(prxattrib->bdecrypted==1
		&& prxattrib->encrypt >0
		&& (psecuritypriv->busetkipkey==1 || prxattrib->encrypt !=_TKIP_ )
		)
	{
#if 0
		if((prxstat->icv==1)&&(prxattrib->encrypt!=_AES_))
		{
			psecuritypriv->hw_decrypted=_FALSE;

			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("psecuritypriv->hw_decrypted=_FALSE"));

			rtw_free_recvframe(precv_frame, &padapter->recvpriv.free_recv_queue);

			return_packet=NULL;

		}
		else
#endif
		{
			psecuritypriv->hw_decrypted=_TRUE;
			#ifdef DBG_RX_DECRYPTOR
			DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n"
			, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
			#endif

		}
	} else {
		#ifdef DBG_RX_DECRYPTOR
		DBG_871X("prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  psecuritypriv->hw_decrypted:%d\n"
		, prxattrib->bdecrypted ,prxattrib->encrypt, psecuritypriv->hw_decrypted);
		#endif
	}

	if(res == _FAIL) {
		rtw_free_recvframe(return_packet,&padapter->recvpriv.free_recv_queue);
		return_packet = NULL;

	}
	//recvframe_chkmic(adapter, precv_frame);   //move to recvframme_defrag function

	return return_packet;

}
//###set the security information in the recv_frame
union recv_frame *portctrl(struct _ADAPTER *adapter,union recv_frame * precv_frame);
union recv_frame * portctrl(struct _ADAPTER *adapter,union recv_frame * precv_frame)
{
	uint8_t   *psta_addr, *ptr;
	uint  auth_alg;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv ;
	union recv_frame *prtnframe;
	uint16_t	ether_type=0;
	uint16_t  eapol_type = 0x888e;//for Funia BD's WPA issue
	struct rx_pkt_attrib *pattrib;

	pstapriv = &adapter->stapriv;
	psta = rtw_get_stainfo(pstapriv, psta_addr);

	auth_alg = adapter->securitypriv.dot11AuthAlgrthm;

	ptr = get_recvframe_data(precv_frame);
	pfhdr = &precv_frame->u.hdr;
	pattrib = &pfhdr->attrib;
	psta_addr = pattrib->ta;

	prtnframe = NULL;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:adapter->securitypriv.dot11AuthAlgrthm=%d\n",adapter->securitypriv.dot11AuthAlgrthm));

	if (auth_alg == 2) {
		if ((psta != NULL) && (psta->ieee8021x_blocked)) {
			/*
			 * blocked
			 * only accept EAPOL frame
			 */
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:psta->ieee8021x_blocked==1\n"));

			prtnframe=precv_frame;

			/* get ether_type */
			ptr=ptr+pfhdr->attrib.hdrlen+pfhdr->attrib.iv_len+LLC_HEADER_SIZE;
			memcpy(&ether_type,ptr, 2);
			ether_type= ntohs((unsigned short )ether_type);

		        if (ether_type == eapol_type) {
				prtnframe=precv_frame;
			} else {
				//free this frame
				rtw_free_recvframe(precv_frame, &adapter->recvpriv.free_recv_queue);
				prtnframe=NULL;
			}
		} else {
			/*
			 * allowed
			 * check decryption status, and decrypt the frame if needed
			 */

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:psta->ieee8021x_blocked==0\n"));
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl:precv_frame->hdr.attrib.privacy=%x\n",precv_frame->u.hdr.attrib.privacy));

			if (pattrib->bdecrypted == 0) {
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("portctrl:prxstat->decrypted=%x\n", pattrib->bdecrypted));
			}

			prtnframe=precv_frame;
			/* check is the EAPOL frame or not (Rekey) */
			if (ether_type == eapol_type) {

				RT_TRACE(_module_rtl871x_recv_c_,_drv_notice_,("########portctrl:ether_type == 0x888e\n"));
				/* check Rekey */

				prtnframe=precv_frame;
			} else {
				RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("########portctrl:ether_type=0x%04x\n", ether_type));
			}
		}
	} else {
		prtnframe=precv_frame;
	}

	return prtnframe;

}

sint recv_decache(union recv_frame *precv_frame, uint8_t bretry, struct stainfo_rxcache *prxcache);
sint recv_decache(union recv_frame *precv_frame, uint8_t bretry, struct stainfo_rxcache *prxcache)
{
	sint tid = precv_frame->u.hdr.attrib.priority;

	uint16_t seq_ctrl = ( (precv_frame->u.hdr.attrib.seq_num&0xffff) << 4) |
		(precv_frame->u.hdr.attrib.frag_num & 0xf);

	if (tid > 15) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("recv_decache, (tid>15)! seq_ctrl=0x%x, tid=0x%x\n", seq_ctrl, tid));

		return _FAIL;
	}

	if (1) { 	/* if(bretry) */
		if(seq_ctrl == prxcache->tid_rxseq[tid]) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("recv_decache, seq_ctrl=0x%x, tid=0x%x, tid_rxseq=0x%x\n", seq_ctrl, tid, prxcache->tid_rxseq[tid]));

			return _FAIL;
		}
	}

	prxcache->tid_rxseq[tid] = seq_ctrl;

	return _SUCCESS;

}

void process_pwrbit_data(struct _ADAPTER *padapter, union recv_frame *precv_frame);
void process_pwrbit_data(struct _ADAPTER *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	unsigned char pwrbit;
	uint8_t *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta=NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);

	pwrbit = GetPwrMgt(ptr);

	if (psta) {
		if (pwrbit) {
			if (!(psta->state & WIFI_SLEEP_STATE)) {
				/*
				 * psta->state |= WIFI_SLEEP_STATE;
				 * pstapriv->sta_dz_bitmap |= BIT(psta->aid);
				 */

				stop_sta_xmit(padapter, psta);

				/* DBG_871X("to sleep, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap); */
			}
		} else {
			if (psta->state & WIFI_SLEEP_STATE) {
				/*
				 * psta->state ^= WIFI_SLEEP_STATE;
				 * pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);
				 */

				 wakeup_sta_to_xmit(padapter, psta);

				/*
				 *DBG_871X("to wakeup, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap);
				 */

			}
		}

	}

#endif
}

void process_wmmps_data(struct _ADAPTER *padapter, union recv_frame *precv_frame);
void process_wmmps_data(struct _ADAPTER *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta=NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);

	if (!psta)
		return;

#ifdef CONFIG_TDLS
	if( !(psta->tdls_sta_state & TDLS_LINKED_STATE ) )
	{
#endif

	if(!psta->qos_option)
		return;

	if(!(psta->qos_info&0xf))
		return;

#ifdef CONFIG_TDLS
	}
#endif

	if (psta->state & WIFI_SLEEP_STATE) {
		uint8_t wmmps_ac=0;

		switch (pattrib->priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk&BIT(1);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi&BIT(1);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo&BIT(1);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be&BIT(1);
			break;
		}

		if (wmmps_ac) {
			if (psta->sleepq_ac_len>0) {
				/* process received triggered frame */
				xmit_delivery_enabled_frames(padapter, psta);
			} else {
				/* issue one qos null frame with More data bit = 0 and the EOSP bit set (=1) */
				issue_qos_nulldata(padapter, psta->hwaddr, (uint16_t)pattrib->priority, 0, 0);
			}
		}

	}


#endif

}

#ifdef CONFIG_TDLS
sint OnTDLS(struct _ADAPTER *adapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	uint8_t *paction = get_recvframe_data(precv_frame);
	uint8_t category_field = 1;
	struct tdls_info *ptdlsinfo = &(adapter->tdlsinfo);

	//point to action field
	paction+=pattrib->hdrlen
			+ pattrib->iv_len
			+ SNAP_SIZE
			+ ETH_TYPE_LEN
			+ PAYLOAD_TYPE_LEN
			+ category_field;

	if(ptdlsinfo->enable == 0) {
		DBG_871X("recv tdls frame, "
				"but tdls haven't enabled\n");
		ret = _FAIL;
		return ret;
	}

	switch (*paction) {
	case TDLS_SETUP_REQUEST:
		DBG_871X("recv tdls setup request frame\n");
		ret=On_TDLS_Setup_Req(adapter, precv_frame);
		break;
	case TDLS_SETUP_RESPONSE:
		DBG_871X("recv tdls setup response frame\n");
		ret=On_TDLS_Setup_Rsp(adapter, precv_frame);
		break;
	case TDLS_SETUP_CONFIRM:
		DBG_871X("recv tdls setup confirm frame\n");
		ret=On_TDLS_Setup_Cfm(adapter, precv_frame);
		break;
	case TDLS_TEARDOWN:
		DBG_871X("recv tdls teardown, free sta_info\n");
		ret=On_TDLS_Teardown(adapter, precv_frame);
		break;
	case TDLS_DISCOVERY_REQUEST:
		DBG_871X("recv tdls discovery request frame\n");
		ret=On_TDLS_Dis_Req(adapter, precv_frame);
		break;
	case TDLS_PEER_TRAFFIC_RESPONSE:
		DBG_871X("recv tdls peer traffic response frame\n");
		ret=On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame);
		break;
	case TDLS_CHANNEL_SWITCH_REQUEST:
		DBG_871X("recv tdls channel switch request frame\n");
		ret=On_TDLS_Ch_Switch_Req(adapter, precv_frame);
		break;
	case TDLS_CHANNEL_SWITCH_RESPONSE:
		DBG_871X("recv tdls channel switch response frame\n");
		ret=On_TDLS_Ch_Switch_Rsp(adapter, precv_frame);
		break;
	default:
		DBG_871X("receive TDLS frame but not supported\n");
		ret=_FAIL;
		break;
	}

exit:
	return ret;

}
#endif

void count_rx_stats(struct _ADAPTER *padapter, union recv_frame *prframe, struct sta_info*sta);
void count_rx_stats(struct _ADAPTER *padapter, union recv_frame *prframe, struct sta_info*sta)
{
	int	sz;
	struct sta_info		*psta = NULL;
	struct stainfo_stats	*pstats = NULL;
	struct rx_pkt_attrib	*pattrib = & prframe->u.hdr.attrib;
	struct recv_priv		*precvpriv = &padapter->recvpriv;

	sz = get_recvframe_len(prframe);
	precvpriv->rx_bytes += sz;

	padapter->mlmepriv.LinkDetectInfo.NumRxOkInPeriod++;

	if( (!MacAddr_isBcst(pattrib->dst)) && (!IS_MCAST(pattrib->dst))){
		padapter->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod++;
	}

	if(sta)
		psta = sta;
	else
		psta = prframe->u.hdr.psta;

	if(psta) {
		pstats = &psta->sta_stats;

		pstats->rx_data_pkts++;
		pstats->rx_bytes += sz;
	}

}

sint sta2sta_data_frame(
	struct _ADAPTER *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta
);

sint sta2sta_data_frame(
	struct _ADAPTER *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta)
{
	uint8_t *ptr = precv_frame->u.hdr.rx_data;
	sint ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	uint8_t *mybssid  = get_bssid(pmlmepriv);
	uint8_t *myhwaddr = myid(&adapter->eeprompriv);
	uint8_t * sta_addr = NULL;
	sint bmcast = IS_MCAST(pattrib->dst);

#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
	struct sta_info *ptdls_sta=NULL;
	uint8_t *psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	/* frame body located after [+2]: ether-type, [+1]: payload type */
	uint8_t *pframe_body = psnap_type+2+1;
#endif

_func_enter_;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE))
	{

		// filter packets that SA is myself or multicast or broadcast
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
			ret= _FAIL;
			goto exit;
		}

		if( (!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))	&& (!bmcast) ){
			ret= _FAIL;
			goto exit;
		}

		if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		   (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) ) {
			ret= _FAIL;
			goto exit;
		}

		sta_addr = pattrib->src;

	} else if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	{
#ifdef CONFIG_TDLS

		//direct link data transfer
		if(ptdlsinfo->setup_state == TDLS_LINKED_STATE){
			ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->src);
			if(ptdls_sta==NULL) {
				ret=_FAIL;
				goto exit;
			} else if(ptdls_sta->tdls_sta_state&TDLS_LINKED_STATE) {
				//drop QoS-SubType Data, including QoS NULL, excluding QoS-Data
				if( (GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE )== WIFI_QOS_DATA_TYPE) {
					if(GetFrameSubType(ptr)&(BIT(4)|BIT(5)|BIT(6))) {
						DBG_871X("drop QoS-Sybtype Data\n");
						ret= _FAIL;
						goto exit;
					}
				}
				// filter packets that SA is myself or multicast or broadcast
				if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
					ret= _FAIL;
					goto exit;
				}
				// da should be for me
				if((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast)) {
					ret= _FAIL;
					goto exit;
				}
				// check BSSID
				if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				     _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				     (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) )
				{
					ret= _FAIL;
					goto exit;
				}

				//process UAPSD tdls sta
				process_pwrbit_data(adapter, precv_frame);

				// if NULL-frame, check pwrbit
				if ((GetFrameSubType(ptr)) == WIFI_DATA_NULL) {
					//NULL-frame with pwrbit=1, buffer_STA should buffer frames for sleep_STA
					if(GetPwrMgt(ptr)) {
						DBG_871X("TDLS: recv peer null frame with pwr bit 1\n");
						ptdls_sta->tdls_sta_state|=TDLS_PEER_SLEEP_STATE;
					// it would be triggered when we are off channel and receiving NULL DATA
					// we can confirm that peer STA is at off channel
					}
					else if(ptdls_sta->tdls_sta_state&TDLS_CH_SWITCH_ON_STATE)
					{
						if((ptdls_sta->tdls_sta_state & TDLS_PEER_AT_OFF_STATE) != TDLS_PEER_AT_OFF_STATE)
						{
							issue_nulldata_to_TDLS_peer_STA(adapter, ptdls_sta, 0);
							ptdls_sta->tdls_sta_state |= TDLS_PEER_AT_OFF_STATE;
							On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame);
						}
					}

					ret= _FAIL;
					goto exit;
				}
				//receive some of all TDLS management frames, process it at ON_TDLS
				if((_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, 2))){
					ret= OnTDLS(adapter, precv_frame);
					goto exit;
				}

			}

			sta_addr = pattrib->src;

		}
		else
#endif //CONFIG_TDLS
		{
			// For Station mode, sa and bssid should always be BSSID, and DA is my mac-address
			if(!_rtw_memcmp(pattrib->bssid, pattrib->src, ETH_ALEN) )
			{
				RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("bssid != TA under STATION_MODE; drop pkt\n"));
				ret= _FAIL;
				goto exit;
		}

		sta_addr = pattrib->bssid;
		}

	}
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
		if (bmcast) {
			// For AP mode, if DA == MCAST, then BSSID should be also MCAST
			if (!IS_MCAST(pattrib->bssid)){
					ret= _FAIL;
					goto exit;
			}
		} else { // not mc-frame
			// For AP mode, if DA is non-MCAST, then it must be BSSID, and bssid == BSSID
			if(!_rtw_memcmp(pattrib->bssid, pattrib->dst, ETH_ALEN)) {
				ret= _FAIL;
				goto exit;
			}

			sta_addr = pattrib->src;
		}

	} else if(check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) {
		memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		sta_addr = mybssid;
	} else {
		ret  = _FAIL;
	}



	if(bmcast)
		*psta = rtw_get_bcmc_stainfo(adapter);
	else
		*psta = rtw_get_stainfo(pstapriv, sta_addr); // get ap_info

#ifdef CONFIG_TDLS
	if(ptdls_sta != NULL)
		*psta = ptdls_sta;
#endif //CONFIG_TDLS

	if (*psta == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under sta2sta_data_frame ; drop pkt\n"));
#ifdef CONFIG_MP_INCLUDED
		if (adapter->registrypriv.mp_mode == 1) {
			if(check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
			adapter->mppriv.rx_pktloss++;
		}
#endif
		ret= _FAIL;
		goto exit;
	}

exit:
_func_exit_;
	return ret;

}

sint ap2sta_data_frame(
	struct _ADAPTER *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta );
sint ap2sta_data_frame(
	struct _ADAPTER *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta )
{
	uint8_t *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	uint8_t *mybssid  = get_bssid(pmlmepriv);
	uint8_t *myhwaddr = myid(&adapter->eeprompriv);
	sint bmcast = IS_MCAST(pattrib->dst);

_func_enter_;

	if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		&& (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE
			|| check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE	)
		)
	{

		/*  filter packets that SA is myself or multicast or broadcast */
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)){
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" SA==myself \n"));
			ret= _FAIL;
			goto exit;
		}

		/* da should be for me */
		if((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))&& (!bmcast))
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,
				(" ap2sta_data_frame:  compare DA fail; DA="MAC_FMT"\n", MAC_ARG(pattrib->dst)));
			ret= _FAIL;
			goto exit;
		}


		/* check BSSID */
		if( _rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		     (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) )
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,
				(" ap2sta_data_frame:  compare BSSID fail ; BSSID="MAC_FMT"\n", MAC_ARG(pattrib->bssid)));
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("mybssid="MAC_FMT"\n", MAC_ARG(mybssid)));

			if(!bmcast)
			{
				DBG_871X("issue_deauth to the nonassociated ap=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->bssid));
				issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}

			ret= _FAIL;
			goto exit;
		}

		if(bmcast)
			*psta = rtw_get_bcmc_stainfo(adapter);
		else
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get ap_info

		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("ap2sta: can't get psta under STATION_MODE ; drop pkt\n"));
			ret= _FAIL;
			goto exit;
		}

		if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
		}

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}

	} else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
		  && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) {
		memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		//
		memcpy(pattrib->bssid,  mybssid, ETH_ALEN);


		*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get sta_info
		if (*psta == NULL) {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under MP_MODE ; drop pkt\n"));
			ret= _FAIL;
			goto exit;
		}


	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
		/* Special case */
		ret = RTW_RX_HANDLED;
		goto exit;
	} else {
		if(_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN)&& (!bmcast)) {
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); // get sta_info
			if (*psta == NULL) {
				DBG_871X("issue_deauth to the ap=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->bssid));

				issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}
		}

		ret = _FAIL;
	}

exit:

_func_exit_;

	return ret;

}

sint sta2ap_data_frame(
	struct _ADAPTER *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta );
sint sta2ap_data_frame(
	struct _ADAPTER *adapter,
	union recv_frame *precv_frame,
	struct sta_info**psta )
{
	uint8_t *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;
	struct	sta_priv 		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	unsigned char *mybssid  = get_bssid(pmlmepriv);
	sint ret=_SUCCESS;

_func_enter_;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		//For AP mode, RA=BSSID, TX=STA(SRC_ADDR), A3=DST_ADDR
		if(!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN))
		{
			ret= _FAIL;
			goto exit;
		}

		*psta = rtw_get_stainfo(pstapriv, pattrib->src);
		if (*psta == NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("can't get psta under AP_MODE; drop pkt\n"));
			DBG_871X("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));

			issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);

			ret = RTW_RX_HANDLED;
			goto exit;
		}

		process_pwrbit_data(adapter, precv_frame);

		if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
			process_wmmps_data(adapter, precv_frame);
		}

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}
	}
	else {
		uint8_t *myhwaddr = myid(&adapter->eeprompriv);
		if (!_rtw_memcmp(pattrib->ra, myhwaddr, ETH_ALEN)) {
			ret = RTW_RX_HANDLED;
			goto exit;
		}
		DBG_871X("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));
		issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
		ret = RTW_RX_HANDLED;
		goto exit;
	}

exit:

_func_exit_;

	return ret;

}

sint validate_recv_ctrl_frame(struct _ADAPTER *padapter, union recv_frame *precv_frame);
sint validate_recv_ctrl_frame(struct _ADAPTER *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	uint8_t *pframe = precv_frame->u.hdr.rx_data;

	/* uint len = precv_frame->u.hdr.len; */

	/* DBG_871X("+validate_recv_ctrl_frame\n"); */

	if (GetFrameType(pframe) != WIFI_CTRL_TYPE)
	{
		return _FAIL;
	}

	/* receive the frames that ra(a1) is my address */
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN))
	{
		return _FAIL;
	}

	/* only handle ps-poll */
	if (GetFrameSubType(pframe) == WIFI_PSPOLL)
	{
		uint16_t aid;
		uint8_t wmmps_ac=0;
		struct sta_info *psta=NULL;

		aid = GetAid(pframe);
		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));

		if ((psta==NULL) || (psta->aid!=aid)) {
			return _FAIL;
		}

		/* for rx pkt statistics */
		psta->sta_stats.rx_ctrl_pkts++;

		switch(pattrib->priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk&BIT(0);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi&BIT(0);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo&BIT(0);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be&BIT(0);
			break;
		}

		if(wmmps_ac)
			return _FAIL;

		if(psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			DBG_871X("%s alive check-rx ps-poll\n", __func__);
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		if ((psta->state&WIFI_SLEEP_STATE)
		   && (pstapriv->sta_dz_bitmap&BIT(psta->aid))) {
			_irqL irqL;
			struct list_head	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe=NULL;
			struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

			/* _enter_critical_bh(&psta->sleep_q.lock, &irqL);*/
			_enter_critical_bh(&pxmitpriv->lock, &irqL);

			xmitframe_phead = get_list_head(&psta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			if ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

				xmitframe_plist = get_next(xmitframe_plist);

				rtw_list_delete(&pxmitframe->list);

				psta->sleepq_len--;

				if(psta->sleepq_len>0)
					pxmitframe->attrib.mdata = 1;
                                else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered = 1;

	                        /*
	                         * DBG_871X("handling ps-poll, q_len=%d, tim=%x\n", psta->sleepq_len, pstapriv->tim_bitmap);
	                         */

#if 0
                                _exit_critical_bh(&psta->sleep_q.lock, &irqL);
				if (rtw_hal_xmit(padapter, pxmitframe) == _TRUE) {
					rtw_os_xmit_complete(padapter, pxmitframe);
				}
                                _enter_critical_bh(&psta->sleep_q.lock, &irqL);
#endif
				rtw_hal_xmitframe_enqueue(padapter, pxmitframe);

				if (psta->sleepq_len==0) {
					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					/*
					 * DBG_871X("after handling ps-poll, tim=%x\n", pstapriv->tim_bitmap);
					 */

					/* upate BCN for TIM IE */
					/* update_BCNTIM(padapter); */
					update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
				}

				/* _exit_critical_bh(&psta->sleep_q.lock, &irqL); */
				_exit_critical_bh(&pxmitpriv->lock, &irqL);
			} else {
				/* _exit_critical_bh(&psta->sleep_q.lock, &irqL); */
				_exit_critical_bh(&pxmitpriv->lock, &irqL);

				/* DBG_871X("no buffered packets to xmit\n"); */
				if (pstapriv->tim_bitmap&BIT(psta->aid)) {
					if(psta->sleepq_len==0) {
						DBG_871X("no buffered packets to xmit\n");

						/* issue nulldata with More data bit = 0 to indicate we have no buffered packets */
						issue_nulldata(padapter, psta->hwaddr, 0, 0, 0);
					} else {
						DBG_871X("error!psta->sleepq_len=%d\n", psta->sleepq_len);
						psta->sleepq_len=0;
					}

					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					/* upate BCN for TIM IE */
					/* update_BCNTIM(padapter); */
					update_beacon(padapter, _TIM_IE_, NULL, _FALSE);
				}

			}

		}

	}

#endif

	return _FAIL;

}

union recv_frame* recvframe_chk_defrag(PADAPTER padapter, union recv_frame *precv_frame);
sint validate_recv_mgnt_frame(PADAPTER padapter, union recv_frame *precv_frame);
sint validate_recv_mgnt_frame(PADAPTER padapter, union recv_frame *precv_frame)
{
	/* struct mlme_priv *pmlmepriv = &adapter->mlmepriv; */

	RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("+validate_recv_mgnt_frame\n"));

#if 0
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
#ifdef CONFIG_NATIVEAP_MLME
		mgt_dispatcher(padapter, precv_frame);
#else
		rtw_hostapd_mlme_rx(padapter, precv_frame);
#endif
	} else {
		mgt_dispatcher(padapter, precv_frame);
	}
#endif

	precv_frame = recvframe_chk_defrag(padapter, precv_frame);
	if (precv_frame == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,("%s: fragment packet\n",__FUNCTION__));
		return _SUCCESS;
	}

	{
		//for rx pkt statistics
		struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(precv_frame->u.hdr.rx_data));
		if (psta) {
			psta->sta_stats.rx_mgnt_pkts++;
			if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_BEACON)
				psta->sta_stats.rx_beacon_pkts++;
			else if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_PROBEREQ)
				psta->sta_stats.rx_probereq_pkts++;
			else if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_PROBERSP) {
				if (_rtw_memcmp(padapter->eeprompriv.mac_addr, GetAddr1Ptr(precv_frame->u.hdr.rx_data), ETH_ALEN) == _TRUE)
					psta->sta_stats.rx_probersp_pkts++;
				else if (is_broadcast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data))
					|| is_multicast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data)))
					psta->sta_stats.rx_probersp_bm_pkts++;
				else
					psta->sta_stats.rx_probersp_uo_pkts++;
			}
		}
	}

	mgt_dispatcher(padapter, precv_frame);

	return _SUCCESS;

}

sint validate_recv_data_frame(struct _ADAPTER *adapter, union recv_frame *precv_frame);
sint validate_recv_data_frame(struct _ADAPTER *adapter, union recv_frame *precv_frame)
{
	uint8_t bretry;
	uint8_t *psa, *pda, *pbssid;
	struct sta_info *psta = NULL;
	uint8_t *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*pattrib = & precv_frame->u.hdr.attrib;
	struct sta_priv 	*pstapriv = &adapter->stapriv;
	struct security_priv	*psecuritypriv = &adapter->securitypriv;
	sint ret = _SUCCESS;
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#endif

	bretry = GetRetry(ptr);
	pda = get_da(ptr);
	psa = get_sa(ptr);
	pbssid = get_hdr_bssid(ptr);

	if(pbssid == NULL){
		ret= _FAIL;
		goto exit;
	}

	memcpy(pattrib->dst, pda, ETH_ALEN);
	memcpy(pattrib->src, psa, ETH_ALEN);

	memcpy(pattrib->bssid, pbssid, ETH_ALEN);

	switch(pattrib->to_fr_ds) {
	case 0:
		memcpy(pattrib->ra, pda, ETH_ALEN);
		memcpy(pattrib->ta, psa, ETH_ALEN);
		ret = sta2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case 1:
		memcpy(pattrib->ra, pda, ETH_ALEN);
		memcpy(pattrib->ta, pbssid, ETH_ALEN);
		ret = ap2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case 2:
		memcpy(pattrib->ra, pbssid, ETH_ALEN);
		memcpy(pattrib->ta, psa, ETH_ALEN);
		ret = sta2ap_data_frame(adapter, precv_frame, &psta);
		break;

	case 3:
		memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
		ret =_FAIL;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" case 3\n"));
		break;

	default:
		ret =_FAIL;
		break;
	}

	if(ret ==_FAIL){
		goto exit;
	} else if (ret == RTW_RX_HANDLED) {
		goto exit;
	}


	if(psta==NULL){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,(" after to_fr_ds_chk; psta==NULL \n"));
		ret= _FAIL;
		goto exit;
	}

	/*
	 * psta->rssi = prxcmd->rssi;
	 * psta->signal_quality= prxcmd->sq;
	 */

	precv_frame->u.hdr.psta = psta;


	pattrib->amsdu=0;
	pattrib->ack_policy = 0;
	/* parsing QC field */
	if (pattrib->qos == 1) {
		pattrib->priority = GetPriority((ptr + 24));
		pattrib->ack_policy = GetAckpolicy((ptr + 24));
		pattrib->amsdu = GetAMsdu((ptr + 24));
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 32 : 26;

		if(pattrib->priority!=0 && pattrib->priority!=3) {
			adapter->recvpriv.bIsAnyNonBEPkts = _TRUE;
		}
	} else {
		pattrib->priority=0;
		pattrib->hdrlen = pattrib->to_fr_ds==3 ? 30 : 24;
	}


	if(pattrib->order) {//HT-CTRL 11n
		pattrib->hdrlen += 4;
	}

	precv_frame->u.hdr.preorder_ctrl = &psta->recvreorder_ctrl[pattrib->priority];

	/* decache, drop duplicate recv packets */
	if(recv_decache(precv_frame, bretry, &psta->sta_recvpriv.rxcache) == _FAIL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("decache : drop pkt\n"));
		ret= _FAIL;
		goto exit;
	}

#if 0
	if(psta->tdls_sta_state & TDLS_LINKED_STATE ) {
		if(psta->dot118021XPrivacy==_AES_)
			pattrib->encrypt=psta->dot118021XPrivacy;
	}
#endif

	if(pattrib->privacy){

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("validate_recv_data_frame:pattrib->privacy=%x\n", pattrib->privacy));
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n ^^^^^^^^^^^IS_MCAST(pattrib->ra(0x%02x))=%d^^^^^^^^^^^^^^^6\n", pattrib->ra[0],IS_MCAST(pattrib->ra)));

#ifdef CONFIG_TDLS
		if((psta->tdls_sta_state & TDLS_LINKED_STATE) && (psta->dot118021XPrivacy==_AES_))
		{
			pattrib->encrypt=psta->dot118021XPrivacy;
		}
		else
#endif
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, IS_MCAST(pattrib->ra));

		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n pattrib->encrypt=%d\n",pattrib->encrypt));

		SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
	}
	else
	{
		pattrib->encrypt = 0;
		pattrib->iv_len = pattrib->icv_len = 0;
	}

exit:

	return ret;
}

sint validate_recv_frame(struct _ADAPTER *adapter, union recv_frame *precv_frame);
sint validate_recv_frame(struct _ADAPTER *adapter, union recv_frame *precv_frame)
{
	/* shall check frame subtype, to / from ds, da, bssid */

	/* then call check if rx seq/frag. duplicated. */

	uint8_t type;
	uint8_t subtype;
	sint retval = _SUCCESS;

	struct rx_pkt_attrib *pattrib = & precv_frame->u.hdr.attrib;

	uint8_t *ptr = precv_frame->u.hdr.rx_data;
	uint8_t  ver =(unsigned char) (*ptr)&0x3 ;
#ifdef CONFIG_FIND_BEST_CHANNEL
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
#endif

#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#endif

_func_enter_;


#ifdef CONFIG_FIND_BEST_CHANNEL
	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		int ch_set_idx = rtw_ch_set_search_ch(pmlmeext->channel_set, rtw_get_oper_ch(adapter));
		if (ch_set_idx >= 0)
			pmlmeext->channel_set[ch_set_idx].rx_count++;
	}
#endif

#ifdef CONFIG_TDLS
	if(ptdlsinfo->ch_sensing==1 && ptdlsinfo->cur_channel !=0){
		ptdlsinfo->collect_pkt_num[ptdlsinfo->cur_channel-1]++;
	}
#endif

	/* add version chk */
	if (ver != 0){
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail! (ver!=0)\n"));
		retval= _FAIL;
		goto exit;
	}

	type =  GetFrameType(ptr);
	subtype = GetFrameSubType(ptr); //bit(7)~bit(2)

	pattrib->to_fr_ds = get_tofr_ds(ptr);

	pattrib->frag_num = GetFragNum(ptr);
	pattrib->seq_num = GetSequence(ptr);

	pattrib->pw_save = GetPwrMgt(ptr);
	pattrib->mfrag = GetMFrag(ptr);
	pattrib->mdata = GetMData(ptr);
	pattrib->privacy = GetPrivacy(ptr);
	pattrib->order = GetOrder(ptr);

#if 1 //Dump rx packets
{
	uint8_t bDumpRxPkt;
	rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
	if(bDumpRxPkt ==1){//dump all rx packets
		int i;
		DBG_871X("############################# \n");

		for(i=0; i<64;i=i+8)
			DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
			*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
		DBG_871X("############################# \n");
	}
	else if(bDumpRxPkt ==2){
		if(type== WIFI_MGT_TYPE){
			int i;
			DBG_871X("############################# \n");

			for(i=0; i<64;i=i+8)
				DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
				*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
			DBG_871X("############################# \n");
		}
	}
	else if(bDumpRxPkt ==3){
		if(type== WIFI_DATA_TYPE){
			int i;
			DBG_871X("############################# \n");

			for(i=0; i<64;i=i+8)
				DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr+i),
				*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));
			DBG_871X("############################# \n");
		}
	}
}
#endif
	switch (type) {
	case WIFI_MGT_TYPE: /* mgnt */
		retval = validate_recv_mgnt_frame(adapter, precv_frame);
		if (retval == _FAIL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_mgnt_frame fail\n"));
		}
		retval = _FAIL; /* only data frame return _SUCCESS */
		break;
	case WIFI_CTRL_TYPE: //ctrl
		retval = validate_recv_ctrl_frame(adapter, precv_frame);
		if (retval == _FAIL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_ctrl_frame fail\n"));
		}
		retval = _FAIL; /* only data frame return _SUCCESS */
		break;
	case WIFI_DATA_TYPE: /* data */
		pattrib->qos = (subtype & BIT(7))? 1:0;
		retval = validate_recv_data_frame(adapter, precv_frame);
		if (retval == _FAIL)
		{
			struct recv_priv *precvpriv = &adapter->recvpriv;
			//RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail\n"));
			precvpriv->rx_drop++;
		}
		break;
	default:
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("validate_recv_data_frame fail! type=0x%x\n", type));
		retval = _FAIL;
		break;
	}

exit:

	return retval;
}


/* remove the wlanhdr and add the eth_hdr */
#if 1

sint wlanhdr_to_ethhdr ( union recv_frame *precvframe);
sint wlanhdr_to_ethhdr ( union recv_frame *precvframe)
{
	sint rmv_len;
	uint16_t eth_type, len;
	uint8_t	bsnaphdr;
	uint8_t	*psnap_type;
	struct ieee80211_snap_hdr *psnap;

	sint ret=_SUCCESS;
	struct _ADAPTER *adapter =precvframe->u.hdr.adapter;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	uint8_t	*ptr = get_recvframe_data(precvframe) ; /*  point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = & precvframe->u.hdr.attrib;

	if (pattrib->encrypt) {
		recvframe_pull_tail(precvframe, pattrib->icv_len);
	}

	psnap=(struct ieee80211_snap_hdr	*)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;

	/* convert hdr + possible LLC headers into Ethernet header */
	/* eth_type = (psnap_type[0] << 8) | psnap_type[1]; */

	if((_rtw_memcmp(psnap, rtw_rfc1042_header, SNAP_SIZE) &&
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == _FALSE) &&
		(_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2)==_FALSE) )||
		/* eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) || */
		 _rtw_memcmp(psnap, rtw_bridge_tunnel_header, SNAP_SIZE)){
		 /* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		bsnaphdr = _TRUE;
	}
	else {
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = _FALSE;
	}

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);
	len = precvframe->u.hdr.len - rmv_len;

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("\n===pattrib->hdrlen: %x,  pattrib->iv_len:%x ===\n\n", pattrib->hdrlen,  pattrib->iv_len));

	memcpy(&eth_type, ptr+rmv_len, 2);
	eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type
	pattrib->eth_type = eth_type;

#ifdef CONFIG_AUTO_AP_MODE
	if (0x8899 == pattrib->eth_type) {
		struct sta_info *psta = precvframe->u.hdr.psta;

		DBG_871X("wlan rx: got eth_type=0x%x\n", pattrib->eth_type);

		if (psta && psta->isrc && psta->pid>0) {
			uint16_t rx_pid;

			rx_pid = *(uint16_t *)(ptr+rmv_len+2);

			DBG_871X("wlan rx(pid=0x%x): sta("MAC_FMT") pid=0x%x\n",
				rx_pid, MAC_ARG(psta->hwaddr), psta->pid);

			if(rx_pid == psta->pid) {
				int i;
				uint16_t len = *(uint16_t *)(ptr+rmv_len+4);
				/* uint16_t ctrl_type = *(uint16_t *)(ptr+rmv_len+6); */

				/*
				 * DBG_871X("RC: len=0x%x, ctrl_type=0x%x\n", len, ctrl_type);
				 */

				DBG_871X("RC: len=0x%x\n", len);

				for(i=0;i<len;i++)
					DBG_871X("0x%x\n", *(ptr+rmv_len+6+i));
					//DBG_871X("0x%x\n", *(ptr+rmv_len+8+i));

				DBG_871X("RC-end\n");
			}
		}
	}
#endif

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)) {
		ptr += rmv_len ;
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		eth_type = 0x8712;
		/* append rx status for mp test packets */
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	} else {
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+ (bsnaphdr?2:0)));
	}

	memcpy(ptr, pattrib->dst, ETH_ALEN);
	memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	if(!bsnaphdr) {
		len = htons(len);
		memcpy(ptr+12, &len, 2);
	}

_func_exit_;
	return ret;

}

#else

sint wlanhdr_to_ethhdr ( union recv_frame *precvframe)
{
	sint rmv_len;
	uint16_t eth_type;
	uint8_t	bsnaphdr;
	uint8_t	*psnap_type;
	struct ieee80211_snap_hdr	*psnap;

	sint ret=_SUCCESS;
	struct _ADAPTER	*adapter =precvframe->u.hdr.adapter;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	uint8_t * ptr = get_recvframe_data(precvframe) ; /* point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = & precvframe->u.hdr.attrib;
	struct _vlan *pvlan = NULL;

_func_enter_;

	psnap=(struct ieee80211_snap_hdr *)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type=ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	if (psnap->dsap==0xaa && psnap->ssap==0xaa && psnap->ctrl==0x03) {
		if (_rtw_memcmp(psnap->oui, oui_rfc1042, WLAN_IEEE_OUI_LEN))
			bsnaphdr=_TRUE;		/*wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_RFC1042; */
		else if (_rtw_memcmp(psnap->oui, SNAP_HDR_APPLETALK_DDP, WLAN_IEEE_OUI_LEN) &&
			_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_DDP, 2) )
			bsnaphdr=_TRUE;		/* wlan_pkt_format = WLAN_PKT_FORMAT_APPLETALK; */
		else if (_rtw_memcmp( psnap->oui, oui_8021h, WLAN_IEEE_OUI_LEN))
			bsnaphdr=_TRUE;		/*wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_TUNNEL; */
		else {
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("drop pkt due to invalid frame format!\n"));
			ret= _FAIL;
			goto exit;
		}

	} else
		bsnaphdr=_FALSE;	/* wlan_pkt_format = WLAN_PKT_FORMAT_OTHERS; */

	rmv_len = pattrib->hdrlen + pattrib->iv_len +(bsnaphdr?SNAP_SIZE:0);
	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("===pattrib->hdrlen: %x,  pattrib->iv_len:%x ===\n", pattrib->hdrlen,  pattrib->iv_len));

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) {
		ptr += rmv_len ;
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		/* back to original pointer */
		ptr -= rmv_len;
	}

	ptr += rmv_len ;

	memcpy(&eth_type, ptr, 2);
	eth_type= ntohs((unsigned short )eth_type); //pattrib->ether_type
	ptr +=2;

	if(pattrib->encrypt){
		recvframe_pull_tail(precvframe, pattrib->icv_len);
	}

	if(eth_type == 0x8100) /* vlan */
	{
		pvlan = (struct _vlan *) ptr;

		/*
		 * eth_type = get_vlan_encap_proto(pvlan);
		 * eth_type = pvlan->h_vlan_encapsulated_proto;//?
		 */
		rmv_len += 4;
		ptr+=4;
	}

	if(eth_type==0x0800) { /* ip */
		/*
		 * struct iphdr*  piphdr = (struct iphdr*) ptr;
		 * __uint8_t tos = (unsigned char)(pattrib->priority & 0xff);
		 *
		 * piphdr->tos = tos;
		 *
		 * if (piphdr->protocol == 0x06) {
		 * 	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("@@@===recv tcp len:%d @@@===\n", precvframe->u.hdr.len));
		 * }
		 */

	} else if(eth_type==0x8712) { 	/* append rx status for mp test packets */
		/*
		 * ptr -= 16;
		 * memcpy(ptr, get_rxmem(precvframe), 16);
		 */
	} else {
	}

	if(eth_type==0x8712) { 	/*  append rx status for mp test packets */
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		memcpy(ptr, get_rxmem(precvframe), 24);
		ptr+=24;
	} else
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2));

	memcpy(ptr, pattrib->dst, ETH_ALEN);
	memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	eth_type = htons((unsigned short)eth_type) ;
	memcpy(ptr+12, &eth_type, 2);

exit:

	return ret;
}
#endif

/* perform defrag */

union recv_frame *recvframe_defrag(struct _ADAPTER *adapter,_queue *defrag_q);
union recv_frame *recvframe_defrag(struct _ADAPTER *adapter,_queue *defrag_q)
{
	struct list_head	 *plist, *phead;
	uint8_t	*data,wlanhdr_offset;
	uint8_t	curfragnum;
	struct recv_frame_hdr *pfhdr,*pnfhdr;
	union recv_frame* prframe, *pnextrframe;
	_queue	*pfree_recv_queue;


	curfragnum=0;
	pfree_recv_queue=&adapter->recvpriv.free_recv_queue;

	phead = get_list_head(defrag_q);
	plist = get_next(phead);
	prframe = LIST_CONTAINOR(plist, union recv_frame, u);
	pfhdr=&prframe->u.hdr;
	rtw_list_delete(&(prframe->u.list));

	if (curfragnum != pfhdr->attrib.frag_num) {
		/*
		 * the first fragment number must be 0
		 * free the whole queue
		 */
		rtw_free_recvframe(prframe, pfree_recv_queue);
		rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

		return NULL;
	}


	curfragnum++;

	plist= get_list_head(defrag_q);

	plist = get_next(plist);

	data=get_recvframe_data(prframe);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		pnextrframe = LIST_CONTAINOR(plist, union recv_frame , u);
		pnfhdr=&pnextrframe->u.hdr;

		/* check the fragment sequence  (2nd ~n fragment frame) */

		if(curfragnum!=pnfhdr->attrib.frag_num) {
			/*
			 * the fragment number must be increasing  (after decache)
			 * release the defrag_q & prframe
			 */
			rtw_free_recvframe(prframe, pfree_recv_queue);
			rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);
			return NULL;
		}

		curfragnum++;

		/*
		 * copy the 2nd~n fragment frame's payload to the first fragment
		 * get the 2nd~last fragment frame's payload
		 */

		wlanhdr_offset = pnfhdr->attrib.hdrlen + pnfhdr->attrib.iv_len;

		recvframe_pull(pnextrframe, wlanhdr_offset);

		/*
		 * append  to first fragment frame's tail (if privacy frame, pull the ICV)
		 */
		recvframe_pull_tail(prframe, pfhdr->attrib.icv_len);

		/* memcpy */
		memcpy(pfhdr->rx_tail, pnfhdr->rx_data, pnfhdr->len);

		recvframe_put(prframe, pnfhdr->len);

		pfhdr->attrib.icv_len=pnfhdr->attrib.icv_len;
		plist = get_next(plist);

	};

	/* free the defrag_q queue and return the prframe */
	rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Performance defrag!!!!!\n"));

	return prframe;
}

/* check if need to defrag, if needed queue the frame to defrag_q */
union recv_frame* recvframe_chk_defrag(PADAPTER padapter, union recv_frame *precv_frame)
{
	uint8_t	ismfrag;
	uint8_t	fragnum;
	uint8_t	*psta_addr;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv;
	struct list_head *phead;
	union recv_frame *prtnframe = NULL;
	_queue *pfree_recv_queue, *pdefrag_q;

	pstapriv = &padapter->stapriv;

	pfhdr = &precv_frame->u.hdr;

	pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

	/* need to define struct of wlan header frame ctrl */
	ismfrag = pfhdr->attrib.mfrag;
	fragnum = pfhdr->attrib.frag_num;

	psta_addr = pfhdr->attrib.ta;
	psta = rtw_get_stainfo(pstapriv, psta_addr);
	if (psta == NULL) {
		uint8_t type = GetFrameType(pfhdr->rx_data);
		if (type != WIFI_DATA_TYPE) {
			psta = rtw_get_bcmc_stainfo(padapter);
			pdefrag_q = &psta->sta_recvpriv.defrag_q;
		} else
			pdefrag_q = NULL;
	} else
		pdefrag_q = &psta->sta_recvpriv.defrag_q;

	if ((ismfrag==0) && (fragnum==0)) {
		prtnframe = precv_frame;	/* isn't a fragment frame */
	}

	if (ismfrag==1) {
		/*
		 * 0~(n-1) fragment frame
		 * enqueue to defraf_g
		 */

		if (pdefrag_q != NULL) {
			if (fragnum == 0) {
				/* the first fragment */
				if(_rtw_queue_empty(pdefrag_q) == _FALSE) {
					/* free current defrag_q */
					rtw_free_recvframe_queue(pdefrag_q, pfree_recv_queue);
				}
			}


			/* Then enqueue the 0~(n-1) fragment into the defrag_q */

			/* _rtw_spinlock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			rtw_list_insert_tail(&pfhdr->list, phead);
			/* _rtw_spinunlock(&pdefrag_q->lock); */

			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("Enqueuq: ismfrag = %d, fragnum= %d\n", ismfrag,fragnum));

			prtnframe=NULL;

		} else 	{
			/* can't find this ta's defrag_queue, so free this recv_frame */
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe=NULL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("Free because pdefrag_q ==NULL: ismfrag = %d, fragnum= %d\n", ismfrag, fragnum));
		}

	}

	if((ismfrag==0)&&(fragnum!=0)) {
		/*
		 * the last fragment frame
		 * enqueue the last fragment
		 */

		if (pdefrag_q != NULL) {
			/* _rtw_spinlock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			rtw_list_insert_tail(&pfhdr->list,phead);
			/* _rtw_spinunlock(&pdefrag_q->lock); */

			/* call recvframe_defrag to defrag */
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("defrag: ismfrag = %d, fragnum= %d\n", ismfrag, fragnum));
			precv_frame = recvframe_defrag(padapter, pdefrag_q);
			prtnframe=precv_frame;

		} else {
			/* can't find this ta's defrag_queue, so free this recv_frame */
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe=NULL;
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("Free because pdefrag_q ==NULL: ismfrag = %d, fragnum= %d\n", ismfrag,fragnum));
		}

	}

	if((prtnframe!=NULL)&&(prtnframe->u.hdr.attrib.privacy)) {
		/* after defrag we must check tkip mic code */
		if(recvframe_chkmic(padapter,  prtnframe)==_FAIL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chkmic(padapter,  prtnframe)==_FAIL\n"));
			rtw_free_recvframe(prtnframe,pfree_recv_queue);
			prtnframe=NULL;
		}
	}

	return prtnframe;
}

int amsdu_to_msdu(struct _ADAPTER *padapter, union recv_frame *prframe)
{
	int	a_len, padding_len;
	uint16_t	nSubframe_Length;
	uint8_t	nr_subframes, i;
	uint8_t	*pdata;
	_pkt *sub_pkt,*subframes[MAX_SUBFRAME_COUNT];
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &(precvpriv->free_recv_queue);
	int	ret = _SUCCESS;

	nr_subframes = 0;

	recvframe_pull(prframe, prframe->u.hdr.attrib.hdrlen);

	if (prframe->u.hdr.attrib.iv_len >0) {
		recvframe_pull(prframe, prframe->u.hdr.attrib.iv_len);
	}

	a_len = prframe->u.hdr.len;

	pdata = prframe->u.hdr.rx_data;

	while (a_len > ETH_HLEN) {
		/* Offset 12 denote 2 mac address */
		nSubframe_Length = RTW_GET_BE16(pdata + 12);

		if (a_len < (ETHERNET_HEADER_SIZE + nSubframe_Length) ) {
			DBG_871X("nRemain_Length is %d and nSubframe_Length is : %d\n",a_len,nSubframe_Length);
			break;
		}

		sub_pkt = rtw_os_alloc_msdu_pkt(prframe, nSubframe_Length, pdata);
		if (sub_pkt == NULL) {
			DBG_871X("%s(): allocate sub packet fail !!!\n",__FUNCTION__);
			break;
		}

		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;

		subframes[nr_subframes++] = sub_pkt;

		if (nr_subframes >= MAX_SUBFRAME_COUNT) {
			DBG_871X("ParseSubframe(): Too many Subframes! Packets dropped!\n");
			break;
		}

		pdata += nSubframe_Length;
		a_len -= nSubframe_Length;
		if (a_len != 0) {
			padding_len = 4 - ((nSubframe_Length + ETH_HLEN) & (4-1));
			if (padding_len == 4) {
				padding_len = 0;
			}

			if (a_len < padding_len) {
				DBG_871X("ParseSubframe(): a_len < padding_len !\n");
				break;
			}
			pdata += padding_len;
			a_len -= padding_len;
		}
	}

	for (i = 0; i < nr_subframes; i++){
		sub_pkt = subframes[i];

		/* Indicat the packets to upper layer */
		if (sub_pkt) {
			rtw_os_recv_indicate_pkt(padapter, sub_pkt, &prframe->u.hdr.attrib);
		}
	}

	prframe->u.hdr.len = 0;
	rtw_free_recvframe(prframe, pfree_recv_queue);//free this recv_frame

	return ret;
}

int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, uint16_t seq_num);
int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, uint16_t seq_num)
{
	uint8_t	wsize = preorder_ctrl->wsize_b;
	uint16_t	wend = (preorder_ctrl->indicate_seq + wsize -1) & 0xFFF;//% 4096;

	/*  Rx Reorder initialize condition. */
	if (preorder_ctrl->indicate_seq == 0xFFFF) {
		preorder_ctrl->indicate_seq = seq_num;

		/*
		 * DbgPrint("check_indicate_seq, 1st->indicate_seq=%d\n", precvpriv->indicate_seq);
		 */
	}

	/*
	 * DbgPrint("enter->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
	 */

	/* Drop out the packet which SeqNum is smaller than WinStart */
	if( SN_LESS(seq_num, preorder_ctrl->indicate_seq) ) {
		/*
		 * RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));
		 * DbgPrint("CheckRxTsIndicateSeq(): Packet Drop! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
		 */
		return _FALSE;
	}

	/*
	 * Sliding window manipulation. Conditions includes:
	 * 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	 * 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	 */

	if (SN_EQUAL(seq_num, preorder_ctrl->indicate_seq)) {
		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;
	} else if(SN_LESS(wend, seq_num)) {
		/*
		 * RT_TRACE(COMP_RX_REORDER, DBG_LOUD, ("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, NewSeqNum));
		 * DbgPrint("CheckRxTsIndicateSeq(): Window Shift! IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
		 */

		/* boundary situation, when seq_num cross 0xFFF */
		if(seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 -wsize;
		else
			preorder_ctrl->indicate_seq = 0xFFF - (wsize - (seq_num + 1)) + 1;

	}

	/*
	 * DbgPrint("exit->check_indicate_seq(): IndicateSeq: %d, NewSeq: %d\n", precvpriv->indicate_seq, seq_num);
	 */

	return _TRUE;
}

int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe);
int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct list_head	*phead, *plist;
	union recv_frame *pnextrframe;
	struct rx_pkt_attrib *pnextattrib;

	/* DbgPrint("+enqueue_reorder_recvframe()\n"); */

	/*
	 * _enter_critical_ex(&ppending_recvframe_queue->lock, &irql);
	 * _rtw_spinlock_ex(&ppending_recvframe_queue->lock);
	 */


	phead = get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		pnextrframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pnextattrib = &pnextrframe->u.hdr.attrib;

		if (SN_LESS(pnextattrib->seq_num, pattrib->seq_num)) {
			plist = get_next(plist);
		} else if( SN_EQUAL(pnextattrib->seq_num, pattrib->seq_num)) {
			/*
			 * Duplicate entry is found!! Do not insert current entry.
			 * RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("InsertRxReorderList(): Duplicate packet is dropped!! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum));
			 */

			/*
			 * _exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
			 */

			return _FALSE;
		} else {
			break;
		}

		/* DbgPrint("enqueue_reorder_recvframe():while\n"); */
	}


	/*
	 * _enter_critical_ex(&ppending_recvframe_queue->lock, &irql);
	 * _rtw_spinlock_ex(&ppending_recvframe_queue->lock);
	 */

	rtw_list_delete(&(prframe->u.hdr.list));

	rtw_list_insert_tail(&(prframe->u.hdr.list), plist);

	/*
	 * _rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
	 *_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
	 */


	/*
	 * RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("InsertRxReorderList(): Pkt insert into buffer!! IndicateSeq: %d, NewSeq: %d\n", pTS->RxIndicateSeq, SeqNum));
	 */
	return _TRUE;

}

int recv_indicatepkts_in_order(struct _ADAPTER *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced);
int recv_indicatepkts_in_order(struct _ADAPTER *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced)
{
	/* _irqL irql; */
	/* uint8_t bcancelled; */
	struct list_head	*phead, *plist;
	union recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	/* uint8_t index = 0; */
	int bPktInBuf = _FALSE;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	/* DbgPrint("+recv_indicatepkts_in_order\n"); */

	/*
	 * _enter_critical_ex(&ppending_recvframe_queue->lock, &irql);
	 * _rtw_spinlock_ex(&ppending_recvframe_queue->lock);
	 */

	phead = 	get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

#if 0
	/* Check if there is any other indication thread running. */
	if(pTS->RxIndicateState == RXTS_INDICATE_PROCESSING)
		return;
#endif

	/* Handling some condition for forced indicate case. */
	if (bforced==_TRUE) {
		if(rtw_is_list_empty(phead)) {
			/*
			 * _exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
			 * _rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
			 */
			return _TRUE;
		}

		 prframe = LIST_CONTAINOR(plist, union recv_frame, u);
	        pattrib = &prframe->u.hdr.attrib;
		preorder_ctrl->indicate_seq = pattrib->seq_num;
	}

	/*
	 *  Prepare indication list and indication.
	 * Check if there is any packet need indicate.
	 */

	while (!rtw_is_list_empty(phead)) {

		prframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pattrib = &prframe->u.hdr.attrib;

		if(!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
				 ("recv_indicatepkts_in_order: indicate=%d seq=%d amsdu=%d\n",
				  preorder_ctrl->indicate_seq, pattrib->seq_num, pattrib->amsdu));

#if 0
			/* This protect buffer from overflow. */
			if (index >= REORDER_WIN_SIZE) {
				RT_ASSERT(FALSE, ("IndicateRxReorderList(): Buffer overflow!! \n"));
				bPktInBuf = TRUE;
				break;
			}
#endif

			plist = get_next(plist);
			rtw_list_delete(&(prframe->u.hdr.list));

			if(SN_EQUAL(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
				preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;
			}

#if 0
			index++;
			if (index == 1) {
				/*
				 * Cancel previous pending timer.
				 * PlatformCancelTimer(Adapter, &pTS->RxPktPendingTimer);
				 */

				if(bforced!=_TRUE) {
					/*
					 * DBG_871X("_cancel_timer(&preorder_ctrl->reordering_ctrl_timer, &bcancelled);\n");
					 */
					_cancel_timer(&preorder_ctrl->reordering_ctrl_timer, &bcancelled);
				}
			}
#endif

			/*
			 * Set this as a lock to make sure that only one thread is indicating packet.
			 * pTS->RxIndicateState = RXTS_INDICATE_PROCESSING;
			 */

			/*
			 * Indicate packets
			 * RT_ASSERT((index<=REORDER_WIN_SIZE), ("RxReorderIndicatePacket(): Rx Reorder buffer full!! \n"));
			 */

			/*
			 * indicate this recv_frame
			 * DbgPrint("recv_indicatepkts_in_order, indicate_seq=%d, seq_num=%d\n", precvpriv->indicate_seq, pattrib->seq_num);
			 */

			if (!pattrib->amsdu) {
				/* DBG_871X("recv_indicatepkts_in_order, amsdu!=1, indicate_seq=%d, seq_num=%d\n", preorder_ctrl->indicate_seq, pattrib->seq_num); */

				if ((padapter->bDriverStopped == _FALSE)
				  && (padapter->bSurpriseRemoved == _FALSE)) {
					rtw_recv_indicatepkt(padapter, prframe);//indicate this recv_frame

				}
			} else if (pattrib->amsdu == 1) {
				if (amsdu_to_msdu(padapter, prframe) != _SUCCESS) {
					rtw_free_recvframe(prframe, &precvpriv->free_recv_queue);
				}
			} else 	{
				/* error condition; */
			}


			/* Update local variables. */
			bPktInBuf = _FALSE;

		} else {
			bPktInBuf = _TRUE;
			break;
		}

		/* DbgPrint("recv_indicatepkts_in_order():while\n"); */

	}

	/*
	 * _rtw_spinunlock_ex(&ppending_recvframe_queue->lock);
	 * _exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
	 */


/*
	//Release the indication lock and set to new indication step.
	if(bPktInBuf)
	{
		// Set new pending timer.
		//pTS->RxIndicateState = RXTS_INDICATE_REORDER;
		//PlatformSetTimer(Adapter, &pTS->RxPktPendingTimer, pHTInfo->RxReorderPendingTime);
		//DBG_871X("_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME)\n");
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
	}
	else
	{
		//pTS->RxIndicateState = RXTS_INDICATE_IDLE;
	}
*/
	/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */

	/* return _TRUE; */
	return bPktInBuf;

}

int recv_indicatepkt_reorder(struct _ADAPTER *padapter, union recv_frame *prframe);
int recv_indicatepkt_reorder(struct _ADAPTER *padapter, union recv_frame *prframe)
{
	_irqL irql;
	int retval = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_reorder_ctrl *preorder_ctrl = prframe->u.hdr.preorder_ctrl;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	if(!pattrib->amsdu)
	{
		//s1.
		wlanhdr_to_ethhdr(prframe);

		if ((pattrib->qos!=1) /*|| pattrib->priority!=0 || IS_MCAST(pattrib->ra)*/
			|| (pattrib->eth_type==0x0806) || (pattrib->ack_policy!=0))
		{
			if ((padapter->bDriverStopped == _FALSE) &&
			    (padapter->bSurpriseRemoved == _FALSE))
			{
				RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@  recv_indicatepkt_reorder -recv_func recv_indicatepkt\n" ));

				rtw_recv_indicatepkt(padapter, prframe);
				return _SUCCESS;

			}


			return _FAIL;

		}

		if (preorder_ctrl->enable == _FALSE)
		{
			//indicate this recv_frame
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			rtw_recv_indicatepkt(padapter, prframe);

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			return _SUCCESS;
		}

#ifndef CONFIG_RECV_REORDERING_CTRL
		//indicate this recv_frame
		rtw_recv_indicatepkt(padapter, prframe);
		return _SUCCESS;
#endif

	}
	else if(pattrib->amsdu==1) //temp filter -> means didn't support A-MSDUs in a A-MPDU
	{
		if (preorder_ctrl->enable == _FALSE)
		{
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			retval = amsdu_to_msdu(padapter, prframe);

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			if(retval != _SUCCESS){
			}

			return retval;
		}
	}
	else
	{

	}

	_enter_critical_bh(&ppending_recvframe_queue->lock, &irql);

	RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_,
		 ("recv_indicatepkt_reorder: indicate=%d seq=%d\n",
		  preorder_ctrl->indicate_seq, pattrib->seq_num));

	//s2. check if winstart_b(indicate_seq) needs to been updated
	if(!check_indicate_seq(preorder_ctrl, pattrib->seq_num))
	{
		//pHTInfo->RxReorderDropCounter++;
		//ReturnRFDList(Adapter, pRfd);
		//RT_TRACE(COMP_RX_REORDER, DBG_TRACE, ("RxReorderIndicatePacket() ==> Packet Drop!!\n"));
		//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
		//return _FAIL;

#if 0
		rtw_recv_indicatepkt(padapter, prframe);

		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

		goto _success_exit;
#else
		goto _err_exit;
#endif
	}


	//s3. Insert all packet into Reorder Queue to maintain its ordering.
	if(!enqueue_reorder_recvframe(preorder_ctrl, prframe))
	{
		//DbgPrint("recv_indicatepkt_reorder, enqueue_reorder_recvframe fail!\n");
		//_exit_critical_ex(&ppending_recvframe_queue->lock, &irql);
		//return _FAIL;
		goto _err_exit;
	}


	//s4.
	// Indication process.
	// After Packet dropping and Sliding Window shifting as above, we can now just indicate the packets
	// with the SeqNum smaller than latest WinStart and buffer other packets.
	//
	// For Rx Reorder condition:
	// 1. All packets with SeqNum smaller than WinStart => Indicate
	// 2. All packets with SeqNum larger than or equal to WinStart => Buffer it.
	//

	//recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE);
	if(recv_indicatepkts_in_order(padapter, preorder_ctrl, _FALSE)==_TRUE)
	{
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
	}
	else
	{
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
	}


_success_exit:

	return _SUCCESS;

_err_exit:

        _exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

	return _FAIL;
}


void rtw_reordering_ctrl_timeout_handler(void *pcontext)
{
	_irqL irql;
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)pcontext;
	struct _ADAPTER *padapter = preorder_ctrl->padapter;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;


	if(padapter->bDriverStopped ||padapter->bSurpriseRemoved)
	{
		return;
	}

	//DBG_871X("+rtw_reordering_ctrl_timeout_handler()=>\n");

	_enter_critical_bh(&ppending_recvframe_queue->lock, &irql);

	if(recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE)==_TRUE)
	{
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
	}

	_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

}

int process_recv_indicatepkts(struct _ADAPTER *padapter, union recv_frame *prframe);
int process_recv_indicatepkts(struct _ADAPTER *padapter, union recv_frame *prframe)
{
	int retval = _SUCCESS;
	//struct recv_priv *precvpriv = &padapter->recvpriv;
	//struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
#ifdef CONFIG_TDLS
	struct sta_info *psta = prframe->u.hdr.psta;
#endif //CONFIG_TDLS

#ifdef CONFIG_80211N_HT

	struct ht_priv	*phtpriv = &pmlmepriv->htpriv;

#ifdef CONFIG_TDLS
	if( (phtpriv->ht_option==_TRUE) ||
		((psta->tdls_sta_state & TDLS_LINKED_STATE) &&
		 (psta->htpriv.ht_option==_TRUE) &&
		 (psta->htpriv.ampdu_enable==_TRUE))) //B/G/N Mode
#else
	if(phtpriv->ht_option==_TRUE)  //B/G/N Mode
#endif //CONFIG_TDLS
	{
		//prframe->u.hdr.preorder_ctrl = &precvpriv->recvreorder_ctrl[pattrib->priority];

		if(recv_indicatepkt_reorder(padapter, prframe)!=_SUCCESS)// including perform A-MPDU Rx Ordering Buffer Control
		{

			if ((padapter->bDriverStopped == _FALSE) &&
			    (padapter->bSurpriseRemoved == _FALSE))
			{
				retval = _FAIL;
				return retval;
			}
		}
	}
	else //B/G mode
#endif
	{
		retval=wlanhdr_to_ethhdr (prframe);
		if(retval != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
			return retval;
		}

		if ((padapter->bDriverStopped ==_FALSE)&&( padapter->bSurpriseRemoved==_FALSE))
		{
			//indicate this recv_frame
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@ process_recv_indicatepkts- recv_func recv_indicatepkt\n" ));
			rtw_recv_indicatepkt(padapter, prframe);


		}
		else
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("@@@@ process_recv_indicatepkts- recv_func free_indicatepkt\n" ));

			RT_TRACE(_module_rtl871x_recv_c_, _drv_notice_, ("recv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
			retval = _FAIL;
			return retval;
		}

	}

	return retval;

}

int recv_func_prehandle(struct _ADAPTER *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

#ifdef CONFIG_MP_INCLUDED
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
#endif

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
	{
		if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE))//&&(padapter->mppriv.check_mp_pkt == 0))
		{
		if (pattrib->crc_err == 1)
			padapter->mppriv.rx_crcerrpktcount++;
		else
			padapter->mppriv.rx_pktcount++;

		if (check_fwstate(pmlmepriv, WIFI_MP_LPBK_STATE) == _FALSE) {
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("MP - Not in loopback mode , drop pkt \n"));
			ret = _FAIL;
			rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
			goto exit;
		}
		}
	}
#endif

	//check the frame crtl field and decache
	ret = validate_recv_frame(padapter, rframe);
	if (ret != _SUCCESS)
	{
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_, ("recv_func: validate_recv_frame fail! drop pkt\n"));
		rtw_free_recvframe(rframe, pfree_recv_queue);//free this recv_frame
		goto exit;
	}

exit:
	return ret;
}

int recv_func_posthandle(struct _ADAPTER *padapter, union recv_frame *prframe)
{
	int ret = _SUCCESS;
	union recv_frame *orig_prframe = prframe;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;


#ifdef CONFIG_TDLS
	uint8_t *psnap_type, *pcategory;
	struct sta_info *ptdls_sta = NULL;
#endif //CONFIG_TDLS


	// DATA FRAME
	rtw_led_control(padapter, LED_CTL_RX);

	prframe = decryptor(padapter, prframe);
	if (prframe == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("decryptor: drop pkt\n"));
		ret = _FAIL;
		goto _recv_data_drop;
	}

#if 0
	if ( padapter->adapter_type == PRIMARY_ADAPTER )
	{
		DBG_871X("+++\n");
		{
			int i;
			uint8_t	*ptr = get_recvframe_data(prframe);
			for(i=0; i<140;i=i+8)
				DBG_871X("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:", *(ptr+i),
				*(ptr+i+1), *(ptr+i+2) ,*(ptr+i+3) ,*(ptr+i+4),*(ptr+i+5), *(ptr+i+6), *(ptr+i+7));

		}
		DBG_871X("---\n");
	}
#endif

#ifdef CONFIG_TDLS
	//check TDLS frame
	psnap_type = get_recvframe_data(orig_prframe);
	psnap_type+=pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	pcategory = psnap_type + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;

	if((_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, ETH_TYPE_LEN)) &&
		((*pcategory==RTW_WLAN_CATEGORY_TDLS) || (*pcategory==RTW_WLAN_CATEGORY_P2P))){
		ret = OnTDLS(padapter, prframe);	//all of functions will return _FAIL
		goto _exit_recv_func;
	}
#endif //CONFIG_TDLS

	prframe = recvframe_chk_defrag(padapter, prframe);
	if(prframe==NULL)	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvframe_chk_defrag: drop pkt\n"));
		goto _recv_data_drop;
	}

	prframe=portctrl(padapter, prframe);
	if (prframe == NULL) {
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("portctrl: drop pkt \n"));
		ret = _FAIL;
		goto _recv_data_drop;
	}

#ifdef CONFIG_TDLS
	if(padapter->tdlsinfo.setup_state == TDLS_LINKED_STATE)
		ptdls_sta = rtw_get_stainfo(&padapter->stapriv, pattrib->src);
	count_rx_stats(padapter, prframe, ptdls_sta);
#else
	count_rx_stats(padapter, prframe, NULL);
#endif //CONFIG_TDLS

#ifdef CONFIG_80211N_HT
	ret = process_recv_indicatepkts(padapter, prframe);
	if (ret != _SUCCESS)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recv_func: process_recv_indicatepkts fail! \n"));
		rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
		goto _recv_data_drop;
	}
#else // CONFIG_80211N_HT
	if (!pattrib->amsdu)
	{
		ret = wlanhdr_to_ethhdr (prframe);
		if (ret != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("wlanhdr_to_ethhdr: drop pkt \n"));
			rtw_free_recvframe(orig_prframe, pfree_recv_queue);//free this recv_frame
			goto _recv_data_drop;
		}

		if ((padapter->bDriverStopped == _FALSE) && (padapter->bSurpriseRemoved == _FALSE))
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@ recv_func: recv_func rtw_recv_indicatepkt\n" ));
			//indicate this recv_frame
			ret = rtw_recv_indicatepkt(padapter, prframe);
			if (ret != _SUCCESS)
			{
				goto _recv_data_drop;
			}
		}
		else
		{
			RT_TRACE(_module_rtl871x_recv_c_, _drv_alert_, ("@@@@  recv_func: rtw_free_recvframe\n" ));
			RT_TRACE(_module_rtl871x_recv_c_, _drv_debug_, ("recv_func:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
			ret = _FAIL;
			rtw_free_recvframe(orig_prframe, pfree_recv_queue); //free this recv_frame
		}

	}
	else if(pattrib->amsdu==1)
	{

		ret = amsdu_to_msdu(padapter, prframe);
		if(ret != _SUCCESS)
		{
			rtw_free_recvframe(orig_prframe, pfree_recv_queue);
			goto _recv_data_drop;
		}
	}
	else
	{
		goto _recv_data_drop;
	}
#endif // CONFIG_80211N_HT

_exit_recv_func:
	return ret;

_recv_data_drop:
	precvpriv->rx_drop++;
	return ret;
}


int recv_func(struct _ADAPTER *padapter, union recv_frame *rframe);
int recv_func(struct _ADAPTER *padapter, union recv_frame *rframe)
{
	int ret;
	struct rx_pkt_attrib *prxattrib = &rframe->u.hdr.attrib;
	struct recv_priv *recvpriv = &padapter->recvpriv;
	struct security_priv *psecuritypriv=&padapter->securitypriv;
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;

	/* check if need to handle uc_swdec_pending_queue*/
	if (check_fwstate(mlmepriv, WIFI_STATION_STATE) && psecuritypriv->busetkipkey)
	{
		union recv_frame *pending_frame;
		_irqL irqL;

		while((pending_frame=rtw_alloc_recvframe(&padapter->recvpriv.uc_swdec_pending_queue))) {
			if (recv_func_posthandle(padapter, pending_frame) == _SUCCESS)
				DBG_871X("%s: dequeue uc_swdec_pending_queue\n", __func__);
		}
	}

	ret = recv_func_prehandle(padapter, rframe);

	if(ret == _SUCCESS) {

		/* check if need to enqueue into uc_swdec_pending_queue*/
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE) &&
			!IS_MCAST(prxattrib->ra) && prxattrib->encrypt>0 &&
			(prxattrib->bdecrypted == 0 ||psecuritypriv->sw_decrypt == _TRUE) &&
			!is_wep_enc(psecuritypriv->dot11PrivacyAlgrthm) &&
			!psecuritypriv->busetkipkey) {
			rtw_enqueue_recvframe(rframe, &padapter->recvpriv.uc_swdec_pending_queue);
			DBG_871X("%s: no key, enqueue uc_swdec_pending_queue\n", __func__);
			goto exit;
		}

		ret = recv_func_posthandle(padapter, rframe);
	}

exit:
	return ret;
}


int32_t rtw_recv_entry(union recv_frame *precvframe)
{
	struct _ADAPTER *padapter;
	struct recv_priv *precvpriv;
	int32_t ret=_SUCCESS;

_func_enter_;

//	RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("+rtw_recv_entry\n"));

	padapter = precvframe->u.hdr.adapter;

	precvpriv = &padapter->recvpriv;


	if ((ret = recv_func(padapter, precvframe)) == _FAIL)
	{
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("rtw_recv_entry: recv_func return fail!!!\n"));
		goto _recv_entry_drop;
	}


	precvpriv->rx_pkts++;

_func_exit_;

	return ret;

_recv_entry_drop:

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		padapter->mppriv.rx_pktloss = precvpriv->rx_drop;
#endif

	//RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("_recv_entry_drop\n"));

_func_exit_;

	return ret;
}

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
void rtw_signal_stat_timer_hdl(RTW_TIMER_HDL_ARGS){
	struct _ADAPTER *adapter = (struct _ADAPTER *)FunctionContext;
	struct recv_priv *recvpriv = &adapter->recvpriv;

	uint32_t	 tmp_s, tmp_q;
	uint8_t avg_signal_strength = 0;
	uint8_t avg_signal_qual = 0;
	uint32_t	 num_signal_strength = 0;
	uint32_t	 num_signal_qual = 0;
	uint8_t _alpha = 3; // this value is based on converging_constant = 5000 and sampling_interval = 1000

	if(adapter->recvpriv.is_signal_dbg) {
		//update the user specific value, signal_strength_dbg, to signal_strength, rssi
		adapter->recvpriv.signal_strength= adapter->recvpriv.signal_strength_dbg;
		adapter->recvpriv.rssi=(s8)translate_percentage_to_dbm((uint8_t)adapter->recvpriv.signal_strength_dbg);
	} else {

		if(recvpriv->signal_strength_data.update_req == 0) {// update_req is clear, means we got rx
			avg_signal_strength = recvpriv->signal_strength_data.avg_val;
			num_signal_strength = recvpriv->signal_strength_data.total_num;
			// after avg_vals are accquired, we can re-stat the signal values
			recvpriv->signal_strength_data.update_req = 1;
		}

		if(recvpriv->signal_qual_data.update_req == 0) {// update_req is clear, means we got rx
			avg_signal_qual = recvpriv->signal_qual_data.avg_val;
			num_signal_qual = recvpriv->signal_qual_data.total_num;
			// after avg_vals are accquired, we can re-stat the signal values
			recvpriv->signal_qual_data.update_req = 1;
		}

		//update value of signal_strength, rssi, signal_qual
		if(check_fwstate(&adapter->mlmepriv, _FW_UNDER_SURVEY) == _FALSE) {
			tmp_s = (avg_signal_strength+(_alpha-1)*recvpriv->signal_strength);
			if(tmp_s %_alpha)
				tmp_s = tmp_s/_alpha + 1;
			else
				tmp_s = tmp_s/_alpha;
			if(tmp_s>100)
				tmp_s = 100;

			tmp_q = (avg_signal_qual+(_alpha-1)*recvpriv->signal_qual);
			if(tmp_q %_alpha)
				tmp_q = tmp_q/_alpha + 1;
			else
				tmp_q = tmp_q/_alpha;
			if(tmp_q>100)
				tmp_q = 100;

			recvpriv->signal_strength = tmp_s;
			recvpriv->rssi = (s8)translate_percentage_to_dbm(tmp_s);
			recvpriv->signal_qual = tmp_q;

		}
	}
	rtw_set_signal_stat_timer(recvpriv);

}
#endif //CONFIG_NEW_SIGNAL_STAT_PROCESS



