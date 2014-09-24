/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#define _RTW_P2P_C_

#include <drv_types.h>

#ifdef CONFIG_P2P

int rtw_p2p_is_channel_list_ok( uint8_t desired_ch, uint8_t * ch_list, uint8_t ch_cnt )
{
	int found = 0, i = 0;

	for( i = 0; i < ch_cnt; i++ )
	{
		if ( ch_list[ i ] == desired_ch )
		{
			found = 1;
			break;
		}
	}
	return( found );
}

int is_any_client_associated(_adapter *padapter)
{
	return padapter->stapriv.asoc_list_cnt ? _TRUE : _FALSE;
}

static uint32_t	 go_add_group_info_attr(struct wifidirect_info *pwdinfo, uint8_t *pbuf)
{
	_irqL irqL;
	struct list_head	*phead, *plist;
	uint32_t	 len=0;
	uint16_t attr_len = 0;
	uint8_t tmplen, *pdata_attr, *pstart, *pcur;
	struct sta_info *psta = NULL;
	_adapter *padapter = pwdinfo->padapter;
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("%s\n", __FUNCTION__);

	pdata_attr = rtw_zmalloc(MAX_P2P_IE_LEN);

	pstart = pdata_attr;
	pcur = pdata_attr;

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	//look up sta asoc_queue
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
	{
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);

		plist = get_next(plist);


		if(psta->is_p2p_device)
		{
			tmplen = 0;

			pcur++;

			//P2P device address
			memcpy(pcur, psta->dev_addr, ETH_ALEN);
			pcur += ETH_ALEN;

			//P2P interface address
			memcpy(pcur, psta->hwaddr, ETH_ALEN);
			pcur += ETH_ALEN;

			*pcur = psta->dev_cap;
			pcur++;

			//*(uint16_t *)(pcur) = cpu_to_be16(psta->config_methods);
			RTW_PUT_BE16(pcur, psta->config_methods);
			pcur += 2;

			memcpy(pcur, psta->primary_dev_type, 8);
			pcur += 8;

			*pcur = psta->num_of_secdev_type;
			pcur++;

			memcpy(pcur, psta->secdev_types_list, psta->num_of_secdev_type*8);
			pcur += psta->num_of_secdev_type*8;

			if(psta->dev_name_len>0)
			{
				//*(uint16_t *)(pcur) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
				RTW_PUT_BE16(pcur, WPS_ATTR_DEVICE_NAME);
				pcur += 2;

				//*(uint16_t *)(pcur) = cpu_to_be16( psta->dev_name_len );
				RTW_PUT_BE16(pcur, psta->dev_name_len);
				pcur += 2;

				memcpy(pcur, psta->dev_name, psta->dev_name_len);
				pcur += psta->dev_name_len;
			}


			tmplen = (uint8_t)(pcur-pstart);

			*pstart = (tmplen-1);

			attr_len += tmplen;

			//pstart += tmplen;
			pstart = pcur;

		}


	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	if(attr_len>0)
	{
		len = rtw_set_p2p_attr_content(pbuf, P2P_ATTR_GROUP_INFO, attr_len, pdata_attr);
	}

	rtw_mfree(pdata_attr);

	return len;

}

static void issue_group_disc_req(struct wifidirect_info *pwdinfo, uint8_t *da)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	_adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	unsigned char category = RTW_WLAN_CATEGORY_P2P;//P2P action frame
	uint32_t	p2poui = cpu_to_be32(P2POUI);
	uint8_t	oui_subtype = P2P_GO_DISC_REQUEST;
	uint8_t	dialogToken=0;

	DBG_871X("[%s]\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (uint8_t *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, pwdinfo->interface_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr3, pwdinfo->interface_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	//Build P2P action frame header
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	//there is no IE in this P2P action frame

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

static void issue_p2p_devdisc_resp(struct wifidirect_info *pwdinfo, uint8_t *da, uint8_t status, uint8_t dialogToken)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	_adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	uint8_t			action = P2P_PUB_ACTION_ACTION;
	uint32_t			p2poui = cpu_to_be32(P2POUI);
	uint8_t			oui_subtype = P2P_DEVDISC_RESP;
	uint8_t p2pie[8] = { 0x00 };
	uint32_t	 p2pielen = 0;

	DBG_871X("[%s]\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (uint8_t *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, pwdinfo->device_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr3, pwdinfo->device_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	//Build P2P public action frame header
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));


	//Build P2P IE
	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	// P2P_ATTR_STATUS
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status);

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, p2pie, &pattrib->pktlen);

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

static void issue_p2p_provision_resp(struct wifidirect_info *pwdinfo, uint8_t * raddr, uint8_t * frame_body, uint16_t config_method)
{
	_adapter *padapter = pwdinfo->padapter;
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	uint8_t			action = P2P_PUB_ACTION_ACTION;
	uint8_t			dialogToken = frame_body[7];	//	The Dialog Token of provisioning discovery request frame.
	uint32_t			p2poui = cpu_to_be32(P2POUI);
	uint8_t			oui_subtype = P2P_PROVISION_DISC_RESP;
	uint8_t			wpsie[ 100 ] = { 0x00 };
	uint8_t			wpsielen = 0;

	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (uint8_t *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	wpsielen = 0;
	//	WPS OUI
	//*(uint32_t *) ( wpsie ) = cpu_to_be32( WPSOUI );
	RTW_PUT_BE32(wpsie, WPSOUI);
	wpsielen += 4;

#if 0
	//	WPS version
	//	Type:
	*(uint16_t *) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
	wpsielen += 2;

	//	Length:
	*(uint16_t *) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0
#endif

	//	Config Method
	//	Type:
	//*(uint16_t *) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
	RTW_PUT_BE16(wpsie + wpsielen, WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	//	Length:
	//*(uint16_t *) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	RTW_PUT_BE16(wpsie + wpsielen, 0x0002);
	wpsielen += 2;

	//	Value:
	//*(uint16_t *) ( wpsie + wpsielen ) = cpu_to_be16( config_method );
	RTW_PUT_BE16(wpsie + wpsielen, config_method);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

static void issue_p2p_presence_resp(struct wifidirect_info *pwdinfo, uint8_t *da, uint8_t status, uint8_t dialogToken)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	_adapter *padapter = pwdinfo->padapter;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	unsigned char category = RTW_WLAN_CATEGORY_P2P;//P2P action frame
	uint32_t	p2poui = cpu_to_be32(P2POUI);
	uint8_t	oui_subtype = P2P_PRESENCE_RESPONSE;
	uint8_t p2pie[ MAX_P2P_IE_LEN] = { 0x00 };
	uint8_t noa_attr_content[32] = { 0x00 };
	uint32_t	 p2pielen = 0;

	DBG_871X("[%s]\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (uint8_t *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	memcpy(pwlanhdr->addr2, pwdinfo->interface_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr3, pwdinfo->interface_addr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	//Build P2P action frame header
	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));


	//Add P2P IE header
	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//Add Status attribute in P2P IE
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status);

	//Add NoA attribute in P2P IE
	noa_attr_content[0] = 0x1;//index
	noa_attr_content[1] = 0x0;//CTWindow and OppPS Parameters

	//todo: Notice of Absence Descriptor(s)

	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_NOA, 2, noa_attr_content);



	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, p2pie, &(pattrib->pktlen));


	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

uint32_t	 build_beacon_p2p_ie(struct wifidirect_info *pwdinfo, uint8_t *pbuf)
{
	uint8_t p2pie[ MAX_P2P_IE_LEN] = { 0x00 };
	uint16_t capability=0;
	uint32_t	 len=0, p2pielen = 0;


	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0


	//	According to the P2P Specification, the beacon frame should contain 3 P2P attributes
	//	1. P2P Capability
	//	2. P2P Device ID
	//	3. Notice of Absence ( NOA )

	//	P2P Capability ATTR
	//	Type:
	//	Length:
	//	Value:
	//	Device Capability Bitmap, 1 byte
	//	Be able to participate in additional P2P Groups and
	//	support the P2P Invitation Procedure
	//	Group Capability Bitmap, 1 byte
	capability = P2P_DEVCAP_INVITATION_PROC|P2P_DEVCAP_CLIENT_DISCOVERABILITY;
	capability |=  ((P2P_GRPCAP_GO | P2P_GRPCAP_INTRABSS) << 8);
	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
		capability |= (P2P_GRPCAP_GROUP_FORMATION<<8);

	capability = cpu_to_le16(capability);

	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_CAPABILITY, 2, (uint8_t *)&capability);


	// P2P Device ID ATTR
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_DEVICE_ID, ETH_ALEN, pwdinfo->device_addr);


	// Notice of Absence ATTR
	//	Type:
	//	Length:
	//	Value:

	//go_add_noa_attr(pwdinfo);


	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);


	return len;

}

uint32_t	 build_probe_resp_p2p_ie(struct wifidirect_info *pwdinfo, uint8_t *pbuf)
{
	uint8_t p2pie[ MAX_P2P_IE_LEN] = { 0x00 };
	uint32_t	 len=0, p2pielen = 0;

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20100907
	//	According to the P2P Specification, the probe response frame should contain 5 P2P attributes
	//	1. P2P Capability
	//	2. Extended Listen Timing
	//	3. Notice of Absence ( NOA )	( Only GO needs this )
	//	4. Device Info
	//	5. Group Info	( Only GO need this )

	//	P2P Capability ATTR
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	RTW_PUT_LE16(p2pie + p2pielen, 0x0002);
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;

	//	Group Capability Bitmap, 1 byte
	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		p2pie[ p2pielen ] = (P2P_GRPCAP_GO | P2P_GRPCAP_INTRABSS);

		if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
			p2pie[ p2pielen ] |= P2P_GRPCAP_GROUP_FORMATION;

		p2pielen++;
	}
	else if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) )
	{
		//	Group Capability Bitmap, 1 byte
		if ( pwdinfo->persistent_supported )
			p2pie[ p2pielen++ ] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
		else
			p2pie[ p2pielen++ ] = DMP_P2P_GRPCAP_SUPPORT;
	}

	//	Extended Listen Timing ATTR
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_EX_LISTEN_TIMING;

	//	Length:
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 );
	RTW_PUT_LE16(p2pie + p2pielen, 0x0004);
	p2pielen += 2;

	//	Value:
	//	Availability Period
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	RTW_PUT_LE16(p2pie + p2pielen, 0xFFFF);
	p2pielen += 2;

	//	Availability Interval
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	RTW_PUT_LE16(p2pie + p2pielen, 0xFFFF);
	p2pielen += 2;


	// Notice of Absence ATTR
	//	Type:
	//	Length:
	//	Value:
	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		//go_add_noa_attr(pwdinfo);
	}

	//	Device Info ATTR
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	RTW_PUT_LE16(p2pie + p2pielen, 21 + pwdinfo->device_name_len);
	p2pielen += 2;

	//	Value:
	//	P2P Device Address
	memcpy( p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->supported_wps_cm);
	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	//	OUI
	//*(uint32_t *) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	RTW_PUT_BE32(p2pie + p2pielen, WPSOUI);
	p2pielen += 4;

	//	Sub Category ID
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	RTW_PUT_BE16(p2pie + p2pielen, WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	//	Length:
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->device_name_len);
	p2pielen += 2;

	//	Value:
	memcpy( p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;

	// Group Info ATTR
	//	Type:
	//	Length:
	//	Value:
	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		p2pielen += go_add_group_info_attr(pwdinfo, p2pie + p2pielen);
	}


	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);


	return len;

}

uint32_t	 build_prov_disc_request_p2p_ie(struct wifidirect_info *pwdinfo, uint8_t *pbuf, uint8_t * pssid, uint8_t ussidlen, uint8_t * pdev_raddr )
{
	uint8_t p2pie[ MAX_P2P_IE_LEN] = { 0x00 };
	uint32_t	 len=0, p2pielen = 0;

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20110301
	//	According to the P2P Specification, the provision discovery request frame should contain 3 P2P attributes
	//	1. P2P Capability
	//	2. Device Info
	//	3. Group ID ( When joining an operating P2P Group )

	//	P2P Capability ATTR
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	RTW_PUT_LE16(p2pie + p2pielen, 0x0002);
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;

	//	Group Capability Bitmap, 1 byte
	if ( pwdinfo->persistent_supported )
		p2pie[ p2pielen++ ] = P2P_GRPCAP_PERSISTENT_GROUP | DMP_P2P_GRPCAP_SUPPORT;
	else
		p2pie[ p2pielen++ ] = DMP_P2P_GRPCAP_SUPPORT;


	//	Device Info ATTR
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	RTW_PUT_LE16(p2pie + p2pielen, 21 + pwdinfo->device_name_len);
	p2pielen += 2;

	//	Value:
	//	P2P Device Address
	memcpy( p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.
	if ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PBC )
	{
		//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_PBC );
		RTW_PUT_BE16(p2pie + p2pielen, WPS_CONFIG_METHOD_PBC);
	}
	else
	{
		//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_DISPLAY );
		RTW_PUT_BE16(p2pie + p2pielen, WPS_CONFIG_METHOD_DISPLAY);
	}

	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	//	OUI
	//*(uint32_t *) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	RTW_PUT_BE32(p2pie + p2pielen, WPSOUI);
	p2pielen += 4;

	//	Sub Category ID
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
	RTW_PUT_BE16(p2pie + p2pielen, WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	RTW_PUT_BE16(p2pie + p2pielen, WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	//	Length:
	//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	RTW_PUT_BE16(p2pie + p2pielen, pwdinfo->device_name_len);
	p2pielen += 2;

	//	Value:
	memcpy( p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;

	if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) )
	{
		//	Added by Albert 2011/05/19
		//	In this case, the pdev_raddr is the device address of the group owner.

		//	P2P Group ID ATTR
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

		//	Length:
		//*(uint16_t *) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN + ussidlen );
		RTW_PUT_LE16(p2pie + p2pielen, ETH_ALEN + ussidlen);
		p2pielen += 2;

		//	Value:
		memcpy( p2pie + p2pielen, pdev_raddr, ETH_ALEN );
		p2pielen += ETH_ALEN;

		memcpy( p2pie + p2pielen, pssid, ussidlen );
		p2pielen += ussidlen;

	}

	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);


	return len;

}


uint32_t	 build_assoc_resp_p2p_ie(struct wifidirect_info *pwdinfo, uint8_t *pbuf, uint8_t status_code)
{
	uint8_t p2pie[ MAX_P2P_IE_LEN] = { 0x00 };
	uint32_t	 len=0, p2pielen = 0;

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	// According to the P2P Specification, the Association response frame should contain 2 P2P attributes
	//	1. Status
	//	2. Extended Listen Timing (optional)


	//	Status ATTR
	p2pielen += rtw_set_p2p_attr_content(&p2pie[p2pielen], P2P_ATTR_STATUS, 1, &status_code);


	// Extended Listen Timing ATTR
	//	Type:
	//	Length:
	//	Value:


	pbuf = rtw_set_ie(pbuf, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &len);

	return len;

}

uint32_t	 build_deauth_p2p_ie(struct wifidirect_info *pwdinfo, uint8_t *pbuf)
{
	uint32_t	 len=0;

	return len;
}

uint32_t	 process_probe_req_p2p_ie(struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len)
{
	uint8_t *p;
	uint32_t	 ret=_FALSE;
	uint8_t *p2pie;
	uint32_t	p2pielen = 0;
	int ssid_len=0, rate_cnt = 0;

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SUPPORTEDRATES_IE_, (int *)&rate_cnt,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

	if ( rate_cnt <= 4 )
	{
		int i, g_rate =0;

		for( i = 0; i < rate_cnt; i++ )
		{
			if ( ( ( *( p + 2 + i ) & 0xff ) != 0x02 ) &&
				( ( *( p + 2 + i ) & 0xff ) != 0x04 ) &&
				( ( *( p + 2 + i ) & 0xff ) != 0x0B ) &&
				( ( *( p + 2 + i ) & 0xff ) != 0x16 ) )
			{
				g_rate = 1;
			}
		}

		if ( g_rate == 0 )
		{
			//	There is no OFDM rate included in SupportedRates IE of this probe request frame
			//	The driver should response this probe request.
			return ret;
		}
	}
	else
	{
		//	rate_cnt > 4 means the SupportRates IE contains the OFDM rate because the count of CCK rates are 4.
		//	We should proceed the following check for this probe request.
	}

	//	Added comments by Albert 20100906
	//	There are several items we should check here.
	//	1. This probe request frame must contain the P2P IE. (Done)
	//	2. This probe request frame must contain the wildcard SSID. (Done)
	//	3. Wildcard BSSID. (Todo)
	//	4. Destination Address. ( Done in mgt_dispatcher function )
	//	5. Requested Device Type in WSC IE. (Todo)
	//	6. Device ID attribute in P2P IE. (Todo)

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ssid_len,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);

	ssid_len &= 0xff;	//	Just last 1 byte is valid for ssid len of the probe request
	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		if((p2pie=rtw_get_p2p_ie( pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_ , len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_ , NULL, &p2pielen)))
		{
			if ( (p != NULL) && _rtw_memcmp( ( void * ) ( p+2 ), ( void * ) pwdinfo->p2p_wildcard_ssid , 7 ))
			{
				//todo:
				//Check Requested Device Type attributes in WSC IE.
				//Check Device ID attribute in P2P IE

				ret = _TRUE;
			}
			else if ( (p != NULL) && ( ssid_len == 0 ) )
			{
				ret = _TRUE;
			}
		}
		else
		{
			//non -p2p device
		}

	}


	return ret;

}

uint32_t	 process_assoc_req_p2p_ie(struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len, struct sta_info *psta)
{
	uint8_t status_code = P2P_STATUS_SUCCESS;
	uint8_t *pbuf, *pattr_content=NULL;
	uint32_t	 attr_contentlen = 0;
	uint16_t cap_attr=0;
	unsigned short	frame_type, ie_offset=0;
	uint8_t * ies;
	uint32_t	 ies_len;
	uint8_t * p2p_ie;
	uint32_t	p2p_ielen = 0;

	if(!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		return P2P_STATUS_FAIL_REQUEST_UNABLE;

	frame_type = GetFrameSubType(pframe);
	if (frame_type == WIFI_ASSOCREQ)
	{
		ie_offset = _ASOCREQ_IE_OFFSET_;
	}
	else // WIFI_REASSOCREQ
	{
		ie_offset = _REASOCREQ_IE_OFFSET_;
	}

	ies = pframe + WLAN_HDR_A3_LEN + ie_offset;
	ies_len = len - WLAN_HDR_A3_LEN - ie_offset;

	p2p_ie = rtw_get_p2p_ie(ies , ies_len , NULL, &p2p_ielen);

	if ( !p2p_ie )
	{
		DBG_8192C( "[%s] P2P IE not Found!!\n", __FUNCTION__ );
		status_code =  P2P_STATUS_FAIL_INVALID_PARAM;
	}
	else
	{
		DBG_8192C( "[%s] P2P IE Found!!\n", __FUNCTION__ );
	}

	while ( p2p_ie )
	{
		//Check P2P Capability ATTR
		if( rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (uint8_t *)&cap_attr, (uint*) &attr_contentlen) )
		{
			DBG_8192C( "[%s] Got P2P Capability Attr!!\n", __FUNCTION__ );
			cap_attr = le16_to_cpu(cap_attr);
			psta->dev_cap = cap_attr&0xff;
		}

		//Check Extended Listen Timing ATTR


		//Check P2P Device Info ATTR
		if(rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO, NULL, (uint*)&attr_contentlen))
		{
			DBG_8192C( "[%s] Got P2P DEVICE INFO Attr!!\n", __FUNCTION__ );
			pattr_content = pbuf = rtw_zmalloc(attr_contentlen);
			if(pattr_content)
			{
				uint8_t num_of_secdev_type;
				uint16_t dev_name_len;


				rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO , pattr_content, (uint*)&attr_contentlen);

				memcpy(psta->dev_addr, 	pattr_content, ETH_ALEN);//P2P Device Address

				pattr_content += ETH_ALEN;

				memcpy(&psta->config_methods, pattr_content, 2);//Config Methods
				psta->config_methods = be16_to_cpu(psta->config_methods);

				pattr_content += 2;

				memcpy(psta->primary_dev_type, pattr_content, 8);

				pattr_content += 8;

				num_of_secdev_type = *pattr_content;
				pattr_content += 1;

				if(num_of_secdev_type==0)
				{
					psta->num_of_secdev_type = 0;
				}
				else
				{
					uint32_t	 len;

					psta->num_of_secdev_type = num_of_secdev_type;

					len = (sizeof(psta->secdev_types_list)<(num_of_secdev_type*8)) ? (sizeof(psta->secdev_types_list)) : (num_of_secdev_type*8);

					memcpy(psta->secdev_types_list, pattr_content, len);

					pattr_content += (num_of_secdev_type*8);
				}


				//dev_name_len = attr_contentlen - ETH_ALEN - 2 - 8 - 1 - (num_of_secdev_type*8);
				psta->dev_name_len=0;
				if(WPS_ATTR_DEVICE_NAME == be16_to_cpu(*(uint16_t *)pattr_content))
				{
					dev_name_len = be16_to_cpu(*(uint16_t *)(pattr_content+2));

					psta->dev_name_len = (sizeof(psta->dev_name)<dev_name_len) ? sizeof(psta->dev_name):dev_name_len;

					memcpy(psta->dev_name, pattr_content+4, psta->dev_name_len);
				}
				/* ULLI check usage of attr_contentlen */
				rtw_mfree(pbuf);

			}

		}

		//Get the next P2P IE
		p2p_ie = rtw_get_p2p_ie(p2p_ie+p2p_ielen, ies_len -(p2p_ie -ies + p2p_ielen), NULL, &p2p_ielen);

	}

	return status_code;

}

uint32_t	 process_p2p_devdisc_req(struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len)
{
	uint8_t *frame_body;
	uint8_t status, dialogToken;
	struct sta_info *psta = NULL;
	_adapter *padapter = pwdinfo->padapter;
	struct sta_priv *pstapriv = &padapter->stapriv;
	uint8_t *p2p_ie;
	uint32_t	p2p_ielen = 0;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	dialogToken = frame_body[7];
	status = P2P_STATUS_FAIL_UNKNOWN_P2PGROUP;

	if ( (p2p_ie=rtw_get_p2p_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen)) )
	{
		uint8_t groupid[ 38 ] = { 0x00 };
		uint8_t dev_addr[ETH_ALEN] = { 0x00 };
		uint32_t	attr_contentlen = 0;

		if(rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen))
		{
			if(_rtw_memcmp(pwdinfo->device_addr, groupid, ETH_ALEN) &&
				_rtw_memcmp(pwdinfo->p2p_group_ssid, groupid+ETH_ALEN, pwdinfo->p2p_group_ssid_len))
			{
				attr_contentlen=0;
				if(rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_ID, dev_addr, &attr_contentlen))
				{
					_irqL irqL;
					struct list_head	*phead, *plist;

					_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
					phead = &pstapriv->asoc_list;
					plist = get_next(phead);

					//look up sta asoc_queue
					while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
					{
						psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);

						plist = get_next(plist);

						if(psta->is_p2p_device && (psta->dev_cap&P2P_DEVCAP_CLIENT_DISCOVERABILITY) &&
							_rtw_memcmp(psta->dev_addr, dev_addr, ETH_ALEN))
						{

							//_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
							//issue GO Discoverability Request
							issue_group_disc_req(pwdinfo, psta->hwaddr);
							//_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

							status = P2P_STATUS_SUCCESS;

							break;
						}
						else
						{
							status = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
						}

					}
					_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

				}
				else
				{
					status = P2P_STATUS_FAIL_INVALID_PARAM;
				}

			}
			else
			{
				status = P2P_STATUS_FAIL_INVALID_PARAM;
			}

		}

	}


	//issue Device Discoverability Response
	issue_p2p_devdisc_resp(pwdinfo, GetAddr2Ptr(pframe), status, dialogToken);


	return (status==P2P_STATUS_SUCCESS) ? _TRUE:_FALSE;

}

uint32_t	 process_p2p_devdisc_resp(struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len)
{
	return _TRUE;
}

uint8_t process_p2p_provdisc_req(struct wifidirect_info *pwdinfo,  uint8_t *pframe, uint len )
{
	uint8_t *frame_body;
	uint8_t *wpsie;
	uint	wps_ielen = 0, attr_contentlen = 0;
	uint16_t	uconfig_method = 0;


	frame_body = (pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	if ( (wpsie=rtw_get_wps_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &wps_ielen)) )
	{
		if ( rtw_get_wps_attr_content( wpsie, wps_ielen, WPS_ATTR_CONF_METHOD , ( uint8_t * ) &uconfig_method, &attr_contentlen) )
		{
			uconfig_method = be16_to_cpu( uconfig_method );
			switch( uconfig_method )
			{
				case WPS_CM_DISPLYA:
				{
					memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3 );
					break;
				}
				case WPS_CM_LABEL:
				{
					memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "lab", 3 );
					break;
				}
				case WPS_CM_PUSH_BUTTON:
				{
					memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3 );
					break;
				}
				case WPS_CM_KEYPAD:
				{
					memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3 );
					break;
				}
			}
			issue_p2p_provision_resp( pwdinfo, GetAddr2Ptr(pframe), frame_body, uconfig_method);
		}
	}
	DBG_871X( "[%s] config method = %s\n", __FUNCTION__, pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req );
	return _TRUE;

}

uint8_t process_p2p_provdisc_resp(struct wifidirect_info *pwdinfo,  uint8_t *pframe)
{

	return _TRUE;
}

uint8_t rtw_p2p_get_peer_ch_list(struct wifidirect_info *pwdinfo, uint8_t *ch_content, uint8_t ch_cnt, uint8_t *peer_ch_list)
{
	uint8_t i = 0, j = 0;
	uint8_t temp = 0;
	uint8_t ch_no = 0;
	ch_content += 3;
	ch_cnt -= 3;

	while( ch_cnt > 0)
	{
		ch_content += 1;
		ch_cnt -= 1;
		temp = *ch_content;
		for( i = 0 ; i < temp ; i++, j++ )
		{
			peer_ch_list[j] = *( ch_content + 1 + i );
		}
		ch_content += (temp + 1);
		ch_cnt -= (temp + 1);
		ch_no += temp ;
	}

	return ch_no;
}

uint8_t rtw_p2p_check_peer_oper_ch(struct mlme_ext_priv *pmlmeext, uint8_t ch)
{
	uint8_t i = 0;

	for( i = 0; i < pmlmeext->max_chan_nums; i++ )
	{
		if ( pmlmeext->channel_set[ i ].ChannelNum == ch )
		{
			return _SUCCESS;
		}
	}

	return _FAIL;
}

uint8_t rtw_p2p_ch_inclusion(struct mlme_ext_priv *pmlmeext, uint8_t *peer_ch_list, uint8_t peer_ch_num, uint8_t *ch_list_inclusioned)
{
	int	i = 0, j = 0, temp = 0;
	uint8_t ch_no = 0;

	for( i = 0; i < peer_ch_num; i++ )
	{
		for( j = temp; j < pmlmeext->max_chan_nums; j++ )
		{
			if( *( peer_ch_list + i ) == pmlmeext->channel_set[ j ].ChannelNum )
			{
				ch_list_inclusioned[ ch_no++ ] = *( peer_ch_list + i );
				temp = j;
				break;
			}
		}
	}

	return ch_no;
}

uint8_t process_p2p_group_negotation_req( struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len )
{
	_adapter *padapter = pwdinfo->padapter;
	uint8_t	result = P2P_STATUS_SUCCESS;
	uint32_t	p2p_ielen = 0, wps_ielen = 0;
	uint8_t * ies;
	uint32_t	 ies_len;
	uint8_t *p2p_ie;
	uint8_t *wpsie;
	uint16_t		wps_devicepassword_id = 0x0000;
	uint	wps_devicepassword_id_len = 0;

	if ( (wpsie=rtw_get_wps_ie( pframe + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &wps_ielen)) )
	{
		//	Commented by Kurt 20120113
		//	If some device wants to do p2p handshake without sending prov_disc_req
		//	We have to get peer_req_cm from here.
		if(_rtw_memcmp( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "000", 3) )
		{
			rtw_get_wps_attr_content( wpsie, wps_ielen, WPS_ATTR_DEVICE_PWID, (uint8_t *) &wps_devicepassword_id, &wps_devicepassword_id_len);
			wps_devicepassword_id = be16_to_cpu( wps_devicepassword_id );

			if ( wps_devicepassword_id == WPS_DPID_USER_SPEC )
			{
				memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "dis", 3 );
			}
			else if ( wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC )
			{
				memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pad", 3 );
			}
			else
			{
				memcpy( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, "pbc", 3 );
			}
		}
	}
	else
	{
		DBG_871X( "[%s] WPS IE not Found!!\n", __FUNCTION__ );
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
		return( result );
	}

	if ( pwdinfo->ui_got_wps_info == P2P_NO_WPSINFO )
	{
		result = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_TX_INFOR_NOREADY);
		return( result );
	}

	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie( ies, ies_len, NULL, &p2p_ielen );

	if ( !p2p_ie )
	{
		DBG_871X( "[%s] P2P IE not Found!!\n", __FUNCTION__ );
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
	}

	while ( p2p_ie )
	{
		uint8_t	attr_content = 0x00;
		uint32_t	attr_contentlen = 0;
		uint8_t	ch_content[100] = { 0x00 };
		uint	ch_cnt = 0;
		uint8_t	peer_ch_list[100] = { 0x00 };
		uint8_t	peer_ch_num = 0;
		uint8_t	ch_list_inclusioned[100] = { 0x00 };
		uint8_t	ch_num_inclusioned = 0;
		uint16_t	cap_attr;

		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_ING);

		//Check P2P Capability ATTR
		if(rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (uint8_t *)&cap_attr, (uint*)&attr_contentlen) )
		{
			cap_attr = le16_to_cpu(cap_attr);
		}

		if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT , &attr_content, &attr_contentlen) )
		{
			DBG_871X( "[%s] GO Intent = %d, tie = %d\n", __FUNCTION__, attr_content >> 1, attr_content & 0x01 );
			pwdinfo->peer_intent = attr_content;	//	include both intent and tie breaker values.

			if ( pwdinfo->intent == ( pwdinfo->peer_intent >> 1 ) )
			{
				//	Try to match the tie breaker value
				if ( pwdinfo->intent == P2P_MAX_INTENT )
				{
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					result = P2P_STATUS_FAIL_BOTH_GOINTENT_15;
				}
				else
				{
					if ( attr_content & 0x01 )
					{
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
					}
					else
					{
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
					}
				}
			}
			else if ( pwdinfo->intent > ( pwdinfo->peer_intent >> 1 ) )
			{
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
			}
			else
			{
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
			}

			if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
			{
				//	Store the group id information.
				memcpy( pwdinfo->groupid_info.go_device_addr, pwdinfo->device_addr, ETH_ALEN );
				memcpy( pwdinfo->groupid_info.ssid, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen );
			}
		}


		attr_contentlen = 0;
		if ( rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_INTENTED_IF_ADDR, pwdinfo->p2p_peer_interface_addr, &attr_contentlen ) )
		{
			if ( attr_contentlen != ETH_ALEN )
			{
				memset( pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN );
			}
		}

		if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, ch_content, &ch_cnt) )
		{
			peer_ch_num = rtw_p2p_get_peer_ch_list(pwdinfo, ch_content, ch_cnt, peer_ch_list);
			ch_num_inclusioned = rtw_p2p_ch_inclusion(&padapter->mlmeextpriv, peer_ch_list, peer_ch_num, ch_list_inclusioned);

			if( ch_num_inclusioned == 0)
			{
				DBG_871X( "[%s] No common channel in channel list!\n", __FUNCTION__ );
				result = P2P_STATUS_FAIL_NO_COMMON_CH;
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
				break;
			}

			if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
			{
				if ( !rtw_p2p_is_channel_list_ok( pwdinfo->operating_channel,
												ch_list_inclusioned, ch_num_inclusioned) )
				{
					{
						uint8_t operatingch_info[5] = { 0x00 }, peer_operating_ch = 0;
						attr_contentlen = 0;

						if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen) )
						{
							peer_operating_ch = operatingch_info[4];
						}

						if ( rtw_p2p_is_channel_list_ok( peer_operating_ch,
														ch_list_inclusioned, ch_num_inclusioned) )
						{
							/**
							 *	Change our operating channel as peer's for compatibility.
							 */
							pwdinfo->operating_channel = peer_operating_ch;
							DBG_871X( "[%s] Change op ch to %02x as peer's\n", __FUNCTION__, pwdinfo->operating_channel);
						}
						else
						{
							// Take first channel of ch_list_inclusioned as operating channel
							pwdinfo->operating_channel = ch_list_inclusioned[0];
							DBG_871X( "[%s] Change op ch to %02x\n", __FUNCTION__, pwdinfo->operating_channel);
						}
					}

				}
			}
		}

		//Get the next P2P IE
		p2p_ie = rtw_get_p2p_ie(p2p_ie+p2p_ielen, ies_len -(p2p_ie -ies + p2p_ielen), NULL, &p2p_ielen);
	}

	return( result );
}

uint8_t process_p2p_group_negotation_resp( struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len )
{
	_adapter *padapter = pwdinfo->padapter;
	uint8_t	result = P2P_STATUS_SUCCESS;
	uint32_t	p2p_ielen, wps_ielen;
	uint8_t * ies;
	uint32_t	 ies_len;
	uint8_t * p2p_ie;

	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	//	Be able to know which one is the P2P GO and which one is P2P client.

	if ( rtw_get_wps_ie( ies, ies_len, NULL, &wps_ielen) )
	{

	}
	else
	{
		DBG_871X( "[%s] WPS IE not Found!!\n", __FUNCTION__ );
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
	}

	p2p_ie = rtw_get_p2p_ie( ies, ies_len, NULL, &p2p_ielen );
	if ( !p2p_ie )
	{
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
		result = P2P_STATUS_FAIL_INCOMPATIBLE_PARAM;
	}
	else
	{

		uint8_t	attr_content = 0x00;
		uint32_t	attr_contentlen = 0;
		uint8_t	operatingch_info[5] = { 0x00 };
		uint	ch_cnt = 0;
		uint8_t	ch_content[100] = { 0x00 };
		uint8_t	groupid[ 38 ];
		uint16_t	cap_attr;
		uint8_t	peer_ch_list[100] = { 0x00 };
		uint8_t	peer_ch_num = 0;
		uint8_t	ch_list_inclusioned[100] = { 0x00 };
		uint8_t	ch_num_inclusioned = 0;

		while ( p2p_ie )	//	Found the P2P IE.
		{

			//Check P2P Capability ATTR
			if(rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (uint8_t *)&cap_attr, (uint*)&attr_contentlen) )
			{
				cap_attr = le16_to_cpu(cap_attr);
#ifdef CONFIG_TDLS
				if(!(cap_attr & P2P_GRPCAP_INTRABSS) )
					ptdlsinfo->ap_prohibited = _TRUE;
#endif // CONFIG_TDLS
			}

			rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);
			if ( attr_contentlen == 1 )
			{
				DBG_871X( "[%s] Status = %d\n", __FUNCTION__, attr_content );
				if ( attr_content == P2P_STATUS_SUCCESS )
				{
					//	Do nothing.
				}
				else
				{
					if ( P2P_STATUS_FAIL_INFO_UNAVAILABLE == attr_content ) {
						rtw_p2p_set_state(pwdinfo, P2P_STATE_RX_INFOR_NOREADY);
					} else {
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					}
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
					result = attr_content;
					break;
				}
			}

			//	Try to get the peer's interface address
			attr_contentlen = 0;
			if ( rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_INTENTED_IF_ADDR, pwdinfo->p2p_peer_interface_addr, &attr_contentlen ) )
			{
				if ( attr_contentlen != ETH_ALEN )
				{
					memset( pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN );
				}
			}

			//	Try to get the peer's intent and tie breaker value.
			attr_content = 0x00;
			attr_contentlen = 0;
			if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT , &attr_content, &attr_contentlen) )
			{
				DBG_871X( "[%s] GO Intent = %d, tie = %d\n", __FUNCTION__, attr_content >> 1, attr_content & 0x01 );
				pwdinfo->peer_intent = attr_content;	//	include both intent and tie breaker values.

				if ( pwdinfo->intent == ( pwdinfo->peer_intent >> 1 ) )
				{
					//	Try to match the tie breaker value
					if ( pwdinfo->intent == P2P_MAX_INTENT )
					{
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
						result = P2P_STATUS_FAIL_BOTH_GOINTENT_15;
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					}
					else
					{
						rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
						rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
						if ( attr_content & 0x01 )
						{
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
						}
						else
						{
							rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
						}
					}
				}
				else if ( pwdinfo->intent > ( pwdinfo->peer_intent >> 1 ) )
				{
					rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				}
				else
				{
					rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
				}

				if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
				{
					//	Store the group id information.
					memcpy( pwdinfo->groupid_info.go_device_addr, pwdinfo->device_addr, ETH_ALEN );
					memcpy( pwdinfo->groupid_info.ssid, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen );

				}
			}

			//	Try to get the operation channel information

			attr_contentlen = 0;
			if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen))
			{
				DBG_871X( "[%s] Peer's operating channel = %d\n", __FUNCTION__, operatingch_info[4] );
				pwdinfo->peer_operating_ch = operatingch_info[4];
			}

			//	Try to get the channel list information
			if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, pwdinfo->channel_list_attr, &pwdinfo->channel_list_attr_len ) )
			{
				DBG_871X( "[%s] channel list attribute found, len = %d\n", __FUNCTION__,  pwdinfo->channel_list_attr_len );

				peer_ch_num = rtw_p2p_get_peer_ch_list(pwdinfo, pwdinfo->channel_list_attr, pwdinfo->channel_list_attr_len, peer_ch_list);
				ch_num_inclusioned = rtw_p2p_ch_inclusion(&padapter->mlmeextpriv, peer_ch_list, peer_ch_num, ch_list_inclusioned);

				if( ch_num_inclusioned == 0)
				{
					DBG_871X( "[%s] No common channel in channel list!\n", __FUNCTION__ );
					result = P2P_STATUS_FAIL_NO_COMMON_CH;
					rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
					break;
				}

				if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
				{
					if ( !rtw_p2p_is_channel_list_ok( pwdinfo->operating_channel,
													ch_list_inclusioned, ch_num_inclusioned) )
					{
						{
							uint8_t operatingch_info[5] = { 0x00 }, peer_operating_ch = 0;
							attr_contentlen = 0;

							if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen) )
							{
								peer_operating_ch = operatingch_info[4];
							}

							if ( rtw_p2p_is_channel_list_ok( peer_operating_ch,
															ch_list_inclusioned, ch_num_inclusioned) )
							{
								/**
								 *	Change our operating channel as peer's for compatibility.
								 */
								pwdinfo->operating_channel = peer_operating_ch;
								DBG_871X( "[%s] Change op ch to %02x as peer's\n", __FUNCTION__, pwdinfo->operating_channel);
							}
							else
							{
								// Take first channel of ch_list_inclusioned as operating channel
								pwdinfo->operating_channel = ch_list_inclusioned[0];
								DBG_871X( "[%s] Change op ch to %02x\n", __FUNCTION__, pwdinfo->operating_channel);
							}
						}

					}
				}

			}
			else
			{
				DBG_871X( "[%s] channel list attribute not found!\n", __FUNCTION__);
			}

			//	Try to get the group id information if peer is GO
			attr_contentlen = 0;
			memset( groupid, 0x00, 38 );
			if ( rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen) )
			{
				memcpy( pwdinfo->groupid_info.go_device_addr, &groupid[0], ETH_ALEN );
				memcpy( pwdinfo->groupid_info.ssid, &groupid[6], attr_contentlen - ETH_ALEN );
			}

			//Get the next P2P IE
			p2p_ie = rtw_get_p2p_ie(p2p_ie+p2p_ielen, ies_len -(p2p_ie -ies + p2p_ielen), NULL, &p2p_ielen);
		}

	}

	return( result );

}

uint8_t process_p2p_group_negotation_confirm( struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len )
{
	uint8_t * ies;
	uint32_t	 ies_len;
	uint8_t * p2p_ie;
	uint32_t	p2p_ielen = 0;
	uint8_t	result = P2P_STATUS_SUCCESS;
	ies = pframe + _PUBLIC_ACTION_IE_OFFSET_;
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie( ies, ies_len, NULL, &p2p_ielen );
	while ( p2p_ie )	//	Found the P2P IE.
	{
		uint8_t	attr_content = 0x00, operatingch_info[5] = { 0x00 };
		uint8_t	groupid[ 38 ] = { 0x00 };
		uint32_t	attr_contentlen = 0;

		pwdinfo->negotiation_dialog_token = 1;
		rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);
		if ( attr_contentlen == 1 )
		{
			DBG_871X( "[%s] Status = %d\n", __FUNCTION__, attr_content );
			result = attr_content;

			if ( attr_content == P2P_STATUS_SUCCESS )
			{
				uint8_t	bcancelled = 0;

				_cancel_timer( &pwdinfo->restore_p2p_state_timer, &bcancelled );

				//	Commented by Albert 20100911
				//	Todo: Need to handle the case which both Intents are the same.
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
				rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
				if ( ( pwdinfo->intent ) > ( pwdinfo->peer_intent >> 1 ) )
				{
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				}
				else if ( ( pwdinfo->intent ) < ( pwdinfo->peer_intent >> 1 ) )
				{
					rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
				}
				else
				{
					//	Have to compare the Tie Breaker
					if ( pwdinfo->peer_intent & 0x01 )
					{
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
					}
					else
					{
						rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
					}
				}

			}
			else
			{
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_FAIL);
				break;
			}
		}

		//	Try to get the group id information
		attr_contentlen = 0;
		memset( groupid, 0x00, 38 );
		if ( rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen) )
		{
			DBG_871X( "[%s] Ssid = %s, ssidlen = %zu\n", __FUNCTION__, &groupid[ETH_ALEN], strlen(&groupid[ETH_ALEN]) );
			memcpy( pwdinfo->groupid_info.go_device_addr, &groupid[0], ETH_ALEN );
			memcpy( pwdinfo->groupid_info.ssid, &groupid[6], attr_contentlen - ETH_ALEN );
		}

		attr_contentlen = 0;
		if ( rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, operatingch_info, &attr_contentlen) )
		{
			DBG_871X( "[%s] Peer's operating channel = %d\n", __FUNCTION__, operatingch_info[4] );
			pwdinfo->peer_operating_ch = operatingch_info[4];
		}

		//Get the next P2P IE
		p2p_ie = rtw_get_p2p_ie(p2p_ie+p2p_ielen, ies_len -(p2p_ie -ies + p2p_ielen), NULL, &p2p_ielen);

	}

	return( result );
}

uint8_t process_p2p_presence_req(struct wifidirect_info *pwdinfo, uint8_t *pframe, uint len)
{
	uint8_t *frame_body;
	uint8_t dialogToken=0;
	uint8_t status = P2P_STATUS_SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));

	dialogToken = frame_body[6];

	//todo: check NoA attribute

	issue_p2p_presence_resp(pwdinfo, GetAddr2Ptr(pframe), status, dialogToken);

	return _TRUE;
}

void find_phase_handler( _adapter*	padapter )
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	NDIS_802_11_SSID 	ssid;
	_irqL				irqL;
	uint8_t					_status = 0;

_func_enter_;

	memset((unsigned char*)&ssid, 0, sizeof(NDIS_802_11_SSID));
	memcpy(ssid.Ssid, pwdinfo->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN );
	ssid.SsidLength = P2P_WILDCARD_SSID_LEN;

	rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH);

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	_status = rtw_sitesurvey_cmd(padapter, &ssid, 1, NULL, 0);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);


_func_exit_;
}

void p2p_concurrent_handler(  _adapter* padapter );

void restore_p2p_state_handler( _adapter*	padapter )
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;

_func_enter_;

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_FAIL))
	{
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
	}


	rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));

	if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE))
	{
		//	In the P2P client mode, the driver should not switch back to its listen channel
		//	because this P2P client should stay at the operating channel of P2P GO.
		set_channel_bwmode( padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}
_func_exit_;
}

void pre_tx_invitereq_handler( _adapter*	padapter )
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	uint8_t	val8 = 1;
_func_enter_;

	set_channel_bwmode(padapter, pwdinfo->invitereq_info.peer_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_SITESURVEY, (uint8_t *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );

_func_exit_;
}

void pre_tx_provdisc_handler( _adapter*	padapter )
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	uint8_t	val8 = 1;
_func_enter_;

	set_channel_bwmode(padapter, pwdinfo->tx_prov_disc_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (uint8_t *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );

_func_exit_;
}

void pre_tx_negoreq_handler( _adapter*	padapter )
{
	struct wifidirect_info  *pwdinfo = &padapter->wdinfo;
	uint8_t	val8 = 1;
_func_enter_;

	set_channel_bwmode(padapter, pwdinfo->nego_req_info.peer_channel_num[0], HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	rtw_hal_set_hwreg(padapter, HW_VAR_MLME_SITESURVEY, (uint8_t *)(&val8));
	issue_probereq_p2p(padapter, NULL);
	_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );

_func_exit_;
}


#ifdef CONFIG_IOCTL_CFG80211
static void ro_ch_handler(_adapter *padapter)
{
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
_func_enter_;

	{

		if( pcfg80211_wdinfo->restore_channel != pmlmeext->cur_channel )
		{
			if ( !check_fwstate(&padapter->mlmepriv, _FW_LINKED ) )
				pmlmeext->cur_channel = pcfg80211_wdinfo->restore_channel;

			set_channel_bwmode(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
		}

		rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211
		DBG_871X("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif
	}

	pcfg80211_wdinfo->is_ro_ch = _FALSE;

	DBG_871X("cfg80211_remain_on_channel_expired\n");

	rtw_cfg80211_remain_on_channel_expired(padapter,
		pcfg80211_wdinfo->remain_on_ch_cookie,
		&pcfg80211_wdinfo->remain_on_ch_channel,
		pcfg80211_wdinfo->remain_on_ch_type, GFP_KERNEL);

_func_exit_;
}

static void ro_ch_timer_process (void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(adapter->rtw_wdev);

	//printk("%s \n", __FUNCTION__);

	p2p_protocol_wk_cmd( adapter, P2P_RO_CH_WK);
}

static void rtw_change_p2pie_op_ch(_adapter *padapter, const uint8_t *frame_body, uint32_t	 len, uint8_t ch)
{
	uint8_t *ies, *p2p_ie;
	uint32_t	 ies_len, p2p_ielen;
	PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;
	struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

	ies = (uint8_t *)(frame_body + _PUBLIC_ACTION_IE_OFFSET_);
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie( ies, ies_len, NULL, &p2p_ielen );

	while ( p2p_ie ) {
		uint32_t	attr_contentlen = 0;
		uint8_t *pattr = NULL;

		//Check P2P_ATTR_OPERATING_CH
		attr_contentlen = 0;
		pattr = NULL;
		if((pattr = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, (uint*)&attr_contentlen))!=NULL)
		{
			*(pattr+4) = ch;
		}

		//Get the next P2P IE
		p2p_ie = rtw_get_p2p_ie(p2p_ie+p2p_ielen, ies_len -(p2p_ie -ies + p2p_ielen), NULL, &p2p_ielen);
	}
}

static void rtw_change_p2pie_ch_list(_adapter *padapter, const uint8_t *frame_body, uint32_t	 len, uint8_t ch)
{
	uint8_t *ies, *p2p_ie;
	uint32_t	 ies_len, p2p_ielen;
	PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;
	struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

	ies = (uint8_t *)(frame_body + _PUBLIC_ACTION_IE_OFFSET_);
	ies_len = len - _PUBLIC_ACTION_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie( ies, ies_len, NULL, &p2p_ielen );

	while (p2p_ie) {
		uint32_t	attr_contentlen = 0;
		uint8_t *pattr = NULL;

		//Check P2P_ATTR_CH_LIST
		if ((pattr=rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL, (uint*)&attr_contentlen))!=NULL) {
			int i;
			uint32_t	 num_of_ch;
			uint8_t *pattr_temp = pattr + 3 ;

			attr_contentlen -= 3;

			while (attr_contentlen>0) {
				num_of_ch = *(pattr_temp+1);

				for(i=0; i<num_of_ch; i++)
					*(pattr_temp+2+i) = ch;

				pattr_temp += (2+num_of_ch);
				attr_contentlen -= (2+num_of_ch);
			}
		}

		//Get the next P2P IE
		p2p_ie = rtw_get_p2p_ie(p2p_ie+p2p_ielen, ies_len -(p2p_ie -ies + p2p_ielen), NULL, &p2p_ielen);
	}
}

static bool rtw_chk_p2pie_ch_list_with_buddy(_adapter *padapter, const uint8_t *frame_body, uint32_t	 len)
{
	bool fit = _FALSE;
	return fit;
}

static bool rtw_chk_p2pie_op_ch_with_buddy(_adapter *padapter, const uint8_t *frame_body, uint32_t	 len)
{
	bool fit = _FALSE;
	return fit;
}

static void rtw_cfg80211_adjust_p2pie_channel(_adapter *padapter, const uint8_t *frame_body, uint32_t	 len)
{
}

uint8_t *dump_p2p_attr_ch_list(uint8_t *p2p_ie, uint p2p_ielen, uint8_t *buf, uint32_t	 buf_len)
{
	uint attr_contentlen = 0;
	uint8_t *pattr = NULL;
	int w_sz = 0;
	uint8_t ch_cnt = 0;
	uint8_t ch_list[40];
	bool continuous = _FALSE;

	if ((pattr=rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CH_LIST, NULL, &attr_contentlen))!=NULL) {
		int i, j;
		uint32_t	 num_of_ch;
		uint8_t *pattr_temp = pattr + 3 ;

		attr_contentlen -= 3;

		memset(ch_list, 0, 40);

		while (attr_contentlen>0) {
			num_of_ch = *(pattr_temp+1);

			for(i=0; i<num_of_ch; i++) {
				for (j=0;j<ch_cnt;j++) {
					if (ch_list[j] == *(pattr_temp+2+i))
						break;
				}
				if (j>=ch_cnt)
					ch_list[ch_cnt++] = *(pattr_temp+2+i);

			}

			pattr_temp += (2+num_of_ch);
			attr_contentlen -= (2+num_of_ch);
		}


		for (j=0;j<ch_cnt;j++) {
			if (j == 0) {
				w_sz += snprintf(buf+w_sz, buf_len-w_sz, "%u", ch_list[j]);
			} else if (ch_list[j] - ch_list[j-1] != 1) {
				w_sz += snprintf(buf+w_sz, buf_len-w_sz, ", %u", ch_list[j]);
			} else if (j != ch_cnt-1 && ch_list[j+1] - ch_list[j] == 1) {
				/* empty */
			} else {
				w_sz += snprintf(buf+w_sz, buf_len-w_sz, "-%u", ch_list[j]);
			}
		}
	}
	return buf;
}

int rtw_p2p_check_frames(_adapter *padapter, const uint8_t *buf, uint32_t	 len, uint8_t tx)
{
	int is_p2p_frame = (-1);
	unsigned char	*frame_body;
	uint8_t category, action, OUI_Subtype, dialogToken=0;
	uint8_t *p2p_ie = NULL;
	uint p2p_ielen = 0;
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	int status = -1;
	uint8_t ch_list_buf[128] = {'\0'};
	int op_ch = -1;
	uint8_t intent = 0;

	frame_body = (unsigned char *)(buf + sizeof(struct rtw_ieee80211_hdr_3addr));
	category = frame_body[0];
	//just for check
	if(category == RTW_WLAN_CATEGORY_PUBLIC)
	{
		action = frame_body[1];
		if (action == ACT_PUBLIC_VENDOR
			&& _rtw_memcmp(frame_body+2, P2P_OUI, 4) == _TRUE
		)
		{
			OUI_Subtype = frame_body[6];
			dialogToken = frame_body[7];
			is_p2p_frame = OUI_Subtype;
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_871X("ACTION_CATEGORY_PUBLIC: ACT_PUBLIC_VENDOR, OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n",
				cpu_to_be32( *( ( u32* ) ( frame_body + 2 ) ) ), OUI_Subtype, dialogToken);
			#endif

			p2p_ie = rtw_get_p2p_ie(
				(uint8_t *)buf+sizeof(struct rtw_ieee80211_hdr_3addr)+_PUBLIC_ACTION_IE_OFFSET_,
				len-sizeof(struct rtw_ieee80211_hdr_3addr)-_PUBLIC_ACTION_IE_OFFSET_,
				NULL, &p2p_ielen);

			switch( OUI_Subtype )//OUI Subtype
			{
				uint8_t *cont;
				uint cont_len;
				case P2P_GO_NEGO_REQ:
					if (tx) {
						#ifdef CONFIG_DRV_ISSUE_PROV_REQ // IOT FOR S2
						if(pwdev_priv->provdisc_req_issued == _FALSE)
							rtw_cfg80211_issue_p2p_provision_request(padapter, buf, len);
						#endif //CONFIG_DRV_ISSUE_PROV_REQ

						//pwdev_priv->provdisc_req_issued = _FALSE;

					}

					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
						op_ch = *(cont+4);
					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT, NULL, &cont_len)))
						intent = *cont;
					dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
					DBG_871X("RTW_%s:P2P_GO_NEGO_REQ, dialogToken=%d, intent:%u%s, op_ch:%d, ch_list:%s\n",
							(tx==_TRUE)?"Tx":"Rx", dialogToken, (intent>>1), intent&0x1 ? "+" : "-", op_ch, ch_list_buf);

					if (!tx) {
					}

					break;
				case P2P_GO_NEGO_RESP:
					if (tx) {
					}

					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
						op_ch = *(cont+4);
					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_GO_INTENT, NULL, &cont_len)))
						intent = *cont;
					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len)))
						status = *cont;
					dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
					DBG_871X("RTW_%s:P2P_GO_NEGO_RESP, dialogToken=%d, intent:%u%s, status:%d, op_ch:%d, ch_list:%s\n",
							(tx==_TRUE)?"Tx":"Rx", dialogToken, (intent>>1), intent&0x1 ? "+" : "-", status, op_ch, ch_list_buf);

					if (!tx) {
						pwdev_priv->provdisc_req_issued = _FALSE;
					}

					break;
				case P2P_GO_NEGO_CONF:
					if (tx) {
					}

					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
						op_ch = *(cont+4);
					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len)))
						status = *cont;
					dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
					DBG_871X("RTW_%s:P2P_GO_NEGO_CONF, dialogToken=%d, status:%d, op_ch:%d, ch_list:%s\n",
						(tx==_TRUE)?"Tx":"Rx", dialogToken, status, op_ch, ch_list_buf);

					if (!tx) {
					}

					break;
				case P2P_INVIT_REQ:
				{
					struct rtw_wdev_invit_info* invit_info = &pwdev_priv->invit_info;
					int flags = -1;

					if (tx) {
					}

					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_INVITATION_FLAGS, NULL, &cont_len)))
						flags = *cont;
					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
						op_ch = *(cont+4);

					if (invit_info->token != dialogToken)
						rtw_wdev_invit_info_init(invit_info);

					invit_info->token = dialogToken;
					invit_info->flags = (flags==-1) ? 0x0 : flags;
					invit_info->req_op_ch= op_ch;

					dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
					DBG_871X("RTW_%s:P2P_INVIT_REQ, dialogToken=%d, flags:0x%02x, op_ch:%d, ch_list:%s\n",
							(tx==_TRUE)?"Tx":"Rx", dialogToken, flags, op_ch, ch_list_buf);

					if (!tx) {
					}

					break;
				}
				case P2P_INVIT_RESP:
				{
					struct rtw_wdev_invit_info* invit_info = &pwdev_priv->invit_info;

					if (tx) {
					}

					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len)))
						status = *cont;
					if ((cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_OPERATING_CH, NULL, &cont_len)))
						op_ch = *(cont+4);

					if (invit_info->token != dialogToken) {
						rtw_wdev_invit_info_init(invit_info);
					} else {
						invit_info->token = 0;
						invit_info->status = (status==-1) ? 0xff : status;
						invit_info->rsp_op_ch= op_ch;
					}

					dump_p2p_attr_ch_list(p2p_ie, p2p_ielen, ch_list_buf, 128);
					DBG_871X("RTW_%s:P2P_INVIT_RESP, dialogToken=%d, status:%d, op_ch:%d, ch_list:%s\n",
							(tx==_TRUE)?"Tx":"Rx", dialogToken, status, op_ch, ch_list_buf);

					if (!tx) {
					}

					break;
				}
				case P2P_DEVDISC_REQ:
					DBG_871X("RTW_%s:P2P_DEVDISC_REQ, dialogToken=%d\n", (tx==_TRUE)?"Tx":"Rx", dialogToken);
					break;
				case P2P_DEVDISC_RESP:
					cont = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, NULL, &cont_len);
					DBG_871X("RTW_%s:P2P_DEVDISC_RESP, dialogToken=%d, status:%d\n", (tx==_TRUE)?"Tx":"Rx", dialogToken, cont?*cont:-1);
					break;
				case P2P_PROVISION_DISC_REQ:
				{
					size_t frame_body_len = len - sizeof(struct rtw_ieee80211_hdr_3addr);
					uint8_t *p2p_ie;
					uint p2p_ielen = 0;
					uint contentlen = 0;

					DBG_871X("RTW_%s:P2P_PROVISION_DISC_REQ, dialogToken=%d\n", (tx==_TRUE)?"Tx":"Rx", dialogToken);

					//if(tx)
					{
						pwdev_priv->provdisc_req_issued = _FALSE;

						if( (p2p_ie=rtw_get_p2p_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, frame_body_len - _PUBLIC_ACTION_IE_OFFSET_, NULL, &p2p_ielen)))
						{

							if(rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, NULL, &contentlen))
							{
								pwdev_priv->provdisc_req_issued = _FALSE;//case: p2p_client join p2p GO
							}
							else
							{
								#ifdef CONFIG_DEBUG_CFG80211
								DBG_871X("provdisc_req_issued is _TRUE\n");
								#endif //CONFIG_DEBUG_CFG80211
								pwdev_priv->provdisc_req_issued = _TRUE;//case: p2p_devices connection before Nego req.
							}

						}
					}
				}
					break;
				case P2P_PROVISION_DISC_RESP:
					DBG_871X("RTW_%s:P2P_PROVISION_DISC_RESP, dialogToken=%d\n", (tx==_TRUE)?"Tx":"Rx", dialogToken);
					break;
				default:
					DBG_871X("RTW_%s:OUI_Subtype=%d, dialogToken=%d\n", (tx==_TRUE)?"Tx":"Rx", OUI_Subtype, dialogToken);
					break;
			}

		}

	}
	else if(category == RTW_WLAN_CATEGORY_P2P)
	{
		OUI_Subtype = frame_body[5];
		dialogToken = frame_body[6];

		#ifdef CONFIG_DEBUG_CFG80211
		DBG_871X("ACTION_CATEGORY_P2P: OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n",
			cpu_to_be32( *( ( u32* ) ( frame_body + 1 ) ) ), OUI_Subtype, dialogToken);
		#endif

		is_p2p_frame = OUI_Subtype;

		switch(OUI_Subtype)
		{
			case P2P_NOTICE_OF_ABSENCE:
				DBG_871X("RTW_%s:P2P_NOTICE_OF_ABSENCE, dialogToken=%d\n", (tx==_TRUE)?"TX":"RX", dialogToken);
				break;
			case P2P_PRESENCE_REQUEST:
				DBG_871X("RTW_%s:P2P_PRESENCE_REQUEST, dialogToken=%d\n", (tx==_TRUE)?"TX":"RX", dialogToken);
				break;
			case P2P_PRESENCE_RESPONSE:
				DBG_871X("RTW_%s:P2P_PRESENCE_RESPONSE, dialogToken=%d\n", (tx==_TRUE)?"TX":"RX", dialogToken);
				break;
			case P2P_GO_DISC_REQUEST:
				DBG_871X("RTW_%s:P2P_GO_DISC_REQUEST, dialogToken=%d\n", (tx==_TRUE)?"TX":"RX", dialogToken);
				break;
			default:
				DBG_871X("RTW_%s:OUI_Subtype=%d, dialogToken=%d\n", (tx==_TRUE)?"TX":"RX", OUI_Subtype, dialogToken);
				break;
		}

	}
	else
	{
		DBG_871X("RTW_%s:action frame category=%d\n", (tx==_TRUE)?"TX":"RX", category);
		//is_p2p_frame = (-1);
	}

	return is_p2p_frame;
}

void rtw_init_cfg80211_wifidirect_info( _adapter*	padapter)
{
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;

	memset(pcfg80211_wdinfo, 0x00, sizeof(struct cfg80211_wifidirect_info) );

	_init_timer( &pcfg80211_wdinfo->remain_on_ch_timer, padapter->ndev, ro_ch_timer_process, padapter );
}
#endif //CONFIG_IOCTL_CFG80211

void p2p_protocol_wk_hdl(_adapter *padapter, int intCmdType)
{
	struct wifidirect_info	*pwdinfo= &(padapter->wdinfo);

_func_enter_;

	switch(intCmdType)
	{
		case P2P_FIND_PHASE_WK:
		{
			find_phase_handler( padapter );
			break;
		}
		case P2P_RESTORE_STATE_WK:
		{
			restore_p2p_state_handler( padapter );
			break;
		}
		case P2P_PRE_TX_PROVDISC_PROCESS_WK:
		{
			pre_tx_provdisc_handler( padapter );
			break;
		}
		case P2P_PRE_TX_INVITEREQ_PROCESS_WK:
		{
			pre_tx_invitereq_handler( padapter );
			break;
		}
		case P2P_PRE_TX_NEGOREQ_PROCESS_WK:
		{
			pre_tx_negoreq_handler( padapter );
			break;
		}
#ifdef CONFIG_P2P
#endif
#ifdef CONFIG_IOCTL_CFG80211
		case P2P_RO_CH_WK:
		{
			ro_ch_handler( padapter );
			break;
		}
#endif //CONFIG_IOCTL_CFG80211

	}

_func_exit_;
}

#ifdef CONFIG_P2P_PS
void process_p2p_ps_ie(PADAPTER padapter, uint8_t *IEs, uint32_t	 IELength)
{
	uint8_t * ies;
	uint32_t	 ies_len;
	uint8_t * p2p_ie;
	uint32_t	p2p_ielen = 0;
	uint8_t	noa_attr[MAX_P2P_IE_LEN] = { 0x00 };// NoA length should be n*(13) + 2
	uint32_t	attr_contentlen = 0;

	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	uint8_t	find_p2p = _FALSE, find_p2p_ps = _FALSE;
	uint8_t	noa_offset, noa_num, noa_index;

_func_enter_;

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		return;
	}
	if(IELength <= _BEACON_IE_OFFSET_)
		return;

	ies = IEs + _BEACON_IE_OFFSET_;
	ies_len = IELength - _BEACON_IE_OFFSET_;

	p2p_ie = rtw_get_p2p_ie( ies, ies_len, NULL, &p2p_ielen);

	while(p2p_ie)
	{
		find_p2p = _TRUE;
		// Get Notice of Absence IE.
		if(rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_NOA, noa_attr, &attr_contentlen))
		{
			find_p2p_ps = _TRUE;
			noa_index = noa_attr[0];

			if( (pwdinfo->p2p_ps_mode == P2P_PS_NONE) ||
				(noa_index != pwdinfo->noa_index) )// if index change, driver should reconfigure related setting.
			{
				pwdinfo->noa_index = noa_index;
				pwdinfo->opp_ps = noa_attr[1] >> 7;
				pwdinfo->ctwindow = noa_attr[1] & 0x7F;

				noa_offset = 2;
				noa_num = 0;
				// NoA length should be n*(13) + 2
				if(attr_contentlen > 2)
				{
					while(noa_offset < attr_contentlen)
					{
						//memcpy(&wifidirect_info->noa_count[noa_num], &noa_attr[noa_offset], 1);
						pwdinfo->noa_count[noa_num] = noa_attr[noa_offset];
						noa_offset += 1;

						memcpy(&pwdinfo->noa_duration[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						memcpy(&pwdinfo->noa_interval[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						memcpy(&pwdinfo->noa_start_time[noa_num], &noa_attr[noa_offset], 4);
						noa_offset += 4;

						noa_num++;
					}
				}
				pwdinfo->noa_num = noa_num;

				if( pwdinfo->opp_ps == 1 )
				{
					pwdinfo->p2p_ps_mode = P2P_PS_CTWINDOW;
					// driver should wait LPS for entering CTWindow
					if(padapter->pwrctrlpriv.bFwCurrentInPSMode == _TRUE)
					{
						p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 1);
					}
				}
				else if( pwdinfo->noa_num > 0 )
				{
					pwdinfo->p2p_ps_mode = P2P_PS_NOA;
					p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 1);
				}
				else if( pwdinfo->p2p_ps_mode > P2P_PS_NONE)
				{
					p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
				}
			}

			break; // find target, just break.
		}

		//Get the next P2P IE
		p2p_ie = rtw_get_p2p_ie(p2p_ie+p2p_ielen, ies_len -(p2p_ie -ies + p2p_ielen), NULL, &p2p_ielen);

	}

	if(find_p2p == _TRUE)
	{
		if( (pwdinfo->p2p_ps_mode > P2P_PS_NONE) && (find_p2p_ps == _FALSE) )
		{
			p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
		}
	}

_func_exit_;
}

void p2p_ps_wk_hdl(_adapter *padapter, uint8_t p2p_ps_state)
{
	struct pwrctrl_priv		*pwrpriv = &padapter->pwrctrlpriv;
	struct wifidirect_info	*pwdinfo= &(padapter->wdinfo);

_func_enter_;

	// Pre action for p2p state
	switch(p2p_ps_state)
	{
		case P2P_PS_DISABLE:
			pwdinfo->p2p_ps_state = p2p_ps_state;

			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_P2P_PS_OFFLOAD, (uint8_t *)(&p2p_ps_state));

			pwdinfo->noa_index = 0;
			pwdinfo->ctwindow = 0;
			pwdinfo->opp_ps = 0;
			pwdinfo->noa_num = 0;
			pwdinfo->p2p_ps_mode = P2P_PS_NONE;
			if(padapter->pwrctrlpriv.bFwCurrentInPSMode == _TRUE)
			{
				if(pwrpriv->smart_ps == 0)
				{
					pwrpriv->smart_ps = 2;
					rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (uint8_t *)(&(padapter->pwrctrlpriv.pwr_mode)));
				}
			}
			break;
		case P2P_PS_ENABLE:
			if (pwdinfo->p2p_ps_mode > P2P_PS_NONE) {
				pwdinfo->p2p_ps_state = p2p_ps_state;

				if( pwdinfo->ctwindow > 0 )
				{
					if(pwrpriv->smart_ps != 0)
					{
						pwrpriv->smart_ps = 0;
						DBG_871X("%s(): Enter CTW, change SmartPS\n", __FUNCTION__);
						rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (uint8_t *)(&(padapter->pwrctrlpriv.pwr_mode)));
					}
				}
				rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_P2P_PS_OFFLOAD, (uint8_t *)(&p2p_ps_state));
			}
			break;
		case P2P_PS_SCAN:
		case P2P_PS_SCAN_DONE:
		case P2P_PS_ALLSTASLEEP:
			if (pwdinfo->p2p_ps_mode > P2P_PS_NONE) {
				pwdinfo->p2p_ps_state = p2p_ps_state;
				rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_P2P_PS_OFFLOAD, (uint8_t *)(&p2p_ps_state));
			}
			break;
		default:
			break;
	}

_func_exit_;
}

uint8_t p2p_ps_wk_cmd(_adapter*padapter, uint8_t p2p_ps_state, uint8_t enqueue)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct wifidirect_info	*pwdinfo= &(padapter->wdinfo);
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	uint8_t	res = _SUCCESS;

_func_enter_;

	if ( rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
		)
	{
		return res;
	}

	if(enqueue)
	{
		ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj));
		if(ph2c==NULL){
			res= _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm*)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if(pdrvextra_cmd_parm==NULL){
			rtw_mfree(ph2c);
			res= _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = P2P_PS_WK_CID;
		pdrvextra_cmd_parm->type_size = p2p_ps_state;
		pdrvextra_cmd_parm->pbuf = NULL;

		init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

		res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	}
	else
	{
		p2p_ps_wk_hdl(padapter, p2p_ps_state);
	}

exit:

_func_exit_;

	return res;

}
#endif // CONFIG_P2P_PS

static void reset_ch_sitesurvey_timer_process (void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	DBG_871X( "[%s] In\n", __FUNCTION__ );
	//	Reset the operation channel information
	pwdinfo->rx_invitereq_info.operation_ch[0] = 0;
	pwdinfo->rx_invitereq_info.scan_op_ch_only = 0;
}

static void reset_ch_sitesurvey_timer_process2 (void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	DBG_871X( "[%s] In\n", __FUNCTION__ );
	//	Reset the operation channel information
	pwdinfo->p2p_info.operation_ch[0] = 0;
	pwdinfo->p2p_info.scan_op_ch_only = 0;
}

static void restore_p2p_state_timer_process (void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	p2p_protocol_wk_cmd( adapter, P2P_RESTORE_STATE_WK );
}

static void pre_tx_scan_timer_process (void *FunctionContext)
{
	_adapter 							*adapter = (_adapter *) FunctionContext;
	struct	wifidirect_info				*pwdinfo = &adapter->wdinfo;
	_irqL							irqL;
	struct mlme_priv					*pmlmepriv = &adapter->mlmepriv;
	uint8_t								_status = 0;

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);


	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ))
	{
		if ( _TRUE == pwdinfo->tx_prov_disc_info.benable )	//	the provision discovery request frame is trigger to send or not
		{
			p2p_protocol_wk_cmd( adapter, P2P_PRE_TX_PROVDISC_PROCESS_WK );
			//issue_probereq_p2p(adapter, NULL);
			//_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );
		}
	}
	else if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_ING))
	{
		if ( _TRUE == pwdinfo->nego_req_info.benable )
		{
			p2p_protocol_wk_cmd( adapter, P2P_PRE_TX_NEGOREQ_PROCESS_WK );
		}
	}
	else if ( rtw_p2p_chk_state(pwdinfo, P2P_STATE_TX_INVITE_REQ ) )
	{
		if ( _TRUE == pwdinfo->invitereq_info.benable )
		{
			p2p_protocol_wk_cmd( adapter, P2P_PRE_TX_INVITEREQ_PROCESS_WK );
		}
	}
	else
	{
		DBG_8192C( "[%s] p2p_state is %d, ignore!!\n", __FUNCTION__, rtw_p2p_state(pwdinfo) );
	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}

static void find_phase_timer_process (void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;
	struct	wifidirect_info		*pwdinfo = &adapter->wdinfo;

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;

	adapter->wdinfo.find_phase_state_exchange_cnt++;

	p2p_protocol_wk_cmd( adapter, P2P_FIND_PHASE_WK );
}


void reset_global_wifidirect_info( _adapter* padapter )
{
	struct wifidirect_info	*pwdinfo;

	pwdinfo = &padapter->wdinfo;
	pwdinfo->persistent_supported = 0;
	pwdinfo->session_available = _TRUE;
	pwdinfo->wfd_tdls_enable = 0;
	pwdinfo->wfd_tdls_weaksec = 0;
}


void rtw_init_wifidirect_timers(_adapter* padapter)
{
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	_init_timer( &pwdinfo->find_phase_timer, padapter->ndev, find_phase_timer_process, padapter );
	_init_timer( &pwdinfo->restore_p2p_state_timer, padapter->ndev, restore_p2p_state_timer_process, padapter );
	_init_timer( &pwdinfo->pre_tx_scan_timer, padapter->ndev, pre_tx_scan_timer_process, padapter );
	_init_timer( &pwdinfo->reset_ch_sitesurvey, padapter->ndev, reset_ch_sitesurvey_timer_process, padapter );
	_init_timer( &pwdinfo->reset_ch_sitesurvey2, padapter->ndev, reset_ch_sitesurvey_timer_process2, padapter );
}

void rtw_init_wifidirect_addrs(_adapter* padapter, uint8_t *dev_addr, uint8_t *iface_addr)
{
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	/*init device&interface address */
	if (dev_addr) {
		memcpy(pwdinfo->device_addr, dev_addr, ETH_ALEN);
	}
	if (iface_addr) {
		memcpy(pwdinfo->interface_addr, iface_addr, ETH_ALEN);
	}
#endif
}

void init_wifidirect_info( _adapter* padapter, enum P2P_ROLE role)
{
	struct wifidirect_info	*pwdinfo;

	pwdinfo = &padapter->wdinfo;

	pwdinfo->padapter = padapter;

	//	1, 6, 11 are the social channel defined in the WiFi Direct specification.
	pwdinfo->social_chan[0] = 1;
	pwdinfo->social_chan[1] = 6;
	pwdinfo->social_chan[2] = 11;
	pwdinfo->social_chan[3] = 0;	//	channel 0 for scanning ending in site survey function.

	{
		//	Use the channel 11 as the listen channel
		pwdinfo->listen_channel = 11;
	}

	if (role == P2P_ROLE_DEVICE)
	{
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
		{
			rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);
		}
		pwdinfo->intent = 1;
		rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_LISTEN);
	}
	else if (role == P2P_ROLE_CLIENT)
	{
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
		pwdinfo->intent = 1;
		rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
	}
	else if (role == P2P_ROLE_GO)
	{
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
		pwdinfo->intent = 15;
		rtw_p2p_set_pre_state(pwdinfo, P2P_STATE_GONEGO_OK);
	}

//	Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 )
	pwdinfo->support_rate[0] = 0x8c;	//	6(B)
	pwdinfo->support_rate[1] = 0x92;	//	9(B)
	pwdinfo->support_rate[2] = 0x18;	//	12
	pwdinfo->support_rate[3] = 0x24;	//	18
	pwdinfo->support_rate[4] = 0x30;	//	24
	pwdinfo->support_rate[5] = 0x48;	//	36
	pwdinfo->support_rate[6] = 0x60;	//	48
	pwdinfo->support_rate[7] = 0x6c;	//	54

	memcpy( ( void* ) pwdinfo->p2p_wildcard_ssid, "DIRECT-", 7 );

	memset( pwdinfo->device_name, 0x00, WPS_MAX_DEVICE_NAME_LEN );
	pwdinfo->device_name_len = 0;

	memset( &pwdinfo->invitereq_info, 0x00, sizeof( struct tx_invite_req_info ) );
	pwdinfo->invitereq_info.token = 3;	//	Token used for P2P invitation request frame.

	memset( &pwdinfo->inviteresp_info, 0x00, sizeof( struct tx_invite_resp_info ) );
	pwdinfo->inviteresp_info.token = 0;

	pwdinfo->profileindex = 0;
	memset( &pwdinfo->profileinfo[ 0 ], 0x00, sizeof( struct profile_info ) * P2P_MAX_PERSISTENT_GROUP_NUM );

	rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);

	pwdinfo->listen_dwell = ( uint8_t ) (( rtw_get_current_time() % 3 ) + 1);
	//DBG_8192C( "[%s] listen_dwell time is %d00ms\n", __FUNCTION__, pwdinfo->listen_dwell );

	memset( &pwdinfo->tx_prov_disc_info, 0x00, sizeof( struct tx_provdisc_req_info ) );
	pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_NONE;

	memset( &pwdinfo->nego_req_info, 0x00, sizeof( struct tx_nego_req_info ) );

	pwdinfo->device_password_id_for_nego = WPS_DPID_PBC;
	pwdinfo->negotiation_dialog_token = 1;

	memset( pwdinfo->nego_ssid, 0x00, WLAN_SSID_MAXLEN );
	pwdinfo->nego_ssidlen = 0;

	pwdinfo->ui_got_wps_info = P2P_NO_WPSINFO;
	pwdinfo->supported_wps_cm = WPS_CONFIG_METHOD_DISPLAY | WPS_CONFIG_METHOD_PBC | WPS_CONFIG_METHOD_KEYPAD;
	pwdinfo->channel_list_attr_len = 0;
	memset( pwdinfo->channel_list_attr, 0x00, 100 );

	memset( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, 0x00, 4 );
	memset( pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req, '0', 3 );
	memset( &pwdinfo->groupid_info, 0x00, sizeof( struct group_id_info ) );

// Commented by Kurt 20130319
// For WiDi purpose: Use CFG80211 interface but controled WFD/RDS frame by driver itself.
#ifdef CONFIG_IOCTL_CFG80211
	pwdinfo->driver_interface = DRIVER_CFG80211;
#else
	pwdinfo->driver_interface = DRIVER_WEXT;
#endif //CONFIG_IOCTL_CFG80211

	pwdinfo->wfd_tdls_enable = 0;
	memset( pwdinfo->p2p_peer_interface_addr, 0x00, ETH_ALEN );
	memset( pwdinfo->p2p_peer_device_addr, 0x00, ETH_ALEN );

	pwdinfo->rx_invitereq_info.operation_ch[0] = 0;
	pwdinfo->rx_invitereq_info.operation_ch[1] = 0;	//	Used to indicate the scan end in site survey function
	pwdinfo->rx_invitereq_info.scan_op_ch_only = 0;
	pwdinfo->p2p_info.operation_ch[0] = 0;
	pwdinfo->p2p_info.operation_ch[1] = 0;			//	Used to indicate the scan end in site survey function
	pwdinfo->p2p_info.scan_op_ch_only = 0;
}

#ifdef CONFIG_DBG_P2P
char * p2p_role_str[] = {
	"P2P_ROLE_DISABLE",
	"P2P_ROLE_DEVICE",
	"P2P_ROLE_CLIENT",
	"P2P_ROLE_GO"
};

char * p2p_state_str[] = {
	"P2P_STATE_NONE",
	"P2P_STATE_IDLE",
	"P2P_STATE_LISTEN",
	"P2P_STATE_SCAN",
	"P2P_STATE_FIND_PHASE_LISTEN",
	"P2P_STATE_FIND_PHASE_SEARCH",
	"P2P_STATE_TX_PROVISION_DIS_REQ",
	"P2P_STATE_RX_PROVISION_DIS_RSP",
	"P2P_STATE_RX_PROVISION_DIS_REQ",
	"P2P_STATE_GONEGO_ING",
	"P2P_STATE_GONEGO_OK",
	"P2P_STATE_GONEGO_FAIL",
	"P2P_STATE_RECV_INVITE_REQ_MATCH",
	"P2P_STATE_PROVISIONING_ING",
	"P2P_STATE_PROVISIONING_DONE",
	"P2P_STATE_RECV_INVITE_REQ_DISMATCH",
	"P2P_STATE_RECV_INVITE_REQ_GO"
};

void dbg_rtw_p2p_set_state(struct wifidirect_info *wdinfo, enum P2P_STATE state, const char *caller, int line)
{
	if(!_rtw_p2p_chk_state(wdinfo, state)) {
		enum P2P_STATE old_state = _rtw_p2p_state(wdinfo);
		_rtw_p2p_set_state(wdinfo, state);
		DBG_871X("[CONFIG_DBG_P2P]%s:%d set_state from %s to %s\n", caller, line
			, p2p_state_str[old_state], p2p_state_str[_rtw_p2p_state(wdinfo)]
		);
	} else {
		DBG_871X("[CONFIG_DBG_P2P]%s:%d set_state to same state %s\n", caller, line
			, p2p_state_str[_rtw_p2p_state(wdinfo)]
		);
	}
}
void dbg_rtw_p2p_set_pre_state(struct wifidirect_info *wdinfo, enum P2P_STATE state, const char *caller, int line)
{
	if(_rtw_p2p_pre_state(wdinfo) != state) {
		enum P2P_STATE old_state = _rtw_p2p_pre_state(wdinfo);
		_rtw_p2p_set_pre_state(wdinfo, state);
		DBG_871X("[CONFIG_DBG_P2P]%s:%d set_pre_state from %s to %s\n", caller, line
			, p2p_state_str[old_state], p2p_state_str[_rtw_p2p_pre_state(wdinfo)]
		);
	} else {
		DBG_871X("[CONFIG_DBG_P2P]%s:%d set_pre_state to same state %s\n", caller, line
			, p2p_state_str[_rtw_p2p_pre_state(wdinfo)]
		);
	}
}
#if 0
void dbg_rtw_p2p_restore_state(struct wifidirect_info *wdinfo, const char *caller, int line)
{
	if(wdinfo->pre_p2p_state != -1) {
		DBG_871X("[CONFIG_DBG_P2P]%s:%d restore from %s to %s\n", caller, line
			, p2p_state_str[wdinfo->p2p_state], p2p_state_str[wdinfo->pre_p2p_state]
		);
		_rtw_p2p_restore_state(wdinfo);
	} else {
		DBG_871X("[CONFIG_DBG_P2P]%s:%d restore no pre state, cur state %s\n", caller, line
			, p2p_state_str[wdinfo->p2p_state]
		);
	}
}
#endif
void dbg_rtw_p2p_set_role(struct wifidirect_info *wdinfo, enum P2P_ROLE role, const char *caller, int line)
{
	if(wdinfo->role != role) {
		enum P2P_ROLE old_role = wdinfo->role;
		_rtw_p2p_set_role(wdinfo, role);
		DBG_871X("[CONFIG_DBG_P2P]%s:%d set_role from %s to %s\n", caller, line
			, p2p_role_str[old_role], p2p_role_str[wdinfo->role]
		);
	} else {
		DBG_871X("[CONFIG_DBG_P2P]%s:%d set_role to same role %s\n", caller, line
			, p2p_role_str[wdinfo->role]
		);
	}
}
#endif //CONFIG_DBG_P2P


int rtw_p2p_enable(_adapter *padapter, enum P2P_ROLE role)
{
	int ret = _SUCCESS;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	if (role == P2P_ROLE_DEVICE || role == P2P_ROLE_CLIENT|| role == P2P_ROLE_GO)
	{
		uint8_t channel, ch_offset;
		uint16_t bwmode;


		//leave IPS/Autosuspend
		if (_FAIL == rtw_pwr_wakeup(padapter)) {
			ret = _FAIL;
			goto exit;
		}

		//	Added by Albert 2011/03/22
		//	In the P2P mode, the driver should not support the b mode.
		//	So, the Tx packet shouldn't use the CCK rate
		update_tx_basic_rate(padapter, WIRELESS_11AGN);

		//Enable P2P function
		init_wifidirect_info(padapter, role);

		rtw_hal_set_odm_var(padapter,HAL_ODM_P2P_STATE,NULL,_TRUE);

	}
	else if (role == P2P_ROLE_DISABLE)
	{
		if (_FAIL == rtw_pwr_wakeup(padapter)) {
			ret = _FAIL;
			goto exit;
		}

		//Disable P2P function
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			_cancel_timer_ex( &pwdinfo->find_phase_timer );
			_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
			_cancel_timer_ex( &pwdinfo->pre_tx_scan_timer);
			_cancel_timer_ex( &pwdinfo->reset_ch_sitesurvey);
			_cancel_timer_ex( &pwdinfo->reset_ch_sitesurvey2);
			reset_ch_sitesurvey_timer_process( padapter );
			reset_ch_sitesurvey_timer_process2( padapter );
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_DISABLE);
			memset(&pwdinfo->rx_prov_disc_info, 0x00, sizeof(struct rx_provdisc_req_info));
		}

		rtw_hal_set_odm_var(padapter,HAL_ODM_P2P_STATE,NULL,_FALSE);

		//Restore to initial setting.
		update_tx_basic_rate(padapter, padapter->registrypriv.wireless_mode);

		//For WiDi purpose.
#ifdef CONFIG_IOCTL_CFG80211
		pwdinfo->driver_interface = DRIVER_CFG80211;
#else
		pwdinfo->driver_interface = DRIVER_WEXT;
#endif //CONFIG_IOCTL_CFG80211

	}

exit:
	return ret;
}

#endif //CONFIG_P2P

