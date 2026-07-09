#ifndef WIFI_UDP_H
#define WIFI_UDP_H

#ifdef __cplusplus
extern "C" {
#endif

void wifi_udp_init(void);
void udp_send2com(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif