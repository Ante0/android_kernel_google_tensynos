# SPDX-License-Identifier: GPL-2.0

akita_overlays := \
	zuma-akita-dev1.dtbo \
	zuma-akita-dev1-gnss.dtbo \
	zuma-akita-proto1-A0-broadcom-gnss.dtbo \
	zuma-akita-proto1-B1-broadcom-gnss.dtbo \
	zuma-akita-proto1-A0-samsung-gnss.dtbo \
	zuma-akita-proto1-B1-samsung-gnss.dtbo \
	zuma-akita-proto1_1-broadcom-gnss.dtbo \
	zuma-akita-proto1_1-samsung-gnss.dtbo \
	zuma-akita-proto1_1-broadcom-gnss-wingboard.dtbo \
	zuma-akita-proto1_1-samsung-gnss-wingboard.dtbo \
	zuma-akita-proto1_1-broadcom-gnss-sky.dtbo \
	zuma-akita-proto1_1-samsung-gnss-sky.dtbo \
	zuma-akita-evt1-broadcom-gnss.dtbo \
	zuma-akita-evt1-samsung-gnss.dtbo \
	zuma-akita-evt1_1.dtbo \
	zuma-akita-dvt1.dtbo \
	zuma-akita-dvt1-2nd-fab.dtbo \
	zuma-akita-pvt1.dtbo \
	zuma-akita-mp.dtbo

akita_dtbs := \
	zuma/zuma-a0-foplp.dtb \
	zuma/zuma-a0-ipop.dtb \
	zuma/zuma-b0-foplp.dtb \
	zuma/zuma-b0-ipop.dtb

# Usage:
#   $(call DTBS_overlay_template, overlay, base_dtb, suffix)
#
# This is a generic template to create the following construct:
#   <overlay>-<suffix>-dtbs := <base_dtb>.dtb <overlay>.dtbo
define DTBS_overlay_template
	$(1:%.dtbo=%-$(strip $3)-dtbs) := $2 $1
endef

# Usage:
#   $(call DTB_object_template, CONFIG_, overlay, suffix)
#
# This is a generic template to create the following construct:
#   dtb-$(CONFIG_) += <overlay>-<suffix>.dtb
define DTB_object_template
	dtb-$(strip $1) += $(2:%.dtbo=%-$(strip $3).dtb)
endef

# Usage:
#   $(call multi_dtbs_overlay, list_of_base_dtbs, list_of_overlays, CONFIG_)
#
# For each (base_dtb,overlay) pair, this will create the following constructs:
#   <overlay>-<base_dtb>-dtbs := <base_dtb>.dtb <overlay>.dtbo
#   dtb-$(CONFIG_) += <overlay>-<base_dtb>.dtb
define multi_dtbs_overlay
	$(foreach base, $1, \
		$(foreach overlay, $2, \
			$(eval $(call DTBS_overlay_template, $(overlay), $(base), $(notdir $(base:%.dtb=%)))) \
			$(eval $(call DTB_object_template, $3, $(overlay), $(notdir $(base:%.dtb=%))))))
endef

# Create the -dtbs and dtb-y constructs. We can do this by looping through the
# base dtbs and apply each overlay to it. Kbuild will use these -dtbs
# constructs to determine which dtbs are the base dtbs and which are the
# overlays. For the base dtbs, kbuild will expose the symbols as needed for the
# overlays. Additionally, kbuild will statically apply the overlay to the base
# dtb which could be used to verify the overlay applies cleanly.
#$(call multi_dtbs_overlay, $(akita_dtbs), $(akita_overlays), $(CONFIG_SOC_ZUMA))
$(call multi_dtbs_overlay, $(akita_dtbs), $(akita_overlays), y)
