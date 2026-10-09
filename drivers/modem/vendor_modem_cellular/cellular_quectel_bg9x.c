/*
 * Copyright (c) 2026 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/modem/modem_cellular.h>

#include <stdlib.h>
#include <string.h>

/* Serving network from AT+QNWINFO, completed with the signal from AT+QCSQ. */
struct quectel_bg9x_network {
	enum cellular_access_technology access_tech;
	uint16_t mcc;
	uint16_t mnc;
	uint16_t band;
	uint32_t earfcn;
};

static void quectel_bg9x_on_qnwinfo(struct modem_chat *chat, char **argv, uint16_t argc,
				    void *user_data);
static void quectel_bg9x_on_qcsq(struct modem_chat *chat, char **argv, uint16_t argc,
				 void *user_data);

MODEM_CELLULAR_COMMON_CHAT_MATCHES();

MODEM_CHAT_MATCHES_DEFINE(quectel_bg9x_unsol, MODEM_CELLULAR_COMMON_UNSOL_MATCHES,
			  MODEM_CHAT_MATCH("+QNWINFO: ", ",", quectel_bg9x_on_qnwinfo),
			  MODEM_CHAT_MATCH("+QCSQ: ", ",", quectel_bg9x_on_qcsq));

MODEM_CHAT_SCRIPT_CMDS_DEFINE(
	quectel_bg9x_set_baudrate_cmds, MODEM_CHAT_SCRIPT_CMD_RESP("ATE0", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+IPR=" STRINGIFY(CONFIG_MODEM_CELLULAR_NEW_BAUDRATE),
						       ok_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_bg9x_set_baudrate_chat_script, quectel_bg9x_set_baudrate_cmds,
			 abort_matches, modem_cellular_chat_callback_handler, 1);

/* A module variant may reject a RAT setting (e.g. "nwscanmode" on the BG95-M1): go on. */
MODEM_CHAT_MATCHES_DEFINE(__maybe_unused quectel_bg9x_rat_match, MODEM_CHAT_MATCH("OK", "", NULL),
			  MODEM_CHAT_MATCH("ERROR", "", NULL),
			  MODEM_CHAT_MATCH("+CME ERROR: ", "", NULL));

#if defined(CONFIG_MODEM_CELLULAR_QUECTEL_BG9X_RAT_LTE_M)
#define QUECTEL_BG9X_IOTOPMODE "0"
#define QUECTEL_BG9X_SCANSEQ   "02"
#elif defined(CONFIG_MODEM_CELLULAR_QUECTEL_BG9X_RAT_NB_IOT)
#define QUECTEL_BG9X_IOTOPMODE "1"
#define QUECTEL_BG9X_SCANSEQ   "03"
#elif defined(CONFIG_MODEM_CELLULAR_QUECTEL_BG9X_RAT_LTE_M_NB_IOT)
#define QUECTEL_BG9X_IOTOPMODE "2"
#define QUECTEL_BG9X_SCANSEQ   "0203"
#endif

MODEM_CHAT_SCRIPT_CMDS_DEFINE(
	quectel_bg9x_init_chat_script_cmds, MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=4", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CMEE=1", ok_match),
#if defined(QUECTEL_BG9X_IOTOPMODE)
	/* Radio off: the settings take effect without starting a network search. */
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QCFG=\"nwscanmode\",3,1", quectel_bg9x_rat_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QCFG=\"iotopmode\"," QUECTEL_BG9X_IOTOPMODE ",1",
					quectel_bg9x_rat_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QCFG=\"nwscanseq\"," QUECTEL_BG9X_SCANSEQ ",1",
					quectel_bg9x_rat_match),
#endif
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+CGSN", imei_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+CGMM", cgmm_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+CGMI", cgmi_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QGMR", cgmr_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+CIMI", cimi_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_MULT("AT+QCCID", qccid_match),
	MODEM_CHAT_SCRIPT_CMD_RESP_NONE("AT+CMUX=0,0,5," STRINGIFY(CONFIG_MODEM_CMUX_MTU), 300));

MODEM_CHAT_SCRIPT_DEFINE(quectel_bg9x_init_chat_script, quectel_bg9x_init_chat_script_cmds,
			 abort_matches, modem_cellular_chat_callback_handler, 10);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_bg9x_dial_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP(
				"ATD*99***" STRINGIFY(CONFIG_MODEM_CELLULAR_PDP_CONTEXT_ID) "#",
				connect_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_bg9x_dial_chat_script, quectel_bg9x_dial_chat_script_cmds,
			 dial_abort_matches, modem_cellular_chat_callback_handler, 10);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_bg9x_network_setup_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+QCFG=\"cmux/urcport\",1", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG=1", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG?", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=1", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_bg9x_network_chat_script, quectel_bg9x_network_setup_cmds,
			 abort_matches, modem_cellular_chat_callback_handler, 60);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_bg9x_periodic_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG?", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+QNWINFO", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+QCSQ", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_bg9x_periodic_chat_script, quectel_bg9x_periodic_chat_script_cmds,
			 abort_matches, modem_cellular_chat_callback_handler, 4);

/* Strip the quotes around a string field, in place. */
static const char *quectel_bg9x_unquote(char *field)
{
	size_t len = strlen(field);

	if (len >= 2 && field[0] == '"' && field[len - 1] == '"') {
		field[len - 1] = '\0';
		return field + 1;
	}

	return field;
}

static struct quectel_bg9x_network *quectel_bg9x_network_get(struct modem_cellular_data *data)
{
	const struct modem_cellular_config *config = data->dev->config;

	/* Per-instance scratch area, written by the chat callbacks only. */
	return (struct quectel_bg9x_network *)config->vendor_specific;
}

/*
 * +QNWINFO: <AcT>,<oper>,<band>,<channel>
 * e.g. +QNWINFO: "eMTC","72405","LTE BAND 28",9410
 */
static void quectel_bg9x_on_qnwinfo(struct modem_chat *chat, char **argv, uint16_t argc,
				    void *user_data)
{
	struct modem_cellular_data *data = (struct modem_cellular_data *)user_data;
	struct quectel_bg9x_network *net = quectel_bg9x_network_get(data);
	const char *act;
	const char *oper;
	const char *band;

	ARG_UNUSED(chat);

	net->access_tech = CELLULAR_ACCESS_TECHNOLOGY_UNKNOWN;
	if (argc < 5) {
		/* "No Service" */
		return;
	}

	act = quectel_bg9x_unquote(argv[1]);
	if (strcmp(act, "eMTC") == 0) {
		net->access_tech = CELLULAR_ACCESS_TECHNOLOGY_E_UTRAN;
	} else if (strcmp(act, "NBIoT") == 0) {
		net->access_tech = CELLULAR_ACCESS_TECHNOLOGY_E_UTRAN_NB_S1;
	} else {
		/* GSM, GPRS and EDGE have no LTE serving cell to report. */
		return;
	}

	/* <oper> is MCC (3 digits) followed by MNC (2 or 3 digits). */
	oper = quectel_bg9x_unquote(argv[2]);
	net->mnc = (strlen(oper) > 3) ? (uint16_t)strtoul(oper + 3, NULL, 10) : 0;
	net->mcc = (uint16_t)(strtoul(oper, NULL, 10) / ((strlen(oper) > 5) ? 1000 : 100));

	band = quectel_bg9x_unquote(argv[3]);
	band = strrchr(band, ' ');
	net->band = (band != NULL) ? (uint16_t)strtoul(band + 1, NULL, 10) : 0;

	net->earfcn = (uint32_t)strtoul(argv[4], NULL, 10);
}

/*
 * +QCSQ: <sysmode>[,<rssi>,<rsrp>,<sinr>,<rsrq>]
 * e.g. +QCSQ: "eMTC",-52,-81,195,-10
 */
static void quectel_bg9x_on_qcsq(struct modem_chat *chat, char **argv, uint16_t argc,
				 void *user_data)
{
	struct modem_cellular_data *data = (struct modem_cellular_data *)user_data;
	struct quectel_bg9x_network *net = quectel_bg9x_network_get(data);
	struct cellular_evt_network_status evt = {
		.status = data->registration_status_lte,
		.access_tech = net->access_tech,
	};

	ARG_UNUSED(chat);

	/* AT+QNWINFO runs first in the same script; only an LTE serving cell is reported. */
	if (argc < 6 || (net->access_tech != CELLULAR_ACCESS_TECHNOLOGY_E_UTRAN &&
			 net->access_tech != CELLULAR_ACCESS_TECHNOLOGY_E_UTRAN_NB_S1)) {
		return;
	}

	evt.cell.lte.mcc = net->mcc;
	evt.cell.lte.mnc = net->mnc;
	evt.cell.lte.earfcn = net->earfcn;
	evt.cell.lte.band = net->band;
	/* Not reported by these commands. */
	evt.cell.lte.gci = CELLULAR_EUTRAN_CELL_ID_INVALID;
	evt.cell.lte.rsrp = (int16_t)strtol(argv[3], NULL, 10);
	evt.cell.lte.rsrq = (int8_t)strtol(argv[5], NULL, 10);

	modem_cellular_emit_network_status(data, &evt);
}

MODEM_CHAT_MATCH_DEFINE(powerdown_match, "POWERED DOWN", "", NULL);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(quectel_bg9x_shutdown_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG=0", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+QPOWD=1", powerdown_match));

MODEM_CHAT_SCRIPT_DEFINE(quectel_bg9x_shutdown_chat_script, quectel_bg9x_shutdown_chat_script_cmds,
			 abort_matches, modem_cellular_chat_callback_handler, 5);

static const struct modem_cellular_vendor_config quectel_bg9x_vendor = {
	/* clang-format off */
	.scripts = {
		.set_baudrate = &quectel_bg9x_set_baudrate_chat_script,
		.init = &quectel_bg9x_init_chat_script,
		.network = &quectel_bg9x_network_chat_script,
		.dial = &quectel_bg9x_dial_chat_script,
		.periodic = &quectel_bg9x_periodic_chat_script,
		.shutdown = &quectel_bg9x_shutdown_chat_script,
	},
	.unsol_matches = {
		.matches = quectel_bg9x_unsol,
		.size = ARRAY_SIZE(quectel_bg9x_unsol),
	},
	/* clang-format on */
	.chat_delimiter = "\r",
	.chat_filter = "\n",
	.power_pulse_duration_ms = 500,
	.reset_pulse_duration_ms = 1000,
	.startup_time_ms = 5000,
	.shutdown_time_ms = 2000,
};

#define MODEM_CELLULAR_DEVICE_QUECTEL_BG9X(inst)                                                   \
	MODEM_DT_INST_PPP_DEFINE(inst, MODEM_CELLULAR_INST_NAME(ppp, inst), NULL, 1500, 64);       \
                                                                                                   \
	static struct modem_cellular_data MODEM_CELLULAR_INST_NAME(data, inst);                    \
	static struct quectel_bg9x_network MODEM_CELLULAR_INST_NAME(network, inst);                \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_AND_INIT_USER_PIPES(inst, (user_pipe_0, 3), (user_pipe_1, 4))        \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_INSTANCE(inst, &quectel_bg9x_vendor,                                 \
				       &MODEM_CELLULAR_INST_NAME(network, inst))

#define DT_DRV_COMPAT quectel_bg95
DT_INST_FOREACH_STATUS_OKAY(MODEM_CELLULAR_DEVICE_QUECTEL_BG9X)
#undef DT_DRV_COMPAT

#define DT_DRV_COMPAT quectel_bg96
DT_INST_FOREACH_STATUS_OKAY(MODEM_CELLULAR_DEVICE_QUECTEL_BG9X)
#undef DT_DRV_COMPAT
