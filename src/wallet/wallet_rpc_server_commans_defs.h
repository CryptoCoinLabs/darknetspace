// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "currency_protocol/currency_protocol_defs.h"
#include "currency_core/currency_basic.h"
#include "crypto/hash.h"
#include "wallet_rpc_server_error_codes.h"
namespace tools
{
namespace wallet_rpc
{
#define WALLET_RPC_STATUS_OK      "OK"
#define WALLET_RPC_STATUS_BUSY    "BUSY"


  struct wallet_transfer_info_details
  {
    std::list<uint64_t> rcv;
    std::list<uint64_t> spn;

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(rcv)
      KV_SERIALIZE(spn)
    END_KV_SERIALIZE_MAP()

  };

  struct wallet_transfer_info
  {
    uint64_t      amount;
    uint64_t      timestamp;
    std::string   tx_hash;
    uint64_t      height;          //if height == 0 than tx is unconfirmed
    uint64_t      unlock_time;
    uint32_t      tx_blob_size;
    std::string   payment_id;
    std::string   recipient;       //optional
    std::string   recipient_alias; //optional
    bool          is_income;
    uint64_t      fee;
    wallet_transfer_info_details td;
    
    //not included in serialization map
    currency::transaction tx;

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(amount)
      KV_SERIALIZE(tx_hash)
      KV_SERIALIZE(height)
      KV_SERIALIZE(unlock_time)
      KV_SERIALIZE(tx_blob_size)
      KV_SERIALIZE(payment_id)
      KV_SERIALIZE(recipient)      
      KV_SERIALIZE(recipient_alias)
      KV_SERIALIZE(is_income)
      KV_SERIALIZE(timestamp)
      KV_SERIALIZE(td)
      KV_SERIALIZE(fee)
    END_KV_SERIALIZE_MAP()
  };


  struct COMMAND_RPC_GET_BALANCE
  {
    struct request
    {
      BEGIN_KV_SERIALIZE_MAP()
      END_KV_SERIALIZE_MAP()
    };

    struct response
    {
      uint64_t 	 balance;
      uint64_t 	 unlocked_balance;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(balance)
        KV_SERIALIZE(unlocked_balance)
      END_KV_SERIALIZE_MAP()
    };
  };

  struct COMMAND_RPC_GET_ADDRESS
  {
    struct request
    {
      BEGIN_KV_SERIALIZE_MAP()
      END_KV_SERIALIZE_MAP()
    };

    struct response
    {
      std::string   address;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(address)
      END_KV_SERIALIZE_MAP()
    };
  };

  struct trnsfer_destination
  {
    uint64_t amount;
    std::string address;
    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(amount)
      KV_SERIALIZE(address)
    END_KV_SERIALIZE_MAP()
  };

  struct COMMAND_RPC_TRANSFER
  {
    struct request
    {
      std::list<trnsfer_destination> destinations;
      uint64_t fee;
      uint64_t mixin;
      uint64_t unlock_time;
      std::string payment_id_hex;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(destinations)
        KV_SERIALIZE(fee)
        KV_SERIALIZE(mixin)
        KV_SERIALIZE(unlock_time)
        KV_SERIALIZE(payment_id_hex)
      END_KV_SERIALIZE_MAP()
    };

    struct response
    {
      std::string tx_hash;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(tx_hash)
      END_KV_SERIALIZE_MAP()
    };
  };

  struct COMMAND_RPC_STORE
  {
    struct request
    {
      BEGIN_KV_SERIALIZE_MAP()
      END_KV_SERIALIZE_MAP()
    };

    struct response
    {
      BEGIN_KV_SERIALIZE_MAP()
      END_KV_SERIALIZE_MAP()
    };
  };

  struct payment_details
  {
    std::string tx_hash;
    uint64_t amount;
    uint64_t block_height;
    uint64_t unlock_time;

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(tx_hash)
      KV_SERIALIZE(amount)
      KV_SERIALIZE(block_height)
      KV_SERIALIZE(unlock_time)
    END_KV_SERIALIZE_MAP()
  };

  struct COMMAND_RPC_GET_PAYMENTS
  {
    struct request
    {
      std::string payment_id;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(payment_id)
      END_KV_SERIALIZE_MAP()
    };

    struct response
    {
      std::list<payment_details> payments;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(payments)
      END_KV_SERIALIZE_MAP()
    };
  };

  struct incoming_tx_details: public payment_details
  {
	  std::string tx_hash;
	  uint64_t amount;
	  uint64_t block_height;
	  uint64_t unlock_time;
	  uint64_t timestamp;
	  std::string payment_id;

	  BEGIN_KV_SERIALIZE_MAP()
		  KV_SERIALIZE(timestamp)
		  KV_SERIALIZE(payment_id)
		  KV_SERIALIZE(tx_hash)
		  KV_SERIALIZE(amount)
		  KV_SERIALIZE(block_height)
		  KV_SERIALIZE(unlock_time)
	  END_KV_SERIALIZE_MAP()
  };

  struct COMMAND_RPC_GET_INCOMING_TX
  {
    struct request
    {
	  uint64_t tx_start_index;
	  uint64_t tx_get_count;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(tx_start_index)
		KV_SERIALIZE(tx_get_count)
      END_KV_SERIALIZE_MAP()
    };

    struct response
    {
      std::list<incoming_tx_details> incoming_txs;
	  uint64_t tx_total_count;

      BEGIN_KV_SERIALIZE_MAP()
        KV_SERIALIZE(incoming_txs)
		KV_SERIALIZE(tx_total_count)
      END_KV_SERIALIZE_MAP()
    };
  };

}
}

