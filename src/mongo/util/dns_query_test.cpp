/**
 *    Copyright (C) 2017 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */
#include "mongo/util/dns_query.h"

#include "mongo/unittest/unittest.h"

using namespace std::literals::string_literals;

namespace {
std::string getFirstARecord(const std::string& service) {
    auto res = mongo::dns::lookupARecords(service);
    if (res.empty())
        return "";
    return res.front();
}

TEST(MongoDnsQuery, basic) {
    // We only require 50% of the records to pass, because it is possible that some large scale
    // outages could cause some of these records to fail.
    const double kPassingPercentage = 0.50;
    std::size_t resolution_count = 0;
    const struct {
        std::string dns;
        std::string ip;
    } tests[] =
        // The reason for a vast number of tests over basic DNS query calls is to provide a
        // redundancy in testing.  We'd like to make sure that this test always passes.  Lazy
        // maintanance will cause some references to be commented out.  Our belief is that all 13
        // root servers and both of Google's public servers will all be unresolvable (when
        // connections are available) only when a major problem occurs.  This test only fails if
        // more than half of the resolved names fail.
        {
            // These can be kept up to date by checking the root-servers.org listings.  Note that
            // root name servers are located in the `root-servers.net.` domain, NOT in the
            // `root-servers.org.` domain.  The `.org` domain is for webpages with statistics on
            // these servers.  The `.net` domain is the domain with the canonical addresses for
            // these servers.
            {"a.root-servers.net.", "198.41.0.4"},
            {"b.root-servers.net.", "192.228.79.201"},
            {"c.root-servers.net.", "192.33.4.12"},
            {"d.root-servers.net.", "199.7.91.13"},
            {"e.root-servers.net.", "192.203.230.10"},
            {"f.root-servers.net.", "192.5.5.241"},
            {"g.root-servers.net.", "192.112.36.4"},
            {"h.root-servers.net.", "198.97.190.53"},
            {"i.root-servers.net.", "192.36.148.17"},
            {"j.root-servers.net.", "192.58.128.30"},
            {"k.root-servers.net.", "193.0.14.129"},
            {"l.root-servers.net.", "199.7.83.42"},
            {"m.root-servers.net.", "202.12.27.33"},

            // These can be kept up to date by checking with Google's public dns service.
            {"google-public-dns-a.google.com.", "8.8.8.8"},
            {"google-public-dns-b.google.com.", "8.8.4.4"},
        };
    for (const auto& test : tests) {
        try {
            const auto witness = getFirstARecord(test.dns);
            std::cout << "Resolved " << test.dns << " to: " << witness << std::endl;

            const bool resolution = (witness == test.ip);
            if (!resolution)
                std::cerr << "Warning: Did not correctly resolve " << test.dns << std::endl;
            resolution_count += resolution;
        }
        // Failure to resolve is okay, but not great -- print a warning
        catch (const mongo::DBException& ex) {
            std::cerr << "Warning: Did not resolve " << test.dns << " at all: " << ex.what()
                      << std::endl;
        }
    }

    // As long as enough tests pass, we're okay -- this means that a single DNS name server drift
    // won't cause a BF -- when enough fail, then we can rebuild the list in one pass.
    const std::size_t kPassingRate = sizeof(tests) / sizeof(tests[0]) * kPassingPercentage;
    ASSERT_GTE(resolution_count, kPassingRate);
}

TEST(MongoDnsQuery, srvRecords) {
    const auto kMongodbSRVPrefix = "_mongodb._tcp."s;
    const struct {
        std::string query;
        std::vector<mongo::dns::SRVHostEntry> result;
    } tests[] = {
        {"test1.test.build.10gen.cc.",
         {
             {"localhost.build.10gen.cc.", 27017}, {"localhost.build.10gen.cc.", 27018},
         }},
        {"test2.test.build.10gen.cc.",
         {
             {"localhost.build.10gen.cc.", 27018}, {"localhost.build.10gen.cc.", 27019},
         }},
        {"test3.test.build.10gen.cc.",
         {
             {"localhost.build.10gen.cc.", 27017},
         }},

        // Test case 4 does not exist in the expected DNS records.
        {"test4.test.build.10gen.cc.", {}},

        {"test5.test.build.10gen.cc.",
         {
             {"localhost.build.10gen.cc.", 27017},
         }},
        {"test6.test.build.10gen.cc.",
         {
             {"localhost.build.10gen.cc.", 27017},
         }},
    };
    for (const auto& test : tests) {
        const auto& expected = test.result;
        if (expected.empty()) {
            ASSERT_THROWS_CODE(mongo::dns::lookupSRVRecords(kMongodbSRVPrefix + test.query),
                               mongo::DBException,
                               mongo::ErrorCodes::DNSHostNotFound);
            continue;
        }

        auto witness = mongo::dns::lookupSRVRecords(kMongodbSRVPrefix + test.query);
        std::sort(begin(witness), end(witness));

        for (const auto& entry : witness) {
            std::cout << "Entry: " << entry << std::endl;
        }

        for (std::size_t i = 0; i < witness.size() && i < expected.size(); ++i) {
            std::cout << "Expected: " << expected.at(i) << std::endl;
            std::cout << "Witness:  " << witness.at(i) << std::endl;
            ASSERT_EQ(witness.at(i), expected.at(i));
        }

        ASSERT_TRUE(std::equal(begin(witness), end(witness), begin(expected), end(expected)));
        ASSERT_TRUE(witness.size() == expected.size());
    }
}

TEST(MongoDnsQuery, txtRecords) {
    const struct {
        std::string query;
        std::vector<std::string> result;
    } tests[] = {
        // Test case 4 does not exist in the expected DNS records.
        {"test4.test.build.10gen.cc.", {}},

        {"test5.test.build.10gen.cc",
         {
             "connectTimeoutMS=300000&socketTimeoutMS=300000",
         }},
        {"test6.test.build.10gen.cc",
         {
             "connectTimeoutMS=200000", "socketTimeoutMS=200000",
         }},
    };

    for (const auto& test : tests) {
        try {
            auto witness = mongo::dns::getTXTRecords(test.query);
            std::sort(begin(witness), end(witness));

            const auto& expected = test.result;

            ASSERT_TRUE(std::equal(begin(witness), end(witness), begin(expected), end(expected)));
            ASSERT_EQ(witness.size(), expected.size());
        } catch (const mongo::DBException& ex) {
            if (ex.code() != mongo::ErrorCodes::DNSHostNotFound)
                throw;
            ASSERT_TRUE(test.result.empty());
        }
    }
}
}  // namespace
