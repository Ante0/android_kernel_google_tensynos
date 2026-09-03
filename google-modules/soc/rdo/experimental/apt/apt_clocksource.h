/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _GOOGLE_APT_CLOCKSOURCE_H
#define _GOOGLE_APT_CLOCKSOURCE_H

struct google_apt;

int google_apt_clocksource_init(struct google_apt *apt);
void google_apt_clocksource_exit(struct google_apt *apt);

#endif /* _GOOGLE_APT_CLOCKSOURCE_H */
