// cppimport
#include <pybind11/pybind11.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "orderbook.hpp"
#include "main.cpp"

//defined in main.cpp
void run_kalshi_feed(std::shared_ptr<SharedOrderBook> book, std::string market_ticker, bool print_updates);

namespace py = pybind11;

class PyKalshiBook{
    public:
        explicit PyKalshiBook(std::string market_ticker) : book_(std::make_shared<SharedOrderBook>()){
            std::thread(run_kalshi_feed, book_, std::move(market_ticker), false).detach();
        }

        BookTop get_best_bid_ask() const {
            return book_->read_snapshot();
        }
    private:
        std::shared_ptr<SharedOrderBook> book_;
};

PYBIND11_MODULE(kalshi_orderbook, m){
    m.doc() = "Live kalshi order book, updated on a background C++ thread";

    // register the BookTop struct so Python can understand it
    py::class_<BookTop>(m, "BookTop")
        .def_readonly("yes_bid", &BookTop::yes_bid)
        .def_readonly("yes_ask", &BookTop::yes_ask)
        .def_readonly("no_bid", &BookTop::no_bid)
        .def_readonly("no_ask", &BookTop::no_ask)
        .def_readonly("is_synced", &BookTop::is_synced);
    
    //register OrderBook Class
    py::class_<PyKalshiBook>(m, "OrderBook")    
        .def(py::init<std::string>(), py::arg("market_ticker"))
        .def("get_best_bid_ask", &PyKalshiBook::get_best_bid_ask,
            py::call_guard<py::gil_scoped_release>());
}

/*
<%
setup_pybind11(cfg)
cfg['compiler_args'] = ['-std=c++17', '-O2', '-march=native', '-Wall', '-Wextra', '-DKALSHI_PYBIND_BUILD']
cfg['libraries'] = ['boost_system', 'boost_json', 'ssl', 'crypto']
%>
*/