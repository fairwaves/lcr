
struct gsm_network *bootstrap_network(int (*mncc_recv)(struct gsm_network *, int, void *), gsm_bts_type bts_type, int mcc, int mnc, int lac, int arfcn, int cardnr, int release_l2, char *name_short, char *name_long, char *database_name, int allow_all);
int shutdown_net(struct gsm_network *network);

