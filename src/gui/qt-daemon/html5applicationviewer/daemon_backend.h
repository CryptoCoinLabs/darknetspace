// Copyright (c) 2012-2013 The Boolberry developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <boost/program_options.hpp>
#include "version.h"

using namespace epee;

#include "console_handler.h"
#include "p2p/net_node.h"
#include "currency_core/checkpoints_create.h"
#include "currency_core/currency_core.h"
#include "rpc/core_rpc_server.h"
#include "currency_protocol/currency_protocol_handler.h"
#include "daemon/daemon_commands_handler.h"
//#include "common/miniupnp_helper.h"
#include "view_iface.h"
#include "daemon_rpc_proxy.h"
#include "wallet/wallet2.h"

POP_WARNINGS

namespace po = boost::program_options;

#if defined(WIN32)
#include <crtdbg.h>
#endif

//TODO: need refactoring here. (template classes can't be used in BOOST_CLASS_VERSION)
BOOST_CLASS_VERSION(nodetool::node_server<currency::t_currency_protocol_handler<currency::core> >, CURRENT_P2P_STORAGE_ARCHIVE_VER);

class daemon_backend: public tools::i_wallet2_callback
{
public:
  daemon_backend();
  ~daemon_backend();
  bool start(int argc, char* argv[], view::i_view* pview_handler);
  bool stop();
  bool send_stop_signal();
  bool open_wallet(const std::string& path, const std::string& password);
  bool generate_wallet(const std::string& path, const std::string& password);
  bool change_password(const view::change_password_params cpp);
  bool make_alias(const view::make_alias_params& alias, currency::transaction& res_tx);
  bool close_wallet();
  bool transfer(const view::transfer_params& tp, currency::transaction& res_tx);
  bool send_tx(const view::transfer_params& tp, currency::transaction& res_tx, std::vector<uint8_t> &extra);
  bool get_aliases(view::alias_set& al_set);
  bool is_alias_exist(const std::string& alias);
  bool enable_proxy(bool bEnabled, std::string ip_address = "127.0.0.1", short port = 9050);
  bool test_proxy(const std::string ip_address, const int port, std::string & err);

  std::string get_config_folder();

  void set_wallet_callback(bool bStart)
  {
	  if (bStart) m_cprotocol.set_wallet_callback(m_wallet->process_new_block); 
	  else m_cprotocol.set_wallet_callback(NULL);
  }

private:
  void main_worker(const po::variables_map& vm);
  bool update_state_info(uint64_t &state);
  bool update_wallets();
  void loop();
  bool update_wallet_info();
  bool load_recent_transfers();
  bool get_transfer_address(const std::string& adr_str, currency::account_public_address& addr);


  
  //----- tools::i_wallet2_callback ------
  virtual void on_new_block(uint64_t height, const currency::block& block);
  virtual void on_transfer2(const tools::wallet_rpc::wallet_transfer_info& wti);

  std::thread m_main_worker_thread;
  std::atomic<bool> m_stop_singal_sent;
  view::view_stub m_view_stub;
  view::i_view* m_pview;
  tools::daemon_rpc_proxy_fast m_rpc_proxy;
  critical_section m_wallet_lock;
  std::unique_ptr<tools::wallet2> m_wallet;
  std::atomic<uint64_t> m_last_daemon_height;
  std::atomic<uint64_t> m_last_wallet_synch_height;
  std::string m_data_dir;

  

  //daemon stuff
  currency::core m_ccore;
  currency::t_currency_protocol_handler<currency::core> m_cprotocol;
  nodetool::node_server<currency::t_currency_protocol_handler<currency::core> > m_p2psrv;
  currency::core_rpc_server m_rpc_server;
};





