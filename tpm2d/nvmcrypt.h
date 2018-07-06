/*
 * This file is part of trust|me
 * Copyright(c) 2013 - 2018 Fraunhofer AISEC
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

#ifndef FDE_H
#define FDE_H

typedef enum nvmcrypt_fde_state {
	FDE_OK = 1,
	FDE_AUTH_FAILED,
	FDE_KEYGEN_FAILED,
	FDE_NO_DEVICE,
	FDE_UNEXPECTED_ERROR
} nvmcrypt_fde_state_t;

nvmcrypt_fde_state_t
nvmcrypt_dm_setup(const char* device_path, const char* fde_pw);

#endif // FDE_H
