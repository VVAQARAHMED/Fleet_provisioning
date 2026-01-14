#ifndef CONNECT__INTERFACE__
#define CONNECT__INTERFACE__
#define L4_EVENT_MASK	      (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;
void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
			     struct net_if *iface);

void connectivity_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
				       struct net_if *iface);
#endif