// Copyright (c) 2012-2013 The Cryptonote developers
// Copyright (c) 2012-2013 The Boolberry developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "include_base_utils.h"
#include <algorithm>
#include <cstdio>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include "currency_basic_impl.h"
#include "currency_boost_serialization.h"
#include "currency_format_utils.h"
#include "currency_config.h"
#include "miner.h"
#include "misc_language.h"
#include "profile_tools.h"
#include "file_io_utils.h"
#include "common/boost_serialization_helper.h"
#include "warnings.h"
#include "crypto/hash.h"
#include "miner_common.h"
#include "common/uint128_t.h"
#include "blockchain_storage.h"
#include "blockchain_storage_boost_serialization.h"

using namespace std;
using namespace epee;
using namespace currency;

DISABLE_VS_WARNINGS(4267)

namespace currency
{
	template<class Archive> void blockchain_storage::BlockCacheSerializer::serialize(Archive& ar, unsigned int version)
	{
		// ignore old versions, do rebuild
		if (version < 1)
		{
			LOG_PRINT_L0("Wrong version: " << version << " ,at least " << 1 << ", current version: " << CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER);
			return;
		}
		uint64_t nTmpHeight = HEIGHT_NOT_FOUND;
		uint64_t nHeight = m_nHeight;
		crypto::hash lastBlockHash = m_storedLastBlockHash;

		std::string operation;
		if (Archive::is_loading::value)
		{
			operation = "- loading ";
			ar & m_storedLastBlockHash;
			ar & m_nHeight;
			//try not to rebuild index and cache
			//if (m_nHeight > nHeight)
			//{
			//	LOG_PRINT_L0(operation << "block index error, index: " << m_nHeight + 1 << ", block size: " << nHeight + 1 << ", dnsd will rebuild internal stuctures from blocks. ");
			//	return;
			//}
		}
		else
		{
			operation = "- saving ";

			//wrong input height and block id
			nTmpHeight = m_bs.m_blocks_index.getBlockHeight(lastBlockHash);
			if (nHeight != nTmpHeight)
			{
				LOG_PRINT_L0(operation << "block index error, block id: " << lastBlockHash << ", index height " << m_nHeight << ", block height: " << nHeight);
				m_storedLastBlockHash = null_hash;
				m_nHeight = HEIGHT_NOT_FOUND;
				return;
			}

			//bigger than or equalt to block size, need to rebulid all blockcache.
			m_bs.fix_blocks_index();
			/*
			if (m_bs.m_blocks_index.size() != m_bs.m_blocks.size())
			{
				LOG_PRINT_L0(operation << "block index error, index: " << m_bs.m_blocks_index.size() << ", block size: " << m_bs.m_blocks.size() << ", please exit dnsd and restart it to rebuild internal stuctures... ");
				return;
			}*/
			m_storedLastBlockHash = lastBlockHash;
			m_nHeight = nHeight;

			ar & m_storedLastBlockHash;
			ar & m_nHeight;
		}

		LOG_PRINT_L1(operation << "block index...");
		ar & m_bs.m_blocks_index;

		if (Archive::is_loading::value && m_nHeight > nHeight)
		{
			LOG_PRINT_L0(operation << "block index error, index: try to fix it ");

			while (m_nHeight > nHeight)
			{
				m_bs.m_blocks_index.pop();
				m_nHeight--;
			}
		}


		if (Archive::is_loading::value)
		{
			//when loading, check the height and last block hash are correct or not
			nTmpHeight = m_bs.m_blocks_index.getBlockHeight(m_storedLastBlockHash);
			if (nTmpHeight != m_nHeight)
			{
				LOG_PRINT_L0(operation << "block index error, stored last block id: " << m_storedLastBlockHash << ", height is " << (nTmpHeight == HEIGHT_NOT_FOUND ? HEIGHT_NOT_FOUND : nHeight) << ", expect height: " << m_nHeight);
				m_storedLastBlockHash = null_hash;
				m_nHeight = HEIGHT_NOT_FOUND;
				m_bs.m_blocks_index.clear();
				return;
			}

			//if stored last block hash is not equal to current block hash, there is something wrong
			if (m_storedLastBlockHash != lastBlockHash)
			{
				//bigger than or equalt to block size, need to rebulid all blockcache.
				if (m_bs.m_blocks_index.size() >= m_bs.m_blocks.size())
				{
					m_bs.m_blocks_index.clear();
					m_storedLastBlockHash = null_hash;
					return;
				}
				else //(m_bs.m_blocks_index.size() < m_bs.m_blocks.size())
				{
					//less than block size, maybe we just need to rebuild missing indexes. Do nothing and go on load blockcache.
				}
			}
		}

		LOG_PRINT_L1(operation << "transaction map...");
		ar & m_bs.m_transactions;

		LOG_PRINT_L1(operation << "spend keys...");
		ar & m_bs.m_spent_keys;

		LOG_PRINT_L1(operation << "outputs...");
		ar & m_bs.m_outputs;

		LOG_PRINT_L1(operation << "aliases...");
		ar & m_bs.m_aliases;

		LOG_PRINT_L1(operation << "scratchpad...");
		ar & m_bs.m_scratchpad;

		LOG_PRINT_L1(operation << "others...");
		ar & m_bs.m_current_block_cumul_sz_limit;
		ar & m_bs.m_current_pruned_rs_height;

		if (version < CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER && Archive::is_loading::value)
		{
			m_loaded = true;
			return;
		}

		//simple count checksum
		uint64_t stored_total_count = 0;
		uint64_t total_count = m_bs.m_blocks_index.size() + m_bs.m_transactions.size() + m_bs.m_spent_keys.size() + \
			m_bs.m_outputs.size() + m_bs.m_aliases.size() + m_bs.m_scratchpad.size();

		if (Archive::is_saving::value)
		{
			ar & total_count;
		}
		else
		{
			ar & stored_total_count;
			if (total_count != stored_total_count)
			{
				LOG_PRINT_L0(operation << "block cache error, stored count: " << stored_total_count << ", expect count: " << total_count);
				m_bs.clear_all_cache_data();
				m_bs.m_current_pruned_rs_height = 0;
				m_bs.m_current_block_cumul_sz_limit = 0;
				return;
			}
		}

		m_loaded = true;
	}
}

//------------------------------------------------------------------
blockchain_storage::blockchain_storage(tx_memory_pool& tx_pool):m_tx_pool(tx_pool), 
                                                                m_current_block_cumul_sz_limit(0), 
                                                                m_is_in_checkpoint_zone(false), 
                                                                m_donations_account(AUTO_VAL_INIT(m_donations_account)), 
                                                                m_royalty_account(AUTO_VAL_INIT(m_royalty_account)),
                                                                m_is_blockchain_storing(false), 
																m_is_blockchain_transforming(false),
                                                                m_current_pruned_rs_height(0)
{
  bool r = get_donation_accounts(m_donations_account, m_royalty_account);
  CHECK_AND_ASSERT_THROW_MES(r, "failed to load donation accounts");
}
//------------------------------------------------------------------
bool blockchain_storage::have_tx(const crypto::hash &id)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  return m_transactions.find(id) != m_transactions.end();
}
//------------------------------------------------------------------
bool blockchain_storage::have_tx_keyimg_as_spent(const crypto::key_image &key_im)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  return  m_spent_keys.find(key_im) != m_spent_keys.end();
}
//------------------------------------------------------------------
transaction *blockchain_storage::get_tx(const crypto::hash &id)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  auto it = m_transactions.find(id);
  if (it == m_transactions.end())
    return NULL;

  return &transactionByIndex(it->second).tx;
}
//------------------------------------------------------------------
uint64_t blockchain_storage::get_current_blockchain_height()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  return m_blocks.size();
}
//------------------------------------------------------------------
void blockchain_storage::fill_addr_to_alias_dict()
{
  for (const auto& a : m_aliases)
  {
    if (a.second.size())
    {
      m_addr_to_alias[a.second.back().m_address] = a.first;
    }
  }
}
//------------------------------------------------------------------
bool blockchain_storage::rebuildcache(uint64_t start_height)
{
	CRITICAL_REGION_LOCAL(m_blockchain_lock);
	m_is_blockchain_transforming = true;
	std::chrono::steady_clock::time_point timePoint = std::chrono::steady_clock::now();
	bool bReturn = false;

	for (uint64_t b = start_height; b < m_blocks.size(); ++b)
	{
		if (b % 1000 == 0)
		{
			std::cout << "Height " << b << " of " << m_blocks.size() << '\r';
		}
		const BlockEntry& block = m_blocks[b];
		crypto::hash blockHash = get_block_hash(block.bl);

		m_blocks_index.push(blockHash);
		for (uint16_t t = 0; t < block.transactions.size(); ++t)
		{
			const TransactionEntry& transaction = block.transactions[t];
			crypto::hash transactionHash = get_transaction_hash(transaction.tx);

			bool r = process_blockchain_tx_extra(transaction.tx);
			if (r == false)
			{
				LOG_PRINT_L0("failed to process_blockchain_tx_extra, block_id: " << blockHash << ", tx_id = " << transactionHash); 
				break;
			}

			TransactionIndex transactionIndex = { b, t };
			m_transactions.insert(std::make_pair(transactionHash, transactionIndex));

			//m_spent_keys
			for (auto & i : transaction.tx.vin)
			{
				if (i.type() == typeid(txin_to_key))
				{
					m_spent_keys.insert(boost::get<txin_to_key>(i).k_image);
				}
			}
			//m_outputs
			for (uint16_t o = 0; o < transaction.tx.vout.size(); ++o)
			{
				m_outputs[transaction.tx.vout[o].amount].push_back(std::make_pair<>(transactionIndex, o));
			}
		}

		//append to scratchpad
		if (!push_block_scratchpad_data(block.bl, m_scratchpad))
		{
			LOG_PRINT_L0("failed to push_block_scratchpad_data, block_id: " << blockHash); 
			break;
		}
	}
	bReturn = true;
	m_is_blockchain_transforming = false;
	std::chrono::duration<double> duration = std::chrono::steady_clock::now() - timePoint;

	if (bReturn)
	{
		LOG_PRINT_L0("Rebuilding internal structures took: " << duration.count());
	}
	else
	{
		LOG_PRINT_RED("Failed to rebuild internal structures, blocks and indexes files error,please exit dnsd, delete all blockchain data and restart dnsd.", LOG_LEVEL_0);
	}
	return bReturn;
}

//--------------------------------------------------------------------
bool blockchain_storage::init(const std::string& config_folder)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  if (!config_folder.empty() && !tools::create_directories_if_necessary(config_folder)) 
  {
	  LOG_ERROR("Failed to create data directory: " << config_folder);
	  return false;
  }

  m_config_folder = config_folder;

  if (!m_blocks.open(tools::appendPath(config_folder, CURRENCY_BLOCKS_FILENAME), tools::appendPath(config_folder, CURRENCY_BLOCKINDEX_FILENAME), 102400))
  {
	  LOG_ERROR("Open blockchain file and index file error, maybe it is in use. ");
	  return false;
  }

  LOG_PRINT_L0("Loading blockchain...");
  if (m_blocks.empty()) 
  {
	  const std::string filename = tools::appendPath(m_config_folder, CURRENCY_BLOCKCHAINDATA_FILENAME);
	  if (!tools::unserialize_obj_from_file(*this, filename)) 
	  {
		  LOG_PRINT_L0("Can't load blockchain storage from file.");
		  m_blocks.clear();
		  clear_all_cache_data();
	  }
	  else
	  {
		  LOG_PRINT_L0("Rebuild internal structures...");
		  clear_all_cache_data();
		  CHECK_AND_ASSERT_MES(rebuildcache(), false, "Failed to rebuild cache from old blockchain.");
		  store_blockchain();
	  }
  }
  else 
  {
	  auto hash = get_block_hash(m_blocks.back().bl);
	  BlockCacheSerializer loader(*this, hash, m_blocks.back().height);
	  tools::unserialize_obj_from_file(loader, tools::appendPath(config_folder, CURRENCY_BLOCKCACHE_FILENAME));

	  if (!loader.loaded() || hash != loader.getStoredLastBlockHash())
	  {
		  uint32_t b = 0;
		  if (!loader.loaded())
		  {
			  LOG_PRINT_L0("No actual blockchain cache found, rebuilding internal structures...");
		  }
		  else
		  {
			  b = loader.getStoredHeight();
			  if (b < m_blocks.size()-1)
			  {
				  LOG_PRINT_L0("Abnormal exit detected, rebuilding internal structures from height " << b);
				  b++;
			  }
			  else 
			  {
				  LOG_PRINT_L0("Wrong index  " << b << " found,  while hashes not corresponding, rebuilding internal structures from height 0...");
				  b = 0;
			  }

			  std::cout << "Height " << b << " of " << m_blocks.size() << '\r';
		  }

		  CHECK_AND_ASSERT_MES(rebuildcache(b), false, "Failed to rebuild cache.");
		  store_blockchain();
	  }
  }
  block bl = boost::value_initialized<block>();
  generate_genesis_block(bl);

  if (m_blocks.empty()) 
  {
	  LOG_PRINT_L0("Blockchain not loaded, generating genesis block.");
	  block_verification_context bvc = boost::value_initialized<block_verification_context>();
	  add_new_block(bl, bvc);
	  CHECK_AND_ASSERT_MES(!bvc.m_verifivation_failed && bvc.m_added_to_main_chain, false, "Failed to add genesis block to blockchain");
  }
  else 
  {
	  crypto::hash firstBlockHash = get_block_hash(m_blocks[0].bl);
	  crypto::hash id = get_block_hash(bl);
	  CHECK_AND_ASSERT_MES(firstBlockHash == id, false,
		  "Failed to init: genesis block mismatch. Probably you set --testnet flag with data dir with non-test blockchain or another network.");
  }

  update_next_comulative_size_limit();
  m_is_in_checkpoint_zone = m_checkpoints.is_in_checkpoint_zone(get_current_blockchain_height());

  uint64_t timestamp_diff = time(NULL) - m_blocks.back().bl.timestamp;
  if (!m_blocks.back().bl.timestamp) 
  {
	  timestamp_diff = time(NULL) - 1341378000;
  }
  fill_addr_to_alias_dict();
  currency::wide_difficulty_type diff = get_difficulty_for_next_block();
  LOG_PRINT_GREEN("Blockchain initialized. last block: " << m_blocks.size() - 1 << ", " << epee::misc_utils::get_time_interval_string(timestamp_diff)  \
	  << " time ago, current difficulty: " << diff, LOG_LEVEL_0);
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::store_blockchain()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  m_is_blockchain_storing = true;

  LOG_PRINT_L0("Storing blockchain...");
  BlockCacheSerializer ser(*this, get_top_block_id(),m_blocks.back().height );
  if (!tools::serialize_obj_to_file(ser, tools::appendPath(m_config_folder, CURRENCY_BLOCKCACHE_FILENAME)))
  {
	  LOG_ERROR("Failed to save blockchain cache");
	  m_is_blockchain_storing = false;
	  return false;
  }
  m_is_blockchain_storing = false;
  LOG_PRINT_L0("Blockchain cache stored OK.");
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::deinit()
{
  return store_blockchain();
}
//------------------------------------------------------------------
bool blockchain_storage::pop_block_from_blockchain()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  CHECK_AND_ASSERT_MES(m_blocks.size() > 1, false, "pop_block_from_blockchain: can't pop from blockchain with size = " << m_blocks.size());
  size_t h = m_blocks.size()-1;
  BlockEntry bei = m_blocks[h];
  pop_block_scratchpad_data(bei.bl, m_scratchpad);
  //crypto::hash id = get_block_hash(bei.bl);
  bool r = purge_block_data_from_blockchain(bei.bl, bei.bl.tx_hashes.size());
  CHECK_AND_ASSERT_MES(r, false, "Failed to purge_block_data_from_blockchain for block " << get_block_hash(bei.bl) << " on height " << h);

  //remove from index
  auto bl_ind = m_blocks_index.getTailId();
  CHECK_AND_ASSERT_MES(bl_ind != get_block_hash(bei.bl), false, "pop_block_from_blockchain: blockchain id not found in index");
  m_blocks_index.pop();
  //pop block from core
  m_blocks.pop_back();
  m_tx_pool.on_blockchain_dec(m_blocks.size()-1, get_top_block_id());
  return true;
}
//------------------------------------------------------------------
void blockchain_storage::set_checkpoints(checkpoints&& chk_pts) 
{
  m_checkpoints = chk_pts;
  prune_ring_signatures_if_need();
}
//------------------------------------------------------------------
blockchain_storage::TransactionEntry& blockchain_storage::transactionByIndex(TransactionIndex index) 
{
	return (TransactionEntry&)m_blocks[index.block].transactions[index.transaction];
}
//------------------------------------------------------------------
bool blockchain_storage::prune_ring_signatures(uint64_t height, uint64_t& transactions_pruned, uint64_t& signatures_pruned)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  CHECK_AND_ASSERT_MES(height < m_blocks.size(), false, "prune_ring_signatures called with wrong parametr " << height << ", m_blocks.size() " << m_blocks.size());
  for(const auto& h: m_blocks[height].bl.tx_hashes)
  {
    auto it = m_transactions.find(h);
    CHECK_AND_ASSERT_MES(it != m_transactions.end(), false, "failed to find transaction " << h << " in blockchain index, in block on height = " << height);    
    
	TransactionEntry& txe = transactionByIndex(it->second);
	CHECK_AND_ASSERT_MES(txe.m_keeper_block_height == height, false,
		"failed to validate extra check, it->second.m_keeper_block_height = " << txe.m_keeper_block_height <<
      "is mot equal to height = " << height << " in blockchain index, for block on height = " << height);
    
	signatures_pruned += txe.tx.signatures.size();
	txe.tx.signatures.clear();
    ++transactions_pruned;
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::prune_ring_signatures_if_need()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(m_blocks.size() && m_checkpoints.get_top_checkpoint_height() && m_checkpoints.get_top_checkpoint_height() > m_current_pruned_rs_height)
  {    
    LOG_PRINT_CYAN("Starting pruning ring signatues...", LOG_LEVEL_0);
    uint64_t tx_count = 0, sig_count = 0;
    for(uint64_t height = m_current_pruned_rs_height; height < m_blocks.size() && height <= m_checkpoints.get_top_checkpoint_height(); height++)
    {
      bool res = prune_ring_signatures(height, tx_count, sig_count);
      CHECK_AND_ASSERT_MES(res, false, "filed to prune_ring_signatures for height = " << height);
    }
    m_current_pruned_rs_height = m_checkpoints.get_top_checkpoint_height();
    LOG_PRINT_CYAN("Transaction pruning finished: " << sig_count << " signatures released in " << tx_count << " transactions.", LOG_LEVEL_0);
  }
  return true;
}
//------------------------------------------------------------------
void blockchain_storage::clear_all_cache_data()
{
	CRITICAL_REGION_LOCAL(m_blockchain_lock);
	m_transactions.clear();
	m_spent_keys.clear();
	m_blocks_index.clear();
	m_alternative_chains.clear();
	m_outputs.clear();
	m_scratchpad.clear();
	m_aliases.clear();
	m_addr_to_alias.clear();
	return;
}

bool blockchain_storage::reset_and_set_genesis_block(const block& b)
{
  clear_all_cache_data();
  m_blocks.clear();
  block_verification_context bvc = boost::value_initialized<block_verification_context>();
  add_new_block(b, bvc);
  return bvc.m_added_to_main_chain && !bvc.m_verifivation_failed;
}
//------------------------------------------------------------------
//TODO: not the best way, add later update method instead of full copy
bool blockchain_storage::copy_scratchpad(std::vector<crypto::hash>& scr)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  scr = m_scratchpad;
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::copy_scratchpad(std::string& dst)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if (m_scratchpad.size())
  {
    dst.append(reinterpret_cast<const char*>(&m_scratchpad[0]), m_scratchpad.size() * 32);
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::purge_transaction_keyimages_from_blockchain(const transaction& tx, bool strict_check)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
    struct purge_transaction_visitor: public boost::static_visitor<bool>
  {
    blockchain_storage& m_bcs;
    key_images_container& m_spent_keys;
    bool m_strict_check;
    purge_transaction_visitor(blockchain_storage& bcs, key_images_container& spent_keys, bool strict_check):
      m_bcs(bcs),
      m_spent_keys(spent_keys), 
      m_strict_check(strict_check){}

    bool operator()(const txin_to_key& inp) const
    {
      //const crypto::key_image& ki = inp.k_image;
      auto r = m_spent_keys.find(inp.k_image);
      if(r != m_spent_keys.end())
      {
        m_spent_keys.erase(r);
      }else
      {
        CHECK_AND_ASSERT_MES(!m_strict_check, false, "purge_block_data_from_blockchain: key image in transaction not found");
      }

      if(inp.key_offsets.size() == 1)
      {
        //direct spend detected
        if(!m_bcs.update_spent_tx_flags_for_input(inp.amount, inp.key_offsets[0], false))
        {
          //internal error
          LOG_PRINT_L0("Failed to  update_spent_tx_flags_for_input");
          return false;
        }
      }

      return true;
    }
    bool operator()(const txin_gen& inp) const
    {
      return true;
    }
    bool operator()(const txin_to_script& tx) const
    {
      return false;
    }

    bool operator()(const txin_to_scripthash& tx) const
    {
      return false;
    }
  };

  BOOST_FOREACH(const txin_v& in, tx.vin)
  {
    bool r = boost::apply_visitor(purge_transaction_visitor(*this, m_spent_keys, strict_check), in);
    CHECK_AND_ASSERT_MES(!strict_check || r, false, "failed to process purge_transaction_visitor");
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::purge_transaction_from_blockchain(const crypto::hash& tx_id)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  auto tx_index_it = m_transactions.find(tx_id);
  CHECK_AND_ASSERT_MES(tx_index_it != m_transactions.end(), false, "purge_block_data_from_blockchain: transaction not found in blockchain index!!");
  transaction& tx = transactionByIndex(tx_index_it->second).tx;

  purge_transaction_keyimages_from_blockchain(tx, true);
  
  bool r = unprocess_blockchain_tx_extra(tx);
  CHECK_AND_ASSERT_MES(r, false, "failed to unprocess_blockchain_tx_extra");

  if(!is_coinbase(tx))
  {
    currency::tx_verification_context tvc = AUTO_VAL_INIT(tvc);
    bool r = m_tx_pool.add_tx(tx, tvc, true);
    CHECK_AND_ASSERT_MES(r, false, "purge_block_data_from_blockchain: failed to add transaction to transaction pool");
  }

  bool res = pop_transaction_from_global_index(tx, tx_id);
  m_transactions.erase(tx_index_it);
  LOG_PRINT_L1("Removed transaction from blockchain history:" << tx_id << ENDL);
  return res;
}
//------------------------------------------------------------------
bool blockchain_storage::purge_block_data_from_blockchain(const block& bl, size_t processed_tx_count)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  bool res = true;
  CHECK_AND_ASSERT_MES(processed_tx_count <= bl.tx_hashes.size(), false, "wrong processed_tx_count in purge_block_data_from_blockchain");
  for(size_t count = 0; count != processed_tx_count; count++)
  {
    res = purge_transaction_from_blockchain(bl.tx_hashes[(processed_tx_count -1)- count]) && res;
  }

  res = purge_transaction_from_blockchain(get_transaction_hash(bl.miner_tx)) && res;
  return res;
}
//------------------------------------------------------------------
crypto::hash blockchain_storage::get_top_block_id(uint64_t& height)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  height = get_current_blockchain_height()-1;
  return get_top_block_id();
}
//------------------------------------------------------------------
crypto::hash blockchain_storage::get_top_block_id()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  crypto::hash id = null_hash;
  if(m_blocks.size())
  {
    get_block_hash(m_blocks.back().bl, id);
  }
  return id;
}
//------------------------------------------------------------------
bool blockchain_storage::get_top_block(block& b)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  CHECK_AND_ASSERT_MES(m_blocks.size(), false, "Wrong blockchain state, m_blocks.size()=0!");
  b = m_blocks.back().bl;
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_short_chain_history(std::list<crypto::hash>& ids)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  size_t i = 0;
  size_t current_multiplier = 1;
  size_t sz = m_blocks.size();
  if(!sz)
    return true;
  size_t current_back_offset = 1;
  bool genesis_included = false;
  while(current_back_offset < sz)
  {
    ids.push_back(get_block_hash(m_blocks[sz-current_back_offset].bl));
    if(sz-current_back_offset == 0)
      genesis_included = true;
    if(i < 10)
    {
      ++current_back_offset;
    }else
    {
      current_back_offset += current_multiplier *= 2;
    }
    ++i;
  }
  if(!genesis_included)
    ids.push_back(get_block_hash(m_blocks[0].bl));

  return true;
}
//------------------------------------------------------------------
crypto::hash blockchain_storage::get_block_id_by_height(uint64_t height)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(height >= m_blocks.size())
    return null_hash;

  return get_block_hash(m_blocks[height].bl);
}
//------------------------------------------------------------------
bool blockchain_storage::get_block_by_hash(const crypto::hash &h, block &blk) {
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  // try to find block in main chain

  if (m_blocks_index.hasBlock(h))
  {
	  uint64_t nHeight = -1;
	  m_blocks_index.getBlockHeight(h,nHeight);
	  blk = m_blocks[nHeight].bl;
    return true;
  }

  // try to find block in alternative chain
  auto it_alt = m_alternative_chains.find(h);
  if (m_alternative_chains.end() != it_alt) 
  {
    blk = it_alt->second.bl;
    return true;
  }

  return false;
}
//------------------------------------------------------------------
bool blockchain_storage::get_block_by_height(uint64_t h, block &blk)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(h >= m_blocks.size() )
    return false;
  blk = m_blocks[h].bl;
  return true;
}
//------------------------------------------------------------------
void blockchain_storage::get_all_known_block_ids(std::list<crypto::hash> &main, std::list<crypto::hash> &alt, std::list<crypto::hash> &invalid) {
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  m_blocks_index.getBlockIds(0, m_blocks.size(), main);

  BOOST_FOREACH(AlternativeChains::value_type &v, m_alternative_chains)
    alt.push_back(v.first);

  BOOST_FOREACH(AlternativeChains::value_type &v, m_invalid_blocks)
    invalid.push_back(v.first);
}
//------------------------------------------------------------------
wide_difficulty_type blockchain_storage::get_difficulty_for_next_block()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  std::vector<uint64_t> timestamps;
  std::vector<wide_difficulty_type> commulative_difficulties;
  size_t offset = m_blocks.size() - std::min((size_t)m_blocks.size(), static_cast<size_t>(DIFFICULTY_BLOCKS_COUNT));
  if(!offset)
    ++offset;//skip genesis block
  for(; offset < m_blocks.size(); offset++)
  {
    timestamps.push_back(m_blocks[offset].bl.timestamp);
    commulative_difficulties.push_back(m_blocks[offset].cumulative_difficulty);
  }
  return next_difficulty(timestamps, commulative_difficulties);
}
//------------------------------------------------------------------
bool blockchain_storage::rollback_blockchain_switching(std::list<block>& original_chain, size_t rollback_height)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  //remove failed subchain
  for(size_t i = m_blocks.size()-1; i >=rollback_height; i--)
  {
    bool r = pop_block_from_blockchain();
    CHECK_AND_ASSERT_MES(r, false, "PANIC!!! failed to remove block while chain switching during the rollback!");
  }
  //return back original chain
  BOOST_FOREACH(auto& bl, original_chain)
  {
    block_verification_context bvc = boost::value_initialized<block_verification_context>();
    bool r = handle_block_to_main_chain(bl, bvc);
    CHECK_AND_ASSERT_MES(r && bvc.m_added_to_main_chain, false, "PANIC!!! failed to add (again) block while chain switching during the rollback!");
  }

  LOG_PRINT_L0("Rollback success.");
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::switch_to_alternative_blockchain(std::list<AlternativeChains::iterator>& alt_chain)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  CHECK_AND_ASSERT_MES(alt_chain.size(), false, "switch_to_alternative_blockchain: empty chain passed");

  size_t split_height = alt_chain.front()->second.height;
  CHECK_AND_ASSERT_MES(m_blocks.size() > split_height, false, "switch_to_alternative_blockchain: blockchain size is lower than split height");

  //disconnecting old chain
  std::list<block> disconnected_chain;
  for(size_t i = m_blocks.size()-1; i >=split_height; i--)
  {
    block b = m_blocks[i].bl;
    bool r = pop_block_from_blockchain();
    CHECK_AND_ASSERT_MES(r, false, "failed to remove block on chain switching");
    disconnected_chain.push_front(b);
  }

  //connecting new alternative chain
  for(auto alt_ch_iter = alt_chain.begin(); alt_ch_iter != alt_chain.end(); alt_ch_iter++)
  {
    auto ch_ent = *alt_ch_iter;
    block_verification_context bvc = boost::value_initialized<block_verification_context>();
    bool r = handle_block_to_main_chain(ch_ent->second.bl, bvc);
    if(!r || !bvc.m_added_to_main_chain)
    {
      LOG_PRINT_L0("Failed to switch to alternative blockchain");
      rollback_blockchain_switching(disconnected_chain, split_height);
      add_block_as_invalid(ch_ent->second, get_block_hash(ch_ent->second.bl));
      LOG_PRINT_L0("The block was inserted as invalid while connecting new alternative chain,  block_id: " << get_block_hash(ch_ent->second.bl));
      m_alternative_chains.erase(ch_ent);

      for(auto alt_ch_to_orph_iter = ++alt_ch_iter; alt_ch_to_orph_iter != alt_chain.end(); alt_ch_to_orph_iter++)
      {
        //block_verification_context bvc = boost::value_initialized<block_verification_context>();
        add_block_as_invalid((*alt_ch_iter)->second, (*alt_ch_iter)->first);
        m_alternative_chains.erase(*alt_ch_to_orph_iter);
      }
      return false;
    }
  }

  //pushing old chain as alternative chain
  BOOST_FOREACH(auto& old_ch_ent, disconnected_chain)
  {
    block_verification_context bvc = boost::value_initialized<block_verification_context>();
    bool r = handle_alternative_block(old_ch_ent, get_block_hash(old_ch_ent), bvc);
    if(!r)
    {
      LOG_ERROR("Failed to push ex-main chain blocks to alternative chain ");
      rollback_blockchain_switching(disconnected_chain, split_height);
      return false;
    }
  }

  //removing all_chain entries from alternative chain
  BOOST_FOREACH(auto ch_ent, alt_chain)
  {
    m_alternative_chains.erase(ch_ent);
  }

  LOG_PRINT_GREEN("REORGANIZE SUCCESS! on height: " << split_height << ", new blockchain size: " << m_blocks.size(), LOG_LEVEL_0);
  return true;
}
//------------------------------------------------------------------
wide_difficulty_type blockchain_storage::get_next_difficulty_for_alternative_chain(const std::list<AlternativeChains::iterator>& alt_chain, BlockEntry& bei)
{
  std::vector<uint64_t> timestamps;
  std::vector<wide_difficulty_type> commulative_difficulties;
  if(alt_chain.size()< DIFFICULTY_BLOCKS_COUNT)
  {
    CRITICAL_REGION_LOCAL(m_blockchain_lock);
    size_t main_chain_stop_offset = alt_chain.size() ? alt_chain.front()->second.height : bei.height;
    size_t main_chain_count = DIFFICULTY_BLOCKS_COUNT - std::min(static_cast<size_t>(DIFFICULTY_BLOCKS_COUNT), alt_chain.size());
    main_chain_count = std::min(main_chain_count, main_chain_stop_offset);
    size_t main_chain_start_offset = main_chain_stop_offset - main_chain_count;

    if(!main_chain_start_offset)
      ++main_chain_start_offset; //skip genesis block
    for(; main_chain_start_offset < main_chain_stop_offset; ++main_chain_start_offset)
    {
      timestamps.push_back(m_blocks[main_chain_start_offset].bl.timestamp);
      commulative_difficulties.push_back(m_blocks[main_chain_start_offset].cumulative_difficulty);
    }

    CHECK_AND_ASSERT_MES((alt_chain.size() + timestamps.size()) <= DIFFICULTY_BLOCKS_COUNT, false, "Internal error, alt_chain.size()["<< alt_chain.size()
                                                                                    << "] + vtimestampsec.size()[" << timestamps.size() << "] NOT <= DIFFICULTY_WINDOW[]" << DIFFICULTY_BLOCKS_COUNT );
    BOOST_FOREACH(auto it, alt_chain)
    {
      timestamps.push_back(it->second.bl.timestamp);
      commulative_difficulties.push_back(it->second.cumulative_difficulty);
    }
  }else
  {
    timestamps.resize(std::min(alt_chain.size(), static_cast<size_t>(DIFFICULTY_BLOCKS_COUNT)));
    commulative_difficulties.resize(std::min(alt_chain.size(), static_cast<size_t>(DIFFICULTY_BLOCKS_COUNT)));
    size_t count = 0;
    size_t max_i = timestamps.size()-1;
    BOOST_REVERSE_FOREACH(auto it, alt_chain)
    {
      timestamps[max_i - count] = it->second.bl.timestamp;
      commulative_difficulties[max_i - count] = it->second.cumulative_difficulty;
      count++;
      if(count >= DIFFICULTY_BLOCKS_COUNT)
        break;
    }
  }
  return next_difficulty(timestamps, commulative_difficulties);
}
//------------------------------------------------------------------
bool blockchain_storage::prevalidate_miner_transaction(const block& b, uint64_t height)
{
  CHECK_AND_ASSERT_MES(b.miner_tx.vin.size() == 1, false, "coinbase transaction in the block has no inputs");
  CHECK_AND_ASSERT_MES(b.miner_tx.vin[0].type() == typeid(txin_gen), false, "coinbase transaction in the block has the wrong type");
  if(boost::get<txin_gen>(b.miner_tx.vin[0]).height != height)
  {
    LOG_PRINT_RED_L0("The miner transaction in block has invalid height: " << boost::get<txin_gen>(b.miner_tx.vin[0]).height << ", expected: " << height);
    return false;
  }
  CHECK_AND_ASSERT_MES(b.miner_tx.unlock_time == height + CURRENCY_MINED_MONEY_UNLOCK_WINDOW,
                  false,
                  "coinbase transaction transaction have wrong unlock time=" << b.miner_tx.unlock_time << ", expected " << height + CURRENCY_MINED_MONEY_UNLOCK_WINDOW);

  //check outs overflow
  if(!check_outs_overflow(b.miner_tx))
  {
    LOG_PRINT_RED_L0("miner transaction have money overflow in block " << get_block_hash(b));
    return false;
  }

  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::lookfor_donation(const transaction& tx, uint64_t& donation, uint64_t& royalty)
{
  crypto::public_key coin_base_tx_pub_key = null_pkey;
  bool r = parse_and_validate_tx_extra(tx, coin_base_tx_pub_key);
  CHECK_AND_ASSERT_MES(r, false, "Failed to validate coinbase extra");

  if(tx.vout.size() >= 2)
  {
    //check donations value
    size_t i = tx.vout.size()-2;
    CHECK_AND_ASSERT_MES(tx.vout[i].target.type() ==  typeid(txout_to_key), false, "wrong type id in transaction out" );
    if(is_out_to_acc(m_donations_account, boost::get<txout_to_key>(tx.vout[i].target), coin_base_tx_pub_key, i)  )
    {
      donation = tx.vout[i].amount;
    }
  }
  if(tx.vout.size() >= 1)
  {
    size_t i = tx.vout.size()-1;
    CHECK_AND_ASSERT_MES(tx.vout[i].target.type() ==  typeid(txout_to_key), false, "wrong type id in transaction out" );
    if(donation)
    {
      //donations already found, check royalty
      if(is_out_to_acc(m_royalty_account, boost::get<txout_to_key>(tx.vout[i].target), coin_base_tx_pub_key, i)  )
      {
        royalty = tx.vout[i].amount;
      }
    }else
    {
      //only donations, without royalty 
      if(is_out_to_acc(m_donations_account, boost::get<txout_to_key>(tx.vout[i].target), coin_base_tx_pub_key, i)  )
      {
        donation = tx.vout[i].amount;
      }
    }
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_required_donations_value_for_next_block(uint64_t& don_am)
{
  TRY_ENTRY();
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  uint64_t sz = get_current_blockchain_height();
  if(sz < CURRENCY_DONATIONS_INTERVAL || sz%CURRENCY_DONATIONS_INTERVAL)
  {
    LOG_ERROR("internal error: validate_donations_value at wrong height: " << get_current_blockchain_height());
    return false;
  }
  std::vector<bool> donation_votes;
  for(size_t i = m_blocks.size() - CURRENCY_DONATIONS_INTERVAL; i!= m_blocks.size(); i++)
    donation_votes.push_back(!(m_blocks[i].bl.flags&BLOCK_FLAGS_SUPPRESS_DONATION));

  don_am = get_donations_anount_for_day(m_blocks.back().already_donated_coins, donation_votes);
  return true;
  
  CATCH_ENTRY_L0("blockchain_storage::validate_donations_value", false);
}
//------------------------------------------------------------------
bool blockchain_storage::validate_donations_value(uint64_t donation, uint64_t royalty)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  uint64_t expected_don_total = 0;
  if(!get_required_donations_value_for_next_block(expected_don_total))
    return false;

  CHECK_AND_ASSERT_MES(donation + royalty == expected_don_total, false, "Wrong donations amount: " << donation + royalty << ", expected " << expected_don_total);
  uint64_t expected_donation = 0;
  uint64_t expected_royalty = 0;
  get_donation_parts(expected_don_total, expected_royalty, expected_donation);
  CHECK_AND_ASSERT_MES(expected_royalty == royalty && expected_donation == donation, 
                       false, 
                       "wrong donation parts: " << donation << "/" << royalty << ", expected: " << expected_donation << "/" << expected_royalty);
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::validate_miner_transaction(const block& b, size_t cumulative_block_size, uint64_t fee, uint64_t& base_reward, uint64_t already_generated_coins, uint64_t already_donated_coins, uint64_t& donation_total)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  //validate reward
  uint64_t money_in_use = 0;
  uint64_t royalty = 0;
  uint64_t donation = 0;

  //donations should be last one 
  //size_t outs_total = b.miner_tx.vout.size();
  BOOST_FOREACH(auto& o, b.miner_tx.vout)
  {
    money_in_use += o.amount;
  }
  uint64_t h = get_block_height(b);
  
  bool r = false;
  //check every block
  if(h >= CURRENCY_DONATIONS_START_BLOCKNO)
  {
    r = lookfor_donation(b.miner_tx, donation, royalty);
    CHECK_AND_ASSERT_MES(r, false, "Failed to lookfor_donation");   
  }

  std::vector<size_t> last_blocks_sizes;
  get_last_n_blocks_sizes(last_blocks_sizes, CURRENCY_REWARD_BLOCKS_WINDOW);
  uint64_t max_donation = 0;
  if(!get_block_reward(misc_utils::median(last_blocks_sizes), cumulative_block_size, already_generated_coins, already_donated_coins, base_reward, max_donation,h))
  {
    LOG_PRINT_L0("block size " << cumulative_block_size << " is bigger than allowed for this blockchain");
    return false;
  }
  //check every block
  if(h >= CURRENCY_DONATIONS_START_BLOCKNO)
  {
    r = (base_reward ==10 * (donation + royalty));
    CHECK_AND_ASSERT_MES(r, false, "Failed to validate donations value");
  }

  if(base_reward + fee < money_in_use)
  {
    LOG_ERROR("coinbase transaction spend too much money (" << print_money(money_in_use) << "). Block reward is " << print_money(base_reward + fee) << "(" << print_money(base_reward) << "+" << print_money(fee) << ")");
    return false;
  }
  if(base_reward + fee != money_in_use)
  {
    LOG_ERROR("coinbase transaction doesn't use full amount of block reward:  spent: "
                            << print_money(money_in_use) << ",  block reward " << print_money(base_reward + fee) << "(" << print_money(base_reward) << "+" << print_money(fee) << ")");
    return false;
  }  

  donation_total = royalty + donation;
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_backward_blocks_sizes(size_t from_height, std::vector<size_t>& sz, size_t count)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  CHECK_AND_ASSERT_MES(from_height < m_blocks.size(), false, "Internal error: get_backward_blocks_sizes called with from_height=" << from_height << ", blockchain height = " << m_blocks.size());

  size_t start_offset = (from_height+1) - std::min((from_height+1), count);
  for(size_t i = start_offset; i != from_height+1; i++)
    sz.push_back(m_blocks[i].block_cumulative_size);

  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_last_n_blocks_sizes(std::vector<size_t>& sz, size_t count)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(!m_blocks.size())
    return true;
  return get_backward_blocks_sizes(m_blocks.size() -1, sz, count);
}
//------------------------------------------------------------------
uint64_t blockchain_storage::get_current_comulative_blocksize_limit()
{
  return m_current_block_cumul_sz_limit;
}
//------------------------------------------------------------------
bool blockchain_storage::create_block_template(block& b, const account_public_address& miner_address, wide_difficulty_type& diffic, uint64_t& height, const blobdata& ex_nonce, bool vote_for_donation, const alias_info& ai)
{
  size_t median_size;
  uint64_t already_generated_coins;
  uint64_t already_donated_coins;
  uint64_t donation_amount_for_this_block = 0;

  CRITICAL_REGION_BEGIN(m_blockchain_lock);
  b.major_version = CURRENT_BLOCK_MAJOR_VERSION;
  b.minor_version = CURRENT_BLOCK_MINOR_VERSION;
  b.prev_id = get_top_block_id();
  b.timestamp = time(NULL);
  b.flags = 0;
  if(!vote_for_donation)
    b.flags = BLOCK_FLAGS_SUPPRESS_DONATION;
  height = m_blocks.size();
  diffic = get_difficulty_for_next_block();
  if(!(height%CURRENCY_DONATIONS_INTERVAL))
    get_required_donations_value_for_next_block(donation_amount_for_this_block);
  CHECK_AND_ASSERT_MES(diffic, false, "difficulty owverhead.");

  median_size = m_current_block_cumul_sz_limit / 2;
  already_generated_coins = m_blocks.back().already_generated_coins;
  already_donated_coins = m_blocks.back().already_donated_coins;

  CRITICAL_REGION_END();

  size_t txs_size;
  uint64_t fee;
  if (!m_tx_pool.fill_block_template(b, median_size, already_generated_coins, already_donated_coins,  txs_size, fee, height)) {
    return false;
  }
  /*
     two-phase miner transaction generation: we don't know exact block size until we prepare block, but we don't know reward until we know
     block size, so first miner transaction generated with fake amount of money, and with phase we know think we know expected block size
  */
  //make blocks coin-base tx looks close to real coinbase tx to get truthful blob size
  bool r = construct_miner_tx(height, median_size, already_generated_coins, already_donated_coins, 
                                                   txs_size, 
                                                   fee, 
                                                   miner_address, 
                                                   m_donations_account.m_account_address, 
                                                   m_royalty_account.m_account_address, 
                                                   b.miner_tx, ex_nonce, 
                                                   11, donation_amount_for_this_block, ai);
  CHECK_AND_ASSERT_MES(r, false, "Failed to construc miner tx, first chance");
#ifdef _DEBUG
  std::list<size_t> try_val;
  try_val.push_back(get_object_blobsize(b.miner_tx));
#endif

  size_t cumulative_size = txs_size + get_object_blobsize(b.miner_tx);
  for (size_t try_count = 0; try_count != 10; ++try_count) {
    r = construct_miner_tx(height, median_size, already_generated_coins, already_donated_coins, 
                                                cumulative_size, 
                                                fee, 
                                                miner_address, 
                                                m_donations_account.m_account_address, 
                                                m_royalty_account.m_account_address, 
                                                b.miner_tx, ex_nonce, 
                                                11, 
                                                donation_amount_for_this_block, ai);
#ifdef _DEBUG
    try_val.push_back(get_object_blobsize(b.miner_tx));
#endif

    CHECK_AND_ASSERT_MES(r, false, "Failed to construc miner tx, second chance");
    size_t coinbase_blob_size = get_object_blobsize(b.miner_tx);
    if (coinbase_blob_size > cumulative_size - txs_size) {
      cumulative_size = txs_size + coinbase_blob_size;
      continue;
    }

    if (coinbase_blob_size < cumulative_size - txs_size) {
      size_t delta = cumulative_size - txs_size - coinbase_blob_size;
      b.miner_tx.extra.insert(b.miner_tx.extra.end(), delta, 0);
      //here  could be 1 byte difference, because of extra field counter is varint, and it can become from 1-byte len to 2-bytes len.
      if (cumulative_size != txs_size + get_object_blobsize(b.miner_tx)) {
        CHECK_AND_ASSERT_MES(cumulative_size + 1 == txs_size + get_object_blobsize(b.miner_tx), false, "unexpected case: cumulative_size=" << cumulative_size << " + 1 is not equal txs_cumulative_size=" << txs_size << " + get_object_blobsize(b.miner_tx)=" << get_object_blobsize(b.miner_tx));
        b.miner_tx.extra.resize(b.miner_tx.extra.size() - 1);
        if (cumulative_size != txs_size + get_object_blobsize(b.miner_tx)) {
          //fuck, not lucky, -1 makes varint-counter size smaller, in that case we continue to grow with cumulative_size
          LOG_PRINT_RED("Miner tx creation have no luck with delta_extra size = " << delta << " and " << delta - 1 , LOG_LEVEL_2);
          cumulative_size += delta - 1;
          continue;
        }
        LOG_PRINT_GREEN("Setting extra for block: " << b.miner_tx.extra.size() << ", try_count=" << try_count, LOG_LEVEL_1);
      }
    }
    CHECK_AND_ASSERT_MES(cumulative_size == txs_size + get_object_blobsize(b.miner_tx), false, "unexpected case: cumulative_size=" << cumulative_size << " is not equal txs_cumulative_size=" << txs_size << " + get_object_blobsize(b.miner_tx)=" << get_object_blobsize(b.miner_tx));
    return true;
  }
  LOG_ERROR("Failed to create_block_template with " << 10 << " tries");
  return false;
}
//------------------------------------------------------------------
bool blockchain_storage::print_transactions_statistics()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  LOG_PRINT_L0("Started to collect transaction statistics, pleas wait...");
  size_t total_count = 0;
  size_t coinbase_count = 0;
  size_t total_full_blob = 0;
  size_t total_cropped_blob = 0;
  for(auto tx_entry: m_transactions)
  {
    ++total_count;
    if(is_coinbase(transactionByIndex(tx_entry.second).tx))
      ++coinbase_count;
    else
    {
	   total_full_blob += get_object_blobsize<transaction>(transactionByIndex(tx_entry.second).tx);
	   transaction tx = transactionByIndex(tx_entry.second).tx;
      tx.signatures.clear();
      total_cropped_blob += get_object_blobsize<transaction>(tx);
    }    
  }
  LOG_PRINT_L0("Done" << ENDL
      << "total transactions: " << total_count << ENDL 
      << "coinbase transactions: " << coinbase_count << ENDL 
      << "avarage size of transaction: " << total_full_blob/(total_count-coinbase_count) << ENDL
      << "avarage size of transaction without ring signatures: " << total_cropped_blob/(total_count-coinbase_count) << ENDL
      );
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::complete_timestamps_vector(uint64_t start_top_height, std::vector<uint64_t>& timestamps)
{

  if(timestamps.size() >= BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW)
    return true;

  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  size_t need_elements = BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW - timestamps.size();
  CHECK_AND_ASSERT_MES(start_top_height < m_blocks.size(), false, "internal error: passed start_height = " << start_top_height << " not less then m_blocks.size()=" << m_blocks.size());
  size_t stop_offset = start_top_height > need_elements ? start_top_height - need_elements:0;
  do
  {
    timestamps.push_back(m_blocks[start_top_height].bl.timestamp);
    if(start_top_height == 0)
      break;
    --start_top_height;
  }while(start_top_height != stop_offset);
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::handle_alternative_block(const block& b, const crypto::hash& id, block_verification_context& bvc)
{
  if(m_checkpoints.is_height_passed_zone(get_block_height(b), get_current_blockchain_height()-1))
  {
    LOG_PRINT_RED_L0("Block with id: " << id << ENDL << " for alternative chain, is under checkpoint zone, declined");
    bvc.m_verifivation_failed = true;
    return false;
  }

  TRY_ENTRY();
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  //block is not related with head of main chain
  //first of all - look in alternative chains container

  uint64_t nHeight = -1;
  m_blocks_index.getBlockHeight(b.prev_id, nHeight);

  auto it_prev = m_alternative_chains.find(b.prev_id);
  if (it_prev != m_alternative_chains.end() || nHeight != -1)
  {
    //we have new block in alternative chain
    //build alternative subchain, front -> mainchain, back -> alternative head
	 AlternativeChains::iterator alt_it = it_prev; //m_alternative_chains.find()
    std::list<AlternativeChains::iterator> alt_chain;
    std::vector<crypto::hash> alt_scratchppad;
    std::map<uint64_t, crypto::hash> alt_scratchppad_patch;
    std::vector<uint64_t> timestamps;
    while(alt_it != m_alternative_chains.end())
    {
      alt_chain.push_front(alt_it);
      timestamps.push_back(alt_it->second.bl.timestamp);
      alt_it = m_alternative_chains.find(alt_it->second.bl.prev_id);
    }

    if(alt_chain.size())
    {
      //make sure that it has right connection to main chain
      CHECK_AND_ASSERT_MES(m_blocks.size() > alt_chain.front()->second.height, false, "main blockchain wrong height");
      crypto::hash h = null_hash;
      get_block_hash(m_blocks[alt_chain.front()->second.height - 1].bl, h);
      CHECK_AND_ASSERT_MES(h == alt_chain.front()->second.bl.prev_id, false, "alternative chain have wrong connection to main chain");
      complete_timestamps_vector(alt_chain.front()->second.height - 1, timestamps);
      //build alternative scratchpad
      for(auto& ach: alt_chain)
      {
        if(!push_block_scratchpad_data(ach->second.scratch_offset, ach->second.bl, alt_scratchppad, alt_scratchppad_patch))
        {
          LOG_PRINT_RED_L0("Block with id: " << id
            << ENDL << " for alternative chain, have invalid data");
          bvc.m_verifivation_failed = true;
          return false;
        }
      }
    }else
    {
	   CHECK_AND_ASSERT_MES(nHeight != HEIGHT_NOT_FOUND, false, "internal error: broken imperative condition");
	  complete_timestamps_vector(nHeight, timestamps);
    }
    //check timestamp correct
    if(!check_block_timestamp(timestamps, b))
    {
      LOG_PRINT_RED_L0("Block with id: " << id
        << ENDL << " for alternative chain, have invalid timestamp: " << b.timestamp);
      //add_block_as_invalid(b, id);//do not add blocks to invalid storage before proof of work check was passed
      bvc.m_verifivation_failed = true;
      return false;
    }

    BlockEntry bei = boost::value_initialized<BlockEntry>();
    bei.bl = b;
	bei.height = alt_chain.size() ? it_prev->second.height + 1 : nHeight + 1;
    uint64_t connection_height = alt_chain.size() ? alt_chain.front()->second.height:bei.height;
    CHECK_AND_ASSERT_MES(connection_height, false, "INTERNAL ERROR: Wrong connection_height==0 in handle_alternative_block");
    bei.scratch_offset = m_blocks[connection_height].scratch_offset + alt_scratchppad.size();
    CHECK_AND_ASSERT_MES(bei.scratch_offset, false, "INTERNAL ERROR: Wrong bei.scratch_offset==0 in handle_alternative_block");
    
    //lets collect patchs from main line
    std::map<uint64_t, crypto::hash> main_line_patches;
    for(uint64_t i = connection_height; i != m_blocks.size(); i++)
    {
      std::vector<crypto::hash> block_addendum;
      bool res = get_block_scratchpad_addendum(m_blocks[i].bl, block_addendum);
      CHECK_AND_ASSERT_MES(res, false, "Failed to get_block_scratchpad_addendum for alt block");
      get_scratchpad_patch(m_blocks[i].scratch_offset, 
                           0, 
                           block_addendum.size(), 
                           block_addendum, 
                           main_line_patches);
    }
    //apply only that patches that lay under alternative scratchpad offset
    for(auto ml: main_line_patches)
    {
      if(ml.first < m_blocks[connection_height].scratch_offset)
        alt_scratchppad_patch[ml.first] = crypto::xor_pod(alt_scratchppad_patch[ml.first], ml.second);
    }

    wide_difficulty_type current_diff = get_next_difficulty_for_alternative_chain(alt_chain, bei);
    CHECK_AND_ASSERT_MES(current_diff, false, "!!!!!!! DIFFICULTY OVERHEAD !!!!!!!");
    crypto::hash proof_of_work = null_hash;
    // POW
#ifdef ENABLE_HASHING_DEBUG
    size_t call_no = 0;
    std::stringstream ss;      
#endif
    get_block_longhash(bei.bl, proof_of_work, bei.height, [&](uint64_t index) -> crypto::hash
    {
      uint64_t offset = index%bei.scratch_offset;
      crypto::hash res;
      if(offset >= m_blocks[connection_height].scratch_offset)
      {
        res = alt_scratchppad[offset - m_blocks[connection_height].scratch_offset];
      }else
      {
        res = m_scratchpad[offset];  
      }
      auto it = alt_scratchppad_patch.find(offset);
      if(it != alt_scratchppad_patch.end())
      {//apply patch
        res = crypto::xor_pod(res, it->second);
      }
#ifdef ENABLE_HASHING_DEBUG
      ss << "[" << call_no << "][" << index << "%" << bei.scratch_offset <<"(" << index%bei.scratch_offset << ")]" << res << ENDL;
      ++call_no;
#endif
      return res;
    });
#ifdef ENABLE_HASHING_DEBUG
    LOG_PRINT_L3("ID: " << get_block_hash(bei.bl) << "[" << bei.height <<  "]" << ENDL << "POW:" << proof_of_work << ENDL << ss.str());
#endif
    if(!check_hash(proof_of_work, current_diff))
    {
      LOG_PRINT_RED_L0("Block with id: " << id
        << ENDL << " for alternative chain, have not enough proof of work: " << proof_of_work
        << ENDL << " expected difficulty: " << current_diff);
      bvc.m_verifivation_failed = true;
      return false;
    }
    //
    
    if(!m_checkpoints.is_in_checkpoint_zone(bei.height))
    {
      m_is_in_checkpoint_zone = false;
    }else
    {
      m_is_in_checkpoint_zone = true;
      if(!m_checkpoints.check_block(bei.height, id))
      {
        LOG_ERROR("CHECKPOINT VALIDATION FAILED");
        bvc.m_verifivation_failed = true;
        return false;
      }
    }

    if(!prevalidate_miner_transaction(b, bei.height))
    {
      LOG_PRINT_RED_L0("Block with id: " << string_tools::pod_to_hex(id)
        << " (as alternative) have wrong miner transaction.");
      bvc.m_verifivation_failed = true;
      return false;

    }

	bei.cumulative_difficulty = alt_chain.size() ? it_prev->second.cumulative_difficulty: m_blocks[nHeight].cumulative_difficulty;
    ((uint128_t)bei.cumulative_difficulty) += current_diff;

#ifdef _DEBUG
    auto i_dres = m_alternative_chains.find(id);
    CHECK_AND_ASSERT_MES(i_dres == m_alternative_chains.end(), false, "insertion of new alternative block returned as it already exist");
#endif
    auto i_res = m_alternative_chains.insert(AlternativeChains::value_type(id, bei));
    CHECK_AND_ASSERT_MES(i_res.second, false, "insertion of new alternative block returned as it already exist");
    alt_chain.push_back(i_res.first);
    //check if difficulty bigger then in main chain
    if(m_blocks.back().cumulative_difficulty < bei.cumulative_difficulty)
    {
      //do reorganize!
		LOG_PRINT_GREEN("###### REORGANIZE on height: " << alt_chain.front()->second.height << " of " << m_blocks.size() - 1 << " with cum_difficulty " << (currency::wide_difficulty_type&)m_blocks.back().cumulative_difficulty
		  << ENDL << " alternative blockchain size: " << alt_chain.size() << " with cum_difficulty " << (currency::wide_difficulty_type&) bei.cumulative_difficulty, LOG_LEVEL_0);
      bool r = switch_to_alternative_blockchain(alt_chain);
      if(r) bvc.m_added_to_main_chain = true;
      else bvc.m_verifivation_failed = true;
      return r;
    }
    LOG_PRINT_BLUE("----- BLOCK ADDED AS ALTERNATIVE ON HEIGHT " << bei.height
      << ENDL << "id:\t" << id
      << ENDL << "PoW:\t" << proof_of_work
      << ENDL << "difficulty:\t" << current_diff, LOG_LEVEL_0);
    return true;
  }else
  {
    //block orphaned
    bvc.m_marked_as_orphaned = true;
    LOG_PRINT_RED_L0("Block recognized as orphaned and rejected, id = " << id);
  }
  
  return true;
  CATCH_ENTRY_L0("blockchain_storage::handle_alternative_block", false);
}
//------------------------------------------------------------------
bool blockchain_storage::get_blocks(uint64_t start_offset, size_t count, std::list<block>& blocks, std::list<transaction>& txs)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(start_offset >= m_blocks.size())
    return false;
  for(size_t i = start_offset; i < start_offset + count && i < m_blocks.size();i++)
  {
    blocks.push_back(m_blocks[i].bl);
    std::list<crypto::hash> missed_ids;
    get_transactions(m_blocks[i].bl.tx_hashes, txs, missed_ids);
    CHECK_AND_ASSERT_MES(!missed_ids.size(), false, "have missed transactions in own block in main blockchain");
  }

  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_blocks(uint64_t start_offset, size_t count, std::list<block>& blocks)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(start_offset >= m_blocks.size())
    return false;

  for(size_t i = start_offset; i < start_offset + count && i < m_blocks.size();i++)
    blocks.push_back(m_blocks[i].bl);
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::handle_get_objects(NOTIFY_REQUEST_GET_OBJECTS::request& arg, NOTIFY_RESPONSE_GET_OBJECTS::request& rsp)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  rsp.current_blockchain_height = get_current_blockchain_height();
  std::list<block> blocks;
  get_blocks(arg.blocks, blocks, rsp.missed_ids);

  BOOST_FOREACH(const auto& bl, blocks)
  {
    std::list<crypto::hash> missed_tx_id;
    std::list<transaction> txs;
    get_transactions(bl.tx_hashes, txs, rsp.missed_ids);
    CHECK_AND_ASSERT_MES(!missed_tx_id.size(), false, "Internal error: have missed missed_tx_id.size()=" << missed_tx_id.size()
      << ENDL << "for block id = " << get_block_hash(bl));
   rsp.blocks.push_back(block_complete_entry());
   block_complete_entry& e = rsp.blocks.back();
   //pack block
   e.block = t_serializable_object_to_blob(bl);
   //pack transactions
   BOOST_FOREACH(transaction& tx, txs)
     e.txs.push_back(t_serializable_object_to_blob(tx));

  }
  //get another transactions, if need
  std::list<transaction> txs;
  get_transactions(arg.txs, txs, rsp.missed_ids);
  //pack aside transactions
  BOOST_FOREACH(const auto& tx, txs)
    rsp.txs.push_back(t_serializable_object_to_blob(tx));

  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_transactions_daily_stat(uint64_t& daily_cnt, uint64_t& daily_volume)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  daily_cnt = daily_volume = 0;
  for(size_t i =  (m_blocks.size() > CURRENCY_BLOCK_PER_DAY ? m_blocks.size() - CURRENCY_BLOCK_PER_DAY:0 ); i!=m_blocks.size(); i++)
  {
    for(auto& h: m_blocks[i].bl.tx_hashes)
    {
      ++daily_cnt;
      auto tx_it = m_transactions.find(h);
      CHECK_AND_ASSERT_MES(tx_it != m_transactions.end(), false, "Wrong transaction hash "<<  h <<" in block on height " << i );
      uint64_t am = 0;
      bool r = get_inputs_money_amount(transactionByIndex(tx_it->second).tx, am);
      CHECK_AND_ASSERT_MES(r, false, "failed to get_inputs_money_amount");
      daily_volume += am;
    }
  }
  return true;
}
//------------------------------------------------------------------
uint64_t blockchain_storage::get_current_hashrate(size_t aprox_count)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(m_blocks.size() <= aprox_count)
    return 0;

  wide_difficulty_type w_hr = (m_blocks.back().cumulative_difficulty - m_blocks[m_blocks.size() - aprox_count].cumulative_difficulty)/
                              (m_blocks.back().bl.timestamp - m_blocks[m_blocks.size() - aprox_count].bl.timestamp);
  return (uint64_t)w_hr;
}
//------------------------------------------------------------------
bool blockchain_storage::extport_scratchpad_to_file(const std::string& path)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  export_scratchpad_file_header fh;
  memset(&fh, 0, sizeof(fh));

  fh.current_hi.prevhash = currency::get_block_hash(m_blocks.back().bl);
  fh.current_hi.height = m_blocks.size()-1;
  fh.scratchpad_size = m_scratchpad.size()*4;
  
  try
  {

    std::string tmp_path = path + ".tmp";
    std::ofstream fstream;
    fstream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fstream.open(tmp_path, std::ios_base::binary | std::ios_base::out | std::ios_base::trunc);
    fstream.write((const char*)&fh, sizeof(fh));
    fstream.write((const char*)&m_scratchpad[0], m_scratchpad.size()*32);
    fstream.close();

    boost::filesystem::remove(path);
    boost::filesystem::rename(tmp_path, path);

    LOG_PRINT_L0("Scratchpad exported to " << path << ", " << (m_scratchpad.size()*32)/1024 << "kbytes" );
    return true;
  }
  catch(const std::exception& e)
  {
    LOG_PRINT_L0("Failed to store scratchpad, error: " << e.what());
    return false;
  }

  catch(...)
  {
    LOG_PRINT_L0("Failed to store scratchpad, unknown error" );
    return false;
  }

  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_alternative_blocks(std::list<block>& blocks)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  BOOST_FOREACH(const auto& alt_bl, m_alternative_chains)
  {
    blocks.push_back(alt_bl.second.bl);
  }
  return true;
}
//------------------------------------------------------------------
size_t blockchain_storage::get_alternative_blocks_count()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  return m_alternative_chains.size();
}
//------------------------------------------------------------------
bool blockchain_storage::add_out_to_get_random_outs(std::vector<std::pair<crypto::hash, size_t> >& amount_outs, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount& result_outs, uint64_t amount, size_t i, uint64_t mix_count, bool use_only_forced_to_mix)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  TransactionMap::iterator tx_it = m_transactions.find(amount_outs[i].first);
  CHECK_AND_ASSERT_MES(tx_it != m_transactions.end(), false, "internal error: transaction with id " << amount_outs[i].first << ENDL <<
    ", used in mounts global index for amount=" << amount << ": i=" << i << "not found in transactions index");

  TransactionEntry & txe = transactionByIndex(tx_it->second);

  CHECK_AND_ASSERT_MES(txe.tx.vout.size() > amount_outs[i].second, false, "internal error: in global outs index, transaction out index="
	  << amount_outs[i].second << " more than transaction outputs = " << txe.tx.vout.size() << ", for tx id = " << amount_outs[i].first);
  
  CHECK_AND_ASSERT_MES(txe.tx.vout[amount_outs[i].second].target.type() == typeid(txout_to_key), false, "unknown tx out type");

  CHECK_AND_ASSERT_MES(txe.m_spent_flags.size() == txe.tx.vout.size(), false, "internal error");

  //do not use outputs that obviously spent for mixins
  if (txe.m_spent_flags[amount_outs[i].second])
    return false;

  //check if transaction is unlocked
  if (!is_tx_spendtime_unlocked(txe.tx.unlock_time))
    return false;

  //use appropriate mix_attr out 
  uint8_t mix_attr = boost::get<txout_to_key>(txe.tx.vout[amount_outs[i].second].target).mix_attr;
  
  if(mix_attr == CURRENCY_TO_KEY_OUT_FORCED_NO_MIX)
    return false; //COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS call means that ring signature will have more than one entry.
  else if(use_only_forced_to_mix && mix_attr == CURRENCY_TO_KEY_OUT_RELAXED)
    return false; //relaxed not allowed
  else if(mix_attr != CURRENCY_TO_KEY_OUT_RELAXED && mix_attr > mix_count)
    return false;//mix_attr set to specific minimum, and mix_count is less then desired count


  COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry& oen = *result_outs.outs.insert(result_outs.outs.end(), COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry());
  oen.global_amount_index = i;
  oen.out_key = boost::get<txout_to_key>(txe.tx.vout[amount_outs[i].second].target).key;
  return true;
}
//------------------------------------------------------------------
size_t blockchain_storage::find_end_of_allowed_index(const std::vector<std::pair<crypto::hash, size_t> >& amount_outs)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(!amount_outs.size())
    return 0;
  size_t i = amount_outs.size();
  do
  {
    --i;
    auto it = m_transactions.find(amount_outs[i].first);
    CHECK_AND_ASSERT_MES(it != m_transactions.end(), 0, "internal error: failed to find transaction from outputs index with tx_id=" << amount_outs[i].first);
	if (transactionByIndex(it->second).m_keeper_block_height + CURRENCY_MINED_MONEY_UNLOCK_WINDOW <= get_current_blockchain_height())
      return i+1;
  } while (i != 0);
  return 0;
}
//------------------------------------------------------------------
bool blockchain_storage::get_random_outs_for_amounts(const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request& req, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response& res)
{  
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  BOOST_FOREACH(uint64_t amount, req.amounts)
  {
    COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount& result_outs = *res.outs.insert(res.outs.end(), COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount());
    result_outs.amount = amount;
    auto it = m_outputs.find(amount);
    if(it == m_outputs.end())
    {
      LOG_ERROR("COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS: not outs for amount " << amount << ", wallet should use some real outs when it lookup for some mix, so, at least one out for this amount should exist");
      continue;//actually this is strange situation, wallet should use some real outs when it lookup for some mix, so, at least one out for this amount should exist
    }
	std::vector<std::pair<crypto::hash, size_t> >& amount_outs = (std::vector<std::pair<crypto::hash, size_t> >&)it->second;
    //it is not good idea to use top fresh outs, because it increases possibility of transaction canceling on split
    //lets find upper bound of not fresh outs
    size_t up_index_limit = find_end_of_allowed_index(amount_outs);
    CHECK_AND_ASSERT_MES(up_index_limit <= amount_outs.size(), false, "internal error: find_end_of_allowed_index returned wrong index=" << up_index_limit << ", with amount_outs.size = " << amount_outs.size());
    if(amount_outs.size() > req.outs_count)
    {
      std::set<size_t> used;
      size_t try_count = 0;
      for(uint64_t j = 0; j != req.outs_count && try_count < up_index_limit;)
      {
        size_t i = crypto::rand<size_t>()%up_index_limit;
        if(used.count(i))
          continue;
        bool added = add_out_to_get_random_outs(amount_outs, result_outs, amount, i, req.outs_count, req.use_forced_mix_outs);
        used.insert(i);
        if(added)
          ++j;
        ++try_count;
      }
    }else
    {
      for(size_t i = 0; i != up_index_limit; i++)
        add_out_to_get_random_outs(amount_outs, result_outs, amount, i, req.outs_count, req.use_forced_mix_outs);
    }
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::update_spent_tx_flags_for_input(uint64_t amount, uint64_t global_index, bool spent)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  auto it = m_outputs.find(amount);
  CHECK_AND_ASSERT_MES(it != m_outputs.end(), false, "Amount " << amount << " have not found during update_spent_tx_flags_for_input()");
  CHECK_AND_ASSERT_MES(global_index < it->second.size(), false, "Global index" << global_index << " for amount " << amount << " bigger value than amount's vector size()=" << it->second.size());

  TransactionEntry & txe = transactionByIndex(it->second[global_index].first);
  uint64_t out_amount = it->second[global_index].second;
  auto hash = get_transaction_hash(txe.tx);
  auto tx_it = m_transactions.find(hash);

  CHECK_AND_ASSERT_MES(tx_it != m_transactions.end(), false, "Can't find transaction id: " << hash << ", refferenced in amount=" << amount << ", global index=" << global_index);
  CHECK_AND_ASSERT_MES(out_amount < txe.m_spent_flags.size(), false, "Wrong input offset: " << out_amount << " in transaction id: " << hash << ", refferenced in amount=" << amount << ", global index=" << global_index);
   
  txe.m_spent_flags[out_amount] = spent;
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::resync_spent_tx_flags()
{
  LOG_PRINT_L0("Started re-building spent tx outputs data...");
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  for(auto& tx: m_transactions)
  {
	  transaction &t = transactionByIndex(tx.second).tx;
	  if (is_coinbase(t))
      continue;

	  for (auto& in : t.vin)
    {      
      CHECKED_GET_SPECIFIC_VARIANT(in, txin_to_key, in_to_key, false);
      if(in_to_key.key_offsets.size() != 1)
        continue;

      //direct spending
      if(!update_spent_tx_flags_for_input(in_to_key.amount, in_to_key.key_offsets[0], true))
        return false;

    }
  }
  LOG_PRINT_L0("Finished re-building spent tx outputs data");
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::find_blockchain_supplement(const std::list<crypto::hash>& qblock_ids, uint64_t& starter_offset)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  if(!qblock_ids.size() /*|| !req.m_total_height*/)
  {
    LOG_ERROR("Client sent wrong NOTIFY_REQUEST_CHAIN: m_block_ids.size()=" << qblock_ids.size() << /*", m_height=" << req.m_total_height <<*/ ", dropping connection");
    return false;
  } 
  //check genesis match
  if(qblock_ids.back() != get_block_hash(m_blocks[0].bl))
  {
    LOG_ERROR("Client sent wrong NOTIFY_REQUEST_CHAIN: genesis block missmatch: " << ENDL << "id: "
      << qblock_ids.back() << ", " << ENDL << "expected: " << get_block_hash(m_blocks[0].bl)
      << "," << ENDL << " dropping connection");
    return false;
  }

  /* Figure out what blocks we should request to get state_normal */
  size_t i = 0;
  auto bl_it = qblock_ids.begin();
  uint64_t nHeight = m_blocks_index.getBlockHeight(*bl_it);
  for(; bl_it != qblock_ids.end(); bl_it++, i++)
  {
	  nHeight = m_blocks_index.getBlockHeight(*bl_it);
	  if (nHeight != HEIGHT_NOT_FOUND) break;
  }

  if(bl_it == qblock_ids.end())
  {
    LOG_ERROR("Internal error handling connection, can't find split point");
    return false;
  }

  if (nHeight == HEIGHT_NOT_FOUND)
  {
    //this should NEVER happen, but, dose of paranoia in such cases is not too bad
    LOG_ERROR("Internal error handling connection, can't find split point");
    return false;
  }

  //we start to put block ids INCLUDING last known id, just to make other side be sure
  starter_offset = nHeight;
  return true;
}
//------------------------------------------------------------------
wide_difficulty_type blockchain_storage::block_difficulty(size_t i)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  CHECK_AND_ASSERT_MES(i < m_blocks.size(), false, "wrong block index i = " << i << " at blockchain_storage::block_difficulty()");
  if(i == 0)
    return m_blocks[i].cumulative_difficulty;

  return m_blocks[i].cumulative_difficulty - m_blocks[i-1].cumulative_difficulty;
}
//------------------------------------------------------------------
void blockchain_storage::print_blockchain(uint64_t start_index, uint64_t end_index)
{
  std::stringstream ss;
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(start_index >=m_blocks.size())
  {
    LOG_PRINT_L0("Wrong starter index set: " << start_index << ", expected max index " << m_blocks.size()-1);
    return;
  }

  for(size_t i = start_index; i != m_blocks.size() && i != end_index; i++)
  {
	  currency::wide_difficulty_type diff = block_difficulty(i);
	  ss << "height " << i << ", timestamp " << m_blocks[i].bl.timestamp << ", cumul_dif " << (currency::wide_difficulty_type&) m_blocks[i].cumulative_difficulty << ", cumul_size " << m_blocks[i].block_cumulative_size
      << "\nid\t\t" <<  get_block_hash(m_blocks[i].bl)
	  << "\ndifficulty\t\t" << diff << ", nonce " << m_blocks[i].bl.nonce << ", tx_count " << m_blocks[i].bl.tx_hashes.size() << ENDL;
  }
  LOG_PRINT_L1("Current blockchain:" << ENDL << ss.str());
  LOG_PRINT_L0("Blockchain printed with log level 1");
}
//------------------------------------------------------------------
void blockchain_storage::print_blockchain_index()
{
  std::stringstream ss;
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  for (uint64_t h = 0; h < m_blocks_index.size(); h++)
  {
	  ss << "id\t\t" << h << " height" << m_blocks_index.getBlockId(h) << ENDL << "";
  }

  LOG_PRINT_L0("Current blockchain index:" << ENDL << ss.str());
}
//------------------------------------------------------------------
void blockchain_storage::print_blockchain_outs(const std::string& file)
{
  std::stringstream ss;
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  BOOST_FOREACH(const Outputs::value_type& v, m_outputs)
  {
	const std::vector<std::pair<TransactionIndex, uint16_t> > & vals = v.second;
    if(vals.size())
    {
      ss << "amount: " <<  v.first << ENDL;
      for(size_t i = 0; i != vals.size(); i++)
        ss << "\t" << get_transaction_hash(transactionByIndex(vals[i].first).tx) << ": " << vals[i].second << ENDL;
    }
  }
  if(file_io_utils::save_string_to_file(file, ss.str()))
  {
    LOG_PRINT_L0("Current outputs index writen to file: " << file);
  }else
  {
    LOG_PRINT_L0("Failed to write current outputs index to file: " << file);
  }
}
//------------------------------------------------------------------
bool blockchain_storage::find_blockchain_supplement(const std::list<crypto::hash>& qblock_ids, NOTIFY_RESPONSE_CHAIN_ENTRY::request& resp)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(!find_blockchain_supplement(qblock_ids, resp.start_height))
    return false;

  resp.total_height = get_current_blockchain_height();
  size_t count = 0;
  for(size_t i = resp.start_height; i != m_blocks.size() && count < BLOCKS_IDS_SYNCHRONIZING_DEFAULT_COUNT; i++, count++)
    resp.m_block_ids.push_back(get_block_hash(m_blocks[i].bl));
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::find_blockchain_supplement(const std::list<crypto::hash>& qblock_ids, std::list<std::pair<block, std::list<transaction> > >& blocks, uint64_t& total_height, uint64_t& start_height, size_t max_count)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(!find_blockchain_supplement(qblock_ids, start_height))
    return false;

  total_height = get_current_blockchain_height();
  size_t count = 0;
  for(size_t i = start_height; i != m_blocks.size() && count < max_count; i++, count++)
  {
    blocks.resize(blocks.size()+1);
    blocks.back().first = m_blocks[i].bl;
    std::list<crypto::hash> mis;
    get_transactions(m_blocks[i].bl.tx_hashes, blocks.back().second, mis);
    CHECK_AND_ASSERT_MES(!mis.size(), false, "internal error, transaction from block not found");
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::add_block_as_invalid(const block& bl, const crypto::hash& h)
{
  BlockEntry bei = AUTO_VAL_INIT(bei);
  bei.bl = bl;
  return add_block_as_invalid(bei, h);
}
//------------------------------------------------------------------
bool blockchain_storage::add_block_as_invalid(const BlockEntry& bei, const crypto::hash& h)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  auto i_res = m_invalid_blocks.insert(std::map<crypto::hash, BlockEntry>::value_type(h, bei));
  CHECK_AND_ASSERT_MES(i_res.second, false, "at insertion invalid by tx returned status existed");
  LOG_PRINT_L0("BLOCK ADDED AS INVALID: " << h << ENDL << ", prev_id=" << bei.bl.prev_id << ", m_invalid_blocks count=" << m_invalid_blocks.size());
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::have_block(const crypto::hash& id)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  if(m_blocks_index.hasBlock(id))
    return true;
  if(m_alternative_chains.count(id))
    return true;
  /*if(m_orphaned_blocks.get<by_id>().count(id))
    return true;*/

  /*if(m_orphaned_by_tx.count(id))
    return true;*/
  if(m_invalid_blocks.count(id))
    return true;

  return false;
}
//------------------------------------------------------------------
bool blockchain_storage::handle_block_to_main_chain(const block& bl, block_verification_context& bvc)
{
  crypto::hash id = get_block_hash(bl);
  return handle_block_to_main_chain(bl, id, bvc);
}
//------------------------------------------------------------------
bool blockchain_storage::push_transaction_to_global_outs_index(const transaction& tx, const crypto::hash& tx_id, std::vector<uint64_t>& global_indexes)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  size_t i = 0;
  bool bReturn = true;
  BOOST_FOREACH(const auto& ot, tx.vout)
  {
	  auto it = m_transactions.find(tx_id);
	  if (it == m_transactions.end())
	  {
		  LOG_ERROR("push_transaction_to_global_outs_index: Cannot find transaction : " << tx_id);
		  bReturn = false;
		  break;
	  }
	auto& amount_index = m_outputs[ot.amount];
	amount_index.push_back(std::make_pair<>(it->second , (uint16_t)i));
    global_indexes.push_back(amount_index.size()-1);
    ++i;
  }
  return bReturn;
}
//------------------------------------------------------------------
size_t blockchain_storage::get_total_transactions()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  return m_transactions.size();
}
//------------------------------------------------------------------
bool blockchain_storage::get_outs(uint64_t amount, std::list<crypto::public_key>& pkeys)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  auto it = m_outputs.find(amount);
  if(it == m_outputs.end())
    return true;

  BOOST_FOREACH(const auto& out_entry, it->second)
  {
	  const transaction& tx = transactionByIndex(out_entry.first).tx;
	  auto hash = get_transaction_hash(tx);
	  auto tx_it = m_transactions.find(hash);

    CHECK_AND_ASSERT_MES(tx_it != m_transactions.end(), false, "transactions outs global index consistency broken: wrong tx id in index");
	CHECK_AND_ASSERT_MES(tx.vout.size() > out_entry.second, false, "transactions outs global index consistency broken: index in tx_outx more then size");
	CHECK_AND_ASSERT_MES(tx.vout[out_entry.second].target.type() == typeid(txout_to_key), false, "transactions outs global index consistency broken: index in tx_outx more then size");
	pkeys.push_back(boost::get<txout_to_key>(tx.vout[out_entry.second].target).key);
  }

  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::pop_transaction_from_global_index(const transaction& tx, const crypto::hash& tx_id)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  size_t i = tx.vout.size()-1;
  BOOST_REVERSE_FOREACH(const auto& ot, tx.vout)
  {
    auto it = m_outputs.find(ot.amount);
    CHECK_AND_ASSERT_MES(it != m_outputs.end(), false, "transactions outs global index consistency broken");

	const transaction& tx = transactionByIndex(it->second.back().first).tx;
	auto hash = get_transaction_hash(tx);

    CHECK_AND_ASSERT_MES(it->second.size(), false, "transactions outs global index: empty index for amount: " << ot.amount);
	CHECK_AND_ASSERT_MES(hash == tx_id, false, "transactions outs global index consistency broken: tx id missmatch");
    CHECK_AND_ASSERT_MES(it->second.back().second == i, false, "transactions outs global index consistency broken: in transaction index missmatch");
    it->second.pop_back();
    //do not let to exist empty m_outputs entries - this will broke scratchpad selector
    if(!it->second.size())
      m_outputs.erase(it);
    --i;
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::unprocess_blockchain_tx_extra(const transaction& tx)
{
  if(is_coinbase(tx))
  {
    tx_extra_info ei = AUTO_VAL_INIT(ei);
    bool r = parse_and_validate_tx_extra(tx, ei);
    CHECK_AND_ASSERT_MES(r, false, "failed to validate transaction extra on unprocess_blockchain_tx_extra");
    if(ei.m_alias.m_alias.size())
    {
      r = pop_alias_info(ei.m_alias);
      CHECK_AND_ASSERT_MES(r, false, "failed to pop_alias_info");
    }
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_alias_info(const std::string& alias, alias_info_base& info)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  auto it = m_aliases.find(alias);
  if(it != m_aliases.end())
  {
    if(it->second.size())
    {
      info = it->second.back();
      return true;
    }
  }
  return false;
}
//------------------------------------------------------------------
uint64_t blockchain_storage::get_aliases_count()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  return m_aliases.size();
}
//------------------------------------------------------------------
uint64_t blockchain_storage::get_scratchpad_size()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  return m_scratchpad.size()*32;
}
//------------------------------------------------------------------
bool blockchain_storage::get_all_aliases(std::list<alias_info>& aliases)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  for(auto a: m_aliases)
  {
    if(a.second.size())
    {
      aliases.push_back(alias_info());
      aliases.back().m_alias = a.first;
      static_cast<alias_info_base&>(aliases.back()) = a.second.back();
    }
  }
  return true;
}
//------------------------------------------------------------------
std::string blockchain_storage::get_alias_by_address(const account_public_address& addr)
{
  auto it = m_addr_to_alias.find(addr);
  if (it != m_addr_to_alias.end())
    return it->second;
  
  return "";
}
//------------------------------------------------------------------
bool blockchain_storage::pop_alias_info(const alias_info& ai)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  CHECK_AND_ASSERT_MES(ai.m_alias.size(), false, "empty name in pop_alias_info");
  aliases_container::mapped_type& alias_history = m_aliases[ai.m_alias];
  CHECK_AND_ASSERT_MES(alias_history.size(), false, "empty name list in pop_alias_info");
  
  auto it = m_addr_to_alias.find(alias_history.back().m_address);
  if (it != m_addr_to_alias.end())
  {
    m_addr_to_alias.erase(it);
  }
  else
  {
    LOG_ERROR("In m_addr_to_alias not found " << get_account_address_as_str(alias_history.back().m_address));
  }

  alias_history.pop_back();
  if (alias_history.size())
  {
    m_addr_to_alias[alias_history.back().m_address] = ai.m_alias;
  }
  
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::put_alias_info(const alias_info& ai)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  CHECK_AND_ASSERT_MES(ai.m_alias.size(), false, "empty name in put_alias_info");

  aliases_container::mapped_type& alias_history = m_aliases[ai.m_alias];
  if(ai.m_sign == null_sig)
  {//adding new alias, check sat name is free
    CHECK_AND_ASSERT_MES(!alias_history.size(), false, "alias " << ai.m_alias << " already in use");
    alias_history.push_back(ai);
    m_addr_to_alias[alias_history.back().m_address] = ai.m_alias;
  }else
  {
    //update procedure
    CHECK_AND_ASSERT_MES(alias_history.size(), false, "alias " << ai.m_alias << " can't be update becouse it doesn't exists");
    std::string signed_buff;
    make_tx_extra_alias_entry(signed_buff, ai, true);
    bool r = crypto::check_signature(get_blob_hash(signed_buff), alias_history.back().m_address.m_spend_public_key, ai.m_sign);
    CHECK_AND_ASSERT_MES(r, false, "Failed to check signature, alias update failed");
    //update granted
    auto it = m_addr_to_alias.find(alias_history.back().m_address);
    if (it != m_addr_to_alias.end())
      m_addr_to_alias.erase(it);
    else
      LOG_ERROR("Wromg m_addr_to_alias state: address not found " << get_account_address_as_str(alias_history.back().m_address));

    alias_history.push_back(ai);
    m_addr_to_alias[alias_history.back().m_address] = ai.m_alias;
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::process_blockchain_tx_extra(const transaction& tx)
{
  //check transaction extra
  tx_extra_info ei = AUTO_VAL_INIT(ei);
  bool r = parse_and_validate_tx_extra(tx, ei);
  CHECK_AND_ASSERT_MES(r, false, "failed to validate transaction extra");

  if(ei.m_alias.m_alias.size())
  {
    r = put_alias_info(ei.m_alias);
    CHECK_AND_ASSERT_MES(r, false, "failed to put_alias_info");
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::add_transaction_from_block(TransactionEntry& txe, const crypto::hash& tx_id, const crypto::hash& bl_id, uint64_t bl_height, uint16_t tx_index)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  transaction & tx = txe.tx;

  bool r = process_blockchain_tx_extra(tx);
  CHECK_AND_ASSERT_MES(r, false, "failed to process_blockchain_tx_extra");

  struct add_transaction_input_visitor: public boost::static_visitor<bool>
  {
    blockchain_storage& m_bcs;
    key_images_container& m_spent_keys;
    const crypto::hash& m_tx_id;
    const crypto::hash& m_bl_id;
    add_transaction_input_visitor(blockchain_storage& bcs, key_images_container& spent_keys, const crypto::hash& tx_id, const crypto::hash& bl_id):
      m_bcs(bcs),
      m_spent_keys(spent_keys), 
      m_tx_id(tx_id), 
      m_bl_id(bl_id){}
    bool operator()(const txin_to_key& in) const
    {
      const crypto::key_image& ki = in.k_image;
      auto r = m_spent_keys.insert(ki);
      if(!r.second)
      {
        //double spend detected
        LOG_PRINT_L0("tx with id: " << m_tx_id << " in block id: " << m_bl_id << " have input marked as spent with key image: " << ki << ", block declined");
        return false;
      }

      if(in.key_offsets.size() == 1)
      {
        //direct spend detected
        if(!m_bcs.update_spent_tx_flags_for_input(in.amount, in.key_offsets[0], true))
        {
          //internal error
          LOG_PRINT_L0("Failed to  update_spent_tx_flags_for_input");
          return false;
        }
      }

      return true;
    }

    bool operator()(const txin_gen& tx) const{return true;}
    bool operator()(const txin_to_script& tx) const{return false;}
    bool operator()(const txin_to_scripthash& tx) const{return false;}
  };

  BOOST_FOREACH(const txin_v& in, tx.vin)
  {
    if(!boost::apply_visitor(add_transaction_input_visitor(*this, m_spent_keys, tx_id, bl_id), in))
    {
      LOG_ERROR("critical internal error: add_transaction_input_visitor failed. but key_images should be already checked");
      purge_transaction_keyimages_from_blockchain(tx, false);
      bool r = unprocess_blockchain_tx_extra(tx);
      CHECK_AND_ASSERT_MES(r, false, "failed to unprocess_blockchain_tx_extra");
      return false;
    }
  }

  txe.m_keeper_block_height = bl_height;
  txe.m_spent_flags.resize(tx.vout.size(), false);

  auto it = m_transactions.find(tx_id);
  if (it != m_transactions.end())
  {
	 LOG_ERROR("critical internal error: tx with id: " << tx_id << " in block id: " << bl_id << " already in blockchain");
    purge_transaction_keyimages_from_blockchain(tx, true);
    bool r = unprocess_blockchain_tx_extra(tx);
    CHECK_AND_ASSERT_MES(r, false, "failed to unprocess_blockchain_tx_extra");
    return false;
  }
  else
  {
	  TransactionIndex transactionIndex = { (uint32_t)bl_height, tx_index };
	  auto i_r = m_transactions.insert(std::make_pair<>(tx_id, transactionIndex));
	  if (!i_r.second)
	  {
		  LOG_ERROR("critical internal error: tx with id: " << tx_id << " in block id: " << bl_id << " insert in blockchain error.");
		  purge_transaction_keyimages_from_blockchain(tx, true);
		  bool r = unprocess_blockchain_tx_extra(tx);
		  CHECK_AND_ASSERT_MES(r, false, "failed to unprocess_blockchain_tx_extra");
		  return false;
	  }
  }
  r = push_transaction_to_global_outs_index(tx, tx_id, txe.m_global_output_indexes);
  CHECK_AND_ASSERT_MES(r, false, "failed to return push_transaction_to_global_outs_index tx id " << tx_id);
  LOG_PRINT_L2("Added transaction to blockchain history:" << ENDL
    << "tx_id: " << tx_id << ENDL
    << "inputs: " << tx.vin.size() << ", outs: " << tx.vout.size() << ", spend money: " << print_money(get_outs_money_amount(tx)) << "(fee: " << (is_coinbase(tx) ? "0[coinbase]" : print_money(get_tx_fee(tx))) << ")");
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::get_tx_outputs_gindexs(const crypto::hash& tx_id, std::vector<uint64_t>& indexs)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  auto it = m_transactions.find(tx_id);
  if(it == m_transactions.end())
  {
    LOG_PRINT_RED_L0("warning: get_tx_outputs_gindexs failed to find transaction with id = " << tx_id);
    return false;
  }

  CHECK_AND_ASSERT_MES(transactionByIndex(it->second).m_global_output_indexes.size(), false, "internal error: global indexes for transaction " << tx_id << " is empty");
  indexs = transactionByIndex(it->second).m_global_output_indexes;
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::check_tx_inputs(const transaction& tx, uint64_t& max_used_block_height, crypto::hash& max_used_block_id)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  bool res = check_tx_inputs(tx, &max_used_block_height);
  if(!res) return false;
  CHECK_AND_ASSERT_MES(max_used_block_height < m_blocks.size(), false,  "internal error: max used block index=" << max_used_block_height << " is not less then blockchain size = " << m_blocks.size());
  get_block_hash(m_blocks[max_used_block_height].bl, max_used_block_id);
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::have_tx_keyimges_as_spent(const transaction &tx)
{
  BOOST_FOREACH(const txin_v& in, tx.vin)
  {
    CHECKED_GET_SPECIFIC_VARIANT(in, const txin_to_key, in_to_key, true);
    if(have_tx_keyimg_as_spent(in_to_key.k_image))
      return true;
  }
  return false;
}
//------------------------------------------------------------------
bool blockchain_storage::check_tx_inputs(const transaction& tx, uint64_t* pmax_used_block_height)
{
  crypto::hash tx_prefix_hash = get_transaction_prefix_hash(tx);
  return check_tx_inputs(tx, tx_prefix_hash, pmax_used_block_height);
}
//------------------------------------------------------------------
bool blockchain_storage::check_tx_inputs(const transaction& tx, const crypto::hash& tx_prefix_hash, uint64_t* pmax_used_block_height)
{
  size_t sig_index = 0;
  if(pmax_used_block_height)
    *pmax_used_block_height = 0;

  BOOST_FOREACH(const auto& txin,  tx.vin)
  {
    CHECK_AND_ASSERT_MES(txin.type() == typeid(txin_to_key), false, "wrong type id in tx input at blockchain_storage::check_tx_inputs");
    const txin_to_key& in_to_key = boost::get<txin_to_key>(txin);

    CHECK_AND_ASSERT_MES(in_to_key.key_offsets.size(), false, "empty in_to_key.key_offsets in transaction with id " << get_transaction_hash(tx));

    if(have_tx_keyimg_as_spent(in_to_key.k_image))
    {
      LOG_PRINT_L1("Key image already spent in blockchain: " << string_tools::pod_to_hex(in_to_key.k_image));
      return false;
    }

    std::vector<crypto::signature> sig_stub;
    const std::vector<crypto::signature>* psig = &sig_stub;
    if (!m_is_in_checkpoint_zone)
    {
		CHECK_AND_ASSERT_MES(sig_index < tx.signatures.size(), false, "wrong transaction: " << get_transaction_hash(tx) <<", not signature entry for input with index = " << sig_index);
      psig = &tx.signatures[sig_index];
    }    
    if (!check_tx_input(in_to_key, tx_prefix_hash, *psig, pmax_used_block_height))
    {
      LOG_PRINT_L0("Failed to check ring signature for tx " << get_transaction_hash(tx));
      return false;
    }

    sig_index++;
  }
  if (!m_is_in_checkpoint_zone)
  {
    CHECK_AND_ASSERT_MES(tx.signatures.size() == sig_index, false, "tx signatures count differs from inputs");
  }

  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::is_tx_spendtime_unlocked(uint64_t unlock_time)
{
  if(unlock_time < CURRENCY_MAX_BLOCK_NUMBER)
  {
    //interpret as block index
    if(get_current_blockchain_height()-1 + CURRENCY_LOCKED_TX_ALLOWED_DELTA_BLOCKS >= unlock_time)
      return true;
    else
      return false;
  }else
  {
    //interpret as time
    uint64_t current_time = static_cast<uint64_t>(time(NULL));
    if(current_time + CURRENCY_LOCKED_TX_ALLOWED_DELTA_SECONDS >= unlock_time)
      return true;
    else
      return false;
  }
  return false;
}
//------------------------------------------------------------------
bool blockchain_storage::check_tx_input(const txin_to_key& txin, const crypto::hash& tx_prefix_hash, const std::vector<crypto::signature>& sig, uint64_t* pmax_related_block_height)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  struct outputs_visitor
  {
    std::vector<const crypto::public_key *>& m_results_collector;
    blockchain_storage& m_bch;
    outputs_visitor(std::vector<const crypto::public_key *>& results_collector, blockchain_storage& bch):m_results_collector(results_collector), m_bch(bch)
    {}
    bool handle_output(const transaction& tx, const tx_out& out)
    {
      //check tx unlock time
      if(!m_bch.is_tx_spendtime_unlocked(tx.unlock_time))
      {
        LOG_PRINT_L0("One of outputs for one of inputs have wrong tx.unlock_time = " << tx.unlock_time);
        return false;
      }

      if(out.target.type() != typeid(txout_to_key))
      {
        LOG_PRINT_L0("Output have wrong type id, which=" << out.target.which());
        return false;
      }

      m_results_collector.push_back(&boost::get<txout_to_key>(out.target).key);
      return true;
    }
  };

  //check ring signature
  std::vector<const crypto::public_key *> output_keys;
  outputs_visitor vi(output_keys, *this);
  if(!scan_outputkeys_for_indexes(txin, vi, pmax_related_block_height))
  {
    LOG_PRINT_L0("Failed to get output keys for tx with amount = " << print_money(txin.amount) << " and count indexes " << txin.key_offsets.size());
    return false;
  }

  if(txin.key_offsets.size() != output_keys.size())
  {
    LOG_PRINT_L0("Output keys for tx with amount = " << txin.amount << " and count indexes " << txin.key_offsets.size() << " returned wrong keys count " << output_keys.size());
    return false;
  }
  if(m_is_in_checkpoint_zone)
    return true;

  CHECK_AND_ASSERT_MES(sig.size() == output_keys.size(), false, "internal error: tx signatures count=" << sig.size() << " mismatch with outputs keys count for inputs=" << output_keys.size());
  return crypto::check_ring_signature(tx_prefix_hash, txin.k_image, output_keys, sig.data());
}
//------------------------------------------------------------------
uint64_t blockchain_storage::get_adjusted_time()
{
  //TODO: add collecting median time
  return time(NULL);
}
//------------------------------------------------------------------
bool blockchain_storage::check_block_timestamp_main(const block& b)
{
  if(b.timestamp > get_adjusted_time() + CURRENCY_BLOCK_FUTURE_TIME_LIMIT)
  {
    LOG_PRINT_L0("Timestamp of block with id: " << get_block_hash(b) << ", " << b.timestamp << ", bigger than adjusted time + 2 hours");
    return false;
  }

  std::vector<uint64_t> timestamps;
  size_t offset = m_blocks.size() <= BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW ? 0: m_blocks.size()- BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW;
  for(;offset!= m_blocks.size(); ++offset)
    timestamps.push_back(m_blocks[offset].bl.timestamp);

  return check_block_timestamp(std::move(timestamps), b);
}
//------------------------------------------------------------------
bool blockchain_storage::check_block_timestamp(std::vector<uint64_t> timestamps, const block& b)
{
  if(timestamps.size() < BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW)
    return true;

  uint64_t median_ts = epee::misc_utils::median(timestamps);

  if(b.timestamp < median_ts)
  {
    LOG_PRINT_L0("Timestamp of block with id: " << get_block_hash(b) << ", " << b.timestamp << ", less than median of last " << BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW << " blocks, " << median_ts);
    return false;
  }

  return true;
}

//------------------------------------------------------------------
bool blockchain_storage::get_block_for_scratchpad_alt(uint64_t connection_height, uint64_t block_index, std::list<blockchain_storage::AlternativeChains::iterator>& alt_chain, block & b)
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(block_index >= connection_height)
  {
    //take it from alt chain
    for(auto it: alt_chain)
    {
      if(it->second.height == block_index)
      {
        b = it->second.bl;
        return true;
      }
    }
    CHECK_AND_ASSERT_MES(false, false, "Failed to find alternative block with height=" << block_index);
  }else
  {
    b = m_blocks[block_index].bl;
  }
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::prune_aged_alt_blocks()
{
  CRITICAL_REGION_LOCAL(m_blockchain_lock);

  uint64_t current_height = get_current_blockchain_height();

  for(auto it = m_alternative_chains.begin(); it != m_alternative_chains.end();)
  {
    if( current_height > it->second.height && current_height - it->second.height > CURRENCY_ALT_BLOCK_LIVETIME_COUNT)
      m_alternative_chains.erase(it++);
    else
      ++it;
  }

  return true;
}

bool blockchain_storage::fix_blocks_index()
{
	if (m_blocks_index.size() == m_blocks.size())
		return true;
	else if (m_blocks_index.size() > m_blocks.size())
	{
		LOG_PRINT_RED("the inde of m_blocks_index is bigger than that of the blocks. try to fix it...", LOG_LEVEL_0);
		while (m_blocks_index.size() > m_blocks.size())
			m_blocks_index.pop();

		return true;
	}
	else if (m_blocks_index.size() < m_blocks.size())
	{
		LOG_PRINT_RED("block and index not corresponding. Rebuilding internal structures...", LOG_LEVEL_0);
		clear_all_cache_data();
		CHECK_AND_ASSERT_MES(rebuildcache(0), false, "Failed to rebuild cache.");
		store_blockchain();
	}
	return true;
}
//------------------------------------------------------------------
bool blockchain_storage::handle_block_to_main_chain(const block& bl, const crypto::hash& id, block_verification_context& bvc)
{
  TIME_MEASURE_START(block_processing_time);
  CRITICAL_REGION_LOCAL(m_blockchain_lock);
  if(bl.prev_id != get_top_block_id())
  {
    LOG_PRINT_L0("Block with id: " << id << ENDL << "have wrong prev_id: " << bl.prev_id << ENDL  << "expected: " << get_top_block_id());
    return false;
  }

  if(!check_block_timestamp_main(bl))
  {
    LOG_PRINT_L0("Block with id: " << id << ENDL << "have invalid timestamp: " << bl.timestamp);
    //add_block_as_invalid(bl, id);//do not add blocks to invalid storage befor proof of work check was passed
    bvc.m_verifivation_failed = true;
    return false;
  }

  //check proof of work
  TIME_MEASURE_START(target_calculating_time);
  wide_difficulty_type current_diffic = get_difficulty_for_next_block();
  CHECK_AND_ASSERT_MES(current_diffic, false, "!!!!!!!!! difficulty overhead !!!!!!!!!");
  TIME_MEASURE_FINISH(target_calculating_time);
  TIME_MEASURE_START(longhash_calculating_time);
  crypto::hash proof_of_work = null_hash;
  
  proof_of_work = get_block_longhash(bl, m_blocks.size(), [&](uint64_t index) -> crypto::hash&
  {
    return m_scratchpad[index%m_scratchpad.size()];
  });

  if(!check_hash(proof_of_work, current_diffic))
  {
    LOG_PRINT_L0("Block with id: " << id << ENDL << "have not enough proof of work: " << proof_of_work << ENDL  << "nexpected difficulty: " << current_diffic );
    bvc.m_verifivation_failed = true;
    return false;
  }

  m_is_in_checkpoint_zone = false;
  if(m_checkpoints.is_in_checkpoint_zone(get_current_blockchain_height()))
  {
    m_is_in_checkpoint_zone = true;
    if(!m_checkpoints.check_block(get_current_blockchain_height(), id))
    {
      LOG_ERROR("CHECKPOINT VALIDATION FAILED");
      bvc.m_verifivation_failed = true;
      return false;
    }
  }

  TIME_MEASURE_FINISH(longhash_calculating_time);

  if(!prevalidate_miner_transaction(bl, m_blocks.size()))
  {
    LOG_PRINT_L0("Block with id: " << id << " failed to pass prevalidation");
    bvc.m_verifivation_failed = true;
    return false;
  }
  size_t coinbase_blob_size = get_object_blobsize(bl.miner_tx);
  size_t cumulative_block_size = coinbase_blob_size;

  BlockEntry bei = boost::value_initialized<BlockEntry>();
  bei.bl = bl;
  bei.transactions.resize(bl.tx_hashes.size()+1);
  bei.transactions[0].tx = bl.miner_tx;

  //process transactions
  if (!add_transaction_from_block(bei.transactions[0], get_transaction_hash(bl.miner_tx), id, get_current_blockchain_height(), 0))
  {
    LOG_PRINT_L0("Block with id: " << id << " failed to add transaction to blockchain storage");
    bvc.m_verifivation_failed = true;
    return false;
  }

  size_t tx_processed_count = 0;
  uint64_t fee_summary = 0;
  BOOST_FOREACH(const crypto::hash& tx_id, bl.tx_hashes)
  {
    transaction tx;
    size_t blob_size = 0;
    uint64_t fee = 0;
    if(!m_tx_pool.take_tx(tx_id, tx, blob_size, fee))
    {
      LOG_PRINT_L0("Block with id: " << id  << "have at least one unknown transaction with id: " << tx_id);
      purge_block_data_from_blockchain(bl, tx_processed_count);
      //add_block_as_invalid(bl, id);
      bvc.m_verifivation_failed = true;
      return false;
    }

    //If we under checkpoints, ring signatures should be pruned    
    if(m_is_in_checkpoint_zone)
    {
      tx.signatures.clear();
    }

    if(!check_tx_inputs(tx))
    {
      LOG_PRINT_L0("Block with id: " << id  << "have at least one transaction (id: " << tx_id << ") with wrong inputs.");
      currency::tx_verification_context tvc = AUTO_VAL_INIT(tvc);
      bool add_res = m_tx_pool.add_tx(tx, tvc, true);
      CHECK_AND_ASSERT_MES2(add_res, "handle_block_to_main_chain: failed to add transaction back to transaction pool");

      purge_block_data_from_blockchain(bl, tx_processed_count);
      add_block_as_invalid(bl, id);
      LOG_PRINT_L0("Block with id " << id << " added as invalid becouse of wrong inputs in transactions");
      bvc.m_verifivation_failed = true;
      return false;
    }

	bei.transactions[tx_processed_count + 1].tx = tx;
	if (!add_transaction_from_block(bei.transactions[tx_processed_count + 1], tx_id, id, get_current_blockchain_height(), tx_processed_count + 1))
    {
       LOG_PRINT_L0("Block with id: " << id << " failed to add transaction to blockchain storage");
       currency::tx_verification_context tvc = AUTO_VAL_INIT(tvc);
       bool add_res = m_tx_pool.add_tx(tx, tvc, true);
       CHECK_AND_ASSERT_MES2(add_res, "handle_block_to_main_chain: failed to add transaction back to transaction pool");
       purge_block_data_from_blockchain(bl, tx_processed_count);
       bvc.m_verifivation_failed = true;
       return false;
    }
    fee_summary += fee;
    cumulative_block_size += blob_size;
    ++tx_processed_count;
  }
  uint64_t base_reward = 0;
  uint64_t already_generated_coins = m_blocks.size() ? m_blocks.back().already_generated_coins:0;
  uint64_t already_donated_coins = m_blocks.size() ? m_blocks.back().already_donated_coins:0;
  uint64_t donation_total = 0;
  if(!validate_miner_transaction(bl, cumulative_block_size, fee_summary, base_reward, already_generated_coins, already_donated_coins, donation_total))
  {
    LOG_PRINT_L0("Block with id: " << id << " have wrong miner transaction");
    purge_block_data_from_blockchain(bl, tx_processed_count);
    bvc.m_verifivation_failed = true;
    return false;
  }

  bei.scratch_offset = m_scratchpad.size();
  bei.block_cumulative_size = cumulative_block_size;
  bei.cumulative_difficulty = current_diffic;
  bei.already_generated_coins = already_generated_coins + base_reward;
  bei.already_donated_coins = already_donated_coins + donation_total;
  if(m_blocks.size())
    bei.cumulative_difficulty += m_blocks.back().cumulative_difficulty;

  bei.height = m_blocks.size();

  bool bReturn = m_blocks_index.push(id);
  if (!bReturn)
  {
    LOG_ERROR("block with id: " << id << " already in block indexes");
    purge_block_data_from_blockchain(bl, tx_processed_count);
    bvc.m_verifivation_failed = true;
    return false;
  }

  //append to scratchpad
  if(!push_block_scratchpad_data(bl, m_scratchpad))
  {
    LOG_ERROR("Internal error for block id: " << id << ": failed to put_block_scratchpad_data");
    purge_block_data_from_blockchain(bl, tx_processed_count);
	m_blocks_index.pop();
    bvc.m_verifivation_failed = true;
    return false;    
  }

#ifdef ENABLE_HASHING_DEBUG  
  LOG_PRINT_L3("SCRATCHPAD_SHOT FOR H=" << bei.height+1 << ENDL << dump_scratchpad(m_scratchpad));
#endif

  m_blocks.push_back(bei);

  fix_blocks_index();

  update_next_comulative_size_limit();
  TIME_MEASURE_FINISH(block_processing_time);
  LOG_PRINT_L1("+++++ BLOCK SUCCESSFULLY ADDED" << ENDL << "id:\t" << id
    << ENDL << "PoW:\t" << proof_of_work
    << ENDL << "HEIGHT " << bei.height << ", difficulty:\t" << current_diffic
    << ENDL << "block reward: " << print_money(fee_summary + base_reward) << "(" << print_money(base_reward) << " + " << print_money(fee_summary) 
    << ")" << ( (bei.height%CURRENCY_DONATIONS_INTERVAL)?std::string(""):std::string("donation: ") + print_money(donation_total) ) << ", coinbase_blob_size: " << coinbase_blob_size << ", cumulative size: " << cumulative_block_size
    << ", " << block_processing_time << "("<< target_calculating_time << "/" << longhash_calculating_time << ")ms");

  bvc.m_added_to_main_chain = true;
  
  m_tx_pool.on_blockchain_inc(bei.height, id);
  //LOG_PRINT_L0("BLOCK: " << ENDL << "" << dump_obj_as_json(bei.bl));
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::update_next_comulative_size_limit()
{
  std::vector<size_t> sz;
  get_last_n_blocks_sizes(sz, CURRENCY_REWARD_BLOCKS_WINDOW);

  uint64_t median = misc_utils::median(sz);
  if(median <= get_block_granted_full_reward_zone(get_current_blockchain_height()))
    median =  get_block_granted_full_reward_zone(get_current_blockchain_height());

  m_current_block_cumul_sz_limit = median*2;
  return true;
}
//------------------------------------------------------------------
bool blockchain_storage::add_new_block(const block& bl_, block_verification_context& bvc)
{
  //copy block here to let modify block.target
  block bl = bl_;
  crypto::hash id = get_block_hash(bl);
  CRITICAL_REGION_LOCAL(m_tx_pool);//to avoid deadlock lets lock tx_pool for whole add/reorganize process
  CRITICAL_REGION_LOCAL1(m_blockchain_lock);

  if(have_block(id))
  {
    LOG_PRINT_L3("block with id = " << id << " already exists");
    bvc.m_already_exists = true;
    return false;
  }
  
  crypto::hash h = get_top_block_id();
  //check that block refers to chain tail
  if(!(bl.prev_id == h))
  {
    //chain switching or wrong block
    bvc.m_added_to_main_chain = false;
    bool r = handle_alternative_block(bl, id, bvc);
    return r;
    //never relay alternative blocks
  }

  return handle_block_to_main_chain(bl, id, bvc);
}


/************************************************************************/
/*                                                                      */
/************************************************************************/

template<class t_ids_container, class t_blocks_container, class t_missed_container>
bool  blockchain_storage::get_blocks(const t_ids_container& block_ids, t_blocks_container& blocks, t_missed_container& missed_bs)
{
	CRITICAL_REGION_LOCAL(m_blockchain_lock);

	BOOST_FOREACH(const auto& bl_id, block_ids)
	{
		uint64_t nHeight = m_blocks_index.getBlockHeight(bl_id);
		if (HEIGHT_NOT_FOUND == nHeight)
			missed_bs.push_back(bl_id);
		else
		{
			CHECK_AND_ASSERT_MES(nHeight < m_blocks.size(), false, "Internal error: bl_id=" << string_tools::pod_to_hex(bl_id)
				<< " have index record with offset=" << nHeight << ", bigger then m_blocks.size()=" << m_blocks.size());
			blocks.push_back(m_blocks[nHeight].bl);
		}
	}
	return true;
}

template<class t_ids_container, class t_tx_container, class t_missed_container>
bool  blockchain_storage::get_transactions(const t_ids_container& txs_ids, t_tx_container& txs, t_missed_container& missed_txs)
{
	CRITICAL_REGION_LOCAL(m_blockchain_lock);

	BOOST_FOREACH(const auto& tx_id, txs_ids)
	{
		auto it = m_transactions.find(tx_id);
		if (it == m_transactions.end())
		{
			transaction tx;
			if (!m_tx_pool.get_transaction(tx_id, tx))
				missed_txs.push_back(tx_id);
			else
				txs.push_back(tx);
		}
		else
			txs.push_back(transactionByIndex(it->second).tx);
	}
	return true;
}

template<class archive_t>
void blockchain_storage::serialize(archive_t & ar, const unsigned int version)
{
	//old structure
	typedef std::vector<block_extended_info_old> blocks_container;
	typedef std::unordered_map<crypto::hash, size_t> blocks_by_id_index;
	typedef std::unordered_map<crypto::hash, transaction_chain_entry> transactions_container;
	typedef std::unordered_map<crypto::hash, block_extended_info_old> blocks_ext_by_hash;
	typedef std::map<uint64_t, std::vector<std::pair<crypto::hash, size_t>>> outputs_container; //crypto::hash - tx hash, size_t - index of out in transaction

	blocks_container blocks;
	blocks_by_id_index blocks_index;
	transactions_container transactions;
	key_images_container spent_keys;
	blocks_ext_by_hash alternative_chains, invalid_blocks;
	outputs_container outputs;
	aliases_container aliases;
	std::vector<crypto::hash> scratchpad;

	if (version < 22)
	{
		LOG_PRINT_L0("Detected blockchain of unsupported version, migration is not possible.");
		return;
	}

	LOG_PRINT_L0("Blockchain of previous version detected, migrating. This may take several minutes, please be patient...");

	CHECK_PROJECT_NAME();
	CRITICAL_REGION_LOCAL(m_blockchain_lock);
	ar & blocks;
	ar & blocks_index;
	ar & transactions;
	/*
	ar & m_spent_keys;

	//do not keep alternative blocks
	if (version < 27)
		ar & alternative_chains;

	size_t current_block_cumul_sz_limit;

	ar & outputs;
	ar & invalid_blocks;
	ar & current_block_cumul_sz_limit;
	ar & aliases;
	ar & m_scratchpad;

	//serialization m_alternative_chains excluding
	uint64_t total_check_count = 0;
	if (archive_t::is_loading::value && version < 27)
		total_check_count = blocks.size() + blocks_index.size() + transactions.size() + m_spent_keys.size() + alternative_chains.size() + outputs.size() + invalid_blocks.size() + current_block_cumul_sz_limit;
	else
		total_check_count = blocks.size() + blocks_index.size() + transactions.size() + m_spent_keys.size() + outputs.size() + invalid_blocks.size() + current_block_cumul_sz_limit;

	if (archive_t::is_saving::value)
	{
		ar & total_check_count;
	}
	else
	{
		uint64_t total_check_count_loaded = 0;
		ar & total_check_count_loaded;
		if (total_check_count != total_check_count_loaded)
		{
			LOG_ERROR("Blockchain storage data corruption detected. total_count loaded from file = " << total_check_count_loaded << ", expected = " << total_check_count);

			LOG_PRINT_L0("Old blockchain storage:" << ENDL << 
				"m_blocks: " << blocks.size() << ENDL <<
				"m_blocks_index: " << blocks_index.size() << ENDL <<
				"m_transactions: " << transactions.size() << ENDL <<
				"m_spent_keys: " << spent_keys.size() << ENDL <<
				"m_alternative_chains: " << alternative_chains.size() << ENDL <<
				"m_outputs: " << outputs.size() << ENDL <<
				"m_invalid_blocks: " << invalid_blocks.size() << ENDL <<
				"m_current_block_cumul_sz_limit: " << current_block_cumul_sz_limit << ENDL);

			throw std::runtime_error("Blockchain data corruption");
		}
	}
	

	if (version < 26)
		m_current_pruned_rs_height = 0;
	else
		ar & m_current_pruned_rs_height;

	LOG_PRINT_L0("Old blockchain:" << ENDL <<
		"m_blocks: " << blocks.size() << ENDL <<
		"m_blocks_index: " << blocks_index.size() << ENDL <<
		"m_transactions: " << transactions.size() << ENDL <<
		"m_spent_keys: " << spent_keys.size() << ENDL <<
		"m_alternative_chains: " << alternative_chains.size() << ENDL <<
		"m_outputs: " << outputs.size() << ENDL <<
		"m_invalid_blocks: " << invalid_blocks.size() << ENDL <<
		"m_current_block_cumul_sz_limit: " << current_block_cumul_sz_limit << ENDL <<
		"m_current_pruned_rs_height: " << m_current_pruned_rs_height << ENDL);
		*/

	LOG_PRINT_L0("Read old blockchain completed, begin to migrate...");
	m_is_blockchain_transforming = true;

	blockchain_storage::BlockEntry block;
	for (uint32_t b = 0; b < blocks.size(); ++b)
	{
		if (b % 1000 == 0)
		{
			std::cout << "Height " << b << " of " << blocks.size() << '\r';
		}
		block.bl = blocks[b].bl;

		auto hash = get_block_hash(block.bl);

		block.height = b;
		block.block_cumulative_size = blocks[b].block_cumulative_size;
		block.cumulative_difficulty = uint128_b2n(blocks[b].cumulative_difficulty);
		block.already_generated_coins = blocks[b].already_generated_coins;
		block.already_donated_coins = blocks[b].already_donated_coins;
		block.scratch_offset = blocks[b].scratch_offset;

		block.transactions.resize(1 + blocks[b].bl.tx_hashes.size());
		crypto::hash tx_id = get_transaction_hash(blocks[b].bl.miner_tx);
		block.transactions[0] = transactions[tx_id];

		//add other txs
		for (uint32_t t = 0; t < blocks[b].bl.tx_hashes.size(); ++t)
		{
			//transactionIndex = { b, 1 + t };
			block.transactions[1 + t] = transactions[blocks[b].bl.tx_hashes[t]];
		}
		/*
		//add the first tx, m_alias
		bool r = process_blockchain_tx_extra(block.transactions[0].tx);
		CHECK_AND_ASSERT_MES_NO_RET(r, "failed to process_blockchain_tx_extra");

		//m_transactions
		blockchain_storage::TransactionIndex transactionIndex = {b, 0};
		auto it = m_transactions.insert(std::make_pair(hash, transactionIndex));
		CHECK_AND_ASSERT_MES_NO_RET(it.second ,"transaction " << hash << "exists.");

		//m_outputs
		for (uint16_t o = 0; o < block.transactions[0].tx.vout.size(); ++o)
		{
			m_outputs[block.transactions[0].tx.vout[o].amount].push_back(std::make_pair<>(transactionIndex, o));
		}
		//add other txs
		for (uint32_t t = 0; t < blocks[b].bl.tx_hashes.size(); ++t)
		{
			transactionIndex = { b, 1 + t };
			block.transactions[1 + t].tx = transactions[blocks[b].bl.tx_hashes[t]].tx;

			//add items to ouputs
			for (uint16_t o = 0; o < block.transactions[1 + t].tx.vout.size(); ++o)
			{
				m_outputs[block.transactions[1 + t].tx.vout[o].amount].push_back(std::make_pair<>(transactionIndex, o));
			}
		}*/

		m_blocks.push_back(block);// m_blocks
		//m_blocks_index.push(hash); // m_blocks_index

		//CHECK_AND_ASSERT_MES_NO_RET(m_blocks_index.size() == m_blocks.size(),"blocks and indexed are not coresponding to each other(m_blocks_index: " << m_blocks_index.size() << ",m_blocks: " << m_blocks.size());
	}
	
	update_next_comulative_size_limit();
	m_is_blockchain_transforming = false;
//	CHECK_AND_ASSERT_MES_NO_RET(m_current_block_cumul_sz_limit == current_block_cumul_sz_limit, "Migration was unsuccessful.");
}


//------------------------------------------------------------------
template<class visitor_t>
bool blockchain_storage::scan_outputkeys_for_indexes(const txin_to_key& tx_in_to_key, visitor_t& vis, uint64_t* pmax_related_block_height)
{
	CRITICAL_REGION_LOCAL(m_blockchain_lock);
	auto it = m_outputs.find(tx_in_to_key.amount);
	if (it == m_outputs.end() || !tx_in_to_key.key_offsets.size())
		return false;

	std::vector<uint64_t> absolute_offsets = relative_output_offsets_to_absolute(tx_in_to_key.key_offsets);

	std::vector<std::pair<TransactionIndex, uint16_t>>& amount_outs_vec = it->second;
	size_t count = 0;
	BOOST_FOREACH(uint64_t i, absolute_offsets)
	{
		if (i >= amount_outs_vec.size())
		{
			LOG_PRINT_L0("Wrong index in transaction inputs: " << i << ", expected maximum " << amount_outs_vec.size() - 1);
			return false;
		}
		
		TransactionEntry &txe = transactionByIndex(amount_outs_vec[i].first);
		auto tx_it = m_transactions.find(get_transaction_hash(txe.tx));

		CHECK_AND_ASSERT_MES(tx_it != m_transactions.end(), false, "Wrong transaction id in output indexes: " << string_tools::pod_to_hex(amount_outs_vec[i].first));
		CHECK_AND_ASSERT_MES(amount_outs_vec[i].second < txe.tx.vout.size(), false,
			"Wrong index in transaction outputs: " << amount_outs_vec[i].second << ", expected less then " << txe.tx.vout.size());
		//check mix_attr

		CHECKED_GET_SPECIFIC_VARIANT(txe.tx.vout[amount_outs_vec[i].second].target, const txout_to_key, outtk, false);
		if (outtk.mix_attr > 1)
			CHECK_AND_ASSERT_MES(tx_in_to_key.key_offsets.size() >= outtk.mix_attr, false, "transaction out[" << count << "] is marked to be used minimum with " << static_cast<uint32_t>(outtk.mix_attr) << "parts in ring signature, but input used only " << tx_in_to_key.key_offsets.size());
		else if (outtk.mix_attr == CURRENCY_TO_KEY_OUT_FORCED_NO_MIX)
			CHECK_AND_ASSERT_MES(tx_in_to_key.key_offsets.size() == 1, false, "transaction out[" << count << "] is marked to be used without mixins in ring signature, but input used is " << tx_in_to_key.key_offsets.size());

		if (!vis.handle_output(txe.tx, txe.tx.vout[amount_outs_vec[i].second]))
		{
			LOG_PRINT_L0("Failed to handle_output for output no = " << count << ", with absolute offset " << i);
			return false;
		}
		if (count++ == absolute_offsets.size() - 1 && pmax_related_block_height)
		{
			if (*pmax_related_block_height < txe.m_keeper_block_height)
				*pmax_related_block_height = txe.m_keeper_block_height;
		}
	}

	return true;
}


