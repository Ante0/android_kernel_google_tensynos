/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

int ctrl_regmap_fields_init(struct dptx *dptx);

struct ctrl_regfield_variant {
	struct reg_field	cfg_version_number;
	struct reg_field	cfg_version_type;
	struct reg_field	cfg_vendor_id;
	struct reg_field	cfg_device_id;
	struct reg_field	cfg_hdcp_select;
	struct reg_field	cfg_audio_select;
	struct reg_field	cfg_phy_used;
	struct reg_field	cfg_sdp_reg_bank_size;
	struct reg_field	cfg_fpga_en;
	struct reg_field	cfg_dpk_romless;
	struct reg_field	cfg_dpk_8bit;
	struct reg_field	cfg_sync_depth;
	struct reg_field	cfg_num_streams;
	struct reg_field	cfg_mp_mode;
	struct reg_field	cfg_dsc_en;
	struct reg_field	cfg_edp_en;
	struct reg_field	cfg_fec_en;
	struct reg_field	cfg_gen2_phy;
	struct reg_field	cfg_phy_type;
	struct reg_field	cfg_adsync_en;
	struct reg_field	cfg_psr_ver;
	struct reg_field	cfg_scramble_dis;
	struct reg_field	cfg_enhance_framing_en;
	struct reg_field	cfg_default_fast_link_train_en;
	struct reg_field	cfg_scale_down_mode;
	struct reg_field	cfg_force_hpd;
	struct reg_field	cfg_disable_interleaving;
	struct reg_field	cfg_sel_aux_timeout_32ms;
	struct reg_field	cfg_debug_control;
	struct reg_field	cfg_sr_scale_down;
	struct reg_field	cfg_bs_512_scale_down;
	struct reg_field	cfg_enable_mst_mode;
	struct reg_field	cfg_enable_fec;
	struct reg_field	cfg_enable_edp;
	struct reg_field	cfg_initiate_mst_act_seq;
	struct reg_field	cfg_enhance_framing_with_fec_en;
	struct reg_field	cfg_controller_reset;
	struct reg_field	cfg_phy_soft_reset;
	struct reg_field	cfg_hdcp_module_reset;
	struct reg_field	cfg_audio_sampler_reset;
	struct reg_field	cfg_aux_reset;
	struct reg_field	cfg_video_reset;
	struct reg_field	cfg_audio_sampler_reset_stream1;
	struct reg_field	cfg_audio_sampler_reset_stream2;
	struct reg_field	cfg_audio_sampler_reset_stream3;
	struct reg_field	cfg_aux_cdr_state;
	struct reg_field	cfg_aux_cdr_clock_cycle;
	struct reg_field	cfg_video_stream_enable;
	struct reg_field	cfg_video_mapping_ipi_en;
	struct reg_field	cfg_video_mapping;
	struct reg_field	cfg_pixel_mode_select;
	struct reg_field	cfg_enable_dsc;
	struct reg_field	cfg_encryption_enable;
	struct reg_field	cfg_stream_type;
	struct reg_field	cfg_bcb_data_stuffing_en;
	struct reg_field	cfg_rcr_data_stuffing_en;
	struct reg_field	cfg_gy_data_stuffing_en;
	struct reg_field	cfg_bcb_stuff_data;
	struct reg_field	cfg_rcr_stuff_data;
	struct reg_field	cfg_gy_stuff_data;
	struct reg_field	cfg_vsync_in_polarity;
	struct reg_field	cfg_hsync_in_polarity;
	struct reg_field	cfg_de_in_polarity;
	struct reg_field	cfg_r_v_blank_in_osc;
	struct reg_field	cfg_i_p;
	struct reg_field	cfg_hblank_video_config1; /* hblank */
	struct reg_field	cfg_hactive_video_config1; /* hactive */
	struct reg_field	cfg_vactive_video_config2; /* vactive */
	struct reg_field	cfg_vblank_video_config2; /* vblank */
	struct reg_field	cfg_h_sync_width_video_config3; /* h_sync_width */
	struct reg_field	cfg_v_sync_width_video_config4; /* v_sync_width */
	struct reg_field	cfg_average_bytes_per_tu;
	struct reg_field	cfg_init_threshold;
	struct reg_field	cfg_average_bytes_per_tu_frac;
	struct reg_field	cfg_enable_3d_frame_field_seq;
	struct reg_field	cfg_init_threshold_hi;
	struct reg_field	cfg_hstart;
	struct reg_field	cfg_vstart;
	struct reg_field	cfg_mvid;
	struct reg_field	cfg_misc0;
	struct reg_field	cfg_nvid;
	struct reg_field	cfg_misc1;
	struct reg_field	cfg_hblank_interval;
	struct reg_field	cfg_mvid_cust_en;
	struct reg_field	cfg_mvid_out_clr_mode;
	struct reg_field	cfg_mvid_cust_den;
	struct reg_field	cfg_mvid_cust_mod;
	struct reg_field	cfg_mvid_cust_quo;
	struct reg_field	cfg_audio_inf_select;
	struct reg_field	cfg_audio_data_in_en;
	struct reg_field	cfg_audio_data_width;
	struct reg_field	cfg_hbr_mode_enable;
	struct reg_field	cfg_num_channels;
	struct reg_field	cfg_audio_mute;
	struct reg_field	cfg_audio_packet_id;
	struct reg_field	cfg_audio_timestamp_version_num;
	struct reg_field	cfg_audio_clk_mult_fs;
	struct reg_field	cfg_en_audio_timestamp_sdp_vertical_ctrl; /* en_audio_timestamp_sdp */
	struct reg_field	cfg_en_audio_stream_sdp_vertical_ctrl; /* en_audio_stream_sdp */
	struct reg_field	cfg_en_vertical_sdp_n;
	struct reg_field	cfg_en_128bytes_sdp_1;
	struct reg_field	cfg_disable_ext_sdp;
	struct reg_field	cfg_fixed_priority_arbitration_vertical_ctrl; /* fixed_priority_arbitration */
	struct reg_field	cfg_en_audio_timestamp_sdp_horizontal_ctrl; /* en_audio_timestamp_sdp */
	struct reg_field	cfg_en_audio_stream_sdp_horizontal_ctrl; /* en_audio_stream_sdp */
	struct reg_field	cfg_en_horizontal_sdp_n;
	struct reg_field	cfg_fixed_priority_arbitration_horizontal_ctrl; /* fixed_priority_arbitration */
	struct reg_field	cfg_audio_timestamp_sdp_status;
	struct reg_field	cfg_audio_stream_sdp_status;
	struct reg_field	cfg_sdp_n_tx_status;
	struct reg_field	cfg_manual_mode_sdp;
	struct reg_field	cfg_audio_timestamp_sdp_status_en;
	struct reg_field	cfg_audio_stream_sdp_status_en;
	struct reg_field	cfg_sdp_status_en;
	struct reg_field	cfg_sdp_16b_bytes_reqd_vblank_ovr;
	struct reg_field	cfg_sdp_16b_bytes_reqd_hblank_ovr;
	struct reg_field	cfg_sdp_32b_bytes_reqd_vblank_ovr;
	struct reg_field	cfg_sdp_32b_bytes_reqd_hblank_ovr;
	struct reg_field	cfg_sdp_128b_bytes_reqd_vblank_ovr;
	struct reg_field	cfg_sdp_128b_bytes_reqd_hblank_ovr;
	struct reg_field	cfg_tps_sel;
	struct reg_field	cfg_phyrate;
	struct reg_field	cfg_phy_lanes;
	struct reg_field	cfg_xmit_enable;
	struct reg_field	cfg_phy_busy;
	struct reg_field	cfg_ssc_dis;
	struct reg_field	cfg_phy_powerdown;
	struct reg_field	cfg_phy_width;
	struct reg_field	cfg_edp_phy_rate;
	struct reg_field	cfg_lane0_tx_preemp;
	struct reg_field	cfg_lane0_tx_vswing;
	struct reg_field	cfg_lane1_tx_preemp;
	struct reg_field	cfg_lane1_tx_vswing;
	struct reg_field	cfg_lane2_tx_preemp;
	struct reg_field	cfg_lane2_tx_vswing;
	struct reg_field	cfg_lane3_tx_preemp;
	struct reg_field	cfg_lane3_tx_vswing;
	struct reg_field	cfg_custom80b_0;
	struct reg_field	cfg_custom80b_1;
	struct reg_field	cfg_custom80b_2;
	struct reg_field	cfg_num_sr_zeros;
	struct reg_field	cfg_aux_len_req;
	struct reg_field	cfg_i2c_addr_only;
	struct reg_field	cfg_aux_addr;
	struct reg_field	cfg_aux_cmd_type;
	struct reg_field	cfg_aux_status;
	struct reg_field	cfg_aux_m;
	struct reg_field	cfg_aux_reply_received;
	struct reg_field	cfg_aux_timeout;
	struct reg_field	cfg_aux_reply_err;
	struct reg_field	cfg_aux_bytes_read;
	struct reg_field	cfg_sink_disconnect_while_active;
	struct reg_field	cfg_aux_reply_err_code;
	struct reg_field	cfg_aux_state;
	struct reg_field	cfg_aux_data0;
	struct reg_field	cfg_aux_data1;
	struct reg_field	cfg_aux_data2;
	struct reg_field	cfg_aux_data3;
	struct reg_field	cfg_aux_250us_cnt_limit;
	struct reg_field	cfg_aux_2000us_cnt_limit;
	struct reg_field	cfg_aux_100000us_cnt_limit;
	struct reg_field	cfg_typec_disable_ack;
	struct reg_field	cfg_typec_disable_status;
	struct reg_field	cfg_typec_interrupt_status;
	struct reg_field	cfg_tx0_in_generic_bus;
	struct reg_field	cfg_tx0_hp_prot_en;
	struct reg_field	cfg_tx0_bypass_eq_calc;
	struct reg_field	cfg_tx1_in_generic_bus;
	struct reg_field	cfg_tx1_hp_prot_en;
	struct reg_field	cfg_tx1_bypass_eq_calc;
	struct reg_field	cfg_tx2_in_generic_bus;
	struct reg_field	cfg_tx2_hp_prot_en;
	struct reg_field	cfg_tx2_bypass_eq_calc;
	struct reg_field	cfg_tx3_in_generic_bus;
	struct reg_field	cfg_tx3_hp_prot_en;
	struct reg_field	cfg_tx3_bypass_eq_calc;
	struct reg_field	cfg_tx0_out_generic_bus;
	struct reg_field	cfg_tx1_out_generic_bus;
	struct reg_field	cfg_tx2_out_generic_bus;
	struct reg_field	cfg_tx3_out_generic_bus;
	struct reg_field	cfg_combo_phy_ovr;
	struct reg_field	cfg_combo_phy_ovr_mpll_multiplier;
	struct reg_field	cfg_combo_phy_ovr_mpll_div_multiplier;
	struct reg_field	cfg_combo_phy_ovr_mpll_tx_clk_div;
	struct reg_field	cfg_combo_phy_ovr_mpll_ssc_freq_cnt_init;
	struct reg_field	cfg_combo_phy_ovr_mpll_ssc_freq_cnt_peak;
	struct reg_field	cfg_combo_phy_ovr_mpll_ssc_freq_cnt_ovrd_en;
	struct reg_field	cfg_combo_phy_ovr_mpll_div_clk_en;
	struct reg_field	cfg_combo_phy_ovr_mpll_word_div2_en;
	struct reg_field	cfg_combo_phy_ovr_mpll_init_cal_disable;
	struct reg_field	cfg_combo_phy_ovr_mpll_pmix_en;
	struct reg_field	cfg_combo_phy_ovr_mpll_v2i;
	struct reg_field	cfg_combo_phy_ovr_mpll_cp_int;
	struct reg_field	cfg_combo_phy_ovr_mpll_cp_prop;
	struct reg_field	cfg_combo_phy_ovr_mpll_ssc_up_spread;
	struct reg_field	cfg_combo_phy_ovr_mpll_ssc_peak;
	struct reg_field	cfg_combo_phy_ovr_mpll_ssc_stepsize;
	struct reg_field	cfg_combo_phy_ovr_mpll_fracn_cfg_update_en;
	struct reg_field	cfg_combo_phy_ovr_mpll_fracn_en;
	struct reg_field	cfg_combo_phy_ovr_mpll_fracn_den;
	struct reg_field	cfg_combo_phy_ovr_mpll_fracn_quot;
	struct reg_field	cfg_combo_phy_ovr_mpll_fracn_rem;
	struct reg_field	cfg_combo_phy_ovr_mpll_freq_vco;
	struct reg_field	cfg_combo_phy_ovr_ref_clk_mpll_div;
	struct reg_field	cfg_combo_phy_ovr_mpll_div5_clk_en;
	struct reg_field	cfg_combo_phy_ovr_tx0_term_ctrl;
	struct reg_field	cfg_combo_phy_ovr_tx1_term_ctrl;
	struct reg_field	cfg_combo_phy_ovr_tx2_term_ctrl;
	struct reg_field	cfg_combo_phy_ovr_tx3_term_ctrl;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g1;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g1;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g1;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g1;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g2;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g2;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g2;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g2;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g3;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g3;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g3;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g3;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g4;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g4;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g4;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g4;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g5;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g5;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g5;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g5;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g6;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g6;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g6;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g6;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g7;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g7;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g7;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g7;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_g8;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_main_g8;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_post_g8;
	struct reg_field	cfg_combo_phy_ovr_tx_eq_pre_g8;
	struct reg_field	cfg_combo_phy_ovr_tx0_en;
	struct reg_field	cfg_combo_phy_ovr_tx0_vboost_en;
	struct reg_field	cfg_combo_phy_ovr_tx0_iboost_lvl;
	struct reg_field	cfg_combo_phy_ovr_tx0_clk_rdy;
	struct reg_field	cfg_combo_phy_ovr_tx0_invert;
	struct reg_field	cfg_combo_phy_ovr_tx1_en;
	struct reg_field	cfg_combo_phy_ovr_tx1_vboost_en;
	struct reg_field	cfg_combo_phy_ovr_tx1_iboost_lvl;
	struct reg_field	cfg_combo_phy_ovr_tx1_clk_rdy;
	struct reg_field	cfg_combo_phy_ovr_tx1_invert;
	struct reg_field	cfg_combo_phy_ovr_tx2_en;
	struct reg_field	cfg_combo_phy_ovr_tx2_vboost_en;
	struct reg_field	cfg_combo_phy_ovr_tx2_iboost_lvl;
	struct reg_field	cfg_combo_phy_ovr_tx2_clk_rdy;
	struct reg_field	cfg_combo_phy_ovr_tx2_invert;
	struct reg_field	cfg_combo_phy_ovr_tx3_en;
	struct reg_field	cfg_combo_phy_ovr_tx3_vboost_en;
	struct reg_field	cfg_combo_phy_ovr_tx3_iboost_lvl;
	struct reg_field	cfg_combo_phy_ovr_tx3_clk_rdy;
	struct reg_field	cfg_combo_phy_ovr_tx3_invert;
	struct reg_field	cfg_hpd_event;
	struct reg_field	cfg_aux_reply_event;
	struct reg_field	cfg_hdcp_event;
	struct reg_field	cfg_aux_cmd_invalid;
	struct reg_field	cfg_sdp_event_stream0;
	struct reg_field	cfg_audio_fifo_overflow_stream0;
	struct reg_field	cfg_video_fifo_overflow_stream0;
	struct reg_field	cfg_video_fifo_underflow_stream0;
	struct reg_field	cfg_sdp_event_stream1;
	struct reg_field	cfg_audio_fifo_overflow_stream1;
	struct reg_field	cfg_video_fifo_overflow_stream1;
	struct reg_field	cfg_video_fifo_underflow_stream1;
	struct reg_field	cfg_sdp_event_stream2;
	struct reg_field	cfg_audio_fifo_overflow_stream2;
	struct reg_field	cfg_video_fifo_overflow_stream2;
	struct reg_field	cfg_video_fifo_underflow_stream2;
	struct reg_field	cfg_sdp_event_stream3;
	struct reg_field	cfg_audio_fifo_overflow_stream3;
	struct reg_field	cfg_video_fifo_overflow_stream3;
	struct reg_field	cfg_video_fifo_underflow_stream3;
	struct reg_field	cfg_dsc_event;
	struct reg_field	cfg_hpd_event_en;
	struct reg_field	cfg_aux_reply_event_en;
	struct reg_field	cfg_hdcp_event_en;
	struct reg_field	cfg_aux_cmd_invalid_en;
	struct reg_field	cfg_sdp_event_en_stream0;
	struct reg_field	cfg_audio_fifo_overflow_en_stream0;
	struct reg_field	cfg_video_fifo_overflow_en_stream0;
	struct reg_field	cfg_video_fifo_underflow_en_stream0;
	struct reg_field	cfg_sdp_event_en_stream1;
	struct reg_field	cfg_audio_fifo_overflow_en_stream1;
	struct reg_field	cfg_video_fifo_overflow_en_stream1;
	struct reg_field	cfg_video_fifo_underflow_en_stream1;
	struct reg_field	cfg_sdp_event_en_stream2;
	struct reg_field	cfg_audio_fifo_overflow_en_stream2;
	struct reg_field	cfg_video_fifo_overflow_en_stream2;
	struct reg_field	cfg_video_fifo_underflow_en_stream2;
	struct reg_field	cfg_sdp_event_en_stream3;
	struct reg_field	cfg_audio_fifo_overflow_en_stream3;
	struct reg_field	cfg_video_fifo_overflow_en_stream3;
	struct reg_field	cfg_video_fifo_underflow_en_stream3;
	struct reg_field	cfg_dsc_event_en;
	struct reg_field	cfg_hpd_irq;
	struct reg_field	cfg_hpd_hot_plug;
	struct reg_field	cfg_hpd_hot_unplug;
	struct reg_field	cfg_hpd_unplug_err;
	struct reg_field	cfg_hpd_status;
	struct reg_field	cfg_hpd_state;
	struct reg_field	cfg_hpd_timer;
	struct reg_field	cfg_hpd_irq_en;
	struct reg_field	cfg_hpd_plug_en;
	struct reg_field	cfg_hpd_unplug_en;
	struct reg_field	cfg_hpd_unplug_err_en;
	struct reg_field	cfg_enable_hdcp;
	struct reg_field	cfg_enable_hdcp_13;
	struct reg_field	cfg_encryptiondisable;
	struct reg_field	cfg_hdcp_lock;
	struct reg_field	cfg_bypencryption;
	struct reg_field	cfg_cp_irq;
	struct reg_field	cfg_dpcd12plus;
	struct reg_field	cfg_hdcpengaged;
	struct reg_field	cfg_substatea;
	struct reg_field	cfg_statea;
	struct reg_field	cfg_stater;
	struct reg_field	cfg_stateoeg;
	struct reg_field	cfg_statee;
	struct reg_field	cfg_hdcp_capable;
	struct reg_field	cfg_repeater;
	struct reg_field	cfg_hdcp13_bstatus;
	struct reg_field	cfg_hdcp2_booted;
	struct reg_field	cfg_hdcp2_state;
	struct reg_field	cfg_hdcp2_sink_cap_check_complete;
	struct reg_field	cfg_hdcp2_capable_sink;
	struct reg_field	cfg_hdcp2_authentication_success;
	struct reg_field	cfg_hdcp2_authentication_failed;
	struct reg_field	cfg_hdcp2_re_authentication_req;
	struct reg_field	cfg_ksvaccessint_clr; /* ksvaccessint */
	struct reg_field	cfg_auxrespdefer7times_clr; /* auxrespdefer7times */
	struct reg_field	cfg_auxresptimeout_clr; /* auxresptimeout */
	struct reg_field	cfg_auxrespnack7times_clr; /* auxrespnack7times */
	struct reg_field	cfg_ksvsha1calcdoneint_clr; /* ksvsha1calcdoneint */
	struct reg_field	cfg_hdcp_failed_clr; /* hdcp_failed */
	struct reg_field	cfg_hdcp_engaged_clr; /* hdcp_engaged */
	struct reg_field	cfg_hdcp2_gpioint_clr; /* hdcp2_gpioint */
	struct reg_field	cfg_ksvaccessint_stat; /* ksvaccessint */
	struct reg_field	cfg_auxrespdefer7times_stat; /* auxrespdefer7times */
	struct reg_field	cfg_auxresptimeout_stat; /* auxresptimeout */
	struct reg_field	cfg_auxrespnack7times_stat; /* auxrespnack7times */
	struct reg_field	cfg_ksvsha1calcdoneint_stat; /* ksvsha1calcdoneint */
	struct reg_field	cfg_hdcp_failed_stat; /* hdcp_failed */
	struct reg_field	cfg_hdcp_engaged_stat; /* hdcp_engaged */
	struct reg_field	cfg_hdcp2_gpioint_stat; /* hdcp2_gpioint */
	struct reg_field	cfg_ksvaccessint_msk; /* ksvaccessint */
	struct reg_field	cfg_auxrespdefer7times_msk; /* auxrespdefer7times */
	struct reg_field	cfg_auxresptimeout_msk; /* auxresptimeout */
	struct reg_field	cfg_auxrespnack7times_msk; /* auxrespnack7times */
	struct reg_field	cfg_ksvsha1calcdoneint_msk; /* ksvsha1calcdoneint */
	struct reg_field	cfg_hdcp_failed_msk; /* hdcp_failed */
	struct reg_field	cfg_hdcp_engaged_msk; /* hdcp_engaged */
	struct reg_field	cfg_hdcp2_gpioint_msk; /* hdcp2_gpioint */
	struct reg_field	cfg_ksvmemrequest;
	struct reg_field	cfg_ksvmemaccess;
	struct reg_field	cfg_ksvlistprocessupd;
	struct reg_field	cfg_ksvsha1swstatus;
	struct reg_field	cfg_ksvsha1status;
	struct reg_field	cfg_hdcpreg_bksv0;
	struct reg_field	cfg_hdcpreg_bksv1;
	struct reg_field	cfg_oanbypass;
	struct reg_field	cfg_hdcpreg_an0;
	struct reg_field	cfg_hdcpreg_an1;
	struct reg_field	cfg_odpk_decrypt_enable;
	struct reg_field	cfg_idpk_data_index;
	struct reg_field	cfg_idpk_wr_ok_sts;
	struct reg_field	cfg_hdcpreg_seed;
	struct reg_field	cfg_dpk_data_0; /* dpk_data */
	struct reg_field	cfg_dpk_data_1; /* dpk_data */
	struct reg_field	cfg_hdcp2gpiooutsts;
	struct reg_field	cfg_hdcp2gpiooutchngsts;
	struct reg_field	cfg_dpk_crc;
	struct reg_field	cfg_vg_swrst;
	struct reg_field	cfg_odepolarity;
	struct reg_field	cfg_ohsyncpolarity;
	struct reg_field	cfg_ovsyncpolarity;
	struct reg_field	cfg_oip;
	struct reg_field	cfg_ocolorincrement;
	struct reg_field	cfg_ovblankoscillation;
	struct reg_field	cfg_ycc_422_mapping;
	struct reg_field	cfg_ycc_pattern_generation;
	struct reg_field	cfg_pixel_repetition;
	struct reg_field	cfg_bits_per_comp;
	struct reg_field	cfg_ycc_420_mapping;
	struct reg_field	cfg_internal_external_gen;
	struct reg_field	cfg_pattern_mode;
	struct reg_field	cfg_hactive_vg_config2; /* hactive */
	struct reg_field	cfg_hblank_vg_config2; /* hblank */
	struct reg_field	cfg_h_front_porch;
	struct reg_field	cfg_h_sync_width_vg_config3; /* h_sync_width */
	struct reg_field	cfg_vactive_vg_config4; /* vactive */
	struct reg_field	cfg_vblank_vg_config4; /* vblank */
	struct reg_field	cfg_v_front_porch;
	struct reg_field	cfg_v_sync_width_vg_config5; /* v_sync_width */
	struct reg_field	cfg_td_structure;
	struct reg_field	cfg_td_enable;
	struct reg_field	cfg_td_frameseq;
	struct reg_field	cfg_ipi_enable;
	struct reg_field	cfg_ipi_select;
	struct reg_field	cfg_ram_addr_start;
	struct reg_field	cfg_start_write_ram;
	struct reg_field	cfg_write_ram_data;
	struct reg_field	cfg_ram_stop_addr;
	struct reg_field	cfg_vg_cb_width;
	struct reg_field	cfg_vg_cb_height;
	struct reg_field	cfg_vg_cb_colora_lsb;
	struct reg_field	cfg_vg_cb_color_a_msb;
	struct reg_field	cfg_vg_cb_color_b_lsb;
	struct reg_field	cfg_vg_cb_color_b_msb;
	struct reg_field	cfg_ag_swrst;
	struct reg_field	cfg_hbren;
	struct reg_field	cfg_audiosource_clockmultiplier;
	struct reg_field	cfg_i2s_wordwidth;
	struct reg_field	cfg_audio_source;
	struct reg_field	cfg_nlpcm_en;
	struct reg_field	cfg_spdiftxdata;
	struct reg_field	cfg_audio_use_lut;
	struct reg_field	cfg_audio_use_counter;
	struct reg_field	cfg_audio_counter_offset;
	struct reg_field	cfg_incleft;
	struct reg_field	cfg_incright;
	struct reg_field	cfg_iec_copyright;
	struct reg_field	cfg_iec_cgmsa;
	struct reg_field	cfg_iec_nlpcm;
	struct reg_field	cfg_iec_categorycode;
	struct reg_field	cfg_iec_sourcenumber;
	struct reg_field	cfg_iec_pcm_audio_mode;
	struct reg_field	cfg_iec_channelnumcl0_3; /* iec_channelnumcl0 */
	struct reg_field	cfg_iec_channelnumcr0_3; /* iec_channelnumcr0 */
	struct reg_field	cfg_iec_samp_freq;
	struct reg_field	cfg_iec_clkaccuracy;
	struct reg_field	cfg_iec_word_length;
	struct reg_field	cfg_iec_origsampfreq;
	struct reg_field	cfg_iec_channelnumcl0_5; /* iec_channelnumcl0 */
	struct reg_field	cfg_iec_channelnumcr0_5; /* iec_channelnumcr0 */
	struct reg_field	cfg_iec_channelnumcl1;
	struct reg_field	cfg_iec_channelnumcr1;
	struct reg_field	cfg_iec_channelnumcl2;
	struct reg_field	cfg_iec_channelnumcr2;
	struct reg_field	cfg_iec_channelnumcl2a;
	struct reg_field	cfg_iec_channelnumcr2a;
	struct reg_field	cfg_userdata_cl0;
	struct reg_field	cfg_userdata_cr0;
	struct reg_field	cfg_userdata_cl1;
	struct reg_field	cfg_userdata_cr1;
	struct reg_field	cfg_userdata_cl2;
	struct reg_field	cfg_userdata_cr2;
	struct reg_field	cfg_userdata_cl3;
	struct reg_field	cfg_userdata_cr3;
	struct reg_field	cfg_validity_bit_cl0;
	struct reg_field	cfg_validity_bit_cr0;
	struct reg_field	cfg_validity_bit_cl1;
	struct reg_field	cfg_validity_bit_cr1;
	struct reg_field	cfg_validity_bit_cl2;
	struct reg_field	cfg_validity_bit_cr2;
	struct reg_field	cfg_validity_bit_cl3;
	struct reg_field	cfg_validity_bit_cr3;
};

static const struct ctrl_regfield_variant ctrl_regfield_cfg = {
	.cfg_version_number = REG_FIELD(DPTX_VERSION_NUMBER, 0, 31),
	.cfg_version_type = REG_FIELD(DPTX_VERSION_TYPE, 0, 31),
	.cfg_vendor_id = REG_FIELD(DPTX_ID, 0, 15),
	.cfg_device_id = REG_FIELD(DPTX_ID, 16, 31),
	.cfg_hdcp_select = REG_FIELD(DPTX_CONFIG_REG1, 0, 0),
	.cfg_audio_select = REG_FIELD(DPTX_CONFIG_REG1, 1, 2),
	.cfg_phy_used = REG_FIELD(DPTX_CONFIG_REG1, 3, 3),
	.cfg_sdp_reg_bank_size = REG_FIELD(DPTX_CONFIG_REG1, 4, 8),
	.cfg_fpga_en = REG_FIELD(DPTX_CONFIG_REG1, 9, 9),
	.cfg_dpk_romless = REG_FIELD(DPTX_CONFIG_REG1, 10, 10),
	.cfg_dpk_8bit = REG_FIELD(DPTX_CONFIG_REG1, 11, 11),
	.cfg_sync_depth = REG_FIELD(DPTX_CONFIG_REG1, 12, 13),
	.cfg_num_streams = REG_FIELD(DPTX_CONFIG_REG1, 16, 18),
	.cfg_mp_mode = REG_FIELD(DPTX_CONFIG_REG1, 19, 21),
	.cfg_dsc_en = REG_FIELD(DPTX_CONFIG_REG1, 22, 22),
	.cfg_edp_en = REG_FIELD(DPTX_CONFIG_REG1, 23, 23),
	.cfg_fec_en = REG_FIELD(DPTX_CONFIG_REG1, 24, 24),
	.cfg_gen2_phy = REG_FIELD(DPTX_CONFIG_REG1, 29, 29),
	.cfg_phy_type = REG_FIELD(DPTX_CONFIG_REG3, 7, 8),
	.cfg_adsync_en = REG_FIELD(DPTX_CONFIG_REG3, 16, 16),
	.cfg_psr_ver = REG_FIELD(DPTX_CONFIG_REG3, 20, 21),
	.cfg_scramble_dis = REG_FIELD(CCTL, 0, 0),
	.cfg_enhance_framing_en = REG_FIELD(CCTL, 1, 1),
	.cfg_default_fast_link_train_en = REG_FIELD(CCTL, 2, 2),
	.cfg_scale_down_mode = REG_FIELD(CCTL, 3, 3),
	.cfg_force_hpd = REG_FIELD(CCTL, 4, 4),
	.cfg_disable_interleaving = REG_FIELD(CCTL, 5, 5),
	.cfg_sel_aux_timeout_32ms = REG_FIELD(CCTL, 6, 6),
	.cfg_debug_control = REG_FIELD(CCTL, 8, 11),
	.cfg_sr_scale_down = REG_FIELD(CCTL, 16, 16),
	.cfg_bs_512_scale_down = REG_FIELD(CCTL, 17, 24),
	.cfg_enable_mst_mode = REG_FIELD(CCTL, 25, 25),
	.cfg_enable_fec = REG_FIELD(CCTL, 26, 26),
	.cfg_enable_edp = REG_FIELD(CCTL, 27, 27),
	.cfg_initiate_mst_act_seq = REG_FIELD(CCTL, 28, 28),
	.cfg_enhance_framing_with_fec_en = REG_FIELD(CCTL, 29, 29),
	.cfg_controller_reset = REG_FIELD(SOFT_RESET_CTRL, 0, 0),
	.cfg_phy_soft_reset = REG_FIELD(SOFT_RESET_CTRL, 1, 1),
	.cfg_hdcp_module_reset = REG_FIELD(SOFT_RESET_CTRL, 2, 2),
	.cfg_audio_sampler_reset = REG_FIELD(SOFT_RESET_CTRL, 3, 3),
	.cfg_aux_reset = REG_FIELD(SOFT_RESET_CTRL, 4, 4),
	.cfg_video_reset = REG_FIELD(SOFT_RESET_CTRL, 5, 8),
	.cfg_audio_sampler_reset_stream1 = REG_FIELD(SOFT_RESET_CTRL, 9, 9),
	.cfg_audio_sampler_reset_stream2 = REG_FIELD(SOFT_RESET_CTRL, 10, 10),
	.cfg_audio_sampler_reset_stream3 = REG_FIELD(SOFT_RESET_CTRL, 11, 11),
	.cfg_aux_cdr_state = REG_FIELD(SOFT_RESET_CTRL, 16, 19),
	.cfg_aux_cdr_clock_cycle = REG_FIELD(SOFT_RESET_CTRL, 24, 28),
	.cfg_video_stream_enable = REG_FIELD(VSAMPLE_CTRL, 5, 5),
	.cfg_video_mapping_ipi_en = REG_FIELD(VSAMPLE_CTRL, 15, 15),
	.cfg_video_mapping = REG_FIELD(VSAMPLE_CTRL, 16, 20),
	.cfg_pixel_mode_select = REG_FIELD(VSAMPLE_CTRL, 21, 22),
	.cfg_enable_dsc = REG_FIELD(VSAMPLE_CTRL, 23, 23),
	.cfg_encryption_enable = REG_FIELD(VSAMPLE_CTRL, 24, 24),
	.cfg_stream_type = REG_FIELD(VSAMPLE_CTRL, 25, 25),
	.cfg_bcb_data_stuffing_en = REG_FIELD(VSAMPLE_STUFF_CTRL1, 0, 0),
	.cfg_rcr_data_stuffing_en = REG_FIELD(VSAMPLE_STUFF_CTRL1, 1, 1),
	.cfg_gy_data_stuffing_en = REG_FIELD(VSAMPLE_STUFF_CTRL1, 2, 2),
	.cfg_bcb_stuff_data = REG_FIELD(VSAMPLE_STUFF_CTRL1, 16, 31),
	.cfg_rcr_stuff_data = REG_FIELD(VSAMPLE_STUFF_CTRL2, 0, 15),
	.cfg_gy_stuff_data = REG_FIELD(VSAMPLE_STUFF_CTRL2, 16, 31),
	.cfg_vsync_in_polarity = REG_FIELD(VINPUT_POLARITY_CTRL, 0, 0),
	.cfg_hsync_in_polarity = REG_FIELD(VINPUT_POLARITY_CTRL, 1, 1),
	.cfg_de_in_polarity = REG_FIELD(VINPUT_POLARITY_CTRL, 2, 2),
	.cfg_r_v_blank_in_osc = REG_FIELD(VIDEO_CONFIG1, 0, 0),
	.cfg_i_p = REG_FIELD(VIDEO_CONFIG1, 1, 1),
	.cfg_hblank_video_config1 = REG_FIELD(VIDEO_CONFIG1, 2, 15), /* hblank */
	.cfg_hactive_video_config1 = REG_FIELD(VIDEO_CONFIG1, 16, 31), /* hactive */
	.cfg_vactive_video_config2 = REG_FIELD(VIDEO_CONFIG2, 0, 15), /* vactive */
	.cfg_vblank_video_config2 = REG_FIELD(VIDEO_CONFIG2, 16, 31), /* vblank */
	.cfg_h_sync_width_video_config3 = REG_FIELD(VIDEO_CONFIG3, 16, 30), /* h_sync_width */
	.cfg_v_sync_width_video_config4 = REG_FIELD(VIDEO_CONFIG4, 16, 30), /* v_sync_width */
	.cfg_average_bytes_per_tu = REG_FIELD(VIDEO_CONFIG5, 0, 6),
	.cfg_init_threshold = REG_FIELD(VIDEO_CONFIG5, 7, 13),
	.cfg_average_bytes_per_tu_frac = REG_FIELD(VIDEO_CONFIG5, 14, 19),
	.cfg_enable_3d_frame_field_seq = REG_FIELD(VIDEO_CONFIG5, 20, 20),
	.cfg_init_threshold_hi = REG_FIELD(VIDEO_CONFIG5, 21, 22),
	.cfg_hstart = REG_FIELD(VIDEO_MSA1, 0, 15),
	.cfg_vstart = REG_FIELD(VIDEO_MSA1, 16, 31),
	.cfg_mvid = REG_FIELD(VIDEO_MSA2, 0, 23),
	.cfg_misc0 = REG_FIELD(VIDEO_MSA2, 24, 31),
	.cfg_nvid = REG_FIELD(VIDEO_MSA3, 0, 23),
	.cfg_misc1 = REG_FIELD(VIDEO_MSA3, 24, 31),
	.cfg_hblank_interval = REG_FIELD(VIDEO_HBLANK_INTERVAL, 0, 15),
	.cfg_mvid_cust_en = REG_FIELD(MVID_CONFIG1, 0, 0),
	.cfg_mvid_out_clr_mode = REG_FIELD(MVID_CONFIG1, 1, 1),
	.cfg_mvid_cust_den = REG_FIELD(MVID_CONFIG1, 16, 31),
	.cfg_mvid_cust_mod = REG_FIELD(MVID_CONFIG2, 0, 14),
	.cfg_mvid_cust_quo = REG_FIELD(MVID_CONFIG2, 16, 31),
	.cfg_audio_inf_select = REG_FIELD(AUD_CONFIG1, 0, 0),
	.cfg_audio_data_in_en = REG_FIELD(AUD_CONFIG1, 1, 4),
	.cfg_audio_data_width = REG_FIELD(AUD_CONFIG1, 5, 9),
	.cfg_hbr_mode_enable = REG_FIELD(AUD_CONFIG1, 10, 10),
	.cfg_num_channels = REG_FIELD(AUD_CONFIG1, 12, 14),
	.cfg_audio_mute = REG_FIELD(AUD_CONFIG1, 15, 15),
	.cfg_audio_packet_id = REG_FIELD(AUD_CONFIG1, 16, 23),
	.cfg_audio_timestamp_version_num = REG_FIELD(AUD_CONFIG1, 24, 29),
	.cfg_audio_clk_mult_fs = REG_FIELD(AUD_CONFIG1, 30, 31),
	.cfg_en_audio_timestamp_sdp_vertical_ctrl = REG_FIELD(SDP_VERTICAL_CTRL, 0, 0), /* en_audio_timestamp_sdp */
	.cfg_en_audio_stream_sdp_vertical_ctrl = REG_FIELD(SDP_VERTICAL_CTRL, 1, 1), /* en_audio_stream_sdp */
	.cfg_en_vertical_sdp_n = REG_FIELD(SDP_VERTICAL_CTRL, 2, 19),
	.cfg_en_128bytes_sdp_1 = REG_FIELD(SDP_VERTICAL_CTRL, 24, 24),
	.cfg_disable_ext_sdp = REG_FIELD(SDP_VERTICAL_CTRL, 30, 30),
	.cfg_fixed_priority_arbitration_vertical_ctrl = REG_FIELD(SDP_VERTICAL_CTRL, 31, 31), /* fixed_priority_arbitration */
	.cfg_en_audio_timestamp_sdp_horizontal_ctrl = REG_FIELD(SDP_HORIZONTAL_CTRL, 0, 0), /* en_audio_timestamp_sdp */
	.cfg_en_audio_stream_sdp_horizontal_ctrl = REG_FIELD(SDP_HORIZONTAL_CTRL, 1, 1), /* en_audio_stream_sdp */
	.cfg_en_horizontal_sdp_n = REG_FIELD(SDP_HORIZONTAL_CTRL, 2, 19),
	.cfg_fixed_priority_arbitration_horizontal_ctrl = REG_FIELD(SDP_HORIZONTAL_CTRL, 31, 31), /* fixed_priority_arbitration */
	.cfg_audio_timestamp_sdp_status = REG_FIELD(SDP_STATUS_REGISTER, 0, 0),
	.cfg_audio_stream_sdp_status = REG_FIELD(SDP_STATUS_REGISTER, 1, 1),
	.cfg_sdp_n_tx_status = REG_FIELD(SDP_STATUS_REGISTER, 2, 19),
	.cfg_manual_mode_sdp = REG_FIELD(SDP_MANUAL_CTRL, 0, 19),
	.cfg_audio_timestamp_sdp_status_en = REG_FIELD(SDP_STATUS_EN, 0, 0),
	.cfg_audio_stream_sdp_status_en = REG_FIELD(SDP_STATUS_EN, 1, 1),
	.cfg_sdp_status_en = REG_FIELD(SDP_STATUS_EN, 2, 19),
	.cfg_sdp_16b_bytes_reqd_vblank_ovr = REG_FIELD(SDP_CONFIG1, 0, 7),
	.cfg_sdp_16b_bytes_reqd_hblank_ovr = REG_FIELD(SDP_CONFIG1, 16, 23),
	.cfg_sdp_32b_bytes_reqd_vblank_ovr = REG_FIELD(SDP_CONFIG2, 0, 7),
	.cfg_sdp_32b_bytes_reqd_hblank_ovr = REG_FIELD(SDP_CONFIG2, 16, 23),
	.cfg_sdp_128b_bytes_reqd_vblank_ovr = REG_FIELD(SDP_CONFIG3, 0, 7),
	.cfg_sdp_128b_bytes_reqd_hblank_ovr = REG_FIELD(SDP_CONFIG3, 16, 23),
	.cfg_tps_sel = REG_FIELD(PHYIF_CTRL, 0, 3),
	.cfg_phyrate = REG_FIELD(PHYIF_CTRL, 4, 5),
	.cfg_phy_lanes = REG_FIELD(PHYIF_CTRL, 6, 7),
	.cfg_xmit_enable = REG_FIELD(PHYIF_CTRL, 8, 11),
	.cfg_phy_busy = REG_FIELD(PHYIF_CTRL, 12, 15),
	.cfg_ssc_dis = REG_FIELD(PHYIF_CTRL, 16, 16),
	.cfg_phy_powerdown = REG_FIELD(PHYIF_CTRL, 17, 20),
	.cfg_phy_width = REG_FIELD(PHYIF_CTRL, 25, 25),
	.cfg_edp_phy_rate = REG_FIELD(PHYIF_CTRL, 26, 28),
	.cfg_lane0_tx_preemp = REG_FIELD(PHY_TX_EQ, 0, 1),
	.cfg_lane0_tx_vswing = REG_FIELD(PHY_TX_EQ, 2, 3),
	.cfg_lane1_tx_preemp = REG_FIELD(PHY_TX_EQ, 6, 7),
	.cfg_lane1_tx_vswing = REG_FIELD(PHY_TX_EQ, 8, 9),
	.cfg_lane2_tx_preemp = REG_FIELD(PHY_TX_EQ, 12, 13),
	.cfg_lane2_tx_vswing = REG_FIELD(PHY_TX_EQ, 14, 15),
	.cfg_lane3_tx_preemp = REG_FIELD(PHY_TX_EQ, 18, 19),
	.cfg_lane3_tx_vswing = REG_FIELD(PHY_TX_EQ, 20, 21),
	.cfg_custom80b_0 = REG_FIELD(CUSTOMPAT0, 0, 29),
	.cfg_custom80b_1 = REG_FIELD(CUSTOMPAT1, 0, 29),
	.cfg_custom80b_2 = REG_FIELD(CUSTOMPAT2, 0, 19),
	.cfg_num_sr_zeros = REG_FIELD(HBR2_COMPLIANCE_SCRAMBLER_RESET, 0, 15),
	.cfg_aux_len_req = REG_FIELD(AUX_CMD, 0, 3),
	.cfg_i2c_addr_only = REG_FIELD(AUX_CMD, 4, 4),
	.cfg_aux_addr = REG_FIELD(AUX_CMD, 8, 27),
	.cfg_aux_cmd_type = REG_FIELD(AUX_CMD, 28, 31),
	.cfg_aux_status = REG_FIELD(AUX_STATUS, 0, 7),
	.cfg_aux_m = REG_FIELD(AUX_STATUS, 8, 15),
	.cfg_aux_reply_received = REG_FIELD(AUX_STATUS, 16, 16),
	.cfg_aux_timeout = REG_FIELD(AUX_STATUS, 17, 17),
	.cfg_aux_reply_err = REG_FIELD(AUX_STATUS, 18, 18),
	.cfg_aux_bytes_read = REG_FIELD(AUX_STATUS, 19, 23),
	.cfg_sink_disconnect_while_active = REG_FIELD(AUX_STATUS, 24, 24),
	.cfg_aux_reply_err_code = REG_FIELD(AUX_STATUS, 25, 27),
	.cfg_aux_state = REG_FIELD(AUX_STATUS, 28, 31),
	.cfg_aux_data0 = REG_FIELD(AUX_DATA0, 0, 31),
	.cfg_aux_data1 = REG_FIELD(AUX_DATA1, 0, 31),
	.cfg_aux_data2 = REG_FIELD(AUX_DATA2, 0, 31),
	.cfg_aux_data3 = REG_FIELD(AUX_DATA3, 0, 31),
	.cfg_aux_250us_cnt_limit = REG_FIELD(AUX_250US_CNT_LIMIT, 0, 16),
	.cfg_aux_2000us_cnt_limit = REG_FIELD(AUX_2000US_CNT_LIMIT, 0, 16),
	.cfg_aux_100000us_cnt_limit = REG_FIELD(AUX_100000US_CNT_LIMIT, 0, 16),
	.cfg_typec_disable_ack = REG_FIELD(TYPEC_CTRL, 0, 0),
	.cfg_typec_disable_status = REG_FIELD(TYPEC_CTRL, 1, 1),
	.cfg_typec_interrupt_status = REG_FIELD(TYPEC_CTRL, 2, 2),
	.cfg_tx0_in_generic_bus = REG_FIELD(COMBO_PHY_CTRL1, 0, 4),
	.cfg_tx0_hp_prot_en = REG_FIELD(COMBO_PHY_CTRL1, 5, 5),
	.cfg_tx0_bypass_eq_calc = REG_FIELD(COMBO_PHY_CTRL1, 6, 6),
	.cfg_tx1_in_generic_bus = REG_FIELD(COMBO_PHY_CTRL1, 8, 12),
	.cfg_tx1_hp_prot_en = REG_FIELD(COMBO_PHY_CTRL1, 13, 13),
	.cfg_tx1_bypass_eq_calc = REG_FIELD(COMBO_PHY_CTRL1, 14, 14),
	.cfg_tx2_in_generic_bus = REG_FIELD(COMBO_PHY_CTRL1, 16, 20),
	.cfg_tx2_hp_prot_en = REG_FIELD(COMBO_PHY_CTRL1, 21, 21),
	.cfg_tx2_bypass_eq_calc = REG_FIELD(COMBO_PHY_CTRL1, 22, 22),
	.cfg_tx3_in_generic_bus = REG_FIELD(COMBO_PHY_CTRL1, 24, 28),
	.cfg_tx3_hp_prot_en = REG_FIELD(COMBO_PHY_CTRL1, 29, 29),
	.cfg_tx3_bypass_eq_calc = REG_FIELD(COMBO_PHY_CTRL1, 30, 30),
	.cfg_tx0_out_generic_bus = REG_FIELD(COMBO_PHY_STATUS1, 0, 4),
	.cfg_tx1_out_generic_bus = REG_FIELD(COMBO_PHY_STATUS1, 8, 12),
	.cfg_tx2_out_generic_bus = REG_FIELD(COMBO_PHY_STATUS1, 16, 20),
	.cfg_tx3_out_generic_bus = REG_FIELD(COMBO_PHY_STATUS1, 24, 28),
	.cfg_combo_phy_ovr = REG_FIELD(COMBO_PHY_OVR, 0, 0),
	.cfg_combo_phy_ovr_mpll_multiplier = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL0, 0, 11),
	.cfg_combo_phy_ovr_mpll_div_multiplier = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL0, 12, 19),
	.cfg_combo_phy_ovr_mpll_tx_clk_div = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL0, 20, 22),
	.cfg_combo_phy_ovr_mpll_ssc_freq_cnt_init = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL1, 0, 11),
	.cfg_combo_phy_ovr_mpll_ssc_freq_cnt_peak = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL1, 12, 19),
	.cfg_combo_phy_ovr_mpll_ssc_freq_cnt_ovrd_en = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL1, 20, 20),
	.cfg_combo_phy_ovr_mpll_div_clk_en = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL2, 0, 0),
	.cfg_combo_phy_ovr_mpll_word_div2_en = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL2, 4, 4),
	.cfg_combo_phy_ovr_mpll_init_cal_disable = REG_FIELD(COMBO_PHY_OVR_MPLL_CTRL2, 8, 8),
	.cfg_combo_phy_ovr_mpll_pmix_en = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL0, 0, 0),
	.cfg_combo_phy_ovr_mpll_v2i = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL0, 4, 5),
	.cfg_combo_phy_ovr_mpll_cp_int = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL0, 8, 14),
	.cfg_combo_phy_ovr_mpll_cp_prop = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL0, 16, 22),
	.cfg_combo_phy_ovr_mpll_ssc_up_spread = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL0, 24, 24),
	.cfg_combo_phy_ovr_mpll_ssc_peak = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL1, 0, 19),
	.cfg_combo_phy_ovr_mpll_ssc_stepsize = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL2, 0, 20),
	.cfg_combo_phy_ovr_mpll_fracn_cfg_update_en = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL3, 0, 0),
	.cfg_combo_phy_ovr_mpll_fracn_en = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL3, 4, 4),
	.cfg_combo_phy_ovr_mpll_fracn_den = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL3, 8, 23),
	.cfg_combo_phy_ovr_mpll_fracn_quot = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL4, 0, 15),
	.cfg_combo_phy_ovr_mpll_fracn_rem = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL4, 16, 31),
	.cfg_combo_phy_ovr_mpll_freq_vco = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL5, 0, 1),
	.cfg_combo_phy_ovr_ref_clk_mpll_div = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL5, 4, 6),
	.cfg_combo_phy_ovr_mpll_div5_clk_en = REG_FIELD(COMBO_PHY_GEN2_OVR_MPLL_CTRL5, 8, 8),
	.cfg_combo_phy_ovr_tx0_term_ctrl = REG_FIELD(COMBO_PHY_OVR_TERM_CTRL, 0, 2),
	.cfg_combo_phy_ovr_tx1_term_ctrl = REG_FIELD(COMBO_PHY_OVR_TERM_CTRL, 4, 6),
	.cfg_combo_phy_ovr_tx2_term_ctrl = REG_FIELD(COMBO_PHY_OVR_TERM_CTRL, 8, 10),
	.cfg_combo_phy_ovr_tx3_term_ctrl = REG_FIELD(COMBO_PHY_OVR_TERM_CTRL, 12, 14),
	.cfg_combo_phy_ovr_tx_eq_g1 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G1_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g1 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G1_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g1 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G1_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g1 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G1_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx_eq_g2 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G2_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g2 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G2_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g2 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G2_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g2 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G2_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx_eq_g3 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G3_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g3 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G3_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g3 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G3_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g3 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G3_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx_eq_g4 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G4_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g4 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G4_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g4 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G4_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g4 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G4_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx_eq_g5 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G5_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g5 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G5_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g5 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G5_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g5 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G5_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx_eq_g6 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G6_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g6 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G6_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g6 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G6_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g6 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G6_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx_eq_g7 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G7_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g7 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G7_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g7 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G7_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g7 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G7_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx_eq_g8 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G8_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx_eq_main_g8 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G8_CTRL, 8, 13),
	.cfg_combo_phy_ovr_tx_eq_post_g8 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G8_CTRL, 16, 21),
	.cfg_combo_phy_ovr_tx_eq_pre_g8 = REG_FIELD(COMBO_PHY_OVR_TX_EQ_G8_CTRL, 24, 29),
	.cfg_combo_phy_ovr_tx0_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE0_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx0_vboost_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE0_CTRL, 4, 4),
	.cfg_combo_phy_ovr_tx0_iboost_lvl = REG_FIELD(COMBO_PHY_OVR_TX_LANE0_CTRL, 8, 11),
	.cfg_combo_phy_ovr_tx0_clk_rdy = REG_FIELD(COMBO_PHY_OVR_TX_LANE0_CTRL, 12, 12),
	.cfg_combo_phy_ovr_tx0_invert = REG_FIELD(COMBO_PHY_OVR_TX_LANE0_CTRL, 16, 16),
	.cfg_combo_phy_ovr_tx1_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE1_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx1_vboost_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE1_CTRL, 4, 4),
	.cfg_combo_phy_ovr_tx1_iboost_lvl = REG_FIELD(COMBO_PHY_OVR_TX_LANE1_CTRL, 8, 11),
	.cfg_combo_phy_ovr_tx1_clk_rdy = REG_FIELD(COMBO_PHY_OVR_TX_LANE1_CTRL, 12, 12),
	.cfg_combo_phy_ovr_tx1_invert = REG_FIELD(COMBO_PHY_OVR_TX_LANE1_CTRL, 16, 16),
	.cfg_combo_phy_ovr_tx2_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE2_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx2_vboost_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE2_CTRL, 4, 4),
	.cfg_combo_phy_ovr_tx2_iboost_lvl = REG_FIELD(COMBO_PHY_OVR_TX_LANE2_CTRL, 8, 11),
	.cfg_combo_phy_ovr_tx2_clk_rdy = REG_FIELD(COMBO_PHY_OVR_TX_LANE2_CTRL, 12, 12),
	.cfg_combo_phy_ovr_tx2_invert = REG_FIELD(COMBO_PHY_OVR_TX_LANE2_CTRL, 16, 16),
	.cfg_combo_phy_ovr_tx3_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE3_CTRL, 0, 0),
	.cfg_combo_phy_ovr_tx3_vboost_en = REG_FIELD(COMBO_PHY_OVR_TX_LANE3_CTRL, 4, 4),
	.cfg_combo_phy_ovr_tx3_iboost_lvl = REG_FIELD(COMBO_PHY_OVR_TX_LANE3_CTRL, 8, 11),
	.cfg_combo_phy_ovr_tx3_clk_rdy = REG_FIELD(COMBO_PHY_OVR_TX_LANE3_CTRL, 12, 12),
	.cfg_combo_phy_ovr_tx3_invert = REG_FIELD(COMBO_PHY_OVR_TX_LANE3_CTRL, 16, 16),
	.cfg_hpd_event = REG_FIELD(GENERAL_INTERRUPT, 0, 0),
	.cfg_aux_reply_event = REG_FIELD(GENERAL_INTERRUPT, 1, 1),
	.cfg_hdcp_event = REG_FIELD(GENERAL_INTERRUPT, 2, 2),
	.cfg_aux_cmd_invalid = REG_FIELD(GENERAL_INTERRUPT, 3, 3),
	.cfg_sdp_event_stream0 = REG_FIELD(GENERAL_INTERRUPT, 4, 4),
	.cfg_audio_fifo_overflow_stream0 = REG_FIELD(GENERAL_INTERRUPT, 5, 5),
	.cfg_video_fifo_overflow_stream0 = REG_FIELD(GENERAL_INTERRUPT, 6, 6),
	.cfg_video_fifo_underflow_stream0 = REG_FIELD(GENERAL_INTERRUPT, 8, 8),
	.cfg_sdp_event_stream1 = REG_FIELD(GENERAL_INTERRUPT, 12, 12),
	.cfg_audio_fifo_overflow_stream1 = REG_FIELD(GENERAL_INTERRUPT, 13, 13),
	.cfg_video_fifo_overflow_stream1 = REG_FIELD(GENERAL_INTERRUPT, 14, 14),
	.cfg_video_fifo_underflow_stream1 = REG_FIELD(GENERAL_INTERRUPT, 15, 15),
	.cfg_sdp_event_stream2 = REG_FIELD(GENERAL_INTERRUPT, 18, 18),
	.cfg_audio_fifo_overflow_stream2 = REG_FIELD(GENERAL_INTERRUPT, 19, 19),
	.cfg_video_fifo_overflow_stream2 = REG_FIELD(GENERAL_INTERRUPT, 20, 20),
	.cfg_video_fifo_underflow_stream2 = REG_FIELD(GENERAL_INTERRUPT, 21, 21),
	.cfg_sdp_event_stream3 = REG_FIELD(GENERAL_INTERRUPT, 24, 24),
	.cfg_audio_fifo_overflow_stream3 = REG_FIELD(GENERAL_INTERRUPT, 25, 25),
	.cfg_video_fifo_overflow_stream3 = REG_FIELD(GENERAL_INTERRUPT, 26, 26),
	.cfg_video_fifo_underflow_stream3 = REG_FIELD(GENERAL_INTERRUPT, 27, 27),
	.cfg_dsc_event = REG_FIELD(GENERAL_INTERRUPT, 30, 30),
	.cfg_hpd_event_en = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 0, 0),
	.cfg_aux_reply_event_en = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 1, 1),
	.cfg_hdcp_event_en = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 2, 2),
	.cfg_aux_cmd_invalid_en = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 3, 3),
	.cfg_sdp_event_en_stream0 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 4, 4),
	.cfg_audio_fifo_overflow_en_stream0 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 5, 5),
	.cfg_video_fifo_overflow_en_stream0 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 6, 6),
	.cfg_video_fifo_underflow_en_stream0 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 8, 8),
	.cfg_sdp_event_en_stream1 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 12, 12),
	.cfg_audio_fifo_overflow_en_stream1 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 13, 13),
	.cfg_video_fifo_overflow_en_stream1 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 14, 14),
	.cfg_video_fifo_underflow_en_stream1 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 15, 15),
	.cfg_sdp_event_en_stream2 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 18, 18),
	.cfg_audio_fifo_overflow_en_stream2 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 19, 19),
	.cfg_video_fifo_overflow_en_stream2 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 20, 20),
	.cfg_video_fifo_underflow_en_stream2 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 21, 21),
	.cfg_sdp_event_en_stream3 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 24, 24),
	.cfg_audio_fifo_overflow_en_stream3 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 25, 25),
	.cfg_video_fifo_overflow_en_stream3 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 26, 26),
	.cfg_video_fifo_underflow_en_stream3 = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 27, 27),
	.cfg_dsc_event_en = REG_FIELD(GENERAL_INTERRUPT_ENABLE, 30, 30),
	.cfg_hpd_irq = REG_FIELD(HPD_STATUS, 0, 0),
	.cfg_hpd_hot_plug = REG_FIELD(HPD_STATUS, 1, 1),
	.cfg_hpd_hot_unplug = REG_FIELD(HPD_STATUS, 2, 2),
	.cfg_hpd_unplug_err = REG_FIELD(HPD_STATUS, 3, 3),
	.cfg_hpd_status = REG_FIELD(HPD_STATUS, 8, 8),
	.cfg_hpd_state = REG_FIELD(HPD_STATUS, 9, 11),
	.cfg_hpd_timer = REG_FIELD(HPD_STATUS, 15, 31),
	.cfg_hpd_irq_en = REG_FIELD(HPD_INTERRUPT_ENABLE, 0, 0),
	.cfg_hpd_plug_en = REG_FIELD(HPD_INTERRUPT_ENABLE, 1, 1),
	.cfg_hpd_unplug_en = REG_FIELD(HPD_INTERRUPT_ENABLE, 2, 2),
	.cfg_hpd_unplug_err_en = REG_FIELD(HPD_INTERRUPT_ENABLE, 3, 3),
	.cfg_enable_hdcp = REG_FIELD(HDCPCFG, 1, 1),
	.cfg_enable_hdcp_13 = REG_FIELD(HDCPCFG, 2, 2),
	.cfg_encryptiondisable = REG_FIELD(HDCPCFG, 3, 3),
	.cfg_hdcp_lock = REG_FIELD(HDCPCFG, 4, 4),
	.cfg_bypencryption = REG_FIELD(HDCPCFG, 5, 5),
	.cfg_cp_irq = REG_FIELD(HDCPCFG, 6, 6),
	.cfg_dpcd12plus = REG_FIELD(HDCPCFG, 7, 7),
	.cfg_hdcpengaged = REG_FIELD(HDCPOBS, 0, 0),
	.cfg_substatea = REG_FIELD(HDCPOBS, 1, 3),
	.cfg_statea = REG_FIELD(HDCPOBS, 4, 7),
	.cfg_stater = REG_FIELD(HDCPOBS, 8, 10),
	.cfg_stateoeg = REG_FIELD(HDCPOBS, 11, 13),
	.cfg_statee = REG_FIELD(HDCPOBS, 14, 16),
	.cfg_hdcp_capable = REG_FIELD(HDCPOBS, 17, 17),
	.cfg_repeater = REG_FIELD(HDCPOBS, 18, 18),
	.cfg_hdcp13_bstatus = REG_FIELD(HDCPOBS, 19, 22),
	.cfg_hdcp2_booted = REG_FIELD(HDCPOBS, 23, 23),
	.cfg_hdcp2_state = REG_FIELD(HDCPOBS, 24, 26),
	.cfg_hdcp2_sink_cap_check_complete = REG_FIELD(HDCPOBS, 27, 27),
	.cfg_hdcp2_capable_sink = REG_FIELD(HDCPOBS, 28, 28),
	.cfg_hdcp2_authentication_success = REG_FIELD(HDCPOBS, 29, 29),
	.cfg_hdcp2_authentication_failed = REG_FIELD(HDCPOBS, 30, 30),
	.cfg_hdcp2_re_authentication_req = REG_FIELD(HDCPOBS, 31, 31),
	.cfg_ksvaccessint_clr = REG_FIELD(HDCPAPIINTCLR, 0, 0), /* ksvaccessint */
	.cfg_auxrespdefer7times_clr = REG_FIELD(HDCPAPIINTCLR, 2, 2), /* auxrespdefer7times */
	.cfg_auxresptimeout_clr = REG_FIELD(HDCPAPIINTCLR, 3, 3), /* auxresptimeout */
	.cfg_auxrespnack7times_clr = REG_FIELD(HDCPAPIINTCLR, 4, 4), /* auxrespnack7times */
	.cfg_ksvsha1calcdoneint_clr = REG_FIELD(HDCPAPIINTCLR, 5, 5), /* ksvsha1calcdoneint */
	.cfg_hdcp_failed_clr = REG_FIELD(HDCPAPIINTCLR, 6, 6), /* hdcp_failed */
	.cfg_hdcp_engaged_clr = REG_FIELD(HDCPAPIINTCLR, 7, 7), /* hdcp_engaged */
	.cfg_hdcp2_gpioint_clr = REG_FIELD(HDCPAPIINTCLR, 8, 8), /* hdcp2_gpioint */
	.cfg_ksvaccessint_stat = REG_FIELD(HDCPAPIINTSTAT, 0, 0), /* ksvaccessint */
	.cfg_auxrespdefer7times_stat = REG_FIELD(HDCPAPIINTSTAT, 2, 2), /* auxrespdefer7times */
	.cfg_auxresptimeout_stat = REG_FIELD(HDCPAPIINTSTAT, 3, 3), /* auxresptimeout */
	.cfg_auxrespnack7times_stat = REG_FIELD(HDCPAPIINTSTAT, 4, 4), /* auxrespnack7times */
	.cfg_ksvsha1calcdoneint_stat = REG_FIELD(HDCPAPIINTSTAT, 5, 5), /* ksvsha1calcdoneint */
	.cfg_hdcp_failed_stat = REG_FIELD(HDCPAPIINTSTAT, 6, 6), /* hdcp_failed */
	.cfg_hdcp_engaged_stat = REG_FIELD(HDCPAPIINTSTAT, 7, 7), /* hdcp_engaged */
	.cfg_hdcp2_gpioint_stat = REG_FIELD(HDCPAPIINTSTAT, 8, 8), /* hdcp2_gpioint */
	.cfg_ksvaccessint_msk = REG_FIELD(HDCPAPIINTMSK, 0, 0), /* ksvaccessint */
	.cfg_auxrespdefer7times_msk = REG_FIELD(HDCPAPIINTMSK, 2, 2), /* auxrespdefer7times */
	.cfg_auxresptimeout_msk = REG_FIELD(HDCPAPIINTMSK, 3, 3), /* auxresptimeout */
	.cfg_auxrespnack7times_msk = REG_FIELD(HDCPAPIINTMSK, 4, 4), /* auxrespnack7times */
	.cfg_ksvsha1calcdoneint_msk = REG_FIELD(HDCPAPIINTMSK, 5, 5), /* ksvsha1calcdoneint */
	.cfg_hdcp_failed_msk = REG_FIELD(HDCPAPIINTMSK, 6, 6), /* hdcp_failed */
	.cfg_hdcp_engaged_msk = REG_FIELD(HDCPAPIINTMSK, 7, 7), /* hdcp_engaged */
	.cfg_hdcp2_gpioint_msk = REG_FIELD(HDCPAPIINTMSK, 8, 8), /* hdcp2_gpioint */
	.cfg_ksvmemrequest = REG_FIELD(HDCPKSVMEMCTRL, 0, 0),
	.cfg_ksvmemaccess = REG_FIELD(HDCPKSVMEMCTRL, 1, 1),
	.cfg_ksvlistprocessupd = REG_FIELD(HDCPKSVMEMCTRL, 2, 2),
	.cfg_ksvsha1swstatus = REG_FIELD(HDCPKSVMEMCTRL, 3, 3),
	.cfg_ksvsha1status = REG_FIELD(HDCPKSVMEMCTRL, 4, 4),
	.cfg_hdcpreg_bksv0 = REG_FIELD(HDCPREG_BKSV0, 0, 31),
	.cfg_hdcpreg_bksv1 = REG_FIELD(HDCPREG_BKSV1, 0, 7),
	.cfg_oanbypass = REG_FIELD(HDCPREG_ANCONF, 0, 0),
	.cfg_hdcpreg_an0 = REG_FIELD(HDCPREG_AN0, 0, 31),
	.cfg_hdcpreg_an1 = REG_FIELD(HDCPREG_AN1, 0, 31),
	.cfg_odpk_decrypt_enable = REG_FIELD(HDCPREG_RMLCTL, 0, 0),
	.cfg_idpk_data_index = REG_FIELD(HDCPREG_RMLSTS, 0, 5),
	.cfg_idpk_wr_ok_sts = REG_FIELD(HDCPREG_RMLSTS, 6, 6),
	.cfg_hdcpreg_seed = REG_FIELD(HDCPREG_SEED, 0, 15),
	.cfg_dpk_data_0 = REG_FIELD(HDCPREG_DPK0, 0, 31), /* dpk_data */
	.cfg_dpk_data_1 = REG_FIELD(HDCPREG_DPK1, 0, 23), /* dpk_data */
	.cfg_hdcp2gpiooutsts = REG_FIELD(HDCP2GPIOSTS, 0, 19),
	.cfg_hdcp2gpiooutchngsts = REG_FIELD(HDCP2GPIOCHNGSTS, 0, 19),
	.cfg_dpk_crc = REG_FIELD(HDCPREG_DPK_CRC, 0, 31),
	.cfg_vg_swrst = REG_FIELD(VG_SWRST, 0, 0),
	.cfg_odepolarity = REG_FIELD(VG_CONFIG1, 0, 0),
	.cfg_ohsyncpolarity = REG_FIELD(VG_CONFIG1, 1, 1),
	.cfg_ovsyncpolarity = REG_FIELD(VG_CONFIG1, 2, 2),
	.cfg_oip = REG_FIELD(VG_CONFIG1, 3, 3),
	.cfg_ocolorincrement = REG_FIELD(VG_CONFIG1, 4, 4),
	.cfg_ovblankoscillation = REG_FIELD(VG_CONFIG1, 5, 5),
	.cfg_ycc_422_mapping = REG_FIELD(VG_CONFIG1, 6, 6),
	.cfg_ycc_pattern_generation = REG_FIELD(VG_CONFIG1, 7, 7),
	.cfg_pixel_repetition = REG_FIELD(VG_CONFIG1, 8, 11),
	.cfg_bits_per_comp = REG_FIELD(VG_CONFIG1, 12, 14),
	.cfg_ycc_420_mapping = REG_FIELD(VG_CONFIG1, 15, 15),
	.cfg_internal_external_gen = REG_FIELD(VG_CONFIG1, 16, 16),
	.cfg_pattern_mode = REG_FIELD(VG_CONFIG1, 17, 18),
	.cfg_hactive_vg_config2 = REG_FIELD(VG_CONFIG2, 0, 15), /* hactive */
	.cfg_hblank_vg_config2 = REG_FIELD(VG_CONFIG2, 16, 31), /* hblank */
	.cfg_h_front_porch = REG_FIELD(VG_CONFIG3, 0, 15),
	.cfg_h_sync_width_vg_config3 = REG_FIELD(VG_CONFIG3, 16, 31), /* h_sync_width */
	.cfg_vactive_vg_config4 = REG_FIELD(VG_CONFIG4, 0, 15), /* vactive */
	.cfg_vblank_vg_config4 = REG_FIELD(VG_CONFIG4, 16, 31), /* vblank */
	.cfg_v_front_porch = REG_FIELD(VG_CONFIG5, 0, 15),
	.cfg_v_sync_width_vg_config5 = REG_FIELD(VG_CONFIG5, 16, 31), /* v_sync_width */
	.cfg_td_structure = REG_FIELD(VG_CONFIG6, 0, 3),
	.cfg_td_enable = REG_FIELD(VG_CONFIG6, 4, 4),
	.cfg_td_frameseq = REG_FIELD(VG_CONFIG6, 5, 5),
	.cfg_ipi_enable = REG_FIELD(VG_CONFIG6, 6, 6),
	.cfg_ipi_select = REG_FIELD(VG_CONFIG6, 7, 11),
	.cfg_ram_addr_start = REG_FIELD(VG_RAM_ADDR, 0, 12),
	.cfg_start_write_ram = REG_FIELD(VG_WRT_RAM_CTRL, 0, 0),
	.cfg_write_ram_data = REG_FIELD(VG_WRT_RAM_DATA, 0, 7),
	.cfg_ram_stop_addr = REG_FIELD(VG_WRT_RAM_STOP_ADDR, 0, 12),
	.cfg_vg_cb_width = REG_FIELD(VG_CB_PATTERN_CONFIG, 0, 9),
	.cfg_vg_cb_height = REG_FIELD(VG_CB_PATTERN_CONFIG, 16, 24),
	.cfg_vg_cb_colora_lsb = REG_FIELD(VG_CB_COLOR_A_1, 0, 31),
	.cfg_vg_cb_color_a_msb = REG_FIELD(VG_CB_COLOR_A_2, 0, 15),
	.cfg_vg_cb_color_b_lsb = REG_FIELD(VG_CB_COLOR_B_1, 0, 31),
	.cfg_vg_cb_color_b_msb = REG_FIELD(VG_CB_COLOR_B_2, 0, 15),
	.cfg_ag_swrst = REG_FIELD(AG_SWRSTZ, 0, 0),
	.cfg_hbren = REG_FIELD(AG_CONFIG1, 2, 2),
	.cfg_audiosource_clockmultiplier = REG_FIELD(AG_CONFIG1, 3, 4),
	.cfg_i2s_wordwidth = REG_FIELD(AG_CONFIG1, 6, 9),
	.cfg_audio_source = REG_FIELD(AG_CONFIG1, 10, 10),
	.cfg_nlpcm_en = REG_FIELD(AG_CONFIG1, 11, 11),
	.cfg_spdiftxdata = REG_FIELD(AG_CONFIG1, 13, 13),
	.cfg_audio_use_lut = REG_FIELD(AG_CONFIG1, 14, 14),
	.cfg_audio_use_counter = REG_FIELD(AG_CONFIG1, 15, 15),
	.cfg_audio_counter_offset = REG_FIELD(AG_CONFIG1, 16, 23),
	.cfg_incleft = REG_FIELD(AG_CONFIG2, 0, 15),
	.cfg_incright = REG_FIELD(AG_CONFIG2, 16, 31),
	.cfg_iec_copyright = REG_FIELD(AG_CONFIG3, 0, 0),
	.cfg_iec_cgmsa = REG_FIELD(AG_CONFIG3, 1, 2),
	.cfg_iec_nlpcm = REG_FIELD(AG_CONFIG3, 3, 3),
	.cfg_iec_categorycode = REG_FIELD(AG_CONFIG3, 8, 15),
	.cfg_iec_sourcenumber = REG_FIELD(AG_CONFIG3, 16, 19),
	.cfg_iec_pcm_audio_mode = REG_FIELD(AG_CONFIG3, 20, 22),
	.cfg_iec_channelnumcl0_3 = REG_FIELD(AG_CONFIG3, 24, 27), /* iec_channelnumcl0 */
	.cfg_iec_channelnumcr0_3 = REG_FIELD(AG_CONFIG3, 28, 31), /* iec_channelnumcr0 */
	.cfg_iec_samp_freq = REG_FIELD(AG_CONFIG4, 0, 5),
	.cfg_iec_clkaccuracy = REG_FIELD(AG_CONFIG4, 6, 7),
	.cfg_iec_word_length = REG_FIELD(AG_CONFIG4, 8, 11),
	.cfg_iec_origsampfreq = REG_FIELD(AG_CONFIG4, 12, 15),
	.cfg_iec_channelnumcl0_5 = REG_FIELD(AG_CONFIG5, 0, 3), /* iec_channelnumcl0 */
	.cfg_iec_channelnumcr0_5 = REG_FIELD(AG_CONFIG5, 4, 7), /* iec_channelnumcr0 */
	.cfg_iec_channelnumcl1 = REG_FIELD(AG_CONFIG5, 8, 11),
	.cfg_iec_channelnumcr1 = REG_FIELD(AG_CONFIG5, 12, 15),
	.cfg_iec_channelnumcl2 = REG_FIELD(AG_CONFIG5, 16, 19),
	.cfg_iec_channelnumcr2 = REG_FIELD(AG_CONFIG5, 20, 23),
	.cfg_iec_channelnumcl2a = REG_FIELD(AG_CONFIG5, 24, 27),
	.cfg_iec_channelnumcr2a = REG_FIELD(AG_CONFIG5, 28, 31),
	.cfg_userdata_cl0 = REG_FIELD(AG_CONFIG6, 0, 0),
	.cfg_userdata_cr0 = REG_FIELD(AG_CONFIG6, 1, 1),
	.cfg_userdata_cl1 = REG_FIELD(AG_CONFIG6, 2, 2),
	.cfg_userdata_cr1 = REG_FIELD(AG_CONFIG6, 3, 3),
	.cfg_userdata_cl2 = REG_FIELD(AG_CONFIG6, 4, 4),
	.cfg_userdata_cr2 = REG_FIELD(AG_CONFIG6, 5, 5),
	.cfg_userdata_cl3 = REG_FIELD(AG_CONFIG6, 6, 6),
	.cfg_userdata_cr3 = REG_FIELD(AG_CONFIG6, 7, 7),
	.cfg_validity_bit_cl0 = REG_FIELD(AG_CONFIG6, 8, 8),
	.cfg_validity_bit_cr0 = REG_FIELD(AG_CONFIG6, 9, 9),
	.cfg_validity_bit_cl1 = REG_FIELD(AG_CONFIG6, 10, 10),
	.cfg_validity_bit_cr1 = REG_FIELD(AG_CONFIG6, 11, 11),
	.cfg_validity_bit_cl2 = REG_FIELD(AG_CONFIG6, 12, 12),
	.cfg_validity_bit_cr2 = REG_FIELD(AG_CONFIG6, 13, 13),
	.cfg_validity_bit_cl3 = REG_FIELD(AG_CONFIG6, 14, 14),
	.cfg_validity_bit_cr3 = REG_FIELD(AG_CONFIG6, 15, 15),
};

struct ctrl_regfields {
	struct regmap_field	*field_version_number;
	struct regmap_field	*field_version_type;
	struct regmap_field	*field_vendor_id;
	struct regmap_field	*field_device_id;
	struct regmap_field	*field_hdcp_select;
	struct regmap_field	*field_audio_select;
	struct regmap_field	*field_phy_used;
	struct regmap_field	*field_sdp_reg_bank_size;
	struct regmap_field	*field_fpga_en;
	struct regmap_field	*field_dpk_romless;
	struct regmap_field	*field_dpk_8bit;
	struct regmap_field	*field_sync_depth;
	struct regmap_field	*field_num_streams;
	struct regmap_field	*field_mp_mode;
	struct regmap_field	*field_dsc_en;
	struct regmap_field	*field_edp_en;
	struct regmap_field	*field_fec_en;
	struct regmap_field	*field_gen2_phy;
	struct regmap_field	*field_phy_type;
	struct regmap_field	*field_adsync_en;
	struct regmap_field	*field_psr_ver;
	struct regmap_field	*field_scramble_dis;
	struct regmap_field	*field_enhance_framing_en;
	struct regmap_field	*field_default_fast_link_train_en;
	struct regmap_field	*field_scale_down_mode;
	struct regmap_field	*field_force_hpd;
	struct regmap_field	*field_disable_interleaving;
	struct regmap_field	*field_sel_aux_timeout_32ms;
	struct regmap_field	*field_debug_control;
	struct regmap_field	*field_sr_scale_down;
	struct regmap_field	*field_bs_512_scale_down;
	struct regmap_field	*field_enable_mst_mode;
	struct regmap_field	*field_enable_fec;
	struct regmap_field	*field_enable_edp;
	struct regmap_field	*field_initiate_mst_act_seq;
	struct regmap_field	*field_enhance_framing_with_fec_en;
	struct regmap_field	*field_controller_reset;
	struct regmap_field	*field_phy_soft_reset;
	struct regmap_field	*field_hdcp_module_reset;
	struct regmap_field	*field_audio_sampler_reset;
	struct regmap_field	*field_aux_reset;
	struct regmap_field	*field_video_reset;
	struct regmap_field	*field_audio_sampler_reset_stream1;
	struct regmap_field	*field_audio_sampler_reset_stream2;
	struct regmap_field	*field_audio_sampler_reset_stream3;
	struct regmap_field	*field_aux_cdr_state;
	struct regmap_field	*field_aux_cdr_clock_cycle;
	struct regmap_field	*field_video_stream_enable;
	struct regmap_field	*field_video_mapping_ipi_en;
	struct regmap_field	*field_video_mapping;
	struct regmap_field	*field_pixel_mode_select;
	struct regmap_field	*field_enable_dsc;
	struct regmap_field	*field_encryption_enable;
	struct regmap_field	*field_stream_type;
	struct regmap_field	*field_bcb_data_stuffing_en;
	struct regmap_field	*field_rcr_data_stuffing_en;
	struct regmap_field	*field_gy_data_stuffing_en;
	struct regmap_field	*field_bcb_stuff_data;
	struct regmap_field	*field_rcr_stuff_data;
	struct regmap_field	*field_gy_stuff_data;
	struct regmap_field	*field_vsync_in_polarity;
	struct regmap_field	*field_hsync_in_polarity;
	struct regmap_field	*field_de_in_polarity;
	struct regmap_field	*field_r_v_blank_in_osc;
	struct regmap_field	*field_i_p;
	struct regmap_field	*field_hblank_video_config1; /* hblank */
	struct regmap_field	*field_hactive_video_config1; /* hactive */
	struct regmap_field	*field_vactive_video_config2; /* vactive */
	struct regmap_field	*field_vblank_video_config2; /* vblank */
	struct regmap_field	*field_h_sync_width_video_config3; /* h_sync_width */
	struct regmap_field	*field_v_sync_width_video_config4; /* v_sync_width */
	struct regmap_field	*field_average_bytes_per_tu;
	struct regmap_field	*field_init_threshold;
	struct regmap_field	*field_average_bytes_per_tu_frac;
	struct regmap_field	*field_enable_3d_frame_field_seq;
	struct regmap_field	*field_init_threshold_hi;
	struct regmap_field	*field_hstart;
	struct regmap_field	*field_vstart;
	struct regmap_field	*field_mvid;
	struct regmap_field	*field_misc0;
	struct regmap_field	*field_nvid;
	struct regmap_field	*field_misc1;
	struct regmap_field	*field_hblank_interval;
	struct regmap_field	*field_mvid_cust_en;
	struct regmap_field	*field_mvid_out_clr_mode;
	struct regmap_field	*field_mvid_cust_den;
	struct regmap_field	*field_mvid_cust_mod;
	struct regmap_field	*field_mvid_cust_quo;
	struct regmap_field	*field_audio_inf_select;
	struct regmap_field	*field_audio_data_in_en;
	struct regmap_field	*field_audio_data_width;
	struct regmap_field	*field_hbr_mode_enable;
	struct regmap_field	*field_num_channels;
	struct regmap_field	*field_audio_mute;
	struct regmap_field	*field_audio_packet_id;
	struct regmap_field	*field_audio_timestamp_version_num;
	struct regmap_field	*field_audio_clk_mult_fs;
	struct regmap_field	*field_en_audio_timestamp_sdp_vertical_ctrl; /* en_audio_timestamp_sdp */
	struct regmap_field	*field_en_audio_stream_sdp_vertical_ctrl; /* en_audio_stream_sdp */
	struct regmap_field	*field_en_vertical_sdp_n;
	struct regmap_field	*field_en_128bytes_sdp_1;
	struct regmap_field	*field_disable_ext_sdp;
	struct regmap_field	*field_fixed_priority_arbitration_vertical_ctrl; /* fixed_priority_arbitration */
	struct regmap_field	*field_en_audio_timestamp_sdp_horizontal_ctrl; /* en_audio_timestamp_sdp */
	struct regmap_field	*field_en_audio_stream_sdp_horizontal_ctrl; /* en_audio_stream_sdp */
	struct regmap_field	*field_en_horizontal_sdp_n;
	struct regmap_field	*field_fixed_priority_arbitration_horizontal_ctrl; /* fixed_priority_arbitration */
	struct regmap_field	*field_audio_timestamp_sdp_status;
	struct regmap_field	*field_audio_stream_sdp_status;
	struct regmap_field	*field_sdp_n_tx_status;
	struct regmap_field	*field_manual_mode_sdp;
	struct regmap_field	*field_audio_timestamp_sdp_status_en;
	struct regmap_field	*field_audio_stream_sdp_status_en;
	struct regmap_field	*field_sdp_status_en;
	struct regmap_field	*field_sdp_16b_bytes_reqd_vblank_ovr;
	struct regmap_field	*field_sdp_16b_bytes_reqd_hblank_ovr;
	struct regmap_field	*field_sdp_32b_bytes_reqd_vblank_ovr;
	struct regmap_field	*field_sdp_32b_bytes_reqd_hblank_ovr;
	struct regmap_field	*field_sdp_128b_bytes_reqd_vblank_ovr;
	struct regmap_field	*field_sdp_128b_bytes_reqd_hblank_ovr;
	struct regmap_field	*field_tps_sel;
	struct regmap_field	*field_phyrate;
	struct regmap_field	*field_phy_lanes;
	struct regmap_field	*field_xmit_enable;
	struct regmap_field	*field_phy_busy;
	struct regmap_field	*field_ssc_dis;
	struct regmap_field	*field_phy_powerdown;
	struct regmap_field	*field_phy_width;
	struct regmap_field	*field_edp_phy_rate;
	struct regmap_field	*field_lane0_tx_preemp;
	struct regmap_field	*field_lane0_tx_vswing;
	struct regmap_field	*field_lane1_tx_preemp;
	struct regmap_field	*field_lane1_tx_vswing;
	struct regmap_field	*field_lane2_tx_preemp;
	struct regmap_field	*field_lane2_tx_vswing;
	struct regmap_field	*field_lane3_tx_preemp;
	struct regmap_field	*field_lane3_tx_vswing;
	struct regmap_field	*field_custom80b_0;
	struct regmap_field	*field_custom80b_1;
	struct regmap_field	*field_custom80b_2;
	struct regmap_field	*field_num_sr_zeros;
	struct regmap_field	*field_aux_len_req;
	struct regmap_field	*field_i2c_addr_only;
	struct regmap_field	*field_aux_addr;
	struct regmap_field	*field_aux_cmd_type;
	struct regmap_field	*field_aux_status;
	struct regmap_field	*field_aux_m;
	struct regmap_field	*field_aux_reply_received;
	struct regmap_field	*field_aux_timeout;
	struct regmap_field	*field_aux_reply_err;
	struct regmap_field	*field_aux_bytes_read;
	struct regmap_field	*field_sink_disconnect_while_active;
	struct regmap_field	*field_aux_reply_err_code;
	struct regmap_field	*field_aux_state;
	struct regmap_field	*field_aux_data0;
	struct regmap_field	*field_aux_data1;
	struct regmap_field	*field_aux_data2;
	struct regmap_field	*field_aux_data3;
	struct regmap_field	*field_aux_250us_cnt_limit;
	struct regmap_field	*field_aux_2000us_cnt_limit;
	struct regmap_field	*field_aux_100000us_cnt_limit;
	struct regmap_field	*field_typec_disable_ack;
	struct regmap_field	*field_typec_disable_status;
	struct regmap_field	*field_typec_interrupt_status;
	struct regmap_field	*field_tx0_in_generic_bus;
	struct regmap_field	*field_tx0_hp_prot_en;
	struct regmap_field	*field_tx0_bypass_eq_calc;
	struct regmap_field	*field_tx1_in_generic_bus;
	struct regmap_field	*field_tx1_hp_prot_en;
	struct regmap_field	*field_tx1_bypass_eq_calc;
	struct regmap_field	*field_tx2_in_generic_bus;
	struct regmap_field	*field_tx2_hp_prot_en;
	struct regmap_field	*field_tx2_bypass_eq_calc;
	struct regmap_field	*field_tx3_in_generic_bus;
	struct regmap_field	*field_tx3_hp_prot_en;
	struct regmap_field	*field_tx3_bypass_eq_calc;
	struct regmap_field	*field_tx0_out_generic_bus;
	struct regmap_field	*field_tx1_out_generic_bus;
	struct regmap_field	*field_tx2_out_generic_bus;
	struct regmap_field	*field_tx3_out_generic_bus;
	struct regmap_field	*field_combo_phy_ovr;
	struct regmap_field	*field_combo_phy_ovr_mpll_multiplier;
	struct regmap_field	*field_combo_phy_ovr_mpll_div_multiplier;
	struct regmap_field	*field_combo_phy_ovr_mpll_tx_clk_div;
	struct regmap_field	*field_combo_phy_ovr_mpll_ssc_freq_cnt_init;
	struct regmap_field	*field_combo_phy_ovr_mpll_ssc_freq_cnt_peak;
	struct regmap_field	*field_combo_phy_ovr_mpll_ssc_freq_cnt_ovrd_en;
	struct regmap_field	*field_combo_phy_ovr_mpll_div_clk_en;
	struct regmap_field	*field_combo_phy_ovr_mpll_word_div2_en;
	struct regmap_field	*field_combo_phy_ovr_mpll_init_cal_disable;
	struct regmap_field	*field_combo_phy_ovr_mpll_pmix_en;
	struct regmap_field	*field_combo_phy_ovr_mpll_v2i;
	struct regmap_field	*field_combo_phy_ovr_mpll_cp_int;
	struct regmap_field	*field_combo_phy_ovr_mpll_cp_prop;
	struct regmap_field	*field_combo_phy_ovr_mpll_ssc_up_spread;
	struct regmap_field	*field_combo_phy_ovr_mpll_ssc_peak;
	struct regmap_field	*field_combo_phy_ovr_mpll_ssc_stepsize;
	struct regmap_field	*field_combo_phy_ovr_mpll_fracn_cfg_update_en;
	struct regmap_field	*field_combo_phy_ovr_mpll_fracn_en;
	struct regmap_field	*field_combo_phy_ovr_mpll_fracn_den;
	struct regmap_field	*field_combo_phy_ovr_mpll_fracn_quot;
	struct regmap_field	*field_combo_phy_ovr_mpll_fracn_rem;
	struct regmap_field	*field_combo_phy_ovr_mpll_freq_vco;
	struct regmap_field	*field_combo_phy_ovr_ref_clk_mpll_div;
	struct regmap_field	*field_combo_phy_ovr_mpll_div5_clk_en;
	struct regmap_field	*field_combo_phy_ovr_tx0_term_ctrl;
	struct regmap_field	*field_combo_phy_ovr_tx1_term_ctrl;
	struct regmap_field	*field_combo_phy_ovr_tx2_term_ctrl;
	struct regmap_field	*field_combo_phy_ovr_tx3_term_ctrl;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g1;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g1;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g1;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g1;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g2;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g2;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g2;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g2;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g3;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g3;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g3;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g3;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g4;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g4;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g4;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g4;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g5;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g5;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g5;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g5;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g6;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g6;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g6;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g6;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g7;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g7;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g7;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g7;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_g8;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_main_g8;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_post_g8;
	struct regmap_field	*field_combo_phy_ovr_tx_eq_pre_g8;
	struct regmap_field	*field_combo_phy_ovr_tx0_en;
	struct regmap_field	*field_combo_phy_ovr_tx0_vboost_en;
	struct regmap_field	*field_combo_phy_ovr_tx0_iboost_lvl;
	struct regmap_field	*field_combo_phy_ovr_tx0_clk_rdy;
	struct regmap_field	*field_combo_phy_ovr_tx0_invert;
	struct regmap_field	*field_combo_phy_ovr_tx1_en;
	struct regmap_field	*field_combo_phy_ovr_tx1_vboost_en;
	struct regmap_field	*field_combo_phy_ovr_tx1_iboost_lvl;
	struct regmap_field	*field_combo_phy_ovr_tx1_clk_rdy;
	struct regmap_field	*field_combo_phy_ovr_tx1_invert;
	struct regmap_field	*field_combo_phy_ovr_tx2_en;
	struct regmap_field	*field_combo_phy_ovr_tx2_vboost_en;
	struct regmap_field	*field_combo_phy_ovr_tx2_iboost_lvl;
	struct regmap_field	*field_combo_phy_ovr_tx2_clk_rdy;
	struct regmap_field	*field_combo_phy_ovr_tx2_invert;
	struct regmap_field	*field_combo_phy_ovr_tx3_en;
	struct regmap_field	*field_combo_phy_ovr_tx3_vboost_en;
	struct regmap_field	*field_combo_phy_ovr_tx3_iboost_lvl;
	struct regmap_field	*field_combo_phy_ovr_tx3_clk_rdy;
	struct regmap_field	*field_combo_phy_ovr_tx3_invert;
	struct regmap_field	*field_hpd_event;
	struct regmap_field	*field_aux_reply_event;
	struct regmap_field	*field_hdcp_event;
	struct regmap_field	*field_aux_cmd_invalid;
	struct regmap_field	*field_sdp_event_stream0;
	struct regmap_field	*field_audio_fifo_overflow_stream0;
	struct regmap_field	*field_video_fifo_overflow_stream0;
	struct regmap_field	*field_video_fifo_underflow_stream0;
	struct regmap_field	*field_sdp_event_stream1;
	struct regmap_field	*field_audio_fifo_overflow_stream1;
	struct regmap_field	*field_video_fifo_overflow_stream1;
	struct regmap_field	*field_video_fifo_underflow_stream1;
	struct regmap_field	*field_sdp_event_stream2;
	struct regmap_field	*field_audio_fifo_overflow_stream2;
	struct regmap_field	*field_video_fifo_overflow_stream2;
	struct regmap_field	*field_video_fifo_underflow_stream2;
	struct regmap_field	*field_sdp_event_stream3;
	struct regmap_field	*field_audio_fifo_overflow_stream3;
	struct regmap_field	*field_video_fifo_overflow_stream3;
	struct regmap_field	*field_video_fifo_underflow_stream3;
	struct regmap_field	*field_dsc_event;
	struct regmap_field	*field_hpd_event_en;
	struct regmap_field	*field_aux_reply_event_en;
	struct regmap_field	*field_hdcp_event_en;
	struct regmap_field	*field_aux_cmd_invalid_en;
	struct regmap_field	*field_sdp_event_en_stream0;
	struct regmap_field	*field_audio_fifo_overflow_en_stream0;
	struct regmap_field	*field_video_fifo_overflow_en_stream0;
	struct regmap_field	*field_video_fifo_underflow_en_stream0;
	struct regmap_field	*field_sdp_event_en_stream1;
	struct regmap_field	*field_audio_fifo_overflow_en_stream1;
	struct regmap_field	*field_video_fifo_overflow_en_stream1;
	struct regmap_field	*field_video_fifo_underflow_en_stream1;
	struct regmap_field	*field_sdp_event_en_stream2;
	struct regmap_field	*field_audio_fifo_overflow_en_stream2;
	struct regmap_field	*field_video_fifo_overflow_en_stream2;
	struct regmap_field	*field_video_fifo_underflow_en_stream2;
	struct regmap_field	*field_sdp_event_en_stream3;
	struct regmap_field	*field_audio_fifo_overflow_en_stream3;
	struct regmap_field	*field_video_fifo_overflow_en_stream3;
	struct regmap_field	*field_video_fifo_underflow_en_stream3;
	struct regmap_field	*field_dsc_event_en;
	struct regmap_field	*field_hpd_irq;
	struct regmap_field	*field_hpd_hot_plug;
	struct regmap_field	*field_hpd_hot_unplug;
	struct regmap_field	*field_hpd_unplug_err;
	struct regmap_field	*field_hpd_status;
	struct regmap_field	*field_hpd_state;
	struct regmap_field	*field_hpd_timer;
	struct regmap_field	*field_hpd_irq_en;
	struct regmap_field	*field_hpd_plug_en;
	struct regmap_field	*field_hpd_unplug_en;
	struct regmap_field	*field_hpd_unplug_err_en;
	struct regmap_field	*field_enable_hdcp;
	struct regmap_field	*field_enable_hdcp_13;
	struct regmap_field	*field_encryptiondisable;
	struct regmap_field	*field_hdcp_lock;
	struct regmap_field	*field_bypencryption;
	struct regmap_field	*field_cp_irq;
	struct regmap_field	*field_dpcd12plus;
	struct regmap_field	*field_hdcpengaged;
	struct regmap_field	*field_substatea;
	struct regmap_field	*field_statea;
	struct regmap_field	*field_stater;
	struct regmap_field	*field_stateoeg;
	struct regmap_field	*field_statee;
	struct regmap_field	*field_hdcp_capable;
	struct regmap_field	*field_repeater;
	struct regmap_field	*field_hdcp13_bstatus;
	struct regmap_field	*field_hdcp2_booted;
	struct regmap_field	*field_hdcp2_state;
	struct regmap_field	*field_hdcp2_sink_cap_check_complete;
	struct regmap_field	*field_hdcp2_capable_sink;
	struct regmap_field	*field_hdcp2_authentication_success;
	struct regmap_field	*field_hdcp2_authentication_failed;
	struct regmap_field	*field_hdcp2_re_authentication_req;
	struct regmap_field	*field_ksvaccessint_clr; /* ksvaccessint */
	struct regmap_field	*field_auxrespdefer7times_clr; /* auxrespdefer7times */
	struct regmap_field	*field_auxresptimeout_clr; /* auxresptimeout */
	struct regmap_field	*field_auxrespnack7times_clr; /* auxrespnack7times */
	struct regmap_field	*field_ksvsha1calcdoneint_clr; /* ksvsha1calcdoneint */
	struct regmap_field	*field_hdcp_failed_clr; /* hdcp_failed */
	struct regmap_field	*field_hdcp_engaged_clr; /* hdcp_engaged */
	struct regmap_field	*field_hdcp2_gpioint_clr; /* hdcp2_gpioint */
	struct regmap_field	*field_ksvaccessint_stat; /* ksvaccessint */
	struct regmap_field	*field_auxrespdefer7times_stat; /* auxrespdefer7times */
	struct regmap_field	*field_auxresptimeout_stat; /* auxresptimeout */
	struct regmap_field	*field_auxrespnack7times_stat; /* auxrespnack7times */
	struct regmap_field	*field_ksvsha1calcdoneint_stat; /* ksvsha1calcdoneint */
	struct regmap_field	*field_hdcp_failed_stat; /* hdcp_failed */
	struct regmap_field	*field_hdcp_engaged_stat; /* hdcp_engaged */
	struct regmap_field	*field_hdcp2_gpioint_stat; /* hdcp2_gpioint */
	struct regmap_field	*field_ksvaccessint_msk; /* ksvaccessint */
	struct regmap_field	*field_auxrespdefer7times_msk; /* auxrespdefer7times */
	struct regmap_field	*field_auxresptimeout_msk; /* auxresptimeout */
	struct regmap_field	*field_auxrespnack7times_msk; /* auxrespnack7times */
	struct regmap_field	*field_ksvsha1calcdoneint_msk; /* ksvsha1calcdoneint */
	struct regmap_field	*field_hdcp_failed_msk; /* hdcp_failed */
	struct regmap_field	*field_hdcp_engaged_msk; /* hdcp_engaged */
	struct regmap_field	*field_hdcp2_gpioint_msk; /* hdcp2_gpioint */
	struct regmap_field	*field_ksvmemrequest;
	struct regmap_field	*field_ksvmemaccess;
	struct regmap_field	*field_ksvlistprocessupd;
	struct regmap_field	*field_ksvsha1swstatus;
	struct regmap_field	*field_ksvsha1status;
	struct regmap_field	*field_hdcpreg_bksv0;
	struct regmap_field	*field_hdcpreg_bksv1;
	struct regmap_field	*field_oanbypass;
	struct regmap_field	*field_hdcpreg_an0;
	struct regmap_field	*field_hdcpreg_an1;
	struct regmap_field	*field_odpk_decrypt_enable;
	struct regmap_field	*field_idpk_data_index;
	struct regmap_field	*field_idpk_wr_ok_sts;
	struct regmap_field	*field_hdcpreg_seed;
	struct regmap_field	*field_dpk_data_0; /* dpk_data */
	struct regmap_field	*field_dpk_data_1; /* dpk_data */
	struct regmap_field	*field_hdcp2gpiooutsts;
	struct regmap_field	*field_hdcp2gpiooutchngsts;
	struct regmap_field	*field_dpk_crc;
	struct regmap_field	*field_vg_swrst;
	struct regmap_field	*field_odepolarity;
	struct regmap_field	*field_ohsyncpolarity;
	struct regmap_field	*field_ovsyncpolarity;
	struct regmap_field	*field_oip;
	struct regmap_field	*field_ocolorincrement;
	struct regmap_field	*field_ovblankoscillation;
	struct regmap_field	*field_ycc_422_mapping;
	struct regmap_field	*field_ycc_pattern_generation;
	struct regmap_field	*field_pixel_repetition;
	struct regmap_field	*field_bits_per_comp;
	struct regmap_field	*field_ycc_420_mapping;
	struct regmap_field	*field_internal_external_gen;
	struct regmap_field	*field_pattern_mode;
	struct regmap_field	*field_hactive_vg_config2; /* hactive */
	struct regmap_field	*field_hblank_vg_config2; /* hblank */
	struct regmap_field	*field_h_front_porch;
	struct regmap_field	*field_h_sync_width_vg_config3; /* h_sync_width */
	struct regmap_field	*field_vactive_vg_config4; /* vactive */
	struct regmap_field	*field_vblank_vg_config4; /* vblank */
	struct regmap_field	*field_v_front_porch;
	struct regmap_field	*field_v_sync_width_vg_config5; /* v_sync_width */
	struct regmap_field	*field_td_structure;
	struct regmap_field	*field_td_enable;
	struct regmap_field	*field_td_frameseq;
	struct regmap_field	*field_ipi_enable;
	struct regmap_field	*field_ipi_select;
	struct regmap_field	*field_ram_addr_start;
	struct regmap_field	*field_start_write_ram;
	struct regmap_field	*field_write_ram_data;
	struct regmap_field	*field_ram_stop_addr;
	struct regmap_field	*field_vg_cb_width;
	struct regmap_field	*field_vg_cb_height;
	struct regmap_field	*field_vg_cb_colora_lsb;
	struct regmap_field	*field_vg_cb_color_a_msb;
	struct regmap_field	*field_vg_cb_color_b_lsb;
	struct regmap_field	*field_vg_cb_color_b_msb;
	struct regmap_field	*field_ag_swrst;
	struct regmap_field	*field_hbren;
	struct regmap_field	*field_audiosource_clockmultiplier;
	struct regmap_field	*field_i2s_wordwidth;
	struct regmap_field	*field_audio_source;
	struct regmap_field	*field_nlpcm_en;
	struct regmap_field	*field_spdiftxdata;
	struct regmap_field	*field_audio_use_lut;
	struct regmap_field	*field_audio_use_counter;
	struct regmap_field	*field_audio_counter_offset;
	struct regmap_field	*field_incleft;
	struct regmap_field	*field_incright;
	struct regmap_field	*field_iec_copyright;
	struct regmap_field	*field_iec_cgmsa;
	struct regmap_field	*field_iec_nlpcm;
	struct regmap_field	*field_iec_categorycode;
	struct regmap_field	*field_iec_sourcenumber;
	struct regmap_field	*field_iec_pcm_audio_mode;
	struct regmap_field	*field_iec_channelnumcl0_3; /* iec_channelnumcl0 */
	struct regmap_field	*field_iec_channelnumcr0_3; /* iec_channelnumcr0 */
	struct regmap_field	*field_iec_samp_freq;
	struct regmap_field	*field_iec_clkaccuracy;
	struct regmap_field	*field_iec_word_length;
	struct regmap_field	*field_iec_origsampfreq;
	struct regmap_field	*field_iec_channelnumcl0_5; /* iec_channelnumcl0 */
	struct regmap_field	*field_iec_channelnumcr0_5; /* iec_channelnumcr0 */
	struct regmap_field	*field_iec_channelnumcl1;
	struct regmap_field	*field_iec_channelnumcr1;
	struct regmap_field	*field_iec_channelnumcl2;
	struct regmap_field	*field_iec_channelnumcr2;
	struct regmap_field	*field_iec_channelnumcl2a;
	struct regmap_field	*field_iec_channelnumcr2a;
	struct regmap_field	*field_userdata_cl0;
	struct regmap_field	*field_userdata_cr0;
	struct regmap_field	*field_userdata_cl1;
	struct regmap_field	*field_userdata_cr1;
	struct regmap_field	*field_userdata_cl2;
	struct regmap_field	*field_userdata_cr2;
	struct regmap_field	*field_userdata_cl3;
	struct regmap_field	*field_userdata_cr3;
	struct regmap_field	*field_validity_bit_cl0;
	struct regmap_field	*field_validity_bit_cr0;
	struct regmap_field	*field_validity_bit_cl1;
	struct regmap_field	*field_validity_bit_cr1;
	struct regmap_field	*field_validity_bit_cl2;
	struct regmap_field	*field_validity_bit_cr2;
	struct regmap_field	*field_validity_bit_cl3;
	struct regmap_field	*field_validity_bit_cr3;
};

static const struct regmap_config dwc_dptx_ctrl_regmap_cfg = {
	.name = "dwc_dptx_ctrl",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.cache_type = REGCACHE_NONE,
};
