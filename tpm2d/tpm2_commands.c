/*
 * This file is part of trust|me
 * Copyright(c) 2013 - 2017 Fraunhofer AISEC
 * Fraunhofer-Gesellschaft zur Förderung der angewandten Forschung e.V.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 (GPL 2), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GPL 2 license for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Fraunhofer AISEC <trustme@aisec.fraunhofer.de>
 */

#include "tpm2d.h"

#include "common/mem.h"
#include "common/macro.h"
#include "common/file.h"

#include <tss2/tss.h>
#include <tss2/tssutils.h>
#include <tss2/tssresponsecode.h>
#include <tss2/tssmarshal.h>
#include <tss2/tsstransmit.h>
#include <tss2/Unmarshal_fp.h>

#include <tss2/tssprint.h>

static TSS_CONTEXT *tss_context = NULL;

/************************************************************************************/

void
tss2_init(void)
{
	int ret;

	if (tss_context) {
		WARN("Context already exists");
		return;
	}

	if (TPM_RC_SUCCESS != (ret = TSS_Create(&tss_context)))
		FATAL("Cannot create tss context error code: %08x", ret);

	TSS_SetProperty(NULL, TPM_TRACE_LEVEL, "2");
}

void
tss2_destroy(void)
{
	int ret;
	IF_NULL_RETURN_ERROR(tss_context);

	if (TPM_RC_SUCCESS != (ret = TSS_Delete(tss_context)))
		FATAL("Cannot destroy tss context error code: %08x", ret);

	tss_context = NULL;
}

char *
convert_bin_to_hex_new(const uint8_t *bin, int length)
{
	char *hex = mem_alloc0(sizeof(char)*length*2 + 1);

	for (int i=0; i < length; ++i) {
		// remember snprintf additionally writs a '0' byte
		snprintf(hex+i*2, 3, "%.2x", bin[i]);
	}

	return hex;
}

uint8_t *
convert_hex_to_bin_new(const char *hex_str, int *out_length)
{
	int len = strlen(hex_str);
	int i = 0, j = 0;
	*out_length = (len+1)/2;

	uint8_t *bin = mem_alloc0(*out_length);

	if (len % 2 == 1)
	{
		// odd length -> we need to pad
		IF_FALSE_GOTO(sscanf(&(hex_str[0]), "%1hhx", &(bin[0])) == 1, err);
		i = j = 1;
	}

	for (; i < len; i+=2, j++)
	{
		IF_FALSE_GOTO(sscanf(&(hex_str[i]), "%2hhx", &(bin[j])) == 1, err);
	}

	return bin;
err:
	ERROR("Converstion of hex string to bin failed!");
	mem_free(bin);
	return NULL;
}

#ifndef TPM2D_NVMCRYPT_ONLY
static char *
halg_id_to_string_new(TPM_ALG_ID alg_id)
{
	switch (alg_id) {
		case TPM_ALG_SHA1:
			return mem_printf("TPM_ALG_SHA1");
		case TPM_ALG_SHA256:
			return mem_printf("TPM_ALG_SHA256");
		case TPM_ALG_SHA384:
			return mem_printf("TPM_ALG_SHA384");
		default:
			return "NONE";
	}
}

static char *
tpm2d_marshal_structure_new(void *structure, MarshalFunction_t marshal_function)
{
	uint8_t *bin_stream = NULL;
	char *hex_stream = NULL;

	uint16_t written_size = 0;

	if (TPM_RC_SUCCESS != TSS_Structure_Marshal(&bin_stream, &written_size,
						structure, marshal_function)) {
		WARN("no data written to stream!");
		goto err;
	}

	hex_stream = convert_bin_to_hex_new(bin_stream, written_size*2 + 1);
err:
	mem_free(bin_stream);
	return hex_stream;
}
#endif

/************************************************************************************/

TPM_RC
tpm2_powerup(void)
{
	TPM_RC rc = TPM_RC_SUCCESS;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	if (TPM_RC_SUCCESS != (rc = TSS_TransmitPlatform(tss_context, TPM_SIGNAL_POWER_OFF, "TPM2_PowerOffPlatform")))
		goto err;

	if (TPM_RC_SUCCESS != (rc = TSS_TransmitPlatform(tss_context, TPM_SIGNAL_POWER_ON, "TPM2_PowerOnPlatform")))
		goto err;

	rc = TSS_TransmitPlatform(tss_context, TPM_SIGNAL_NV_ON, "TPM2_NvOnPlatform");
err:
	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_PowerUp failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}

	return rc;
}

TPM_RC
tpm2_startup(TPM_SU startup_type)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	Startup_In in;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	in.startupType = startup_type;

	rc = TSS_Execute(tss_context, NULL, (COMMAND_PARAMETERS *)&in, NULL,
			 TPM_CC_Startup, TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_StartUp failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}

	return rc;
}

TPM_RC
tpm2_selftest(void)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	SelfTest_In in;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	in.fullTest = YES;

	rc = TSS_Execute(tss_context, NULL, (COMMAND_PARAMETERS *)&in, NULL,
	                 TPM_CC_SelfTest, TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_SelfTest failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}

	return rc;
}

TPM_RC
tpm2_clear(const char *lockout_pwd)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	Clear_In in;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);
	
	in.authHandle = TPM_RH_LOCKOUT;

	rc = TSS_Execute(tss_context, NULL, (COMMAND_PARAMETERS *)&in, NULL,
			TPM_CC_Clear, TPM_RS_PW, lockout_pwd, 0,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_Clear failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}

	return rc;
}

static TPM_RC
tpm2_startauthsession(TPM_SE session_type, TPMI_SH_AUTH_SESSION *out_session_handle,
		TPMI_DH_OBJECT bind_handle, const char *bind_pwd)
{
	TPM_RC rc;
	StartAuthSession_In in;
	StartAuthSession_Out out;
	StartAuthSession_Extra extra;

	in.sessionType = session_type;

	/* bind password */
	in.bind = bind_handle;
	if (in.bind != TPM_RH_NULL)
		extra.bindPassword = bind_pwd;

	/* salt key default NULL*/
	in.tpmKey = tpm2d_get_salt_key_handle();
	/* encryptedSalt (not required) */
	in.encryptedSalt.b.size = 0;
	/* nonceCaller (not required) */
	in.nonceCaller.t.size = 0;

	/* parameter encryption */
	in.symmetric.algorithm = TPM2D_SYM_SESSION_ALGORITHM;
	if (in.symmetric.algorithm == TPM_ALG_XOR) {
	    /* Table 61 - Definition of (TPM_ALG_ID) TPMI_ALG_SYM Type */
	    /* Table 125 - Definition of TPMU_SYM_KEY_BITS Union */
	    in.symmetric.keyBits.xorr = TPM2D_HASH_ALGORITHM;
	    /* Table 126 - Definition of TPMU_SYM_MODE Union */
	    in.symmetric.mode.sym = TPM_ALG_NULL;
	}
	else { /* TPM_ALG_AES */
	    /* Table 61 - Definition of (TPM_ALG_ID) TPMI_ALG_SYM Type */
	    /* Table 125 - Definition of TPMU_SYM_KEY_BITS Union */
	    in.symmetric.keyBits.aes = 128;
	    /* Table 126 - Definition of TPMU_SYM_MODE Union */
	    /* Table 63 - Definition of (TPM_ALG_ID) TPMI_ALG_SYM_MODE Type */
	    in.symmetric.mode.aes = TPM_ALG_CFB;
	}

	/* authHash */
	in.authHash = TPM2D_HASH_ALGORITHM;

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in,(EXTRA_PARAMETERS *)&extra, TPM_CC_StartAuthSession,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_StartAuthSession failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		return rc;
	}

	// return handle to just created object
	*out_session_handle = out.sessionHandle;

	return rc;
}

TPM_RC
tpm2_flushcontext(TPMI_DH_CONTEXT handle)
{
	TPM_RC rc;
	FlushContext_In in;

	in.flushHandle = handle;

	rc = TSS_Execute(tss_context, NULL,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_FlushContext,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_FlushContext failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}
	return rc;
}

static TPM_RC
tpm2_fill_rsa_details(TPMT_PUBLIC *out_public_area, tpm2d_key_type_t key_type)
{
	ASSERT(out_public_area);

	out_public_area->parameters.rsaDetail.keyBits = 2048;
	out_public_area->parameters.rsaDetail.exponent = 0;

	switch (key_type) {
		case TPM2D_KEY_TYPE_STORAGE_U:
			out_public_area->parameters.rsaDetail.symmetric.algorithm = TPM_ALG_NULL;
			out_public_area->parameters.rsaDetail.scheme.scheme = TPM_ALG_NULL;
			break;
		case TPM2D_KEY_TYPE_STORAGE_R:
			out_public_area->parameters.rsaDetail.symmetric.algorithm = TPM_ALG_AES;
			out_public_area->parameters.rsaDetail.symmetric.keyBits.aes = 128;
			out_public_area->parameters.rsaDetail.symmetric.mode.aes = TPM_ALG_CFB;
			out_public_area->parameters.rsaDetail.scheme.scheme = TPM_ALG_NULL;
			break;
		case TPM2D_KEY_TYPE_SIGNING_U:
			out_public_area->parameters.rsaDetail.symmetric.algorithm = TPM_ALG_NULL;
			out_public_area->parameters.rsaDetail.scheme.scheme = TPM_ALG_NULL;
			break;
		case TPM2D_KEY_TYPE_SIGNING_R:
			out_public_area->parameters.rsaDetail.symmetric.algorithm = TPM_ALG_NULL;
			out_public_area->parameters.rsaDetail.scheme.scheme = TPM_ALG_RSASSA;
			out_public_area->parameters.rsaDetail.scheme.details.rsassa.hashAlg =
									TPM2D_HASH_ALGORITHM;
			break;
		default:
			ERROR("Keytype not supported for rsa keys!");
			return TPM_RC_VALUE;
			break;
	}

	return TPM_RC_SUCCESS;
}

static TPM_RC
tpm2_fill_ecc_details(TPMT_PUBLIC *out_public_area, tpm2d_key_type_t key_type)
{
	ASSERT(out_public_area);

	switch (key_type) {
		case TPM2D_KEY_TYPE_SIGNING_U:
			// non-storage keys require TPM_ALG_NULL set for the symmetric algorithm
			out_public_area->parameters.eccDetail.symmetric.algorithm = TPM_ALG_NULL;
			out_public_area->parameters.eccDetail.scheme.scheme = TPM_ALG_NULL;
			out_public_area->parameters.eccDetail.curveID = TPM2D_CURVE_ID;
			out_public_area->parameters.eccDetail.kdf.scheme = TPM_ALG_NULL;
			break;
		case TPM2D_KEY_TYPE_SIGNING_R:
			// non-storage keys require TPM_ALG_NULL set for the symmetric algorithm
			out_public_area->parameters.eccDetail.symmetric.algorithm = TPM_ALG_NULL;

			out_public_area->parameters.eccDetail.scheme.scheme = TPM_ALG_ECDSA;
			out_public_area->parameters.eccDetail.scheme.details.ecdsa.hashAlg =
									TPM2D_HASH_ALGORITHM;
			out_public_area->parameters.eccDetail.kdf.details.mgf1.hashAlg =
									TPM2D_HASH_ALGORITHM;
			out_public_area->parameters.eccDetail.curveID = TPM2D_CURVE_ID;
			out_public_area->parameters.eccDetail.kdf.scheme = TPM_ALG_NULL;
			break;
		case TPM2D_KEY_TYPE_STORAGE_U:
		case TPM2D_KEY_TYPE_STORAGE_R:
			out_public_area->parameters.eccDetail.symmetric.algorithm = TPM_ALG_AES;
			out_public_area->parameters.eccDetail.symmetric.keyBits.aes = 128;
			out_public_area->parameters.eccDetail.symmetric.mode.aes = TPM_ALG_CFB;
			out_public_area->parameters.eccDetail.scheme.scheme = TPM_ALG_NULL;
			out_public_area->parameters.eccDetail.scheme.details.anySig.hashAlg = 0;
			out_public_area->parameters.eccDetail.curveID = TPM2D_CURVE_ID;
			out_public_area->parameters.eccDetail.kdf.scheme = TPM_ALG_NULL;
			out_public_area->parameters.eccDetail.kdf.details.mgf1.hashAlg = 0;
			break;
		default:
			ERROR("Keytype not supported for ecc keys!");
			return TPM_RC_VALUE;
			break;
	}

	return TPM_RC_SUCCESS;
}

static TPM_RC
tpm2_public_area_helper(TPMT_PUBLIC *out_public_area, TPMA_OBJECT object_attrs, tpm2d_key_type_t key_type)
{
	ASSERT(out_public_area);

	TPM_RC rc = TPM_RC_SUCCESS;

	out_public_area->type = TPM2D_ASYM_ALGORITHM;
	out_public_area->nameAlg = TPM2D_HASH_ALGORITHM;
	out_public_area->objectAttributes = object_attrs;

	out_public_area->objectAttributes.val |= TPMA_OBJECT_SENSITIVEDATAORIGIN;
	out_public_area->objectAttributes.val |= TPMA_OBJECT_USERWITHAUTH;
	out_public_area->objectAttributes.val &= ~TPMA_OBJECT_ADMINWITHPOLICY;

	// set default empty policy
	out_public_area->authPolicy.t.size = 0;

	switch (key_type) {
		case TPM2D_KEY_TYPE_STORAGE_U:
			out_public_area->objectAttributes.val &= ~TPMA_OBJECT_SIGN;
			out_public_area->objectAttributes.val |= TPMA_OBJECT_DECRYPT;
			out_public_area->objectAttributes.val &= ~TPMA_OBJECT_RESTRICTED;
			break;
		case TPM2D_KEY_TYPE_STORAGE_R:
			out_public_area->objectAttributes.val &= ~TPMA_OBJECT_SIGN;
			out_public_area->objectAttributes.val |= TPMA_OBJECT_DECRYPT;
			out_public_area->objectAttributes.val |= TPMA_OBJECT_RESTRICTED;
			break;
		case TPM2D_KEY_TYPE_SIGNING_U:
			out_public_area->objectAttributes.val |= TPMA_OBJECT_SIGN;
			out_public_area->objectAttributes.val &= ~TPMA_OBJECT_DECRYPT;
			out_public_area->objectAttributes.val &= ~TPMA_OBJECT_RESTRICTED;
			break;
		case TPM2D_KEY_TYPE_SIGNING_R:
			out_public_area->objectAttributes.val |= TPMA_OBJECT_SIGN;
			out_public_area->objectAttributes.val &= ~TPMA_OBJECT_DECRYPT;
			out_public_area->objectAttributes.val |= TPMA_OBJECT_RESTRICTED;
			break;
		default:
			ERROR("Only support creation of signing and storage keys!");
			return TPM_RC_VALUE;
			break;
	}

	if (TPM2D_ASYM_ALGORITHM == TPM_ALG_RSA) {
		out_public_area->unique.rsa.t.size = 0;
		rc = tpm2_fill_rsa_details(out_public_area, key_type);
	} else {
		// TPM2D_ASYM_ALGORITHM == TPM_ALG_ECC
		out_public_area->unique.ecc.x.t.size = 0;
		out_public_area->unique.ecc.y.t.size = 0;
		rc = tpm2_fill_ecc_details(out_public_area, key_type);
	}

	return rc;
}

TPM_RC
tpm2_createprimary_asym(TPMI_RH_HIERARCHY hierachy, tpm2d_key_type_t key_type,
		const char *hierachy_pwd, const char *key_pwd,
		const char *file_name_pub_key, uint32_t *out_handle)
{
	TPM_RC rc = TPM_RC_SUCCESS;

	CreatePrimary_In in;
	CreatePrimary_Out out;
	TPMA_OBJECT object_attrs;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	object_attrs.val = 0;
	object_attrs.val |= TPMA_OBJECT_NODA;
	object_attrs.val |= TPMA_OBJECT_SENSITIVEDATAORIGIN;
	object_attrs.val |= TPMA_OBJECT_USERWITHAUTH;
	object_attrs.val &= ~TPMA_OBJECT_ADMINWITHPOLICY;
	object_attrs.val |= TPMA_OBJECT_RESTRICTED;
	object_attrs.val |= TPMA_OBJECT_DECRYPT;
	object_attrs.val &= ~TPMA_OBJECT_SIGN;
	object_attrs.val |= TPMA_OBJECT_FIXEDTPM;
	object_attrs.val |= TPMA_OBJECT_FIXEDPARENT;

	in.primaryHandle = hierachy;

	// Table 134 - Definition of TPM2B_SENSITIVE_CREATE inSensitive
	if (key_pwd == NULL)
		in.inSensitive.sensitive.userAuth.t.size = 0;
	else if (TPM_RC_SUCCESS != (rc = TSS_TPM2B_StringCopy(
				&in.inSensitive.sensitive.userAuth.b,
				key_pwd, sizeof(TPMU_HA))))
			return rc;
	in.inSensitive.sensitive.data.t.size = 0;

	// fill in TPM2B_PUBLIC
	if (TPM_RC_SUCCESS != (rc = tpm2_public_area_helper(
			&in.inPublic.publicArea, object_attrs, key_type)))
		return rc;

	// TPM2B_DATA outsideInfo
	in.outsideInfo.t.size = 0;
	// Table 102 - TPML_PCR_SELECTION creationPCR
	in.creationPCR.count = 0;


	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_CreatePrimary,
			TPM_RS_PW, hierachy_pwd, 0,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_CreatePrimary failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		return rc;
	}

	// save the public key
	if (file_name_pub_key) {
		rc = TSS_File_WriteStructure(&out.outPublic,
				(MarshalFunction_t)TSS_TPM2B_PUBLIC_Marshal,
				file_name_pub_key);
	}

	// return handle to just created object
	*out_handle = out.objectHandle;

	return rc;
}

#ifndef TPM2D_NVMCRYPT_ONLY
TPM_RC
tpm2_create_asym(TPMI_DH_OBJECT parent_handle, tpm2d_key_type_t key_type,
		uint32_t object_vals, const char *parent_pwd, const char *key_pwd,
		const char *file_name_priv_key, const char *file_name_pub_key)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	Create_In in;
	Create_Out out;
	TPMA_OBJECT object_attrs;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	in.parentHandle = parent_handle;
	object_attrs.val = object_vals;

	// Table 134 - Definition of TPM2B_SENSITIVE_CREATE inSensitive
	if (key_pwd == NULL)
		in.inSensitive.sensitive.userAuth.t.size = 0;
	else if (TPM_RC_SUCCESS != (rc = TSS_TPM2B_StringCopy(
				&in.inSensitive.sensitive.userAuth.b,
				key_pwd, sizeof(TPMU_HA))))
			return rc;
	in.inSensitive.sensitive.data.t.size = 0;

	// fill in TPM2B_PUBLIC
	if (TPM_RC_SUCCESS != (rc = tpm2_public_area_helper(
			&in.inPublic.publicArea, object_attrs, key_type)))
		return rc;

	// TPM2B_DATA outsideInfo
	in.outsideInfo.t.size = 0;
	// Table 102 - TPML_PCR_SELECTION creationPCR
	in.creationPCR.count = 0;

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_Create,
			TPM_RS_PW, parent_pwd, 0,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_Create failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		return rc;
	}

	// save the private key
	if (file_name_priv_key) {
		if (TPM_RC_SUCCESS != (rc = TSS_File_WriteStructure(&out.outPrivate,
				(MarshalFunction_t)TSS_TPM2B_PRIVATE_Marshal,
				file_name_priv_key))) {
			return rc;
		}
	}

	// save the public key
	if (file_name_pub_key) {
		rc = TSS_File_WriteStructure(&out.outPublic,
				(MarshalFunction_t)TSS_TPM2B_PUBLIC_Marshal,
				file_name_pub_key);
	}

	return rc;
}

TPM_RC
tpm2_load(TPMI_DH_OBJECT parent_handle, const char *parent_pwd,
		const char *file_name_priv_key, const char *file_name_pub_key,
		uint32_t *out_handle)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	Load_In in;
	Load_Out out;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	in.parentHandle = parent_handle;

	if (TPM_RC_SUCCESS != (rc = TSS_File_ReadStructure(&in.inPrivate,
			(UnmarshalFunction_t)TPM2B_PRIVATE_Unmarshal,
			file_name_priv_key)))
		return rc;

	if (TPM_RC_SUCCESS != (rc = TSS_File_ReadStructure(&in.inPublic,
			(UnmarshalFunction_t)TPM2B_PUBLIC_Unmarshal,
			file_name_pub_key)))
		return rc;

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_Load,
			TPM_RS_PW, parent_pwd, 0,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_Load failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		return rc;
	}

	// return handle to just created object
	*out_handle = out.objectHandle;

	return rc;
}

TPM_RC
tpm2_pcrextend(TPMI_DH_PCR pcr_index, TPMI_ALG_HASH hash_alg, const char *data)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	PCR_Extend_In in;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	if (strlen(data) > sizeof(TPMU_HA)) {
		ERROR("Data length %zu exceeds hash size %zu!", strlen(data), sizeof(TPMU_HA));
		return EXIT_FAILURE;
	}

	in.pcrHandle = pcr_index;

	// extend one bank
	in.digests.count = 1;

	// pad and set data
	in.digests.digests[0].hashAlg = hash_alg;
	memset((uint8_t *)&in.digests.digests[0].digest, 0, sizeof(TPMU_HA));
	memcpy((uint8_t *)&in.digests.digests[0].digest, data, strlen(data));

	rc = TSS_Execute(tss_context, NULL,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_PCR_Extend,
			TPM_RS_PW, NULL, 0,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_PCR_Extend failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}

	return rc;
}

tpm2d_pcr_string_t *
tpm2_pcrread_new(TPMI_DH_PCR pcr_index, TPMI_ALG_HASH hash_alg)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	PCR_Read_In in;
	PCR_Read_Out out;
	tpm2d_pcr_string_t *pcr_string = NULL;

	IF_NULL_RETVAL_ERROR(tss_context, NULL);

	/* Table 102 - Definition of TPML_PCR_SELECTION Structure */
	in.pcrSelectionIn.count = 1;
	/* Table 85 - Definition of TPMS_PCR_SELECTION Structure */
	in.pcrSelectionIn.pcrSelections[0].hash = hash_alg;
	in.pcrSelectionIn.pcrSelections[0].sizeofSelect = 3;
	in.pcrSelectionIn.pcrSelections[0].pcrSelect[0] = 0;
	in.pcrSelectionIn.pcrSelections[0].pcrSelect[1] = 0;
	in.pcrSelectionIn.pcrSelections[0].pcrSelect[2] = 0;
	in.pcrSelectionIn.pcrSelections[0].pcrSelect[pcr_index / 8] = 1 << (pcr_index % 8);

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_PCR_Read,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_PCR_Read failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		return NULL;
	}

	// finally fill the output structure with converted hex strings needed for protobuf
	pcr_string = mem_alloc0(sizeof(tpm2d_pcr_string_t));
	pcr_string->halg_str = halg_id_to_string_new(in.pcrSelectionIn.pcrSelections[0].hash);
	pcr_string->pcr_str = convert_bin_to_hex_new(out.pcrValues.digests[0].t.buffer,
						out.pcrValues.digests[0].t.size);
	return pcr_string;
}

void
tpm2_pcrread_free(tpm2d_pcr_string_t *pcr_string)
{
	if (pcr_string->halg_str)
		mem_free(pcr_string->halg_str);
	if (pcr_string->pcr_str)
		mem_free(pcr_string->pcr_str);
	mem_free(pcr_string);
}

tpm2d_quote_string_t *
tpm2_quote_new(TPMI_DH_PCR pcr_indices, TPMI_DH_OBJECT sig_key_handle,
			const char *sig_key_pwd, const char *qualifying_data)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	Quote_In in;
	Quote_Out out;
	TPMS_ATTEST tpms_attest;
	uint8_t *qualifying_data_bin = NULL;
	tpm2d_quote_string_t *quote_string = NULL;

	IF_NULL_RETVAL_ERROR(tss_context, NULL);

	if (pcr_indices > 23) {
		ERROR("Exceeded maximum available PCR registers!");
		return NULL;
	}

	in.PCRselect.pcrSelections[0].sizeofSelect = 3;
	in.PCRselect.pcrSelections[0].pcrSelect[0] = 0;
	in.PCRselect.pcrSelections[0].pcrSelect[1] = 0;
	in.PCRselect.pcrSelections[0].pcrSelect[2] = 0;
	for (size_t i=0; i < pcr_indices; ++i) {
		in.PCRselect.pcrSelections[0].pcrSelect[pcr_indices / 8] |= 1 << (pcr_indices % 8);
	}

	in.signHandle = sig_key_handle;
	if (TPM2D_ASYM_ALGORITHM == TPM_ALG_RSA) {
		in.inScheme.scheme = TPM_ALG_RSASSA;
		in.inScheme.details.rsassa.hashAlg = TPM2D_HASH_ALGORITHM;
	} else {
		// TPM2D_ASYM_ALGORITHM == TPM_ALG_ECC
		in.inScheme.scheme = TPM_ALG_ECDSA;
		in.inScheme.details.ecdsa.hashAlg = TPM2D_HASH_ALGORITHM;
	}

	in.PCRselect.count = 1;
	in.PCRselect.pcrSelections[0].hash = TPM2D_HASH_ALGORITHM;

	if (qualifying_data != NULL) {
		int length;
		qualifying_data_bin = convert_hex_to_bin_new(qualifying_data, &length);
		IF_NULL_RETVAL(qualifying_data_bin, NULL);
		if (TPM_RC_SUCCESS != (rc = TSS_TPM2B_Create(&in.qualifyingData.b,
				qualifying_data_bin, length, sizeof(TPMT_HA))))
			goto err;
	} else
		in.qualifyingData.t.size = 0;

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_Quote,
			TPM_RS_PW, sig_key_pwd, 0,
			TPM_RH_NULL, NULL, 0);

	if (rc != TPM_RC_SUCCESS)
		goto err;

	// check if input qualifying data matches output extra data
	BYTE *buf_byte = out.quoted.t.attestationData;
	uint32_t size_int32 = out.quoted.t.size;
        if (TPM_RC_SUCCESS != (rc = TPMS_ATTEST_Unmarshal(&tpms_attest, &buf_byte, &size_int32)))
		goto err;
        if (!TSS_TPM2B_Compare(&in.qualifyingData.b, &tpms_attest.extraData.b))
		goto err;

	// finally fill the output structure with converted hex strings needed for protobuf
	quote_string = mem_alloc0(sizeof(tpm2d_quote_string_t));
	quote_string->halg_str = halg_id_to_string_new(in.PCRselect.pcrSelections[0].hash);
	quote_string->quoted_str = convert_bin_to_hex_new(out.quoted.t.attestationData,
							out.quoted.t.size);
	quote_string->signature_str = tpm2d_marshal_structure_new(&out.signature,
					(MarshalFunction_t)TSS_TPMT_SIGNATURE_Marshal);

	if (in.inScheme.scheme == TPM_ALG_RSASSA) {
		TSS_PrintAll("RSA signature", out.signature.signature.rsassa.sig.t.buffer,
					out.signature.signature.rsassa.sig.t.size);
	}

	if (qualifying_data_bin)
		mem_free(qualifying_data_bin);
	return quote_string;
err:
	{
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_Quote failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}
	if (qualifying_data_bin)
		mem_free(qualifying_data_bin);
	return NULL;
}

void
tpm2_quote_free(tpm2d_quote_string_t* quote_string)
{
	if (quote_string->halg_str)
		mem_free(quote_string->halg_str);
	if (quote_string->quoted_str)
		mem_free(quote_string->quoted_str);
	if (quote_string->signature_str)
		mem_free(quote_string->signature_str);
	mem_free(quote_string);
}

char *
tpm2_read_file_to_hex_string_new(const char *file_name)
{
	uint8_t *data_bin = NULL;
	size_t len;

	if (TPM_RC_SUCCESS != TSS_File_ReadBinaryFile(&data_bin, &len ,file_name))
		goto err;

	if (data_bin)
		mem_free(data_bin);
	return convert_bin_to_hex_new(data_bin, len);
err:
	if (data_bin)
		mem_free(data_bin);
	return NULL;
}

TPM_RC
tpm2_evictcontrol(TPMI_RH_HIERARCHY auth, char* auth_pwd, TPMI_DH_OBJECT obj_handle,
						 TPMI_DH_PERSISTENT persist_handle)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	EvictControl_In in;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	in.auth = auth;
	in.objectHandle = obj_handle;
	in.persistentHandle = persist_handle;

	rc = TSS_Execute(tss_context, NULL, (COMMAND_PARAMETERS *)&in, NULL,
			TPM_CC_EvictControl,
			TPM_RS_PW, auth_pwd, 0,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_EvictControl failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	}

	return rc;
}

TPM_RC
tpm2_rsaencrypt(TPMI_DH_OBJECT key_handle, uint8_t *in_buffer, size_t in_length,
			 uint8_t *out_buffer, size_t *out_length)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	RSA_Encrypt_In in;
	RSA_Encrypt_Out out;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	if (in_length > MAX_RSA_KEY_BYTES) {
	    ERROR("Input buffer exceeds RSA Blocksize %zu\n", in_length);
	    return TSS_RC_INSUFFICIENT_BUFFER;
	}

	in.keyHandle = key_handle;
	/* Table 158 - Definition of {RSA} TPM2B_PUBLIC_KEY_RSA Structure */
	in.message.t.size = (uint16_t)in_length;
	memcpy(in.message.t.buffer, in_buffer, in_length);
	/* Table 157 - Definition of {RSA} TPMT_RSA_DECRYPT Structure */
	in.inScheme.scheme = TPM_ALG_OAEP;
	in.inScheme.details.oaep.hashAlg = TPM2D_HASH_ALGORITHM;
	/* Table 73 - Definition of TPM2B_DATA Structure */
	in.label.t.size = 0;

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_RSA_Encrypt,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_RSA_encrypt failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		return rc;
	}

	TSS_PrintAll("RSA encrypted data", out.outData.t.buffer, out.outData.t.size);

	// return handle to just created object
	if (out.outData.t.size > *out_length) {
		ERROR("Output buffer (size=%zu) is to small for encrypted data of size %u\n",
						*out_length, out.outData.t.size);
	    return TSS_RC_INSUFFICIENT_BUFFER;
	}
	memcpy(out_buffer, out.outData.t.buffer, out.outData.t.size);

	return rc;
}

TPM_RC
tpm2_rsadecrypt(TPMI_DH_OBJECT key_handle, const char *key_pwd, uint8_t *in_buffer,
			size_t in_length, uint8_t *out_buffer, size_t *out_length)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	RSA_Decrypt_In in;
	RSA_Decrypt_Out out;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	if (in_length > MAX_RSA_KEY_BYTES) {
	    ERROR("Input buffer exceeds RSA block size %zu\n", in_length);
	    return TSS_RC_INSUFFICIENT_BUFFER;
	}

	in.keyHandle = key_handle;
	/* Table 158 - Definition of {RSA} TPM2B_PUBLIC_KEY_RSA Structure */
	in.cipherText.t.size = (uint16_t)in_length;
	memcpy(in.cipherText.t.buffer, in_buffer, in_length);
	/* Table 157 - Definition of {RSA} TPMT_RSA_DECRYPT Structure */
	in.inScheme.scheme = TPM_ALG_OAEP;
	in.inScheme.details.oaep.hashAlg = TPM2D_HASH_ALGORITHM;
	/* Table 73 - Definition of TPM2B_DATA Structure */
	in.label.t.size = 0;

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_RSA_Decrypt,
			TPM_RS_PW, key_pwd, 0,
			TPM_RH_NULL, NULL, 0);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_RSA_decrypt failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		return rc;
	}

	TSS_PrintAll("RSA Decrypted message", out.message.t.buffer, out.message.t.size);

	// return handle to just created object
	if (out.message.t.size > *out_length) {
		ERROR("Output buffer (size=%zu) is to small for decrypted message of size %u\n",
						*out_length, out.message.t.size);
	    return TSS_RC_INSUFFICIENT_BUFFER;
	}
	memcpy(out_buffer, out.message.t.buffer, out.message.t.size);
	*out_length = out.message.t.size;

	return rc;
}
#endif // ndef TPM2D_NVMCRYPT_ONLY

uint8_t *
tpm2_getrandom_new(size_t rand_length)
{
	TPM_RC rc = TPM_RC_SUCCESS;
	TPMI_SH_AUTH_SESSION se_handle;
	GetRandom_In in;
	GetRandom_Out out;

	IF_NULL_RETVAL_ERROR(tss_context, NULL);

	// since we use this to generate symetric keys, start an encrypted transport */
	rc = tpm2_startauthsession(TPM_SE_HMAC, &se_handle, TPM_RH_NULL, NULL);
	if (TPM_RC_SUCCESS != rc) return NULL;

	uint8_t *rand = mem_new0(uint8_t, rand_length);
	size_t recv_bytes = 0;
	do {
		in.bytesRequested = rand_length - recv_bytes;
		rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
				(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_GetRandom,
				se_handle, NULL, TPMA_SESSION_ENCRYPT|TPMA_SESSION_CONTINUESESSION,
				TPM_RH_NULL, NULL, 0);
		if (rc != TPM_RC_SUCCESS)
			break;
		memcpy(&rand[recv_bytes], out.randomBytes.t.buffer, out.randomBytes.t.size);
		recv_bytes += out.randomBytes.t.size;
	} while (recv_bytes < rand_length);

	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_GetRandom failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
		mem_free(rand);
		return NULL;
	}

	char *rand_hex = convert_bin_to_hex_new(rand, rand_length);
	INFO("Generated Rand: %s", rand_hex);

	mem_free(rand_hex);

	if (TPM_RC_SUCCESS != tpm2_flushcontext(se_handle))
		WARN("Flush failed, maybe session handle was allready flushed.");

	return rand;
}

static size_t
tpm2_nv_get_data_size(TSS_CONTEXT *tss_context, TPMI_RH_NV_INDEX nv_index_handle)
{
	NV_ReadPublic_In in;
	NV_ReadPublic_Out out;
	size_t data_size = 0;

	IF_NULL_RETVAL_WARN(tss_context, 0);

	if ((nv_index_handle >> 24) != TPM_HT_NV_INDEX) {
		ERROR("bad index handle %x", nv_index_handle);
		return -1;
	}

	in.nvIndex = nv_index_handle;

	if (TPM_RC_SUCCESS != TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
				(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_NV_ReadPublic,
				TPM_RH_NULL, NULL, 0))
			return 0;

	uint32_t nv_type = (out.nvPublic.nvPublic.attributes.val & TPMA_NVA_TPM_NT_MASK) >> 4;
	if (nv_type == TPM_NT_ORDINARY) {
		data_size = out.nvPublic.nvPublic.dataSize;
	} else {
		WARN("Only ORDINARY data have variable size!");
	}
	INFO("Data size of NV index %x is %zd", nv_index_handle, data_size);

	return data_size;
}

static size_t
tpm2_nv_get_max_buffer_size(TSS_CONTEXT *tss_context)
{
	GetCapability_In in;
	GetCapability_Out out;

	in.capability = TPM_CAP_TPM_PROPERTIES;
	in.property = TPM_PT_NV_BUFFER_MAX;
	in.propertyCount = 1;

	// set a small default fallback value;
	size_t buffer_size = 512;

	IF_NULL_RETVAL_WARN(tss_context, buffer_size);

	if (TPM_RC_SUCCESS != TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
				(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_GetCapability,
				TPM_RH_NULL, NULL, 0)) {
		ERROR("GetCapability failed, returning default value %zd", buffer_size);
		return buffer_size;
	}

	if (out.capabilityData.data.tpmProperties.count > 0 &&
			 out.capabilityData.data.tpmProperties.tpmProperty[0].property == TPM_PT_NV_BUFFER_MAX)
		buffer_size = out.capabilityData.data.tpmProperties.tpmProperty[0].value;
	else
		ERROR("GetCapability failed, returning default value %zd", buffer_size);

	INFO("NV buffer maximum size is set to %zd", buffer_size);
	return buffer_size;
}

TPM_RC
tpm2_nv_definespace(TPMI_RH_HIERARCHY hierarchy, TPMI_RH_NV_INDEX nv_index_handle,
		size_t nv_size, const char *hierarchy_pwd, const char *nv_pwd)
{
	TPM_RC rc, rc_flush = TPM_RC_SUCCESS;
	TPMI_SH_AUTH_SESSION se_handle;
	NV_DefineSpace_In in;
	TPMA_NV nv_attr;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	if ((nv_index_handle >> 24) != TPM_HT_NV_INDEX) {
		ERROR("bad index handle %x", nv_index_handle);
		return TSS_RC_BAD_HANDLE_NUMBER;
	}

	if (nv_pwd == NULL)
		in.auth.b.size = 0;
	else if (TPM_RC_SUCCESS != (rc = TSS_TPM2B_StringCopy(&in.auth.b,
					nv_pwd, sizeof(TPMU_HA))))
		return rc;

	in.authHandle = hierarchy;

	nv_attr.val = 0;
	if (hierarchy == TPM_RH_PLATFORM) {
		nv_attr.val |= TPMA_NVA_PLATFORMCREATE;
		nv_attr.val |= TPMA_NVA_PPWRITE;
		nv_attr.val |= TPMA_NVA_PPREAD;
	} else { // TPM_RH_OWNER
		nv_attr.val |= TPMA_NVA_OWNERWRITE;
		nv_attr.val |= TPMA_NVA_OWNERREAD;
	}
	nv_attr.val |= TPMA_NVA_ORDINARY;
	nv_attr.val |= TPMA_NVA_AUTHREAD;
	nv_attr.val |= TPMA_NVA_AUTHWRITE;

	in.publicInfo.nvPublic.nvIndex = nv_index_handle;
	in.publicInfo.nvPublic.nameAlg = TPM2D_HASH_ALGORITHM;
	in.publicInfo.nvPublic.attributes = nv_attr;
	in.publicInfo.nvPublic.dataSize = nv_size;
	// set default empty policy
	in.publicInfo.nvPublic.authPolicy.t.size = 0;

	// since we use this to store symetric keys, start an encrypted transport */
	rc = tpm2_startauthsession(TPM_SE_HMAC, &se_handle, hierarchy, hierarchy_pwd);
	if (TPM_RC_SUCCESS != rc) goto err;

	rc = TSS_Execute(tss_context, NULL,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_NV_DefineSpace,
			//TPM_RS_PW, hierarchy_pwd, 0,
			se_handle, 0, TPMA_SESSION_DECRYPT|TPMA_SESSION_CONTINUESESSION,
			TPM_RH_NULL, NULL, 0);

	rc_flush = tpm2_flushcontext(se_handle);
err:
	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_NV_DefineSpace failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	} else {
		rc = rc_flush;
	}

	return rc;
}

TPM_RC
tpm2_nv_write(TPMI_RH_NV_INDEX nv_index_handle, const char *nv_pwd,
					uint8_t *data, size_t data_length)
{
	TPM_RC rc, rc_flush = TPM_RC_SUCCESS;
	TPMI_SH_AUTH_SESSION se_handle;
	NV_Write_In in;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);
	if ((nv_index_handle >> 24) != TPM_HT_NV_INDEX) {
		ERROR("bad index handle %x", nv_index_handle);
		return TSS_RC_BAD_HANDLE_NUMBER;
	}

	in.authHandle = nv_index_handle;
	in.nvIndex = nv_index_handle;
	in.offset = 0;

	size_t buffer_max = tpm2_nv_get_max_buffer_size(tss_context);
	if (data_length > buffer_max) {
		INFO("Only one chunk is supported by this implementation!");
		rc = TSS_RC_INSUFFICIENT_BUFFER;
		goto err;
	}
	memcpy(in.data.b.buffer, data, data_length);
	in.data.b.size = data_length;

	// since we use this to read symetric keys, start an encrypted transport */
	rc = tpm2_startauthsession(TPM_SE_HMAC, &se_handle, nv_index_handle, nv_pwd);
	if (TPM_RC_SUCCESS != rc) goto err;

	rc = TSS_Execute(tss_context, NULL,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_NV_Write,
			//TPM_RS_PW, nv_pwd, 0,
			se_handle, 0, TPMA_SESSION_DECRYPT|TPMA_SESSION_CONTINUESESSION,
			TPM_RH_NULL, NULL, 0);

	rc_flush = tpm2_flushcontext(se_handle);
err:
	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_NV_Write failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	} else {
		rc = rc_flush;
	}

	return rc;
}

TPM_RC
tpm2_nv_read(TPMI_RH_NV_INDEX nv_index_handle, const char *nv_pwd,
				uint8_t *out_buffer, size_t *out_length)
{
	TPM_RC rc, rc_flush = TPM_RC_SUCCESS;
	TPMI_SH_AUTH_SESSION se_handle;
	NV_Read_In in;
	NV_Read_Out out;

	IF_NULL_RETVAL_ERROR(tss_context, TSS_RC_NULL_PARAMETER);

	if ((nv_index_handle >> 24) != TPM_HT_NV_INDEX) {
		ERROR("bad index handle %x", nv_index_handle);
		return TSS_RC_BAD_HANDLE_NUMBER;
	}

	in.authHandle = nv_index_handle;
	in.nvIndex = nv_index_handle;
	in.offset = 0;

	size_t data_size = tpm2_nv_get_data_size(tss_context, nv_index_handle);
	size_t buffer_max = tpm2_nv_get_max_buffer_size(tss_context);
	if (data_size > buffer_max) {
		INFO("Only one chunk of size=%zd is supported by this implementation!", buffer_max);
		rc = TSS_RC_INSUFFICIENT_BUFFER;
		goto err;
	}
	if (data_size > *out_length) {
		ERROR("Output buffer (size=%zd) is to small for nv data of size %zd\n",
						*out_length, data_size);
		rc = TSS_RC_INSUFFICIENT_BUFFER;
		goto err;
	}

	in.size = data_size;

	// since we use this to read symetric keys, start an encrypted transport
	rc = tpm2_startauthsession(TPM_SE_HMAC, &se_handle, nv_index_handle, nv_pwd);
	if (TPM_RC_SUCCESS != rc)
		goto err;

	rc = TSS_Execute(tss_context, (RESPONSE_PARAMETERS *)&out,
			(COMMAND_PARAMETERS *)&in, NULL, TPM_CC_NV_Read,
			//TPM_RS_PW, nv_pwd, 0,
			se_handle, 0, TPMA_SESSION_ENCRYPT|TPMA_SESSION_CONTINUESESSION,
			TPM_RH_NULL, NULL, 0);
	if (TPM_RC_SUCCESS != rc)
		goto flush;

	memcpy(out_buffer, out.data.b.buffer, out.data.b.size);

	// set ouput length of caller
	*out_length = out.data.b.size;

	TSS_PrintAll("nv_read data: ", out_buffer, *out_length);

flush:
	rc_flush = tpm2_flushcontext(se_handle);

err:
	if (TPM_RC_SUCCESS != rc) {
		const char *msg;
		const char *submsg;
		const char *num;
		TSS_ResponseCode_toString(&msg, &submsg, &num, rc);
		ERROR("CC_NV_Read failed, rc %08x: %s%s%s\n", rc, msg, submsg, num);
	} else {
		rc = rc_flush;
	}
	return rc;
}
