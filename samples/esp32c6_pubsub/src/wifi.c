// SPDX-License-Identifier: Apache-2.0
#include "wifi.h"
#include "wifi_credentials.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(address_ready, 0, 1);
static struct net_mgmt_event_callback wifi_cb, ip_cb;
static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
  ARG_UNUSED(iface);
  if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
    const struct wifi_status *s = cb->info;
    if (s && s->status == 0)
      k_sem_give(&connected);
    else
      printk("APP_WIFI_CONNECT_ERROR %d\n", s ? s->status : -1);
  }
}
static void ip_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
  ARG_UNUSED(cb);
  ARG_UNUSED(iface);
  if (event == NET_EVENT_IPV4_ADDR_ADD)
    k_sem_give(&address_ready);
}
int app_wifi_connect(void)
{
  struct net_if *iface = net_if_get_default();
  if (!iface)
    return -ENODEV;
  net_mgmt_init_event_callback(&wifi_cb, wifi_event, NET_EVENT_WIFI_CONNECT_RESULT);
  net_mgmt_init_event_callback(&ip_cb, ip_event, NET_EVENT_IPV4_ADDR_ADD);
  net_mgmt_add_event_callback(&wifi_cb);
  net_mgmt_add_event_callback(&ip_cb);
  struct wifi_connect_req_params params = {
      .ssid = (const uint8_t *)APP_WIFI_SSID,
      .ssid_length = sizeof(APP_WIFI_SSID) - 1,
      .psk = (const uint8_t *)APP_WIFI_PSK,
      .psk_length = sizeof(APP_WIFI_PSK) - 1,
      .channel = WIFI_CHANNEL_ANY,
      .security = WIFI_SECURITY_TYPE_PSK,
  };
  int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
  if (rc)
    return rc;
  if (k_sem_take(&connected, K_SECONDS(30)) || k_sem_take(&address_ready, K_SECONDS(30)))
    return -ETIMEDOUT;
  struct wifi_ps_params ps = {.enabled = WIFI_PS_DISABLED, .type = WIFI_PS_PARAM_STATE};
  rc = net_mgmt(NET_REQUEST_WIFI_PS, iface, &ps, sizeof(ps));
  if (rc)
    return rc; // Keep the reference profile reproducible.
  const struct net_in_addr *ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
  char address[NET_IPV4_ADDR_LEN];
  if (!ip || !net_addr_ntop(NET_AF_INET, ip, address, sizeof(address)))
    return -EADDRNOTAVAIL;
  struct wifi_iface_status status = {0};
  rc = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
  printk("APP_WIFI_READY ip=%s power_save=disabled status_rc=%d rssi=%d channel=%u\n", address, rc,
         status.rssi, status.channel);
  return 0;
}
