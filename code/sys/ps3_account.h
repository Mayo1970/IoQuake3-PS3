/*
 * code/sys/ps3_account.h -- XMB user / PSN nickname as the default player name.
 *
 * This has to feed Cvar_Get's *default* (the cvar's resetString), not a post-
 * Com_Init Cvar_Set: Cvar_Set only writes the value, so the setup menu's Defaults
 * button (exec default.cfg + cvar_restart) restored the stock "UnnamedPlayer".
 */
#ifndef PS3_ACCOUNT_H
#define PS3_ACCOUNT_H

/*
 * Resolves and caches the XMB user name (falling back to the PSN nickname).
 * Call once early in boot, before Com_Init; PS3_DefaultPlayerName() is then a
 * pure read, safe to call from CL_Init.
 */
void PS3_InitDefaultPlayerName(void);

/* Cached user name, sanitised for cvar/userinfo use. Never NULL. */
const char *PS3_DefaultPlayerName(void);

#endif /* PS3_ACCOUNT_H */
