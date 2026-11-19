/*
 * A Wi-Fi radio on PCIe (a QCA6390 behind an SM8250's PCIe 0), for drivers/wifi/hakios.wifi.c and hakios.lx.c: the
 * device's own driver powers the radio, brings the controller and the link up and answers for the device's configuration
 * space and memory window; ath11k's pci.c (wifi/lx) does the rest through MHI. A device repository that has such a radio
 * builds a driver that defines these (devices/spartan/drivers/wifi/hakios.spartan_wifi.c), with -DHAKIOS_SPARTAN_WIFI;
 * without one, the radio is redwood's WCN6750 inside the SoC.
 *
 * After the OnePlus 8 port's instantnoodle_wifi.c (github.com/dekefake/hackios-op8pro), which found the order and the
 * numbers on the same SoC and radio.
 */
#ifndef _SYS_HAKIOS_PCIE_WIFI_H_
#define _SYS_HAKIOS_PCIE_WIFI_H_

#include <stdbool.h>
#include <stdint.h>

/* The radio powered and the link up (true), or all of it off (false; always true). */
bool     hakios_pcie_wifi_power(bool on);
/* The device's configuration space; all ones while the link is down. */
uint32_t hakios_pcie_wifi_config_read(uint32_t offset, unsigned width);
void     hakios_pcie_wifi_config_write(uint32_t offset, uint32_t value, unsigned width);
/* Where BAR `bar` is in the machine's physical addresses; false: there is none. */
bool     hakios_pcie_wifi_bar(unsigned bar, uint64_t *physical, uint64_t *size);
/* On the way back to the loader, one processor left: the device held in reset so that it writes to no memory any more. */
void     hakios_pcie_wifi_quiesce(void);

#endif /* _SYS_HAKIOS_PCIE_WIFI_H_ */
