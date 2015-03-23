// Copyright (c) 2012-2013 The Boolberry developers
// Copyright (c) 2014-2014 The DarkNetSpace developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/core_rpc_server.h"
#include "net/http_client.h"
#include "wallet/wallet2.h"

namespace tools
{
  class daemon_rpc_proxy_fast
  {
  public:
	  daemon_rpc_proxy_fast(currency::core_rpc_server& rpc_srv, epee::net_utils::http::http_simple_client &http_client, std::string &daemon_address, bool & is_lightwallet_enabled) : \
		  m_rpc(rpc_srv), m_http_client(http_client), m_daemon_address(daemon_address), m_is_lightwallet_enabled(is_lightwallet_enabled)
    {}

    bool get_info(currency::COMMAND_RPC_GET_INFO::response& res)
    {
		currency::COMMAND_RPC_GET_INFO::request req = AUTO_VAL_INIT(req);

		if (!m_is_lightwallet_enabled)
		{
			currency::core_rpc_server::connection_context stub_cntxt = AUTO_VAL_INIT(stub_cntxt);
			return m_rpc.on_get_info(req, res, stub_cntxt);
		}

		bool r = net_utils::invoke_http_json_remote_command2(m_daemon_address + "/getinfo", req, res, m_http_client, WALLET_RCP_CONNECTION_TIMEOUT);
		CHECK_AND_THROW_WALLET_EX(!r, error::no_connection_to_daemon, "getinfo");
		CHECK_AND_THROW_WALLET_EX(res.status == CORE_RPC_STATUS_BUSY, error::daemon_busy, "getinfo");
		CHECK_AND_THROW_WALLET_EX(res.status != CORE_RPC_STATUS_OK, error::get_blocks_error, res.status);

		return true;
    }

	bool get_aliases(currency::COMMAND_RPC_GET_ALL_ALIASES::response& res)
    {		
		currency::COMMAND_RPC_GET_ALL_ALIASES::request req = AUTO_VAL_INIT(req);

		if (!m_is_lightwallet_enabled)
		{
			currency::core_rpc_server::connection_context stub_cntxt = AUTO_VAL_INIT(stub_cntxt);
			epee::json_rpc::error error_resp;
			return m_rpc.on_get_all_aliases(req, res, error_resp, stub_cntxt);
		}

		bool r = net_utils::invoke_http_json_remote_command2(m_daemon_address + "/get_all_alias_details", req, res, m_http_client, WALLET_RCP_CONNECTION_TIMEOUT);
		CHECK_AND_THROW_WALLET_EX(!r, error::no_connection_to_daemon, "get_all_alias_details");
		CHECK_AND_THROW_WALLET_EX(res.status == CORE_RPC_STATUS_BUSY, error::daemon_busy, "get_all_alias_details");
		CHECK_AND_THROW_WALLET_EX(res.status != CORE_RPC_STATUS_OK, error::get_blocks_error, res.status);

		return true;
    }

	uint64_t get_height()
	{
		currency::COMMAND_RPC_GET_HEIGHT::request req = AUTO_VAL_INIT(req);
		currency::COMMAND_RPC_GET_HEIGHT::response res = AUTO_VAL_INIT(res);

		if (!m_is_lightwallet_enabled)
		{
			currency::core_rpc_server::connection_context stub_cntxt = AUTO_VAL_INIT(stub_cntxt);
			m_rpc.on_get_height(req, res, stub_cntxt);
			return res.height;
		}

		bool r = net_utils::invoke_http_json_remote_command2(m_daemon_address + "/getheight", req, res, m_http_client, WALLET_RCP_CONNECTION_TIMEOUT);
		CHECK_AND_THROW_WALLET_EX(!r, error::no_connection_to_daemon, "getheight");
		CHECK_AND_THROW_WALLET_EX(res.status == CORE_RPC_STATUS_BUSY, error::daemon_busy, "getheight");
		CHECK_AND_THROW_WALLET_EX(res.status != CORE_RPC_STATUS_OK, error::get_blocks_error, res.status);
		return res.height;
	}

	bool get_alias_info(const std::string& alias, currency::COMMAND_RPC_GET_ALIAS_DETAILS::response& res)
    {
		currency::COMMAND_RPC_GET_ALIAS_DETAILS::request req = AUTO_VAL_INIT(req);

		if (!m_is_lightwallet_enabled)
		{
			currency::core_rpc_server::connection_context stub_cntxt = AUTO_VAL_INIT(stub_cntxt);
			req.alias = alias;
			epee::json_rpc::error error_resp;
			return m_rpc.on_get_alias_details(req, res, error_resp, stub_cntxt);
		}

		bool r = net_utils::invoke_http_json_remote_command2(m_daemon_address + "/get_alias_details", req, res, m_http_client, WALLET_RCP_CONNECTION_TIMEOUT);
		CHECK_AND_THROW_WALLET_EX(!r, error::no_connection_to_daemon, "get_alias_details");
		CHECK_AND_THROW_WALLET_EX(res.status == CORE_RPC_STATUS_BUSY, error::daemon_busy, "get_alias_details");
		CHECK_AND_THROW_WALLET_EX(res.status != CORE_RPC_STATUS_OK, error::get_blocks_error, res.status);
		return true;
    }


  private:
    currency::core_rpc_server & m_rpc;
	epee::net_utils::http::http_simple_client &m_http_client;
	std::string &m_daemon_address;
	bool &m_is_lightwallet_enabled;
  };

}



