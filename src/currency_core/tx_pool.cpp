// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "include_base_utils.h"
#include <algorithm>
#include <boost/filesystem.hpp>
#include <unordered_set>
#include <vector>

#include "tx_pool.h"
#include "currency_format_utils.h"
#include "currency_boost_serialization.h"
#include "currency_config.h"
#include "blockchain_storage.h"
#include "common/boost_serialization_helper.h"
#include "common/int-util.h"
#include "misc_language.h"
#include "crypto/hash.h"

namespace currency
{
  //---------------------------------------------------------------------------------
  tx_memory_pool::tx_memory_pool(blockchain_storage& bchs): m_blockchain(bchs)
  {

  }
  //---------------------------------------------------------------------------------
  bool  tx_memory_pool::remove_alias_tx_pair(crypto::hash id)
  {
	  CRITICAL_REGION_LOCAL(m_aliases_lock);
	  bool bFound = false;
	  for (auto it = m_aliases_to_txid.begin(); it != m_aliases_to_txid.end();)
	  {
		  crypto::hash hash = it->second;
		  if (hash == id)
		  {
			  m_aliases_to_txid.erase(it);
			  bFound = true;
			  break;
		  }
	  }
	  return bFound;
  }
  //---------------------------------------------------------------------------------
  crypto::hash  tx_memory_pool::find_alias(std::string & alias)
  {
	  //remove_alias
	  if (alias.size())
	  {
		  CRITICAL_REGION_LOCAL(m_aliases_lock);
		  auto it = m_aliases_to_txid.find(alias);
		  if (it != m_aliases_to_txid.end())
		  {
			  return it->second ;
		  }
	  }
	  return null_hash;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::add_alias_tx_pair(std::string & alias, crypto::hash id)
  {
	  //check alias repeat or not
	  if (alias.size())
	  {
		  crypto::hash h = find_alias(alias);
		  if (h != null_hash)
		  {
			  LOG_ERROR("the same alias " << alias << " exists in pool, id: " << h <<", so tx: "<< id << " can't be added to pool.");
			  return false;
		  }
		  m_aliases_to_txid[alias] = id;
	  }
	  return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::add_tx(const transaction &tx, const crypto::hash &id, tx_verification_context& tvc, bool kept_by_block,std::string alias)
  {    
    size_t blob_size = get_object_blobsize(tx);
    //#9Protection from big transaction flood
	if(!kept_by_block && blob_size > currency::get_max_transaction_blob_size(m_blockchain.get_current_blockchain_height()))
    {
      LOG_PRINT_L0("transaction is too big (" << blob_size << ")bytes for current transaction flow, tx_id: " << id);
      tvc.m_verifivation_failed = true;
      return false;
    }
    
    if(!check_inputs_types_supported(tx))
    {
      tvc.m_verifivation_failed = true;
      return false;
    }

    uint64_t inputs_amount = 0;
    if(!get_inputs_money_amount(tx, inputs_amount))
    {
      tvc.m_verifivation_failed = true;
      return false;
    }

    uint64_t outputs_amount = get_outs_money_amount(tx);

    if(outputs_amount >= inputs_amount)
    {
      LOG_PRINT_L0("transaction use more money then it has: use " << outputs_amount << ", have " << inputs_amount);
      tvc.m_verifivation_failed = true;
      return false;
    }

    //check key images for transaction if it is not kept by block
    if(!kept_by_block)
    {
      if(have_tx_keyimges_as_spent(tx))
      {
        LOG_ERROR("Transaction with id= "<< id << " used already spent key images");
        tvc.m_verifivation_failed = true;
        return false;
      }

      //transaction spam protection, soft rule
      if (inputs_amount - outputs_amount < TX_POOL_MINIMUM_FEE)
      {
        LOG_ERROR("Transaction with id= " << id << " has to small fee: " << inputs_amount - outputs_amount << ", expected fee: " << DEFAULT_FEE);
        tvc.m_verifivation_failed = true;
        return false;
      }
    }

    crypto::hash max_used_block_id = null_hash;
    uint64_t max_used_block_height = 0;
    bool ch_inp_res = m_blockchain.check_tx_inputs(tx, max_used_block_height, max_used_block_id);
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    if(!ch_inp_res)
    {
      if(kept_by_block)
      {
		  //if there is a same alias on the block, then delete the tx with the same alias in the pool
		  crypto::hash hash;
		  if (alias.size() && (hash = find_alias(alias)) != null_hash)
		  {
			  transaction tx = AUTO_VAL_INIT(tx);
			  size_t size = 0;
			  uint64_t  fee = 0;
			  take_tx(hash, tx, size, fee);
			  LOG_PRINT_L2("Found alias " << alias <<" in block, delete pool tx with the same alias: " << id );
		  }
        //anyway add this transaction to pool, because it related to block
        auto txd_p = m_transactions.insert(transactions_container::value_type(id, tx_details()));
        CHECK_AND_ASSERT_MES(txd_p.second, false, "transaction already exists at inserting in memory pool");
        txd_p.first->second.blob_size = blob_size;
        txd_p.first->second.tx = tx;
        txd_p.first->second.fee = inputs_amount - outputs_amount;
        txd_p.first->second.max_used_block_id = null_hash;
        txd_p.first->second.max_used_block_height = 0;
        txd_p.first->second.kept_by_block = kept_by_block;
        txd_p.first->second.receive_time = time(nullptr);
        tvc.m_verifivation_impossible = true;
        tvc.m_added_to_pool = true;
      }else
      {
        LOG_PRINT_L0("tx used wrong inputs, rejected");
        tvc.m_verifivation_failed = true;
        return false;
      }
    }
	else
    {
		//check alias repeat or not
		if (!add_alias_tx_pair(alias, id))
		{
			tvc.m_verifivation_failed = true;
			tvc.m_added_to_pool = false;
			return false;
		}

      //update transactions container
      auto txd_p = m_transactions.insert(transactions_container::value_type(id, tx_details()));
      CHECK_AND_ASSERT_MES(txd_p.second, false, "intrnal error: transaction already exists at inserting in memorypool");
      txd_p.first->second.blob_size = blob_size;
      txd_p.first->second.tx = tx;
      txd_p.first->second.kept_by_block = kept_by_block;
      txd_p.first->second.fee = inputs_amount - outputs_amount;
      txd_p.first->second.max_used_block_id = max_used_block_id;
      txd_p.first->second.max_used_block_height = max_used_block_height;
      txd_p.first->second.last_failed_height = 0;
      txd_p.first->second.last_failed_id = null_hash;
      txd_p.first->second.receive_time = time(nullptr);
      tvc.m_added_to_pool = true;

      if(txd_p.first->second.fee > 0)
        tvc.m_should_be_relayed = true;
    }

    tvc.m_verifivation_failed = true;
    //update image_keys container, here should everything goes ok.
    BOOST_FOREACH(const auto& in, tx.vin)
    {
      CHECKED_GET_SPECIFIC_VARIANT(in, const txin_to_key, txin, false);
      std::unordered_set<crypto::hash>& kei_image_set = m_spent_key_images[txin.k_image];
      CHECK_AND_ASSERT_MES(kept_by_block || kei_image_set.size() == 0, false, "internal error: keeped_by_block=" << kept_by_block
                                          << ",  kei_image_set.size()=" << kei_image_set.size() << ENDL << "txin.k_image=" << txin.k_image << ENDL
                                          << "tx_id=" << id );
      auto ins_res = kei_image_set.insert(id);
      CHECK_AND_ASSERT_MES(ins_res.second, false, "internal error: try to insert duplicate iterator in key_image set");
    }

    tvc.m_verifivation_failed = false;
    //succeed
    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::add_tx(const transaction &tx, tx_verification_context& tvc, bool keeped_by_block,std::string alias)
  {
    crypto::hash h = null_hash;
    get_transaction_hash(tx, h);
    return add_tx(tx, h, tvc, keeped_by_block,alias);
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::remove_transaction_keyimages(const transaction& tx)
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    BOOST_FOREACH(const txin_v& vi, tx.vin)
    {
      CHECKED_GET_SPECIFIC_VARIANT(vi, const txin_to_key, txin, false);
      auto it = m_spent_key_images.find(txin.k_image);
      CHECK_AND_ASSERT_MES(it != m_spent_key_images.end(), false, "failed to find transaction input in key images. img=" << txin.k_image << ENDL
                                    << "transaction id = " << get_transaction_hash(tx));
      std::unordered_set<crypto::hash>& key_image_set =  it->second;
      CHECK_AND_ASSERT_MES(key_image_set.size(), false, "empty key_image set, img=" << txin.k_image << ENDL
        << "transaction id = " << get_transaction_hash(tx));

      auto it_in_set = key_image_set.find(get_transaction_hash(tx));
      CHECK_AND_ASSERT_MES(it_in_set != key_image_set.end(), false, "transaction id not found in key_image set, img=" << txin.k_image << ENDL
        << "transaction id = " << get_transaction_hash(tx));
      key_image_set.erase(it_in_set);
      if(!key_image_set.size())
      {
        //it is now empty hash container for this key_image
        m_spent_key_images.erase(it);
      }

    }
    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::take_tx(const crypto::hash &id, transaction &tx, size_t& blob_size, uint64_t& fee)
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    auto it = m_transactions.find(id);
    if(it == m_transactions.end())
      return false;

    tx = it->second.tx;
    blob_size = it->second.blob_size;
    fee = it->second.fee;
    remove_transaction_keyimages(it->second.tx);
    m_transactions.erase(it);
	remove_alias_tx_pair(id);
    return true;
  }
  //---------------------------------------------------------------------------------
  void tx_memory_pool::on_idle()
  {
    m_remove_stuck_tx_interval.do_call([this](){return remove_stuck_transactions();});
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::remove_stuck_transactions()
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    for(auto it = m_transactions.begin(); it!= m_transactions.end();)
    {
      uint64_t tx_age = time(nullptr) - it->second.receive_time;

      if((tx_age > CURRENCY_MEMPOOL_TX_LIVETIME && !it->second.kept_by_block) || 
         (tx_age > CURRENCY_MEMPOOL_TX_FROM_ALT_BLOCK_LIVETIME && it->second.kept_by_block) )
      {
        LOG_PRINT_L0("Tx " << it->first << " removed from tx pool due to outdated, age: " << tx_age );
		remove_alias_tx_pair(it->first);
        m_transactions.erase(it++);
		
      }else
        ++it;
    }
    return true;
  }
  //---------------------------------------------------------------------------------
  size_t tx_memory_pool::get_transactions_count()
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    return m_transactions.size();
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::get_transactions(std::list<transaction>& txs)
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    BOOST_FOREACH(const auto& tx_vt, m_transactions)
      txs.push_back(tx_vt.second.tx);

    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::get_transaction(const crypto::hash& id, transaction& tx)
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    auto it = m_transactions.find(id);
    if(it == m_transactions.end())
      return false;
    tx = it->second.tx;
    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::on_blockchain_inc(uint64_t new_block_height, const crypto::hash& top_block_id)
  {
    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::on_blockchain_dec(uint64_t new_block_height, const crypto::hash& top_block_id)
  {
    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::have_tx(const crypto::hash &id)
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    if(m_transactions.count(id))
      return true;
    return false;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::have_tx_keyimges_as_spent(const transaction& tx)
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    BOOST_FOREACH(const auto& in, tx.vin)
    {
      CHECKED_GET_SPECIFIC_VARIANT(in, const txin_to_key, tokey_in, true);//should never fail
      if(have_tx_keyimg_as_spent(tokey_in.k_image))
         return true;
    }
    return false;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::have_tx_keyimg_as_spent(const crypto::key_image& key_im)
  {
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    return m_spent_key_images.end() != m_spent_key_images.find(key_im);
  }
  //---------------------------------------------------------------------------------
  void tx_memory_pool::lock()
  {
    m_transactions_lock.lock();
  }
  //---------------------------------------------------------------------------------
  void tx_memory_pool::unlock()
  {
    m_transactions_lock.unlock();
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::is_transaction_ready_to_go(tx_details& txd)
  {
    //not the best implementation at this time, sorry :(
    //check is ring_signature already checked ?
    if(txd.max_used_block_id == null_hash)
    {//not checked, lets try to check

      if(txd.last_failed_id != null_hash && m_blockchain.get_current_blockchain_height() > txd.last_failed_height && txd.last_failed_id == m_blockchain.get_block_id_by_height(txd.last_failed_height))
        return false;//we already sure that this tx is broken for this height

      if(!m_blockchain.check_tx_inputs(txd.tx, txd.max_used_block_height, txd.max_used_block_id))
      {
        txd.last_failed_height = m_blockchain.get_current_blockchain_height()-1;
        txd.last_failed_id = m_blockchain.get_block_id_by_height(txd.last_failed_height);
        return false;
      }
    }else
    {
      if(txd.max_used_block_height >= m_blockchain.get_current_blockchain_height())
        return false;
      if(m_blockchain.get_block_id_by_height(txd.max_used_block_height) != txd.max_used_block_id)
      {
        //if we already failed on this height and id, skip actual ring signature check
        if(txd.last_failed_id == m_blockchain.get_block_id_by_height(txd.last_failed_height))
          return false;
        //check ring signature again, it is possible (with very small chance) that this transaction become again valid
        if(!m_blockchain.check_tx_inputs(txd.tx, txd.max_used_block_height, txd.max_used_block_id))
        {
          txd.last_failed_height = m_blockchain.get_current_blockchain_height()-1;
          txd.last_failed_id = m_blockchain.get_block_id_by_height(txd.last_failed_height);
          return false;
        }
      }
    }
    //if we here, transaction seems valid, but, anyway, check for key_images collisions with blockchain, just to be sure
    if(m_blockchain.have_tx_keyimges_as_spent(txd.tx))
      return false;

    //transaction is ok.
    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::have_key_images(const std::unordered_set<crypto::key_image>& k_images, const transaction& tx)
  {
    for(size_t i = 0; i!= tx.vin.size(); i++)
    {
      CHECKED_GET_SPECIFIC_VARIANT(tx.vin[i], const txin_to_key, itk, false);
      if(k_images.count(itk.k_image))
        return true;
    }
    return false;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::append_key_images(std::unordered_set<crypto::key_image>& k_images, const transaction& tx)
  {
    for(size_t i = 0; i!= tx.vin.size(); i++)
    {
      CHECKED_GET_SPECIFIC_VARIANT(tx.vin[i], const txin_to_key, itk, false);
      auto i_res = k_images.insert(itk.k_image);
      CHECK_AND_ASSERT_MES(i_res.second, false, "internal error: key images pool cache - inserted duplicate image in set: " << itk.k_image);
    }
    return true;
  }
  //---------------------------------------------------------------------------------
  std::string tx_memory_pool::print_pool(bool short_format)
  {
    std::stringstream ss;
    CRITICAL_REGION_LOCAL(m_transactions_lock);
    BOOST_FOREACH(transactions_container::value_type& txe,  m_transactions)
    {
      if(short_format)
      {
        tx_details& txd = txe.second;
        ss << "id: " << txe.first << ENDL
          << "blob_size: " << txd.blob_size << ENDL
          << "fee: " << txd.fee << ENDL
          << "kept_by_block: " << (txd.kept_by_block ? "true":"false") << ENDL
          << "max_used_block_height: " << txd.max_used_block_height << ENDL
          << "max_used_block_id: " << txd.max_used_block_id << ENDL
          << "last_failed_height: " << txd.last_failed_height << ENDL
          << "last_failed_id: " << txd.last_failed_id << ENDL
          << "live_time: " << epee::misc_utils::get_time_interval_string(time(nullptr) - txd.receive_time) << ENDL;
      }else
      {
        tx_details& txd = txe.second;
        ss << "id: " << txe.first << ENDL
          <<  obj_to_json_str(txd.tx) << ENDL
          << "blob_size: " << txd.blob_size << ENDL
          << "fee: " << txd.fee << ENDL
          << "kept_by_block: " << (txd.kept_by_block ? "true":"false") << ENDL
          << "max_used_block_height: " << txd.max_used_block_height << ENDL
          << "max_used_block_id: " << txd.max_used_block_id << ENDL
          << "last_failed_height: " << txd.last_failed_height << ENDL
          << "last_failed_id: " << txd.last_failed_id << ENDL
          << "live_time: " << epee::misc_utils::get_time_interval_string(time(nullptr) - txd.receive_time) << ENDL;
      }

    }
    return ss.str();
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::fill_block_template(block &bl, size_t median_size, uint64_t already_generated_coins, uint64_t already_donated_coins, size_t &total_size, uint64_t &fee, uint64_t height) 
  {
    typedef transactions_container::value_type txv;
    CRITICAL_REGION_LOCAL(m_transactions_lock);

    std::vector<txv *> txs(m_transactions.size());
    std::transform(m_transactions.begin(), m_transactions.end(), txs.begin(), [](txv &a) -> txv * { return &a; });
    std::sort(txs.begin(), txs.end(), [](txv *a, txv *b) -> bool {
      uint64_t a_hi, a_lo = mul128(a->second.fee, b->second.blob_size, &a_hi);
      uint64_t b_hi, b_lo = mul128(b->second.fee, a->second.blob_size, &b_hi);
      return a_hi > b_hi || (a_hi == b_hi && a_lo > b_lo);
    });

    size_t current_size = 0;
    uint64_t current_fee = 0;
    uint64_t best_money;
    uint64_t max_donation = 0;
    if (!get_block_reward(median_size, CURRENCY_COINBASE_BLOB_RESERVED_SIZE, already_generated_coins, already_donated_coins, best_money, max_donation,height)) 
	{
      LOG_ERROR("Block with just a miner transaction is already too large!");
      return false;
    }
    size_t best_position = 0;
    total_size = 0;
    fee = 0;

    std::unordered_set<crypto::key_image> k_images;

    for (size_t i = 0; i < txs.size(); i++) 
	{
      txv &tx(*txs[i]);

      if(!is_transaction_ready_to_go(tx.second) || have_key_images(k_images, tx.second.tx)) 
	  {
        txs[i] = NULL;
        continue;
      }
      append_key_images(k_images, tx.second.tx);

      current_size += tx.second.blob_size;
      current_fee += tx.second.fee;

      uint64_t current_reward;
      if (!get_block_reward(median_size, current_size + CURRENCY_COINBASE_BLOB_RESERVED_SIZE, already_generated_coins, already_donated_coins, current_reward, max_donation,height)) 
	  {
        break;
      }

      if (best_money < current_reward + current_fee) 
	  {
        best_money = current_reward + current_fee;
        best_position = i + 1;
        total_size = current_size;
        fee = current_fee;
      }
    }

    for (size_t i = 0; i < best_position; i++) 
	{
      if (txs[i]) 
	  {
        bl.tx_hashes.push_back(txs[i]->first);
      }
    }

    return true;
  }
  //---------------------------------------------------------------------------------
  bool tx_memory_pool::init(const std::string& config_folder)
  {
    m_config_folder = config_folder;
    std::string state_file_path = config_folder + "/" + CURRENCY_POOLDATA_FILENAME;
    boost::system::error_code ec;
    if(!boost::filesystem::exists(state_file_path, ec))
      return true;
    bool res = tools::unserialize_obj_from_file(*this, state_file_path);
    if(!res)
    {
      LOG_PRINT_L0("Failed to load memory pool from file " << state_file_path);
    }
    return res;
  }

  //---------------------------------------------------------------------------------
  bool tx_memory_pool::deinit()
  {
    if (!tools::create_directories_if_necessary(m_config_folder))
    {
      LOG_PRINT_L0("Failed to create data directory: " << m_config_folder);
      return false;
    }

    std::string state_file_path = m_config_folder + "/" + CURRENCY_POOLDATA_FILENAME;
    bool res = tools::serialize_obj_to_file(*this, state_file_path);
    if(!res)
    {
      LOG_PRINT_L0("Failed to serialize memory pool to file " << state_file_path);
    }
    return true;
  }
}
