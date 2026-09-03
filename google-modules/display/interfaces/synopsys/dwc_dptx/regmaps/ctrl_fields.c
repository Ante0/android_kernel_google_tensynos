// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "regmaps/ctrl_fields.h"

#define INIT_CTRL_FIELD(f) INIT_CTRL_FIELD_CFG(field_##f, cfg_##f)
#define INIT_CTRL_FIELD_CFG(f, conf)	({\
	dptx->ctrl_fields->f = devm_regmap_field_alloc(dptx->dev, dptx->regs[DPTX],\
			variant->conf);\
	if (IS_ERR(dptx->ctrl_fields->f))\
		dev_warn(dptx->dev, "Ignoring regmap field"#f "\n");\
	})

int ctrl_regmap_fields_init(struct dptx *dptx)
{
	const struct ctrl_regfield_variant *variant = &ctrl_regfield_cfg;

	INIT_CTRL_FIELD(version_number);
	INIT_CTRL_FIELD(version_type);
	INIT_CTRL_FIELD(vendor_id);
	INIT_CTRL_FIELD(device_id);
	INIT_CTRL_FIELD(hdcp_select);
	INIT_CTRL_FIELD(audio_select);
	INIT_CTRL_FIELD(phy_used);
	INIT_CTRL_FIELD(sdp_reg_bank_size);
	INIT_CTRL_FIELD(fpga_en);
	INIT_CTRL_FIELD(dpk_romless);
	INIT_CTRL_FIELD(dpk_8bit);
	INIT_CTRL_FIELD(sync_depth);
	INIT_CTRL_FIELD(num_streams);
	INIT_CTRL_FIELD(mp_mode);
	INIT_CTRL_FIELD(dsc_en);
	INIT_CTRL_FIELD(edp_en);
	INIT_CTRL_FIELD(fec_en);
	INIT_CTRL_FIELD(gen2_phy);
	INIT_CTRL_FIELD(phy_type);
	INIT_CTRL_FIELD(adsync_en);
	INIT_CTRL_FIELD(psr_ver);
	INIT_CTRL_FIELD(scramble_dis);
	INIT_CTRL_FIELD(enhance_framing_en);
	INIT_CTRL_FIELD(default_fast_link_train_en);
	INIT_CTRL_FIELD(scale_down_mode);
	INIT_CTRL_FIELD(force_hpd);
	INIT_CTRL_FIELD(disable_interleaving);
	INIT_CTRL_FIELD(sel_aux_timeout_32ms);
	INIT_CTRL_FIELD(debug_control);
	INIT_CTRL_FIELD(sr_scale_down);
	INIT_CTRL_FIELD(bs_512_scale_down);
	INIT_CTRL_FIELD(enable_mst_mode);
	INIT_CTRL_FIELD(enable_fec);
	INIT_CTRL_FIELD(enable_edp);
	INIT_CTRL_FIELD(initiate_mst_act_seq);
	INIT_CTRL_FIELD(enhance_framing_with_fec_en);
	INIT_CTRL_FIELD(controller_reset);
	INIT_CTRL_FIELD(phy_soft_reset);
	INIT_CTRL_FIELD(hdcp_module_reset);
	INIT_CTRL_FIELD(audio_sampler_reset);
	INIT_CTRL_FIELD(aux_reset);
	INIT_CTRL_FIELD(video_reset);
	INIT_CTRL_FIELD(audio_sampler_reset_stream1);
	INIT_CTRL_FIELD(audio_sampler_reset_stream2);
	INIT_CTRL_FIELD(audio_sampler_reset_stream3);
	INIT_CTRL_FIELD(aux_cdr_state);
	INIT_CTRL_FIELD(aux_cdr_clock_cycle);
	INIT_CTRL_FIELD(video_stream_enable);
	INIT_CTRL_FIELD(video_mapping_ipi_en);
	INIT_CTRL_FIELD(video_mapping);
	INIT_CTRL_FIELD(pixel_mode_select);
	INIT_CTRL_FIELD(enable_dsc);
	INIT_CTRL_FIELD(encryption_enable);
	INIT_CTRL_FIELD(stream_type);
	INIT_CTRL_FIELD(bcb_data_stuffing_en);
	INIT_CTRL_FIELD(rcr_data_stuffing_en);
	INIT_CTRL_FIELD(gy_data_stuffing_en);
	INIT_CTRL_FIELD(bcb_stuff_data);
	INIT_CTRL_FIELD(rcr_stuff_data);
	INIT_CTRL_FIELD(gy_stuff_data);
	INIT_CTRL_FIELD(vsync_in_polarity);
	INIT_CTRL_FIELD(hsync_in_polarity);
	INIT_CTRL_FIELD(de_in_polarity);
	INIT_CTRL_FIELD(r_v_blank_in_osc);
	INIT_CTRL_FIELD(i_p);
	INIT_CTRL_FIELD(hblank_video_config1); /* hblank */
	INIT_CTRL_FIELD(hactive_video_config1); /* hactive */
	INIT_CTRL_FIELD(vactive_video_config2); /* vactive */
	INIT_CTRL_FIELD(vblank_video_config2); /* vblank */
	INIT_CTRL_FIELD(h_sync_width_video_config3); /* h_sync_width */
	INIT_CTRL_FIELD(v_sync_width_video_config4); /* v_sync_width */
	INIT_CTRL_FIELD(average_bytes_per_tu);
	INIT_CTRL_FIELD(init_threshold);
	INIT_CTRL_FIELD(average_bytes_per_tu_frac);
	INIT_CTRL_FIELD(enable_3d_frame_field_seq);
	INIT_CTRL_FIELD(init_threshold_hi);
	INIT_CTRL_FIELD(hstart);
	INIT_CTRL_FIELD(vstart);
	INIT_CTRL_FIELD(mvid);
	INIT_CTRL_FIELD(misc0);
	INIT_CTRL_FIELD(nvid);
	INIT_CTRL_FIELD(misc1);
	INIT_CTRL_FIELD(hblank_interval);
	INIT_CTRL_FIELD(mvid_cust_en);
	INIT_CTRL_FIELD(mvid_out_clr_mode);
	INIT_CTRL_FIELD(mvid_cust_den);
	INIT_CTRL_FIELD(mvid_cust_mod);
	INIT_CTRL_FIELD(mvid_cust_quo);
	INIT_CTRL_FIELD(audio_inf_select);
	INIT_CTRL_FIELD(audio_data_in_en);
	INIT_CTRL_FIELD(audio_data_width);
	INIT_CTRL_FIELD(hbr_mode_enable);
	INIT_CTRL_FIELD(num_channels);
	INIT_CTRL_FIELD(audio_mute);
	INIT_CTRL_FIELD(audio_packet_id);
	INIT_CTRL_FIELD(audio_timestamp_version_num);
	INIT_CTRL_FIELD(audio_clk_mult_fs);
	INIT_CTRL_FIELD(en_audio_timestamp_sdp_vertical_ctrl); /* en_audio_timestamp_sdp */
	INIT_CTRL_FIELD(en_audio_stream_sdp_vertical_ctrl); /* en_audio_stream_sdp */
	INIT_CTRL_FIELD(en_vertical_sdp_n);
	INIT_CTRL_FIELD(en_128bytes_sdp_1);
	INIT_CTRL_FIELD(disable_ext_sdp);
	INIT_CTRL_FIELD(fixed_priority_arbitration_vertical_ctrl); /* fixed_priority_arbitration */
	INIT_CTRL_FIELD(en_audio_timestamp_sdp_horizontal_ctrl); /* en_audio_timestamp_sdp */
	INIT_CTRL_FIELD(en_audio_stream_sdp_horizontal_ctrl); /* en_audio_stream_sdp */
	INIT_CTRL_FIELD(en_horizontal_sdp_n);
	INIT_CTRL_FIELD(fixed_priority_arbitration_horizontal_ctrl); /* fixed_priority_arbitration */
	INIT_CTRL_FIELD(audio_timestamp_sdp_status);
	INIT_CTRL_FIELD(audio_stream_sdp_status);
	INIT_CTRL_FIELD(sdp_n_tx_status);
	INIT_CTRL_FIELD(manual_mode_sdp);
	INIT_CTRL_FIELD(audio_timestamp_sdp_status_en);
	INIT_CTRL_FIELD(audio_stream_sdp_status_en);
	INIT_CTRL_FIELD(sdp_status_en);
	INIT_CTRL_FIELD(sdp_16b_bytes_reqd_vblank_ovr);
	INIT_CTRL_FIELD(sdp_16b_bytes_reqd_hblank_ovr);
	INIT_CTRL_FIELD(sdp_32b_bytes_reqd_vblank_ovr);
	INIT_CTRL_FIELD(sdp_32b_bytes_reqd_hblank_ovr);
	INIT_CTRL_FIELD(sdp_128b_bytes_reqd_vblank_ovr);
	INIT_CTRL_FIELD(sdp_128b_bytes_reqd_hblank_ovr);
	INIT_CTRL_FIELD(tps_sel);
	INIT_CTRL_FIELD(phyrate);
	INIT_CTRL_FIELD(phy_lanes);
	INIT_CTRL_FIELD(xmit_enable);
	INIT_CTRL_FIELD(phy_busy);
	INIT_CTRL_FIELD(ssc_dis);
	INIT_CTRL_FIELD(phy_powerdown);
	INIT_CTRL_FIELD(phy_width);
	INIT_CTRL_FIELD(edp_phy_rate);
	INIT_CTRL_FIELD(lane0_tx_preemp);
	INIT_CTRL_FIELD(lane0_tx_vswing);
	INIT_CTRL_FIELD(lane1_tx_preemp);
	INIT_CTRL_FIELD(lane1_tx_vswing);
	INIT_CTRL_FIELD(lane2_tx_preemp);
	INIT_CTRL_FIELD(lane2_tx_vswing);
	INIT_CTRL_FIELD(lane3_tx_preemp);
	INIT_CTRL_FIELD(lane3_tx_vswing);
	INIT_CTRL_FIELD(custom80b_0);
	INIT_CTRL_FIELD(custom80b_1);
	INIT_CTRL_FIELD(custom80b_2);
	INIT_CTRL_FIELD(num_sr_zeros);
	INIT_CTRL_FIELD(aux_len_req);
	INIT_CTRL_FIELD(i2c_addr_only);
	INIT_CTRL_FIELD(aux_addr);
	INIT_CTRL_FIELD(aux_cmd_type);
	INIT_CTRL_FIELD(aux_status);
	INIT_CTRL_FIELD(aux_m);
	INIT_CTRL_FIELD(aux_reply_received);
	INIT_CTRL_FIELD(aux_timeout);
	INIT_CTRL_FIELD(aux_reply_err);
	INIT_CTRL_FIELD(aux_bytes_read);
	INIT_CTRL_FIELD(sink_disconnect_while_active);
	INIT_CTRL_FIELD(aux_reply_err_code);
	INIT_CTRL_FIELD(aux_state);
	INIT_CTRL_FIELD(aux_data0);
	INIT_CTRL_FIELD(aux_data1);
	INIT_CTRL_FIELD(aux_data2);
	INIT_CTRL_FIELD(aux_data3);
	INIT_CTRL_FIELD(aux_250us_cnt_limit);
	INIT_CTRL_FIELD(aux_2000us_cnt_limit);
	INIT_CTRL_FIELD(aux_100000us_cnt_limit);
	INIT_CTRL_FIELD(typec_disable_ack);
	INIT_CTRL_FIELD(typec_disable_status);
	INIT_CTRL_FIELD(typec_interrupt_status);
	INIT_CTRL_FIELD(tx0_in_generic_bus);
	INIT_CTRL_FIELD(tx0_hp_prot_en);
	INIT_CTRL_FIELD(tx0_bypass_eq_calc);
	INIT_CTRL_FIELD(tx1_in_generic_bus);
	INIT_CTRL_FIELD(tx1_hp_prot_en);
	INIT_CTRL_FIELD(tx1_bypass_eq_calc);
	INIT_CTRL_FIELD(tx2_in_generic_bus);
	INIT_CTRL_FIELD(tx2_hp_prot_en);
	INIT_CTRL_FIELD(tx2_bypass_eq_calc);
	INIT_CTRL_FIELD(tx3_in_generic_bus);
	INIT_CTRL_FIELD(tx3_hp_prot_en);
	INIT_CTRL_FIELD(tx3_bypass_eq_calc);
	INIT_CTRL_FIELD(tx0_out_generic_bus);
	INIT_CTRL_FIELD(tx1_out_generic_bus);
	INIT_CTRL_FIELD(tx2_out_generic_bus);
	INIT_CTRL_FIELD(tx3_out_generic_bus);
	INIT_CTRL_FIELD(combo_phy_ovr);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_multiplier);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_div_multiplier);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_tx_clk_div);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_ssc_freq_cnt_init);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_ssc_freq_cnt_peak);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_ssc_freq_cnt_ovrd_en);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_div_clk_en);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_word_div2_en);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_init_cal_disable);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_pmix_en);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_v2i);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_cp_int);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_cp_prop);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_ssc_up_spread);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_ssc_peak);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_ssc_stepsize);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_fracn_cfg_update_en);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_fracn_en);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_fracn_den);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_fracn_quot);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_fracn_rem);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_freq_vco);
	INIT_CTRL_FIELD(combo_phy_ovr_ref_clk_mpll_div);
	INIT_CTRL_FIELD(combo_phy_ovr_mpll_div5_clk_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx0_term_ctrl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx1_term_ctrl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx2_term_ctrl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx3_term_ctrl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g1);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g1);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g1);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g1);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g2);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g2);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g2);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g2);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g3);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g3);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g3);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g3);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g4);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g4);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g4);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g4);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g5);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g5);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g5);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g5);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g6);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g6);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g6);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g6);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g7);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g7);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g7);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g7);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_g8);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_main_g8);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_post_g8);
	INIT_CTRL_FIELD(combo_phy_ovr_tx_eq_pre_g8);
	INIT_CTRL_FIELD(combo_phy_ovr_tx0_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx0_vboost_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx0_iboost_lvl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx0_clk_rdy);
	INIT_CTRL_FIELD(combo_phy_ovr_tx0_invert);
	INIT_CTRL_FIELD(combo_phy_ovr_tx1_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx1_vboost_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx1_iboost_lvl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx1_clk_rdy);
	INIT_CTRL_FIELD(combo_phy_ovr_tx1_invert);
	INIT_CTRL_FIELD(combo_phy_ovr_tx2_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx2_vboost_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx2_iboost_lvl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx2_clk_rdy);
	INIT_CTRL_FIELD(combo_phy_ovr_tx2_invert);
	INIT_CTRL_FIELD(combo_phy_ovr_tx3_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx3_vboost_en);
	INIT_CTRL_FIELD(combo_phy_ovr_tx3_iboost_lvl);
	INIT_CTRL_FIELD(combo_phy_ovr_tx3_clk_rdy);
	INIT_CTRL_FIELD(combo_phy_ovr_tx3_invert);
	INIT_CTRL_FIELD(hpd_event);
	INIT_CTRL_FIELD(aux_reply_event);
	INIT_CTRL_FIELD(hdcp_event);
	INIT_CTRL_FIELD(aux_cmd_invalid);
	INIT_CTRL_FIELD(sdp_event_stream0);
	INIT_CTRL_FIELD(audio_fifo_overflow_stream0);
	INIT_CTRL_FIELD(video_fifo_overflow_stream0);
	INIT_CTRL_FIELD(video_fifo_underflow_stream0);
	INIT_CTRL_FIELD(sdp_event_stream1);
	INIT_CTRL_FIELD(audio_fifo_overflow_stream1);
	INIT_CTRL_FIELD(video_fifo_overflow_stream1);
	INIT_CTRL_FIELD(video_fifo_underflow_stream1);
	INIT_CTRL_FIELD(sdp_event_stream2);
	INIT_CTRL_FIELD(audio_fifo_overflow_stream2);
	INIT_CTRL_FIELD(video_fifo_overflow_stream2);
	INIT_CTRL_FIELD(video_fifo_underflow_stream2);
	INIT_CTRL_FIELD(sdp_event_stream3);
	INIT_CTRL_FIELD(audio_fifo_overflow_stream3);
	INIT_CTRL_FIELD(video_fifo_overflow_stream3);
	INIT_CTRL_FIELD(video_fifo_underflow_stream3);
	INIT_CTRL_FIELD(dsc_event);
	INIT_CTRL_FIELD(hpd_event_en);
	INIT_CTRL_FIELD(aux_reply_event_en);
	INIT_CTRL_FIELD(hdcp_event_en);
	INIT_CTRL_FIELD(aux_cmd_invalid_en);
	INIT_CTRL_FIELD(sdp_event_en_stream0);
	INIT_CTRL_FIELD(audio_fifo_overflow_en_stream0);
	INIT_CTRL_FIELD(video_fifo_overflow_en_stream0);
	INIT_CTRL_FIELD(video_fifo_underflow_en_stream0);
	INIT_CTRL_FIELD(sdp_event_en_stream1);
	INIT_CTRL_FIELD(audio_fifo_overflow_en_stream1);
	INIT_CTRL_FIELD(video_fifo_overflow_en_stream1);
	INIT_CTRL_FIELD(video_fifo_underflow_en_stream1);
	INIT_CTRL_FIELD(sdp_event_en_stream2);
	INIT_CTRL_FIELD(audio_fifo_overflow_en_stream2);
	INIT_CTRL_FIELD(video_fifo_overflow_en_stream2);
	INIT_CTRL_FIELD(video_fifo_underflow_en_stream2);
	INIT_CTRL_FIELD(sdp_event_en_stream3);
	INIT_CTRL_FIELD(audio_fifo_overflow_en_stream3);
	INIT_CTRL_FIELD(video_fifo_overflow_en_stream3);
	INIT_CTRL_FIELD(video_fifo_underflow_en_stream3);
	INIT_CTRL_FIELD(dsc_event_en);
	INIT_CTRL_FIELD(hpd_irq);
	INIT_CTRL_FIELD(hpd_hot_plug);
	INIT_CTRL_FIELD(hpd_hot_unplug);
	INIT_CTRL_FIELD(hpd_unplug_err);
	INIT_CTRL_FIELD(hpd_status);
	INIT_CTRL_FIELD(hpd_state);
	INIT_CTRL_FIELD(hpd_timer);
	INIT_CTRL_FIELD(hpd_irq_en);
	INIT_CTRL_FIELD(hpd_plug_en);
	INIT_CTRL_FIELD(hpd_unplug_en);
	INIT_CTRL_FIELD(hpd_unplug_err_en);
	INIT_CTRL_FIELD(enable_hdcp);
	INIT_CTRL_FIELD(enable_hdcp_13);
	INIT_CTRL_FIELD(encryptiondisable);
	INIT_CTRL_FIELD(hdcp_lock);
	INIT_CTRL_FIELD(bypencryption);
	INIT_CTRL_FIELD(cp_irq);
	INIT_CTRL_FIELD(dpcd12plus);
	INIT_CTRL_FIELD(hdcpengaged);
	INIT_CTRL_FIELD(substatea);
	INIT_CTRL_FIELD(statea);
	INIT_CTRL_FIELD(stater);
	INIT_CTRL_FIELD(stateoeg);
	INIT_CTRL_FIELD(statee);
	INIT_CTRL_FIELD(hdcp_capable);
	INIT_CTRL_FIELD(repeater);
	INIT_CTRL_FIELD(hdcp13_bstatus);
	INIT_CTRL_FIELD(hdcp2_booted);
	INIT_CTRL_FIELD(hdcp2_state);
	INIT_CTRL_FIELD(hdcp2_sink_cap_check_complete);
	INIT_CTRL_FIELD(hdcp2_capable_sink);
	INIT_CTRL_FIELD(hdcp2_authentication_success);
	INIT_CTRL_FIELD(hdcp2_authentication_failed);
	INIT_CTRL_FIELD(hdcp2_re_authentication_req);
	INIT_CTRL_FIELD(ksvaccessint_clr); /* ksvaccessint */
	INIT_CTRL_FIELD(auxrespdefer7times_clr); /* auxrespdefer7times */
	INIT_CTRL_FIELD(auxresptimeout_clr); /* auxresptimeout */
	INIT_CTRL_FIELD(auxrespnack7times_clr); /* auxrespnack7times */
	INIT_CTRL_FIELD(ksvsha1calcdoneint_clr); /* ksvsha1calcdoneint */
	INIT_CTRL_FIELD(hdcp_failed_clr); /* hdcp_failed */
	INIT_CTRL_FIELD(hdcp_engaged_clr); /* hdcp_engaged */
	INIT_CTRL_FIELD(hdcp2_gpioint_clr); /* hdcp2_gpioint */
	INIT_CTRL_FIELD(ksvaccessint_stat); /* ksvaccessint */
	INIT_CTRL_FIELD(auxrespdefer7times_stat); /* auxrespdefer7times */
	INIT_CTRL_FIELD(auxresptimeout_stat); /* auxresptimeout */
	INIT_CTRL_FIELD(auxrespnack7times_stat); /* auxrespnack7times */
	INIT_CTRL_FIELD(ksvsha1calcdoneint_stat); /* ksvsha1calcdoneint */
	INIT_CTRL_FIELD(hdcp_failed_stat); /* hdcp_failed */
	INIT_CTRL_FIELD(hdcp_engaged_stat); /* hdcp_engaged */
	INIT_CTRL_FIELD(hdcp2_gpioint_stat); /* hdcp2_gpioint */
	INIT_CTRL_FIELD(ksvaccessint_msk); /* ksvaccessint */
	INIT_CTRL_FIELD(auxrespdefer7times_msk); /* auxrespdefer7times */
	INIT_CTRL_FIELD(auxresptimeout_msk); /* auxresptimeout */
	INIT_CTRL_FIELD(auxrespnack7times_msk); /* auxrespnack7times */
	INIT_CTRL_FIELD(ksvsha1calcdoneint_msk); /* ksvsha1calcdoneint */
	INIT_CTRL_FIELD(hdcp_failed_msk); /* hdcp_failed */
	INIT_CTRL_FIELD(hdcp_engaged_msk); /* hdcp_engaged */
	INIT_CTRL_FIELD(hdcp2_gpioint_msk); /* hdcp2_gpioint */
	INIT_CTRL_FIELD(ksvmemrequest);
	INIT_CTRL_FIELD(ksvmemaccess);
	INIT_CTRL_FIELD(ksvlistprocessupd);
	INIT_CTRL_FIELD(ksvsha1swstatus);
	INIT_CTRL_FIELD(ksvsha1status);
	INIT_CTRL_FIELD(hdcpreg_bksv0);
	INIT_CTRL_FIELD(hdcpreg_bksv1);
	INIT_CTRL_FIELD(oanbypass);
	INIT_CTRL_FIELD(hdcpreg_an0);
	INIT_CTRL_FIELD(hdcpreg_an1);
	INIT_CTRL_FIELD(odpk_decrypt_enable);
	INIT_CTRL_FIELD(idpk_data_index);
	INIT_CTRL_FIELD(idpk_wr_ok_sts);
	INIT_CTRL_FIELD(hdcpreg_seed);
	INIT_CTRL_FIELD(dpk_data_0); /* dpk_data */
	INIT_CTRL_FIELD(dpk_data_1); /* dpk_data */
	INIT_CTRL_FIELD(hdcp2gpiooutsts);
	INIT_CTRL_FIELD(hdcp2gpiooutchngsts);
	INIT_CTRL_FIELD(dpk_crc);
	INIT_CTRL_FIELD(vg_swrst);
	INIT_CTRL_FIELD(odepolarity);
	INIT_CTRL_FIELD(ohsyncpolarity);
	INIT_CTRL_FIELD(ovsyncpolarity);
	INIT_CTRL_FIELD(oip);
	INIT_CTRL_FIELD(ocolorincrement);
	INIT_CTRL_FIELD(ovblankoscillation);
	INIT_CTRL_FIELD(ycc_422_mapping);
	INIT_CTRL_FIELD(ycc_pattern_generation);
	INIT_CTRL_FIELD(pixel_repetition);
	INIT_CTRL_FIELD(bits_per_comp);
	INIT_CTRL_FIELD(ycc_420_mapping);
	INIT_CTRL_FIELD(internal_external_gen);
	INIT_CTRL_FIELD(pattern_mode);
	INIT_CTRL_FIELD(hactive_vg_config2); /* hactive */
	INIT_CTRL_FIELD(hblank_vg_config2); /* hblank */
	INIT_CTRL_FIELD(h_front_porch);
	INIT_CTRL_FIELD(h_sync_width_vg_config3); /* h_sync_width */
	INIT_CTRL_FIELD(vactive_vg_config4); /* vactive */
	INIT_CTRL_FIELD(vblank_vg_config4); /* vblank */
	INIT_CTRL_FIELD(v_front_porch);
	INIT_CTRL_FIELD(v_sync_width_vg_config5); /* v_sync_width */
	INIT_CTRL_FIELD(td_structure);
	INIT_CTRL_FIELD(td_enable);
	INIT_CTRL_FIELD(td_frameseq);
	INIT_CTRL_FIELD(ipi_enable);
	INIT_CTRL_FIELD(ipi_select);
	INIT_CTRL_FIELD(ram_addr_start);
	INIT_CTRL_FIELD(start_write_ram);
	INIT_CTRL_FIELD(write_ram_data);
	INIT_CTRL_FIELD(ram_stop_addr);
	INIT_CTRL_FIELD(vg_cb_width);
	INIT_CTRL_FIELD(vg_cb_height);
	INIT_CTRL_FIELD(vg_cb_colora_lsb);
	INIT_CTRL_FIELD(vg_cb_color_a_msb);
	INIT_CTRL_FIELD(vg_cb_color_b_lsb);
	INIT_CTRL_FIELD(vg_cb_color_b_msb);
	INIT_CTRL_FIELD(ag_swrst);
	INIT_CTRL_FIELD(hbren);
	INIT_CTRL_FIELD(audiosource_clockmultiplier);
	INIT_CTRL_FIELD(i2s_wordwidth);
	INIT_CTRL_FIELD(audio_source);
	INIT_CTRL_FIELD(nlpcm_en);
	INIT_CTRL_FIELD(spdiftxdata);
	INIT_CTRL_FIELD(audio_use_lut);
	INIT_CTRL_FIELD(audio_use_counter);
	INIT_CTRL_FIELD(audio_counter_offset);
	INIT_CTRL_FIELD(incleft);
	INIT_CTRL_FIELD(incright);
	INIT_CTRL_FIELD(iec_copyright);
	INIT_CTRL_FIELD(iec_cgmsa);
	INIT_CTRL_FIELD(iec_nlpcm);
	INIT_CTRL_FIELD(iec_categorycode);
	INIT_CTRL_FIELD(iec_sourcenumber);
	INIT_CTRL_FIELD(iec_pcm_audio_mode);
	INIT_CTRL_FIELD(iec_channelnumcl0_3); /* iec_channelnumcl0 */
	INIT_CTRL_FIELD(iec_channelnumcr0_3); /* iec_channelnumcr0 */
	INIT_CTRL_FIELD(iec_samp_freq);
	INIT_CTRL_FIELD(iec_clkaccuracy);
	INIT_CTRL_FIELD(iec_word_length);
	INIT_CTRL_FIELD(iec_origsampfreq);
	INIT_CTRL_FIELD(iec_channelnumcl0_5); /* iec_channelnumcl0 */
	INIT_CTRL_FIELD(iec_channelnumcr0_5); /* iec_channelnumcr0 */
	INIT_CTRL_FIELD(iec_channelnumcl1);
	INIT_CTRL_FIELD(iec_channelnumcr1);
	INIT_CTRL_FIELD(iec_channelnumcl2);
	INIT_CTRL_FIELD(iec_channelnumcr2);
	INIT_CTRL_FIELD(iec_channelnumcl2a);
	INIT_CTRL_FIELD(iec_channelnumcr2a);
	INIT_CTRL_FIELD(userdata_cl0);
	INIT_CTRL_FIELD(userdata_cr0);
	INIT_CTRL_FIELD(userdata_cl1);
	INIT_CTRL_FIELD(userdata_cr1);
	INIT_CTRL_FIELD(userdata_cl2);
	INIT_CTRL_FIELD(userdata_cr2);
	INIT_CTRL_FIELD(userdata_cl3);
	INIT_CTRL_FIELD(userdata_cr3);
	INIT_CTRL_FIELD(validity_bit_cl0);
	INIT_CTRL_FIELD(validity_bit_cr0);
	INIT_CTRL_FIELD(validity_bit_cl1);
	INIT_CTRL_FIELD(validity_bit_cr1);
	INIT_CTRL_FIELD(validity_bit_cl2);
	INIT_CTRL_FIELD(validity_bit_cr2);
	INIT_CTRL_FIELD(validity_bit_cl3);
	INIT_CTRL_FIELD(validity_bit_cr3);

	return 0;
}
