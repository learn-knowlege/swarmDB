// Copyright (C) 2018 Bluzelle
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License, version 3,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <crud/crud.hpp>
#include <storage/mem_storage.hpp>
#include <mocks/mock_session_base.hpp>
#include <mocks/mock_subscription_manager_base.hpp>
#include <mocks/mock_node_base.hpp>
#include <mocks/mock_pbft_base.hpp>
#include <mocks/mock_boost_asio_beast.hpp>
#include <algorithm>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <boost/format.hpp>

using namespace ::testing;


namespace
{
    const std::string TTL_UUID{"TTL"};

    bool parse_env_to_db_resp(database_response& target, const std::string& source)
    {
        bzn_envelope intermediate;
        return intermediate.ParseFromString(source) && target.ParseFromString(intermediate.database_response());
    }

    void expect_signed_response(const std::shared_ptr<bzn::Mocksession_base>& session,
            std::optional<bzn::uuid_t> db_uuid = std::nullopt,
            std::optional<uint64_t> nonce = std::nullopt,
            std::optional<database_response::ResponseCase> response_case = std::nullopt,
            std::optional<std::string> error_msg = std::nullopt,
            std::function<void(const database_response&)> additional_checks = [](auto){})
    {
        EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
            [=](std::shared_ptr<bzn_envelope> env)
            {
                EXPECT_EQ(env->payload_case(), bzn_envelope::kDatabaseResponse);
                database_response resp;
                resp.ParseFromString(env->database_response());

                if (db_uuid)
                {
                    EXPECT_EQ(resp.header().db_uuid(), *db_uuid);
                }

                if (nonce)
                {
                    EXPECT_EQ(resp.header().nonce(), *nonce);
                }

                if (response_case)
                {
                    EXPECT_EQ(resp.response_case(), *response_case);
                }

                if (error_msg)
                {
                    EXPECT_EQ(resp.error().message(), *error_msg);
                }

                additional_checks(resp);
            }));
    }

    void expect_response(const std::shared_ptr<bzn::Mocksession_base>& session,
        std::optional<bzn::uuid_t> db_uuid = std::nullopt,
        std::optional<uint64_t> nonce = std::nullopt,
        std::optional<database_response::ResponseCase> response_case = std::nullopt,
        std::optional<std::string> error_msg = std::nullopt,
        std::function<void(const database_response&)> additional_checks = [](auto){})
    {
        EXPECT_CALL(*session, send_message(_)).WillOnce(Invoke(
            [=](std::shared_ptr<std::string> msg)
            {
                bzn_envelope env;
                env.ParseFromString(*msg);

                EXPECT_EQ(env.payload_case(), bzn_envelope::kDatabaseResponse);
                database_response resp;
                resp.ParseFromString(env.database_response());

                if (db_uuid)
                {
                    EXPECT_EQ(resp.header().db_uuid(), *db_uuid);
                }

                if (nonce)
                {
                    EXPECT_EQ(resp.header().nonce(), *nonce);
                }

                if (response_case)
                {
                    EXPECT_EQ(resp.response_case(), *response_case);
                }

                if (error_msg)
                {
                    EXPECT_EQ(resp.error().message(), *error_msg);
                }

                additional_checks(resp);
            }));
    }

    std::shared_ptr<bzn::crud>
    initialize_crud(
            std::shared_ptr<bzn::Mocksession_base>& session
            , std::shared_ptr<bzn::Mocknode_base>& mock_node
            , bzn::uuid_t caller_id)
    {
        mock_node = std::make_shared<bzn::Mocknode_base>();
        auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
        auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
        session = std::make_shared<bzn::Mocksession_base>();

        EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
                [&]()
                {
                    return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
                }));

        auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, mock_node, caller_id);

        auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

        EXPECT_CALL(*mock_pbft,
                    current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));
        crud->start(mock_pbft);
        return crud;
    }

    void
    create_test_database(
            const std::shared_ptr<bzn::crud>& crud
            , const std::shared_ptr<bzn::Mocksession_base>& session
            , const std::shared_ptr<bzn::Mocknode_base>& mock_node
            , const bzn::uuid_t& caller_uuid
            , const bzn::uuid_t& db_uuid
            , uint64_t max_size = 0
            , database_create_db_eviction_policy_type eviction_policy = database_create_db_eviction_policy_type_NONE)
    {
        database_msg msg;
        msg.mutable_header()->set_db_uuid(db_uuid);
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_header()->set_point_of_contact(caller_uuid);
        msg.mutable_create_db()->set_max_size(max_size);
        msg.mutable_create_db()->set_eviction_policy(eviction_policy);

        // We are not testing create database, so we can suppress the send_signed_message calls.
        EXPECT_CALL(*session, send_signed_message(_));
        EXPECT_CALL(*mock_node, send_signed_message(A<const std::string&>(),_));

        crud->handle_request(caller_uuid, msg, session);
    }


    void
    remove_test_database(
            const std::shared_ptr<bzn::crud>& crud
            , const std::shared_ptr<bzn::Mocksession_base>& session
            , const std::shared_ptr<bzn::Mocknode_base>& mock_node
            , const bzn::uuid_t& caller_uuid
            , const bzn::uuid_t& db_uuid)
    {
        database_msg msg;
        msg.mutable_header()->set_db_uuid(db_uuid);
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_header()->set_point_of_contact(caller_uuid);

        msg.mutable_delete_db();

        // We are not testing remove database, so hide these messsages.
        EXPECT_CALL(*mock_node, send_signed_message(A<const std::string&>(),_));
        EXPECT_CALL(*session, send_signed_message(_));

        crud->handle_request(caller_uuid, msg, session);
    }


    void
    create_key_value(
            const std::shared_ptr<bzn::crud>& crud
            , const std::shared_ptr<bzn::Mocksession_base>& session
            , const std::shared_ptr<bzn::Mocknode_base>& mock_node
            , const bzn::uuid_t& caller
            , const bzn::uuid_t& db
            , const std::string& key
            , const std::string& value )
    {
        database_msg msg;
        msg.mutable_header()->set_point_of_contact(caller);
        msg.mutable_header()->set_db_uuid(db);
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_create()->set_key(key);
        msg.mutable_create()->set_value(value);

        // We are not testing create, so we can suppress the send_signed_message calls.
        EXPECT_CALL(*session, send_signed_message(_));
        EXPECT_CALL(*mock_node, send_signed_message(A<const std::string&>(),_));

        crud->handle_request(caller, msg, session);
    }


    std::pair<std::size_t, std::size_t>
    get_database_size(
            const std::shared_ptr<bzn::crud>& crud
            , const std::shared_ptr<bzn::Mocksession_base>& session
            , const std::shared_ptr<bzn::Mocknode_base>& mock_node
            , const bzn::uuid_t& caller, const bzn::uuid_t& db)
    {
        std::promise<std::pair<std::size_t, std::size_t>> db_size_promise;
        database_msg msg;
        msg.mutable_header()->set_db_uuid(db);
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_header()->set_point_of_contact(caller);
        msg.mutable_size();

        EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
                [&](const std::shared_ptr<bzn_envelope>& env)
                {
                    database_response resp;
                    resp.ParseFromString(env->database_response());
                    db_size_promise.set_value(std::pair<std::size_t, std::size_t>{resp.size().keys(), resp.size().bytes()});
                }));


        // We are not testing get database size, so we can suppress the mock_node send_signed_message call.
        EXPECT_CALL(*mock_node, send_signed_message(A<const std::string&>(),_));
        std::future<std::pair<std::size_t, std::size_t>> future = db_size_promise.get_future();

        crud->handle_request("", msg, session);

        return future.get();
    }


    std::set<std::string>
    get_database_keys(
            const std::shared_ptr<bzn::crud>& crud
            , const std::shared_ptr<bzn::Mocksession_base>& session
            , const std::shared_ptr<bzn::Mocknode_base>& mock_node
            , const bzn::uuid_t& caller_id
            , const bzn::uuid_t& db_uuid)
    {
        database_msg msg;
        std::promise<std::set<std::string>> keys_promise;

        msg.mutable_header()->set_point_of_contact(caller_id);
        msg.mutable_header()->set_db_uuid(db_uuid);
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_keys();

        expect_signed_response(session, db_uuid, uint64_t(123), database_response::kKeys, std::nullopt,
                               [&](const auto& resp)
                               {
                                   std::set<std::string> tmp_set(resp.keys().keys().begin(), resp.keys().keys().end());
                                   keys_promise.set_value(tmp_set);
                               });

        // We are not testing get database keys, so we can suppress the mock_node send_signed_message call.
        EXPECT_CALL(*mock_node, send_signed_message(A<const std::string&>(),_));

        std::future<std::set<std::string>> future = keys_promise.get_future();
        crud->handle_request(db_uuid, msg, session);
        return future.get();
    }
}


TEST(crud, test_that_create_sends_proper_response)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    auto session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(session, "uuid", 123, std::nullopt,
        bzn::storage_result_msg.at(bzn::storage_result::db_not_found));

    crud->handle_request("caller_id", msg, session);

    // now create the db...
    msg.mutable_create_db();

    expect_signed_response(session, "uuid", 123, database_response::RESPONSE_NOT_SET);

    crud->handle_request("caller_id", msg, session);

    // now test creates...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");
    msg.mutable_create()->set_expire(2);

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    expect_signed_response(session, "uuid", 123, database_response::RESPONSE_NOT_SET);

    crud->handle_request("caller_id", msg, session);

    // fail to create same key...
    expect_signed_response(session, "uuid", 123, database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::exists));

    crud->handle_request("caller_id", msg, session);

    // fail because key is too big...
    msg.mutable_create()->set_key(std::string(bzn::MAX_KEY_SIZE + 1, '*'));
    expect_signed_response(session, "uuid", 123, database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::key_too_large));

    crud->handle_request("caller_id", msg, session);

    // fail because value is too big...
    msg.mutable_create()->set_value(std::string(bzn::MAX_VALUE_SIZE + 1, '*'));
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::value_too_large));

    crud->handle_request("caller_id", msg, session);

    // get ttl for new key
    msg.mutable_ttl()->set_key("key");

    expect_signed_response(session, "uuid", uint64_t(123), database_response::kTtl, std::nullopt,
        [](const database_response& resp)
        {
            EXPECT_EQ(resp.ttl().key(), "key");
            EXPECT_GE(resp.ttl().ttl(), uint64_t(1));
        });

    crud->handle_request("caller_id", msg, session);

    // expire key...
    sleep(2);

    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::delete_pending));

    crud->handle_request("caller_id", msg, session);

    // calling create should return delete pending...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");
    msg.mutable_create()->set_expire(2);

    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::delete_pending));

    crud->handle_request("caller_id", msg, session);
}


TEST(crud, test_that_point_of_contact_create_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));

    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact",_)).WillOnce(Invoke(
        [&](const bzn::uuid_t& , auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::db_not_found));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // now create the db...
    msg.mutable_create_db();

    // virtual void send_signed_message(const bzn::uuid_t& , std::shared_ptr<bzn_envelope> msg, bool close_session) = 0;
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", _)).WillOnce(Invoke(
        [&](const auto& , auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // now test creates...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", _)).WillOnce(Invoke(
        [&](const auto& , auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));

            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // ttl should fail since default is zero...
    msg.mutable_ttl()->set_key("key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", _)).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::ttl_not_found));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // fail to create same key...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", _)).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));

            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kError);
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::exists));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // fail because key is too big...
    msg.mutable_create()->set_key(std::string(bzn::MAX_KEY_SIZE+1,'*'));
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", _)).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kError);
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::key_too_large));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // fail because value is too big...
    msg.mutable_create()->set_value(std::string(bzn::MAX_VALUE_SIZE+1,'*'));
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", _)).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kError);
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::value_too_large));
        }));

    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_read_sends_proper_response)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    crud->handle_request("caller_id", msg, nullptr);

    // test reads...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");
    msg.mutable_create()->set_expire(2);

    // add key...
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    expect_signed_response(session);
    crud->handle_request("caller_id", msg, session);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // read key...
    msg.mutable_read()->set_key("key");
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kRead, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.read().key(), "key");
            ASSERT_EQ(resp.read().value(), "value");
        });

    crud->handle_request("caller_id", msg, session);

    // quick read key...
    msg.mutable_quick_read()->set_key("key");
    expect_response(session, "uuid", uint64_t(123), database_response::kQuickRead, std::nullopt,
            [](const auto& resp)
            {
                ASSERT_EQ(resp.quick_read().key(), "key");
                ASSERT_EQ(resp.quick_read().value(), "value");
            });

    crud->handle_request("caller_id", msg, session);

    // invalid db...
    msg.mutable_header()->set_db_uuid("invalid");
    msg.mutable_read()->set_key("key");
    expect_signed_response(session, "invalid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::db_not_found));

    crud->handle_request("caller_id", msg, session);

    msg.mutable_quick_read()->set_key("key");
    expect_response(session, "invalid", uint64_t(123), database_response::kQuickRead, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.quick_read().error(), bzn::storage_result_msg.at(bzn::storage_result::db_not_found));
        });

    crud->handle_request("caller_id", msg, session);

    // read invalid key...
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_read()->set_key("invalid-key");
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::not_found));

    crud->handle_request("caller_id", msg, session);

    // quick read invalid key...
    msg.mutable_quick_read()->set_key("invalid-key");
    expect_response(session, "uuid", uint64_t(123), database_response::kQuickRead, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.quick_read().error(), bzn::storage_result_msg.at(bzn::storage_result::not_found));
        });

    crud->handle_request("caller_id", msg, session);

    // null session nothing should happen...
    crud->handle_request("caller_id", msg, nullptr);

    // expired key should return delete_pending
    msg.mutable_read()->set_key("key");

    sleep(2);

    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::delete_pending));

    crud->handle_request("caller_id", msg, session);

}


TEST(crud, test_that_point_of_contact_read_sends_proper_response)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));

    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // test reads...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // read key...
    msg.mutable_read()->set_key("key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kRead);
            ASSERT_EQ(resp.read().key(), "key");
            ASSERT_EQ(resp.read().value(), "value");
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // quick read key...
    msg.mutable_quick_read()->set_key("key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", _)).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kQuickRead);
            ASSERT_EQ(resp.quick_read().key(), "key");
            ASSERT_EQ(resp.quick_read().value(), "value");
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // read invalid key...
    msg.mutable_read()->set_key("invalid-key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kError);
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::not_found));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // quick read invalid key...
    msg.mutable_quick_read()->set_key("invalid-key");
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kQuickRead);
			ASSERT_EQ(resp.quick_read().error(), bzn::storage_result_msg.at(bzn::storage_result::not_found));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // null point_of_contact, nothing should happen...
    msg.mutable_header()->clear_point_of_contact();
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_update_sends_proper_response)
{
    auto mock_subscription_manager = std::make_shared<bzn::Mocksubscription_manager_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    EXPECT_CALL(*mock_subscription_manager, start());

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    crud->handle_request("caller_id", msg, nullptr);

    // test updates...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    expect_signed_response(session);
    crud->handle_request("caller_id", msg, session);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // update key...
    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    msg.mutable_update()->set_key("key");
    msg.mutable_update()->set_value("updated");
    msg.mutable_update()->set_expire(2);
    expect_signed_response(session, "uuid", uint64_t(123), database_response::RESPONSE_NOT_SET);

    crud->handle_request("caller_id", msg, session);

    // clear msg...
    msg.mutable_update()->release_key();
    msg.mutable_update()->release_value();

    // read updated key...
    msg.mutable_read()->set_key("key");
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kRead, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.read().key(), "key");
            ASSERT_EQ(resp.read().value(), "updated");
        });

    crud->handle_request("caller_id", msg, session);

    // expired key should return delete_pending
    msg.mutable_update()->set_key("key");
    msg.mutable_update()->set_value("updated");

    sleep(2);

    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::delete_pending));

    crud->handle_request("caller_id", msg, session);
}


TEST(crud, test_that_point_of_contact_update_sends_proper_response)
{
    auto mock_subscription_manager = std::make_shared<bzn::Mocksubscription_manager_base>();
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    EXPECT_CALL(*mock_subscription_manager, start());

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));

    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // test updates...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // update key...
    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    msg.mutable_update()->set_key("key");
    msg.mutable_update()->set_value("updated");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // clear msg...
    msg.mutable_update()->release_key();
    msg.mutable_update()->release_value();

    // read updated key...
    msg.mutable_read()->set_key("key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kRead);
            ASSERT_EQ(resp.read().key(), "key");
            ASSERT_EQ(resp.read().value(), "updated");
        }));

    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_delete_sends_proper_response)
{
    auto mock_subscription_manager = std::make_shared<bzn::Mocksubscription_manager_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    EXPECT_CALL(*mock_subscription_manager, start());

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    crud->handle_request("caller_id", msg, nullptr);

    // test deletes...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    expect_signed_response(session);
    crud->handle_request("caller_id", msg, session);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // delete key...
    msg.mutable_delete_()->set_key("key");
    expect_signed_response(session, "uuid", uint64_t(123), database_response::RESPONSE_NOT_SET);

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    crud->handle_request("caller_id", msg, session);

    // delete invalid key...
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError, bzn::storage_result_msg.at(bzn::storage_result::not_found));

    crud->handle_request("caller_id", msg, session);
}


TEST(crud, test_that_point_of_contact_delete_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_subscription_manager = std::make_shared<bzn::Mocksubscription_manager_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        mock_subscription_manager, mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    EXPECT_CALL(*mock_subscription_manager, start());

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // test deletes...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // delete key...
    msg.mutable_delete_()->set_key("key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));

    crud->handle_request("caller_id", msg, nullptr);

    // delete invalid key...
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kError);
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::not_found));
        }));

    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_has_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    crud->handle_request("caller_id", msg, nullptr);

    // test has...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    auto session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(session);
    crud->handle_request("caller_id", msg, session);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // valid key...
    msg.mutable_has()->set_key("key");
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kHas, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.has().key(), "key");
            ASSERT_TRUE(resp.has().has());
        });

    crud->handle_request("caller_id", msg, session);

    // invalid key...
    msg.mutable_has()->set_key("invalid-key");
    expect_signed_response(session, "uuid", 123, database_response::kHas, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.has().key(), "invalid-key");
            ASSERT_FALSE(resp.has().has());
        });

    crud->handle_request("caller_id", msg, session);

    // invalid database
    msg.mutable_header()->set_db_uuid("invalid-uuid");
    msg.mutable_has()->set_key("key");
    expect_signed_response(session, "invalid-uuid", 123, database_response::kError, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.header().db_uuid(), "invalid-uuid");
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::db_not_found));
        });

    crud->handle_request("caller_id", msg, session);

    // null session nothing should happen...
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_point_of_contact_has_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // test has...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // valid key...
    msg.mutable_has()->set_key("key");
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kHas);
            ASSERT_EQ(resp.has().key(), "key");
            ASSERT_TRUE(resp.has().has());
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // invalid key...
    msg.mutable_has()->set_key("invalid-key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kHas);
            ASSERT_EQ(resp.has().key(), "invalid-key");
            ASSERT_FALSE(resp.has().has());
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // null session nothing should happen...
    msg.mutable_header()->clear_point_of_contact();
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_keys_sends_proper_response)
{
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    crud->handle_request("caller_id", msg, nullptr);

    // test keys...
    msg.mutable_create()->set_key("key1");
    msg.mutable_create()->set_value("value");

    // add key...
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*session, send_signed_message(_)).Times(2);
    crud->handle_request("caller_id", msg, session);

    // add another...
    msg.mutable_create()->set_key("key2");
    crud->handle_request("caller_id", msg, session);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // get keys...
    msg.mutable_keys();
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kKeys, std::nullopt,
        [](auto resp)
        {
            ASSERT_EQ(resp.keys().keys().size(), int(2));
            // keys are not returned in order created...
            auto keys = resp.keys().keys();
            std::sort(keys.begin(), keys.end());
            ASSERT_EQ(keys[0], "key1");
            ASSERT_EQ(keys[1], "key2");
        });

    crud->handle_request("caller_id", msg, session);

    // invalid uuid returns empty message...
    msg.mutable_header()->set_db_uuid("invalid-uuid");
    expect_signed_response(session, "invalid-uuid", uint64_t(123), database_response::kError, std::nullopt,
        [](const auto& resp)
        {
            ASSERT_EQ(resp.header().db_uuid(), "invalid-uuid");
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::db_not_found));
        });

    crud->handle_request("caller_id", msg, session);

    // null session nothing should happen...
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_point_of_contact_keys_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // test keys...
    msg.mutable_create()->set_key("key1");
    msg.mutable_create()->set_value("value");

    // add key...
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // add another...
    msg.mutable_create()->set_key("key2");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // get keys...
    msg.mutable_keys();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kKeys);
            ASSERT_EQ(resp.keys().keys().size(), int(2));
            // keys are not returned in order created...
            auto keys = resp.keys().keys();
            std::sort(keys.begin(), keys.end());
            ASSERT_EQ(keys[0], "key1");
            ASSERT_EQ(keys[1], "key2");
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // invalid uuid returns empty message...
    msg.mutable_header()->set_db_uuid("invalid-uuid");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "invalid-uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kError);
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::db_not_found));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // null session nothing should happen...
    msg.mutable_header()->clear_point_of_contact();
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_size_sends_proper_response)
{
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    crud->handle_request("caller_id", msg, nullptr);

    // test size...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    auto session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(session);
    crud->handle_request("caller_id", msg, session);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // get size...
    msg.mutable_size();
    expect_signed_response(session, "uuid", uint64_t(123), database_response::kSize, std::nullopt,
        [](auto resp)
        {
            ASSERT_EQ(resp.size().bytes(), uint64_t(8));
            ASSERT_EQ(resp.size().keys(), uint32_t(1));
        });

    crud->handle_request("caller_id", msg, session);

    // invalid uuid returns zero...
    msg.mutable_header()->set_db_uuid("invalid-uuid");
    expect_signed_response(session, "invalid-uuid", uint64_t(123), database_response::kError);

    crud->handle_request("caller_id", msg, session);

    // null session nothing should happen...
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_point_of_contact_size_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // test size...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");

    // add key...
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // clear msg...
    msg.mutable_create()->release_key();
    msg.mutable_create()->release_value();

    // get size...
    msg.mutable_size();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kSize);
            ASSERT_EQ(resp.size().bytes(), uint64_t(8));
            ASSERT_EQ(resp.size().keys(), uint32_t(1));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // invalid uuid returns zero...
    msg.mutable_header()->set_db_uuid("invalid-uuid");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [&](const auto&, auto msg)
        {
            database_response resp;
            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "invalid-uuid");
            ASSERT_EQ(resp.header().nonce(), uint64_t(123));
            ASSERT_EQ(resp.response_case(), database_response::kError);
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // null session nothing should happen...
    msg.mutable_header()->clear_point_of_contact();
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_subscribe_request_calls_subscription_manager)
{
    auto mock_subscription_manager = std::make_shared<bzn::Mocksubscription_manager_base>();

    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, nullptr);

    EXPECT_CALL(*mock_subscription_manager, start());

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    // subscribe...
    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_subscribe()->set_key("key");

    // nothing should happen...
    crud->handle_request("caller_id", msg, nullptr);

    // try again with a valid session...
    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_subscription_manager, subscribe(msg.header().db_uuid(), msg.subscribe().key(),
        msg.header().nonce(), _, _));

    expect_signed_response(mock_session);

    crud->handle_request("caller_id", msg, mock_session);
}


TEST(crud, test_that_unsubscribe_request_calls_subscription_manager)
{
    auto mock_subscription_manager = std::make_shared<bzn::Mocksubscription_manager_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, nullptr);

    EXPECT_CALL(*mock_subscription_manager, start());

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    // unsubscribe...
    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_unsubscribe()->set_key("key");
    msg.mutable_unsubscribe()->set_nonce(321);

    // nothing should happen...
    crud->handle_request("caller_id", msg, nullptr);
    
    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_subscription_manager, unsubscribe(msg.header().db_uuid(), msg.unsubscribe().key(),
        msg.unsubscribe().nonce(), _, _));

    expect_signed_response(mock_session);

    crud->handle_request("caller_id", msg, mock_session);
}


TEST(crud, test_that_create_db_request_sends_proper_response)
{
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    // create database...
    database_msg msg;
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(mock_session, "uuid", std::nullopt, database_response::RESPONSE_NOT_SET);

    crud->handle_request("caller_id", msg, mock_session);

    expect_signed_response(mock_session, "uuid", std::nullopt, std::nullopt, bzn::storage_result_msg.at(bzn::storage_result::db_exists));

    // try to create it again...
    crud->handle_request("caller_id", msg, mock_session);
}


TEST(crud, test_that_point_of_contact_create_db_request_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    // create database...
    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));

    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    crud->handle_request("caller_id", msg, nullptr);

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::db_exists));
        }));

    // try to create it again...
    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_has_db_request_sends_proper_response)
{
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    // create db...
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    // nothing should happen...
    crud->handle_request("caller_id", msg, nullptr);

    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    // request has db..
    msg.mutable_has_db();

    expect_signed_response(mock_session, "uuid", std::nullopt, database_response::kHasDb, std::nullopt,
        [](auto resp)
        {
            ASSERT_TRUE(resp.has_db().has());
        });

    crud->handle_request("caller_id", msg, mock_session);

    // request invalid db...
    msg.mutable_header()->set_db_uuid("invalid-uuid");

    expect_signed_response(mock_session, std::nullopt, std::nullopt, database_response::kHasDb, std::nullopt,
        [](auto resp)
        {
            ASSERT_FALSE(resp.has_db().has());
            ASSERT_EQ(resp.has_db().uuid(), "invalid-uuid");
        });

    crud->handle_request("caller_id", msg, mock_session);
}


TEST(crud, test_that_point_of_contact_has_db_request_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    // create db...
    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));
    crud->handle_request("caller_id", msg, nullptr);

    // request has db..
    msg.mutable_has_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.response_case(), database_response::kHasDb);
            ASSERT_EQ(resp.has_db().uuid(), "uuid");
            ASSERT_TRUE(resp.has_db().has());
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // request invalid db...
    msg.mutable_header()->set_db_uuid("invalid-uuid");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.response_case(), database_response::kHasDb);
            ASSERT_EQ(resp.has_db().uuid(), "invalid-uuid");
            ASSERT_FALSE(resp.has_db().has());
        }));

    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_delete_db_sends_proper_response)
{
    auto storage = std::make_shared<bzn::mem_storage>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillOnce(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    // delete database...
    database_msg msg;
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_delete_db();

    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(mock_session, "uuid", std::nullopt, std::nullopt, bzn::storage_result_msg.at(bzn::storage_result::db_not_found));

    crud->handle_request("caller_id", msg, mock_session);

    // create a database...
    msg.mutable_create_db();

    expect_signed_response(mock_session);

    crud->handle_request("caller_id", msg, mock_session);

    // add a key with a ttl
    msg.mutable_create()->set_key("key1");
    msg.mutable_create()->set_value("value");
    msg.mutable_create()->set_expire(123);

    crud->handle_request("caller_id", msg, nullptr);

    // delete database...
    msg.mutable_delete_db();

    expect_signed_response(mock_session, "uuid", std::nullopt, std::nullopt, bzn::storage_result_msg.at(bzn::storage_result::access_denied));

    // non-owner caller...
    crud->handle_request("bad_caller_id", msg, mock_session);

    expect_signed_response(mock_session, "uuid", std::nullopt, database_response::RESPONSE_NOT_SET);

    crud->handle_request("caller_id", msg, mock_session);

    // test storage for ttl entry
    Json::Value ttl_key;
    ttl_key["uuid"] = "uuid";
    ttl_key["key"] = "key1";

    ASSERT_FALSE(storage->has(TTL_UUID, ttl_key.toStyledString()));
}


TEST(crud, test_that_point_of_contact_delete_db_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    // delete database...
    database_msg msg;

    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_delete_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::db_not_found));
        }));

    crud->handle_request("caller_id", msg, nullptr);

    // create a database...
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));

    crud->handle_request("caller_id", msg, nullptr);

    // delete database...
    msg.mutable_delete_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::access_denied));
        }));

    // non-owner caller...
    crud->handle_request("bad_caller_id", msg, nullptr);

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    crud->handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_state_can_be_saved_and_retrieved)
{
    bzn::crud crud(std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>(), std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    ASSERT_TRUE(crud.save_state());

    auto state = crud.get_saved_state();

    ASSERT_TRUE(state);

    ASSERT_TRUE(crud.load_state(*state));
}


TEST(crud, test_that_writers_sends_proper_response)
{
    bzn::crud crud(std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>(), std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    // create database...
    database_msg msg;
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(mock_session);

    crud.handle_request("caller_id", msg, mock_session);

    // request writers...
    msg.mutable_writers();

    // only the owner should be set at this stage...
    expect_signed_response(mock_session, "uuid", std::nullopt, database_response::kWriters, std::nullopt,
        [](auto resp)
        {
            ASSERT_EQ(resp.writers().owner(), "caller_id");
            ASSERT_EQ(resp.writers().writers().size(), 0);
        });

    crud.handle_request("caller_id", msg, mock_session);
}


TEST(crud, test_that_point_of_contact_writers_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();

    bzn::crud crud(std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>(), std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    // create database...
    database_msg msg;
    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));

    crud.handle_request("caller_id", msg, nullptr);

    // request writers...
    msg.mutable_writers();

    // only the owner should be set at this stage...
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.response_case(), database_response::kWriters);
            ASSERT_EQ(resp.writers().owner(), "caller_id");
            ASSERT_EQ(resp.writers().writers().size(), 0);
        }));

    crud.handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_add_writers_sends_proper_response)
{
    bzn::crud crud(std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>(), std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    // create database...
    database_msg msg;
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(mock_session);

    crud.handle_request("caller_id", msg, mock_session);

    // request writers...

    // should not be added to writers as this is the owner
    msg.mutable_add_writers()->add_writers("caller_id");
    msg.mutable_add_writers()->add_writers("client_1_key");
    msg.mutable_add_writers()->add_writers("client_2_key");

    expect_signed_response(mock_session, "uuid", std::nullopt, database_response::RESPONSE_NOT_SET);

    crud.handle_request("caller_id", msg, mock_session);

    // access test
    expect_signed_response(mock_session, "uuid", std::nullopt, std::nullopt, bzn::storage_result_msg.at(bzn::storage_result::access_denied));

    crud.handle_request("other_caller_id", msg, mock_session);

    // request writers...
    msg.mutable_writers();

    // only the owner should be set at this stage...
    expect_signed_response(mock_session, "uuid", std::nullopt, database_response::kWriters, std::nullopt,
        [](auto resp)
        {
            ASSERT_EQ(resp.writers().owner(), "caller_id");
            ASSERT_EQ(resp.writers().writers().size(), 2);
            ASSERT_EQ(resp.writers().writers()[0], "client_1_key");
            ASSERT_EQ(resp.writers().writers()[1], "client_2_key");
        });

    crud.handle_request("caller_id", msg, mock_session);
}


TEST(crud, test_that_point_of_contact_add_writers_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();

    bzn::crud crud(std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>(), std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    // create database...
    database_msg msg;
    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));

    crud.handle_request("caller_id", msg, nullptr);

    // request writers...

    // should not be added to writers as this is the owner
    msg.mutable_add_writers()->add_writers("caller_id");

    msg.mutable_add_writers()->add_writers("client_1_key");
    msg.mutable_add_writers()->add_writers("client_2_key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    crud.handle_request("caller_id", msg, nullptr);

    // access test
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::access_denied));
        }));

    crud.handle_request("other_caller_id", msg, nullptr);

    // request writers...
    msg.mutable_writers();

    // only the owner should be set at this stage...
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.response_case(), database_response::kWriters);
            ASSERT_EQ(resp.writers().owner(), "caller_id");
            ASSERT_EQ(resp.writers().writers().size(), 2);
            ASSERT_EQ(resp.writers().writers()[0], "client_1_key");
            ASSERT_EQ(resp.writers().writers()[1], "client_2_key");
        }));

    crud.handle_request("caller_id", msg, nullptr);
}


TEST(crud, test_that_remove_writers_sends_proper_response)
{
    bzn::crud crud(std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>(), std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    // create database...
    database_msg msg;
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    auto mock_session = std::make_shared<bzn::Mocksession_base>();

    expect_signed_response(mock_session);

    crud.handle_request("caller_id", msg, mock_session);

    // request writers...

    msg.mutable_add_writers()->add_writers("client_1_key");
    msg.mutable_add_writers()->add_writers("client_2_key");

    expect_signed_response(mock_session);

    crud.handle_request("caller_id", msg, mock_session);

    // remove writers...
    msg.mutable_remove_writers()->add_writers("client_2_key");

    expect_signed_response(mock_session, "uuid", std::nullopt, database_response::RESPONSE_NOT_SET);

    crud.handle_request("caller_id", msg, mock_session);

    // access test
    expect_signed_response(mock_session, "uuid", std::nullopt, std::nullopt, bzn::storage_result_msg.at(bzn::storage_result::access_denied));

    crud.handle_request("other_caller_id", msg, mock_session);
}


TEST(crud, test_that_point_of_contact_remove_writers_sends_proper_response)
{
    auto mock_node = std::make_shared<bzn::Mocknode_base>();

    bzn::crud crud(std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>(), std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), mock_node);

    // create database...
    database_msg msg;
    msg.mutable_header()->set_point_of_contact("point_of_contact");
    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));
    msg.mutable_create_db();

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));

    crud.handle_request("caller_id", msg, nullptr);

    // request writers...

    msg.mutable_add_writers()->add_writers("client_1_key");
    msg.mutable_add_writers()->add_writers("client_2_key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>()));

    crud.handle_request("caller_id", msg, nullptr);

    // remove writers...
    msg.mutable_remove_writers()->add_writers("client_2_key");

    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.response_case(), database_response::RESPONSE_NOT_SET);
        }));

    crud.handle_request("caller_id", msg, nullptr);

    // access test
    EXPECT_CALL(*mock_node, send_signed_message("point_of_contact", An<std::shared_ptr<bzn_envelope>>())).WillOnce(Invoke(
        [](const auto&, auto msg)
        {
            database_response resp;

            ASSERT_TRUE(parse_env_to_db_resp(resp, msg->SerializeAsString()));
            ASSERT_EQ(resp.header().db_uuid(), "uuid");
            ASSERT_EQ(resp.error().message(), bzn::storage_result_msg.at(bzn::storage_result::access_denied));
        }));

    crud.handle_request("other_caller_id", msg, nullptr);
}


TEST(crud, test_that_key_with_expire_set_is_deleted_by_timer_callback)
{
    auto mock_subscription_manager = std::make_shared<bzn::Mocksubscription_manager_base>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    auto mock_steady_timer = std::make_unique<bzn::asio::Mocksteady_timer_base>();

    EXPECT_CALL(*mock_steady_timer, expires_from_now(_)).Times(2);

    bzn::asio::wait_handler wh;
    EXPECT_CALL(*mock_steady_timer, async_wait(_)).WillRepeatedly(Invoke(
        [&](auto handler)
        {
            wh = handler;
        }));

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::move(mock_steady_timer);
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, nullptr);

    EXPECT_CALL(*mock_subscription_manager, start());

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    bzn::uuid_t node_uuid{"node-uuid"};
    EXPECT_CALL(*mock_pbft, get_uuid()).WillOnce(ReturnRef(node_uuid));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));

    auto session = std::make_shared<bzn::Mocksession_base>();

    msg.mutable_create_db();
    expect_signed_response(session, "uuid", 123, database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    // create key with expiration...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");
    msg.mutable_create()->set_expire(1);

    EXPECT_CALL(*mock_subscription_manager, inspect_commit(_));
    expect_signed_response(session, "uuid", 123, database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    sleep(2); // force expiration

    // call background timer...
    EXPECT_CALL(*mock_pbft, handle_database_message(_,_));
    wh(boost::system::error_code());

    // key should be gone...
    msg.mutable_read()->set_key("key");

    expect_signed_response(session, "uuid", 123, database_response::kError);
    crud->handle_request("caller_id", msg, session);

    // failure does nothing...
    wh(make_error_code(boost::system::errc::timed_out));
}


TEST(crud, test_that_key_with_expiration_can_be_made_persistent)
{
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    auto mock_steady_timer = std::make_unique<bzn::asio::Mocksteady_timer_base>();

    EXPECT_CALL(*mock_steady_timer, expires_from_now(_));
    EXPECT_CALL(*mock_steady_timer, async_wait(_));

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::move(mock_steady_timer);
        }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
        std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>(), nullptr);

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));

    auto session = std::make_shared<bzn::Mocksession_base>();

    msg.mutable_create_db();
    expect_signed_response(session, "uuid", 123, database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    // create key with expiration...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("value");
    msg.mutable_create()->set_expire(123);

    expect_signed_response(session, "uuid", 123, database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    // make key persist
    msg.mutable_persist()->set_key("key");

    expect_signed_response(session, "uuid", 123, database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    // should be gone
    msg.mutable_ttl()->set_key("key");
    expect_signed_response(session, "uuid", 123, database_response::kError, bzn::storage_result_msg.at(bzn::storage_result::ttl_not_found));
    crud->handle_request("caller_id", msg, session);

    // make key persist that no longer exists
    msg.mutable_persist()->set_key("key");
    expect_signed_response(session, "uuid", 123, database_response::kError, bzn::storage_result_msg.at(bzn::storage_result::ttl_not_found));
    crud->handle_request("caller_id", msg, session);
}


TEST(crud, test_that_create_db_uses_bluzelle_key_to_validate)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
            [&]()
            {
                return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
            }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(),
                                            mock_subscription_manager, nullptr, "caller_id");

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;
    msg.mutable_create_db();

    EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
            [=](std::shared_ptr<bzn_envelope> env)
            {
                database_response resp;
                resp.ParseFromString(env->database_response());
                EXPECT_EQ(database_response::ResponseCase::RESPONSE_NOT_SET,resp.response_case());
                EXPECT_EQ("", resp.error().message());
            }));
    crud->handle_request("caller_id", msg, session);
}


TEST(crud, test_that_create_db_with_incorrect_bluzelle_key_fails_to_validate)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
            [&]()
            {
                return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
            }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, nullptr, "caller_id");

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;
    msg.mutable_create_db();

    EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
            [=](std::shared_ptr<bzn_envelope> env)
            {
                database_response resp;
                resp.ParseFromString(env->database_response());
                EXPECT_EQ(database_response::ResponseCase::kError,resp.response_case());
                EXPECT_EQ(bzn::storage_result_msg.at(bzn::storage_result::access_denied), resp.error().message());
            }));
    crud->handle_request("not_the_caller_id", msg, session);
}


TEST(crud, test_that_delete_db_uses_bluzelle_key_to_validate)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
            [&]()
            {
                return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
            }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, nullptr, "caller_id");

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    {
        database_msg msg;
        msg.mutable_header()->set_db_uuid("uuid");
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_create_db();

        EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
                [=](std::shared_ptr<bzn_envelope> env)
                {
                    database_response resp;
                    resp.ParseFromString(env->database_response());
                    EXPECT_EQ(database_response::ResponseCase::RESPONSE_NOT_SET,resp.response_case());
                    EXPECT_EQ("", resp.error().message());
                }));
        crud->handle_request("caller_id", msg, session);
    }

    {
        database_msg msg;
        msg.mutable_header()->set_db_uuid("uuid");
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_delete_db();

        EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
                [=](std::shared_ptr<bzn_envelope> env)
                {
                    database_response resp;
                    resp.ParseFromString(env->database_response());
                    EXPECT_EQ(database_response::ResponseCase::RESPONSE_NOT_SET,resp.response_case());
                    EXPECT_EQ("", resp.error().message());
                }));
        crud->handle_request("caller_id", msg, session);
    }
}


TEST(crud, test_that_delete_db_with_incorrect_bluzelle_key_fails_to_validate)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
            [&]()
            {
                return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
            }));

    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, nullptr, "caller_id");

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    {
        database_msg msg;
        msg.mutable_header()->set_db_uuid("uuid");
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_create_db();

        EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
                [=](std::shared_ptr<bzn_envelope> env)
                {
                    database_response resp;
                    resp.ParseFromString(env->database_response());
                    EXPECT_EQ(database_response::ResponseCase::RESPONSE_NOT_SET, resp.response_case());
                    EXPECT_EQ("", resp.error().message());
                }));
        crud->handle_request("caller_id", msg, session);
    }

    {
        database_msg msg;
        msg.mutable_header()->set_db_uuid("uuid");
        msg.mutable_header()->set_nonce(uint64_t(123));
        msg.mutable_delete_db();

        EXPECT_CALL(*session, send_signed_message(_)).WillOnce(Invoke(
                [=](std::shared_ptr<bzn_envelope> env)
                {
                    database_response resp;
                    resp.ParseFromString(env->database_response());
                    EXPECT_EQ(database_response::ResponseCase::kError, resp.response_case());
                    EXPECT_EQ(bzn::storage_result_msg.at(bzn::storage_result::access_denied), resp.error().message());
                }));
        crud->handle_request("NOT_caller_id", msg, session);
    }
}


TEST(crud, test_assumption_that_boost_random_mt19937_produces_the_same_values_for_a_given_seed)
{
    // This test is only to validate the assumption that mt19937 behaves the same on multiple operating systems. The
    // actual values were created on macOS Mojave with boost 1.68.0. If this test ever fails the developers need to be
    // told immediately
    const uint64_t KEY_COUNT{422};
    const uint64_t seed{2615920895};
    boost::random::uniform_int_distribution<> dist(0, KEY_COUNT - 1);
    std::vector<size_t> accepted_random_integers{308, 88, 77, 377, 58, 109, 101, 369, 14, 269};
    std::vector<size_t> actual_random_integers(10,0);
    boost::random::mt19937 mt(seed); // mt19937 chosen because it is fast
    std::for_each(actual_random_integers.begin(), actual_random_integers.end(), [&](auto& val){ val = dist(mt); });
    ASSERT_EQ(actual_random_integers, accepted_random_integers);
}


TEST(crud, test_that_create_and_updates_which_exceed_db_limit_send_proper_responses)
{
    auto mock_subscription_manager = std::make_shared<NiceMock<bzn::Mocksubscription_manager_base>>();
    auto mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    auto session = std::make_shared<bzn::Mocksession_base>();

    EXPECT_CALL(*mock_io_context, make_unique_steady_timer()).WillOnce(Invoke(
        [&]()
        {
            return std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
        }));

    auto mock_pbft = std::make_shared<bzn::Mockpbft_base>();
    auto crud = std::make_shared<bzn::crud>(mock_io_context, std::make_shared<bzn::mem_storage>(), mock_subscription_manager, nullptr);

    EXPECT_CALL(*mock_pbft, current_peers_ptr()).WillRepeatedly(Return(std::make_shared<const std::vector<bzn::peer_address_t>>()));

    crud->start(mock_pbft);

    database_msg msg;

    msg.mutable_header()->set_db_uuid("uuid");
    msg.mutable_header()->set_nonce(uint64_t(123));

    // create db
    msg.mutable_create_db();
    msg.mutable_create_db()->set_max_size(10);

    expect_signed_response(session, "uuid", uint64_t(123), database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    // create a key that exceeds db limit..
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("00000000"); // 1 too many (key+value size)

    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError,
        bzn::storage_result_msg.at(bzn::storage_result::db_full));

    crud->handle_request("caller_id", msg, session);

    // create key...
    msg.mutable_create()->set_key("key");
    msg.mutable_create()->set_value("0000000"); // exactly db limit

    expect_signed_response(session, "uuid", uint64_t(123), database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    // update but not exceed db limit...
    msg.mutable_update()->set_key("key");
    msg.mutable_update()->set_value("1111111"); // exactly db limit

    expect_signed_response(session, "uuid", uint64_t(123), database_response::RESPONSE_NOT_SET);
    crud->handle_request("caller_id", msg, session);

    // update but exceed db limit...
    msg.mutable_update()->set_key("key");
    msg.mutable_update()->set_value("11111110"); // exactly db limit

    expect_signed_response(session, "uuid", uint64_t(123), database_response::kError);
    crud->handle_request("caller_id", msg, session);
}


TEST(crud, test_random_eviction_policy_randomly_removes_a_key_value_pair)
{
    const auto make_key = [](uint16_t index){return str(boost::format("key%04d") % index);};
    const bzn::value_t TEST_VALUE{"This is a very long string to use as a test value..."};
    const u_int64_t MAX_SIZE{8096};
    const bzn::uuid_t DB_UUID{"sut_uuid"};
    const bzn::uuid_t CALLER_UUID{"caller_id"};

    std::shared_ptr<bzn::Mocksession_base> session;
    std::shared_ptr<bzn::Mocknode_base> mock_node;

    auto crud{initialize_crud(session, mock_node, CALLER_UUID)};
    remove_test_database(crud, session, mock_node, CALLER_UUID, DB_UUID);
    create_test_database(crud, session, mock_node, CALLER_UUID, DB_UUID, MAX_SIZE, database_create_db_eviction_policy_type_RANDOM);

    // We have a cache with random eviction and max size MAX_SIZE bytes, fill up the database to just under the limit
    size_t index{0};
    size_t total_used{0};
    bzn::key_t key;
    while(total_used < MAX_SIZE)
    {
        key = make_key(index);
        create_key_value(crud, session, mock_node, CALLER_UUID, DB_UUID, key, TEST_VALUE);
        total_used += key.length() + TEST_VALUE.length();
        index++;
    }

    {
        // did the keys get in there? And are we one key/pair away from overflowing the database?
        const auto [keys, size]{get_database_size(crud, session, mock_node, CALLER_UUID, DB_UUID)};
        ASSERT_EQ( uint32_t(index-1), keys);
        ASSERT_EQ( uint32_t(keys * (key.length() + TEST_VALUE.length())), size);
        ASSERT_LE(MAX_SIZE-size, key.length() + TEST_VALUE.length());
    }

    const std::set<std::string> pre_eviction_keys{get_database_keys(crud, session, mock_node, CALLER_UUID, DB_UUID)};

    // Add one more key, to push the storage over the top, this is a cache, so there will be no error, simply an
    // eviction of an existing key
    create_key_value(crud, session, mock_node, CALLER_UUID, DB_UUID, make_key(index), TEST_VALUE);

    // After 1 replacement the number of keys must be the same, and, in this case, the size of the database must be the
    // same.
    {
        const auto [keys, size]{get_database_size(crud, session, mock_node, CALLER_UUID, DB_UUID)};
        ASSERT_EQ( uint32_t(index-1), keys);
        ASSERT_EQ( keys * (key.length() + TEST_VALUE.length()), size);
    }

    // Compare the post eviction set of keys to the pre eviction set of keys, there should be a difference of one key
    std::set<std::string> post_eviction_keys{get_database_keys(crud, session, mock_node, CALLER_UUID, DB_UUID)};

    std::set<std::string> difference;
    std::set_difference(pre_eviction_keys.begin(), pre_eviction_keys.end(), post_eviction_keys.begin(), post_eviction_keys.end(), std::inserter(difference, difference.begin()));

    ASSERT_EQ(size_t(1), difference.size());

    // Clean up by removing the test database
    remove_test_database(crud, session, mock_node, DB_UUID, CALLER_UUID);
}


TEST(crud, test_random_eviction_policy_with_large_value_requiring_many_evictions)
{
    const auto make_key = [](uint16_t index){return str(boost::format("key%04d") % index);};
    const bzn::value_t TEST_VALUE{"This is a very long string to use as a test value..."};
    const bzn::value_t LARGE_TEST_VALUE{TEST_VALUE + TEST_VALUE + TEST_VALUE + TEST_VALUE + TEST_VALUE + TEST_VALUE + TEST_VALUE + TEST_VALUE};
    const u_int64_t MAX_SIZE{8096};
    const bzn::uuid_t DB_UUID{"sut_uuid"};
    const bzn::uuid_t CALLER_UUID{"caller_id"};

    std::shared_ptr<bzn::Mocksession_base> session;
    std::shared_ptr<bzn::Mocknode_base> mock_node;

    auto crud{initialize_crud(session, mock_node, CALLER_UUID)};
    remove_test_database(crud, session, mock_node, CALLER_UUID, DB_UUID);
    create_test_database(crud, session, mock_node, CALLER_UUID, DB_UUID, MAX_SIZE, database_create_db_eviction_policy_type_RANDOM);

    // We have a cache with random eviction and max size MAX_SIZE bytes, fill up the database to just under the limit
    size_t index{0};
    size_t total_used{0};
    bzn::key_t key;
    while(total_used < MAX_SIZE)
    {
        key = make_key(index);
        create_key_value(crud, session, mock_node, CALLER_UUID, DB_UUID, key, TEST_VALUE);
        total_used += key.length() + TEST_VALUE.length();
        index++;
    }

    const std::set<std::string> pre_eviction_keys{get_database_keys(crud, session, mock_node, CALLER_UUID, DB_UUID)};

    // The cache is now less than one TEST_VALUE from its max storage limit. Add a very large value to ensure that
    // enough key/value pairs are removed to make room for the new key/value pair
    create_key_value(crud, session, mock_node, CALLER_UUID, DB_UUID, make_key(index), LARGE_TEST_VALUE);

    // Compare the actual set of keys, there must be a difference of more than one key
    std::set<std::string> post_eviction_keys{get_database_keys(crud, session, mock_node, CALLER_UUID, DB_UUID)};

    {
        const auto [keys, size]{get_database_size(crud, session, mock_node, CALLER_UUID, DB_UUID)};
        ASSERT_LE(size, MAX_SIZE);
        // NOTE: The following may not always be the case, in this instance, however, since the database contained
        // key/value pairs of the same size, it works out.
        ASSERT_LE(MAX_SIZE - size, ( key.length() + LARGE_TEST_VALUE.length()));
    }

    std::set<std::string> difference;
    std::set_difference(pre_eviction_keys.begin(), pre_eviction_keys.end(), post_eviction_keys.begin(), post_eviction_keys.end(), std::inserter(difference, difference.begin()));
    ASSERT_TRUE(difference.size() > 1);
}


TEST(crud, test_random_eviction_policy_edge_case_of_value_larger_than_max)
{
    const auto make_key = [](uint16_t index){return str(boost::format("key%04d") % index);};
    const auto make_value = [](size_t size){return std::string(size,'X');};
    const uint64_t MAX_SIZE{8096};
    const bzn::value_t TOO_LARGE_TEST_VALUE{make_value(MAX_SIZE)};

    const bzn::uuid_t DB_UUID{"sut_uuid"};
    const bzn::uuid_t CALLER_UUID{"caller_id"};
    const uint64_t NONCE{123};

    std::shared_ptr<bzn::Mocksession_base> session;
    std::shared_ptr<bzn::Mocknode_base> mock_node;

    auto crud{initialize_crud(session, mock_node, CALLER_UUID)};
    remove_test_database(crud, session, mock_node, CALLER_UUID, DB_UUID);
    create_test_database(crud, session, mock_node, CALLER_UUID, DB_UUID, MAX_SIZE, database_create_db_eviction_policy_type_RANDOM);


    database_msg msg;
    msg.mutable_header()->set_point_of_contact(CALLER_UUID);
    msg.mutable_header()->set_db_uuid(DB_UUID);
    msg.mutable_header()->set_nonce(NONCE);
    msg.mutable_create()->set_key(make_key(0));
    msg.mutable_create()->set_value(make_value(MAX_SIZE));

    // In this case we expect a db_full error as the key/value pair is larger than the storage limit.
    expect_signed_response(session
            , DB_UUID
            , NONCE
            , database_response::kError
            , bzn::storage_result_msg.at(bzn::storage_result::db_full));

    EXPECT_CALL(*mock_node, send_signed_message(A<const std::string&>(),_));

    crud->handle_request(CALLER_UUID, msg, session);

    remove_test_database(crud, session, mock_node, CALLER_UUID, DB_UUID);
}
