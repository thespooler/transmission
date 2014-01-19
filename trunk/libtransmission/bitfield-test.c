/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id:$
 */

#include <string.h> /* strlen () */
#include "transmission.h"
#include "crypto.h"
#include "bitfield.h"
#include "utils.h" /* tr_free */

#include "libtransmission-test.h"

static int
test_bitfield_count_range (void)
{
  int i;
  int n;
  int begin;
  int end;
  int count1;
  int count2;
  const int bitCount = 100 + tr_cryptoWeakRandInt (1000);
  tr_bitfield bf;

  /* generate a random bitfield */
  tr_bitfieldConstruct (&bf, bitCount);
  for (i=0, n=tr_cryptoWeakRandInt (bitCount); i<n; ++i)
    tr_bitfieldAdd (&bf, tr_cryptoWeakRandInt (bitCount));
  begin = tr_cryptoWeakRandInt (bitCount);
  do
    end = tr_cryptoWeakRandInt (bitCount);
  while (end == begin);

  /* ensure end <= begin */
  if (end < begin)
    {
      const int tmp = begin;
      begin = end;
      end = tmp;
    }

  /* test the bitfield */
  count1 = 0;
  for (i=begin; i<end; ++i)
    if (tr_bitfieldHas (&bf, i))
      ++count1;
  count2 = tr_bitfieldCountRange (&bf, begin, end);
  check (count1 == count2);

  /* cleanup */
  tr_bitfieldDestruct (&bf);
  return 0;
}

static int
test_bitfields (void)
{
  unsigned int i;
  unsigned int bitcount = 500;
  tr_bitfield field;

  tr_bitfieldConstruct (&field, bitcount);

  /* test tr_bitfieldAdd */
  for (i=0; i<bitcount; i++)
    if (! (i % 7))
      tr_bitfieldAdd (&field, i);
  for (i=0; i<bitcount; i++)
    check (tr_bitfieldHas (&field, i) == (! (i % 7)));

  /* test tr_bitfieldAddRange */
  tr_bitfieldAddRange (&field, 0, bitcount);
  for (i=0; i<bitcount; i++)
    check (tr_bitfieldHas (&field, i));

  /* test tr_bitfieldRemRange in the middle of a boundary */
  tr_bitfieldRemRange (&field, 4, 21);
  for (i=0; i<64; i++)
    check (tr_bitfieldHas (&field, i) == ((i < 4) || (i >= 21)));

  /* test tr_bitfieldRemRange on the boundaries */
  tr_bitfieldAddRange (&field, 0, 64);
  tr_bitfieldRemRange (&field, 8, 24);
  for (i=0; i<64; i++)
    check (tr_bitfieldHas (&field, i) == ((i < 8) || (i >= 24)));

  /* test tr_bitfieldRemRange when begin & end is on the same word */
  tr_bitfieldAddRange (&field, 0, 64);
  tr_bitfieldRemRange (&field, 4, 5);
  for (i=0; i<64; i++)
    check (tr_bitfieldHas (&field, i) == ((i < 4) || (i >= 5)));

  /* test tr_bitfieldAddRange */
  tr_bitfieldRemRange (&field, 0, 64);
  tr_bitfieldAddRange (&field, 4, 21);
  for (i=0; i<64; i++)
    check (tr_bitfieldHas (&field, i) == ((4 <= i) && (i < 21)));

  /* test tr_bitfieldAddRange on the boundaries */
  tr_bitfieldRemRange (&field, 0, 64);
  tr_bitfieldAddRange (&field, 8, 24);
  for (i=0; i<64; i++)
    check (tr_bitfieldHas (&field, i) == ((8 <= i) && (i < 24)));

  /* test tr_bitfieldAddRange when begin & end is on the same word */
  tr_bitfieldRemRange (&field, 0, 64);
  tr_bitfieldAddRange (&field, 4, 5);
  for (i=0; i<64; i++)
    check (tr_bitfieldHas (&field, i) == ((4 <= i) && (i < 5)));

  tr_bitfieldDestruct (&field);
  return 0;
}

int
main (void)
{
  int l;
  int ret;
  const testFunc tests[] = { test_bitfields };

  if ((ret = runTests (tests, NUM_TESTS (tests))))
    return ret;

  /* bitfield count range */
  for (l=0; l<10000; ++l)
    if ((ret = test_bitfield_count_range ()))
      return ret;

  return 0;
}
