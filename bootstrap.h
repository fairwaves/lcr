
void *bootstrap_network(int (*mncc_recv)(void *, int, void *),int bts_type, int mcc, int mnc, int lac, int arfcn, int cardnr, int release_l2, char *name_short, char *name_long, char *hlr, int allow_all);
int shutdown_net(void *network);

