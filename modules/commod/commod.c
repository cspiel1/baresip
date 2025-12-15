/**
 * @file commod.c Commend application module
 *
 * Copyright (C) 2020 Commend.com - c.spielberger@commend.com
 */

#include <re.h>
#include <baresip.h>
#include <stdint.h>
#include <systemd/sd-daemon.h>


/**
 * @defgroup commod commod
 *
 * This module implements Commend specific commands
 */


#define DEBUG_MODULE "commod"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


struct amle {
	struct le he;
	struct account *acc;
	enum answermode am;
};

struct commod {
	struct play *cur_play;
	struct call *cur_call;
	struct hash *answmod;
	struct tmr tmr;
	uint64_t winterval;
	struct tmr tmr_earlym_on;
	struct tmr tmr_earlym_off;
};


static struct commod d = {
	.cur_play=NULL,
	.cur_call=NULL,
	.answmod=NULL};


/**
 * Print info for given call with Commend specific informations
 *
 * Printed the following line where the parameters are as follwing
 * %u %s %d %u %d %s %s %s
 * %u	line number
 * %s	call state
 * %d	outgoing call as bool 1 = outgoing, 0 = incoming
 * %u	call duration in seconds
 * %d	on hold as bool 1 = on hold, 0 = active
 * %s	id
 * %s	peer uri
 * %s	peer name
 *
 * @param pf	Print handler for debug output
 * @param call	call to print
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_call_info(struct re_printf *pf, const struct call *call)
{
	if (!call)
		return 0;

	return re_hprintf(pf, "%u %s %d %u %d %s %s %s",
			  call_linenum(call),
			  call_statename(call),
			  call_is_outgoing(call),
			  call_duration(call),
			  call_is_onhold(call),
			  call_id(call),
			  call_peeruri(call),
			  call_peername(call));
}

/**
 * Commend specific calls print
 *
 * @param pf     Print handler for debug output
 * @param ua     User-Agent
 *
 * @return 0 if success, otherwise errorcode
 */
static int com_ua_print_calls(struct re_printf *pf, const struct ua *ua)
{
	uint32_t n, count=0;
	uint32_t linenum;
	struct account *acc;
	struct uri *uri;
	int err = 0;

	if (!ua) {
		err |= re_hprintf(pf, "\n--- No active calls ---\n");
		return err;
	}

	acc = ua_account(ua);
	uri = account_luri(acc);
	n = list_count(ua_calls(ua));

	err |= re_hprintf(pf, "\nUser-Agent: %r@%r\n", &uri->user, &uri->host);
	err |= re_hprintf(pf, "--- Active calls (%u) ---\n", n);

	for (linenum = 1; linenum < 256; linenum++) {

		const struct call *call;

		call = call_find_linenum(ua_calls(ua), linenum);
		if (call) {
			++count;

			err |= re_hprintf(pf, "%s %H\n",
					  call == ua_call(ua) ? ">" : " ",
					  com_call_info, call);
		}

		if (count >= n)
			break;
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Print all calls with Commend specific informations
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_print_calls(struct re_printf *pf, void *arg)
{
	struct le *le;
	int err;
	(void) arg;

	for (le = list_head(uag_list()); le; le = le->next) {
		const struct ua *ua = le->data;
		err = com_ua_print_calls(pf, ua);
		if (err)
			return err;
	}

	return 0;
}


static int param_decode(const char *prm, const char *name, struct pl *val)
{
	char expr[128];
	struct pl v;

	if (!str_isset(prm) || !name || !val)
		return EINVAL;

	(void)re_snprintf(expr, sizeof(expr),
			  "[ \t\r\n]*%s[ \t\r\n]*=[ \t\r\n]*[~ \t\r\n;]+",
			  name);

	if (re_regex(prm, str_len(prm), expr, NULL, NULL, NULL, &v))
		return ENOENT;

	*val = v;

	return 0;
}


const char *playmod_usage = "/com_playmod"
			    " source=<audiofile>"
			    " [player=<player_mod>,<player_dev>]\n";

static int cmd_playmod_file(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;

	struct pl src_param = PL_INIT;
	struct pl player_param = PL_INIT;

	struct pl mod_param = PL_INIT;
	struct pl dev_param = PL_INIT;

	struct config *cfg;

	char *alert_mod = NULL;
	char *alert_dev = NULL;
	char *filename = NULL;

	int err = 0;

	cfg = conf_config();

	/* Stop the current tone, if any */
	d.cur_play = mem_deref(d.cur_play);

	if (param_decode(carg->prm, "source", &src_param)) {
		re_hprintf(pf, "commod: No source defined.\n");
		goto out;
	}

	pl_strdup(&filename, &src_param);

	err = param_decode(carg->prm, "player", &player_param);
	if (!err) {
		if (!re_regex(player_param.p,
			     player_param.l, "[^,]+,[~]*",
			     &mod_param, &dev_param)) {

			pl_strdup(&alert_mod, &mod_param);

			if (pl_isset(&dev_param))
				pl_strdup(&alert_dev, &dev_param);
		}
	}
	else {
		str_dup(&alert_mod, cfg->audio.alert_mod);
		str_dup(&alert_dev, cfg->audio.alert_dev);
	}


	if (str_isset(filename)) {
		re_hprintf(pf, "playing audio file \"%s\" ..\n", filename);

		err = play_file(
			&d.cur_play, baresip_player(),
			filename, 0,  alert_mod, alert_dev);

		if (err) {
			warning("commod: play_file(%s) failed (%m)\n",
				filename, err);
			goto out;
		}
	}

out:

	if (err)
		(void) re_hprintf(pf, "usage: %s", playmod_usage);

	mem_deref(alert_mod);
	mem_deref(alert_dev);
	mem_deref(filename);

	return err;
}


/**
 * @brief Checks auto answer media direction account setting
 *
 * Account parameter:
 * ;extra=...,auto_audio=recvonly,auto_video=inactive
 *
 * Default is sendrecv. So in order to disable video only specify:
 * ;extra=...,auto_video=inactive
 *
 * @param call The incoming call
 */
static void check_auto_answer_media_direction(struct call *call)
{
	struct ua *ua;
	struct account *acc;
	bool autoanswer = false;

	if (!call)
		return;

	ua = call_get_ua(call);
	acc = ua_account(ua);

	autoanswer = account_answermode(acc) == ANSWERMODE_AUTO ||
		account_answerdelay(acc) ||
		(account_sip_autoanswer(acc) && call_answer_delay(call) != -1);

	if (autoanswer) {
		struct pl pl = PL_INIT;
		struct pl v = PL_INIT;
		enum sdp_dir adir = SDP_SENDRECV;
		enum sdp_dir vdir = SDP_SENDRECV;
		bool found = false;

		pl_set_str(&pl, account_extra(acc));
		if (fmt_param_sep_get(&pl, "auto_audio", ',', &v)) {
			adir = sdp_dir_decode(&v);
			found = true;
		}

		if (fmt_param_sep_get(&pl, "auto_video", ',', &v)) {
			vdir = sdp_dir_decode(&v);
			found = true;
		}

		if (found) {
			call_set_media_estdir(call, adir, vdir);
			if (call_sdp_change_allowed(call))
				call_set_media_direction(call, adir, vdir);
		}
	}
}


static void hangup_outgoing_ua(struct call *call, void *arg)
{
	struct ua *ua = arg;

	if (call_get_ua(call) != ua)
		return;

	if (call_state(call) != CALL_STATE_OUTGOING &&
	    call_state(call) != CALL_STATE_RINGING &&
	    call_state(call) != CALL_STATE_EARLY)
		return;

	ua_hangup(ua, call, 480, "Temporarily Unavailable");
}


static int acc_add_answmod(struct account *acc)
{
	struct amle *amle;

	amle = mem_zalloc(sizeof(*amle), NULL);
	if (!amle)
		return ENOMEM;

	amle->am  = account_answermode(acc);
	amle->acc = acc;
	hash_append(d.answmod, hash_fast_str(account_aor(acc)),
		    &amle->he, amle);
	return 0;
}


static bool amle_restore_applyh(struct le *le, void *arg)
{
	struct amle *amle = le->data;
	(void)arg;

	account_set_answermode(amle->acc, amle->am);
	return true;
}


static void acc_restore_answmods(void)
{
	hash_apply(d.answmod, amle_restore_applyh, NULL);
	hash_flush(d.answmod);
}


static bool amle_get_applyh(struct le *le, void *arg)
{
	struct amle *amle = le->data;
	enum answermode *amp = arg;

	*amp = amle->am;
	return true;
}


static enum answermode acc_answmod_get(const struct account *acc)
{
	enum answermode am = ANSWERMODE_MANUAL;

	hash_lookup(d.answmod, hash_fast_str(account_aor(acc)),
		    amle_get_applyh, &am);

	return am;
}


static void sel_oldest_call(struct call *call, void *arg)
{
	struct call *closed = arg;

	if (call == closed)
		return;

	if (call_state(d.cur_call) != CALL_STATE_ESTABLISHED &&
	    call_state(call)       == CALL_STATE_ESTABLISHED)
		d.cur_call = call;

	else if (!d.cur_call && call_state(call) == CALL_STATE_INCOMING)
		d.cur_call = call;
}


static void call_earlymedia_disable(struct call *call)
{
	enum sdp_dir adir = SDP_INACTIVE;
	enum sdp_dir vdir = SDP_INACTIVE;
	call_get_mdir(call, &adir, &vdir);
	if (adir == SDP_INACTIVE && vdir == SDP_INACTIVE)
		return;

	call_set_audio_ldir(call, SDP_INACTIVE);
	call_set_video_ldir(call, SDP_INACTIVE);
	call_modify(call);
}


static void call_earlymedia_enable(struct call *call)
{
	enum answermode am = acc_answmod_get(call_account(call));
	enum sdp_dir adir = am == ANSWERMODE_EARLY ? SDP_SENDRECV :
			    am == ANSWERMODE_EARLY_AUDIO ? SDP_RECVONLY :
			    SDP_INACTIVE;
	enum sdp_dir vdir = am == ANSWERMODE_EARLY ? SDP_SENDRECV :
			    am == ANSWERMODE_EARLY_VIDEO ? SDP_RECVONLY :
			    SDP_INACTIVE;

	const char *peeruri = call_peeruri(call);
	if (!peeruri)
		return;

	const struct contacts *contacts = baresip_contacts();
	struct contact *con = contact_find_host(contacts, peeruri);
	if (con) {
		enum sdp_dir caudir  = SDP_SENDRECV;
		enum sdp_dir cviddir = SDP_SENDRECV;
		contact_get_ldir(con, &caudir, &cviddir);

		adir &= caudir;
		vdir &= cviddir;
	}

	enum sdp_dir estaudir  = SDP_SENDRECV;
	enum sdp_dir estviddir = SDP_SENDRECV;
	call_get_media_estdir(call, &estaudir, &estviddir);
	adir &= estaudir;
	vdir &= estviddir;

	if (adir == SDP_INACTIVE && vdir == SDP_INACTIVE)
		return;

	call_set_audio_ldir(call, adir);
	call_set_video_ldir(call, vdir);
	if (call_refresh_allowed(call))
		call_modify(call);
	else
		call_progress_dir(call, adir, vdir);
}


static void earlymedia_on(void *arg)
{
	struct call *call = arg;
	call_earlymedia_enable(call);
	info("commod: earlymedia enabled for call %s\n", call_id(call));
}


static void earlymedia_off(void *arg)
{
	struct call *call = arg;
	if (call_state(d.cur_call) == CALL_STATE_INCOMING)
		call_earlymedia_disable(d.cur_call);

	if (d.cur_call)
		info("commod: earlymedia disabled for call %s\n",
		     call_id(call));

	d.cur_call = call;
	if (call_state(d.cur_call) == CALL_STATE_INCOMING)
		tmr_start(&d.tmr_earlym_on, 100, earlymedia_on, call);
}


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct ua      *ua  = bevent_get_ua(event);
	struct account *acc = ua_account(ua);
	struct call   *call = bevent_get_call(event);
	const char    *prm  = bevent_get_text(event);
	enum answermode am = account_answermode(acc);
	(void) arg;

	info("commod: [ ua=%s call=%s ] event: %s (%s)\n",
	     account_aor(acc), call_id(call), bevent_str(ev), prm);

	switch (ev) {
		case BEVENT_CALL_INCOMING:
			check_auto_answer_media_direction(call);
			if (!d.cur_call)
				d.cur_call = call;

			if (d.cur_call != call) {
				if (am != ANSWERMODE_MANUAL)
					acc_add_answmod(acc);

				account_set_answermode(acc, ANSWERMODE_MANUAL);
			}

			/*@fallthrough@*/
		case BEVENT_CALL_OUTGOING:
			d.cur_play = mem_deref(d.cur_play);
			break;
		case BEVENT_REGISTER_FAIL:
			/* SYFU-942: hangup all call-requests */
			uag_filter_calls(hangup_outgoing_ua, NULL, ua);
			break;
		case BEVENT_CALL_ESTABLISHED:
		case BEVENT_CALL_ANSWERED:
			if (call_is_outgoing(call))
			    break;

			if (d.cur_call != call &&
			    call_state(d.cur_call) == CALL_STATE_INCOMING) {
				call_set_video_dir(d.cur_call, SDP_INACTIVE);
			}

			d.cur_call = call;
			break;
		case BEVENT_CALL_CLOSED:
			if (d.cur_call == call)
				d.cur_call = NULL;

			if (uag_call_count() <= 1)
				acc_restore_answmods();

			if (d.tmr_earlym_on.arg == call)
				tmr_cancel(&d.tmr_earlym_on);
			if (d.tmr_earlym_off.arg == call)
				tmr_cancel(&d.tmr_earlym_off);
			break;
		default:
			break;
	}
}


static void find_first_call(struct call *call, void *arg)
{
	struct call **ret = arg;

	*ret = call;
}


static struct call *current_call(void)
{
	struct call *call = NULL;

	uag_filter_calls(find_first_call, NULL, &call);
	return call;
}


/**
 * Removes the current audio codec from local SDP
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_rm_aucodec(struct re_printf *pf, void *arg)
{
	struct call *call = current_call();
	struct stream *stream;
	struct sdp_media *m;
	struct sdp_format *f;
	(void) arg;

	if (!call)
		return EINVAL;

	stream = audio_strm(call_audio(call));
	m = stream_sdpmedia(stream);
	f = sdp_media_format(m, true, NULL, -1, NULL, -1, -1);

	if (f)
		re_hprintf(pf, "Removing SDP format:\n%H\n", sdp_format_debug,
			   f);
	else
		re_hprintf(pf, "No SDP format found\n");

	mem_deref(f);
	return 0;
}


/**
 * Switch early media to given incoming call
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_switch_earlymedia(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;

	struct call *call;
	const char *usage = "usage: /com_switchearly <callid>\n";

	if (!str_isset(carg->prm)) {
		(void) re_hprintf(pf, "%s", usage);
		return EINVAL;
	}

	call = uag_call_find(carg->prm);
	if (!call) {
		(void) re_hprintf(pf, "Could not find call %s\n", carg->prm);
		return EINVAL;
	}

	tmr_cancel(&d.tmr_earlym_on);
	tmr_cancel(&d.tmr_earlym_off);
	if (call == d.cur_call)
		return 0;

	info("commod: switching early media from call %s to call %s\n",
	     d.cur_call  ? call_id(d.cur_call):"-", call_id(call));

	tmr_start(&d.tmr_earlym_off, 50, earlymedia_off, call);
	return 0;
}


static int com_freeze(struct re_printf *pf, void *arg)
{
	(void) arg;

	re_hprintf(pf, "commod: freezing now ...\n");
	while (1)
		sys_msleep(100);

	return 0;
}


static void watchdog(void *arg)
{
	(void)arg;

	tmr_start(&d.tmr, d.winterval, watchdog, NULL);
	sd_notify(0, "WATCHDOG=1");
}


static const struct cmd cmdv[] = {

{"com_listcalls", 0, 0,	"List active calls Commend format", com_print_calls},
{"com_playmod",   0, CMD_PRM,	"Play audio file on audio player",
	cmd_playmod_file},
{"com_rmaucodec", 0, 0, "Remove current audio codec", com_rm_aucodec},
{"com_switchearly", 0, 0, "Switch early media to other incoming call",
	com_switch_earlymedia},
{"com_freeze", 0, 0, "Freeze main thread (for testing watchdog)", com_freeze},
};


static int module_init(void)
{
	int err;

	err  = bevent_register(event_handler, NULL);
	err |= cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
	err |= hash_alloc(&d.answmod, 32);
	tmr_init(&d.tmr);
	tmr_init(&d.tmr_earlym_on);
	tmr_init(&d.tmr_earlym_off);

	uint64_t delay;
	if (sd_watchdog_enabled(0, &delay)) {
		delay /= 1000; /* in [ms] */
		if (delay < 1000) {
			warning("commod: watchdog to low %u[ms]\n",
				(unsigned) delay);
			return EINVAL;
		}

		d.winterval = (uint64_t) delay / 2;
		info("commod: watchdog enabled with delay %u/%u[ms]\n",
		     (unsigned) d.winterval, (unsigned) delay);

		tmr_start(&d.tmr, d.winterval, watchdog, NULL);
	}
	else {
		info("commod: watchdog disabled\n");
	}

	return err;
}


static int module_close(void)
{
	bevent_unregister(event_handler);
	cmd_unregister(baresip_commands(), cmdv);
	hash_flush(d.answmod);
	mem_deref(d.answmod);
	d.cur_play = mem_deref(d.cur_play);
	tmr_cancel(&d.tmr);
	tmr_cancel(&d.tmr_earlym_on);
	tmr_cancel(&d.tmr_earlym_off);

	return 0;
}


const struct mod_export DECL_EXPORTS(commod) = {
	"commod",
	"application",
	module_init,
	module_close
};
