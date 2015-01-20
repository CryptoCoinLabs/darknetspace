// Copyright (c) 2012-2013 The Boolberry developers
// Copyright (c) 2014-2014 The DarkNetSpace developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "include_base_utils.h"
#include "daemon_backend.h"
#include "currency_core/alias_helper.h"
#include <boost/algorithm/string.hpp>


daemon_backend::daemon_backend():m_pview(&m_view_stub),
                                 m_stop_singal_sent(false),
                                 m_ccore(&m_cprotocol),
                                 m_cprotocol(m_ccore, &m_p2psrv),
                                 m_p2psrv(m_cprotocol),
                                 m_rpc_server(m_ccore, m_p2psrv),
                                 m_rpc_proxy(m_rpc_server),
                                 m_last_daemon_height(0),
                                 m_last_wallet_synch_height(0)

{
  m_wallet.reset(new tools::wallet2());
  m_wallet->callback(this);
}

const command_line::arg_descriptor<bool> arg_alloc_win_console = {"alloc-win-clonsole", "Allocates debug console with GUI", false};
const command_line::arg_descriptor<std::string> arg_html_folder = {"html-path", "Manually set GUI html folder path", "",  true};

daemon_backend::~daemon_backend()
{
  stop();
}

bool daemon_backend::start(int argc, char* argv[], view::i_view* pview_handler)
{
  m_stop_singal_sent = false;
  if(pview_handler)
    m_pview = pview_handler;

  view::daemon_status_info dsi = AUTO_VAL_INIT(dsi);
  dsi.difficulty = "---";
  dsi.text_state = "Initializing...";
  pview_handler->update_daemon_status(dsi);

  //#ifdef WIN32
  //_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
  //#endif

  log_space::get_set_log_detalisation_level(true, LOG_LEVEL_0);
  LOG_PRINT_L0("Initing...");

  TRY_ENTRY();
  po::options_description desc_cmd_only("Command line options");
  po::options_description desc_cmd_sett("Command line options and settings options");

  command_line::add_arg(desc_cmd_only, command_line::arg_help);
  command_line::add_arg(desc_cmd_only, command_line::arg_version);
  command_line::add_arg(desc_cmd_only, command_line::arg_os_version);
  // tools::get_default_data_dir() can't be called during static initialization
  command_line::add_arg(desc_cmd_only, command_line::arg_data_dir, tools::get_default_data_dir());
  command_line::add_arg(desc_cmd_only, command_line::arg_config_file);

  command_line::add_arg(desc_cmd_sett, command_line::arg_log_file);
  command_line::add_arg(desc_cmd_sett, command_line::arg_log_level);
  command_line::add_arg(desc_cmd_sett, command_line::arg_console);
  command_line::add_arg(desc_cmd_sett, command_line::arg_show_details);
  command_line::add_arg(desc_cmd_sett, arg_alloc_win_console);
  command_line::add_arg(desc_cmd_sett, arg_html_folder);

  currency::core::init_options(desc_cmd_sett);
  currency::core_rpc_server::init_options(desc_cmd_sett);
  nodetool::node_server<currency::t_currency_protocol_handler<currency::core> >::init_options(desc_cmd_sett);
  currency::miner::init_options(desc_cmd_sett);

  po::options_description desc_options("Allowed options");
  desc_options.add(desc_cmd_only).add(desc_cmd_sett);

  po::variables_map vm;
  bool r = command_line::handle_error_helper(desc_options, [&]()
  {
    po::store(po::parse_command_line(argc, argv, desc_options), vm);

    if (command_line::get_arg(vm, command_line::arg_help))
    {
      std::cout << CURRENCY_NAME << " v" << PROJECT_VERSION_LONG << ENDL << ENDL;
      std::cout << desc_options << std::endl;
      return false;
    }

    m_data_dir = command_line::get_arg(vm, command_line::arg_data_dir);
    std::string config = command_line::get_arg(vm, command_line::arg_config_file);

    boost::filesystem::path data_dir_path(m_data_dir);
    boost::filesystem::path config_path(config);
    if (!config_path.has_parent_path())
    {
      config_path = data_dir_path / config_path;
    }

    boost::system::error_code ec;
    if (boost::filesystem::exists(config_path, ec))
    {
      po::store(po::parse_config_file<char>(config_path.string<std::string>().c_str(), desc_cmd_sett), vm);
    }
    po::notify(vm);

    return true;
  });
  if (!r)
    return false;

  //set up logging options
  if (command_line::has_arg(vm, arg_alloc_win_console))
  {
     log_space::log_singletone::add_logger(LOGGER_CONSOLE, NULL, NULL);
  }

  std::string path_to_html;
  if (!command_line::has_arg(vm, arg_html_folder))
  {
    path_to_html = string_tools::get_current_module_folder() + "/html";
  }
  else
  {
    path_to_html = command_line::get_arg(vm, arg_html_folder);
  }

  m_pview->set_html_path(path_to_html);

  boost::filesystem::path log_file_path(command_line::get_arg(vm, command_line::arg_log_file));
  if (log_file_path.empty())
    log_file_path = log_space::log_singletone::get_default_log_file();
  std::string log_dir;
  log_dir = log_file_path.has_parent_path() ? log_file_path.parent_path().string() : log_space::log_singletone::get_default_log_folder();

  log_space::log_singletone::add_logger(LOGGER_FILE, log_file_path.filename().string().c_str(), log_dir.c_str());
  LOG_PRINT_L0(CURRENCY_NAME << " v" << PROJECT_VERSION_LONG);

  LOG_PRINT("Module folder: " << argv[0], LOG_LEVEL_0);

  bool res = true;
  currency::checkpoints checkpoints;
  res = currency::create_checkpoints(checkpoints);
  CHECK_AND_ASSERT_MES(res, false, "Failed to initialize checkpoints");
  m_ccore.set_checkpoints(std::move(checkpoints));

  m_main_worker_thread = std::thread([this, vm](){main_worker(vm);});

  return true;
  CATCH_ENTRY_L0("main", 1);
 }

bool daemon_backend::send_stop_signal()
{
  m_stop_singal_sent = true;
  return true;
}

bool daemon_backend::stop()
{
  send_stop_signal();
  if(m_main_worker_thread.joinable())
    m_main_worker_thread.join();

  return true;
}

std::string daemon_backend::get_config_folder()
{
  return m_data_dir;
}

void daemon_backend::main_worker(const po::variables_map& vm)
{

#define CHECK_AND_ASSERT_AND_SET_GUI(cond, res, mess) \
  if (!cond) \
  { \
    LOG_ERROR(mess); \
    dsi.daemon_network_state = 4; \
    dsi.text_state = mess; \
    m_pview->update_daemon_status(dsi); \
    m_pview->on_backend_stopped(); \
    return res; \
  }

  view::daemon_status_info dsi = AUTO_VAL_INIT(dsi);
  dsi.difficulty = "---";
  m_pview->update_daemon_status(dsi);

  //initialize objects
  LOG_PRINT_L0("Initializing p2p server...");
  dsi.text_state = "Initializing p2p server";
  m_pview->update_daemon_status(dsi);
  bool res = m_p2psrv.init(vm);
  CHECK_AND_ASSERT_AND_SET_GUI(res, void(), "Failed to initialize p2p server.");
  LOG_PRINT_L0("P2p server initialized OK on port: " << m_p2psrv.get_this_peer_port());

  //LOG_PRINT_L0("Starting UPnP");
  //upnp_helper.run_port_mapping_loop(p2psrv.get_this_peer_port(), p2psrv.get_this_peer_port(), 20*60*1000);

  LOG_PRINT_L0("Initializing currency protocol...");
  dsi.text_state = "Initializing currency protocol";
  m_pview->update_daemon_status(dsi);
  res = m_cprotocol.init(vm);
  CHECK_AND_ASSERT_AND_SET_GUI(res, void(), "Failed to initialize currency protocol.");
  LOG_PRINT_L0("Currency protocol initialized OK");

  LOG_PRINT_L0("Initializing core rpc server...");
  dsi.text_state = "Initializing core rpc server";
  m_pview->update_daemon_status(dsi);
  res = m_rpc_server.init(vm);
  CHECK_AND_ASSERT_AND_SET_GUI(res, void(), "Failed to initialize core rpc server.");
  LOG_PRINT_GREEN("Core rpc server initialized OK on port: " << m_rpc_server.get_binded_port(), LOG_LEVEL_0);

  //initialize core here
  LOG_PRINT_L0("Initializing core...");
  dsi.text_state = "Initializing core";
  m_pview->update_daemon_status(dsi);
  res = m_ccore.init(vm);
  CHECK_AND_ASSERT_AND_SET_GUI(res, void(), "Failed to initialize core");
  LOG_PRINT_L0("Core initialized OK");

  LOG_PRINT_L0("Starting core rpc server...");
  dsi.text_state = "Starting core rpc server";
  m_pview->update_daemon_status(dsi);
     
  res = m_rpc_server.run(2, false);
  CHECK_AND_ASSERT_AND_SET_GUI(res, void(), "Failed to initialize core rpc server.");
  LOG_PRINT_L0("Core rpc server started ok");

  LOG_PRINT_L0("Starting p2p net loop...");
  dsi.text_state = "Starting network loop";
  m_pview->update_daemon_status(dsi);
  res = m_p2psrv.run(false);
  CHECK_AND_ASSERT_AND_SET_GUI(res, void(), "Failed to run p2p loop.");
  LOG_PRINT_L0("p2p net loop stopped");

  //go to monitoring view loop
  loop();

  dsi.daemon_network_state = 3;

  CRITICAL_REGION_BEGIN(m_wallet_lock);
  if(m_wallet->get_wallet_path().size())
  {
    LOG_PRINT_L0("Storing wallet data...");
    dsi.text_state = "Storing wallet data...";
    m_pview->update_daemon_status(dsi);
    m_wallet->store();
  }
  CRITICAL_REGION_END();

  LOG_PRINT_L0("Stopping core p2p server...");
  dsi.text_state = "Stopping p2p network server";
  m_pview->update_daemon_status(dsi);
  m_p2psrv.send_stop_signal();
  m_p2psrv.timed_wait_server_stop(10);

  //stop components
  LOG_PRINT_L0("Stopping core rpc server...");
  dsi.text_state = "Stopping rpc network server";
  m_pview->update_daemon_status(dsi);

  m_rpc_server.send_stop_signal();
  m_rpc_server.timed_wait_server_stop(5000);

  //deinitialize components

  LOG_PRINT_L0("Deinitializing core...");
  dsi.text_state = "Deinitializing core";
  m_pview->update_daemon_status(dsi);
  m_ccore.deinit();


  LOG_PRINT_L0("Deinitializing rpc server ...");
  dsi.text_state = "Deinitializing rpc server";
  m_pview->update_daemon_status(dsi);
  m_rpc_server.deinit();


  LOG_PRINT_L0("Deinitializing currency_protocol...");
  dsi.text_state = "Deinitializing currency_protocol";
  m_pview->update_daemon_status(dsi);
  m_cprotocol.deinit();


  LOG_PRINT_L0("Deinitializing p2p...");
  dsi.text_state = "Deinitializing p2p";
  m_pview->update_daemon_status(dsi);

  m_p2psrv.deinit();

  m_ccore.set_currency_protocol(NULL);
  m_cprotocol.set_p2p_endpoint(NULL);

  LOG_PRINT("Node stopped.", LOG_LEVEL_0);
  dsi.text_state = "Node stopped";
  m_pview->update_daemon_status(dsi);

  m_pview->on_backend_stopped();
}

bool daemon_backend::enable_proxy(bool bEnabled, std::string ip_address, short port)
{
	std::string ip_set;
	int port_set = 0;
	bool bOldEnabled = m_p2psrv.get_proxy_status(ip_set, port_set);
	if (bEnabled == bOldEnabled) return true;
	return m_p2psrv.enable_proxy(bEnabled, true, ip_address, port);
}

bool daemon_backend::test_proxy(const std::string ip_address, const int port, std::string & err)
{
	uint32_t ip = 0;
	bool bReturn = string_tools::get_ip_int32_from_string(ip, ip_address);
	if (!bReturn) { LOG_ERROR("test_proxy: wrong ip address"); return false; }

	if (port <0 || port >65535) { LOG_ERROR("test_proxy: wrong port"); return false; }
	return Socks5(ip_address, port, err);
}

bool daemon_backend::update_state_info(uint64_t &state)
{
  view::daemon_status_info dsi = AUTO_VAL_INIT(dsi);
  dsi.difficulty = "---";
  currency::COMMAND_RPC_GET_INFO::response inf = AUTO_VAL_INIT(inf);
  if(!m_rpc_proxy.get_info(inf))
  {
    dsi.text_state = "get_info failed";
    m_pview->update_daemon_status(dsi);
    LOG_ERROR("Failed to call get_info");
    return false;
  }
  dsi.difficulty = std::to_string(inf.difficulty);
  dsi.hashrate = inf.current_network_hashrate_350;
  dsi.inc_connections_count = inf.incoming_connections_count;
  dsi.out_connections_count = inf.outgoing_connections_count;

  state = inf.daemon_network_state;
  switch(inf.daemon_network_state)
  {
  case currency::COMMAND_RPC_GET_INFO::daemon_network_state_connecting:     dsi.text_state = "Connecting";break;
  case currency::COMMAND_RPC_GET_INFO::daemon_network_state_online:         dsi.text_state = "Online";break;
  case currency::COMMAND_RPC_GET_INFO::daemon_network_state_synchronizing:  dsi.text_state = "Synchronizing";break;
  default: dsi.text_state = "unknown";break;
  }
  dsi.daemon_network_state = inf.daemon_network_state; 
  dsi.synchronization_start_height = inf.synchronization_start_height;
  dsi.max_net_seen_height = inf.max_net_seen_height;

  //staticitics
  dsi.alias_count = inf.alias_count;
  dsi.tx_count = inf.tx_count;
  dsi.transactions_cnt_per_day = inf.transactions_cnt_per_day;
  dsi.tx_pool_size = inf.tx_pool_size;
  dsi.transactions_volume_per_day = inf.transactions_volume_per_day;
  dsi.peer_count = inf.white_peerlist_size+inf.grey_peerlist_size;
  dsi.white_peerlist_size = inf.white_peerlist_size;
  dsi.grey_peerlist_size = inf.grey_peerlist_size;

  dsi.last_build_available = std::to_string(inf.mi.ver_major)
    + "." + std::to_string(inf.mi.ver_minor)
    + "." + std::to_string(inf.mi.ver_revision)
    + "." + std::to_string(inf.mi.build_no);

  if (inf.mi.mode)
  {
    dsi.last_build_displaymode = inf.mi.mode + 1;
  }
  else
  {
    if (inf.mi.build_no > PROJECT_VERSION_BUILD_NO)
      dsi.last_build_displaymode = view::ui_last_build_displaymode::ui_lb_dm_new;
    else
    {
      dsi.last_build_displaymode = view::ui_last_build_displaymode::ui_lb_dm_actual;
    }
  }


  m_last_daemon_height = dsi.height = inf.height;

  m_pview->update_daemon_status(dsi);
  return true;
}

bool daemon_backend::update_wallets()
{
	CRITICAL_REGION_LOCAL(m_wallet_lock);
	view::wallet_status_info wsi = AUTO_VAL_INIT(wsi);

	if (m_wallet->get_wallet_path().size())
	{
		//wallet is opened
		if (m_last_daemon_height != m_last_wallet_synch_height)
		{
			update_wallet_info();
			static view::wallet_recent_transfers t(true);
			m_pview->show_wallet(t);
			
			wsi.wallet_state = view::wallet_status_info::wallet_state_synchronizing;
			m_pview->update_wallet_status(wsi);
			if (t.bclear_recent_transfers){ load_recent_transfers(); t.bclear_recent_transfers = false; }

			try
			{
				m_wallet->refresh();
			}

			catch (const tools::error::daemon_busy& /*e*/)
			{
				LOG_PRINT_L0("Daemon busy while wallet refresh");
				return true;
			}

			catch (const std::exception& e)
			{
				LOG_PRINT_L0("Failed to refresh wallet: " << e.what());
				return false;
			}

			catch (...)
			{
				LOG_PRINT_L0("Failed to refresh wallet, unknown exception");
				return false;
			}
			m_last_wallet_synch_height = m_ccore.get_current_blockchain_height();
			wsi.wallet_state = view::wallet_status_info::wallet_state_ready;
			m_pview->update_wallet_status(wsi);

					
		}

		// scan for unconfirmed trasactions
		try
		{
			m_wallet->scan_tx_pool();
		}

		catch (const tools::error::daemon_busy& /*e*/)
		{
			LOG_PRINT_L0("Daemon busy while wallet refresh");
			return true;
		}

		catch (const std::exception& e)
		{
			LOG_PRINT_L0("Failed to refresh wallet: " << e.what());
			return false;
		}

		catch (...)
		{
			LOG_PRINT_L0("Failed to refresh wallet, unknown exception");
			return false;
		}
	}
	return true;
}

void daemon_backend::loop()
{
  while(!m_stop_singal_sent)
  {
	 uint64_t state = 0;
    update_state_info(state);
	if (state == currency::COMMAND_RPC_GET_INFO::daemon_network_state_online)
	{
		update_wallets();
	}
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

bool daemon_backend::open_wallet(const std::string& path, const std::string& password)
{
  CRITICAL_REGION_LOCAL(m_wallet_lock);
  try
  {
    if (m_wallet->get_wallet_path().size())
    {
      m_wallet->store();
      m_wallet.reset(new tools::wallet2());
      m_wallet->callback(this);
    }
    
    m_wallet->load(path, password);
  }
  catch (const std::exception& e)
  {
    m_pview->show_msg_box(std::string("Failed to load wallet: ") + e.what());
    m_wallet.reset(new tools::wallet2());
    m_wallet->callback(this);
    return false;
  }

  m_wallet->init(std::string("127.0.0.1:") + std::to_string(m_rpc_server.get_binded_port()));
  update_wallet_info();

  view::wallet_recent_transfers t(true);
  m_pview->show_wallet(t);

  load_recent_transfers();
  m_last_wallet_synch_height = 0;
  return true;
}

//----------------------------------------------------------------------------------------------------
bool daemon_backend::change_password(const view::change_password_params cpp)
{
	bool r = false;
	if(cpp.new_password.size()==0) 
	{
		m_pview->show_msg_box("new password can't be empty");
		return r;
	}

	try
	{
		r = m_wallet->changepassword(cpp.old_password, cpp.new_password);
	}
	catch (const std::exception& e)
	{
		m_pview->show_msg_box(std::string("Failed to change password: ") + e.what());
		return r;
	}

	return true;
}

bool daemon_backend::load_recent_transfers()
{
  view::transfers_array tr_hist;
  m_wallet->get_recent_transfers_history(tr_hist.history, 0, 1000);
  m_wallet->get_unconfirmed_transfers(tr_hist.unconfirmed);
  //workaround for missed fee
  for (auto & he : tr_hist.history)
    if (!he.fee && !currency::is_coinbase(he.tx)) 
       he.fee = currency::get_tx_fee(he.tx);

  for (auto & he : tr_hist.unconfirmed)
    if (!he.fee && !currency::is_coinbase(he.tx)) 
       he.fee = currency::get_tx_fee(he.tx);

  return m_pview->set_recent_transfers(tr_hist);
}

bool daemon_backend::generate_wallet(const std::string& path, const std::string& password)
{
  CRITICAL_REGION_LOCAL(m_wallet_lock);
  try
  {
    if (m_wallet->get_wallet_path().size())
    {
      m_wallet->store();
      m_wallet.reset(new tools::wallet2());
      m_wallet->callback(this);
    }

    m_wallet->generate(path, password);
  }
  catch (const std::exception& e)
  {
    m_pview->show_msg_box(std::string("Failed to generate wallet: ") + e.what());
    m_wallet.reset(new tools::wallet2());
    m_wallet->callback(this);
    return false;
  }

  m_wallet->init(std::string("127.0.0.1:") + std::to_string(m_rpc_server.get_binded_port()));
  update_wallet_info();
  m_last_wallet_synch_height = 0;

  view::wallet_recent_transfers t(true);
  m_pview->show_wallet(t);
  return true;

}

bool daemon_backend::close_wallet()
{
  CRITICAL_REGION_LOCAL(m_wallet_lock);
  try
  {
    if (m_wallet->get_wallet_path().size())
    {
      m_wallet->store();
      m_wallet.reset(new tools::wallet2());
      m_wallet->callback(this);
    }
  }

  catch (const std::exception& e)
  {
    m_pview->show_msg_box(std::string("Failed to close wallet: ") + e.what());
    return false;
  }
  m_pview->hide_wallet();
  return true;
}

bool daemon_backend::get_aliases(view::alias_set& al_set)
{
  currency::COMMAND_RPC_GET_ALL_ALIASES::response aliases = AUTO_VAL_INIT(aliases);
  if (m_rpc_proxy.get_aliases(aliases) && aliases.status == CORE_RPC_STATUS_OK)
  {
    al_set.aliases = aliases.aliases;
    return true;
  }

  return false;
}

//------------------------------------------------------------------------------------------------------------------
//return false:  alias not found
//return true:   unkown or exists
//------------------------------------------------------------------------------------------------------------------
bool daemon_backend::is_alias_exist(const std::string& alias)
{
	currency::COMMAND_RPC_GET_ALIAS_DETAILS::response res = AUTO_VAL_INIT(res);
	bool r = m_rpc_proxy.get_alias_info(alias,res);

	if(r == true && res.status == "Alias not found")
		return false;
	else
		return true;
}

bool daemon_backend::make_alias(const view::make_alias_params& alias, currency::transaction& res_tx)
{
	if (alias.alias.size() == 0 || alias.alias.size() > 33)
	{
		m_pview->show_msg_box("Alias name can't be empty or less than 32 bytes");
		return false;
	}
	std::string str;
	str.resize(alias.alias.size());
	std::transform(alias.alias.begin(), alias.alias.end(), str.begin(), (int(*)(int))std::tolower);

	if(!currency::validate_alias_name(str))
	{
		m_pview->show_msg_box("Wrong alias name");
		return false;
	}

	if(is_alias_exist(str))
	{
		m_pview->show_msg_box("Alias already exists");
		return false;
	}
	
	currency::alias_info ai = AUTO_VAL_INIT(ai);
	ai.m_alias = str;

	if (!currency::get_account_address_from_str(ai.m_address, m_wallet->get_account().get_public_address_str()))
	{
		m_pview->show_msg_box("Address has wrong format");
		return false;
	}
	
	if(alias.comment.size())
	{
		if(alias.comment.size() > 64)
		{
			m_pview->show_msg_box("Comment too long, must be less 64 chars.");
			return false;
		}
		ai.m_text_comment = alias.comment;	
	}

	if(alias.tracking_key.size())
	{
		str = alias.tracking_key;
		if (str.size() != 64)
		{
			std::cout << "viewkey has wrong length, must be 64 chars  or empty." << std::endl;
			return true;
		}
		std::string bin_str;
		epee::string_tools::parse_hexstr_to_binbuff(str, bin_str);
		ai.m_view_key = *reinterpret_cast<const crypto::secret_key*>(bin_str.c_str());
	}

  std::vector<uint8_t> extra;
  std::string buff;
  bool r = currency::make_tx_extra_alias_entry(buff, ai);
  if(!r) return false;
  extra.resize(buff.size());
  memcpy(&extra[0], buff.data(), buff.size());

  view::transfer_params tp = AUTO_VAL_INIT(tp);
  view::transfer_destination td = AUTO_VAL_INIT(td);
  td.address = CURRENCY_DONATIONS_ADDRESS;
  td.amount = string_tools::num_to_string_fast(1);
  
  tp.destinations.push_back(td);
  tp.fee = string_tools::num_to_string_fast(MAKE_ALIAS_MINIMUM_FEE/COIN);
  tp.mixin_count = 0;
  tp.lock_time = 0;

  return send_tx(tp,res_tx,extra);
}

bool daemon_backend::transfer(const view::transfer_params& tp, currency::transaction& res_tx)
{
	std::vector<uint8_t> extra;
	return send_tx(tp,res_tx,extra);
}

bool daemon_backend::send_tx(const view::transfer_params& tp, currency::transaction& res_tx, std::vector<uint8_t> &extra)
{
  std::vector<currency::tx_destination_entry> dsts;
  if(!tp.destinations.size())
  {
    m_pview->show_msg_box("Internal error: empty destinations");
    return false;
  }
  uint64_t fee = 0;
  if (!currency::parse_amount(fee, tp.fee))
  {
    m_pview->show_msg_box("Failed to send transaction: wrong fee amount");
    return false;
  }

  for(auto& d: tp.destinations)
  {
	uint64_t amount = 0;
    if(!currency::parse_amount(amount, d.amount))
    {
      m_pview->show_msg_box("Failed to send transaction: wrong amount");
      return false;
    }

	if(amount < DEFAULT_FEE) 
    {
      m_pview->show_msg_box("Failed to send transaction: amount must be greater than DEFAULT_FEE: " +  std::to_string(DEFAULT_FEE/COIN));
      return false;
    }
	dsts.push_back(currency::tx_destination_entry());
	dsts.back().amount = amount;
    if (!tools::get_transfer_address(d.address, dsts.back().addr, m_rpc_proxy))
    {
      m_pview->show_msg_box("Failed to send transaction: invalid address");
      return false;
    }
  }
  //payment_id
  if(tp.payment_id.size())
  {
    crypto::hash payment_id;
    if(!currency::parse_payment_id_from_hex_str(tp.payment_id, payment_id))
    {
      m_pview->show_msg_box("Failed to send transaction: wrong payment_id");
      return false;
    }
    if(!currency::set_payment_id_to_tx_extra(extra, payment_id))
    {
      m_pview->show_msg_box("Failed to send transaction: internal error, failed to set payment id");
      return false;
    }
  }

  try
  { 
    //set transaction unlock time if it was specified by user 
    uint64_t unlock_time = 0;
    if (tp.lock_time)
      unlock_time = m_wallet->get_blockchain_current_height() + tp.lock_time;

    m_wallet->transfer(dsts, tp.mixin_count, unlock_time ? unlock_time + 1:0, fee, extra, res_tx);
    update_wallet_info();
  }
  catch (const std::exception& e)
  {
    LOG_ERROR("Transfer error: " << e.what());
    m_pview->show_msg_box(std::string("Failed to send transaction: ") + e.what());
    return false;
  }
  catch (...)
  {
    LOG_ERROR("Transfer error: unknown error");
    m_pview->show_msg_box("Failed to send transaction: unknown error");
    return false;
  }

  return true;
}

bool daemon_backend::update_wallet_info()
{
  CRITICAL_REGION_LOCAL(m_wallet_lock);
  view::wallet_info wi = AUTO_VAL_INIT(wi);
  wi.address = m_wallet->get_account().get_public_address_str();
  wi.tracking_key = string_tools::pod_to_hex(m_wallet->get_account().get_keys().m_view_secret_key);
  wi.balance = m_wallet->balance();
  wi.unlocked_balance = m_wallet->unlocked_balance();
  wi.path = m_wallet->get_wallet_path();
  m_pview->update_wallet_info(wi);
  return true;
}

void daemon_backend::on_new_block(uint64_t height, const currency::block& block)
{
	if (height <= m_wallet->get_blockchain_current_height()) return;
	uint64_t count = height - m_wallet->get_blockchain_current_height();

	view::wallet_left_height wi = AUTO_VAL_INIT(wi);
	wi.left_height = count;

//	m_pview->set_left_height(wi);
}

void daemon_backend::on_transfer2(const tools::wallet_rpc::wallet_transfer_info& wti)
{
  view::transfer_event_info tei = AUTO_VAL_INIT(tei);
  tei.ti = wti;
  tei.balance = m_wallet->balance();
  tei.unlocked_balance = m_wallet->unlocked_balance();
  m_pview->money_transfer(tei);
}

