#ifndef MOO_BARE_SSH_MLKEM_CONFIG_H
#define MOO_BARE_SSH_MLKEM_CONFIG_H

/* mlkem-native v2.0.0 (d1b2fe782888bdb761a50336012923180be7f502).
 * The vendored sources are unchanged. This is the Bare SSH configuration.
 *
 * Bare SSH needs only deterministic ML-KEM-768 encapsulation. Its caller
 * supplies the FIPS 203 message from the existing kernel DRBG. */
#define MLK_CONFIG_PARAMETER_SET 768
#define MLK_CONFIG_NAMESPACE_PREFIX moo_mlkem768
#define MLK_CONFIG_NO_RANDOMIZED_API
#define MLK_CONFIG_NO_KEYPAIR_API
#define MLK_CONFIG_NO_DECAPS_API

#endif
