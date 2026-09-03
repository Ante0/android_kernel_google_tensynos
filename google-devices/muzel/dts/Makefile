dtbs += \
	google-base/lga-a0.dtb \
	google-base/lga-b0.dtb

overlays += \
	lga-deepspace.dtbo \
	lga-deepspace-tesseract.dtbo \
	lga-deepspace-gem5.dtbo \
	lga-deepspace-soc-hybrid.dtbo \
	lga-frankel-proto0.dtbo \
	lga-frankel-proto1.dtbo \
	lga-frankel-proto1_1.dtbo \
	lga-frankel-evt1.dtbo \
	lga-frankel-evt1_1.dtbo \
	lga-frankel-evt1_1-wingboard.dtbo \
	lga-frankel-dvt1.dtbo \
	lga-frankel-pvt1.dtbo \
	lga-frankel-mp.dtbo \
	lga-blazer-proto1.dtbo \
	lga-blazer-proto1_1.dtbo \
	lga-blazer-evt1.dtbo \
	lga-blazer-evt1_1.dtbo \
	lga-blazer-evt1_1-wingboard.dtbo \
	lga-blazer-dvt1.dtbo \
	lga-blazer-pvt1.dtbo \
	lga-blazer-mp.dtbo \
	lga-mustang-proto1.dtbo \
	lga-mustang-proto1_1.dtbo \
	lga-mustang-proto1_1-wingboard.dtbo \
	lga-mustang-evt1.dtbo \
	lga-mustang-evt1-wingboard.dtbo \
	lga-mustang-evt1_1.dtbo \
	lga-mustang-evt1_1-wingboard.dtbo \
	lga-mustang-dvt1.dtbo \
	lga-mustang-pvt1.dtbo \
	lga-mustang-mp.dtbo \
	lga-dpm-eng.dtbo \
	lga-dpm-user.dtbo \
	lga-dpm-userdebug.dtbo

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
$(call multi_dtbs_overlay, $(dtbs), $(overlays), $(CONFIG_SOC_LGA))
