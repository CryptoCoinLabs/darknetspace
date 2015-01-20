// Copyright (c) 2012-2013 The Boolberry developers
// Copyright (c) 2014-2014 The DarkNetSpace developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "serialization/keyvalue_serialization.h"

enum tor_meek_mode
{
	tor_meek_mode_auto = 0,
	tor_meek_mode_google = 1,
	tor_meek_mode_amazon = 2,
	tor_meek_mode_azure = 3
};


struct gui_config
{
  std::string wallets_last_used_dir;
  bool is_auto_load_default_wallet;
  std::string m_str_default_wallets_path_name;

  std::vector<std::string> m_vect_all_used_wallets;

  bool is_save_default_wallets_password;
  std::string m_str_default_wallets_password;
  bool is_proxy_enabled;
  std::string m_str_proxy_ip;
  uint32_t m_n_proxy_port;
  bool is_proxy_need_auth;
  std::string m_str_proxy_user;
  std::string m_str_proxy_pass;
  bool is_tor_enabled;
  uint32_t m_n_tor_mode;//0-3,0-auto,1-google meek, 2-azure meek, 3-amazon meek
  std::string m_str_language_id;
  std::string m_str_default_exchange;

  gui_config()
  {
	  is_auto_load_default_wallet = is_save_default_wallets_password = is_proxy_enabled = 0;
	  m_n_proxy_port = is_proxy_need_auth = is_tor_enabled = m_n_tor_mode = 0;
  }

  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(wallets_last_used_dir)
    KV_SERIALIZE(is_auto_load_default_wallet)
    KV_SERIALIZE(m_str_default_wallets_path_name)
    KV_SERIALIZE(is_save_default_wallets_password)
    KV_SERIALIZE(m_str_default_wallets_password)
    KV_SERIALIZE(is_proxy_enabled)
    KV_SERIALIZE(m_str_proxy_ip)
    KV_SERIALIZE(m_n_proxy_port)
    KV_SERIALIZE(is_proxy_need_auth)
    KV_SERIALIZE(m_str_proxy_user)
    KV_SERIALIZE(m_str_proxy_pass)
    KV_SERIALIZE(is_tor_enabled)
    KV_SERIALIZE(m_n_tor_mode)
    KV_SERIALIZE(m_str_language_id)
    KV_SERIALIZE(m_str_default_exchange)
	KV_SERIALIZE(m_vect_all_used_wallets)
  END_KV_SERIALIZE_MAP()
};