// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#define CURRENCY_MAX_BLOCK_NUMBER                     500000000
#define CURRENCY_MAX_BLOCK_SIZE                       500000000  // block header blob limit, never used!
#define CURRENCY_MAX_TX_SIZE                          1000000000
#define CURRENCY_PUBLIC_ADDRESS_TEXTBLOB_VER          0
#define CURRENCY_PUBLIC_ADDRESS_BASE58_PREFIX         'H' // addresses start with "D"
#define CURRENCY_MINED_MONEY_UNLOCK_WINDOW            60
#define CURRENT_TRANSACTION_VERSION                   1
#define CURRENT_BLOCK_MAJOR_VERSION                   1
#define CURRENT_BLOCK_MINOR_VERSION                   0
#define CURRENCY_BLOCK_FUTURE_TIME_LIMIT              60*60*2

#define BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW             60

// TOTAL_MONEY_SUPPLY - total number coins to be generated
#define TOTAL_MONEY_SUPPLY                            ((uint64_t)(-1))
#define DONATIONS_SUPPLY                              (0/100) 
#define EMISSION_SUPPLY                               (TOTAL_MONEY_SUPPLY - DONATIONS_SUPPLY) 

#define EMISSION_CURVE_CHARACTER                      17


#define CURRENCY_TO_KEY_OUT_RELAXED                   0
#define CURRENCY_TO_KEY_OUT_FORCED_NO_MIX             1

#define CURRENCY_REWARD_BLOCKS_WINDOW                 400
#define CURRENCY_BLOCK_GRANTED_FULL_REWARD_ZONE       30000 //size of block (bytes) after which reward for block calculated using block size
#define CURRENCY_BLOCK_GRANTED_FULL_REWARD_ZONE_ENLARGE  1000000 //size of block (bytes) after which reward for block calculated using block size
#define CURRENCY_COINBASE_BLOB_RESERVED_SIZE          600
#define CURRENCY_MAX_TRANSACTION_BLOB_SIZE            (CURRENCY_BLOCK_GRANTED_FULL_REWARD_ZONE - CURRENCY_COINBASE_BLOB_RESERVED_SIZE*2) 
#define CURRENCY_MAX_TRANSACTION_BLOB_SIZE_ENLARGE    (CURRENCY_BLOCK_GRANTED_FULL_REWARD_ZONE_ENLARGE - CURRENCY_COINBASE_BLOB_RESERVED_SIZE*2) 


#ifndef TESTNET
#define CURRENCY_BLOCK_GRANTED_FULL_REWARD_ZONE_ENLARGE_STARTING_BLOCK     147000
#else 
#define CURRENCY_BLOCK_GRANTED_FULL_REWARD_ZONE_ENLARGE_STARTING_BLOCK     100
#endif

#define CURRENCY_DISPLAY_DECIMAL_POINT                10

// COIN - number of smallest units in one coin
#define COIN                                            ((uint64_t)10000000000) // pow(10, 10)
#define DEFAULT_DUST_THRESHOLD                          ((uint64_t)1000000) // pow(10, 6)

#define DEFAULT_FEE                                     ((uint64_t)1000000000) // pow(10, 9)
#define TX_POOL_MINIMUM_FEE                             ((uint64_t)10000000) // pow(10, 7)

#define MAKE_ALIAS_MINIMUM_FEE                          ((uint64_t)100 * COIN) //100DNC


#define ORPHANED_BLOCKS_MAX_COUNT                       100


#define DIFFICULTY_TARGET                               60 // seconds
#define DIFFICULTY_WINDOW                               720 // blocks
#define DIFFICULTY_LAG                                  15  // !!!
#define DIFFICULTY_CUT                                  60  // timestamps to cut after sorting
#define DIFFICULTY_BLOCKS_COUNT                         (DIFFICULTY_WINDOW + DIFFICULTY_LAG)

#define CURRENCY_BLOCK_PER_DAY                          ((60*60*24)/(DIFFICULTY_TARGET))

#define CURRENCY_LOCKED_TX_ALLOWED_DELTA_SECONDS        (DIFFICULTY_TARGET * CURRENCY_LOCKED_TX_ALLOWED_DELTA_BLOCKS)
#define CURRENCY_LOCKED_TX_ALLOWED_DELTA_BLOCKS         1

#define DIFFICULTY_BLOCKS_ESTIMATE_TIMESPAN             DIFFICULTY_TARGET //just alias


#define BLOCKS_IDS_SYNCHRONIZING_DEFAULT_COUNT          10000  //by default, blocks ids count in synchronizing
#define BLOCKS_SYNCHRONIZING_DEFAULT_COUNT              200    //by default, blocks count in blocks downloading
#define CURRENCY_PROTOCOL_HOP_RELAX_COUNT               3      //value of hop, after which we use only announce of new block


#define CURRENCY_ALT_BLOCK_LIVETIME_COUNT               (720*7)//one week
#define CURRENCY_MEMPOOL_TX_LIVETIME                    86400 //seconds, one day
#define CURRENCY_MEMPOOL_TX_FROM_ALT_BLOCK_LIVETIME     (CURRENCY_ALT_BLOCK_LIVETIME_COUNT*DIFFICULTY_TARGET) //seconds, one week


#ifndef TESTNET
#define P2P_DEFAULT_PORT                                37708
#define RPC_DEFAULT_PORT                                37709
#else 
#define P2P_DEFAULT_PORT                                57708
#define RPC_DEFAULT_PORT                                57709
#endif

#define COMMAND_RPC_GET_BLOCKS_FAST_MAX_COUNT           1000
#define COMMAND_RPC_GET_BLOCKS_BY_HEIGHTS_MAX_COUNT     100

#define P2P_LOCAL_WHITE_PEERLIST_LIMIT                  1000
#define P2P_LOCAL_GRAY_PEERLIST_LIMIT                   5000

#define P2P_DEFAULT_CONNECTIONS_COUNT                   12
#define P2P_DEFAULT_HANDSHAKE_INTERVAL                  60           //secondes
#define P2P_DEFAULT_PACKET_MAX_SIZE                     50000000     //50000000 bytes maximum packet size
#define P2P_DEFAULT_PEERS_IN_HANDSHAKE                  250
#define P2P_DEFAULT_CONNECTION_TIMEOUT                  5000       //5 seconds
#define P2P_DEFAULT_PING_CONNECTION_TIMEOUT             2000       //2 seconds
#define P2P_DEFAULT_INVOKE_TIMEOUT                      60*2*1000  //2 minutes
#define P2P_DEFAULT_HANDSHAKE_INVOKE_TIMEOUT            5000       //5 seconds
#define P2P_MAINTAINERS_PUB_KEY                         "44165823f0e1257378eb045f9ba9e0beab580cfd3c35bc2b1dd1b5054eca46d4"
#define P2P_DEFAULT_WHITELIST_CONNECTIONS_PERCENT       70
#define P2P_FAILED_ADDR_FORGET_SECONDS                  (60*60)     //1 hour

#define P2P_IP_BLOCKTIME                                (60*60*24)  //24 hour
#define P2P_IP_FAILS_BEFOR_BLOCK                        10
#define P2P_IDLE_CONNECTION_KILL_INTERVAL               (5*60) //5 minutes


/* This money will go to growth of the project */
  #define CURRENCY_DONATIONS_ADDRESS                     "D655ofATSSQLKE8J7Bvv9uYtjQfJucaWo8DHTxmySAV3cbG4AkuyijGX1HRFbFJK5BgektZiU4kQ9Hsri5J4pycuCZA67XN"
  #define CURRENCY_DONATIONS_ADDRESS_TRACKING_KEY        "2b4cb9bf3582becf6c896a83e5c01f2c97ff781fbfcd5e47b3e9f1b0bcb71609"
/* This money will go to the founder of CryptoNote technology, 10% of donations */
  #define CURRENCY_ROYALTY_ADDRESS                       "DBrjxZoEjssR35Y6EQRpaVRjLJSDMuB4bdWZq1gDNPrrV5Jmab6joB1irNvZ2NkirW4AX9AMRLqB66erVDPRTmXr7p4zSbx"
  #define CURRENCY_ROYALTY_ADDRESS_TRACKING_KEY          "d95d2da6eddd558c5538eeefdbdbe48e968a08b4a720ca2eec5369aebba52904"


#ifdef TESTNET
  #define CURRENCY_DONATIONS_INTERVAL                     10
  #define CURRENCY_DONATIONS_START_BLOCKNO                50
#else
  #define CURRENCY_DONATIONS_INTERVAL                     1440
  #define CURRENCY_DONATIONS_START_BLOCKNO                4550
#endif


#define ALLOW_DEBUG_COMMANDS

#define CURRENCY_NAME_BASE                              "darknetspace"
#define CURRENCY_NAME_SHORT_BASE                        "dnc"
#ifndef TESTNET
#define CURRENCY_NAME                                   CURRENCY_NAME_BASE
#define CURRENCY_NAME_SHORT                             CURRENCY_NAME_SHORT_BASE
#else
#define CURRENCY_NAME                                   CURRENCY_NAME_BASE"_testnet"
#define CURRENCY_NAME_SHORT                             CURRENCY_NAME_SHORT_BASE"_testnet"
#endif

#define CURRENCY_POOLDATA_FILENAME                      "poolstate.bin"
#define CURRENCY_BLOCKCHAINDATA_FILENAME                "blockchain.bin"
#define CURRENCY_BLOCKCHAINDATA_TEMP_FILENAME           "blockchain.bin.tmp"
#define P2P_NET_DATA_FILENAME                           "p2pstate.bin"
#define MINER_CONFIG_FILENAME                           "miner_conf.json"
#define GUI_CONFIG_FILENAME                             "gui_conf.json"
