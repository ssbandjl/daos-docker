/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <daos/agent.h>

char *dc_agent_sockpath;

int
dc_agent_init()
{
	char	*path = NULL;
	char	*envpath = getenv(DAOS_AGENT_DRPC_DIR_ENV);

	if (envpath)
		D_ASPRINTF(path, "%s/%s", envpath,
				DAOS_AGENT_DRPC_SOCK_NAME);
	else
		D_STRNDUP_S(path, DEFAULT_DAOS_AGENT_DRPC_SOCK);

	if (path == NULL)
		return -DER_NOMEM;

	dc_agent_sockpath = path;
  D_DEBUG(DB_ALL, "dc_agent_sock path:%s", dc_agent_sockpath);
	return 0;
}

void
dc_agent_fini()
{
	D_FREE(dc_agent_sockpath);
}
