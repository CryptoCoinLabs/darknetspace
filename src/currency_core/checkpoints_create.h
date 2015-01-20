// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "checkpoints.h"
#include "misc_log_ex.h"

#define ADD_CHECKPOINT(h, hash)  CHECK_AND_ASSERT(checkpoints.add_checkpoint(h,  hash), false);

namespace currency {
  inline bool create_checkpoints(currency::checkpoints& checkpoints)
  {
#ifndef TESTNET
ADD_CHECKPOINT(1,  "8c95e6f30de1a27fd560859d7920ae2fcf68ff2e4d7d27ba806be7cc703b6148");
ADD_CHECKPOINT(100,  "f4bffac41991357cf17425ec2dc3a81d51cf8b54e07e456b63c23f98b3da8208");
ADD_CHECKPOINT(1000,  "f1e96d987f679568c0d4fc1da35c0c5b7d5d5c7ce3902ff2a3c4477b83fc5b05");
ADD_CHECKPOINT(2000,  "c40d0fee33f35a8392841382d0f8c99c8d7f36cd1db08cda3fb711437f0795fe");
ADD_CHECKPOINT(3000,  "364ae7784faf9f5edeb2f7bdfb454bf85aa1598c04545a03200f97b2dd3af108");
ADD_CHECKPOINT(5000,  "4b307d660b36f51473a78eee6815c0bf939c2330036a97b9a835f37b80407e92");
ADD_CHECKPOINT(10000,  "3aedefe8d266320d8990eec7d3f9898c870841a2c93636a69fc7dd2fc7e74d92");
ADD_CHECKPOINT(20000,  "24bed6d89303c3b84b7e7964f84e9279f2be103a917f202f2465ae72c101786c");
ADD_CHECKPOINT(30000,  "e3fcdcc57615ebdcf8542a80181fd7f2b3e18e33ee881a311ed6aa64bc4f8eb0");
ADD_CHECKPOINT(35000,  "53168efa5ea0d4be18db52ca7aca5594427891cfac9e4eb8b292e4524b04fcb4");
ADD_CHECKPOINT(40000,  "05584c1b2c5afff170d3e1b2625a1ca7f5c4c9f229d142e53f6ab80c3fb25b19");
ADD_CHECKPOINT(80000,  "d3855e2a339fdf5934e20d5dd397b1a271537e98bd8ae6e144b419dd2da22519");
ADD_CHECKPOINT(100000,  "f16cb7c305021ea9cdaf723afa14c1faac80ec98c4a1d87e8263170cd6be39e2");
ADD_CHECKPOINT(140000, "b574b0dee1cd6a6668fa97a692b508b2bebff5cf364862a1ae74579912ad89a0");
#endif
    return true;
  }
}
