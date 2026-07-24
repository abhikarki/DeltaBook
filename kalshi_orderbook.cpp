// cppimport
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "orderbook.hpp"
#include "main.cpp"

//defined in main.cpp
void run_kalshi_feed(std::shared_ptr<MultiOrderBook> books, FeedConfig feed_config);

namespace py = pybind11;

// Opaque vector registration to prevent pybind11 from copying std::vector into a native Python list
PYBIND11_MAKE_OPAQUE(std::vector<PriceLevel>);

class PyKalshiBook{
    public:
        explicit PyKalshiBook(std::vector<std::string> market_tickers) : books_(std::make_shared<MultiOrderBook>(market_tickers)){
            if(market_tickers.empty()){
                throw std::invalid_argument("OrderBook requires at least one market_ticker");
            }
            std::thread(run_kalshi_feed, books_, FeedConfig{std::move(market_tickers), false}).detach();
        }

        BookTop get_best_bid_ask(const std::string& market_ticker) const {
            size_t index = books_->index_for(market_ticker);
            if(index == MultiOrderBook::kInvalidIndex){
                throw std::invalid_argument("market_ticker not tracked : " + market_ticker);
            }
            return books_->read_snapshot(index);
        }

        FullBookLevels get_full_levels(const std::string& market_ticker) const{
            size_t index = books_->index_for(market_ticker);
            if(index == MultiOrderBook::kInvalidIndex){
                throw std::invalid_argument("market_ticker not tracked: " + market_ticker);
            }
            return books_->read_full_levels(index);
        }
    private:
        std::shared_ptr<MultiOrderBook> books_;
};

PYBIND11_MODULE(kalshi_orderbook, m){
    m.doc() = "Live kalshi order book, updated on a background C++ thread";

    //Bind PriceLevel
    py::class_<PriceLevel>(m, "PriceLevel")
        .def_readonly("price_ticks", &PriceLevel::price_ticks)
        .def_readonly("size", &PriceLevel::size);
    
    //opaque vector wrapper for zero-copy list interface in Python
    py::bind_vector<std::vector<PriceLevel>>(m, "PriceLevelVector");

    // register FullBookLevels 
    py::class_<FullBookLevels>(m, "FullBookLevels")
        .def_readonly("yes_levels", &FullBookLevels::yes_levels)
        .def_readonly("no_levels", &FullBookLevels::no_levels);

    // register the BookTop struct so Python can understand it
    py::class_<BookTop>(m, "BookTop")
        .def_readonly("yes_bid", &BookTop::yes_bid)
        .def_readonly("yes_ask", &BookTop::yes_ask)
        .def_readonly("no_bid", &BookTop::no_bid)
        .def_readonly("no_ask", &BookTop::no_ask)
        .def_readonly("is_synced", &BookTop::is_synced);
    
    //register OrderBook Class
    py::class_<PyKalshiBook>(m, "OrderBook")    
        .def(py::init<std::vector<std::string>>(), py::arg("market_tickers"))
        .def("get_best_bid_ask", &PyKalshiBook::get_best_bid_ask,
            py::call_guard<py::gil_scoped_release>())
        .def("get_full_levels", &PyKalshiBook::get_full_levels,
            py::call_guard<py::gil_scoped_release>());
}

/*
<%
setup_pybind11(cfg)
cfg['compiler_args'] = ['-std=c++17', '-O2', '-march=native', '-Wall', '-Wextra', '-DKALSHI_PYBIND_BUILD']
cfg['libraries'] = ['boost_system', 'boost_json', 'ssl', 'crypto', 'simdjson']
%>
*/