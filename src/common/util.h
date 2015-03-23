// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once 

#include <mutex>
#include <system_error>
#include <boost/filesystem.hpp>
#include <stdarg.h>
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "misc_language.h"
#include "p2p/p2p_protocol_defs.h"

namespace tools
{
  std::string get_default_data_dir();
  std::string get_default_user_dir();
  std::string get_current_username();
  std::string get_os_version_string();
  void get_clear_json(std::string &str);
  std::string format(const char *fmt, ...);
  int string_replace(std::string &strBase, std::string strSrc, std::string strDes);

  bool create_directories_if_necessary(const std::string& path);
  std::error_code replace_file(const std::string& replacement_name, const std::string& replaced_name);

  inline crypto::hash get_proof_of_trust_hash(const nodetool::proof_of_trust& pot)
  {
    std::string s;
    s.append(reinterpret_cast<const char*>(&pot.peer_id), sizeof(pot.peer_id));
    s.append(reinterpret_cast<const char*>(&pot.time), sizeof(pot.time));
    return crypto::cn_fast_hash(s.data(), s.size());
  }

  inline 
  crypto::public_key get_public_key_from_string(const std::string& str_key)
  {
    crypto::public_key k = AUTO_VAL_INIT(k);
    epee::string_tools::hex_to_pod(str_key, k);
    return k;
  }


  class signal_handler
  {
  public:
    template<typename T>
    static bool install(T t)
    {
#if defined(WIN32)
      bool r = TRUE == ::SetConsoleCtrlHandler((PHANDLER_ROUTINE)&win_handler, TRUE);
      if (r)
      {
        m_handler = t;
      }
      return r;
#else
      signal(SIGINT, posix_handler);
      signal(SIGTERM, posix_handler);
      m_handler = t;
      return true;
#endif
    }

  private:
#if defined(WIN32)
    static BOOL win_handler(DWORD type)
    {
      if (CTRL_C_EVENT == type || CTRL_BREAK_EVENT == type)
      {
        handle_signal();
        return TRUE;
      }
      else
      {
        LOG_PRINT_RED_L0("Got control signal " << type << ". Exiting without saving...");
        return FALSE;
      }
      return TRUE;
    }
#else
    static void posix_handler(int /*type*/)
    {
      handle_signal();
    }
#endif

    static void handle_signal()
    {
      static std::mutex m_mutex;
      std::unique_lock<std::mutex> lock(m_mutex);
      m_handler();
    }

  private:
    static std::function<void(void)> m_handler;
  };
}
