#include "doctest.h"

#include "../orderbook.hpp"

TEST_CASE("orderbook basics"){
    SUBCASE("price and size parsing"){
        CHECK(parse_price("1.2345") == 12345);
        CHECK(parse_price("1.23456") == 12345);
        CHECK(parse_size("2.5") == 250);
        CHECK(parse_size(".5") == 50);
        CHECK(parse_price("-0.2500") == -2500);
    }

    SUBCASE("snapshot sets top levels"){
        OrderBook book;
        book.apply_snapshot(
            {{1000, 10}, {1200, 5}},
            {{800, 7}},
            42
        );

        const auto top = book.get_snapshot();
        CHECK(book.is_synced());
        CHECK(book.last_seq() == 42);
        CHECK(top.yes_bid == 1200);
        CHECK(top.no_bid == 800);
        CHECK(top.yes_ask == 9200);
        CHECK(top.no_ask == 8800);
    }

    SUBCASE("delta updates and gap detection"){
        OrderBook book;
        book.apply_snapshot({{1000, 10}}, {}, 7);

        book.apply_delta({Side::Yes, 1000, -4, 8});
        CHECK(book.get_snapshot().yes_bid == 1000);

        book.apply_delta({Side::Yes, 1000, -6, 9});
        CHECK(book.get_snapshot().yes_bid == -1);

        book.apply_delta({Side::Yes, 1100, 3, 11});
        CHECK_FALSE(book.is_synced());
        CHECK(book._gap_count() == 1);
        CHECK(book.last_seq() == 11);
    }
}