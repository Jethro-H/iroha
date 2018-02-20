/**
 * Copyright Soramitsu Co., Ltd. 2018 All Rights Reserved.
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

#ifndef IROHA_WSV_QUERY_HPP
#define IROHA_WSV_QUERY_HPP
#include <string>
#include <vector>

#include <nonstd/optional.hpp>

#include "common/types.hpp"
#include "interfaces/common_objects/account.hpp"
#include "interfaces/common_objects/account_asset.hpp"
#include "interfaces/common_objects/asset.hpp"
#include "interfaces/common_objects/domain.hpp"
#include "interfaces/iroha_internal/block.hpp"
#include "interfaces/queries/query.hpp"
#include "interfaces/query_responses/query_response.hpp"
#include "interfaces/transaction.hpp"
#include "interfaces/transaction_responses/tx_response.hpp"


namespace iroha {
  namespace ametsuchi {

    using shared_model::interface::types::AccountIdType;
    using shared_model::interface::types::PermissionNameType;
    using shared_model::interface::types::DomainIdType;
    using shared_model::interface::types::AssetIdType;
    using shared_model::interface::types::JsonType;
    using shared_model::interface::types::RoleIdType;
    using shared_model::interface::types::DetailType;
    using shared_model::interface::types::PubkeyType;


    /**
     *  Public interface for world state view queries
     */
    class WsvQuery {
     public:
      virtual ~WsvQuery() = default;

      /**
       * Check if permitee has permission on account
       * @param permitee_account_id
       * @param account_id
       * @param permission_id
       * @return true if has permission, false otherwise
       */
      virtual bool hasAccountGrantablePermission(
          const AccountIdType &permitee_account_id,
          const AccountIdType &account_id,
          const PermissionNameType &permission_id) = 0;

      /**
       * Get iroha domain
       * @param domain_id - id in the system
       * @return Domain if exist, nullopt otherwise
       */
      virtual nonstd::optional<std::shared_ptr<shared_model::interface::Domain>> getDomain(
          const DomainIdType &domain_id) = 0;

      /**
       * Get account's roles
       * @param account_id
       * @return
       */
      virtual nonstd::optional<std::vector<RoleIdType>> getAccountRoles(
          const AccountIdType &account_id) = 0;
      /**
       * Get all permissions of a role
       * @param role_name
       * @return
       */
      virtual nonstd::optional<std::vector<PermissionNameType>> getRolePermissions(
          const RoleIdType &role_name) = 0;

      /**
       * @return All roles currently in the system
       */
      virtual nonstd::optional<std::vector<RoleIdType>> getRoles() = 0;

      /**
       * Get account by user account_id
       * @param account_id
       * @return
       */
      virtual nonstd::optional<std::shared_ptr<shared_model::interface::Account>> getAccount(
          const AccountIdType &account_id) = 0;

      /**
       * Get accounts information from its key-value storage
       * @param account_id
       * @param creator_account_id
       * @param detail
       * @return
       */
      virtual nonstd::optional<DetailType> getAccountDetail(
          const AccountIdType &account_id,
          const AccountIdType &creator_account_id,
          const DetailType &detail) = 0;

      /**
       * Get signatories of account by user account_id
       * @param account_id
       * @return
       */
      virtual nonstd::optional<std::vector<PubkeyType>> getSignatories(
          const AccountIdType &account_id) = 0;

      /**
       * Get asset by its name
       * @param asset_id
       * @return
       */
      virtual nonstd::optional<std::shared_ptr<shared_model::interface::Asset>> getAsset(
          const AssetIdType &asset_id) = 0;

      /**
       *
       * @param account_id
       * @param asset_id
       * @return
       */
      virtual nonstd::optional<std::shared_ptr<shared_model::interface::AccountAsset>> getAccountAsset(
          const AccountIdType &account_id, const AssetIdType &asset_id) = 0;

      /**
       *
       * @return
       */
      virtual nonstd::optional<std::vector<std::shared_ptr<shared_model::interface::Peer>>> getPeers() = 0;
    };

  }  // namespace ametsuchi
}  // namespace iroha

#endif  // IROHA_WSV_QUERY_HPP
