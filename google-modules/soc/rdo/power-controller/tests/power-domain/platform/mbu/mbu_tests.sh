# SPDX-License-Identifier: GPL-2.0-only
# Malibu power domain tests

run_pd_test() {
  local domain="$1"
  if ! ./pd_test_runner "${domain}"; then
    fail "Error verifying power domain: ${domain}. See test.log for details."
  fi
}

test_domain_aurdsp() { run_pd_test "sswrp_aurdsp_pd"; }
test_domain_aoss_pg() { run_pd_test "sswrp_aoss_pg_pd"; }
test_domain_codec_3p() { run_pd_test "sswrp_codec_3p_pd"; }
test_domain_cpuacc() { run_pd_test "cpuacc_gpdma_pd"; }
test_domain_gcv() { run_pd_test "sswrp_gcv_pd"; }
test_domain_tpu() { run_pd_test "sswrp_tpu_pd"; }
test_domain_memss_dta() { run_pd_test "memss_dta_pd"; }
test_domain_ispbe() { run_pd_test "sswrp_ispbe_pd"; }
test_domain_g2d() { run_pd_test "sswrp_g2d_pd"; }
test_domain_gpu() { run_pd_test "sswrp_gpu_pd"; }
test_domain_lsio_e() { run_pd_test "sswrp_lsio_e_pd"; }
test_domain_lsio_s() { run_pd_test "sswrp_lsio_s_pd"; }
test_domain_dpu() { run_pd_test "sswrp_dpu_pd"; }
test_domain_hsio_n() { run_pd_test "sswrp_hsio_n_pd"; }
test_domain_hsio_s() { run_pd_test "sswrp_hsio_s_pd"; }
test_domain_ispfe() { run_pd_test "sswrp_ispfe_pd"; }
test_domain_pcie() { run_pd_test "sswrp_pcie_pd"; }
