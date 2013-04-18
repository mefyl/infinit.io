#include "comet.hh"

#include <comet/bn.hh>
#include <comet/rand.hh>

#include <openssl/bn.h>
#include <openssl/rand.h>

/*------.
| Basic |
`------*/

static
void
test_basic()
{
  BOOST_CHECK(::dRAND_init() == 1);

  BOOST_CHECK(::dRAND_start() == 1);
  {
    ::RAND_status();
    ::RAND_status();

    ::dRAND_reset();

    ::RAND_status();
  }
  BOOST_CHECK(::dRAND_stop() == 1);

  BOOST_CHECK(::RAND_status() == 1);

  BOOST_CHECK(::dRAND_clean() == 1);
}

/*-----.
| Seed |
`-----*/

static
void
test_seed()
{
  const char* seed =
    "Sir, an equation has no meaning for me "
    "unless it expresses a thought of GOD.";

  BOOST_CHECK(::dRAND_init() == 1);

  BOOST_CHECK(::dRAND_start() == 1);
  {
    // Reset the random implementation and seed the random generator.
    ::dRAND_reset();
    ::RAND_seed(seed, ::strlen(seed));
    BOOST_CHECK(::RAND_status() == 1);
    char* fingerprint1 = ::dRAND_fingerprint();
    BOOST_CHECK(fingerprint1 != nullptr);

    // Re-reset the random implementation and finally re-seed it:
    // the result should be the same as the first seeding.
    ::dRAND_reset();
    ::RAND_seed(seed, ::strlen(seed));
    BOOST_CHECK(::RAND_status() == 1);
    char* fingerprint2 = ::dRAND_fingerprint();
    BOOST_CHECK(fingerprint2 != nullptr);

    BOOST_CHECK(::strcmp(fingerprint1, fingerprint2) == 0);

    ::free(fingerprint1);
    ::free(fingerprint2);
  }
  BOOST_CHECK(::dRAND_stop() == 1);

  BOOST_CHECK(::dRAND_clean() == 1);
}

/*---------------.
| Generate Prime |
`---------------*/

static
void
test_generate_prime()
{
  BOOST_CHECK(::dRAND_init() == 1);

  // Undeterministically randomly generate numbers should should therefore
  // all be different, with high probability.
  {
    ::BIGNUM* n1 = ::BN_new();
    BOOST_CHECK_EQUAL(::BN_generate_prime_ex(n1, 1024, 0,
                                           NULL, NULL, NULL), 1);

    ::BIGNUM* n2 = ::BN_new();
    BOOST_CHECK_EQUAL(::BN_generate_prime_ex(n2, 1024, 0,
                                           NULL, NULL, NULL), 1);

    ::BIGNUM* n3 = ::BN_new();
    BOOST_CHECK_EQUAL(::BN_generate_prime_ex(n3, 1024, 0,
                                           NULL, NULL, NULL), 1);

    BOOST_CHECK(::BN_cmp(n1, n2) != 0);
    BOOST_CHECK(::BN_cmp(n1, n3) != 0);
    BOOST_CHECK(::BN_cmp(n2, n3) != 0);
  }

  // Generate numbers in a deterministic (but probabilist) way, by resetting the
  // seed for every random generation.
  //
  // Since based on the same seed, all the generated numbers should be equal.
  {
    const char* seed =
      "God exists since mathematics is consistent, "
      "and the Devil exists since we cannot prove it.";

    BOOST_CHECK(::dRAND_start() == 1);
    {
      ::BIGNUM* n1 = ::BN_new();
      ::dRAND_reset();
      ::RAND_seed(seed, ::strlen(seed));
      char* fingerprint1 = ::dRAND_fingerprint();
      BOOST_CHECK(fingerprint1 != nullptr);
      BOOST_CHECK(::RAND_status() == 1);
      BOOST_CHECK_EQUAL(::dBN_generate_prime_ex(n1, 1024, 0,
                                                NULL, NULL, NULL), 1);

      exit(0); // XXX

      ::BIGNUM* n2 = ::BN_new();
      ::dRAND_reset();
      ::RAND_seed(seed, ::strlen(seed));
      char* fingerprint2 = ::dRAND_fingerprint();
      BOOST_CHECK(fingerprint2 != nullptr);
      BOOST_CHECK(::RAND_status() == 1);
      BOOST_CHECK_EQUAL(::dBN_generate_prime_ex(n2, 1024, 0,
                                                NULL, NULL, NULL), 1);

      ::BIGNUM* n3 = ::BN_new();
      ::dRAND_reset();
      ::RAND_seed(seed, ::strlen(seed));
      char* fingerprint3 = ::dRAND_fingerprint();
      BOOST_CHECK(fingerprint3 != nullptr);
      BOOST_CHECK(::RAND_status() == 1);
      BOOST_CHECK_EQUAL(::dBN_generate_prime_ex(n3, 1024, 0,
                                                NULL, NULL, NULL), 1);

      BOOST_CHECK(::strcmp(fingerprint1, fingerprint2) == 0);
      BOOST_CHECK(::strcmp(fingerprint1, fingerprint3) == 0);
      BOOST_CHECK(::strcmp(fingerprint2, fingerprint3) == 0);

      BOOST_CHECK(::BN_cmp(n1, n2) == 0);
      BOOST_CHECK(::BN_cmp(n1, n3) == 0);
      BOOST_CHECK(::BN_cmp(n2, n3) == 0);
    }
    BOOST_CHECK(::dRAND_stop() == 1);
  }

  // Likewise, generate numbers in a deterministic but safe way.
  {
    const char* seed =
      "A mathematician, like a painter or poet, is a maker of patterns. "
      "If his patterns are more permanent than theirs, it is because they "
      "are made with ideas.";

    BOOST_CHECK(::dRAND_start() == 1);
    {
      ::BIGNUM* n1 = ::BN_new();
      ::dRAND_reset();
      ::RAND_seed(seed, ::strlen(seed));
      char* fingerprint1 = ::dRAND_fingerprint();
      BOOST_CHECK(fingerprint1 != nullptr);
      BOOST_CHECK(::RAND_status() == 1);
      BOOST_CHECK_EQUAL(::dBN_generate_prime_ex(n1, 512, 1,
                                                NULL, NULL, NULL), 1);

      ::BIGNUM* n2 = ::BN_new();
      ::dRAND_reset();
      ::RAND_seed(seed, ::strlen(seed));
      char* fingerprint2 = ::dRAND_fingerprint();
      BOOST_CHECK(fingerprint2 != nullptr);
      BOOST_CHECK(::RAND_status() == 1);
      BOOST_CHECK_EQUAL(::dBN_generate_prime_ex(n2, 2048, 1,
                                                NULL, NULL, NULL), 1);

      ::BIGNUM* n3 = ::BN_new();
      ::dRAND_reset();
      ::RAND_seed(seed, ::strlen(seed));
      char* fingerprint3 = ::dRAND_fingerprint();
      BOOST_CHECK(fingerprint3 != nullptr);
      BOOST_CHECK(::RAND_status() == 1);
      BOOST_CHECK_EQUAL(::dBN_generate_prime_ex(n3, 512, 1,
                                                NULL, NULL, NULL), 1);

      BOOST_CHECK(::strcmp(fingerprint1, fingerprint2) == 0);
      BOOST_CHECK(::strcmp(fingerprint1, fingerprint3) == 0);
      BOOST_CHECK(::strcmp(fingerprint2, fingerprint3) == 0);

      BOOST_CHECK(::BN_cmp(n1, n2) != 0);
      BOOST_CHECK(::BN_cmp(n1, n3) == 0);
      BOOST_CHECK(::BN_cmp(n2, n3) != 0);
    }
    BOOST_CHECK(::dRAND_stop() == 1);
  }

  BOOST_CHECK(::dRAND_clean() == 1);
}

/*-----.
| Main |
`-----*/

static
bool
test()
{
  boost::unit_test::test_suite* suite = BOOST_TEST_SUITE("rand");

  suite->add(BOOST_TEST_CASE(test_basic));
  suite->add(BOOST_TEST_CASE(test_seed));
  suite->add(BOOST_TEST_CASE(test_generate_prime));

  boost::unit_test::framework::master_test_suite().add(suite);

  return (true);
}

int
main(int argc,
     char** argv)
{
  return (boost::unit_test::unit_test_main(test, argc, argv));
}
