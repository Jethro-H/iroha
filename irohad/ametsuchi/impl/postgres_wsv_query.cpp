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

#include "ametsuchi/impl/postgres_wsv_query.hpp"

namespace iroha {
  namespace ametsuchi {

    const std::string kRoleId = "role_id";
    const char *kAccountNotFound = "Account {} not found";
    const std::string kPublicKey = "public_key";
    const std::string kAssetId = "asset_id";
    const std::string kAccountId = "account_id";
    const std::string kDomainId = "domain_id";

    PostgresWsvQuery::PostgresWsvQuery(pqxx::nontransaction &transaction)
        : transaction_(transaction),
          log_(logger::log("PostgresWsvQuery")),
          execute_{makeExecuteOptional
                       (transaction_, log_)} {}

    bool PostgresWsvQuery::hasAccountGrantablePermission(
        const AccountIdType &permitee_account_id,
        const AccountIdType &account_id,
        const PermissionNameType &permission_id) {
      return execute_(
                 "SELECT * FROM account_has_grantable_permissions WHERE "
                 "permittee_account_id = "
                 + transaction_.quote(permitee_account_id)
                 + " AND account_id = " + transaction_.quote(account_id)
                 + " AND permission_id = " + transaction_.quote(permission_id)
                 + ";")
          | [](const auto &result) { return result.size() == 1; };
    }

    nonstd::optional<std::vector<RoleIdType>> PostgresWsvQuery::getAccountRoles(
        const AccountIdType &account_id) {
      return execute_(
                 "SELECT role_id FROM account_has_roles WHERE account_id = "
                 + transaction_.quote(account_id) + ";")
          | [&](const auto &result) {
              return transform<std::string>(result, [](const auto &row) {
                return row.at(kRoleId).c_str();
              });
            };
    }

    nonstd::optional<std::vector<PermissionNameType>>
    PostgresWsvQuery::getRolePermissions(const RoleIdType &role_name) {
      return execute_(
                 "SELECT permission_id FROM role_has_permissions WHERE role_id "
                 "= "
                 + transaction_.quote(role_name) + ";")
          | [&](const auto &result) {
              return transform<std::string>(result, [](const auto &row) {
                return row.at("permission_id").c_str();
              });
            };
    }

    nonstd::optional<std::vector<RoleIdType>> PostgresWsvQuery::getRoles() {
      return execute_("SELECT role_id FROM role;") | [&](const auto &result) {
        return transform<std::string>(
            result, [](const auto &row) { return row.at(kRoleId).c_str(); });
      };
    }

    nonstd::optional<std::shared_ptr<shared_model::interface::Account>>
    PostgresWsvQuery::getAccount(const AccountIdType &account_id) {
      return execute_("SELECT * FROM account WHERE account_id = "
                      + transaction_.quote(account_id) + ";")
                 | [&](const auto &result)
                 -> nonstd::optional<
                     std::shared_ptr<shared_model::interface::Account>> {
        if (result.empty()) {
          log_->info(kAccountNotFound, account_id);
          return nonstd::nullopt;
        }

        return fromResult(makeAccount(result.at(0)));
      };
    }

    nonstd::optional<DetailType> PostgresWsvQuery::getAccountDetail(
        const AccountIdType &account_id,
        const AccountIdType &creator_account_id,
        const DetailType &detail) {
      return execute_("SELECT data#>>"
                      + transaction_.quote("{" + creator_account_id + ", "
                                           + detail + "}")
                      + " FROM account WHERE account_id = "
                      + transaction_.quote(account_id) + ";")
                 | [&](const auto &result) -> nonstd::optional<std::string> {
        if (result.empty()) {
          log_->info(kAccountNotFound, account_id);
          return nonstd::nullopt;
        }
        auto row = result.at(0);
        std::string res;
        row.at(0) >> res;

        // if res is empty, then that key does not exist for this account
        if (res.empty()) {
          return nonstd::nullopt;
        }
        return res;
      };
    }

    nonstd::optional<std::vector<PubkeyType>> PostgresWsvQuery::getSignatories(
        const AccountIdType &account_id) {
      return execute_(
                 "SELECT public_key FROM account_has_signatory WHERE "
                 "account_id = "
                 + transaction_.quote(account_id) + ";")
          | [&](const auto &result) {
              return transform<PubkeyType>(result, [&](const auto &row) {
                pqxx::binarystring public_key_str(row.at(kPublicKey));
                return PubkeyType(public_key_str.str());
              });
            };
    }

    nonstd::optional<std::shared_ptr<shared_model::interface::Asset>>
    PostgresWsvQuery::getAsset(const AssetIdType &asset_id) {
      pqxx::result result;
      return execute_("SELECT * FROM asset WHERE asset_id = "
                      + transaction_.quote(asset_id) + ";")
                 | [&](const auto &result)
                 -> nonstd::optional<
                     std::shared_ptr<shared_model::interface::Asset>> {
        if (result.empty()) {
          log_->info("Asset {} not found", asset_id);
          return nonstd::nullopt;
        }
        return fromResult(makeAsset(result.at(0)));
      };
    }

    nonstd::optional<std::shared_ptr<shared_model::interface::AccountAsset>>
    PostgresWsvQuery::getAccountAsset(const AccountIdType &account_id,
                                      const AssetIdType &asset_id) {
      return execute_("SELECT * FROM account_has_asset WHERE account_id = "
                      + transaction_.quote(account_id)
                      + " AND asset_id = " + transaction_.quote(asset_id) + ";")
                 | [&](const auto &result)
                 -> nonstd::optional<
                     std::shared_ptr<shared_model::interface::AccountAsset>> {
        if (result.empty()) {
          log_->info("Account {} does not have asset {}", account_id, asset_id);
          return nonstd::nullopt;
        }

        return fromResult(makeAccountAsset(result.at(0)));
      };
    }

    nonstd::optional<std::shared_ptr<shared_model::interface::Domain>>
    PostgresWsvQuery::getDomain(const DomainIdType &domain_id) {
      return execute_("SELECT * FROM domain WHERE domain_id = "
                      + transaction_.quote(domain_id) + ";")
                 | [&](const auto &result)
                 -> nonstd::optional<
                     std::shared_ptr<shared_model::interface::Domain>> {
        if (result.empty()) {
          log_->info("Domain {} not found", domain_id);
          return nonstd::nullopt;
        }
        return fromResult(makeDomain(result.at(0)));
      };
    }

    nonstd::optional<
        std::vector<std::shared_ptr<shared_model::interface::Peer>>>
    PostgresWsvQuery::getPeers() {
      pqxx::result result;
      return execute_("SELECT * FROM peer;") | [&](const auto &result)
                 -> nonstd::optional<std::vector<
                     std::shared_ptr<shared_model::interface::Peer>>> {
        auto results = transform<BuilderResult<shared_model::interface::Peer>>(
            result, makePeer);
        std::vector<std::shared_ptr<shared_model::interface::Peer>> peers;
        for (auto &r : results) {
          r.match(
              [&](expected::Value<
                  std::shared_ptr<shared_model::interface::Peer>> &v) {
                peers.push_back(v.value);
              },
              [&](expected::Error<std::shared_ptr<std::string>> &e) {});
        }
        return peers;
      };
    }
  }  // namespace ametsuchi
}  // namespace iroha
