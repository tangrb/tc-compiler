/* TC 0.0.46 contract: aggregate freeze check.
 *
 * tc_contract_check() is the single entry point used by the tools at startup
 * and by the L0 contract tests.  It returns NULL when every frozen contract
 * invariant holds, otherwise a static English description of the first
 * violation.  It never reports a language diagnostic.
 */
#ifndef TC_FRONTEND_CONTRACT_H
#define TC_FRONTEND_CONTRACT_H

#define TC_LANGUAGE_VERSION "0.0.46"
#define TC_CONTRACT_FROZEN 1

const char *tc_contract_check(void);

#endif /* TC_FRONTEND_CONTRACT_H */
