// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <unordered_map>

#include "include_base_utils.h"
using namespace epee;
#include "wallet/wallet2.h"
using namespace currency;

std::string generate_random_wallet_name()
{
  std::stringstream ss;
  ss << boost::uuids::random_generator()();
  return ss.str();
}

inline uint64_t random(const uint64_t max_value) {
  return (uint64_t(rand()) ^
          (uint64_t(rand())<<16) ^
          (uint64_t(rand())<<32) ^
          (uint64_t(rand())<<48)) % max_value;
}

bool do_send_money(tools::wallet2& w1, tools::wallet2& w2, size_t mix_in_factor, uint64_t amount_to_transfer, transaction& tx, size_t parts=1)
{
  CHECK_AND_ASSERT_MES(parts > 0, false, "parts must be > 0");

  std::vector<currency::tx_destination_entry> dsts;
  dsts.reserve(parts);
  uint64_t amount_used = 0;
  if (parts == 0) parts = 1;
  uint64_t max_part = amount_to_transfer / parts;

  for (size_t i = 0; i < parts; ++i)
  {
    currency::tx_destination_entry de;
    de.addr = w2.get_account().get_keys().m_account_address;

    if (i < parts - 1)
      de.amount = random(max_part);
    else
      de.amount = amount_to_transfer - amount_used;
    amount_used += de.amount;

    std::cout << "PARTS (" << amount_to_transfer << ") " << amount_used << " " << de.amount << std::endl;

    dsts.push_back(de);
  }

  try
  {
    w1.transfer(dsts, mix_in_factor, 0, DEFAULT_FEE, std::vector<uint8_t>(), tools::detail::null_split_strategy, tools::tx_dust_policy(DEFAULT_FEE), tx);
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

uint64_t get_money_in_first_transfers(const tools::wallet2::transfer_container& incoming_transfers, size_t n_transfers)
{
  uint64_t summ = 0;
  size_t count = 0;
  BOOST_FOREACH(const tools::wallet2::transfer_details& td, incoming_transfers)
  {
    summ += td.m_tx.vout[td.m_internal_output_index].amount;
    if(++count >= n_transfers)
      return summ;
  }
  return summ;
}

#define FIRST_N_TRANSFERS 10*10

bool transactions_flow_test(std::string& working_folder,
  std::string path_source_wallet,
  std::string source_pass,
  std::string path_terget_wallet,
  std::string dest_pass,
  std::string& daemon_addr_a,
  std::string& daemon_addr_b,
  uint64_t amount_to_transfer, size_t mix_in_factor, size_t transactions_count, size_t transactions_per_second)
{
  LOG_PRINT_L0("-----------------------STARTING TRANSACTIONS FLOW TEST-----------------------");
  tools::wallet2 w1, w2;
  if (path_source_wallet.empty())
  {
	  path_source_wallet = generate_random_wallet_name();
	  source_pass = "12345";
  }

  if (path_terget_wallet.empty())
  {
	  path_terget_wallet = generate_random_wallet_name();
	  dest_pass = source_pass;
  }
    
   path_source_wallet = working_folder + "/" + path_source_wallet;
  path_terget_wallet = working_folder + "/" + path_terget_wallet;

  /*
  try
  {
    w1.generate(working_folder + "/" + path_source_wallet, "");
    w2.generate(working_folder + "/" + path_terget_wallet, "");
  }
  catch (const std::exception& e)
  {
    LOG_ERROR("failed to generate wallet: " << e.what());
    return false;
  }
*/
  w1.init(daemon_addr_a);
  w1.load(path_source_wallet, source_pass);

  size_t blocks_fetched = 0;
  bool received_money;
  bool ok;
  if(!w1.refresh(blocks_fetched, received_money, ok))
  {
    LOG_ERROR( "failed to refresh source wallet from " << daemon_addr_a );
    return false;
  }

  w2.init(daemon_addr_b);
  w2.load(path_terget_wallet, dest_pass);

  LOG_PRINT_GREEN("Using wallets: " << ENDL
    << "Source:  " << w1.get_account().get_public_address_str() << ENDL << "Path: " << path_source_wallet << ENDL
    << "Target:  " << w2.get_account().get_public_address_str() << ENDL << "Path: " << path_terget_wallet, LOG_LEVEL_0);

  /*
  //lets do some money
  epee::net_utils::http::http_simple_client http_client;
  COMMAND_RPC_STOP_MINING::request daemon1_req = AUTO_VAL_INIT(daemon1_req);
  COMMAND_RPC_STOP_MINING::response daemon1_rsp = AUTO_VAL_INIT(daemon1_rsp);
  bool r = net_utils::invoke_http_json_remote_command2(daemon_addr_a + "/stop_mine", daemon1_req, daemon1_rsp, http_client, 10000);
  CHECK_AND_ASSERT_MES(r, false, "failed to stop mining");

  COMMAND_RPC_START_MINING::request daemon_req = AUTO_VAL_INIT(daemon_req);
  COMMAND_RPC_START_MINING::response daemon_rsp = AUTO_VAL_INIT(daemon_rsp);
  daemon_req.miner_address = w1.get_account().get_public_address_str();
  daemon_req.threads_count = 9;
  r = net_utils::invoke_http_json_remote_command2(daemon_addr_a + "/start_mining", daemon_req, daemon_rsp, http_client, 10000);
  CHECK_AND_ASSERT_MES(r, false, "failed to get getrandom_outs");
  CHECK_AND_ASSERT_MES(daemon_rsp.status == CORE_RPC_STATUS_OK, false, "failed to getrandom_outs.bin");

  //wait for money, until balance will have enough money
  w1.refresh(blocks_fetched, received_money, ok); 
  */

  while(w1.unlocked_balance() < amount_to_transfer)
  {
    LOG_ERROR("Money in source wallet is not enought , transfer enough money to it, waiting for 5 seconds to conitnue... ");
	misc_utils::sleep_no_w(5000);
	w1.refresh(blocks_fetched, received_money, ok);
  }

  //lets make a lot of small outs to ourselves
  //since it is not possible to start from transaction that bigger than 20Kb, we gonna make transactions 
  //with 500 outs (about 18kb), and we have to wait appropriate count blocks, mined for test wallet
  while(true)
  {
    tools::wallet2::transfer_container incoming_transfers;
    w1.get_transfers(incoming_transfers);
    if(incoming_transfers.size() > FIRST_N_TRANSFERS && get_money_in_first_transfers(incoming_transfers, FIRST_N_TRANSFERS) < w1.unlocked_balance() )
    {
      //lets go!
      size_t count = 0;
      BOOST_FOREACH(tools::wallet2::transfer_details& td, incoming_transfers)
      {
        currency::transaction tx_s;
		uint64_t amount = (td.m_tx.vout[td.m_internal_output_index].amount - DEFAULT_FEE) / COIN;
		bool r = do_send_money(w1, w1, 0, amount, tx_s, 100);
        CHECK_AND_ASSERT_MES(r, false, "Failed to send starter tx " << get_transaction_hash(tx_s));
        LOG_PRINT_GREEN("Starter transaction sent " << get_transaction_hash(tx_s), LOG_LEVEL_0);
        if(++count >= FIRST_N_TRANSFERS)
          break;
      }
      break;
    }else
    {
      misc_utils::sleep_no_w(1000);
      w1.refresh(blocks_fetched, received_money, ok);
    }
  }
  //do actual transfer
  uint64_t transfered_money = 0;
  uint64_t transfer_size = amount_to_transfer/transactions_count;
  size_t i = 0;
  struct tx_test_entry
  {
    transaction tx;
    size_t m_received_count;
    uint64_t amount_transfered;
  };
  crypto::key_image lst_sent_ki = AUTO_VAL_INIT(lst_sent_ki);
  std::unordered_map<crypto::hash, tx_test_entry> txs;
  for(i = 0; i != transactions_count; i++)
  {
    uint64_t amount_to_tx = (amount_to_transfer - transfered_money) > transfer_size ? transfer_size: (amount_to_transfer - transfered_money);
    while(w1.unlocked_balance() < amount_to_tx + DEFAULT_FEE)
    {
	  LOG_PRINT_L0("not enough money, waiting for cashback...5 seconds to conitue...");
      misc_utils::sleep_no_w(5000);     
      w1.refresh(blocks_fetched, received_money, ok);
    }

    transaction tx;
    /*size_t n_attempts = 0;
    while (!do_send_money(w1, w2, mix_in_factor, amount_to_tx, tx)) {
        n_attempts++;
        std::cout << "failed to transfer money, refresh and try again (attempts=" << n_attempts << ")" << std::endl;
        w1.refresh();
    }*/


    if(!do_send_money(w1, w2, mix_in_factor, amount_to_tx, tx))
    {
      LOG_PRINT_L0("failed to transfer money, tx: " << get_transaction_hash(tx) << ", refresh and try again" );
      w1.refresh(blocks_fetched, received_money, ok);
      if(!do_send_money(w1, w2, mix_in_factor, amount_to_tx, tx))
      {
        LOG_PRINT_L0( "failed to transfer money, second chance. tx: " << get_transaction_hash(tx) << ", exit" );
        LOCAL_ASSERT(false);
        return false;
      }
    }
    lst_sent_ki = boost::get<txin_to_key>(tx.vin[0]).k_image;

    transfered_money += amount_to_tx;

    LOG_PRINT_L0("transferred " << amount_to_tx << ", i=" << i );
    tx_test_entry& ent = txs[get_transaction_hash(tx)] = boost::value_initialized<tx_test_entry>();
    ent.amount_transfered = amount_to_tx;
    ent.tx = tx;
    //if(i % transactions_per_second)
    //  misc_utils::sleep_no_w(1000);
  }


  LOG_PRINT_L0( "waiting some new blocks...");
  misc_utils::sleep_no_w(DIFFICULTY_BLOCKS_ESTIMATE_TIMESPAN*20*1000);//wait two blocks before sync on another wallet on another daemon
  LOG_PRINT_L0( "refreshing...");
  bool recvd_money = false;
  while(w2.refresh(blocks_fetched, recvd_money, ok) && ( (blocks_fetched && recvd_money) || !blocks_fetched  ) )
  {
    misc_utils::sleep_no_w(DIFFICULTY_BLOCKS_ESTIMATE_TIMESPAN*1000);//wait two blocks before sync on another wallet on another daemon
  }

  uint64_t money_2 = w2.balance();
  if(money_2 == transfered_money)
  {
    LOG_PRINT_GREEN("-----------------------FINISHING TRANSACTIONS FLOW TEST OK-----------------------", LOG_LEVEL_0);
    LOG_PRINT_GREEN("transferred " << print_money(transfered_money) << " via " << i << " transactions" , LOG_LEVEL_0);
    return true;
  }else
  {
    tools::wallet2::transfer_container tc;
    w2.get_transfers(tc);
    BOOST_FOREACH(tools::wallet2::transfer_details& td, tc)
    {
      auto it = txs.find(get_transaction_hash(td.m_tx));
      CHECK_AND_ASSERT_MES(it != txs.end(), false, "transaction not found in local cache");
      it->second.m_received_count += 1;
    }

    BOOST_FOREACH(auto& tx_pair, txs)
    {
      if(tx_pair.second.m_received_count != 1)
      {
        LOG_PRINT_RED_L0("Transaction lost: " << get_transaction_hash(tx_pair.second.tx));
      }

    }

    LOG_PRINT_RED_L0("-----------------------FINISHING TRANSACTIONS FLOW TEST FAILED-----------------------" );
    LOG_PRINT_RED_L0("income " << print_money(money_2) << " via " << i << " transactions, expected money = " << print_money(transfered_money) );
    LOCAL_ASSERT(false);
    return false;
  }

  return true;
}

