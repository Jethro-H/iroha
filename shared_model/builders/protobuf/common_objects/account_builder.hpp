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

#ifndef IROHA_ACCOUNT_BUILDER_HPP
#define IROHA_ACCOUNT_BUILDER_HPP

#include "interfaces/common_objects/account.hpp"

namespace shared_model {
  namespace proto {
    class AccountBuilder {
     public:
      std::shared_ptr<shared_model::interface::Account> build();

      AccountBuilder accountId(
          const interface::types::AccountIdType &account_id);

      AccountBuilder domainId(const interface::types::DomainIdType &domainId);

      AccountBuilder quorum(const interface::types::QuorumType &quorum);

      AccountBuilder jsonData(const interface::types::JsonType &json);
    };
  }  // namespace proto
}  // namespace shared_model

#endif  // IROHA_ACCOUNT_BUILDER_HPP