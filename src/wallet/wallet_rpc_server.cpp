// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "include_base_utils.h"
using namespace epee;

#include "wallet_rpc_server.h"
#include "common/command_line.h"
#include "currency_core/currency_format_utils.h"
#include "currency_core/account.h"
#include "misc_language.h"
#include "crypto/hash.h"

namespace tools
{
  //-----------------------------------------------------------------------------------
  const command_line::arg_descriptor<std::string> wallet_rpc_server::arg_rpc_bind_port = {"rpc-bind-port", "Starts wallet as rpc server for wallet operations, sets bind port for server", "", true};
  const command_line::arg_descriptor<std::string> wallet_rpc_server::arg_rpc_bind_ip = {"rpc-bind-ip", "Specify ip to bind rpc server", "127.0.0.1"};

  void wallet_rpc_server::init_options(boost::program_options::options_description& desc)
  {
    command_line::add_arg(desc, arg_rpc_bind_ip);
    command_line::add_arg(desc, arg_rpc_bind_port);
  }
  //------------------------------------------------------------------------------------------------------------------------------
  wallet_rpc_server::wallet_rpc_server(wallet2& w):m_wallet(w)
  {}
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::run()
  {
    m_net_server.add_idle_handler([this](){
      m_wallet.refresh();
      return true;
    }, 20*1000);
	m_net_server.add_idle_handler([this](){
      m_wallet.store();
      return true;
    }, 4*60*60*1000);
    //DO NOT START THIS SERVER IN MORE THEN 1 THREADS WITHOUT REFACTORING
    return epee::http_server_impl_base<wallet_rpc_server, connection_context>::run(1, true);
  }
//------------------------------------------------------------------------------------------------------------------------------
//not used now, it is not good to allow or deny ip address in this scope
//should allow or deny ip address on iptables or firewall
//------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::is_ip_allowed(uint32_t ip)
  {
	  if(m_ip_list_file.size() == 0 || m_ip_list.size() == 0) return true;

	  std::string str = string_tools::get_ip_string_from_int32(ip);
	  if(str == "[failed]") return false;
	  std::map<std::string,std::string>::iterator it = m_ip_list.find(str);

	  if(m_ip_list.end() != it) return true;
	  else return false;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::handle_command_line(const boost::program_options::variables_map& vm)
  {
    m_bind_ip = command_line::get_arg(vm, arg_rpc_bind_ip);
    m_port = command_line::get_arg(vm, arg_rpc_bind_port);
	//m_ip_list_file = command_line::get_arg(vm, arg_rpc_allow_ip);

	//arg_rpc_allow_ip
	if(m_ip_list_file.size() == 0) return true;

	if(m_bind_ip != "127.0.0.1")
	{
		if(!file_io_utils::is_file_exist(m_ip_list_file))
		{
			LOG_PRINT_L0("allow ip list file: " << m_ip_list_file << " is not exist, so it will be ignored.");
		}
		else
		{
			if(!file_io_utils::load_file_to_string_map(m_ip_list_file,m_ip_list))
			{
				m_ip_list.clear();
				LOG_PRINT_L0("error read allow ip list file : " << m_ip_list_file << ", so it will be ignored.");
			}
		}
	}
	else
	{
		LOG_PRINT_L0("rpc bind ip is loop address, so allow ip list will be ignored.");
	}
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::init(const boost::program_options::variables_map& vm)
  {
    m_net_server.set_threads_prefix("RPC");
    bool r = handle_command_line(vm);
    CHECK_AND_ASSERT_MES(r, false, "Failed to process command line in core_rpc_server");
    return epee::http_server_impl_base<wallet_rpc_server, connection_context>::init(m_port, m_bind_ip);
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::on_getbalance(const wallet_rpc::COMMAND_RPC_GET_BALANCE::request& req, wallet_rpc::COMMAND_RPC_GET_BALANCE::response& res, epee::json_rpc::error& er, connection_context& cntx)
  {
    try
    {
      res.balance = m_wallet.balance();
      res.unlocked_balance = m_wallet.unlocked_balance();
    }
    catch (std::exception& e)
    {
      er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
      er.message = e.what();
      return false;
    }
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::on_getaddress(const wallet_rpc::COMMAND_RPC_GET_ADDRESS::request& req, wallet_rpc::COMMAND_RPC_GET_ADDRESS::response& res, epee::json_rpc::error& er, connection_context& cntx)
  {
    try
    {
      res.address = m_wallet.get_account().get_public_address_str();
    }
    catch (std::exception& e)
    {
      er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
      er.message = e.what();
      return false;
    }
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::on_transfer(const wallet_rpc::COMMAND_RPC_TRANSFER::request& req, wallet_rpc::COMMAND_RPC_TRANSFER::response& res, epee::json_rpc::error& er, connection_context& cntx)
  {

    std::vector<currency::tx_destination_entry> dsts;
    for (auto it = req.destinations.begin(); it != req.destinations.end(); it++) 
    {
      currency::tx_destination_entry de;
      if(!m_wallet.get_transfer_address(it->address, de.addr))
      {
        er.code = WALLET_RPC_ERROR_CODE_WRONG_ADDRESS;
        er.message = std::string("WALLET_RPC_ERROR_CODE_WRONG_ADDRESS: ") + it->address;
        return false;
      }
      de.amount = it->amount;
      dsts.push_back(de);
    }
    try
    {
      std::vector<uint8_t> extra;
      crypto::hash payment_id = AUTO_VAL_INIT(payment_id);
      if(currency::parse_payment_id_from_hex_str(req.payment_id_hex, payment_id))
      {
        currency::set_payment_id_to_tx_extra(extra, payment_id);
      }

      currency::transaction tx;
      m_wallet.transfer(dsts, req.mixin, req.unlock_time, req.fee, extra, tx);
      res.tx_hash = boost::lexical_cast<std::string>(currency::get_transaction_hash(tx));
      return true;
    }
    catch (const tools::error::daemon_busy& e)
    {
      er.code = WALLET_RPC_ERROR_CODE_DAEMON_IS_BUSY;
      er.message = e.what();
      return false; 
    }
    catch (const std::exception& e)
    {
      er.code = WALLET_RPC_ERROR_CODE_GENERIC_TRANSFER_ERROR;
      er.message = e.what();
      return false; 
    }
    catch (...)
    {
      er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
      er.message = "WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR";
      return false; 
    }
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::on_store(const wallet_rpc::COMMAND_RPC_STORE::request& req, wallet_rpc::COMMAND_RPC_STORE::response& res, epee::json_rpc::error& er, connection_context& cntx)
  {
    try
    {
      m_wallet.store();
    }
    catch (std::exception& e)
    {
      er.code = WALLET_RPC_ERROR_CODE_UNKNOWN_ERROR;
      er.message = e.what();
      return false;
    }
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool wallet_rpc_server::on_get_payments(const wallet_rpc::COMMAND_RPC_GET_PAYMENTS::request& req, wallet_rpc::COMMAND_RPC_GET_PAYMENTS::response& res, epee::json_rpc::error& er, connection_context& cntx)
  {
    crypto::hash payment_id;
    currency::blobdata payment_id_blob;
    if(!epee::string_tools::parse_hexstr_to_binbuff(req.payment_id, payment_id_blob))
    {
      er.code = WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID;
      er.message = "Payment ID has invald format";
      return false;
    }

    if(sizeof(payment_id) != payment_id_blob.size())
    {
      er.code = WALLET_RPC_ERROR_CODE_WRONG_PAYMENT_ID;
      er.message = "Payment ID has invalid size";
      return false;
    }

    payment_id = *reinterpret_cast<const crypto::hash*>(payment_id_blob.data());

    res.payments.clear();
    std::list<wallet2::payment_details> payment_list;
    m_wallet.get_payments(payment_id, payment_list);
    for (auto payment : payment_list)
    {
      wallet_rpc::payment_details rpc_payment;
      rpc_payment.tx_hash      = epee::string_tools::pod_to_hex(payment.m_tx_hash);
      rpc_payment.amount       = payment.m_amount;
      rpc_payment.block_height = payment.m_block_height;
      rpc_payment.unlock_time  = payment.m_unlock_time;
      res.payments.push_back(rpc_payment);
    }

    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------

bool wallet_rpc_server::on_get_incoming_tx(const wallet_rpc::COMMAND_RPC_GET_INCOMING_TX::request& req, wallet_rpc::COMMAND_RPC_GET_INCOMING_TX::response& res, epee::json_rpc::error& er, connection_context& cntx)
{
	uint64_t count = m_wallet.get_incoming_tx_size();
	if(req.tx_start_index > count || req.tx_start_index < 0 )
	{
      er.code = WALLET_RPC_ERROR_CODE_WRONG_TX_START_INDEX;
	  er.message = "Transaction start index should be within 0 to " + std::to_string(count);
      return false;
    }
	uint64_t tx_get_count = req.tx_get_count;
	if(req.tx_get_count + req.tx_start_index > count || req.tx_get_count <= 0 )
	{
      tx_get_count = count - req.tx_start_index;
    }

	std::vector<tools::wallet_rpc::wallet_transfer_info> recent;
	m_wallet.get_recent_transfers_history(recent, req.tx_start_index, tx_get_count, false);  

	res.tx_total_count = count;

	tools::wallet_rpc::incoming_tx_details in_tx;
	for (auto & wti : recent)
	{
		if (!wti.fee) wti.fee = currency::get_tx_fee(wti.tx);

		in_tx.amount = wti.amount;
		in_tx.block_height = wti.height;
		in_tx.payment_id = wti.payment_id;
		in_tx.tx_hash = wti.tx_hash;
		in_tx.unlock_time = wti.unlock_time;
		in_tx.timestamp = wti.timestamp;

		res.incoming_txs.push_back(in_tx);
	}
	return true;	
}
}


