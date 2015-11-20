// Copyright (c) 2012-2013 The Cryptonote developers
// Copyright (c) 2012-2013 The Boolberry developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/version.hpp>
#include <boost/serialization/list.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/foreach.hpp>
#include <atomic>

#include "BlockIndex.h"
#include "tx_pool.h"
#include "difficulty.h"
#include "currency_basic.h"
#include "common/util.h"
#include "currency_protocol/currency_protocol_defs.h"
#include "rpc/core_rpc_server_commands_defs.h"
#include "common/difficulty_boost_serialization.h"
#include "currency_core/currency_format_utils.h"
#include "verification_context.h"
#include "crypto/hash.h"
#include "checkpoints.h"
#include "SwappedVector.h"


POD_MAKE_HASHABLE(currency, account_public_address);

#define CURRENT_BLOCKCHAIN_STORAGE_ARCHIVE_VER          27
#define CURRENT_TRANSACTION_CHAIN_ENTRY_ARCHIVE_VER     3
#define CURRENT_BLOCK_EXTENDED_INFO_ARCHIVE_VER         1
#define CURRENT_BLOCK_ENTRY_ARCHIVE_VER				    1
#define CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER			2

namespace currency
{
	/************************************************************************/
	/*                                                                      */
	/************************************************************************/
	class blockchain_storage
	{
	public:
		class BlockCacheSerializer
		{
		public:
			BlockCacheSerializer(blockchain_storage& bs, const crypto::hash lastBlockHash, uint64_t nHeight) :
				m_bs(bs), m_nHeight(nHeight), m_storedLastBlockHash(lastBlockHash), m_loaded(false) {}

			template<class Archive> void serialize(Archive& ar, unsigned int version);
			bool loaded() const {return m_loaded;}
			crypto::hash getStoredLastBlockHash() const {return m_storedLastBlockHash;}
			uint64_t getStoredHeight() const {return m_nHeight;}

		private:

			bool m_loaded;
			blockchain_storage& m_bs;
			crypto::hash m_storedLastBlockHash;
			uint64_t m_nHeight;
		};

		struct transaction_chain_entry
		{
			transaction tx;
			uint64_t m_keeper_block_height;
			std::vector<uint64_t> m_global_output_indexes;
			std::vector<uint8_t> m_spent_flags;

			transaction_chain_entry & operator=(transaction_chain_entry & tce)
			{
				if (this == &tce) return *this;
				tx = tce.tx;
				m_keeper_block_height = tce.m_keeper_block_height;
				m_global_output_indexes = tce.m_global_output_indexes;
				m_spent_flags = tce.m_spent_flags;
				return *this;
			}
			
			template<class Archive> void serialize(Archive & ar, unsigned int version)
			{
				ar & tx;
				ar & m_keeper_block_height;
				ar & m_global_output_indexes;
				ar & m_spent_flags;
			}

			BEGIN_SERIALIZE_OBJECT()
				FIELD(tx)
				VARINT_FIELD(m_keeper_block_height)
				FIELD(m_global_output_indexes)
				FIELD(m_spent_flags)
			END_SERIALIZE()
		};

		typedef transaction_chain_entry TransactionEntry;

		struct block_extended_info_old
		{
			block   bl;
			uint64_t height;
			size_t block_cumulative_size;
			old_wide_difficulty_type cumulative_difficulty;
			uint64_t already_generated_coins;
			uint64_t already_donated_coins;
			uint64_t scratch_offset;
		};

		struct block_extended_info
		{
			block   bl;
			uint64_t height;
			size_t block_cumulative_size;
			wide_difficulty_type cumulative_difficulty;
			uint64_t already_generated_coins;
			uint64_t already_donated_coins;
			uint64_t scratch_offset;
			
			template<class Archive> void serialize(Archive& ar, unsigned int version)
			{
				ar & bl;
				ar & height;
				ar & block_cumulative_size;
				ar & cumulative_difficulty;
				ar & already_generated_coins;
				ar & already_donated_coins;
				ar & scratch_offset;
			}
			
			BEGIN_SERIALIZE_OBJECT()
				FIELD(bl)
				VARINT_FIELD(height)
				VARINT_FIELD(block_cumulative_size)
				FIELDS(cumulative_difficulty)
				VARINT_FIELD(already_generated_coins)
				VARINT_FIELD(already_donated_coins)
				VARINT_FIELD(scratch_offset)
			END_SERIALIZE()
		};

		struct BlockEntry :public block_extended_info
		{
			std::vector<TransactionEntry> transactions;
			
			template<class Archive> void serialize(Archive& ar, unsigned int version)
			{
				ar & *static_cast<block_extended_info *>(this);
				ar & transactions;
			}
			
			BEGIN_SERIALIZE_OBJECT()
				FIELDS(*static_cast<block_extended_info *>(this))
				FIELD(transactions)
			END_SERIALIZE()
		};

		struct TransactionIndex
		{
			uint32_t block;
			uint16_t transaction;

			template<class Archive> void serialize(Archive& ar, unsigned int version)
			{
				ar & block;
				ar & transaction;
			}
		};

		blockchain_storage(currency::tx_memory_pool& tx_pool);

		bool init() { return init(tools::get_default_data_dir()); }
		bool init(const std::string& config_folder);
		bool deinit();

		bool rebuildcache(uint64_t start_height = 0);

		void set_checkpoints(checkpoints&& chk_pts);
		checkpoints& get_checkpoints() { return m_checkpoints; }

		//bool push_new_block();
		bool get_blocks(uint64_t start_offset, size_t count, std::list<block>& blocks, std::list<transaction>& txs);
		bool get_blocks(uint64_t start_offset, size_t count, std::list<block>& blocks);
		bool get_alternative_blocks(std::list<block>& blocks);
		size_t get_alternative_blocks_count();
		crypto::hash get_block_id_by_height(uint64_t height);
		bool get_block_by_hash(const crypto::hash &h, block &blk);
		bool get_block_by_height(uint64_t h, block &blk);
		void get_all_known_block_ids(std::list<crypto::hash> &main, std::list<crypto::hash> &alt, std::list<crypto::hash> &invalid);

		template<class archive_t>
		void serialize(archive_t & ar, const unsigned int version);

		bool have_tx(const crypto::hash &id);
		bool have_tx_keyimges_as_spent(const transaction &tx);
		bool have_tx_keyimg_as_spent(const crypto::key_image &key_im);
		transaction *get_tx(const crypto::hash &id);

		template<class visitor_t>
		bool scan_outputkeys_for_indexes(const txin_to_key& tx_in_to_key, visitor_t& vis, uint64_t* pmax_related_block_height = NULL);

		uint64_t get_current_blockchain_height();
		crypto::hash get_top_block_id();
		crypto::hash get_top_block_id(uint64_t& height);
		bool get_top_block(block& b);
		wide_difficulty_type get_difficulty_for_next_block();
		bool add_new_block(const block& bl_, block_verification_context& bvc);
		bool reset_and_set_genesis_block(const block& b);
		void clear_all_cache_data();
		bool create_block_template(block& b, const account_public_address& miner_address, wide_difficulty_type& di, uint64_t& height, const blobdata& ex_nonce, bool vote_for_donation, const alias_info& ai);
		bool have_block(const crypto::hash& id);
		size_t get_total_transactions();
		bool get_outs(uint64_t amount, std::list<crypto::public_key>& pkeys);
		bool get_short_chain_history(std::list<crypto::hash>& ids);
		bool find_blockchain_supplement(const std::list<crypto::hash>& qblock_ids, NOTIFY_RESPONSE_CHAIN_ENTRY::request& resp);
		bool find_blockchain_supplement(const std::list<crypto::hash>& qblock_ids, uint64_t& starter_offset);
		bool find_blockchain_supplement(const std::list<crypto::hash>& qblock_ids, std::list<std::pair<block, std::list<transaction> > >& blocks, uint64_t& total_height, uint64_t& start_height, size_t max_count);
		bool handle_get_objects(NOTIFY_REQUEST_GET_OBJECTS::request& arg, NOTIFY_RESPONSE_GET_OBJECTS::request& rsp);
		bool handle_get_objects(const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request& req, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response& res);
		bool get_random_outs_for_amounts(const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request& req, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response& res);
		bool get_backward_blocks_sizes(size_t from_height, std::vector<size_t>& sz, size_t count);
		bool get_tx_outputs_gindexs(const crypto::hash& tx_id, std::vector<uint64_t>& indexs);
		bool get_alias_info(const std::string& alias, alias_info_base& info);
		std::string get_alias_by_address(const account_public_address& addr);
		bool get_all_aliases(std::list<alias_info>& aliases);
		uint64_t get_aliases_count();
		uint64_t get_scratchpad_size();
		bool store_blockchain();
		bool check_tx_input(const txin_to_key& txin, const crypto::hash& tx_prefix_hash, const std::vector<crypto::signature>& sig, uint64_t* pmax_related_block_height = NULL);
		bool check_tx_inputs(const transaction& tx, const crypto::hash& tx_prefix_hash, uint64_t* pmax_used_block_height = NULL);
		bool check_tx_inputs(const transaction& tx, uint64_t* pmax_used_block_height = NULL);
		bool check_tx_inputs(const transaction& tx, uint64_t& pmax_used_block_height, crypto::hash& max_used_block_id);
		uint64_t get_current_comulative_blocksize_limit();
		uint64_t get_current_hashrate(size_t aprox_count);
		bool extport_scratchpad_to_file(const std::string& path);
		bool print_transactions_statistics();
		bool update_spent_tx_flags_for_input(uint64_t amount, uint64_t global_index, bool spent);

		bool is_transforming_blockchain(){ return m_is_blockchain_transforming; }
		bool is_storing_blockchain(){ return m_is_blockchain_storing; }
		wide_difficulty_type block_difficulty(size_t i);
		bool copy_scratchpad(std::vector<crypto::hash>& dst);//TODO: not the best way, add later update method instead of full copy
		bool copy_scratchpad(std::string& dst);
		bool prune_aged_alt_blocks();
		bool get_transactions_daily_stat(uint64_t& daily_cnt, uint64_t& daily_volume);

		//debug functions
		void print_blockchain(uint64_t start_index, uint64_t end_index);
		void print_blockchain_index();
		void print_blockchain_outs(const std::string& file);

		template<class t_ids_container, class t_tx_container, class t_missed_container>
		bool  get_transactions(const t_ids_container& txs_ids, t_tx_container& txs, t_missed_container& missed_txs);

		template<class t_ids_container, class t_blocks_container, class t_missed_container>
		bool  get_blocks(const t_ids_container& block_ids, t_blocks_container& blocks, t_missed_container& missed_bs);

	private:
		//new structure
		typedef SwappedVector<BlockEntry> Blocks;
		typedef std::unordered_map<crypto::hash, TransactionIndex> TransactionMap;
		typedef std::unordered_map<crypto::hash, BlockEntry> AlternativeChains;
		//typedef google::sparse_hash_map<uint64_t, std::vector<std::pair<TransactionIndex, uint16_t>>> Outputs;
		typedef std::unordered_map<uint64_t, std::vector<std::pair<TransactionIndex, uint16_t>>> Outputs;
		friend class BlockCacheSerializer;

		//keep usage
		typedef std::unordered_set<crypto::key_image> key_images_container;
		typedef std::map<std::string, std::list<alias_info_base>> aliases_container; //alias can be address address address + view key
		typedef std::unordered_map<account_public_address, std::string> address_to_aliases_container;

		tx_memory_pool& m_tx_pool;
		critical_section m_blockchain_lock; // TODO: add here reader/writer lock

		// main chain
		Blocks m_blocks;                // height  -> block_extended_info
		BlockIndex m_blocks_index;

		TransactionMap m_transactions;
		key_images_container m_spent_keys;
		size_t m_current_block_cumul_sz_limit;

		// all alternative chains
		AlternativeChains m_alternative_chains; // crypto::hash -> block_extended_info

		// some invalid blocks
		AlternativeChains m_invalid_blocks;     // crypto::hash -> block_extended_info

		Outputs m_outputs;

		aliases_container m_aliases;
		address_to_aliases_container m_addr_to_alias;
		std::vector<crypto::hash> m_scratchpad;
		uint64_t m_current_pruned_rs_height;

		std::string m_config_folder;
		checkpoints m_checkpoints;
		std::atomic<bool> m_is_in_checkpoint_zone;
		std::atomic<bool> m_is_blockchain_storing;
		std::atomic<bool> m_is_blockchain_transforming;

		account_keys m_donations_account;
		account_keys m_royalty_account;

		TransactionEntry& transactionByIndex(TransactionIndex index);

		bool switch_to_alternative_blockchain(std::list<AlternativeChains::iterator>& alt_chain);
		bool pop_block_from_blockchain();
		bool purge_block_data_from_blockchain(const block& b, size_t processed_tx_count);
		bool purge_transaction_from_blockchain(const crypto::hash& tx_id);
		bool purge_transaction_keyimages_from_blockchain(const transaction& tx, bool strict_check);

		bool handle_block_to_main_chain(const block& bl, block_verification_context& bvc);
		bool handle_block_to_main_chain(const block& bl, const crypto::hash& id, block_verification_context& bvc);
		bool handle_alternative_block(const block& b, const crypto::hash& id, block_verification_context& bvc);
		wide_difficulty_type get_next_difficulty_for_alternative_chain(const std::list<AlternativeChains::iterator>& alt_chain, BlockEntry& bei);
		bool prevalidate_miner_transaction(const block& b, uint64_t height);
		bool validate_miner_transaction(const block& b, size_t cumulative_block_size, uint64_t fee, uint64_t& base_reward, uint64_t already_generated_coins, uint64_t already_donated_coins, uint64_t& donation_total);
		bool validate_transaction(const block& b, uint64_t height, const transaction& tx);
		bool rollback_blockchain_switching(std::list<block>& original_chain, size_t rollback_height);
		bool add_transaction_from_block(TransactionEntry& txe, const crypto::hash& tx_id, const crypto::hash& bl_id, uint64_t bl_height, uint16_t tx_index);
		bool push_transaction_to_global_outs_index(const transaction& tx, const crypto::hash& tx_id, std::vector<uint64_t>& global_indexes);
		bool pop_transaction_from_global_index(const transaction& tx, const crypto::hash& tx_id);
		bool get_last_n_blocks_sizes(std::vector<size_t>& sz, size_t count);
		bool add_out_to_get_random_outs(std::vector<std::pair<crypto::hash, size_t> >& amount_outs, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount& result_outs, uint64_t amount, size_t i, uint64_t mix_count, bool use_only_forced_to_mix = false);
		bool is_tx_spendtime_unlocked(uint64_t unlock_time);
		bool add_block_as_invalid(const block& bl, const crypto::hash& h);
		bool add_block_as_invalid(const BlockEntry& bei, const crypto::hash& h);
		size_t find_end_of_allowed_index(const std::vector<std::pair<crypto::hash, size_t> >& amount_outs);
		bool check_block_timestamp_main(const block& b);
		bool check_block_timestamp(std::vector<uint64_t> timestamps, const block& b);
		uint64_t get_adjusted_time();
		bool complete_timestamps_vector(uint64_t start_height, std::vector<uint64_t>& timestamps);
		bool update_next_comulative_size_limit();
		bool lookfor_donation(const transaction& tx, uint64_t& donation, uint64_t& royalty);
		bool get_block_for_scratchpad_alt(uint64_t connection_height, uint64_t block_index, std::list<blockchain_storage::AlternativeChains::iterator>& alt_chain, block & b);
		bool process_blockchain_tx_extra(const transaction& tx);
		bool unprocess_blockchain_tx_extra(const transaction& tx);
		bool pop_alias_info(const alias_info& ai);
		bool put_alias_info(const alias_info& ai);
		bool validate_donations_value(uint64_t donation, uint64_t royalty);
		//uint64_t get_block_avr_donation_vote(const block& b);
		bool get_required_donations_value_for_next_block(uint64_t& don_am); //applicable only for each CURRENCY_DONATIONS_INTERVAL-th block
		void fill_addr_to_alias_dict();
		bool resync_spent_tx_flags();
		bool prune_ring_signatures_if_need();
		bool prune_ring_signatures(uint64_t height, uint64_t& transactions_pruned, uint64_t& signatures_pruned);


	};
}

BOOST_CLASS_VERSION(currency::blockchain_storage, CURRENT_BLOCKCHAIN_STORAGE_ARCHIVE_VER)
BOOST_CLASS_VERSION(currency::blockchain_storage::transaction_chain_entry, CURRENT_TRANSACTION_CHAIN_ENTRY_ARCHIVE_VER)
BOOST_CLASS_VERSION(currency::blockchain_storage::block_extended_info, CURRENT_BLOCK_EXTENDED_INFO_ARCHIVE_VER)
BOOST_CLASS_VERSION(currency::blockchain_storage::block_extended_info_old, CURRENT_BLOCK_EXTENDED_INFO_ARCHIVE_VER)
BOOST_CLASS_VERSION(currency::blockchain_storage::BlockEntry, CURRENT_BLOCK_ENTRY_ARCHIVE_VER)
BOOST_CLASS_VERSION(currency::blockchain_storage::BlockCacheSerializer, CURRENT_BLOCKCACHE_STORAGE_ARCHIVE_VER)
