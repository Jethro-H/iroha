/**
 * Copyright Soramitsu Co., Ltd. 2017 All Rights Reserved.
 * http://soramitsu.co.jp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/combine.hpp>

#include "ametsuchi/impl/postgres_block_query.hpp"
#include "ametsuchi/impl/postgres_ordering_service_persistent_state.hpp"
#include "ametsuchi/impl/postgres_wsv_query.hpp"
#include "ametsuchi/impl/storage_impl.hpp"
#include "ametsuchi/mutable_storage.hpp"
#include "common/byteutils.hpp"
#include "framework/test_subscriber.hpp"
#include "interfaces/iroha_internal/block.hpp"
#include "model/account.hpp"
#include "model/account_asset.hpp"
#include "model/asset.hpp"
#include "model/commands/all.hpp"
#include "model/converters/pb_block_factory.hpp"
#include "model/domain.hpp"
#include "model/peer.hpp"
#include "model/permissions.hpp"
#include "model/sha3_hash.hpp"
#include "module/irohad/ametsuchi/ametsuchi_fixture.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"

// TODO: 14-02-2018 Alexey Chernyshov remove this after relocation to
// shared_model https://soramitsu.atlassian.net/browse/IR-880
#include "backend/protobuf/from_old_model.hpp"

using namespace iroha::ametsuchi;
using namespace iroha::model;
using namespace framework::test_subscriber;

/**
 * Shortcut to create CallExact observable wrapper, subscribe with given lambda,
 * and validate the number of calls with optional custom output
 * @tparam O observable type
 * @tparam F on_next function type
 * @param o observable object
 * @param f function object
 * @param call_count number of expected calls
 * @param msg custom validation failure message
 */
template <typename O, typename F>
void validateCalls(O &&o,
                   F &&f,
                   uint64_t call_count,
                   const std::string &msg = {}) {
  auto wrap = make_test_subscriber<CallExact>(std::forward<O>(o), call_count);
  wrap.subscribe(std::forward<F>(f));
  ASSERT_TRUE(wrap.validate()) << "Expected " << call_count << " calls" << msg;
}

/**
 * Validate getAccountTransaction with given parameters
 * @tparam B block query type
 * @param blocks block query object
 * @param account id to query
 * @param call_count number of observable calls
 * @param command_count number of commands in transaction
 */
template <typename B>
void validateAccountTransactions(B &&blocks,
                                 const std::string &account,
                                 int call_count,
                                 int command_count) {
  validateCalls(
      blocks->getAccountTransactions(account),
      [&](const auto &tx) { EXPECT_EQ(tx.commands.size(), command_count); },
      call_count,
      " for " + account);
}

/**
 * Validate getAccountAssetTransactions with given parameters
 * @tparam B block query type
 * @param blocks block query object
 * @param account id to query
 * @param asset id to query
 * @param call_count number of observable calls
 * @param command_count number of commands in transaction
 */
template <typename B>
void validateAccountAssetTransactions(B &&blocks,
                                      const std::string &account,
                                      const std::string &asset,
                                      int call_count,
                                      int command_count) {
  validateCalls(
      blocks->getAccountAssetTransactions(account, asset),
      [&](const auto &tx) { EXPECT_EQ(tx.commands.size(), command_count); },
      call_count,
      " for " + account + " " + asset);
}

/**
 * Validate getAccountAsset with given parameters
 * @tparam W WSV query type
 * @param wsv WSV query object
 * @param account id to query
 * @param asset id to query
 * @param amount to validate
 */
template <typename W>
void validateAccountAsset(W &&wsv,
                          const std::string &account,
                          const std::string &asset,
                          const iroha::Amount &amount) {
  auto account_asset = wsv->getAccountAsset(account, asset);
  ASSERT_TRUE(account_asset);
  ASSERT_EQ(account_asset->account_id, account);
  ASSERT_EQ(account_asset->asset_id, asset);
  ASSERT_EQ(account_asset->balance, amount);
}

/**
 * Validate getAccount with given parameters
 * @tparam W WSV query type
 * @param wsv WSV query object
 * @param id account to query
 * @param domain id to validate
 */
template <typename W>
void validateAccount(W &&wsv,
                     const std::string &id,
                     const std::string &domain) {
  auto account = wsv->getAccount(id);
  ASSERT_TRUE(account);
  ASSERT_EQ(account->account_id, id);
  ASSERT_EQ(account->domain_id, domain);
}

/**
 * Apply block to given storage
 * @tparam S storage type
 * @param storage storage object
 * @param block to apply
 */
template <typename S>
void apply(S &&storage, const shared_model::interface::Block &block) {
  std::unique_ptr<MutableStorage> ms;
  auto storageResult = storage->createMutableStorage();
  storageResult.match(
      [&](iroha::expected::Value<std::unique_ptr<MutableStorage>> &_storage) {
        ms = std::move(_storage.value);
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "MutableStorage: " << error.error;
      });
  ms->apply(block, [](const auto &, auto &, const auto &) { return true; });
  storage->commit(std::move(ms));
}

TEST_F(AmetsuchiTest, GetBlocksCompletedWhenCalled) {
  // Commit block => get block => observable completed
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto blocks = storage->getBlockQuery();

  auto block = TestBlockBuilder().height(1).build();
  apply(storage, block);

  auto completed_wrapper =
      make_test_subscriber<IsCompleted>(blocks->getBlocks(1, 1));
  completed_wrapper.subscribe();
  ASSERT_TRUE(completed_wrapper.validate());
}

TEST_F(AmetsuchiTest, SampleTest) {
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto wsv = storage->getWsvQuery();
  auto blocks = storage->getBlockQuery();

  const auto domain = "ru", user1name = "user1", user2name = "user2",
             user1id = "user1@ru", user2id = "user2@ru", assetname = "RUB",
             assetid = "RUB#ru";

  std::string account, src_account, dest_account, asset;
  iroha::Amount amount;

  // Tx 1
  Transaction txn;
  txn.creator_account_id = "admin1";

  // Create domain ru
  txn.commands.push_back(std::make_shared<CreateRole>(
      "user",
      std::set<std::string>{
          can_add_peer, can_create_asset, can_get_my_account}));
  txn.commands.push_back(std::make_shared<CreateDomain>(domain, "user"));

  // Create account user1
  txn.commands.push_back(cmd_gen.generateCreateAccount(user1name, domain, {}));

  // Compose block
  Block old_block;
  old_block.transactions.push_back(txn);
  old_block.height = 1;
  old_block.prev_hash.fill(0);
  auto block1hash = iroha::hash(old_block);
  old_block.hash = block1hash;
  old_block.txs_number = old_block.transactions.size();

  auto block = shared_model::proto::from_old(old_block);
  apply(storage, block);

  validateAccount(wsv, user1id, domain);

  // Tx 2
  txn = Transaction();
  txn.creator_account_id = "admin2";

  // Create account user2
  txn.commands.push_back(cmd_gen.generateCreateAccount(user2name, domain, {}));

  // Create asset RUB#ru
  txn.commands.push_back(cmd_gen.generateCreateAsset(assetname, domain, 2));

  // Add RUB#ru to user1
  txn.commands.push_back(cmd_gen.generateAddAssetQuantity(
      user1id, assetid, iroha::Amount(150, 2)));

  // Transfer asset from user 1
  txn.commands.push_back(cmd_gen.generateTransferAsset(
      user1id, user2id, assetid, iroha::Amount(100, 2)));

  // Compose block
  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 2;
  old_block.prev_hash = block1hash;
  auto block2hash = iroha::hash(old_block);
  old_block.hash = block2hash;
  old_block.txs_number = old_block.transactions.size();

  auto block2 = shared_model::proto::from_old(old_block);
  apply(storage, block2);

  validateAccountAsset(wsv, user1id, assetid, iroha::Amount(50, 2));
  validateAccountAsset(wsv, user2id, assetid, iroha::Amount(100, 2));

  // Block store tests
  auto hashes = {block1hash, block2hash};
  validateCalls(blocks->getBlocks(1, 2),
                [ i = 0, &hashes ](auto eachBlock) mutable {
                  // TODO: 14-02-2018 Alexey Chernyshov uncomment this after relocation to
                  // shared_model https://soramitsu.atlassian.net/browse/IR-880
//                EXPECT_EQ(*(hashes.begin() + i), eachBlock.hash);
                  ++i;
                },
                2);

  validateAccountTransactions(blocks, "admin1", 1, 3);
  validateAccountTransactions(blocks, "admin2", 1, 4);
  validateAccountTransactions(blocks, "non_existing_user", 0, 0);

  validateAccountAssetTransactions(blocks, user1id, assetid, 1, 4);
  validateAccountAssetTransactions(blocks, user2id, assetid, 1, 4);
  validateAccountAssetTransactions(
      blocks, "non_existing_user", "non_existing_asset", 0, 0);
}

TEST_F(AmetsuchiTest, PeerTest) {
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto wsv = storage->getWsvQuery();

  Transaction txn;
  AddPeer addPeer;
  addPeer.peer.pubkey.at(0) = 1;
  addPeer.peer.address = "192.168.0.1:50051";
  txn.commands.push_back(std::make_shared<AddPeer>(addPeer));

  Block old_block;
  old_block.transactions.push_back(txn);

  auto block = shared_model::proto::from_old(old_block);
  apply(storage, block);

  auto peers = wsv->getPeers();
  ASSERT_TRUE(peers);
  ASSERT_EQ(peers->size(), 1);
  ASSERT_EQ(peers->at(0), addPeer.peer);
}

TEST_F(AmetsuchiTest, queryGetAccountAssetTransactionsTest) {
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto wsv = storage->getWsvQuery();
  auto blocks = storage->getBlockQuery();

  const auto admin = "admin1", domain = "domain", user1name = "user1",
             user2name = "user2", user3name = "user3", user1id = "user1@domain",
             user2id = "user2@domain", user3id = "user3@domain",
             asset1name = "asset1", asset2name = "asset2",
             asset1id = "asset1#domain", asset2id = "asset2#domain";

  std::string account, src_account, dest_account, asset;
  iroha::Amount amount;

  // 1st tx
  Transaction txn;
  txn.creator_account_id = admin;

  // Create domain
  txn.commands.push_back(std::make_shared<CreateRole>(
      "user",
      std::set<std::string>{
          can_add_peer, can_create_asset, can_get_my_account}));
  txn.commands.push_back(cmd_gen.generateCreateDomain(domain, "user"));

  // Create accounts
  for (const auto &name : {user1name, user2name, user3name}) {
    txn.commands.push_back(cmd_gen.generateCreateAccount(name, domain, {}));
  }

  // Create assets
  for (const auto &name : {asset1name, asset2name}) {
    txn.commands.push_back(cmd_gen.generateCreateAsset(name, domain, 2));
  }

  // Add amounts to users
  txn.commands.push_back(cmd_gen.generateAddAssetQuantity(
      user1id, asset1id, iroha::Amount(300, 2)));
  txn.commands.push_back(cmd_gen.generateAddAssetQuantity(
      user2id, asset2id, iroha::Amount(250, 2)));

  Block old_block;
  old_block.transactions.push_back(txn);
  old_block.height = 1;
  old_block.prev_hash.fill(0);
  auto block1hash = iroha::hash(old_block);
  old_block.hash = block1hash;
  old_block.txs_number = static_cast<uint16_t>(old_block.transactions.size());

  auto block = shared_model::proto::from_old(old_block);
  apply(storage, block);

  // Check querying accounts
  for (const auto &id : {user1id, user2id, user3id}) {
    validateAccount(wsv, id, domain);
  }

  // Check querying assets for users
  validateAccountAsset(wsv, user1id, asset1id, iroha::Amount(300, 2));
  validateAccountAsset(wsv, user2id, asset2id, iroha::Amount(250, 2));

  // 2th tx (user1 -> user2 # asset1)
  txn = Transaction();
  txn.creator_account_id = user1id;

  // Create transfer asset from user 1 to user 2
  txn.commands.push_back(cmd_gen.generateTransferAsset(
      user1id, user2id, asset1id, iroha::Amount(120, 2)));

  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 2;
  old_block.prev_hash = block1hash;
  auto block2hash = iroha::hash(old_block);
  old_block.hash = block2hash;
  old_block.txs_number = static_cast<uint16_t>(old_block.transactions.size());

  auto block2 = shared_model::proto::from_old(old_block);
  apply(storage, block2);

  // Check account asset after transfer assets
  validateAccountAsset(wsv, user1id, asset1id, iroha::Amount(180, 2));
  validateAccountAsset(wsv, user2id, asset1id, iroha::Amount(120, 2));

  // 3rd tx
  //   (user2 -> user3 # asset2)
  //   (user2 -> user1 # asset2)
  txn = Transaction();
  txn.creator_account_id = user2id;

  txn.commands.push_back(cmd_gen.generateTransferAsset(
      user2id, user3id, asset2id, iroha::Amount(150, 2)));
  txn.commands.push_back(cmd_gen.generateTransferAsset(
      user2id, user1id, asset2id, iroha::Amount(10, 2)));

  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 3;
  old_block.prev_hash = block2hash;
  auto block3hash = iroha::hash(old_block);
  old_block.hash = block3hash;
  old_block.txs_number = static_cast<uint16_t>(old_block.transactions.size());

  auto block3 = shared_model::proto::from_old(old_block);
  apply(storage, block3);

  validateAccountAsset(wsv, user2id, asset2id, iroha::Amount(90, 2));
  validateAccountAsset(wsv, user3id, asset2id, iroha::Amount(150, 2));
  validateAccountAsset(wsv, user1id, asset2id, iroha::Amount(10, 2));

  // Block store test
  auto hashes = {block1hash, block2hash, block3hash};
  validateCalls(blocks->getBlocks(1, 3),
                [ i = 0, &hashes ](auto eachBlock) mutable {
                  // TODO: 14-02-2018 Alexey Chernyshov uncomment this after relocation to
                  // shared_model https://soramitsu.atlassian.net/browse/IR-880
//                  EXPECT_EQ(*(hashes.begin() + i), eachBlock.hash);
                  ++i;
                },
                3);

  validateAccountTransactions(blocks, admin, 1, 9);
  validateAccountTransactions(blocks, user1id, 1, 1);
  validateAccountTransactions(blocks, user2id, 1, 2);
  validateAccountTransactions(blocks, user3id, 0, 0);

  // (user1 -> user2 # asset1)
  // (user2 -> user3 # asset2)
  // (user2 -> user1 # asset2)
  validateAccountAssetTransactions(blocks, user1id, asset1id, 1, 1);
  validateAccountAssetTransactions(blocks, user2id, asset1id, 1, 1);
  validateAccountAssetTransactions(blocks, user3id, asset1id, 0, 0);
  validateAccountAssetTransactions(blocks, user1id, asset2id, 1, 2);
  validateAccountAssetTransactions(blocks, user2id, asset2id, 1, 2);
  validateAccountAssetTransactions(blocks, user3id, asset2id, 1, 2);
}

TEST_F(AmetsuchiTest, AddSignatoryTest) {
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto wsv = storage->getWsvQuery();

  iroha::pubkey_t pubkey1, pubkey2;
  pubkey1.at(0) = 1;
  pubkey2.at(0) = 2;

  auto user1id = "user1@domain";
  auto user2id = "user2@domain";

  // 1st tx (create user1 with pubkey1)
  CreateRole createRole;
  createRole.role_name = "user";
  createRole.permissions = {can_add_peer, can_create_asset, can_get_my_account};

  Transaction txn;
  txn.creator_account_id = "admin1";

  txn.commands.push_back(std::make_shared<CreateRole>(createRole));
  CreateDomain createDomain;
  createDomain.domain_id = "domain";
  createDomain.user_default_role = "user";
  txn.commands.push_back(std::make_shared<CreateDomain>(createDomain));

  CreateAccount createAccount;
  createAccount.account_name = "user1";
  createAccount.domain_id = "domain";
  createAccount.pubkey = pubkey1;
  txn.commands.push_back(std::make_shared<CreateAccount>(createAccount));

  Block old_block;
  old_block.transactions.push_back(txn);
  old_block.height = 1;
  old_block.prev_hash.fill(0);
  auto block1hash = iroha::hash(old_block);
  old_block.hash = block1hash;
  old_block.txs_number = old_block.transactions.size();

  auto block = shared_model::proto::from_old(old_block);
  apply(storage, block);

  {
    auto account = wsv->getAccount(user1id);
    ASSERT_TRUE(account);
    ASSERT_EQ(account->account_id, user1id);
    ASSERT_EQ(account->domain_id, createAccount.domain_id);

    auto signatories = wsv->getSignatories(user1id);
    ASSERT_TRUE(signatories);
    ASSERT_EQ(signatories->size(), 1);
    ASSERT_EQ(signatories->at(0), pubkey1);
  }

  // 2nd tx (add sig2 to user1)
  txn = Transaction();
  txn.creator_account_id = user1id;

  auto addSignatory = AddSignatory();
  addSignatory.account_id = user1id;
  addSignatory.pubkey = pubkey2;
  txn.commands.push_back(std::make_shared<AddSignatory>(addSignatory));

  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 2;
  old_block.prev_hash = block1hash;
  auto block2hash = iroha::hash(old_block);
  old_block.hash = block2hash;
  old_block.txs_number = old_block.transactions.size();

  auto block2 = shared_model::proto::from_old(old_block);
  apply(storage, block2);
  {
    auto account = wsv->getAccount(user1id);
    ASSERT_TRUE(account);

    auto signatories = wsv->getSignatories(user1id);
    ASSERT_TRUE(signatories);
    ASSERT_EQ(signatories->size(), 2);
    ASSERT_EQ(signatories->at(0), pubkey1);
    ASSERT_EQ(signatories->at(1), pubkey2);
  }

  // 3rd tx (create user2 with pubkey1 that is same as user1's key)
  txn = Transaction();
  txn.creator_account_id = "admin2";

  createAccount = CreateAccount();
  createAccount.account_name = "user2";
  createAccount.domain_id = "domain";
  createAccount.pubkey = pubkey1;  // same as user1's pubkey1
  txn.commands.push_back(std::make_shared<CreateAccount>(createAccount));

  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 3;
  old_block.prev_hash = block2hash;
  auto block3hash = iroha::hash(old_block);
  old_block.hash = block3hash;
  old_block.txs_number = old_block.transactions.size();

  auto block3 = shared_model::proto::from_old(old_block);
  apply(storage, block3);

  {
    auto account1 = wsv->getAccount(user1id);
    ASSERT_TRUE(account1);

    auto account2 = wsv->getAccount(user2id);
    ASSERT_TRUE(account2);

    auto signatories1 = wsv->getSignatories(user1id);
    ASSERT_TRUE(signatories1);
    ASSERT_EQ(signatories1->size(), 2);
    ASSERT_EQ(signatories1->at(0), pubkey1);
    ASSERT_EQ(signatories1->at(1), pubkey2);

    auto signatories2 = wsv->getSignatories(user2id);
    ASSERT_TRUE(signatories2);
    ASSERT_EQ(signatories2->size(), 1);
    ASSERT_EQ(signatories2->at(0), pubkey1);
  }

  // 4th tx (remove pubkey1 from user1)
  txn = Transaction();
  txn.creator_account_id = user1id;

  auto removeSignatory = RemoveSignatory();
  removeSignatory.account_id = user1id;
  removeSignatory.pubkey = pubkey1;
  txn.commands.push_back(std::make_shared<RemoveSignatory>(removeSignatory));

  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 4;
  old_block.prev_hash = block3hash;
  auto block4hash = iroha::hash(old_block);
  old_block.hash = block4hash;
  old_block.txs_number = old_block.transactions.size();

  auto block4 = shared_model::proto::from_old(old_block);
  apply(storage, block4);

  {
    auto account = wsv->getAccount(user1id);
    ASSERT_TRUE(account);

    // user1 has only pubkey2.
    auto signatories1 = wsv->getSignatories(user1id);
    ASSERT_TRUE(signatories1);
    ASSERT_EQ(signatories1->size(), 1);
    ASSERT_EQ(signatories1->at(0), pubkey2);

    // user2 still has pubkey1.
    auto signatories2 = wsv->getSignatories(user2id);
    ASSERT_TRUE(signatories2);
    ASSERT_EQ(signatories2->size(), 1);
    ASSERT_EQ(signatories2->at(0), pubkey1);
  }

  // 5th tx (add sig2 to user2 and set quorum = 1)
  txn = Transaction();
  txn.creator_account_id = user2id;

  addSignatory = AddSignatory();
  addSignatory.account_id = user2id;
  addSignatory.pubkey = pubkey2;
  txn.commands.push_back(std::make_shared<AddSignatory>(addSignatory));

  auto seqQuorum = SetQuorum();
  seqQuorum.account_id = user2id;
  seqQuorum.new_quorum = 2;
  txn.commands.push_back(std::make_shared<SetQuorum>(seqQuorum));

  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 5;
  old_block.prev_hash = block4hash;
  auto block5hash = iroha::hash(old_block);
  old_block.hash = block5hash;
  old_block.txs_number = old_block.transactions.size();

  auto block5 = shared_model::proto::from_old(old_block);
  apply(storage, block5);

  {
    auto account = wsv->getAccount(user2id);
    ASSERT_TRUE(account);
    ASSERT_EQ(account->quorum, 2);

    // user2 has pubkey1 and pubkey2.
    auto signatories = wsv->getSignatories(user2id);
    ASSERT_TRUE(signatories);
    ASSERT_EQ(signatories->size(), 2);
    ASSERT_EQ(signatories->at(0), pubkey1);
    ASSERT_EQ(signatories->at(1), pubkey2);
  }

  // 6th tx (remove sig2 fro user2: This must success)
  txn = Transaction();
  txn.creator_account_id = user2id;

  removeSignatory = RemoveSignatory();
  removeSignatory.account_id = user2id;
  removeSignatory.pubkey = pubkey2;
  txn.commands.push_back(std::make_shared<RemoveSignatory>(removeSignatory));

  old_block = Block();
  old_block.transactions.push_back(txn);
  old_block.height = 6;
  old_block.prev_hash = block5hash;
  auto block6hash = iroha::hash(old_block);
  old_block.hash = block6hash;
  old_block.txs_number = old_block.transactions.size();

  auto block6 = shared_model::proto::from_old(old_block);
  apply(storage, block6);

  {
    // user2 only has pubkey1.
    auto signatories = wsv->getSignatories(user2id);
    ASSERT_TRUE(signatories);
    ASSERT_EQ(signatories->size(), 1);
    ASSERT_EQ(signatories->at(0), pubkey1);
  }
}

Block getBlock() {
  Transaction txn;
  txn.creator_account_id = "admin1";
  AddPeer add_peer;
  add_peer.peer.address = "192.168.0.0";
  txn.commands.push_back(std::make_shared<AddPeer>(add_peer));
  Block block;
  block.transactions.push_back(txn);
  block.height = 1;
  block.prev_hash.fill(0);
  auto block1hash = iroha::hash(block);
  block.txs_number = block.transactions.size();
  block.hash = block1hash;
  return block;
}

TEST_F(AmetsuchiTest, TestingStorageWhenInsertBlock) {
  auto log = logger::testLog("TestStorage");
  log->info(
      "Test case: create storage "
      "=> insert block "
      "=> assert that inserted");
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto wsv = storage->getWsvQuery();
  ASSERT_EQ(0, wsv->getPeers().value().size());

  log->info("Try insert block");

  auto inserted = storage->insertBlock(getBlock());
  ASSERT_TRUE(inserted);

  log->info("Request ledger information");

  ASSERT_NE(0, wsv->getPeers().value().size());

  log->info("Drop ledger");

  storage->dropStorage();
}

TEST_F(AmetsuchiTest, TestingStorageWhenDropAll) {
  auto logger = logger::testLog("TestStorage");
  logger->info(
      "Test case: create storage "
      "=> insert block "
      "=> assert that written"
      " => drop all "
      "=> assert that all deleted ");

  auto log = logger::testLog("TestStorage");
  log->info(
      "Test case: create storage "
      "=> insert block "
      "=> assert that inserted");
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto wsv = storage->getWsvQuery();
  ASSERT_EQ(0, wsv->getPeers().value().size());

  log->info("Try insert block");

  auto inserted = storage->insertBlock(getBlock());
  ASSERT_TRUE(inserted);

  log->info("Request ledger information");

  ASSERT_NE(0, wsv->getPeers().value().size());

  log->info("Drop ledger");

  storage->dropStorage();

  ASSERT_EQ(0, wsv->getPeers().value().size());
  std::shared_ptr<StorageImpl> new_storage;
  auto new_storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        new_storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_EQ(0, wsv->getPeers().value().size());
  new_storage->dropStorage();
}

/**
 * @given initialized storage
 * @when insert block with 2 transactions in
 * @then both of them are found with getTxByHashSync call by hash. Transaction
 * with some other hash is not found.
 */
TEST_F(AmetsuchiTest, FindTxByHashTest) {
  std::shared_ptr<StorageImpl> storage;
  auto storageResult = StorageImpl::create(block_store_path, pgopt_);
  storageResult.match(
      [&](iroha::expected::Value<std::shared_ptr<StorageImpl>> &_storage) {
        storage = _storage.value;
      },
      [](iroha::expected::Error<std::string> &error) {
        FAIL() << "StorageImpl: " << error.error;
      });
  ASSERT_TRUE(storage);
  auto blocks = storage->getBlockQuery();

  iroha::pubkey_t pubkey1, pubkey2;
  pubkey1.at(0) = 1;
  pubkey2.at(0) = 2;

  CreateRole createRole;
  createRole.role_name = "user";
  createRole.permissions = {can_add_peer, can_create_asset, can_get_my_account};

  Transaction tx1;
  tx1.creator_account_id = "admin1";

  tx1.commands.push_back(std::make_shared<CreateRole>(createRole));
  CreateDomain createDomain;
  createDomain.domain_id = "domain";
  createDomain.user_default_role = "user";
  tx1.commands.push_back(std::make_shared<CreateDomain>(createDomain));

  CreateAccount createAccount;
  createAccount.account_name = "user1";
  createAccount.domain_id = "domain";
  createAccount.pubkey = pubkey1;
  tx1.commands.push_back(std::make_shared<CreateAccount>(createAccount));

  CreateRole createRole2;
  createRole2.role_name = "user2";
  createRole2.permissions = {
      can_add_peer, can_create_asset, can_get_my_account};

  Transaction tx2;
  tx2.commands.push_back(std::make_shared<CreateRole>(createRole2));
  CreateDomain createDomain2;
  createDomain2.domain_id = "domain2";
  createDomain2.user_default_role = "user";
  tx2.commands.push_back(std::make_shared<CreateDomain>(createDomain2));

  Block old_block;
  old_block.transactions.push_back(tx1);
  old_block.transactions.push_back(tx2);
  old_block.height = 1;
  old_block.prev_hash.fill(0);
  old_block.txs_number = old_block.transactions.size();
  old_block.hash = iroha::hash(old_block);

  auto block = shared_model::proto::from_old(old_block);
  apply(storage, block);

  // TODO: 31.10.2017 luckychess move tx3hash case into a separate test after
  // ametsuchi_test redesign
  auto tx1hash = iroha::hash(tx1).to_string();
  auto tx2hash = iroha::hash(tx2).to_string();
  auto tx3hash = "some garbage";

  auto tx1check = *blocks->getTxByHashSync(tx1hash);

  ASSERT_EQ(*blocks->getTxByHashSync(tx1hash), tx1);
  ASSERT_EQ(*blocks->getTxByHashSync(tx2hash), tx2);
  ASSERT_EQ(blocks->getTxByHashSync(tx3hash), boost::none);
}

/**
 * @given initialized storage for ordering service
 * @when save proposal height
 * @then load proposal height and ensure it is correct
 */
TEST_F(AmetsuchiTest, OrderingServicePersistentStorageTest) {
  std::shared_ptr<iroha::ametsuchi::PostgresOrderingServicePersistentState>
      ordering_state;
  iroha::ametsuchi::PostgresOrderingServicePersistentState::create(pgopt_)
      .match([&](iroha::expected::Value<std::shared_ptr<
                     iroha::ametsuchi::PostgresOrderingServicePersistentState>>
                     &_storage) { ordering_state = _storage.value; },
             [](iroha::expected::Error<std::string> &error) {
               FAIL() << "PostgresOrderingServicePersistentState: "
                      << error.error;
             });
  ASSERT_TRUE(ordering_state);

  ordering_state->resetState();
  ASSERT_EQ(2, ordering_state->loadProposalHeight().value());
  ASSERT_TRUE(ordering_state->saveProposalHeight(11));
  ASSERT_EQ(11, ordering_state->loadProposalHeight().value());
  ASSERT_TRUE(ordering_state->saveProposalHeight(33));
  ASSERT_EQ(33, ordering_state->loadProposalHeight().value());
  ordering_state->resetState();
  ASSERT_EQ(2, ordering_state->loadProposalHeight().value());
}

/**
 * @given initialized storage for ordering service
 * @when save proposal height
 * @then load proposal height and ensure it is correct
 */
TEST_F(AmetsuchiTest, OrderingServicePersistentStorageRestartTest) {
  std::shared_ptr<iroha::ametsuchi::PostgresOrderingServicePersistentState>
      ordering_state;
  iroha::ametsuchi::PostgresOrderingServicePersistentState::create(pgopt_)
      .match([&](iroha::expected::Value<std::shared_ptr<
                     iroha::ametsuchi::PostgresOrderingServicePersistentState>>
                     &_storage) { ordering_state = _storage.value; },
             [](iroha::expected::Error<std::string> &error) {
               FAIL() << "PostgresOrderingServicePersistentState: "
                      << error.error;
             });
  ASSERT_TRUE(ordering_state);

  ordering_state->resetState();
  ASSERT_EQ(2, ordering_state->loadProposalHeight().value());
  ASSERT_TRUE(ordering_state->saveProposalHeight(11));
  ASSERT_EQ(11, ordering_state->loadProposalHeight().value());

  // restart Ordering Service Storage
  ordering_state.reset();
  iroha::ametsuchi::PostgresOrderingServicePersistentState::create(pgopt_)
      .match([&](iroha::expected::Value<std::shared_ptr<
                     iroha::ametsuchi::PostgresOrderingServicePersistentState>>
                     &_storage) { ordering_state = _storage.value; },
             [](iroha::expected::Error<std::string> &error) {
               FAIL() << "PostgresOrderingServicePersistentState: "
                      << error.error;
             });
  ASSERT_TRUE(ordering_state);
  ASSERT_EQ(11, ordering_state->loadProposalHeight().value());
}

/**
 * @given 2 different initialized storages for ordering service
 * @when save proposal height to the first one
 * @then the state is consistent
 */
TEST_F(AmetsuchiTest,
       OrderingServicePersistentStorageDifferentConnectionsTest) {
  std::shared_ptr<iroha::ametsuchi::PostgresOrderingServicePersistentState>
      ordering_state_1;
  iroha::ametsuchi::PostgresOrderingServicePersistentState::create(pgopt_)
      .match([&](iroha::expected::Value<std::shared_ptr<
                     iroha::ametsuchi::PostgresOrderingServicePersistentState>>
                     &_storage) { ordering_state_1 = _storage.value; },
             [](iroha::expected::Error<std::string> &error) {
               FAIL() << "PostgresOrderingServicePersistentState: "
                      << error.error;
             });
  ASSERT_TRUE(ordering_state_1);

  std::shared_ptr<iroha::ametsuchi::PostgresOrderingServicePersistentState>
      ordering_state_2;
  iroha::ametsuchi::PostgresOrderingServicePersistentState::create(pgopt_)
      .match([&](iroha::expected::Value<std::shared_ptr<
                     iroha::ametsuchi::PostgresOrderingServicePersistentState>>
                     &_storage) { ordering_state_2 = _storage.value; },
             [](iroha::expected::Error<std::string> &error) {
               FAIL() << "PostgresOrderingServicePersistentState: "
                      << error.error;
             });
  ASSERT_TRUE(ordering_state_2);

  ordering_state_2->resetState();
  ASSERT_EQ(2, ordering_state_1->loadProposalHeight().value());
  ASSERT_EQ(2, ordering_state_2->loadProposalHeight().value());
  ASSERT_TRUE(ordering_state_1->saveProposalHeight(11));
  ASSERT_EQ(11, ordering_state_1->loadProposalHeight().value());
  ASSERT_EQ(11, ordering_state_2->loadProposalHeight().value());

  ordering_state_2->resetState();
  ASSERT_EQ(2, ordering_state_1->loadProposalHeight().value());
  ASSERT_EQ(2, ordering_state_2->loadProposalHeight().value());
  ASSERT_TRUE(ordering_state_2->saveProposalHeight(42));
  ASSERT_EQ(42, ordering_state_1->loadProposalHeight().value());
  ASSERT_EQ(42, ordering_state_2->loadProposalHeight().value());
}
