// Self-contained verification of the OpenSSL 3 crypto port.
//
// The upstream key_tests/base58_tests use hard-coded *Bitcoin* address and WIF
// vectors, which can never match HoboNickels' own network version bytes
// (PUBKEY_ADDRESS = 34, 'H').  This suite instead generates keys with
// HoboNickels' own code and checks the round-trips that exercise the parts of
// key.cpp rewritten for OpenSSL 3: ECDSA sign/verify and — most importantly —
// compact-signature public-key recovery (ECDSA_SIG_get0/set0 + EC_POINT path).

#include <boost/test/unit_test.hpp>

#include "key.h"
#include "base58.h"

#include <secp256k1.h>

static uint256 MakeHash(int seed)
{
    uint256 h;
    unsigned char* p = (unsigned char*)&h;
    for (int j = 0; j < 32; j++)
        p[j] = (unsigned char)(seed * 31 + j * 7 + 1);
    return h;
}

BOOST_AUTO_TEST_SUITE(crypto_selftest)

BOOST_AUTO_TEST_CASE(sign_verify_roundtrip)
{
    for (int i = 0; i < 16; i++)
    {
        CKey key;
        key.MakeNewKey(i % 2 == 0); // mix compressed / uncompressed
        uint256 hash = MakeHash(i);

        std::vector<unsigned char> sig;
        BOOST_CHECK(key.Sign(hash, sig));
        BOOST_CHECK(!sig.empty());
        BOOST_CHECK(key.Verify(hash, sig));

        // A different message must not verify against this signature.
        uint256 other = MakeHash(i + 1000);
        BOOST_CHECK(!key.Verify(other, sig));
    }
}

BOOST_AUTO_TEST_CASE(compact_signature_recovery)
{
    for (int i = 0; i < 16; i++)
    {
        bool compressed = (i % 2 == 0);
        CKey key;
        key.MakeNewKey(compressed);
        uint256 hash = MakeHash(i + 7);

        std::vector<unsigned char> csig;
        BOOST_CHECK(key.SignCompact(hash, csig));
        BOOST_CHECK_EQUAL(csig.size(), 65u);

        CKey rkey;
        BOOST_CHECK(rkey.SetCompactSignature(hash, csig));
        // The recovered public key must equal the signer's public key.
        BOOST_CHECK(rkey.GetPubKey() == key.GetPubKey());
        BOOST_CHECK_EQUAL(rkey.IsCompressed(), compressed);
    }
}

BOOST_AUTO_TEST_CASE(secret_roundtrip)
{
    for (int i = 0; i < 8; i++)
    {
        CKey key;
        key.MakeNewKey(i % 2 == 0);
        bool compressed = false;
        CSecret secret = key.GetSecret(compressed);
        BOOST_CHECK_EQUAL(secret.size(), 32u);

        CKey key2;
        key2.SetSecret(secret, compressed);
        BOOST_CHECK(key2.GetPubKey() == key.GetPubKey());
        BOOST_CHECK(key2.IsValid());
    }
}

BOOST_AUTO_TEST_CASE(hobonickels_address_and_wif_roundtrip)
{
    CKey key;
    key.MakeNewKey(true);

    // Address: must be a valid HoboNickels mainnet address (version byte 34).
    CBitcoinAddress addr(key.GetPubKey().GetID());
    BOOST_CHECK(addr.IsValid());
    std::string addrStr = addr.ToString();
    BOOST_CHECK(!addrStr.empty());

    CBitcoinAddress addr2(addrStr);
    BOOST_CHECK(addr2.IsValid());
    BOOST_CHECK(addr2.Get() == CTxDestination(key.GetPubKey().GetID()));

    // WIF private key round-trip using HoboNickels' own SECRET_KEY version.
    bool compressed = false;
    CSecret secret = key.GetSecret(compressed);
    CBitcoinSecret bsecret;
    bsecret.SetSecret(secret, compressed);
    std::string wif = bsecret.ToString();

    CBitcoinSecret bsecret2;
    BOOST_CHECK(bsecret2.SetString(wif));
    bool compressed2 = false;
    CSecret secret2 = bsecret2.GetSecret(compressed2);
    BOOST_CHECK(secret == secret2);
    BOOST_CHECK_EQUAL(compressed, compressed2);
}

// Verifies the libsecp256k1 fast-verify path (used in script.cpp CheckSig)
// agrees with OpenSSL for real signatures, and that both reject tampered ones.
// This underpins the no-fork guarantee of the libsecp256k1 integration.
BOOST_AUTO_TEST_CASE(secp256k1_matches_openssl)
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    BOOST_REQUIRE(ctx != NULL);

    for (int i = 0; i < 64; i++)
    {
        CKey key;
        key.MakeNewKey(i % 2 == 0); // mix compressed / uncompressed
        uint256 hash = MakeHash(i + 3);

        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign(hash, sig)); // OpenSSL DER signature
        const std::vector<unsigned char>& pub = key.GetPubKey().Raw();

        // OpenSSL verdict
        bool osslOk = key.Verify(hash, sig);
        BOOST_CHECK(osslOk);

        // libsecp256k1 verdict (same steps as VerifyECDSASecp in script.cpp)
        secp256k1_pubkey pk;
        BOOST_REQUIRE(secp256k1_ec_pubkey_parse(ctx, &pk, pub.data(), pub.size()));
        secp256k1_ecdsa_signature s;
        BOOST_REQUIRE(secp256k1_ecdsa_signature_parse_der(ctx, &s, sig.data(), sig.size()));
        secp256k1_ecdsa_signature_normalize(ctx, &s, &s);
        int secpOk = secp256k1_ecdsa_verify(ctx, &s, hash.begin(), &pk);

        // The two libraries must agree on the accept/reject decision.
        BOOST_CHECK_EQUAL(secpOk == 1, osslOk);
        BOOST_CHECK_EQUAL(secpOk, 1);

        // A different message must be rejected by BOTH.
        uint256 other = MakeHash(i + 100000);
        BOOST_CHECK(!key.Verify(other, sig));
        BOOST_CHECK_EQUAL(secp256k1_ecdsa_verify(ctx, &s, other.begin(), &pk), 0);
    }

    secp256k1_context_destroy(ctx);
}

BOOST_AUTO_TEST_SUITE_END()
